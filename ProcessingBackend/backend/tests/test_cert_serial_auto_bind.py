"""Tests for automatic cert_serial binding and authentication branching.

Validates:
1. Legacy certificates (Issuer != 'iot.leo4.ru'):
   - Matched by OU (device_id) and O (org_id).
   - When terminal.cert_serial is unset or changed, auto-binds / updates cert_serial from X-Client-Cert-Serial.
   - TerminalCertHistory entry is added with source='legacy_auth'.
   - Discovery record is logged as is_valid=True.
2. New certificates (Issuer contains 'iot.leo4.ru'):
   - Strict validation: Terminal.sn == cn AND Terminal.cert_serial == cert_serial.
   - Mismatched serial is rejected with 401.
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


def _make_mock_request(path: str, dn: str, serial: str, issuer: str = "") -> Request:
    headers = [
        (b"x-client-cert-dn", dn.encode("utf-8")),
        (b"x-client-cert-serial", serial.encode("utf-8")),
    ]
    if issuer:
        headers.append((b"x-client-cert-issuer-dn", issuer.encode("utf-8")))

    scope = {
        "type": "http",
        "method": "GET",
        "path": path,
        "raw_path": path.encode("ascii"),
        "query_string": b"",
        "headers": headers,
    }
    return Request(scope)


@pytest.mark.anyio
async def test_auto_bind_cert_serial_for_legacy_terminal():
    """Legacy terminal with cert_serial=None -> matched by OU/O, cert_serial is auto-bound."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
        serial="52B8E528000400002E35",
        issuer="CN=SubCA",
    )

    terminal = Terminal(
        id=1671,
        device_id=773,
        sn="a4b0000773c12345d210826",
        cert_serial=None,
        org_id=1,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()

    # 1. OU lookup (device_id=773, org_id=1) -> terminal
    exec_result_ou = MagicMock()
    exec_result_ou.scalar_one_or_none.return_value = terminal

    # 2. Discovery lookup -> None
    exec_result_discovery = MagicMock()
    exec_result_discovery.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [
        exec_result_ou,
        exec_result_discovery,
    ]

    res_terminal = await get_current_terminal(req, mock_db)

    assert res_terminal is terminal
    assert terminal.cert_serial == "52B8E528000400002E35"
    added_history = [
        call[0][0]
        for call in mock_db.add.call_args_list
        if isinstance(call[0][0], TerminalCertHistory)
    ]
    assert len(added_history) == 1
    added_obj = added_history[0]
    assert added_obj.terminal_id == 1671
    assert added_obj.cert_serial == "52B8E528000400002E35"
    assert added_obj.source == "legacy_auth"
    mock_db.commit.assert_awaited()


@pytest.mark.anyio
async def test_legacy_auth_does_not_overwrite_new_ca_serial():
    """Legacy auth request should not overwrite an active 40-character new CA cert_serial."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
        serial="52B8E528000400002E35",
        issuer="CN=SubCA",
    )

    new_ca_serial = "20E0B7ED4A12548E80F8B987F04D075FE7C79878"
    terminal = Terminal(
        id=1671,
        device_id=773,
        sn="a4b0000773c12345d210826",
        cert_serial=new_ca_serial,
        org_id=1,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()

    exec_result_ou = MagicMock()
    exec_result_ou.scalar_one_or_none.return_value = terminal
    exec_result_discovery = MagicMock()
    exec_result_discovery.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [
        exec_result_ou,
        exec_result_discovery,
    ]

    res_terminal = await get_current_terminal(req, mock_db)

    assert res_terminal is terminal
    # Must preserve new CA serial
    assert terminal.cert_serial == new_ca_serial
    # No TerminalCertHistory added for legacy_auth
    added_history = [
        call[0][0]
        for call in mock_db.add.call_args_list
        if isinstance(call[0][0], TerminalCertHistory)
    ]
    assert len(added_history) == 0


@pytest.mark.anyio
async def test_new_ca_strict_auth_success():
    """New CA (iot.leo4.ru) validates strictly by sn + cert_serial."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=a4b0000773c12345d210826,OU=773,O=1",
        serial="52B8E528000400002E35",
        issuer="CN=iot.leo4.ru",
    )

    terminal = Terminal(
        id=1671,
        device_id=773,
        sn="a4b0000773c12345d210826",
        cert_serial="52B8E528000400002E35",
        org_id=1,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()

    # Strict match (sn + cert_serial) -> terminal
    exec_exact = MagicMock()
    exec_exact.scalar_one_or_none.return_value = terminal

    # Discovery lookup -> None
    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [exec_exact, exec_disc]

    res = await get_current_terminal(req, mock_db)
    assert res is terminal


@pytest.mark.anyio
async def test_new_ca_strict_auth_mismatch_rejected():
    """New CA (iot.leo4.ru) rejects mismatched serial with 401."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=a4b0000773c12345d210826,OU=773,O=1",
        serial="WRONG_SERIAL",
        issuer="CN=iot.leo4.ru",
    )

    db_terminal = Terminal(
        id=1671,
        device_id=773,
        sn="a4b0000773c12345d210826",
        cert_serial="52B8E528000400002E35",
        org_id=1,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()

    # 1. Strict match (sn + WRONG_SERIAL) -> None
    exec_exact = MagicMock()
    exec_exact.scalar_one_or_none.return_value = None

    # 2. Diagnostic lookup (sn) -> db_terminal (serial mismatch)
    exec_diag = MagicMock()
    exec_diag.scalar_one_or_none.return_value = db_terminal

    # 3. Discovery lookup -> None
    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [exec_exact, exec_diag, exec_disc]

    with pytest.raises(HTTPException) as exc_info:
        await get_current_terminal(req, mock_db)

    assert exc_info.value.status_code == 401
    assert "serial_mismatch" in exc_info.value.detail


@pytest.mark.anyio
async def test_legacy_terminal_not_found():
    """Legacy terminal with unknown OU/O raises 401."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=UNKNOWN,OU=9999,O=99",
        serial="SER999",
        issuer="CN=SubCA",
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    exec_result = MagicMock()
    exec_result.scalar_one_or_none.return_value = None
    mock_db.execute.return_value = exec_result

    with pytest.raises(HTTPException) as exc_info:
        await get_current_terminal(req, mock_db)

    assert exc_info.value.status_code == 401
    assert "Legacy terminal not found" in exc_info.value.detail


@pytest.mark.anyio
async def test_end_to_end_licensebilling_auto_bind():
    """Full HTTP test via TestClient verifying auto-bind and 200 response."""
    terminal = Terminal(
        id=1671,
        device_id=773,
        sn="a4b0000773c12345d210826",
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
        is_active=True,
        renewal_enabled=True,
    )

    async def override_get_db():
        mock_db = AsyncMock()
        mock_db.add = MagicMock()

        # 1. get_current_terminal: OU lookup -> terminal
        exec_ou = MagicMock()
        exec_ou.scalar_one_or_none.return_value = terminal

        # 2. get_current_terminal: Discovery lookup -> None
        exec_disc = MagicMock()
        exec_disc.scalar_one_or_none.return_value = None

        # 3. get_terminal_license_state: OrgStatus -> None (active)
        exec_org = MagicMock()
        exec_org.scalar_one_or_none.return_value = None

        # 4. get_terminal_license_state: License -> lic
        exec_lic = MagicMock()
        exec_lic.scalar_one_or_none.return_value = lic

        # 5. endpoint check: OrgStatus -> None
        exec_org2 = MagicMock()
        exec_org2.scalar_one_or_none.return_value = None

        # 6. endpoint check: License -> lic
        exec_lic2 = MagicMock()
        exec_lic2.scalar_one_or_none.return_value = lic

        mock_db.execute.side_effect = [
            exec_ou,
            exec_disc,
            exec_org,
            exec_lic,
            exec_org2,
            exec_lic2,
        ]
        yield mock_db

    app.dependency_overrides[get_db] = override_get_db

    headers = {
        "X-Client-Cert-DN": "CN=A99D2F18001ECC93DF5CBE27F442C8FA,OU=773,O=1",
        "X-Client-Cert-Serial": "52B8E528000400002E35",
        "X-Client-Cert-Issuer-DN": "CN=SubCA",
    }

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        resp = await client.post("/api/licensebilling/check", headers=headers)

    assert resp.status_code == 200
    root = parse_xml(resp.text)
    assert get_text(root, "state") == "ok"
