import pytest

from app.services import portal_changes as bus


def test_device_and_file_keys_cannot_cross_tenants_or_id_namespaces():
    with (
        bus.subscribe(7, 12, 32) as owner,
        bus.subscribe(9, 12, 32) as other,
        bus.subscribe(7, 32) as other_device,
        bus.subscribe(7, None) as inventory,
    ):
        bus.dispatch({"v": 1, "org": 7, "terminal": 32, "kinds": ["files"]})
        assert owner.pending == {"files"}
        assert not other.pending and not other_device.pending and not inventory.pending
        bus.dispatch({"v": 1, "org": 7, "device": 12, "kinds": ["quality", "presence"]})
        assert owner.pending == {"files", "quality", "presence"}
        assert inventory.pending == {"presence"}
        assert not other.pending and not other_device.pending


def test_invalid_events_and_duplicates_do_not_grow_subscriber_queue():
    with bus.subscribe(3, 5) as watch:
        for payload in (
            {"org": 3, "device": 5, "kinds": [{}]},
            {"org": True, "device": 5, "kinds": ["files"]},
            {"org": 3, "device": 5, "terminal": 8, "kinds": ["quality"]},
        ):
            bus.dispatch({"v": 1, **payload})
        assert not watch.pending
        for _ in range(20):
            bus.dispatch({"v": 1, "org": 3, "device": 5, "kinds": ["quality"]})
        assert watch.pending == {"quality"}


def test_quality_requires_recent_owner_signature_bound_to_org_and_device(monkeypatch):
    import hmac
    import json
    import secrets
    import time

    from app.services.iot_client import iot_client

    key = secrets.token_hex(32)
    monkeypatch.setattr(iot_client, "service_token", key)
    projection = {
        "stream": "epoch",
        "at": int(time.time() * 1000),
        "value": {"available": True},
    }
    event = {
        "v": 1,
        "org": 7,
        "device": 12,
        "kinds": ["quality"],
        "quality": projection,
    }

    def sign():
        signed = json.dumps(
            ["portal-quality-v1", 7, 12, projection],
            sort_keys=True,
            separators=(",", ":"),
        ).encode()
        return hmac.digest(key.encode(), signed, "sha256").hex()

    with bus.subscribe(7, 12) as owner, bus.subscribe(8, 12) as other:
        bus.dispatch(event)
        assert owner.quality is None
        event["signature"] = sign()
        bus.dispatch({**event, "org": 8})
        assert other.quality is None
        bus.dispatch(event)
        assert owner.quality == projection
        owner.quality = None
        projection["at"] -= 30000
        event["signature"] = sign()
        bus.dispatch(event)
        assert owner.quality is None


@pytest.mark.anyio
@pytest.mark.parametrize("anyio_backend", ["asyncio"])
async def test_monitoring_views_share_sample_until_last_viewer_leaves(monkeypatch, anyio_backend):
    import asyncio
    from unittest.mock import AsyncMock

    from app.services import portal_views

    sample = AsyncMock(return_value={"items": [], "total": 0})
    monkeypatch.setattr(portal_views, "get_monitoring", sample)
    user = {"org_id": 12, "role_id": 2}
    topic = {"channel": "monitoring", "page": 1, "size": 10}
    delivered = [asyncio.Event(), asyncio.Event()]
    sockets = [AsyncMock(), AsyncMock()]
    for socket, event in zip(sockets, delivered, strict=True):
        socket.send_json.side_effect = lambda _, event=event: event.set()
    tasks = [asyncio.create_task(portal_views.view(s, user, topic)) for s in sockets]
    try:
        async with asyncio.timeout(2):
            await asyncio.gather(*(event.wait() for event in delivered))
        sample.assert_awaited_once()
        assert len(portal_views.groups) == 1
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
    assert not portal_views.groups
