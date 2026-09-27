from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.models import Terminal
from app.services.terminal_onboarding_service import IssueCertificatePinResponse
from app.services.terminal_pin_service import TerminalPinService


@pytest.fixture
def anyio_backend():
    return "asyncio"


def response(terminal, operation, **overrides):
    values = {
        "operation_id": str(operation),
        "tenant_id": terminal.org_id,
        "terminal_id": terminal.id,
        "sn": terminal.sn,
        "pin": "012345",
        "pin_masked": "***345",
        "status": "issued",
        "created_at": datetime.now(UTC),
        "expires_at": datetime.now(UTC) + timedelta(hours=1),
        "replayed": True,
    }
    values.update(overrides)
    return IssueCertificatePinResponse(**values)


def fixtures():
    db = AsyncMock()
    db.add = MagicMock()
    result = MagicMock()
    result.scalar_one_or_none.return_value = None

    async def execute(stmt, *args):
        selected = MagicMock()
        latest = None
        if "select l4desk_audit_events.operation_id" in str(stmt).lower():
            requested = [
                call.args[0]
                for call in db.add.call_args_list
                if call.args[0].event_type == "terminal.pin_requested"
            ]
            latest = requested[-1].operation_id if requested else None
        selected.scalar_one_or_none.return_value = latest
        return selected

    db.execute.side_effect = execute
    provider = AsyncMock()
    terminal = Terminal(id=41, org_id=1000, device_id=1000005, sn="synthetic-terminal")
    service = TerminalPinService(db, provider)
    service._owned_terminal = AsyncMock(return_value=terminal)
    return db, provider, terminal, service


@pytest.mark.anyio
async def test_issue_reuses_provider_operation_after_ambiguous_previous_response():
    db, provider, terminal, service = fixtures()
    operation = uuid4()
    provider.get_by_operation.return_value = response(terminal, operation)
    result = await service.issue(41, {"org_id": 1000, "sub": "owner"}, operation)
    assert result.pin == "012345"
    provider.issue_pin.assert_not_awaited()
    provider.get_by_operation.assert_awaited_once_with(str(operation))
    assert db.commit.await_count == 2
    for call in db.add.call_args_list:
        assert "012345" not in str(call.args[0].details)


@pytest.mark.anyio
async def test_timeout_preserves_intent_without_second_issue():
    db, provider, _, service = fixtures()
    provider.get_by_operation.return_value = None
    provider.issue_pin.side_effect = TimeoutError()
    with pytest.raises(HTTPException) as exc:
        await service.issue(41, {"org_id": 1000}, uuid4())
    assert exc.value.status_code == 503
    assert db.commit.await_count == 1  # intent durable before transport
    provider.issue_pin.assert_awaited_once()
    db.rollback.assert_awaited_once()


@pytest.mark.anyio
async def test_foreign_terminal_is_rejected_before_any_provider_call():
    db = AsyncMock()
    result = MagicMock()
    result.scalar_one_or_none.return_value = None
    db.execute.return_value = result
    provider = AsyncMock()
    with pytest.raises(HTTPException) as exc:
        await TerminalPinService(db, provider).current(
            41, {"org_id": 2000, "role_id": 5}
        )
    assert exc.value.status_code == 404
    query = db.execute.call_args.args[0]
    assert query.compile().params["org_id_1"] == 2000
    provider.get_by_operation.assert_not_awaited()


@pytest.mark.parametrize(
    "overrides",
    [
        {"tenant_id": 2000},
        {"terminal_id": 42},
        {"sn": "foreign"},
        {"operation_id": str(uuid4())},
    ],
)
def test_mismatched_provider_identity_is_never_disclosed(overrides):
    _, _, terminal, _ = fixtures()
    operation = uuid4()
    with pytest.raises(HTTPException) as exc:
        TerminalPinService._validate_response(
            response(terminal, operation, **overrides), terminal, str(operation)
        )
    assert exc.value.status_code == 502


@pytest.mark.parametrize("status", ["expired", "consumed"])
def test_inactive_pin_plaintext_is_removed(status):
    _, _, terminal, _ = fixtures()
    operation = uuid4()
    result = TerminalPinService._validate_response(
        response(terminal, operation, status=status), terminal, str(operation)
    )
    assert result.pin is None


def test_expired_timestamp_cannot_display_issued_pin():
    _, _, terminal, _ = fixtures()
    operation = uuid4()
    result = TerminalPinService._validate_response(
        response(
            terminal, operation, expires_at=datetime.now(UTC) - timedelta(seconds=1)
        ),
        terminal,
        str(operation),
    )
    assert result.status == "expired"
    assert result.pin is None


@pytest.mark.anyio
async def test_newer_intent_prevents_old_request_issuing_pin():
    db, provider, terminal, service = fixtures()
    original_execute = db.execute.side_effect

    async def execute(stmt, *args):
        if "select l4desk_audit_events.operation_id" in str(stmt).lower():
            result = MagicMock()
            result.scalar_one_or_none.return_value = str(uuid4())
            return result
        return await original_execute(stmt, *args)

    db.execute.side_effect = execute
    with pytest.raises(HTTPException) as exc:
        await service.issue(terminal.id, {"org_id": 1000}, uuid4())
    assert exc.value.status_code == 409
    provider.get_by_operation.assert_not_awaited()
    provider.issue_pin.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize("role_id, is_superuser", [(1, True), (4, False)])
@pytest.mark.parametrize("method", ["GET", "POST"])
async def test_settings_pin_denies_readonly_roles(role_id, is_superuser, method):
    from httpx import ASGITransport, AsyncClient

    from app.auth import get_current_user
    from app.database import get_db
    from app.main import app

    db = AsyncMock()
    app.dependency_overrides[get_current_user] = lambda: {
        "org_id": 1000,
        "role_id": role_id,
        "is_superuser": is_superuser,
    }
    app.dependency_overrides[get_db] = lambda: db
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            if method == "POST":
                result = await client.post(
                    "/api/settings/terminals/41/pin",
                    json={"operation_id": str(uuid4())},
                )
            else:
                result = await client.get("/api/settings/terminals/41/pin")
        assert result.status_code == 403
        db.execute.assert_not_awaited()
    finally:
        app.dependency_overrides.clear()


@pytest.mark.anyio
async def test_missing_sn_cannot_issue_a_pin_or_fabricate_provider_identity():
    db = AsyncMock()
    result = MagicMock()
    result.scalar_one_or_none.return_value = Terminal(
        id=41, org_id=1000, device_id=773, sn=None
    )
    db.execute.return_value = result
    provider = AsyncMock()
    with pytest.raises(HTTPException) as exc:
        await TerminalPinService(db, provider).issue(
            41, {"org_id": 1000, "role_id": 5}, uuid4()
        )
    assert exc.value.status_code == 409
    query = db.execute.call_args.args[0].compile()
    assert query.params["org_id_1"] == 1000
    provider.get_by_operation.assert_not_awaited()
    provider.issue_pin.assert_not_awaited()
