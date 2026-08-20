"""Tests for automatic cert_serial binding on licensebilling requests.

Validates:
1. When terminal.cert_serial is unset (None/empty) in DB:
   - A request to /api/licensebilling (or /api/licensebilling/check) auto-populates cert_serial from X-Client-Cert-Serial.
   - TerminalCertHistory entry is added with source='licensebilling'.
   - Changes are committed to DB.
2. When auto_set_cert_serial_on_licensebilling is disabled (False):
   - Terminal without cert_serial is rejected with 401.
3. Requests to non-licensebilling routes (e.g. /api/payment, /api/techgate):
   - Do NOT auto-populate cert_serial and return 401 if cert_serial is unset.
4. When terminal already has a different cert_serial:
   - Request with non-matching serial is rejected with 401 and does NOT overwrite cert_serial.
"""

import xml.etree.ElementTree as ET
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from starlette.requests import Request

from app.config import settings
from app.database import get_db
from app.dependencies import get_current_terminal
from app.main import app
from app.models import License, Terminal, TerminalCertHistory


def parse_xml(text: str) -> ET.Element:
    return ET.fromstring(text)


def get_text(xml: ET.Element, tag: str) -> str | None:
    el = xml.find(tag)
    return el.text if el is not None else None


@pytest.fixture(autouse=True)
def cleanup_overrides():
    orig_auto_set = settings.auto_set_cert_serial_on_licensebilling
    yield
    app.dependency_overrides.clear()
    settings.auto_set_cert_serial_on_licensebilling = orig_auto_set


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _make_mock_request(path: str, dn: str, serial: str) -> Request:
    scope = {
        "type": "http",
        "method": "GET",
        "path": path,
        "raw_path": path.encode("ascii"),
        "query_string": b"",
        "headers": [
            (b"x-client-cert-dn", dn.encode("utf-8")),
            (b"x-client-cert-serial", serial.encode("utf-8")),
        ],
    }
    return Request(scope)


@pytest.mark.anyio
async def test_auto_bind_cert_serial_on_licensebilling():
    """Terminal with cert_serial=None calls /api/licensebilling -> cert_serial is populated."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
        serial="52B8E528000400002E35",
    )

    terminal = Terminal(
        id=1671,
        device_id=773,
        sn="A99D2F18001ECC93DF5CBE27F442C8FA",
        cert_serial=None,
        org_id=1,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    # First query (exact match sn + cert_serial) returns None
    # Second query (sn match with cert_serial is None/empty) returns terminal
    exec_result_exact = MagicMock()
    exec_result_exact.scalar_one_or_none.return_value = None

    exec_result_fallback = MagicMock()
    exec_result_fallback.scalar_one_or_none.return_value = terminal

    mock_db.execute.side_effect = [exec_result_exact, exec_result_fallback]

    res_terminal = await get_current_terminal(req, mock_db)

    assert res_terminal is terminal
    assert terminal.cert_serial == "52B8E528000400002E35"
    mock_db.add.assert_called_once()
    added_obj = mock_db.add.call_args[0][0]
    assert isinstance(added_obj, TerminalCertHistory)
    assert added_obj.terminal_id == 1671
    assert added_obj.cert_serial == "52B8E528000400002E35"
    assert added_obj.source == "licensebilling"
    mock_db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_auto_bind_disabled_in_settings():
    """When auto_set_cert_serial_on_licensebilling=False, fallback is skipped and 401 is raised."""
    settings.auto_set_cert_serial_on_licensebilling = False

    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
        serial="52B8E528000400002E35",
    )

    mock_db = AsyncMock()
    exec_result = MagicMock()
    exec_result.scalar_one_or_none.return_value = None
    mock_db.execute.return_value = exec_result

    with pytest.raises(HTTPException) as exc_info:
        await get_current_terminal(req, mock_db)

    assert exc_info.value.status_code == 401
    assert "Terminal not found" in exc_info.value.detail
    mock_db.commit.assert_not_awaited()


@pytest.mark.anyio
async def test_auto_bind_not_performed_on_non_licensebilling_route():
    """Terminal with cert_serial=None calls /api/payment -> 401 without auto-bind."""
    req = _make_mock_request(
        path="/api/payment",
        dn="CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
        serial="52B8E528000400002E35",
    )

    mock_db = AsyncMock()
    exec_result = MagicMock()
    exec_result.scalar_one_or_none.return_value = None
    mock_db.execute.return_value = exec_result

    with pytest.raises(HTTPException) as exc_info:
        await get_current_terminal(req, mock_db)

    assert exc_info.value.status_code == 401
    mock_db.add.assert_not_called()
    mock_db.commit.assert_not_awaited()


@pytest.mark.anyio
async def test_mismatched_cert_serial_not_overwritten():
    """Terminal with existing cert_serial='AAA' calling with 'BBB' is rejected and not overwritten."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
        serial="BBB",
    )

    mock_db = AsyncMock()
    # Primary lookup (sn='CN...', cert_serial='BBB') -> None
    exec_result_exact = MagicMock()
    exec_result_exact.scalar_one_or_none.return_value = None

    # Fallback lookup (sn='CN...', cert_serial is None/empty) -> None (since DB has cert_serial='AAA')
    exec_result_fallback = MagicMock()
    exec_result_fallback.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [exec_result_exact, exec_result_fallback]

    with pytest.raises(HTTPException) as exc_info:
        await get_current_terminal(req, mock_db)

    assert exc_info.value.status_code == 401
    mock_db.add.assert_not_called()
    mock_db.commit.assert_not_awaited()


@pytest.mark.anyio
async def test_end_to_end_licensebilling_auto_bind():
    """End-to-end HTTP test: GET /api/licensebilling with legacy terminal without cert_serial."""
    terminal = Terminal(
        id=1671,
        device_id=773,
        sn="A99D2F18001ECC93DF5CBE27F442C8FA",
        cert_serial=None,
        org_id=1,
        is_active=True,
    )
    lic = License(
        id=1,
        terminal_id=1671,
        org_id=1,
        license_type="standard",
        expires_at=datetime.now(UTC) + timedelta(days=30),
        balance=50000,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()

    def execute_side_effect(stmt):
        stmt_str = str(stmt)
        res = MagicMock()
        if "IS NULL" in stmt_str:
            # Fallback auth query
            res.scalar_one_or_none.return_value = terminal
        elif "terminals.sn =" in stmt_str:
            # Exact auth query
            res.scalar_one_or_none.return_value = None
        elif "licenses" in stmt_str:
            res.scalar_one_or_none.return_value = lic
        elif "org_statuses" in stmt_str:
            res.scalar_one_or_none.return_value = None
        else:
            res.scalar_one_or_none.return_value = None
        return res

    mock_db.execute.side_effect = execute_side_effect

    async def override_get_db():
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get(
            "/api/licensebilling",
            headers={
                "X-Client-Cert-DN": "CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
                "X-Client-Cert-Serial": "52B8E528000400002E35",
            },
        )

    assert resp.status_code == 200
    assert "application/xml" in resp.headers.get("content-type", "")
    root = parse_xml(resp.text)
    assert get_text(root, "Result") == "OK"
    assert get_text(root, "state") == "ok"
    assert get_text(root, "balance") == "50000"

    assert terminal.cert_serial == "52B8E528000400002E35"
    mock_db.commit.assert_awaited()
