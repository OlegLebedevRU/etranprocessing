from unittest.mock import AsyncMock

from fastapi import FastAPI
from fastapi.testclient import TestClient

from app.auth import require_superuser
from app.routers import admin_mqtt


def test_tenant_cannot_read_addresses_or_write_decisions():
    app = FastAPI()
    app.include_router(admin_mqtt.router)
    app.dependency_overrides[require_superuser] = lambda: {"is_superuser": False}
    client = TestClient(app)
    for path in ("history", "observations", "preview?ip=127.0.0.1"):
        assert client.get("/api/admin/mqtt/" + path).status_code == 403
    assert client.post("/api/admin/mqtt/operations", json={}).status_code == 403


def test_preview_passes_only_canonical_address(monkeypatch):
    app = FastAPI()
    app.include_router(admin_mqtt.router)
    app.dependency_overrides[require_superuser] = lambda: {"is_superuser": True}
    edge = AsyncMock(return_value={"active": []})
    monkeypatch.setattr(admin_mqtt, "edge", edge)
    client = TestClient(app)
    url = "/api/admin/mqtt/preview"
    assert client.get(url, params={"ip": "2001:0db8::0001"}).status_code == 200
    assert client.get(url, params={"ip": "::/0"}).status_code == 422
    edge.assert_awaited_once_with("preview", {"ip": "2001:db8::1"})
