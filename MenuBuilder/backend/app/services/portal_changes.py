"""One bounded RabbitMQ subscription per BFF process; clients receive hints only."""

import asyncio
import hmac
import json
import logging
import os
import time
from contextlib import contextmanager, suppress
from uuid import uuid4

import aio_pika

log = logging.getLogger(__name__)
KINDS = frozenset(
    {
        "camera",
        "command",
        "enrollment",
        "quality",
        "stream",
        "presence",
        "files",
        "feed",
    }
)
feed_wake = asyncio.Event()
connected = False


class Watch:
    def __init__(self, org: int, device: int | None, terminal: int | None = None):
        self.terminal = terminal
        self.org = org
        self.device = device
        self.quality: dict | None = None
        self.pending: set[str] = set()
        self.event = asyncio.Event()

    def offer(self, kinds: set[str]) -> None:
        self.pending.update(kinds)
        self.event.set()

    async def take(self) -> list[str]:
        await self.event.wait()
        self.event.clear()
        pending, self.pending = self.pending, set()
        return sorted(pending)


_watches: set[Watch] = set()


@contextmanager
def subscribe(org: int, device: int | None, terminal: int | None = None):
    watch = Watch(org, device, terminal)
    _watches.add(watch)
    try:
        yield watch
    finally:
        _watches.discard(watch)


def dispatch(event) -> None:
    if not isinstance(event, dict) or event.get("v") != 1:
        return
    org, device, raw = event.get("org"), event.get("device"), event.get("kinds")
    terminal = event.get("terminal")
    if type(org) is not int or org <= 0:
        return
    if not (
        (type(device) is int and device > 0 and terminal is None)
        or (
            type(terminal) is int
            and terminal > 0
            and device is None
            and raw == ["files"]
        )
    ):
        return
    if (
        not isinstance(raw, list)
        or len(raw) > len(KINDS)
        or not all(isinstance(k, str) for k in raw)
    ):
        return
    kinds = set(raw) & KINDS
    if "feed" in kinds:
        feed_wake.set()
    projection = event.get("quality")
    valid_quality = False
    if isinstance(projection, dict) and "quality" in kinds and device is not None:
        from app.services.iot_client import iot_client

        signature = event.get("signature")
        at = projection.get("at")
        if (
            iot_client.service_token
            and isinstance(signature, str)
            and signature.isascii()
            and isinstance(at, int)
            and abs(time.time() * 1000 - at) <= 20000
            and isinstance(projection.get("stream"), str)
            and isinstance(projection.get("value"), dict)
        ):
            signed = json.dumps(
                ["portal-quality-v1", org, device, projection],
                sort_keys=True,
                separators=(",", ":"),
            ).encode()
            valid_quality = hmac.compare_digest(
                signature,
                hmac.digest(iot_client.service_token.encode(), signed, "sha256").hex(),
            )
    for watch in _watches:
        if watch.org != org:
            continue
        if kinds and (
            (device is not None and watch.device == device)
            or (terminal is not None and watch.terminal == terminal)
        ):
            if valid_quality and watch.device == device:
                watch.quality = projection
            watch.offer(kinds)
        elif watch.device is None and "presence" in kinds:
            watch.offer({"presence"})


async def wait_for_feed(seconds: float) -> None:
    if not connected:
        await asyncio.sleep(seconds)
        return
    with suppress(TimeoutError):
        await asyncio.wait_for(feed_wake.wait(), max(30, seconds))
    feed_wake.clear()


async def run() -> None:
    global connected
    url = os.environ.get("PORTAL_AMQP_URL")
    if not url:
        return
    name = "portal.watch." + uuid4().hex
    while True:
        try:
            async with await aio_pika.connect(url, timeout=5) as connection:
                channel = await connection.channel()
                exchange = await channel.get_exchange("portal.changes.v1", ensure=False)
                queue = await channel.declare_queue(
                    name,
                    exclusive=True,
                    auto_delete=True,
                    arguments={"x-max-length": 512, "x-message-ttl": 10000},
                )
                await queue.bind(exchange)

                async def receive(message):
                    if len(message.body) <= 4096:
                        with suppress(ValueError, UnicodeError):
                            dispatch(json.loads(message.body))

                await queue.consume(receive, no_ack=True)
                connected = True
                for watch in _watches:
                    watch.offer({"sync"})
                await connection.closed()
        except OSError, TimeoutError, aio_pika.exceptions.AMQPException, RuntimeError:
            log.warning("Portal AMQP unavailable; retrying")
        finally:
            connected = False
        await asyncio.sleep(5)
