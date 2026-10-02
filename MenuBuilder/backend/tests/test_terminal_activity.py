from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

import pytest
from fastapi import HTTPException
from starlette.requests import Request

from app.models import Terminal
from app.routers.settings import TerminalActivityRequest, set_terminal_activity
from app.routers.video import _verify_device_access
from app.security.permissions import PERMISSION_MONITORING_VIEW, require_permission


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_activity_retry_sets_target_without_toggling_and_preserves_identity():
    terminal = Terminal(
        id=7, org_id=10000, device_id=1000009, is_active=True, cert_serial="fixture"
    )
    db = AsyncMock()
    db.scalar.return_value = terminal
    with patch("app.routers.settings.L4DeskRepository") as repo:
        repo.return_value.get_active_session_by_terminal_id = AsyncMock(
            return_value=None
        )
        user = {"org_id": 10000, "role_id": 5}
        for _ in range(2):
            result = await set_terminal_activity(
                7, TerminalActivityRequest(is_active=False), user, db
            )
            assert result == {"id": 7, "is_active": False}
        db.commit.assert_awaited_once()
        assert terminal.cert_serial == "fixture"
        params = db.scalar.call_args.args[0].compile().params
        assert params["org_id_1"] == 10000
        compiled = str(
            db.scalar.call_args.args[0].compile(compile_kwargs={"literal_binds": True})
        )
        assert "terminals.id = 7" in compiled


@pytest.mark.anyio
async def test_activity_denies_open_sessions_and_missing_or_foreign_terminal():
    db = AsyncMock()
    terminal = Terminal(id=7, org_id=10000, is_active=True)
    db.scalar.return_value = terminal
    with patch("app.routers.settings.L4DeskRepository") as repo:
        repo.return_value.get_active_session_by_terminal_id = AsyncMock(
            return_value=SimpleNamespace(state="active")
        )
        with pytest.raises(HTTPException) as error:
            await set_terminal_activity(
                7,
                TerminalActivityRequest(is_active=False),
                {"org_id": 10000, "role_id": 5},
                db,
            )
        assert error.value.status_code == 409
        assert terminal.is_active
    db.scalar.return_value = None
    with pytest.raises(HTTPException) as error:
        await set_terminal_activity(
            7,
            TerminalActivityRequest(is_active=False),
            {"org_id": 20000, "role_id": 5},
            db,
        )
    assert error.value.status_code == 404
    db.commit.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize("role_id", [1, 2, 4])
async def test_activity_denies_readonly_and_unprivileged_roles(role_id):
    db = AsyncMock()
    with pytest.raises(HTTPException) as error:
        await set_terminal_activity(
            7,
            TerminalActivityRequest(is_active=False),
            {"org_id": 10000, "role_id": role_id},
            db,
        )
    assert error.value.status_code == 403
    db.scalar.assert_not_awaited()


@pytest.mark.anyio
async def test_disabled_terminal_denies_new_video_access_but_allows_cleanup():
    db = AsyncMock()
    terminal = Terminal(device_id=9, org_id=10000, is_active=False)
    db.scalar.return_value = terminal
    user = {"org_id": 10000, "role_id": 5}
    with pytest.raises(HTTPException) as error:
        await _verify_device_access(9, user, db, require_active=True)
    assert error.value.status_code == 403
    assert await _verify_device_access(9, user, db) is terminal


@pytest.mark.anyio
async def test_activity_requires_a_tenant_before_accessing_database():
    db = AsyncMock()
    with pytest.raises(HTTPException) as error:
        await set_terminal_activity(
            7, TerminalActivityRequest(is_active=False), {"org_id": 0, "role_id": 5}, db
        )
    assert error.value.status_code == 400
    db.scalar.assert_not_awaited()


@pytest.mark.anyio
async def test_role5_cannot_read_monitoring_even_with_explicit_permission():
    request = Request({"type": "http", "method": "GET"})
    check = require_permission(PERMISSION_MONITORING_VIEW)
    with pytest.raises(HTTPException) as error:
        await check(request, {"role_id": 5, "org_id": 10000, "permissions": ["*"]})
    assert error.value.status_code == 403
    user = {"role_id": 3, "org_id": 10000}
    assert await check(request, user) == user
