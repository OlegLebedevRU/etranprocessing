"""Tests for TechGate router endpoints."""

import xml.etree.ElementTree as ET
from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.dependencies import get_current_terminal
from app.main import app
from app.models import Terminal


def parse_xml(text: str) -> ET.Element:
    return ET.fromstring(text)


def get_text(element: ET.Element, tag: str) -> str | None:
    node = element.find(tag)
    return node.text if node is not None else None


@pytest.fixture(autouse=True)
def cleanup_overrides():
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_techgate_devicestatus():
    terminal = Terminal(
        id=101,
        device_id=202,
        sn="SNTEST01",
        cert_serial="SERIAL01",
        org_id=424,
        is_active=True,
    )
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    mock_db.commit = AsyncMock()

    app.dependency_overrides[get_current_terminal] = lambda: terminal
    app.dependency_overrides[get_db] = lambda: mock_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/techgate?function=devicestatus")
        assert resp.status_code == 200
        assert resp.headers["content-type"].startswith("application/xml")
        xml = parse_xml(resp.text)
        assert get_text(xml, "Result") == "OK"


@pytest.mark.anyio
async def test_techgate_etran_ashx_inkass():
    terminal = Terminal(
        id=101,
        device_id=202,
        sn="SNTEST01",
        cert_serial="SERIAL01",
        org_id=424,
        is_active=True,
    )
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    mock_db.commit = AsyncMock()

    app.dependency_overrides[get_current_terminal] = lambda: terminal
    app.dependency_overrides[get_db] = lambda: mock_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/techgate/etran.ashx?function=inkass&InkassExtId=INK12345"
        )
        assert resp.status_code == 200
        assert resp.headers["content-type"].startswith("application/xml")
        xml = parse_xml(resp.text)
        assert get_text(xml, "Result") == "OK"
        assert get_text(xml, "PaymExtId") == "INK12345"


@pytest.mark.anyio
async def test_techgate_closeshift():
    terminal = Terminal(
        id=101,
        device_id=202,
        sn="SNTEST01",
        cert_serial="SERIAL01",
        org_id=424,
        is_active=True,
    )
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    mock_db.commit = AsyncMock()

    app.dependency_overrides[get_current_terminal] = lambda: terminal
    app.dependency_overrides[get_db] = lambda: mock_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/techgate/etran.ashx?function=closeshift&LastPaymExtId=PAYM999"
        )
        assert resp.status_code == 200
        xml = parse_xml(resp.text)
        assert get_text(xml, "Result") == "OK"
        assert get_text(xml, "PaymExtId") == "PAYM999"


@pytest.mark.anyio
async def test_techgate_getshiftreport():
    terminal = Terminal(
        id=101,
        device_id=202,
        sn="SNTEST01",
        cert_serial="SERIAL01",
        org_id=424,
        is_active=True,
    )
    mock_db = AsyncMock()
    mock_db.add = MagicMock()
    mock_db.commit = AsyncMock()

    app.dependency_overrides[get_current_terminal] = lambda: terminal
    app.dependency_overrides[get_db] = lambda: mock_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/techgate?function=getshiftreport")
        assert resp.status_code == 200
        xml = parse_xml(resp.text)
        assert get_text(xml, "Result") == "OK"
        assert get_text(xml, "ShiftReport/KioskNumber") == "SNTEST01"
