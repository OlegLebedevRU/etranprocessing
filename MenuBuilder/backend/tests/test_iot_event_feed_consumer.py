from __future__ import annotations

import json
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import httpx
import pytest

from app.config import settings
from app.services.iot_consumer_storage import InMemoryIotConsumerStorage
from app.services.iot_event_consumer import IotEventConsumer
from app.services.iot_event_feed_client import (
    IotEventFeedAuthError,
    IotEventFeedClient,
    IotEventFeedNotFoundError,
    IotEventFeedServerError,
    IotEventFeedTimeoutError,
    IotEventFeedValidationError,
    ReconciliationResponse,
    RemoteSessionEventItem,
)

FIXTURE_PATH = Path(__file__).parent / "fixtures" / "iot_event_feed_examples_v1.json"


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def raw_fixture() -> dict[str, Any]:
    with open(FIXTURE_PATH, encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture
def fixture_events(raw_fixture: dict[str, Any]) -> list[RemoteSessionEventItem]:
    """Return all 9 mandatory events sorted by monotonic cursor ASC."""
    events_map = raw_fixture["events"]
    items = [RemoteSessionEventItem.model_validate(val) for val in events_map.values()]
    items.sort(key=lambda x: x.cursor)
    return items


# --- 1. Client Unit & Error Mapping Tests ---


@pytest.mark.anyio
async def test_client_auth_error_mapping():
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(
            403,
            json={"detail": "missing_or_invalid_internal_service_key"},
            request=request,
        )

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="bad-token",
            client=mock_http,
        )
        with pytest.raises(IotEventFeedAuthError) as exc_info:
            await client.get_event_feed(after=0)

        assert exc_info.value.status_code == 403
        assert "missing_or_invalid_internal_service_key" in str(exc_info.value.detail)


@pytest.mark.anyio
async def test_client_validation_and_not_found_errors():
    def handler(request: httpx.Request) -> httpx.Response:
        if "reconciliation" in request.url.path:
            return httpx.Response(404, json={"detail": "not_found"}, request=request)
        return httpx.Response(400, json={"detail": "negative_cursor"}, request=request)

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="token",
            client=mock_http,
        )
        with pytest.raises(IotEventFeedValidationError) as v_exc:
            await client.get_event_feed(after=-1)
        assert v_exc.value.status_code == 400

        with pytest.raises(IotEventFeedNotFoundError) as nf_exc:
            await client.get_reconciliation()
        assert nf_exc.value.status_code == 404


@pytest.mark.anyio
async def test_client_server_error_and_bounded_retry():
    attempts = 0

    def handler(request: httpx.Request) -> httpx.Response:
        nonlocal attempts
        attempts += 1
        return httpx.Response(502, text="Bad Gateway", request=request)

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="token",
            max_retries=2,
            retry_backoff_sec=0.01,
            client=mock_http,
        )
        with pytest.raises(IotEventFeedServerError) as s_exc:
            await client.get_event_feed(after=0)

        assert s_exc.value.status_code == 502
        # Initial attempt + 2 retries = 3 attempts
        assert attempts == 3


@pytest.mark.anyio
async def test_client_timeout_and_bounded_retry():
    attempts = 0

    def handler(request: httpx.Request) -> httpx.Response:
        nonlocal attempts
        attempts += 1
        raise httpx.ReadTimeout("Socket timeout simulated", request=request)

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="token",
            max_retries=2,
            retry_backoff_sec=0.01,
            client=mock_http,
        )
        with pytest.raises(IotEventFeedTimeoutError):
            await client.get_event_feed(after=0)

        assert attempts == 3


# --- 2. Consumer Contract Tests against Immutable Fixtures ---


@pytest.mark.anyio
async def test_device_online_finance_is_allowlisted_and_shadow_safe(
    fixture_events: list[RemoteSessionEventItem], monkeypatch: pytest.MonkeyPatch
):
    event = next(e for e in fixture_events if e.event_type == "device_online")
    monkeypatch.setattr(settings, "iot_consumer_finance_tenant_ids", [event.tenant_id])
    storage = InMemoryIotConsumerStorage()
    consumer = IotEventConsumer(storage=storage, consumer_id="finance_fixture")

    monkeypatch.setattr(settings, "iot_consumer_shadow_mode", True)
    await consumer.process_batch([event])
    assert storage._inbox[event.event_id]["status"] == "processed"

    active_storage = InMemoryIotConsumerStorage()
    active_consumer = IotEventConsumer(
        storage=active_storage, consumer_id="finance_active_fixture"
    )
    monkeypatch.setattr(settings, "iot_consumer_shadow_mode", False)
    await active_consumer.process_batch([event])
    assert active_storage._inbox[event.event_id]["status"] == "pending_finance"

    other_storage = InMemoryIotConsumerStorage()
    other_consumer = IotEventConsumer(
        storage=other_storage, consumer_id="finance_other_fixture"
    )
    monkeypatch.setattr(settings, "iot_consumer_finance_tenant_ids", [])
    await other_consumer.process_batch([event])
    assert other_storage._inbox[event.event_id]["status"] == "processed"


@pytest.mark.anyio
async def test_consumer_empty_feed():
    """Verify consumer handling of empty feed page."""
    storage = InMemoryIotConsumerStorage()

    def handler(request: httpx.Request) -> httpx.Response:
        page_payload = {
            "items": [],
            "next_cursor": 0,
            "has_more": False,
            "total_count": 0,
            "server_time": datetime.now(UTC).isoformat(),
        }
        return httpx.Response(200, json=page_payload, request=request)

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="token",
            client=mock_http,
        )
        consumer = IotEventConsumer(
            client=client, storage=storage, consumer_id="test_empty"
        )

        res = await consumer.poll_once()
        assert res.events_received == 0
        assert res.processed_count == 0
        assert res.duplicate_count == 0
        assert res.quarantine_count == 0
        assert res.has_more is False
        assert res.checkpoint_cursor == 0
        assert res.cursor_lag == 0

        metrics = await consumer.get_metrics()
        assert metrics["checkpoint_cursor"] == 0
        assert metrics["inbox_count"] == 0
        assert metrics["quarantine_count"] == 0


@pytest.mark.anyio
async def test_consumer_duplicate_page(fixture_events: list[RemoteSessionEventItem]):
    """Verify idempotent deduplication when the exact same page is redelivered."""
    storage = InMemoryIotConsumerStorage()

    # Take first 3 events
    first_page_events = fixture_events[:3]

    call_count = 0

    def handler(request: httpx.Request) -> httpx.Response:
        nonlocal call_count
        call_count += 1
        page_payload = {
            "items": [it.model_dump(mode="json") for it in first_page_events],
            "next_cursor": first_page_events[-1].cursor,
            "has_more": True,
            "total_count": len(fixture_events),
            "server_time": datetime.now(UTC).isoformat(),
        }
        return httpx.Response(200, json=page_payload, request=request)

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="token",
            client=mock_http,
        )
        consumer = IotEventConsumer(
            client=client, storage=storage, consumer_id="test_dup_page"
        )

        # 1. First poll processes 3 items
        res1 = await consumer.poll_once()
        assert res1.events_received == 3
        assert res1.processed_count == 3
        assert res1.duplicate_count == 0
        assert res1.checkpoint_cursor == 3

        # 2. Duplicate page delivery (same items)
        res2 = await consumer.poll_once()
        assert res2.events_received == 3
        assert res2.processed_count == 0
        assert res2.duplicate_count == 3
        assert res2.quarantine_count == 0
        assert res2.checkpoint_cursor == 3

        # Check total storage state
        counts = await storage.get_counts()
        assert counts["inbox"] == 3
        assert counts["quarantine"] == 0


@pytest.mark.anyio
async def test_consumer_duplicate_event_in_batch(
    fixture_events: list[RemoteSessionEventItem],
):
    """Verify single-event deduplication by event_id."""
    storage = InMemoryIotConsumerStorage()

    event1 = fixture_events[0]
    # Simulate batch with event1 repeated
    batch = [event1, event1]

    consumer = IotEventConsumer(storage=storage, consumer_id="test_dup_event")
    res = await consumer.process_batch(batch, feed_latest_cursor=1)

    assert res.processed_count == 1
    assert res.duplicate_count == 1
    assert res.quarantine_count == 0
    assert res.last_cursor == 1

    counts = await storage.get_counts()
    assert counts["inbox"] == 1


@pytest.mark.anyio
async def test_consumer_resume_from_checkpoint(
    fixture_events: list[RemoteSessionEventItem],
):
    """Verify that consumer resumes strictly after its saved checkpoint."""
    storage = InMemoryIotConsumerStorage()

    # Pre-populate checkpoint at cursor 3
    event3 = fixture_events[2]
    await storage.save_inbox_event(fixture_events[0])
    await storage.save_inbox_event(fixture_events[1])
    await storage.save_inbox_event(event3)
    await storage.update_checkpoint(
        "test_resume",
        last_cursor=3,
        last_event_id=event3.event_id,
        last_event_occurred_at=event3.occurred_at,
    )

    requested_after: int | None = None

    def handler(request: httpx.Request) -> httpx.Response:
        nonlocal requested_after
        requested_after = int(request.url.params.get("after", 0))
        # Return items strictly after requested cursor
        sub_items = [it for it in fixture_events if it.cursor > requested_after]
        page_payload = {
            "items": [it.model_dump(mode="json") for it in sub_items],
            "next_cursor": sub_items[-1].cursor if sub_items else requested_after,
            "has_more": False,
            "total_count": len(fixture_events),
            "server_time": datetime.now(UTC).isoformat(),
        }
        return httpx.Response(200, json=page_payload, request=request)

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="token",
            client=mock_http,
        )
        consumer = IotEventConsumer(
            client=client, storage=storage, consumer_id="test_resume"
        )

        res = await consumer.poll_once()
        assert requested_after == 3
        # Should receive events with cursor 4..9 (6 events)
        assert res.events_received == 6
        assert res.processed_count == 6
        assert res.checkpoint_cursor == 9

        counts = await storage.get_counts()
        assert counts["inbox"] == 9


@pytest.mark.anyio
async def test_consumer_pagination(fixture_events: list[RemoteSessionEventItem]):
    """Verify multi-page pagination with poll_until_caught_up."""
    storage = InMemoryIotConsumerStorage()

    def handler(request: httpx.Request) -> httpx.Response:
        after = int(request.url.params.get("after", 0))
        limit = int(request.url.params.get("limit", 100))

        remaining = [it for it in fixture_events if it.cursor > after]
        slice_items = remaining[:limit]
        next_cur = slice_items[-1].cursor if slice_items else after
        has_more = len(remaining) > len(slice_items)

        page_payload = {
            "items": [it.model_dump(mode="json") for it in slice_items],
            "next_cursor": next_cur,
            "has_more": has_more,
            "total_count": len(fixture_events),
            "server_time": datetime.now(UTC).isoformat(),
        }
        return httpx.Response(200, json=page_payload, request=request)

    transport = httpx.MockTransport(handler)
    async with httpx.AsyncClient(transport=transport) as mock_http:
        client = IotEventFeedClient(
            base_url="http://iot-mock:8000",
            service_token="token",
            client=mock_http,
        )
        consumer = IotEventConsumer(
            client=client, storage=storage, consumer_id="test_pag"
        )

        results = await consumer.poll_until_caught_up(max_pages=10, batch_size=3)
        assert len(results) == 3
        assert sum(r.processed_count for r in results) == 9

        cp = await storage.get_checkpoint("test_pag")
        assert cp.last_cursor == 9

        metrics = await consumer.get_metrics()
        assert metrics["cursor_lag"] == 0
        assert metrics["checkpoint_cursor"] == 9
        assert metrics["inbox_count"] == 9


@pytest.mark.anyio
async def test_consumer_out_of_order_rejection(
    fixture_events: list[RemoteSessionEventItem],
):
    """Verify that an event with cursor <= checkpoint without known event_id is quarantined."""
    storage = InMemoryIotConsumerStorage()

    consumer = IotEventConsumer(storage=storage, consumer_id="test_ooo")

    # 1. Process items 1 and 2
    res1 = await consumer.process_batch(fixture_events[:2], feed_latest_cursor=2)
    assert res1.last_cursor == 2

    # 2. Create out-of-order event with cursor 1 but a completely new event_id
    corrupted_event = RemoteSessionEventItem(
        cursor=1,
        event_id="evt_unknown_late_001",
        occurred_at=datetime.now(UTC),
        event_type="device_online",
        event_version="1.0.0",
        sn="SNTEST01",
        created_at=datetime.now(UTC),
        payload={},
    )

    res2 = await consumer.process_batch([corrupted_event], feed_latest_cursor=2)
    assert res2.processed_count == 0
    assert res2.quarantine_count == 1
    assert res2.last_cursor == 2  # Cursor did not regress

    counts = await storage.get_counts()
    assert counts["inbox"] == 2
    assert counts["quarantine"] == 1


@pytest.mark.anyio
async def test_consumer_commercial_field_quarantine(
    fixture_events: list[RemoteSessionEventItem],
):
    """Verify contract invariant: payloads with injected commercial fields are quarantined."""
    storage = InMemoryIotConsumerStorage()
    consumer = IotEventConsumer(storage=storage, consumer_id="test_comm_inj")

    bad_payload_event = RemoteSessionEventItem(
        cursor=1,
        event_id="evt_commercial_injected",
        occurred_at=datetime.now(UTC),
        event_type="device_online",
        event_version="1.0.0",
        sn="SNTEST01",
        created_at=datetime.now(UTC),
        payload={"rubles": 100, "channel": "app"},
    )

    res = await consumer.process_batch([bad_payload_event], feed_latest_cursor=1)
    assert res.processed_count == 0
    assert res.quarantine_count == 1
    assert res.last_cursor == 0

    counts = await storage.get_counts()
    assert counts["inbox"] == 0
    assert counts["quarantine"] == 1


@pytest.mark.anyio
async def test_consumer_restart_resilience(
    fixture_events: list[RemoteSessionEventItem],
):
    """Verify fault tolerance across worker restarts using shared storage."""
    storage = InMemoryIotConsumerStorage()

    # Worker instance 1 runs and processes first 4 items
    c1 = IotEventConsumer(storage=storage, consumer_id="shared_worker")
    res1 = await c1.process_batch(fixture_events[:4], feed_latest_cursor=4)
    assert res1.processed_count == 4
    assert res1.last_cursor == 4

    # Worker crashes / restarts -> Worker instance 2 initializes
    c2 = IotEventConsumer(storage=storage, consumer_id="shared_worker")
    cp = await storage.get_checkpoint("shared_worker")
    assert cp.last_cursor == 4

    # Worker instance 2 processes items 5..9
    res2 = await c2.process_batch(fixture_events[4:], feed_latest_cursor=9)
    assert res2.processed_count == 5
    assert res2.last_cursor == 9

    counts = await storage.get_counts()
    assert counts["inbox"] == 9


@pytest.mark.anyio
async def test_reconciliation_response_parsing(raw_fixture: dict[str, Any]):
    """Verify parsing of reconciliation contract example."""
    rec_data = raw_fixture["reconciliation_example"]
    model = ReconciliationResponse.model_validate(rec_data)

    assert model.total_events == 9
    assert model.min_cursor == 1
    assert model.max_cursor == 9
    assert model.events_by_type["device_online"] == 1
    assert model.events_by_type["remote_session_closed"] == 1
    assert (
        model.feed_sha256
        == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    )


# --- 3. Diagnostics & Status API Router Tests ---


@pytest.mark.anyio
async def test_consumer_status_api_endpoint():
    """Verify GET /api/internal/v1/iot-consumer/status authorization and schema."""
    from app.main import app
    from app.services.iot_event_consumer import iot_event_consumer

    orig_storage = iot_event_consumer.storage
    iot_event_consumer.storage = InMemoryIotConsumerStorage()
    try:
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://testserver"
        ) as ac:
            # 1. Unauthenticated -> 403
            r_unauth = await ac.get("/api/internal/v1/iot-consumer/status")
            assert r_unauth.status_code == 403

            # 2. Authenticated via superuser token
            from app.auth import create_access_token

            su_token = create_access_token(
                {"sub": "o.lebedev", "role": "superuser", "is_superuser": True}
            )
            r_auth = await ac.get(
                "/api/internal/v1/iot-consumer/status",
                headers={"Authorization": f"Bearer {su_token}"},
            )
            assert r_auth.status_code == 200
            data = r_auth.json()
            assert "consumer_id" in data
            assert "checkpoint_cursor" in data
            assert "cursor_lag" in data
            assert "enabled" in data
            assert data["enabled"] is False  # Dark consumer by default
            assert data["shadow_mode"] is True  # Shadow mode by default
    finally:
        iot_event_consumer.storage = orig_storage
