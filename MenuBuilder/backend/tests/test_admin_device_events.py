"""Superuser inbox authorization and explicit event acknowledgement."""

from unittest.mock import AsyncMock
from uuid import uuid4

import pytest
from etranprocessing_db.models.device_events import DeviceAdminEvent
from httpx import ASGITransport, AsyncClient

from app.auth import get_current_user
from app.database import get_db
from app.main import app


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.parametrize("user", [{"username": "tenant-admin", "role": "admin", "is_superuser": False}, {"username": "user", "role": "user", "is_superuser": False}])
async def test_inbox_count_list_and_mutation_deny_non_superusers(user):
    async def current_user(): return user
    app.dependency_overrides[get_current_user] = current_user
    try:
        async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
            for method, path, body in [("GET", "/api/admin/device-events/count", None), ("GET", "/api/admin/device-events", None), ("PATCH", "/api/admin/device-events/1", {"status": "closed"}), ("GET", "/api/admin/terminals/1/identity-change", None)]:
                response = await client.request(method, path, json=body, headers={"X-Requested-With": "XMLHttpRequest"})
                assert response.status_code == 403
    finally:
        app.dependency_overrides.clear()


@pytest.mark.anyio
async def test_count_does_not_acknowledge_and_explicit_update_records_actor():
    event = DeviceAdminEvent(id=1, event_key="fixture", terminal_id=1, operation_id=uuid4(), event_type="identity_old_certificate", status="new", details={})
    db = AsyncMock()
    db.scalar.return_value = 1
    db.get.return_value = event
    async def current_user(): return {"username": "fixture-superuser", "role": "superuser", "is_superuser": True}
    async def database(): yield db
    app.dependency_overrides[get_current_user] = current_user
    app.dependency_overrides[get_db] = database
    try:
        async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
            response = await client.get("/api/admin/device-events/count")
            assert response.json() == {"new_count": 1}
            assert event.status == "new"
            db.commit.assert_not_awaited()
            response = await client.patch("/api/admin/device-events/1", json={"status": "closed", "comment": "Установлен новый сертификат"}, headers={"X-Requested-With": "XMLHttpRequest"})
            assert response.status_code == 200
            assert event.status == "closed" and event.updated_by == "fixture-superuser"
            assert event.comment == "Установлен новый сертификат"
    finally:
        app.dependency_overrides.clear()


@pytest.mark.anyio
async def test_start_is_idempotent_and_cancel_does_not_touch_renewal_pins():
    from datetime import UTC, datetime
    from etranprocessing_db.models.device_events import TerminalIdentityChange
    from app.models import Org, Terminal
    from app.routers.admin_identity_changes import StartIdentityChange, cancel_identity_change, start_identity_change
    from unittest.mock import MagicMock

    terminal = Terminal(id=1, org_id=1, sn="fixture-sn", device_id=10, cert_serial="A" * 40, l4desk_subscription_enabled=False)
    operation_id = uuid4()
    db = AsyncMock(); db.add = MagicMock()
    db.scalar.side_effect = [terminal, None]
    db.get.side_effect = [None, Org(org_id=2, site_mode="classic", is_active=True), Org(org_id=1, site_mode="classic")]
    body = StartIdentityChange(target_org_id=2, operation_id=operation_id)
    result = await start_identity_change(1, body, {"username": "fixture-su", "is_superuser": True}, db)
    assert result["status"] == "pending" and terminal.org_id == 1
    change = db.add.call_args.args[0]
    statement = db.execute.call_args.args[0]
    assert "setup" in statement.compile().params.values()
    db.scalar.side_effect = [terminal]
    db.get.side_effect = [change]
    db.commit.reset_mock(); db.add.reset_mock()
    assert (await start_identity_change(1, body, {"username": "fixture-su"}, db))["operation_id"] == str(operation_id)
    db.commit.assert_not_awaited(); db.add.assert_not_called()
    db.scalar.side_effect = [terminal]; db.get.side_effect = [change]
    assert (await cancel_identity_change(1, operation_id, db))["status"] == "cancelled"
    assert terminal.org_id == 1 and terminal.cert_serial == "A" * 40
    assert "setup" in db.execute.call_args.args[0].compile().params.values()
