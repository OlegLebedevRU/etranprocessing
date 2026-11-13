"""Real fixture CA verification, strict current identity, no PIN or issuance."""

from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.main import app
from app.routers import certificates as module
from tests.test_authenticated_renewal import SERIAL, SN, TEST_CA, identity_headers


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def confirmation(monkeypatch):
    terminal = SimpleNamespace(
        id=101, device_id=773, org_id=1, sn=SN, cert_serial=SERIAL, is_active=True
    )
    db = SimpleNamespace(scalar=AsyncMock(return_value=terminal))
    discovered = AsyncMock(return_value=object())
    signer = AsyncMock()
    monkeypatch.setattr(module, "_renewal_ca", lambda: TEST_CA)
    monkeypatch.setattr(module, "subscription_allowance", AsyncMock(return_value=True))
    monkeypatch.setattr(module, "record_terminal_discovery", discovered)
    monkeypatch.setattr(module, "sign_csr", signer)

    async def dependency():
        yield db

    app.dependency_overrides[get_db] = dependency
    yield terminal, discovered, signer
    app.dependency_overrides.clear()


@pytest.mark.anyio
async def test_confirm_current_leaf_and_duplicate_without_issuance(confirmation):
    terminal, discovered, signer = confirmation
    headers = identity_headers(org_id="1", device_id="773")
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        first = await client.post(
            "/api/certificates/renew", headers=headers, json={"function": "confirm"}
        )
        replay = await client.post(
            "/api/certificates/renew", headers=headers, json={"function": "confirm"}
        )
    assert first.status_code == replay.status_code == 200
    data = first.json()
    assert data == replay.json()
    assert data["confirmed"] is True and data["sn"] == SN and data["device_id"] == "773"
    assert len(data["fingerprint_sha256"]) == 64
    assert first.headers["cache-control"] == "no-store"
    assert discovered.await_count == 2 and terminal.cert_serial == SERIAL
    args = discovered.await_args.kwargs
    assert (
        args["cert_serial"] == SERIAL
        and args["is_valid"]
        and args["terminal_id"] == 101
    )
    signer.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize(
    "failure",
    [
        "missing_tls",
        "expired",
        "wrong_serial",
        "wrong_org",
        "wrong_device",
        "missing_org",
        "disabled",
        "extra_pin",
        "discovery_failure",
    ],
)
async def test_confirmation_rejects_invalid_or_unpersisted_proof(confirmation, failure):
    terminal, discovered, signer = confirmation
    headers = identity_headers(
        org_id=None
        if failure == "missing_org"
        else "2"
        if failure == "wrong_org"
        else "1",
        device_id="774" if failure == "wrong_device" else "773",
        expired=failure == "expired",
        serial="5" * 40 if failure == "wrong_serial" else SERIAL,
    )
    body = {"function": "confirm"}
    expected = 401
    if failure == "missing_tls":
        headers = {}
    if failure == "disabled":
        terminal.is_active = False
        expected = 403
    if failure == "extra_pin":
        body["pin"] = "000000"
        expected = 422
    if failure == "discovery_failure":
        discovered.return_value = None
        expected = 503
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        reply = await client.post("/api/certificates/renew", headers=headers, json=body)
    assert reply.status_code == expected
    assert discovered.await_count == int(failure == "discovery_failure")
    signer.assert_not_awaited()
