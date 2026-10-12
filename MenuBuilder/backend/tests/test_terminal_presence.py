import asyncio
from unittest.mock import AsyncMock

import pytest

from app.services import terminal_presence as presence


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_presence_is_tenant_scoped_batched_and_single_flight(monkeypatch):
    presence._cache.clear()
    presence._pending.clear()

    async def statuses(ids, org_id):
        await asyncio.sleep(0)
        return [
            {
                "device_id": identifier,
                "org_id": org_id,
                "sn": f"fixture{identifier}",
                "is_provisioned": True,
                "is_online": True,
            }
            for identifier in ids
        ]

    provider = AsyncMock(side_effect=statuses)
    monkeypatch.setattr(presence.iot_client, "get_terminals_status", provider)
    identifiers = list(range(1, 1201))
    first, second = await asyncio.gather(
        presence.presence_snapshot(1, identifiers),
        presence.presence_snapshot(1, identifiers),
    )
    assert first == second and len(first) == 1200
    assert provider.await_count == 3
    assert max(len(call.args[0]) for call in provider.await_args_list) == 500
    await presence.presence_snapshot(1, identifiers)
    assert provider.await_count == 3
    await presence.presence_snapshot(2, identifiers[:1])
    assert provider.await_count == 4 and provider.await_args.kwargs["org_id"] == 2


@pytest.mark.anyio
async def test_foreign_and_unprovisioned_statuses_stay_unknown(monkeypatch):
    presence._cache.clear()
    provider = AsyncMock(
        return_value=[
            {"device_id": 7, "org_id": 2, "is_provisioned": True, "is_online": True},
            {"device_id": 8, "org_id": 1, "is_provisioned": False, "is_online": False},
        ]
    )
    monkeypatch.setattr(presence.iot_client, "get_terminals_status", provider)
    assert await presence.presence_snapshot(1, [7, 8]) == {}
