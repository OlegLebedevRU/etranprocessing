"""Bounded tenant presence snapshots; never reads event75 history."""

import asyncio
import time
from collections import OrderedDict
from typing import Any

from fastapi import HTTPException

from app.services.iot_client import iot_client

_cache: OrderedDict[
    tuple[int, tuple[int, ...]], tuple[float, dict[int, dict[str, Any]]]
] = OrderedDict()
_pending: dict[tuple[int, tuple[int, ...]], asyncio.Task] = {}
_limit = asyncio.Semaphore(4)


async def presence_snapshot(
    tenant: int, device_ids: list[int]
) -> dict[int, dict[str, Any]]:
    key = tenant, tuple(sorted(set(device_ids)))
    cached = _cache.get(key)
    if cached is not None and time.monotonic() - cached[0] < 30:
        _cache.move_to_end(key)
        return cached[1]

    async def fetch() -> dict[int, dict[str, Any]]:
        result = {}
        async with _limit:
            for offset in range(0, len(key[1]), 500):
                values = await iot_client.get_terminals_status(
                    list(key[1][offset : offset + 500]), org_id=tenant
                )
                for item in values:
                    identifier = item.get("device_id")
                    if (
                        identifier in key[1]
                        and item.get("org_id") == tenant
                        and item.get("is_provisioned") is True
                    ):
                        result[identifier] = item
        _cache[key] = time.monotonic(), result
        _cache.move_to_end(key)
        while len(_cache) > 64:
            _cache.popitem(last=False)
        return result

    task = _pending.get(key)
    if task is None:
        if len(_pending) >= 64:
            raise HTTPException(503, "presence_capacity_exceeded")
        task = asyncio.create_task(fetch())
        _pending[key] = task

        def finished(done: asyncio.Task) -> None:
            _pending.pop(key, None)
            if not done.cancelled():
                done.exception()  # Retrieve failures even when all shielded callers left.

        task.add_done_callback(finished)
    return await asyncio.shield(task)
