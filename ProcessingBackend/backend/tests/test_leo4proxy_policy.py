"""Policy contract exercised with the real terminal authentication dependency."""

from unittest.mock import AsyncMock, MagicMock

import pytest
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.dependencies import get_current_terminal
from app.main import app
from app.models import Terminal

SN = "policy-test-terminal"
SERIAL = "A" * 40
PATH = "/api/leo4proxy/policy"


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def policy_db(monkeypatch):
    database = AsyncMock()
    database.get.return_value = None

    async def override_db():
        yield database

    previous = app.dependency_overrides.copy()
    app.dependency_overrides.pop(get_current_terminal, None)
    app.dependency_overrides[get_db] = override_db
    monkeypatch.setattr("app.dependencies.record_terminal_discovery", AsyncMock())
    yield database
    app.dependency_overrides.clear()
    app.dependency_overrides.update(previous)


def result_for(terminal):
    result = MagicMock()
    result.scalar_one_or_none.return_value = terminal
    return result


def cert_headers(sn=SN, serial=SERIAL):
    return {
        "X-Client-Cert-DN": f"CN={sn}",
        "X-Client-Cert-Serial": serial,
        "X-Client-Cert-Issuer-DN": "CN=iot.leo4.ru",
    }


@pytest.mark.anyio
@pytest.mark.parametrize("is_active", [True, False])
async def test_policy_uses_only_authenticated_terminal_activity(policy_db, is_active):
    terminal = Terminal(
        id=1,
        device_id=1,
        sn=SN,
        cert_serial=SERIAL,
        org_id=1,
        is_active=is_active,
    )
    policy_db.execute.return_value = result_for(terminal)
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        response = await client.get(
            PATH, headers=cert_headers(), params={"sn": "other"}
        )

    assert response.status_code == 200
    assert response.headers["cache-control"] == "no-store"
    assert response.json() == {
        "v": 1,
        "sn": SN,
        "mqtt_rtp_allowed": is_active,
        "outgoing_https_allowed": True,
        "stop_facts": [] if is_active else ["terminal_inactive"],
    }
    # Authentication queries only the terminal: no license or organization gate.
    policy_db.execute.assert_awaited_once()
    statement = policy_db.execute.call_args.args[0]
    assert set(statement.compile().params.values()) == {SN, SERIAL}


@pytest.mark.anyio
async def test_policy_rejects_missing_certificate(policy_db):
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        response = await client.get(PATH)

    assert response.status_code == 401
    assert "mqtt_rtp_allowed" not in response.json()
    policy_db.execute.assert_not_awaited()


@pytest.mark.anyio
@pytest.mark.parametrize("known_sn", [True, False])
async def test_policy_rejects_unmatched_certificate_identity(policy_db, known_sn):
    terminal = Terminal(
        id=1, device_id=1, sn=SN, cert_serial=SERIAL, org_id=1, is_active=True
    )
    policy_db.execute.side_effect = [
        result_for(None),
        result_for(terminal if known_sn else None),
    ]
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        response = await client.get(
            PATH,
            headers=cert_headers(sn=SN if known_sn else "unknown", serial="B" * 40),
        )

    assert response.status_code == 401
    assert "mqtt_rtp_allowed" not in response.json()
    reason = "serial_mismatch" if known_sn else "terminal_not_found"
    assert reason in response.json()["detail"]


def test_policy_is_published_in_openapi():
    operation = app.openapi()["paths"][PATH]["get"]
    assert operation["responses"]["200"]["content"]["application/json"]["schema"] == {
        "$ref": "#/components/schemas/Leo4ProxyPolicy"
    }


@pytest.mark.anyio
@pytest.mark.parametrize("free,enabled,days,deleted,allowed", [
    (True,False,None,False,True),
    (False,False,10,False,False),
    (False,True,None,False,False),
    (False,True,10,False,True),
    (False,True,-1,False,True),
    (False,True,-4,False,False),
    (True,True,10,True,False),
])
async def test_transport_matches_subscription_admission(monkeypatch,free,enabled,days,deleted,allowed):
    from datetime import UTC,datetime,timedelta
    from app.config import settings
    from app.services.leo4proxy_policy import subscription_allowance,get_leo4proxy_policy
    from etranprocessing_db.l4desk import L4DeskTenantProfile,L4DeskTerminal
    now=datetime.now(UTC)
    db=AsyncMock()
    db.get.return_value=L4DeskTenantProfile(tenant_id=1,timezone="UTC")
    record=L4DeskTerminal(terminal_id=2,tenant_id=1,ordinal=2,paid_until=now+timedelta(days=days) if days is not None else None,deleted_at=now if deleted else None)
    db.scalar.side_effect=[record,2 if free else 1,None]
    monkeypatch.setattr(settings,"yookassa_enabled",enabled)
    terminal=Terminal(id=2,device_id=2,org_id=1,sn="scoped-terminal",is_active=True)
    actual=await subscription_allowance(db,terminal)
    assert actual is allowed
    assert get_leo4proxy_policy(terminal,actual).outgoing_https_allowed
    terminal.is_active=False
    assert not get_leo4proxy_policy(terminal,actual).mqtt_rtp_allowed
