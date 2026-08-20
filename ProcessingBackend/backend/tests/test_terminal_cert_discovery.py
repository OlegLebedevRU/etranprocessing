"""Tests for terminal certificate discovery accumulator."""

from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock

import pytest
from fastapi import HTTPException
from starlette.requests import Request

from app.dependencies import get_current_terminal
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
) -> Request:
    headers = []
    if dn:
        headers.append((b"x-client-cert-dn", dn.encode("utf-8")))
    if serial:
        headers.append((b"x-client-cert-serial", serial.encode("utf-8")))
    if client_ip:
        headers.append((b"x-real-ip", client_ip.encode("utf-8")))

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
async def test_discovery_on_authenticated_terminal():
    """get_current_terminal records discovery when terminal authenticates."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=SN123,OU=773,O=1",
        serial="SER123",
    )

    terminal = Terminal(
        id=10,
        device_id=773,
        sn="SN123",
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
async def test_discovery_on_serial_mismatch():
    """get_current_terminal records serial_mismatch and db_cert_serial when serial differs."""
    req = _make_mock_request(
        path="/api/licensebilling",
        dn="CN=SN123,OU=773,O=1",
        serial="NEW_SERIAL_456",
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
    # 1. Exact match (sn='SN123', cert_serial='NEW_SERIAL_456') -> None
    exec_exact = MagicMock()
    exec_exact.scalar_one_or_none.return_value = None

    # 2. Fallback lookup (sn='SN123', cert_serial is None/empty) -> None
    exec_fallback = MagicMock()
    exec_fallback.scalar_one_or_none.return_value = None

    # 3. Diagnostic lookup (sn='SN123') -> db_terminal (serial mismatch!)
    exec_diag = MagicMock()
    exec_diag.scalar_one_or_none.return_value = db_terminal

    # 4. Discovery lookup -> None
    exec_disc = MagicMock()
    exec_disc.scalar_one_or_none.return_value = None

    mock_db.execute.side_effect = [exec_exact, exec_fallback, exec_diag, exec_disc]

    with pytest.raises(HTTPException) as exc:
        await get_current_terminal(req, mock_db)

    assert exc.value.status_code == 401
    mock_db.commit.assert_awaited()
