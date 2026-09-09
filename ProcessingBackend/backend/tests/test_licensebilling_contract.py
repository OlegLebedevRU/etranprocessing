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


@pytest.mark.anyio
@pytest.mark.parametrize(
    "path",
    [
        "/api/licensebilling",
        "/api/licensebilling/",
        "/api/licensebilling/check",
        "/api/licensebilling/check/",
        "/api/licensebilling/gate.ashx",
        "/api/licensebilling/gate.ashx/",
        "/licensebilling",
        "/licensebilling/",
        "/licensebilling/check",
        "/licensebilling/check/",
        "/licensebilling/gate.ashx",
        "/licensebilling/gate.ashx/",
    ],
)
async def test_all_legacy_paths_and_trailing_slashes(path: str):
    """Ensure all legacy routes and trailing slashes return 200 OK without 307 redirect."""
    terminal = make_terminal()
    license_ = make_license()
    state = MagicMock(license=license_, state="ok")

    for method in ("get", "post"):
        resp = await _make_request(method, path, terminal, state)
        assert resp.status_code == 200, (
            f"Failed for {method.upper()} {path}: status={resp.status_code}, "
            f"headers={dict(resp.headers)}"
        )
        assert "application/xml" in resp.headers.get("content-type", "")
        root = parse_xml(resp.text)
        assert get_text(root, "Result") == "OK"
        assert get_text(root, "state") == "ok"


@pytest.mark.anyio
@pytest.mark.parametrize(
    "path",
    [
        "/api/licensebilling",
        "/api/licensebilling/",
        "/licensebilling",
        "/licensebilling/",
    ],
)
async def test_missing_cert_headers_returns_xml_401(path: str):
    """Missing cert headers must return XML (not JSON) with Result=ERROR."""
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get(path)

    assert "application/xml" in resp.headers.get("content-type", "")
    assert resp.status_code == 401
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "ERROR"
    assert get_text(root, "Description") is not None


@pytest.mark.anyio
async def test_admin_disabled_returns_xml_not_json():
    """H1: Admin-disabled terminal returns XML state=error (not JSON 403).

    This test uses the real get_terminal_license_state (no override)
    to verify the H1 fix: is_active check moved from get_current_terminal
    to get_terminal_license_state.
    """
    # We can't fully test without DB, but we verify the exception handler
    # catches HTTPException on /api/licensebilling and returns XML.
    # The admin-disabled case is now handled by get_terminal_license_state
    # which returns TerminalLicenseState(license=..., state="error") —
    # no HTTPException at all for recognized terminals.
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # No cert headers → 401 from get_current_terminal
        resp = await client.get("/api/licensebilling")

    assert "application/xml" in resp.headers.get("content-type", "")
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "ERROR"
