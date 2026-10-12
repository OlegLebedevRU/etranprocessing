from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest
from etranprocessing_db.inventory import (
    TerminalToolsInventory,
    TerminalToolsInventoryInbox,
)
from fastapi import HTTPException
from pydantic import ValidationError
from sqlalchemy.exc import SQLAlchemyError

from app.services.terminal_inventory import InventoryDelivery, apply_inventory


@pytest.fixture
def anyio_backend():
    return "asyncio"


def envelope(*, revision=10, version="1.13.43", at=None, event_uuid=None):
    return InventoryDelivery.model_validate(
        {
            "v": 1,
            "tenant_id": 1,
            "device_id": 773,
            "sn": "fixture",
            "source_revision": revision,
            "event": {
                "101": 123456,
                "102": at or datetime.now(UTC),
                "200": 75,
                "correlationData": str(event_uuid or uuid4()),
                "300": [
                    {
                        "324": "fixture",
                        "440": "l4con",
                        "441": "A" * 40,
                        "442": "B" * 36,
                        "443": datetime.now(UTC) + timedelta(days=365),
                        "445": version,
                        "444": [
                            {
                                "name": "l4con",
                                "file_version": "1.12.4.0",
                                "arch": "x64",
                            },
                            {"name": "mosquitto", "file_version": None, "arch": "x86"},
                        ],
                    }
                ],
            },
        }
    )


def database(snapshot=None, inbox=None, terminal=True):
    return SimpleNamespace(
        scalar=AsyncMock(
            return_value=SimpleNamespace(
                id=7, org_id=1, sn="fixture", cert_serial="authority"
            )
            if terminal
            else None
        ),
        execute=AsyncMock(),
        get=AsyncMock(side_effect=[inbox, snapshot]),
        add=MagicMock(),
        commit=AsyncMock(),
    )


@pytest.mark.anyio
async def test_projection_uses_suite_baseline_and_never_overwrites_ca_identity():
    db = database()
    data = envelope()
    assert await apply_inventory(db, data) == "applied"
    snapshot = next(
        c.args[0]
        for c in db.add.call_args_list
        if isinstance(c.args[0], TerminalToolsInventory)
    )
    assert snapshot.suite_version == "1.13.43"
    assert snapshot.components[0]["file_version"] == "1.12.4.0"
    assert snapshot.components[1] == {
        "name": "mosquitto",
        "file_version": None,
        "arch": "x86",
    }
    assert snapshot.cert_serial == "B" * 36
    assert (await db.scalar()).cert_serial == "authority"
    db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_delayed_event_is_acknowledged_durably_without_rolling_back_version():
    snapshot = TerminalToolsInventory(
        terminal_id=7,
        tenant_id=1,
        sn="fixture",
        suite_version="1.14.0",
        source_revision=20,
        published_at=datetime.now(UTC),
    )
    db = database(snapshot)
    assert (
        await apply_inventory(
            db, envelope(revision=10, at=datetime.now(UTC) - timedelta(hours=1))
        )
        == "older"
    )
    assert snapshot.suite_version == "1.14.0"
    inbox = db.add.call_args.args[0]
    assert isinstance(inbox, TerminalToolsInventoryInbox) and inbox.outcome == "older"
    db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_duplicate_uuid_requires_identical_delivery_and_can_retry_after_ack_loss():
    data = envelope()
    db = database()
    await apply_inventory(db, data)
    inbox = next(
        c.args[0]
        for c in db.add.call_args_list
        if isinstance(c.args[0], TerminalToolsInventoryInbox)
    )
    retry = database(inbox=inbox)
    assert await apply_inventory(retry, data) == "duplicate"
    retry.commit.assert_not_awaited()
    conflict = database(inbox=inbox)
    changed = data.model_copy(deep=True)
    changed.event.properties[0].suite_version = "1.99.0"
    with pytest.raises(HTTPException, match="inventory_event_conflict"):
        await apply_inventory(conflict, changed)


@pytest.mark.anyio
async def test_foreign_binding_future_clock_and_failed_commit_cannot_acknowledge():
    with pytest.raises(HTTPException) as error:
        await apply_inventory(database(terminal=False), envelope())
    assert error.value.status_code == 404
    with pytest.raises(HTTPException) as error:
        await apply_inventory(
            database(), envelope(at=datetime.now(UTC) + timedelta(hours=1))
        )
    assert error.value.status_code == 422
    db = database()
    db.commit.side_effect = SQLAlchemyError("fixture")
    with pytest.raises(SQLAlchemyError):
        await apply_inventory(db, envelope())


def test_allowlist_and_mandatory_installed_baseline():
    raw = envelope().model_dump(mode="json", by_alias=True)
    raw["event"]["200"] = 76
    with pytest.raises(ValidationError):
        InventoryDelivery.model_validate(raw)

    raw["event"]["200"] = 75
    raw["event"]["300"][0]["445"] = ""
    with pytest.raises(ValidationError):
        InventoryDelivery.model_validate(raw)


@pytest.mark.anyio
async def test_private_auth_and_size_limit_run_before_json_parser(monkeypatch):
    import secrets

    from fastapi import FastAPI
    from httpx import ASGITransport, AsyncClient

    from app.config import settings
    from app.routers.terminal_inventory import router

    token = secrets.token_urlsafe(32)
    monkeypatch.setattr(settings, "inventory_delivery_token", token)
    api = FastAPI()
    api.include_router(router)
    path = "/api/internal/v1/terminal-inventory/events"
    async with AsyncClient(
        transport=ASGITransport(app=api), base_url="http://test"
    ) as client:
        assert (await client.post(path, content="invalid JSON")).status_code == 403
        assert (
            await client.post(
                path, headers={"X-Internal-Service-Key": token}, content="x" * 65537
            )
        ).status_code == 413
        assert (
            await client.post(path, headers={"X-Internal-Service-Key": token}, json={})
        ).status_code == 422
        assert path not in (await client.get("/openapi.json")).json()["paths"]
