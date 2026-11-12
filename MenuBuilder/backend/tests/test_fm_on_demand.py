"""Only explicit selected-terminal probes may dispatch readiness commands."""

from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.routers import file_manager as fm


def test_frozen_readiness_messages_reject_extra_fields_and_wrong_types():
    import json
    from pathlib import Path

    from jsonschema import Draft202012Validator, FormatChecker, ValidationError

    root = Path(__file__).resolve().parents[3] / "contracts/file-manager-readiness-v1"
    schema = json.loads((root / "schema.json").read_text())
    fixtures = json.loads((root / "fixtures.json").read_text())
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    for value in fixtures.values():
        validator.validate(value)
    for value in (
        {**fixtures["command"], "lease_id": fixtures["command"]["command_id"]},
        {**fixtures["success"], "filesystem_ready": "true"},
        {**fixtures["failure"], "request_id": "not-a-uuid"},
    ):
        with pytest.raises(ValidationError):
            validator.validate(value)


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_probe_creates_pending_before_dispatch_and_releases_portal_transaction(
    monkeypatch,
):
    db = AsyncMock()
    calls = []
    identifier = uuid4()
    monkeypatch.setattr(
        fm, "_verify_device_access", AsyncMock(return_value=SimpleNamespace(org_id=7))
    )
    monkeypatch.setattr(fm, "actor", lambda *a: {"X-Org-Id": "7"})
    monkeypatch.setattr(
        fm, "readiness_for", AsyncMock(return_value={"available": False})
    )

    async def upstream(target, path, headers, body=None):
        db.rollback.assert_awaited_once()
        calls.append((target, path))
        if path.endswith("/readiness-probes") and target == "pb":
            return {
                "state": "pending",
                "expires_at": (datetime.now(UTC) + timedelta(seconds=10)).isoformat(),
            }
        if target == "iot":
            return {"dispatched": True}
        return {"state": "ready", "available": True, "request_id": str(identifier)}

    monkeypatch.setattr(fm, "upstream", upstream)
    result = await fm.probe(10, fm.ProbeBody(request_id=identifier), uuid4(), {}, db)
    assert result["available"]
    assert [target for target, _ in calls] == ["pb", "iot", "pb"]


@pytest.mark.anyio
async def test_live_legacy_selected_terminal_uses_existing_path_without_probe(
    monkeypatch,
):
    monkeypatch.setattr(
        fm, "_verify_device_access", AsyncMock(return_value=SimpleNamespace(org_id=7))
    )
    monkeypatch.setattr(fm, "actor", lambda *a: {})
    legacy = {"available": True, "capabilities": ["fs.session"]}
    monkeypatch.setattr(fm, "readiness_for", AsyncMock(return_value=legacy))
    dispatch = AsyncMock()
    monkeypatch.setattr(fm, "upstream", dispatch)
    assert (
        await fm.probe(10, fm.ProbeBody(request_id=uuid4()), uuid4(), {}, AsyncMock())
        == legacy
    )
    dispatch.assert_not_awaited()


@pytest.mark.anyio
async def test_unavailable_proof_cannot_acquire_lease(monkeypatch):
    monkeypatch.setattr(
        fm, "_verify_device_access", AsyncMock(return_value=SimpleNamespace(org_id=7))
    )
    monkeypatch.setattr(fm, "actor", lambda *a: {})
    upstream = AsyncMock(return_value={"available": False})
    monkeypatch.setattr(fm, "upstream", upstream)
    with pytest.raises(HTTPException):
        await fm.start(
            10, fm.StartBody(readiness_probe_id=uuid4()), uuid4(), {}, AsyncMock()
        )
    assert upstream.await_count == 1 and upstream.call_args.args[0] == "pb"
