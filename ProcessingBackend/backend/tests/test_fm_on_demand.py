"""Owner/expiry/generation boundaries of the demand readiness contract."""

from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock
from uuid import uuid4

import pytest
from etranprocessing_db.file_manager import FileManagerAgent, FileManagerReadinessProbe
from fastapi import HTTPException
from starlette.requests import Request

from app.models import Terminal
from app.routers import file_manager_probe as api
from app.schemas.file_manager import AgentReadiness
from app.services.file_manager_readiness import FM_REQUIRED_CAPABILITIES


@pytest.fixture
def anyio_backend():
    return "asyncio"


def fixture():
    now = datetime.now(UTC)
    actor = {
        "X-Org-Id": "7",
        "X-User-Id": "fixture",
        "X-Session-Id": "fixture:view",
        "X-Role": "user",
    }
    terminal = Terminal(
        id=1, device_id=10, org_id=7, sn="fixture", cert_serial="AB", is_active=True
    )
    probe = FileManagerReadinessProbe(
        terminal_id=1,
        request_id=uuid4(),
        tenant_id=7,
        actor=actor,
        cert_serial="AB",
        expires_at=now + timedelta(seconds=10),
    )
    agent = FileManagerAgent(terminal_id=1, agent_instance_id=uuid4())
    return terminal, probe, agent, actor


def request(actor, instance):
    return Request(
        {
            "type": "http",
            "headers": [
                (k.lower().encode(), v.encode())
                for k, v in {**actor, "X-FM-Agent-Instance-Id": str(instance)}.items()
            ],
        }
    )


def ready(probe, agent):
    probe.agent_instance_id = agent.agent_instance_id
    probe.result = {"state": "ready", "available": True}
    probe.proof_until = datetime.now(UTC) + timedelta(seconds=15)


@pytest.mark.anyio
@pytest.mark.parametrize(
    "change",
    [
        "actor",
        "serial",
        "instance",
        "tenant",
        "expired",
        "used",
        "unavailable",
        "missing",
    ],
)
async def test_proof_cannot_authorize_wrong_or_stale_session(change):
    terminal, probe, agent, actor = fixture()
    ready(probe, agent)
    db = AsyncMock()
    db.get.return_value = probe
    identifier = probe.request_id
    if change == "actor":
        probe.actor = {**actor, "X-Session-Id": "another:view"}
    if change == "serial":
        probe.cert_serial = "AC"
    if change == "instance":
        probe.agent_instance_id = uuid4()
    if change == "tenant":
        probe.tenant_id = 8
    if change == "expired":
        probe.proof_until = datetime.now(UTC) - timedelta(seconds=1)
    if change == "used":
        probe.consumed_by = uuid4()
    if change == "unavailable":
        probe.result = {"available": False}
    if change == "missing":
        identifier = None
    with pytest.raises(HTTPException) as error:
        await api.consume_probe(db, terminal, actor, identifier, uuid4(), agent)
    assert error.value.status_code == 409


@pytest.mark.anyio
async def test_proof_consume_is_idempotent_only_for_same_session():
    terminal, probe, agent, actor = fixture()
    ready(probe, agent)
    db = AsyncMock()
    db.get.return_value = probe
    session = uuid4()
    await api.consume_probe(db, terminal, actor, probe.request_id, session, agent)
    await api.consume_probe(db, terminal, actor, probe.request_id, session, agent)
    with pytest.raises(HTTPException):
        await api.consume_probe(db, terminal, actor, probe.request_id, uuid4(), agent)
    assert probe.consumed_by == session


@pytest.mark.anyio
async def test_probe_replays_exact_response_without_refreshing_proof(monkeypatch):
    terminal, probe, agent, actor = fixture()
    db = AsyncMock()
    db.scalar.return_value = terminal
    db.get.return_value = probe
    monkeypatch.setattr(api, "verified_terminal_identity", lambda r: ("fixture", "AB"))
    monkeypatch.setattr(api, "get_current_terminal", AsyncMock(return_value=terminal))
    monkeypatch.setattr(
        api,
        "deployment_readiness",
        lambda *a, **kw: AgentReadiness(
            state="ready", available=True, server_time=kw["now"]
        ),
    )
    body = api.ProbeSuccess(
        request_id=probe.request_id,
        agent_instance_id=agent.agent_instance_id,
        agent_version="fixture",
        protocol_version=2,
        capabilities=sorted(FM_REQUIRED_CAPABILITIES | {api.CAPABILITY}),
        filesystem_ready=True,
        readiness_mode="on_demand_v1",
    )
    req = request(actor, agent.agent_instance_id)
    await api.probe_result(probe.request_id, body, req, db)
    deadline = probe.proof_until
    await api.probe_result(probe.request_id, body, req, db)
    assert probe.proof_until == deadline
    db.commit.assert_awaited_once()
    with pytest.raises(HTTPException):
        await api.probe_result(
            probe.request_id,
            body.model_copy(update={"agent_version": "changed"}),
            req,
            db,
        )


@pytest.mark.anyio
@pytest.mark.parametrize(
    "change", ["expired", "serial", "disabled", "header", "late_certificate"]
)
async def test_probe_rejects_invalid_result_before_registration(monkeypatch, change):
    terminal, probe, agent, actor = fixture()
    db = AsyncMock()
    db.scalar.return_value = terminal
    db.get.return_value = probe
    monkeypatch.setattr(api, "verified_terminal_identity", lambda r: ("fixture", "AB"))
    monkeypatch.setattr(api, "get_current_terminal", AsyncMock(return_value=terminal))
    if change == "expired":
        probe.expires_at = datetime.now(UTC) - timedelta(seconds=1)
    if change == "serial":
        probe.cert_serial = "AC"
    if change == "disabled":
        terminal.is_active = False
    if change == "late_certificate":
        db.scalar.return_value = Terminal(
            id=1, org_id=7, sn="fixture", cert_serial="AC", is_active=True
        )
    body = api.ProbeFailure(
        request_id=probe.request_id,
        agent_instance_id=agent.agent_instance_id,
        error_code="fixture",
    )
    with pytest.raises(HTTPException):
        await api.probe_result(
            probe.request_id,
            body,
            request(actor, uuid4() if change == "header" else agent.agent_instance_id),
            db,
        )
    db.execute.assert_not_awaited()
    db.commit.assert_not_awaited()


@pytest.mark.anyio
async def test_create_probe_busy_owner_and_idempotent_request():
    terminal, probe, agent, actor = fixture()
    db = AsyncMock()
    db.scalar.return_value = terminal
    db.get.return_value = probe
    req = request(actor, agent.agent_instance_id)
    result = await api.create_probe(
        10, api.ProbeCreate(request_id=probe.request_id), req, db
    )
    assert result["state"] == "pending"
    db.commit.assert_not_awaited()
    with pytest.raises(HTTPException) as error:
        await api.create_probe(10, api.ProbeCreate(request_id=uuid4()), req, db)
    assert error.value.detail == {"code": "fm_probe_busy"}
    db.scalar.return_value = probe
    with pytest.raises(HTTPException) as error:
        await api.get_probe(
            probe.request_id,
            request({**actor, "X-Org-Id": "8"}, agent.agent_instance_id),
            db,
        )
    assert error.value.status_code == 404


def test_no_proof_for_failed_or_expired_result():
    _, probe, agent, _ = fixture()
    ready(probe, agent)
    probe.proof_until = datetime.now(UTC) - timedelta(seconds=1)
    assert not api.probe_state(probe, datetime.now(UTC))["available"]
    probe.result = {"state": "unavailable", "available": False, "error_code": "fixture"}
    assert api.probe_state(probe, datetime.now(UTC))["state"] == "unavailable"
