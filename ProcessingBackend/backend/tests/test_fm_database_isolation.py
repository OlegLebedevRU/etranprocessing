"""FM resource isolation and discovery sampling without a live database."""

from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from sqlalchemy.exc import OperationalError
from sqlalchemy.exc import TimeoutError as PoolTimeoutError
from sqlalchemy.pool import AsyncAdaptedQueuePool
from sqlalchemy.util.concurrency import greenlet_spawn
from starlette.requests import Request

from app import file_manager_database as fm_db
from app.database import engine as payment_engine
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.routers import file_manager as fm
from app.services import cert_discovery as discovery
from app.services.file_manager_readiness import FM_REQUIRED_CAPABILITIES


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_full_fm_pool_does_not_exhaust_payment_pool(monkeypatch):
    pool = fm_db.engine.pool
    # Exercise the configured queues with DBAPI doubles, without asyncpg's
    # connection-initialization listeners (which require a real PostgreSQL).
    monkeypatch.setattr(pool, "dispatch", AsyncAdaptedQueuePool(MagicMock).dispatch)
    monkeypatch.setattr(
        payment_engine.pool, "dispatch", AsyncAdaptedQueuePool(MagicMock).dispatch
    )
    monkeypatch.setattr(pool, "_creator", MagicMock)
    monkeypatch.setattr(payment_engine.pool, "_creator", MagicMock)
    monkeypatch.setattr(pool, "_timeout", 0.01)
    held = []
    payment = None
    try:
        for _ in range(pool.size()):
            held.append(await greenlet_spawn(pool.connect))
        with pytest.raises(PoolTimeoutError):
            await greenlet_spawn(pool.connect)
        payment = await greenlet_spawn(payment_engine.pool.connect)
        assert payment is not None
    finally:
        for connection in held:
            await greenlet_spawn(connection.close)
        if payment is not None:
            await greenlet_spawn(payment.close)
        await fm_db.engine.dispose()
        await payment_engine.dispose()


@pytest.mark.anyio
@pytest.mark.parametrize("during_handler", [False, True])
@pytest.mark.parametrize(
    "error",
    [
        PoolTimeoutError(),
        TimeoutError(),
        ConnectionResetError(),
        OperationalError("fixture", {}, Exception("fixture")),
    ],
)
async def test_database_failure_is_503_and_session_is_closed(
    monkeypatch, during_handler, error
):
    session = AsyncMock()
    context = MagicMock()
    context.__aenter__ = AsyncMock(return_value=session)
    context.__aexit__ = AsyncMock(return_value=False)
    factory = MagicMock(return_value=context)
    monkeypatch.setattr(fm_db, "async_session", factory)
    dependency = fm_db.get_fm_db(Request({"type": "http", "headers": []}))
    if not during_handler:
        session.connection.side_effect = error
    with pytest.raises(HTTPException) as failure:
        if during_handler:
            await anext(dependency)
            await dependency.athrow(error)
        else:
            await anext(dependency)
    assert failure.value.status_code == 503
    assert failure.value.detail == {"code": "fm_database_unavailable"}
    assert failure.value.headers["Retry-After"] == "5"
    context.__aexit__.assert_awaited_once()
    assert factory.call_count == 1


@pytest.fixture
def samples():
    discovery._fm_samples.clear()
    yield
    discovery._fm_samples.clear()


def observation(**changes):
    return {
        "sn": "fixture",
        "cert_serial": "AB",
        "cert_dn": "CN=fixture",
        "terminal_id": 1,
        "db_cert_serial": "AB",
        "is_valid": True,
        "validation_status": "authenticated",
        "endpoint": "/api/file-manager/v1/agent/hello",
        **changes,
    }


def discovery_db():
    db = AsyncMock()
    db.execute.return_value.scalar_one_or_none = MagicMock(
        return_value=SimpleNamespace(request_count=1)
    )
    return db


@pytest.mark.anyio
async def test_successful_hello_discovery_is_sampled_but_expires(samples, monkeypatch):
    clock = [1000.0]
    monkeypatch.setattr(discovery, "monotonic", lambda: clock[0])
    db = discovery_db()
    await discovery.record_terminal_discovery(db, **observation())
    await discovery.record_terminal_discovery(db, **observation())
    assert db.commit.await_count == db.execute.await_count == 1
    clock[0] += 300
    await discovery.record_terminal_discovery(db, **observation())
    assert db.commit.await_count == 2


@pytest.mark.anyio
async def test_failures_identity_changes_and_other_endpoints_are_not_sampled(samples):
    db = discovery_db()
    await discovery.record_terminal_discovery(db, **observation())
    for _ in range(2):
        await discovery.record_terminal_discovery(
            db, **observation(is_valid=False, validation_status="serial_mismatch")
        )
    await discovery.record_terminal_discovery(db, **observation())
    await discovery.record_terminal_discovery(db, **observation(cert_serial="AC"))
    for _ in range(2):
        await discovery.record_terminal_discovery(
            db, **observation(endpoint="/api/payment/etran.ashx")
        )
    assert db.commit.await_count == 7


@pytest.mark.anyio
async def test_failed_sample_is_not_cached_and_fm_database_error_propagates(samples):
    db = discovery_db()
    db.commit.side_effect = PoolTimeoutError()
    with pytest.raises(PoolTimeoutError):
        await discovery.record_terminal_discovery(db, **observation())
    db.rollback.assert_awaited_once()
    assert not discovery._fm_samples


@pytest.mark.anyio
async def test_sampling_memory_is_bounded(samples, monkeypatch):
    monkeypatch.setattr(discovery, "FM_DISCOVERY_SAMPLE_LIMIT", 2)
    for serial in ("AB", "AC", "AD"):
        await discovery.record_terminal_discovery(
            discovery_db(), **observation(cert_serial=serial)
        )
    assert len(discovery._fm_samples) == 2
    assert all(key[1] != "AB" for key in discovery._fm_samples)


@pytest.mark.anyio
@pytest.mark.parametrize(
    "state",
    ["ready", "disabled", "serial_mismatch", "missing_headers", "instance_mismatch"],
)
async def test_hello_retains_real_auth_and_uses_only_fm_dependency(monkeypatch, state):
    database = AsyncMock()
    terminal = Terminal(
        id=1,
        device_id=10,
        org_id=7,
        sn="fixture",
        cert_serial="AB",
        is_active=state != "disabled",
    )
    result = MagicMock()
    result.scalar_one_or_none.return_value = (
        None if state == "serial_mismatch" else terminal
    )
    database.execute.return_value = result
    database.scalar.return_value = terminal
    instance = uuid4()
    previous = app.dependency_overrides.copy()

    async def override_fm():
        yield database

    async def forbidden_payment():
        raise AssertionError("FM attempted to acquire the payment pool")

    app.dependency_overrides[fm_db.get_fm_db] = override_fm
    app.dependency_overrides[get_db] = forbidden_payment
    monkeypatch.setattr(
        fm, "verified_terminal_identity", lambda request: ("fixture", "AB")
    )
    monkeypatch.setattr("app.dependencies.record_terminal_discovery", AsyncMock())
    monkeypatch.setattr("app.dependencies.observed_recipient", lambda *args: None)
    headers = {
        "X-Client-Cert-DN": "CN=fixture",
        "X-Client-Cert-Serial": "AB",
        "X-Client-Cert-Issuer-DN": "CN=iot.leo4.ru",
        "X-FM-Agent-Instance-Id": str(instance),
    }
    if state == "missing_headers":
        headers.pop("X-Client-Cert-DN")
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            response = await client.post(
                "/api/file-manager/v1/agent/hello",
                headers=headers,
                json={
                    "agent_instance_id": str(
                        uuid4() if state == "instance_mismatch" else instance
                    ),
                    "agent_version": "1.0",
                    "protocol_version": 2,
                    "capabilities": sorted(FM_REQUIRED_CAPABILITIES),
                    "filesystem_ready": True,
                },
            )
        assert (
            response.status_code
            == {
                "ready": 200,
                "disabled": 403,
                "serial_mismatch": 401,
                "missing_headers": 401,
                "instance_mismatch": 403,
            }[state]
        )
        if state == "ready":
            body = response.json()
            assert body["available"]
            from datetime import datetime

            assert (
                datetime.fromisoformat(body["valid_until"])
                - datetime.fromisoformat(body["last_seen_at"])
            ).total_seconds() == 45
            database.commit.assert_awaited_once()
        else:
            database.commit.assert_not_awaited()
    finally:
        app.dependency_overrides.clear()
        app.dependency_overrides.update(previous)
