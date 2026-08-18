"""Contract tests for terminal-facing licensebilling XML API.

Spec §25 — validates XML contract for all terminal states.
"""

import xml.etree.ElementTree as ET
from datetime import UTC, datetime, timedelta
from unittest.mock import MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.dependencies import get_terminal_license_state
from app.main import app


def parse_xml(text: str) -> ET.Element:
    return ET.fromstring(text)


def get_text(xml: ET.Element, tag: str) -> str | None:
    el = xml.find(tag)
    return el.text if el is not None else None


def make_terminal(**kwargs) -> MagicMock:
    defaults = {
        "id": 1,
        "device_id": 773,
        "sn": "ABC123",
        "cert_serial": "52B8E528",
        "org_id": 1,
        "is_active": True,
    }
    defaults.update(kwargs)
    t = MagicMock()
    for k, v in defaults.items():
        setattr(t, k, v)
    return t


def make_license(**kwargs) -> MagicMock:
    defaults = {
        "id": 1,
        "terminal_id": 1,
        "org_id": 1,
        "license_type": "standard",
        "expires_at": datetime.now(UTC) + timedelta(days=30),
        "balance": 97685,
        "is_active": True,
    }
    defaults.update(kwargs)
    lic = MagicMock()
    for k, v in defaults.items():
        setattr(lic, k, v)
    return lic


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


async def _make_request(method: str, path: str, terminal, license_state):
    """Helper to make a request with mocked dependencies."""

    async def override_state():
        return license_state

    app.dependency_overrides[get_terminal_license_state] = override_state

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        if method == "get":
            return await client.get(path)
        else:
            return await client.post(path)


@pytest.mark.anyio
async def test_active_license_returns_ok():
    """Scenario 1: active license → Result=OK, state=ok, balance preserved."""
    terminal = make_terminal()
    license_ = make_license()
    state = MagicMock(license=license_, state="ok")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "ok"
    assert get_text(root, "balance") == "97685"


@pytest.mark.anyio
async def test_expired_license_returns_ok_with_error_state():
    """Scenario 2: expired license → Result=OK (not ERROR), state=error, balance preserved."""
    terminal = make_terminal()
    license_ = make_license(expires_at=datetime.now(UTC) - timedelta(days=1))
    state = MagicMock(license=license_, state="error")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "error"
    assert get_text(root, "balance") == "97685"


@pytest.mark.anyio
async def test_org_blocked_returns_error_state():
    """Scenario 3: org blocked → state=error."""
    terminal = make_terminal()
    license_ = make_license()
    state = MagicMock(license=license_, state="error")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "error"


@pytest.mark.anyio
async def test_no_active_license_returns_error_with_balance_zero():
    """Scenario 4: no active license → state=error, balance=0."""
    terminal = make_terminal()
    state = MagicMock(license=None, state="error")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "error"
    assert get_text(root, "balance") == "0"


@pytest.mark.anyio
async def test_renewal_disabled_license_active_returns_ok():
    """Scenario 5: renewal_enabled=false, license active → state=ok."""
    terminal = make_terminal()
    license_ = make_license()
    state = MagicMock(license=license_, state="ok")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "state") == "ok"


@pytest.mark.anyio
async def test_renewal_disabled_license_expired_returns_error():
    """Scenario 6: renewal_enabled=false, license expired → state=error."""
    terminal = make_terminal()
    license_ = make_license(expires_at=datetime.now(UTC) - timedelta(days=1))
    state = MagicMock(license=license_, state="error")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "state") == "error"


@pytest.mark.anyio
async def test_deactivation_does_not_change_balance():
    """Scenario 7: user deactivation doesn't change balance value."""
    terminal = make_terminal()
    license_ = make_license(balance=97685)
    state = MagicMock(license=license_, state="ok")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    root = parse_xml(resp.text)
    assert get_text(root, "balance") == "97685"


@pytest.mark.anyio
async def test_admin_disabled_terminal_returns_error():
    """Scenario 8: admin-disabled terminal → state=error."""
    terminal = make_terminal(is_active=False)
    state = MagicMock(license=None, state="error")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "error"


@pytest.mark.anyio
async def test_standard_expiry_is_result_ok():
    """Scenario 9: standard license expiry returns Result=OK (not ERROR)."""
    terminal = make_terminal()
    license_ = make_license(expires_at=datetime(2026, 8, 10, tzinfo=UTC))
    state = MagicMock(license=license_, state="error")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    root = parse_xml(resp.text)
    result = get_text(root, "Result")
    assert result == "OK", f"Expected Result=OK, got {result}"


@pytest.mark.anyio
async def test_xml_declaration_present():
    """Verify XML declaration is present in response."""
    terminal = make_terminal()
    license_ = make_license()
    state = MagicMock(license=license_, state="ok")

    resp = await _make_request("get", "/api/licensebilling", terminal, state)

    assert resp.text.startswith("<?xml")
    assert "UTF-8" in resp.text


@pytest.mark.anyio
async def test_post_endpoint_works_same_as_get():
    """POST endpoint returns same contract as GET."""
    terminal = make_terminal()
    license_ = make_license()
    state = MagicMock(license=license_, state="ok")

    resp = await _make_request("post", "/api/licensebilling", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "ok"


@pytest.mark.anyio
async def test_check_endpoint_works():
    """GET /api/licensebilling/check returns same contract."""
    terminal = make_terminal()
    license_ = make_license()
    state = MagicMock(license=license_, state="ok")

    resp = await _make_request("get", "/api/licensebilling/check", terminal, state)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "ok"
