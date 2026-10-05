from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock
from uuid import uuid4

import pytest
from etranprocessing_db.file_manager import FileManagerAgent, FileManagerOperation
from fastapi import HTTPException
from starlette.requests import Request

from app.models import Terminal
from app.routers import file_manager as fm
from app.schemas.file_manager import AgentResult, SourceManifest
from app.services import file_manager_storage as storage
from app.services.file_manager_readiness import FM_REQUIRED_CAPABILITIES, evaluate_readiness


@pytest.fixture
def anyio_backend():
    return "asyncio"


def registered(now):
    return FileManagerAgent(terminal_id=1, tenant_id=7, agent_instance_id=uuid4(), agent_version="1.0",
        protocol_version=1, capabilities=sorted(FM_REQUIRED_CAPABILITIES), filesystem_ready=True,
        cert_serial="AB", last_seen_at=now)


@pytest.mark.parametrize("change,state", [({}, "ready"), ({"tenant_id": 8}, "not_registered"),
    ({"protocol_version": 2}, "incompatible"), ({"capabilities": ["fs.list"]}, "incompatible"),
    ({"filesystem_ready": False}, "filesystem_unavailable"), ({"cert_serial": "AC"}, "certificate_changed")])
def test_readiness_requires_compatible_live_current_identity(change, state):
    now = datetime.now(UTC)
    terminal = Terminal(id=1, device_id=10, org_id=7, sn="fixture", is_active=True, cert_serial="AB")
    agent = registered(now)
    for key, value in change.items():
        setattr(agent, key, value)
    result = evaluate_readiness(terminal, agent, now=now)
    assert result.state == state
    assert result.available == (state == "ready")


@pytest.mark.parametrize("age", [45, 46, -1])
def test_expired_and_future_heartbeat_are_unavailable(age):
    now = datetime.now(UTC)
    terminal = Terminal(id=1, org_id=7, is_active=True, cert_serial="AB")
    assert not evaluate_readiness(terminal, registered(now - timedelta(seconds=age)), now=now).available


def test_disabled_and_unregistered_are_unavailable():
    now = datetime.now(UTC)
    terminal = Terminal(org_id=7, is_active=False)
    assert evaluate_readiness(terminal, registered(now), now=now).state == "disabled"
    terminal.is_active = True
    assert evaluate_readiness(terminal, None, now=now).state == "not_registered"


def operation(kind="upload", state="running"):
    now = datetime.now(UTC)
    return FileManagerOperation(id=uuid4(), lease_id=uuid4(), terminal_id=1, tenant_id=7,
        owner_user_id="fixture", owner_session_id="fixture-tab", owner_role="user", kind=kind, state=state,
        path=r"C:\fixture\report.txt", size_bytes=3, sha256="ab" * 32,
        data={"object_key": "fixture/object", "version_id": "immutable-fixture"},
        created_at=now, updated_at=now, expires_at=now+timedelta(seconds=60))


@pytest.mark.anyio
async def test_cross_tenant_or_tab_cannot_read_operation():
    op = operation()
    db = AsyncMock()
    db.get.return_value = op
    for tenant, tab in ((8, "fixture-tab"), (7, "another-tab")):
        headers = [(b"x-org-id", str(tenant).encode()), (b"x-user-id", b"fixture"), (b"x-session-id", tab.encode()), (b"x-role", b"user")]
        with pytest.raises(HTTPException) as error:
            await fm.owned_operation(op.id, Request({"type": "http", "headers": headers}), db)
        assert error.value.status_code == 404


@pytest.mark.anyio
async def test_upload_completion_requires_commit_permission(monkeypatch):
    op = operation()
    monkeypatch.setattr(fm, "agent_operation", AsyncMock(return_value=op))
    with pytest.raises(HTTPException) as error:
        await fm.agent_result(op.id, AgentResult(state="completed"), SimpleNamespace(), AsyncMock())
    assert error.value.status_code == 409
    assert op.state == "running"


@pytest.mark.anyio
async def test_session_renew_requires_the_current_ticket_ack(monkeypatch):
    op = operation("session", "active")
    op.data = {"grant_id": str(uuid4()), "ticket_expires_at": op.expires_at.isoformat()}
    monkeypatch.setattr(fm, "agent_operation", AsyncMock(return_value=op))
    with pytest.raises(HTTPException):
        await fm.agent_result(op.id, AgentResult(state="active", grant_id=uuid4()), SimpleNamespace(), AsyncMock())
    assert "applied_expires_at" not in op.data
    from uuid import UUID
    await fm.agent_result(op.id, AgentResult(state="active", grant_id=UUID(op.data["grant_id"])), SimpleNamespace(), AsyncMock())
    assert op.data["applied_expires_at"] == op.expires_at.isoformat()


@pytest.mark.anyio
async def test_checksum_failure_ends_operation_without_dispatch(monkeypatch):
    op, db = operation(), AsyncMock()
    monkeypatch.setattr(fm, "lease_request", AsyncMock(return_value={"scope": "files"}))
    monkeypatch.setattr(storage, "verify_object", AsyncMock(side_effect=HTTPException(409, "integrity failed")))
    with pytest.raises(HTTPException):
        await fm.source_complete(op, db, "upload")
    assert op.state == "failed"
    assert fm.lease_request.await_count == 1
    db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_storage_pins_verified_version_without_reading_body(monkeypatch):
    s3 = Mock()
    s3.head_object.return_value = {"ContentLength": 3, "ChecksumSHA256": storage.checksum("ab" * 32), "VersionId": "fixture-version"}
    s3.generate_presigned_url.return_value = "https://example.invalid/fixture"
    monkeypatch.setattr(storage, "client", lambda: s3)
    version = await storage.verify_object("fixture/object", 3, "ab" * 32)
    await storage.download_grant("fixture/object", version, 30)
    assert s3.generate_presigned_url.call_args.kwargs["Params"]["VersionId"] == "fixture-version"
    s3.get_object.assert_not_called()


@pytest.mark.anyio
@pytest.mark.parametrize("mismatch", [{"ContentLength": 4}, {"ChecksumSHA256": "wrong"}, {"VersionId": "null"}, {"VersionId": None}])
async def test_head_rejects_length_checksum_and_unversioned_provider(monkeypatch, mismatch):
    s3 = Mock()
    s3.head_object.return_value = {"ContentLength": 3, "ChecksumSHA256": storage.checksum("ab" * 32), "VersionId": "fixture", **mismatch}
    monkeypatch.setattr(storage, "client", lambda: s3)
    with pytest.raises(HTTPException):
        await storage.verify_object("fixture/object", 3, "ab" * 32)


def test_manifest_rejects_oversize_unknown_fields_and_noncanonical_digest():
    from pydantic import ValidationError
    for values in ({"size_bytes": 67108865, "sha256": "ab" * 32},
        {"size_bytes": 1, "sha256": "AB" * 32}, {"size_bytes": 1, "sha256": "ab" * 32, "url": "https://example.invalid"}):
        with pytest.raises(ValidationError):
            SourceManifest(**values)


@pytest.mark.anyio
async def test_agent_restart_rejects_old_operation_before_lease_lookup(monkeypatch):
    op = operation()
    old_instance, new_instance = uuid4(), uuid4()
    op.data = {"agent_instance_id": str(old_instance), "cert_serial": "AB"}
    db = AsyncMock()
    db.get.side_effect = [op, SimpleNamespace(agent_instance_id=new_instance)]
    lease = AsyncMock()
    monkeypatch.setattr(fm, "lease_request", lease)
    terminal = Terminal(id=1, org_id=7, device_id=10, is_active=True, cert_serial="AB")
    with pytest.raises(HTTPException) as error:
        await fm.agent_operation(op.id, terminal, db)
    assert error.value.status_code == 409
    lease.assert_not_awaited()
