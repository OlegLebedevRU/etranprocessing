"""Multiplex only explicitly registered portal channels over one authenticated socket."""

import asyncio
import json
import logging
from collections.abc import Awaitable, Callable
from contextlib import suppress
from typing import Any

from fastapi import WebSocket, WebSocketDisconnect
from starlette.types import Message

logger = logging.getLogger(__name__)
Handler = Callable[[WebSocket], Awaitable[Any]]


async def serve(
    socket: WebSocket, resolve: Callable[[dict], Handler | None], lifetime: float
):
    channels: dict[int, tuple[asyncio.Queue[Message], asyncio.Task, bool]] = {}
    lock = asyncio.Lock()

    async def emit(value):
        async with lock, asyncio.timeout(5):
            await socket.send_json(value)

    async def relay(key, inbox, handler):
        closed = False

        async def send(message: Message):
            nonlocal closed
            kind = message["type"]
            if kind == "websocket.accept":
                await emit({"id": key, "op": "open"})
            elif kind == "websocket.send":
                await emit({"id": key, "op": "data", "data": message["text"]})
            elif kind == "websocket.close" and not closed:
                closed = True
                await emit(
                    {"id": key, "op": "close", "code": message.get("code", 1000)}
                )

        try:
            child = WebSocket(dict(socket.scope), inbox.get, send)
            await handler(child)
        except WebSocketDisconnect, OSError, TimeoutError:
            pass
        except Exception:
            logger.exception("Portal logical channel failed")
        finally:
            if not closed:
                with suppress(RuntimeError, OSError, TimeoutError, WebSocketDisconnect):
                    await send({"type": "websocket.close", "code": 1011})

    await socket.accept()
    try:
        async with asyncio.timeout(lifetime):
            while True:
                raw = await socket.receive_text()
                if len(raw) > 32768:
                    break
                msg = json.loads(raw)
                if not isinstance(msg, dict):
                    break
                key, op = msg.get("id"), msg.get("op")
                if type(key) is not int or not 0 < key <= 2147483647:
                    break
                entry = channels.get(key)
                if op == "open":
                    if entry or len(channels) >= 16:
                        break
                    handler = resolve(msg)
                    if not handler:
                        await emit({"id": key, "op": "close", "code": 4403})
                        continue
                    inbox: asyncio.Queue[Message] = asyncio.Queue(64)
                    inbox.put_nowait({"type": "websocket.connect"})
                    task = asyncio.create_task(relay(key, inbox, handler))
                    channels[key] = (inbox, task, msg.get("channel") == "control")
                    task.add_done_callback(lambda _, key=key: channels.pop(key, None))
                elif entry and op == "data" and isinstance(msg.get("data"), str):
                    entry[0].put_nowait(
                        {"type": "websocket.receive", "text": msg["data"]}
                    )
                elif entry and op == "close":
                    entry[0].put_nowait({"type": "websocket.disconnect", "code": 1000})
                    # A subscription can be waiting only on broker events.
                    if not entry[2]:
                        entry[1].cancel()
                elif op not in {"data", "close"}:
                    break
    except TimeoutError, ValueError, WebSocketDisconnect, asyncio.QueueFull, OSError:
        pass
    finally:
        tasks = [entry[1] for entry in channels.values()]
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        with suppress(RuntimeError, OSError, WebSocketDisconnect):
            await socket.close(code=1001)
