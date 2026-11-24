"""Owner notifications and shared on-demand monitoring samples for portal tabs."""

import asyncio
import logging
from contextlib import suppress
from functools import partial
from typing import cast

from asyncpg import InterfaceError, PostgresError
from fastapi import HTTPException, Request, WebSocket

from app import database
from app.auth import require_tenant_context, resolve_org_id
from app.routers.admin_device_events import count_events, require_superuser
from app.routers.monitoring import get_monitoring
from app.routers.subscriptions import tenant
from app.security.permissions import PERMISSION_MONITORING_VIEW, require_permission
from app.services.product_scope import authorize_subscription_ui

log = logging.getLogger(__name__)
groups: dict[tuple, Feed] = {}


class Feed:
    def __init__(self, key, read, interval=None):
        self.key = key
        self.read = read
        self.interval = interval
        self.wake = asyncio.Event()
        self.listeners: set[asyncio.Event] = set()
        self.value = None
        self.task = asyncio.create_task(self.run())

    async def run(self):
        while True:
            self.wake.clear()
            try:
                self.value = await self.read()
            except Exception:
                log.exception("Portal view snapshot unavailable: %s", self.key[0])
                self.value = None
            for listener in self.listeners:
                listener.set()
            with suppress(TimeoutError):
                await asyncio.wait_for(
                    self.wake.wait(), self.interval if self.value is not None else 15
                )
            if self.wake.is_set():
                await asyncio.sleep(0.15)


async def listen():
    while True:
        conn = None
        try:
            conn = await database._create_connection()
            closed = asyncio.Event()
            conn.add_termination_listener(lambda _, event=closed: event.set())

            def change(_conn, _pid, _channel, payload):
                for key, group in groups.items():
                    if (
                        key[0] == "admin-events"
                        and payload == "admin-events"
                        or key[0] == "licenses"
                        and payload == f"licenses/{key[1]}"
                    ):
                        group.wake.set()

            await conn.add_listener("portal_views_v1", change)
            for key, group in groups.items():
                if key[0] != "monitoring":
                    group.wake.set()
            await closed.wait()
        except (OSError, TimeoutError, InterfaceError, PostgresError) as exc:
            log.warning("Portal view listener reconnecting: %s", type(exc).__name__)
        finally:
            if conn is not None:
                with suppress(OSError, TimeoutError):
                    await conn.close(timeout=3)
        await asyncio.sleep(5)


async def read_admin_count():
    async with database.async_session() as db:
        return await count_events(db)


async def license_changed():
    return {"changed": True}


listener: asyncio.Task | None = None


async def view(child: WebSocket, user: dict, message: dict) -> None:
    global listener
    kind = message["channel"]
    interval = None
    try:
        if kind == "mqtt-admin":
            from app.routers.admin_mqtt import observations

            await require_superuser(user)
            key = (kind,)
            interval = 5
            read = observations
        elif kind == "admin-events":
            await require_superuser(user)
            key = (kind,)

            read = read_admin_count
        elif kind == "licenses":
            org = tenant(await require_tenant_context(user))
            async with database.async_session() as db:
                await authorize_subscription_ui(db, user, org)
            key = (kind, org)

            read = license_changed
        else:
            await require_permission(PERMISSION_MONITORING_VIEW)(
                cast(Request, child), user
            )
            page, size, search = (
                message.get("page"),
                message.get("size"),
                message.get("search", ""),
            )
            if (
                type(page) is not int
                or not 1 <= page <= 2147483647
                or type(size) is not int
                or not 1 <= size <= 100
                or not isinstance(search, str)
                or len(search) > 512
            ):
                await child.close(code=4400)
                return
            key = (kind, resolve_org_id(user), page, size, search)
            interval = 60

            read = partial(get_monitoring, page, size, search, user)
    except HTTPException:
        await child.close(code=4403)
        return

    await child.accept()
    group = groups.get(key)
    if group is None:
        group = groups[key] = Feed(key, read, interval)
    if kind in ("licenses", "admin-events") and listener is None:
        listener = asyncio.create_task(listen())
    ready = asyncio.Event()
    group.listeners.add(ready)
    if group.value is not None:
        ready.set()
    try:
        while True:
            await ready.wait()
            ready.clear()
            if group.value is None:
                await child.close(code=1013)
                return
            await child.send_json(group.value)
    finally:
        group.listeners.discard(ready)
        if not group.listeners:
            groups.pop(key, None)
            group.task.cancel()
            tasks = [group.task]
            if listener is not None and all(
                k[0] not in ("licenses", "admin-events") for k in groups
            ):
                listener.cancel()
                tasks.append(listener)
                listener = None
            await asyncio.gather(*tasks, return_exceptions=True)
