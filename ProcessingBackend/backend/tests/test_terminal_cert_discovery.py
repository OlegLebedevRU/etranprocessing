"""Tests for terminal certificate discovery accumulator."""

from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock

import pytest
from fastapi import HTTPException
from starlette.requests import Request

from app.dependencies import (
    extract_cert_not_valid_after,
    get_current_terminal,
)
from app.models import Terminal, TerminalCertDiscovery
from app.services.cert_discovery import record_terminal_discovery


@pytest.fixture
def anyio_backend():
    return "asyncio"


def _make_mock_request(
    path: str = "/api/licensebilling",
    dn: str = "",
    serial: str = "",
    client_ip: str = "127.0.0.1",
    issuer: str = "",
) -> Request:
    headers = []
    if dn:
        headers.append((b"x-client-cert-dn", dn.encode("utf-8")))
    if serial:
        headers.append((b"x-client-cert-serial", serial.encode("utf-8")))
    if client_ip:
        headers.append((b"x-real-ip", client_ip.encode("utf-8")))
    if issuer:
        headers.append((b"x-client-cert-issuer-dn", issuer.encode("utf-8")))

    scope = {
        "type": "http",
        "method": "GET",
        "path": path,
        "raw_path": path.encode("ascii"),
        "query_string": b"",
        "headers": headers,
        "client": ("127.0.0.1", 12345),
    }
    return Request(scope)


@pytest.mark.anyio
async def test_record_new_terminal_discovery():
    """Test inserting a new terminal cert discovery record."""
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    exec_result = MagicMock()
    exec_result.scalar_one_or_none.return_value = None
    mock_db.execute.return_value = exec_result

    entry = await record_terminal_discovery(
        mock_db,
        sn="TERM_001",
        cert_serial="SERIAL_AAA",
        cert_dn="CN=TERM_001,OU=10,O=1",
        ou="10",
        o="1",
        is_valid=True,
        validation_status="authenticated",
        terminal_id=42,
        db_cert_serial="SERIAL_AAA",
        endpoint="/api/licensebilling",
        client_ip="1.2.3.4",
    )

    assert entry is not None
    assert entry.sn == "TERM_001"
    assert entry.cert_serial == "SERIAL_AAA"
    assert entry.request_count == 1
    assert entry.is_valid is True
    assert entry.validation_status == "authenticated"
    assert entry.terminal_id == 42
    assert entry.client_ip == "1.2.3.4"
    mock_db.add.assert_called_once_with(entry)
    mock_db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_update_existing_terminal_discovery():
    """Test incrementing request_count on subsequent requests with same cert."""
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    existing = TerminalCertDiscovery(
        id=1,
        sn="TERM_001",
        cert_serial="SERIAL_AAA",
        request_count=5,
        is_valid=False,
        validation_status="serial_mismatch",
        first_seen_at=datetime.now(UTC),
        last_seen_at=datetime.now(UTC),
    )
    exec_result = MagicMock()
    exec_result.scalar_one_or_none.return_value = existing
    mock_db.execute.return_value = exec_result

    entry = await record_terminal_discovery(
        mock_db,
        sn="TERM_001",
        cert_serial="SERIAL_AAA",
        cert_dn="CN=TERM_001,OU=10,O=1",
        is_valid=True,
        validation_status="authenticated",
        terminal_id=42,
        endpoint="/api/gategauge",
    )

    assert entry is not None
    assert entry is existing
    assert entry.request_count == 6
    assert entry.is_valid is True
    assert entry.validation_status == "authenticated"
    assert entry.last_endpoint == "/api/gategauge"
    mock_db.add.assert_not_called()
    mock_db.commit.assert_awaited_once()


@pytest.mark.anyio
async def test_discovery_on_legacy_authenticated_terminal():
    """get_current_terminal records discovery when legacy terminal authenticates via OU/O."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=SN123,OU=773,O=1",
        serial="SER123",
        issuer="CN=SubCA",
    )

    terminal = Terminal(
        id=10,
        device_id=773,
        sn="a4b0000773c12345d210826",
        cert_serial="SER123",
        org_id=1,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    exec_term = MagicMock()
    exec_term.scalar_one_or_none.return_value = terminal

    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [exec_term, exec_disc]

    res = await get_current_terminal(req, mock_db)
    assert res is terminal
    mock_db.commit.assert_awaited()


@pytest.mark.anyio
async def test_discovery_on_new_ca_serial_mismatch():
    """get_current_terminal records serial_mismatch and db_cert_serial when serial differs on new CA."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=SN123,OU=773,O=1",
        serial="NEW_SERIAL_456",
        issuer="CN=iot.leo4.ru",
    )

    db_terminal = Terminal(
        id=10,
        device_id=773,
        sn="SN123",
        cert_serial="OLD_SERIAL_123",
        org_id=1,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()

    # 1. Strict match (sn='SN123', cert_serial='NEW_SERIAL_456') -> None
    exec_exact = MagicMock()
    exec_exact.scalar_one_or_none.return_value = None

    # 2. Diagnostic lookup (sn='SN123') -> db_terminal (serial mismatch!)
    exec_diag = MagicMock()
    exec_diag.scalar_one_or_none.return_value = db_terminal

    # 3. Discovery lookup -> None
    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [
        exec_exact,
        exec_diag,
        exec_disc,
    ]

    with pytest.raises(HTTPException) as exc:
        await get_current_terminal(req, mock_db)

    assert exc.value.status_code == 401
    mock_db.commit.assert_awaited()


@pytest.mark.anyio
async def test_discovery_on_legacy_auto_bind():
    """get_current_terminal authenticates and logs discovery via OU/O fallback for legacy certificates."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=D5DD9F6A7D29079A64668DE35E101263,OU=209,O=424",
        serial="11496",
        issuer="CN=SubCA",
    )

    db_terminal = Terminal(
        id=1076,
        device_id=209,
        sn="a4b0000209c12345d210826",
        cert_serial=None,
        org_id=424,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()

    # 1. OU/O lookup (device_id=209, org_id=424) -> db_terminal
    exec_ou = MagicMock()
    exec_ou.scalar_one_or_none.return_value = db_terminal

    # 2. Discovery lookup -> None
    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [
        exec_ou,
        exec_disc,
    ]

    res = await get_current_terminal(req, mock_db)
    assert res is db_terminal
    assert db_terminal.cert_serial == "11496"
    mock_db.commit.assert_awaited()


@pytest.mark.anyio
async def test_extract_cert_not_valid_after_header():
    """extract_cert_not_valid_after parses expiration date from header."""
    scope = {
        "type": "http",
        "method": "POST",
        "path": "/api/gategauge",
        "headers": [(b"x-client-cert-end", b"2028-12-31T23:59:59+00:00")],
    }
    req = Request(scope)
    dt = extract_cert_not_valid_after(req)
    assert dt is not None
    assert dt.year == 2028
    assert dt.month == 12
    assert dt.day == 31


@pytest.mark.anyio
async def test_legacy_auth_updates_cert_not_valid_after():
    """get_current_terminal updates terminal.cert_not_valid_after from request headers/cert."""
    scope = {
        "type": "http",
        "method": "POST",
        "path": "/api/gategauge",
        "headers": [
            (b"x-client-cert-dn", b"CN=D5DD9F6A,OU=209,O=424"),
            (b"x-client-cert-serial", b"11496"),
            (b"x-client-cert-end", b"2027-08-31T12:00:00+00:00"),
        ],
    }
    req = Request(scope)

    db_terminal = Terminal(
        id=1076,
        device_id=209,
        sn="a4b0000209c12345d210826",
        cert_serial="11496",
        cert_not_valid_after=None,
        org_id=424,
        is_active=True,
    )

    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    exec_ou = MagicMock()
    exec_ou.scalar_one_or_none.return_value = db_terminal
    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None
    mock_db.execute.side_effect = [exec_ou, exec_disc]

    res = await get_current_terminal(req, mock_db)
    assert res is db_terminal
    assert db_terminal.cert_not_valid_after == datetime(2027, 8, 31, 12, 0, tzinfo=UTC)
