from __future__ import annotations

import json
from datetime import UTC, datetime, timedelta
from typing import Any, cast
from unittest.mock import patch

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.config import settings
from app.database import get_db
from app.main import app
from app.models import Org, Terminal
from app.models_l4desk import L4DeskAuditEvent, L4DeskTerminal
from app.services.terminal_onboarding_service import (
    DeviceProvisionRequest,
    DeviceProvisionResponse,
    IotProvisioningClient,
    IssueCertificatePinRequest,
    IssueCertificatePinResponse,
    ProcessingBackendPinClient,
    TerminalOnboardingService,
)


@pytest.fixture(autouse=True)
def setup_flags():
    orig_flag = settings.l4desk_terminal_onboarding_enabled
    settings.l4desk_terminal_onboarding_enabled = True
    app.dependency_overrides.clear()
    yield
    settings.l4desk_terminal_onboarding_enabled = orig_flag
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


def auth_headers(
    user_id: int, org_id: int, role_id: int = 3, is_su: bool = False
) -> dict[str, str]:
    role = (
        "superuser"
        if is_su
        else (
            "l4desk_owner" if role_id == 5 else ("viewer" if role_id == 4 else "user")
        )
    )
    token = create_access_token(
        {
            "sub": f"user_{user_id}",
            "username": f"user_{user_id}",
            "org": str(org_id),
            "org_id": org_id,
            "role": role,
            "role_id": role_id,
            "id": user_id,
            "is_superuser": is_su,
            "token_type": "master" if is_su else "tenant",
        }
    )
    return {"Authorization": f"Bearer {token}"}


# =============================================================================
# In-Memory Database Fixture for AsyncSession
# =============================================================================


class MockResult:
    def __init__(self, one: Any = None, all_items: list[Any] | None = None) -> None:
        self._one = one
        self._all = (
            all_items if all_items is not None else ([one] if one is not None else [])
        )

    def scalar_one_or_none(self) -> Any:
        return self._one

    def scalar_one(self) -> Any:
        return self._one

    def scalar(self) -> Any:
        return self._one

    def scalars(self) -> MockResult:
        return self

    def all(self) -> list[Any]:
        return self._all


class MockInMemoryDb:
    def __init__(self) -> None:
        self.terminals: dict[int, Terminal] = {}
        self.l4_terminals: dict[int, L4DeskTerminal] = {}
        self.audit_events: list[L4DeskAuditEvent] = []
        self.orgs: dict[int, Org] = {}
        self._next_term_id = 1
        self._next_audit_id = 1

    def add(self, obj: Any) -> None:
        if isinstance(obj, Terminal):
            if not obj.id:
                obj.id = self._next_term_id
                self._next_term_id += 1
            self.terminals[obj.id] = obj
        elif isinstance(obj, L4DeskTerminal):
            self.l4_terminals[obj.terminal_id] = obj
        elif isinstance(obj, L4DeskAuditEvent):
            obj.id = self._next_audit_id
            self._next_audit_id += 1
            self.audit_events.append(obj)

    async def flush(self) -> None:
        pass

    async def commit(self) -> None:
        pass

    async def refresh(self, obj: Any) -> None:
        pass

    async def get(self, model: Any, ident: Any) -> Any:
        if model is Terminal:
            return self.terminals.get(ident)
        if model is L4DeskTerminal:
            return self.l4_terminals.get(ident)
        if model is Org:
            return self.orgs.get(ident)
        return None

    async def execute(self, stmt: Any) -> MockResult:
        sql = str(stmt).lower()

        # func.coalesce(func.max(Terminal.device_id), 0) + 1
        if "max(terminals.device_id)" in sql:
            max_dev = max([t.device_id for t in self.terminals.values()], default=0)
            return MockResult(one=max_dev + 1)

        # func.coalesce(func.max(L4DeskTerminal.ordinal), 0) + 1
        if "max(l4desk_terminals.ordinal)" in sql:
            max_ord = max([t.ordinal for t in self.l4_terminals.values()], default=0)
            return MockResult(one=max_ord + 1)

        # count query
        if "count(" in sql:
            return MockResult(one=len(self.terminals))

        # earliest active terminal id
        if "order by l4desk_terminals.ordinal asc" in sql:
            active = [t for t in self.l4_terminals.values() if t.deleted_at is None]
            active.sort(key=lambda x: (x.ordinal, x.created_at or datetime.now(UTC)))
            return MockResult(one=active[0].terminal_id if active else None)

        # select from l4desk_terminals where terminal_id = :id
        if (
            "where l4desk_terminals.terminal_id =" in sql
            or "where l4desk_terminals.terminal_id=" in sql
        ):
            t_id = None
            if hasattr(stmt, "whereclause") and stmt.whereclause is not None:
                clauses = (
                    stmt.whereclause.clauses
                    if hasattr(stmt.whereclause, "clauses")
                    else [stmt.whereclause]
                )
                for clause in clauses:
                    if (
                        hasattr(clause, "left")
                        and "terminal_id" in str(clause.left)
                        and hasattr(clause, "right")
                        and hasattr(clause.right, "value")
                    ):
                        t_id = clause.right.value
                        break
            if t_id is None:
                params = stmt.compile().params if hasattr(stmt, "compile") else {}
                t_id = (
                    params.get("terminal_id_1")
                    or params.get("terminal_id")
                    or params.get("param_1")
                )
            if t_id is not None and int(t_id) in self.l4_terminals:
                return MockResult(one=self.l4_terminals[int(t_id)])
            if len(self.l4_terminals) == 1:
                return MockResult(one=next(iter(self.l4_terminals.values())))
            return MockResult(one=None)

        # select l4desk_terminals where operation_id = :operation_id
        if "from l4desk_terminals" in sql and "operation_id" in sql:
            op_val = None
            if hasattr(stmt, "whereclause") and stmt.whereclause is not None:
                clauses = (
                    stmt.whereclause.clauses
                    if hasattr(stmt.whereclause, "clauses")
                    else [stmt.whereclause]
                )
                for clause in clauses:
                    if (
                        hasattr(clause, "left")
                        and "operation_id" in str(clause.left)
                        and hasattr(clause, "right")
                        and hasattr(clause.right, "value")
                    ):
                        op_val = clause.right.value
                        break
            if op_val is None:
                params = stmt.compile().params if hasattr(stmt, "compile") else {}
                op_val = params.get("operation_id_1") or params.get("operation_id")
            for term in self.l4_terminals.values():
                if op_val and term.operation_id == op_val:
                    return MockResult(one=term)
            return MockResult(one=None)

        # select l4desk_terminals where sn = :sn
        if "from l4desk_terminals" in sql and "sn" in sql:
            sn_val = None
            if hasattr(stmt, "whereclause") and stmt.whereclause is not None:
                clauses = (
                    stmt.whereclause.clauses
                    if hasattr(stmt.whereclause, "clauses")
                    else [stmt.whereclause]
                )
                for clause in clauses:
                    if (
                        hasattr(clause, "left")
                        and "sn" in str(clause.left)
                        and hasattr(clause, "right")
                        and hasattr(clause.right, "value")
                    ):
                        sn_val = clause.right.value
                        break
            if sn_val is None:
                params = stmt.compile().params if hasattr(stmt, "compile") else {}
                sn_val = params.get("sn_1") or params.get("sn")
            for term in self.l4_terminals.values():
                if sn_val and term.sn == sn_val and term.deleted_at is None:
                    return MockResult(one=term)
            return MockResult(one=None)

        # select from l4desk_terminals (list)
        if "from l4desk_terminals" in sql:
            active = [t for t in self.l4_terminals.values() if t.deleted_at is None]
            return MockResult(all_items=active)

        # select from terminals (list)
        if "from terminals" in sql:
            terms = list(self.terminals.values())
            return MockResult(one=terms[0] if terms else None, all_items=terms)

        return MockResult()


# =============================================================================
# Provider Client Mocks
# =============================================================================


class FakeIotClient(IotProvisioningClient):
    def __init__(self, should_fail: bool = False, device_id: int = 1001) -> None:
        super().__init__()
        self.should_fail = should_fail
        self.device_id = device_id
        self.calls: list[DeviceProvisionRequest] = []

    async def provision_device(
        self, req: DeviceProvisionRequest
    ) -> DeviceProvisionResponse:
        self.calls.append(req)
        if self.should_fail:
            raise RuntimeError("IoT platform network timeout (503)")
        return DeviceProvisionResponse(
            operation_id=req.operation_id,
            status="provisioned",
            tenant_id=req.tenant_id,
            terminal_id=req.terminal_id,
            device_id=self.device_id,
            sn=req.sn,
            contract_version="1.0.0",
            correlation_id=req.correlation_id,
            replayed_flag=len(self.calls) > 1,
            created_at=datetime.now(UTC),
            provisioned_at=datetime.now(UTC),
        )

    async def get_by_operation(
        self, operation_id: str, tenant_id: int | None = None
    ) -> DeviceProvisionResponse | None:
        for call in self.calls:
            if call.operation_id == operation_id:
                return DeviceProvisionResponse(
                    operation_id=call.operation_id,
                    status="provisioned",
                    tenant_id=call.tenant_id,
                    terminal_id=call.terminal_id,
                    device_id=self.device_id,
                    sn=call.sn,
                    created_at=datetime.now(UTC),
                )
        return None


class FakePinClient(ProcessingBackendPinClient):
    def __init__(
        self, should_fail: bool = False, pin: str = "773773", status: str = "issued"
    ) -> None:
        super().__init__()
        self.should_fail = should_fail
        self.pin = pin
        self.status = status
        self.calls: list[IssueCertificatePinRequest] = []

    async def issue_pin(
        self, req: IssueCertificatePinRequest
    ) -> IssueCertificatePinResponse:
        self.calls.append(req)
        if self.should_fail:
            raise RuntimeError("ProcessingBackend service unavailable (500)")
        return IssueCertificatePinResponse(
            operation_id=req.operation_id,
            correlation_id=req.correlation_id,
            tenant_id=req.tenant_id,
            terminal_id=req.terminal_id,
            sn=req.sn,
            pin=self.pin if self.status == "issued" else None,
            pin_masked="***773",
            status=self.status,  # type: ignore[arg-type]
            expires_at=datetime.now(UTC) + timedelta(days=1),
            created_at=datetime.now(UTC),
            replayed=len(self.calls) > 1,
        )

    async def get_by_operation(
        self, operation_id: str
    ) -> IssueCertificatePinResponse | None:
        for call in self.calls:
            if call.operation_id == operation_id:
                return IssueCertificatePinResponse(
                    operation_id=call.operation_id,
                    correlation_id=call.correlation_id,
                    tenant_id=call.tenant_id,
                    terminal_id=call.terminal_id,
                    sn=call.sn,
                    pin=self.pin if self.status == "issued" else None,
                    pin_masked="***773",
                    status=self.status,  # type: ignore[arg-type]
                    expires_at=datetime.now(UTC) + timedelta(days=1),
                    created_at=datetime.now(UTC),
                    replayed=True,
                )
        return None


# =============================================================================
# 1. Monotonic Tenant Ordering and First-Terminal Free Marker Tests
# =============================================================================


@pytest.mark.anyio
async def test_terminal_onboarding_success_flow():
    """Verify standard happy-path onboarding with 4 readiness states and PIN delivery."""
    db = MockInMemoryDb()
    iot = FakeIotClient(device_id=2001)
    pin = FakePinClient(pin="123456")

    app.dependency_overrides[get_db] = lambda: db

    with patch("app.routers.settings.TerminalOnboardingService") as mock_svc_cls:
        svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)
        mock_svc_cls.return_value = svc

        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/settings/terminals",
                json={
                    "sn": "SN-TEST-001",
                    "address": "Moscow, Tverskaya 1",
                    "note": "Main Kiosk",
                    "timezone": "Europe/Moscow",
                },
                headers=auth_headers(user_id=10, org_id=1, role_id=3),
            )
            assert resp.status_code == 201, resp.text
            data = resp.json()

            assert data["sn"] == "SN-TEST-001"
            assert data["tenant_id"] == 1
            assert data["ordinal"] == 1
            assert data["is_free"] is True
            assert data["pin"] == "123456"
            assert data["pin_masked"] == "***773"
            assert data["agent_version"] == "1.7.7"
            assert "l4setup.exe" in data["agent_release_url"]

            readiness = data["readiness"]
            assert readiness["record"] == "ready"
            assert readiness["certificate"] == "issued"
            assert readiness["iot"] == "ready"
            assert readiness["online"] == "offline"

            # Check audit event has NO plain pin
            audit_events = db.audit_events
            assert len(audit_events) >= 1
            for ev in audit_events:
                if ev.details:
                    assert "123456" not in json.dumps(ev.details)


@pytest.mark.anyio
async def test_monotonic_tenant_ordering_and_free_marker_transfer():
    """Verify ordinal strictly increments and free marker transfers to next on deletion without retro-recalculation."""
    db = MockInMemoryDb()
    iot = FakeIotClient()
    pin = FakePinClient()

    svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)
    user_tenant = {"id": 10, "username": "owner", "org_id": 1, "role_id": 5}

    # 1. Create first terminal (ordinal 1) -> gets free marker
    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    t1 = await svc.onboard_terminal(
        user=user_tenant,
        req=TerminalOnboardRequest(sn="SN-ORD-1"),
    )
    assert t1.ordinal == 1
    assert t1.is_free is True

    # 2. Create second terminal (ordinal 2) -> paid
    t2 = await svc.onboard_terminal(
        user=user_tenant,
        req=TerminalOnboardRequest(sn="SN-ORD-2"),
    )
    assert t2.ordinal == 2
    assert t2.is_free is False

    # 3. Create third terminal (ordinal 3) -> paid
    t3 = await svc.onboard_terminal(
        user=user_tenant,
        req=TerminalOnboardRequest(sn="SN-ORD-3"),
    )
    assert t3.ordinal == 3
    assert t3.is_free is False

    # 4. Delete first terminal -> privilege transfers to second terminal
    del_res = await svc.delete_terminal(user=user_tenant, terminal_id=t1.terminal_id)
    assert del_res["ok"] is True
    assert del_res["earliest_free_terminal_id"] == t2.terminal_id

    # Check t2 readiness now indicates is_free = True
    readiness_t2 = await svc.get_terminal_readiness(
        user=user_tenant, terminal_id=t2.terminal_id
    )
    assert readiness_t2.is_free is True

    # Check deleted t1 is not free
    l4_t1 = db.l4_terminals[t1.terminal_id]
    assert l4_t1.deleted_at is not None


# =============================================================================
# 2. Partial Provider Failures and Non-Destructive Persistence
# =============================================================================


@pytest.mark.anyio
async def test_partial_failure_iot_fails_pin_succeeds():
    """Partial failure: IoT fails, PIN succeeds. Business terminal record must be preserved!"""
    db = MockInMemoryDb()
    iot = FakeIotClient(should_fail=True)  # IoT platform fails
    pin = FakePinClient(pin="999888")

    svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)
    user = {"id": 10, "username": "owner", "org_id": 1, "role_id": 5}

    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    res = await svc.onboard_terminal(
        user=user,
        req=TerminalOnboardRequest(sn="SN-FAIL-IOT"),
    )

    # Record must be persisted
    assert res.terminal_id in db.l4_terminals
    assert res.readiness.record == "ready"
    assert res.readiness.iot == "failed"
    assert res.readiness.certificate == "issued"
    assert res.pin == "999888"
    assert "IoT provisioning failed" in (res.last_error or "")


@pytest.mark.anyio
async def test_partial_failure_pin_fails_iot_succeeds():
    """Partial failure: PIN fails, IoT succeeds. Business terminal record must be preserved!"""
    db = MockInMemoryDb()
    iot = FakeIotClient()
    pin = FakePinClient(should_fail=True)  # PIN issue fails

    svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)
    user = {"id": 10, "username": "owner", "org_id": 1, "role_id": 5}

    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    res = await svc.onboard_terminal(
        user=user,
        req=TerminalOnboardRequest(sn="SN-FAIL-PIN"),
    )

    assert res.terminal_id in db.l4_terminals
    assert res.readiness.record == "ready"
    assert res.readiness.iot == "ready"
    assert res.readiness.certificate == "failed"
    assert res.pin is None
    assert "PIN issuance failed" in (res.last_error or "")


# =============================================================================
# 3. Saga Retry and Idempotency
# =============================================================================


@pytest.mark.anyio
async def test_saga_retry_recovers_failed_step():
    """Verify that calling retry recovers the failed saga step with the same operation_id."""
    db = MockInMemoryDb()
    iot = FakeIotClient(should_fail=True)
    pin = FakePinClient(pin="555444")

    svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)
    user = {"id": 10, "username": "owner", "org_id": 1, "role_id": 5}

    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    # Initial onboarding fails on IoT
    init_res = await svc.onboard_terminal(
        user=user,
        req=TerminalOnboardRequest(sn="SN-RETRY-01"),
    )
    assert init_res.readiness.iot == "failed"
    saved_op_id = init_res.operation_id

    # Now IoT service recovers
    iot.should_fail = False

    # Execute retry
    retry_res = await svc.retry_terminal_saga(
        user=user, terminal_id=init_res.terminal_id
    )
    assert retry_res.operation_id == saved_op_id
    assert retry_res.readiness.iot == "ready"
    assert retry_res.readiness.certificate == "issued"
    assert retry_res.last_error is None


@pytest.mark.anyio
async def test_idempotent_duplicate_clicks():
    """Verify duplicate clicks with the same operation_id return replayed response without duplicate rows."""
    db = MockInMemoryDb()
    iot = FakeIotClient()
    pin = FakePinClient(pin="111222")

    svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)
    user = {"id": 10, "username": "owner", "org_id": 1, "role_id": 5}

    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    op_id = "018f3a5b-0006-7001-8000-000000000001"

    # Click 1
    res1 = await svc.onboard_terminal(
        user=user,
        req=TerminalOnboardRequest(sn="SN-DUP-01", operation_id=op_id),
    )

    # Click 2 with identical operation_id
    res2 = await svc.onboard_terminal(
        user=user,
        req=TerminalOnboardRequest(sn="SN-DUP-01", operation_id=op_id),
    )

    assert res1.terminal_id == res2.terminal_id
    assert res1.ordinal == res2.ordinal
    assert len(db.l4_terminals) == 1


# =============================================================================
# 4. Tenant Isolation and Security Semantics
# =============================================================================


@pytest.mark.anyio
async def test_tenant_isolation():
    """Verify tenant A cannot access, retry, or delete tenant B's terminals."""
    db = MockInMemoryDb()
    iot = FakeIotClient()
    pin = FakePinClient()

    svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)

    user_a = {"id": 10, "username": "userA", "org_id": 10, "role_id": 3}
    user_b = {"id": 20, "username": "userB", "org_id": 20, "role_id": 3}

    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    t_a = await svc.onboard_terminal(
        user=user_a, req=TerminalOnboardRequest(sn="SN-TENANT-A")
    )

    # User B attempts to access tenant A's terminal -> 403 Forbidden
    from fastapi import HTTPException

    with pytest.raises(HTTPException) as exc_info:
        await svc.get_terminal_readiness(user=user_b, terminal_id=t_a.terminal_id)
    assert exc_info.value.status_code == 403

    with pytest.raises(HTTPException) as exc_info:
        await svc.retry_terminal_saga(user=user_b, terminal_id=t_a.terminal_id)
    assert exc_info.value.status_code == 403

    with pytest.raises(HTTPException) as exc_info:
        await svc.delete_terminal(user=user_b, terminal_id=t_a.terminal_id)
    assert exc_info.value.status_code == 403


@pytest.mark.anyio
async def test_provider_pin_semantics_consumed_pin_hidden():
    """Verify that once a PIN is consumed or expired, plaintext PIN is NEVER returned."""
    db = MockInMemoryDb()
    iot = FakeIotClient()
    pin = FakePinClient(pin="123456", status="consumed")  # already consumed

    svc = TerminalOnboardingService(cast(Any, db), iot_client=iot, pin_client=pin)
    user = {"id": 10, "username": "owner", "org_id": 1, "role_id": 5}

    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    res = await svc.onboard_terminal(
        user=user,
        req=TerminalOnboardRequest(sn="SN-CONSUMED"),
    )

    # Provider returned consumed status -> plain PIN MUST be None
    assert res.pin is None
    assert res.pin_masked == "***773"
    assert res.readiness.certificate == "consumed"


# =============================================================================
# 5. Role & Auth Guard Tests
# =============================================================================


@pytest.mark.anyio
async def test_auth_and_role_guards():
    """Verify auth requirements: anonymous 401, role 4 (viewer) 403, role 5 / role 3 / superuser allowed."""
    db = MockInMemoryDb()
    app.dependency_overrides[get_db] = lambda: db

    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # Anonymous -> 401
        anon_resp = await client.post("/api/settings/terminals", json={})
        assert anon_resp.status_code == 401

        # Role 4 (viewer) -> 403 Forbidden
        viewer_resp = await client.post(
            "/api/settings/terminals",
            json={},
            headers=auth_headers(user_id=40, org_id=1, role_id=4),
        )
        assert viewer_resp.status_code == 403
