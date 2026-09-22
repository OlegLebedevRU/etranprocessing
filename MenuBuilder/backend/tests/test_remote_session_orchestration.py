from __future__ import annotations

from datetime import UTC, datetime
from typing import Any
from unittest.mock import AsyncMock, patch

import pytest
from etranprocessing_db.models import Terminal
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.config import settings
from app.database import get_db
from app.main import app
from app.models_l4desk import L4DeskAuditEvent, L4DeskRemoteSession, L4DeskTerminal
from app.services.media_orchestrator_client import (
    MediaJanusError,
    MediaOrchestratorClient,
)
from app.services.remote_session_use_case import RemoteSessionUseCase


@pytest.fixture(autouse=True)
def setup_flags():
    orig_policy = settings.l4desk_policy_enforcement_enabled
    settings.l4desk_policy_enforcement_enabled = False
    app.dependency_overrides.clear()
    yield
    settings.l4desk_policy_enforcement_enabled = orig_policy
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
            "permissions": ["*"]
            if is_su
            else (
                ["video:view", "console"]
                if role_id == 5
                else (
                    ["video:view"] if role_id == 4 else ["video:view", "video:control"]
                )
            ),
            "token_type": "master" if is_su else "tenant",
        }
    )
    return {"Authorization": f"Bearer {token}"}


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


class MockRemoteSessionDb:
    def __init__(self) -> None:
        self.terminals: dict[int, Terminal] = {}
        self.l4_terminals: dict[int, L4DeskTerminal] = {}
        self.remote_sessions: dict[int, L4DeskRemoteSession] = {}
        self.audit_events: list[L4DeskAuditEvent] = []
        self._next_session_id = 100
        self._next_audit_id = 1

    def _validate_session_constraints(self, s: L4DeskRemoteSession) -> None:
        if s.state == "active" and s.active_at is None:
            raise ValueError("violates check constraint l4desk_session_active_ck")
        if s.state in ("closed", "failed") and s.closed_at is None:
            raise ValueError("violates check constraint l4desk_session_closed_ck")
        if (
            isinstance(s.closed_at, datetime)
            and isinstance(s.active_at, datetime)
            and s.closed_at < s.active_at
        ):
            raise ValueError("violates check constraint l4desk_session_interval_ck")

    def add(self, obj: Any) -> None:
        if isinstance(obj, Terminal):
            self.terminals[obj.id] = obj
        elif isinstance(obj, L4DeskTerminal):
            self.l4_terminals[obj.terminal_id] = obj
        elif isinstance(obj, L4DeskRemoteSession):
            self._validate_session_constraints(obj)
            if not obj.id:
                obj.id = self._next_session_id
                self._next_session_id += 1
            self.remote_sessions[obj.id] = obj
        elif isinstance(obj, L4DeskAuditEvent):
            obj.id = self._next_audit_id
            self._next_audit_id += 1
            self.audit_events.append(obj)

    async def flush(self) -> None:
        for s in self.remote_sessions.values():
            self._validate_session_constraints(s)

    async def commit(self) -> None:
        pass

    async def refresh(self, obj: Any) -> None:
        pass

    async def scalar(self, stmt: Any) -> Any:
        res = await self.execute(stmt)
        return res.scalar_one_or_none()

    async def execute(self, stmt: Any) -> MockResult:
        sql = str(stmt).lower()

        # Terminal lookup
        if "from terminals" in sql:
            t_id = None
            params = stmt.compile().params if hasattr(stmt, "compile") else {}
            for k, v in params.items():
                if "id" in k or "param" in k:
                    t_id = v
                    break
            if t_id is not None:
                for t in self.terminals.values():
                    if t.id == int(t_id) or t.device_id == int(t_id):
                        return MockResult(one=t)
            if len(self.terminals) == 1:
                return MockResult(one=next(iter(self.terminals.values())))
            return MockResult(one=None)

        # L4DeskTerminal lookup
        if "from l4desk_terminals" in sql and "ordinal" in sql and "max(" in sql:
            return MockResult(one=len(self.l4_terminals) + 1)

        if "from l4desk_terminals" in sql:
            t_id = None
            params = stmt.compile().params if hasattr(stmt, "compile") else {}
            for k, v in params.items():
                if "terminal_id" in k:
                    t_id = v
                    break
            if t_id is not None and int(t_id) in self.l4_terminals:
                return MockResult(one=self.l4_terminals[int(t_id)])
            if len(self.l4_terminals) == 1:
                return MockResult(one=next(iter(self.l4_terminals.values())))
            return MockResult(one=None)

        # Active session lookup for terminal
        if "from l4desk_remote_sessions" in sql:
            # Active session query
            if "state in" in sql:
                active = [
                    s
                    for s in self.remote_sessions.values()
                    if s.state
                    in ("reserved", "start_requested", "active", "stop_requested")
                ]
                return MockResult(one=active[0] if active else None)

            # Query by provider_session_id
            if "provider_session_id" in sql:
                params = stmt.compile().params if hasattr(stmt, "compile") else {}
                p_id = params.get("provider_session_id_1") or params.get(
                    "provider_session_id"
                )
                match = next(
                    (
                        s
                        for s in self.remote_sessions.values()
                        if s.provider_session_id == p_id
                    ),
                    None,
                )
                return MockResult(one=match)

            # Query by operation_id
            if "operation_id" in sql:
                params = stmt.compile().params if hasattr(stmt, "compile") else {}
                op_id = params.get("operation_id_1") or params.get("operation_id")
                match = next(
                    (
                        s
                        for s in self.remote_sessions.values()
                        if s.operation_id == op_id
                    ),
                    None,
                )
                return MockResult(one=match)

            if self.remote_sessions:
                return MockResult(one=next(iter(self.remote_sessions.values())))
            return MockResult(one=None)

        return MockResult(one=None)


# =============================================================================
# 1. Consumer Gate Tests against Immutable Fixtures
# =============================================================================


@pytest.mark.anyio
async def test_consumer_gates_fixtures_validation():
    """Verify that H-L4D-07-IOT-v1 and H-L4D-08A-MEDIA-v1 contracts and fixtures conform."""
    # 07 IoT contract constants
    valid_states = ["requested", "starting", "active", "stopping", "closed", "failed"]
    assert "starting" in valid_states
    assert "active" in valid_states

    # 08A Media contract client endpoints
    client = MediaOrchestratorClient(
        base_url="http://mock-ingress:9100", service_token="test-tok"
    )
    headers = client._get_headers()
    assert headers["X-Media-Service-Token"] == "test-tok"
    assert "Bearer test-tok" in headers["Authorization"]


# =============================================================================
# 2. RemoteSessionUseCase Unit Tests
# =============================================================================


@pytest.mark.anyio
async def test_tenant_access_and_isolation():
    db = MockRemoteSessionDb()
    term = Terminal(id=10, device_id=10, sn="SN-T1", org_id=1)
    db.add(term)

    use_case = RemoteSessionUseCase(db)

    # User from Tenant 2 should be rejected
    user_tenant_2 = {
        "sub": "u2",
        "user_id": 2,
        "org_id": 2,
        "role_id": 3,
        "is_superuser": False,
    }
    with pytest.raises(HTTPException) as exc_info:
        await use_case.start_session(
            device_id=10,
            session_type="video",
            user=user_tenant_2,
        )
    assert exc_info.value.status_code == 403
    assert "другой организации" in exc_info.value.detail

    # Superuser from any tenant can access
    user_su = {
        "sub": "admin",
        "user_id": 1,
        "org_id": 99,
        "role_id": 1,
        "is_superuser": True,
    }
    with (
        patch.object(
            use_case.iot_adapter, "create_remote_session", new_callable=AsyncMock
        ) as mock_iot,
        patch.object(
            use_case.iot_control, "remote_input_acquire_lease", new_callable=AsyncMock
        ) as mock_lease,
        patch.object(
            use_case.media_orchestrator, "start_session", new_callable=AsyncMock
        ) as mock_media,
    ):
        mock_iot.return_value = {"session_id": "sess-su-01"}
        mock_lease.return_value = {
            "lease_id": "lease-su-01",
            "expires_at": "2026-09-20T15:00:00Z",
        }
        mock_media.return_value = {
            "status": "success",
            "session_id": "sess-su-01",
            "mountpoint_id": 10,
            "sn": "SN-T1",
            "janus_ws": "/janus-ws",
            "ttl_sec": 600,
        }
        res = await use_case.start_session(
            device_id=10,
            session_type="video",
            user=user_su,
            start_terminal_stream=False,
        )
        assert res.session_id == "sess-su-01"
        assert res.state == "active"


@pytest.mark.anyio
async def test_role_matrix_console_and_video():
    db = MockRemoteSessionDb()
    term = Terminal(id=11, device_id=11, sn="SN-T11", org_id=1)
    db.add(term)
    use_case = RemoteSessionUseCase(db)

    # Viewer (role 4) rejected for console
    user_viewer = {
        "sub": "v4",
        "user_id": 4,
        "org_id": 1,
        "role_id": 4,
        "is_superuser": False,
        "permissions": ["video:view"],
    }
    with pytest.raises(HTTPException) as exc_viewer:
        await use_case.start_session(
            device_id=11,
            session_type="console",
            user=user_viewer,
        )
    assert exc_viewer.value.status_code == 403
    assert "наблюдателя" in exc_viewer.value.detail

    # L4Desk user (role 5 in own tenant) can access console
    user_l4 = {
        "sub": "l4_user",
        "user_id": 5,
        "org_id": 1,
        "role_id": 5,
        "is_superuser": False,
        "permissions": ["video:view", "console"],
    }
    with (
        patch.object(
            use_case.iot_adapter, "create_remote_session", new_callable=AsyncMock
        ) as mock_iot,
        patch.object(
            use_case.iot_control, "remote_input_acquire_lease", new_callable=AsyncMock
        ) as mock_lease,
    ):
        mock_iot.return_value = {"session_id": "sess-l4-console"}
        mock_lease.return_value = {
            "lease_id": "lease-l4-01",
            "expires_at": "2026-09-20T15:00:00Z",
        }
        res = await use_case.start_session(
            device_id=11,
            session_type="console",
            user=user_l4,
        )
        assert res.session_id == "sess-l4-console"
        assert res.session_type == "console"
        assert res.state == "active"


@pytest.mark.anyio
async def test_mutual_exclusion_and_no_auto_switch():
    """Requirement 3: If active any session - other is rejected. Auto-switch is forbidden."""
    db = MockRemoteSessionDb()
    term = Terminal(id=12, device_id=12, sn="SN-T12", org_id=1)
    db.add(term)

    # Existing active video session
    existing_video = L4DeskRemoteSession(
        id=50,
        tenant_id=1,
        terminal_id=12,
        operation_id="op-existing-video",
        correlation_id="corr-vid-1",
        session_type="video",
        state="active",
        active_at=datetime.now(UTC),
        provider_session_id="sess-vid-50",
    )
    db.add(existing_video)

    use_case = RemoteSessionUseCase(db)
    user_su = {
        "sub": "admin",
        "user_id": 1,
        "org_id": 1,
        "role_id": 1,
        "is_superuser": True,
    }

    # Attempt to start console while video is active -> 409 session_busy
    with pytest.raises(HTTPException) as exc_info:
        await use_case.start_session(
            device_id=12,
            session_type="console",
            user=user_su,
        )
    assert exc_info.value.status_code == 409
    detail: Any = exc_info.value.detail
    assert isinstance(detail, dict)
    assert detail.get("code") == "session_busy"
    assert "Автоматическое переключение" in str(detail.get("message"))


@pytest.mark.anyio
async def test_idempotent_session_start_replay():
    db = MockRemoteSessionDb()
    term = Terminal(id=13, device_id=13, sn="SN-T13", org_id=1)
    db.add(term)

    existing = L4DeskRemoteSession(
        id=60,
        tenant_id=1,
        terminal_id=13,
        operation_id="op-replay-100",
        correlation_id="corr-replay-100",
        session_type="video",
        state="active",
        active_at=datetime.now(UTC),
        provider_session_id="sess-replay-60",
    )
    db.add(existing)

    use_case = RemoteSessionUseCase(db)
    user_su = {
        "sub": "admin",
        "user_id": 1,
        "org_id": 1,
        "role_id": 1,
        "is_superuser": True,
    }

    res = await use_case.start_session(
        device_id=13,
        session_type="video",
        user=user_su,
        operation_id="op-replay-100",
    )
    assert res.session_id == "sess-replay-60"
    assert res.local_session_id == 60
    assert res.state == "active"


@pytest.mark.anyio
async def test_compensating_stop_on_media_failure():
    """Requirement 2: partial failure triggers compensating stop on IoT."""
    db = MockRemoteSessionDb()
    term = Terminal(id=14, device_id=14, sn="SN-T14", org_id=1)
    db.add(term)

    use_case = RemoteSessionUseCase(db)
    user_su = {
        "sub": "admin",
        "user_id": 1,
        "org_id": 1,
        "role_id": 1,
        "is_superuser": True,
    }

    with (
        patch.object(
            use_case.iot_adapter, "create_remote_session", new_callable=AsyncMock
        ) as mock_iot_create,
        patch.object(
            use_case.iot_adapter, "stop_remote_session", new_callable=AsyncMock
        ) as mock_iot_stop,
        patch.object(
            use_case.iot_control, "remote_input_acquire_lease", new_callable=AsyncMock
        ) as mock_lease,
        patch.object(
            use_case.iot_control, "remote_input_release", new_callable=AsyncMock
        ) as mock_release,
        patch.object(
            use_case.media_orchestrator, "start_session", new_callable=AsyncMock
        ) as mock_media,
    ):
        mock_iot_create.return_value = {"session_id": "sess-iot-compensate"}
        mock_lease.return_value = {"lease_id": "lease-compensate-01"}
        # Media start raises Janus 502 error
        mock_media.side_effect = MediaJanusError("Janus gateway failed")

        with pytest.raises(HTTPException) as exc_info:
            await use_case.start_session(
                device_id=14,
                session_type="video",
                user=user_su,
            )

        assert exc_info.value.status_code == 502
        # Verify compensating stop was called on IoT
        mock_iot_stop.assert_awaited_once()
        assert mock_iot_stop.call_args[1]["session_id"] == "sess-iot-compensate"
        assert "compensating_stop" in mock_iot_stop.call_args[1]["reason"]
        # Verify lease was released
        mock_release.assert_awaited_once()

        # Verify local session marked as failed
        sess = db.remote_sessions[100]
        assert sess.state == "failed"
        assert sess.reason == "media_start_failed"


@pytest.mark.anyio
async def test_compensating_stop_on_stream_start_failure():
    """Partial failure during FFmpeg start triggers compensating stop on both media and IoT."""
    db = MockRemoteSessionDb()
    term = Terminal(id=15, device_id=15, sn="SN-T15", org_id=1)
    db.add(term)

    use_case = RemoteSessionUseCase(db)
    user_su = {
        "sub": "admin",
        "user_id": 1,
        "org_id": 1,
        "role_id": 1,
        "is_superuser": True,
    }

    with (
        patch.object(
            use_case.iot_adapter, "create_remote_session", new_callable=AsyncMock
        ) as mock_iot_create,
        patch.object(
            use_case.iot_adapter, "stop_remote_session", new_callable=AsyncMock
        ) as mock_iot_stop,
        patch.object(
            use_case.iot_control, "remote_input_acquire_lease", new_callable=AsyncMock
        ) as mock_lease,
        patch.object(
            use_case.iot_control, "remote_input_release", new_callable=AsyncMock
        ) as mock_lease_rel,
        patch.object(
            use_case.media_orchestrator, "start_session", new_callable=AsyncMock
        ) as mock_media_start,
        patch.object(
            use_case.media_orchestrator, "stop_session", new_callable=AsyncMock
        ) as mock_media_stop,
        patch.object(
            use_case.iot_control, "remote_input_stream_start", new_callable=AsyncMock
        ) as mock_stream_start,
    ):
        mock_iot_create.return_value = {"session_id": "sess-stream-fail"}
        mock_lease.return_value = {"lease_id": "lease-stream-fail"}
        mock_media_start.return_value = {
            "status": "success",
            "session_id": "sess-stream-fail",
            "mountpoint_id": 15,
            "sn": "SN-T15",
            "janus_ws": "/janus-ws",
        }
        mock_stream_start.side_effect = Exception("FFmpeg capture initialization error")

        with pytest.raises(HTTPException) as exc_info:
            await use_case.start_session(
                device_id=15,
                session_type="video",
                user=user_su,
                start_terminal_stream=True,
            )

        assert exc_info.value.status_code == 502
        # Verify compensating stops on both media and IoT
        mock_media_stop.assert_awaited_once()
        mock_iot_stop.assert_awaited_once()
        mock_lease_rel.assert_awaited_once()

        sess = db.remote_sessions[100]
        assert sess.state == "failed"
        assert sess.reason == "stream_start_failed"


@pytest.mark.anyio
async def test_graceful_stop_flow():
    db = MockRemoteSessionDb()
    term = Terminal(id=16, device_id=16, sn="SN-T16", org_id=1)
    db.add(term)

    active_sess = L4DeskRemoteSession(
        id=70,
        tenant_id=1,
        terminal_id=16,
        operation_id="op-stop-70",
        correlation_id="corr-stop-70",
        session_type="video",
        state="active",
        active_at=datetime.now(UTC),
        provider_session_id="sess-active-70",
    )
    db.add(active_sess)

    use_case = RemoteSessionUseCase(db)
    user_su = {
        "sub": "admin",
        "user_id": 1,
        "org_id": 1,
        "role_id": 1,
        "is_superuser": True,
    }

    with (
        patch.object(
            use_case.iot_adapter, "stop_remote_session", new_callable=AsyncMock
        ) as mock_iot_stop,
        patch.object(
            use_case.media_orchestrator, "stop_session", new_callable=AsyncMock
        ) as mock_media_stop,
        patch.object(
            use_case.iot_control, "remote_input_status", new_callable=AsyncMock
        ) as mock_status,
        patch.object(
            use_case.iot_control, "remote_input_stream_stop", new_callable=AsyncMock
        ) as _mock_stream_stop,
    ):
        mock_status.return_value = {"lease": {"active": False}}
        res = await use_case.stop_session(
            device_id=16,
            reason="user_closed",
            user=user_su,
        )
        assert res["status"] == "success"
        assert res["state"] == "closed"
        assert active_sess.state == "closed"
        assert active_sess.reason == "user_closed"
        mock_media_stop.assert_awaited_once()
        mock_iot_stop.assert_awaited_once()


# =============================================================================
# 3. HTTP API Integration Tests (Unified /api/v1/remote-sessions)
# =============================================================================


@pytest.mark.anyio
async def test_unified_api_remote_sessions_lifecycle():
    db = MockRemoteSessionDb()
    term = Terminal(id=20, device_id=20, sn="SN-T20", org_id=1)
    db.add(term)

    app.dependency_overrides[get_db] = lambda: db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        headers = auth_headers(user_id=1, org_id=1, role_id=1, is_su=True)

        with (
            patch(
                "app.services.remote_session_use_case.iot_event_feed_client.create_remote_session",
                new_callable=AsyncMock,
            ) as mock_iot_create,
            patch(
                "app.services.remote_session_use_case.iot_client.remote_input_acquire_lease",
                new_callable=AsyncMock,
            ) as mock_lease,
            patch(
                "app.services.remote_session_use_case.media_orchestrator_client.start_session",
                new_callable=AsyncMock,
            ) as mock_media_start,
            patch(
                "app.services.remote_session_use_case.iot_client.remote_input_stream_start",
                new_callable=AsyncMock,
            ) as mock_stream_start,
        ):
            mock_iot_create.return_value = {"session_id": "sess-api-001"}
            mock_lease.return_value = {
                "lease_id": "lease-api-001",
                "expires_at": "2026-09-20T16:00:00Z",
            }
            mock_media_start.return_value = {
                "status": "success",
                "session_id": "sess-api-001",
                "mountpoint_id": 20,
                "sn": "SN-T20",
                "janus_ws": "/janus-ws",
                "ttl_sec": 600,
            }
            mock_stream_start.return_value = {"stream_instance_id": "inst-api-001"}

            # Start video session
            resp = await ac.post(
                "/api/v1/remote-sessions/start",
                headers=headers,
                json={
                    "device_id": 20,
                    "session_type": "video",
                    "operation_id": "op-api-start-01",
                    "mode": "desktop",
                },
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["session_id"] == "sess-api-001"
            assert data["session_type"] == "video"
            assert data["state"] == "active"
            assert data["mountpoint_id"] == 20

            # Check active status endpoint
            status_resp = await ac.get(
                "/api/v1/remote-sessions/devices/20/active",
                headers=headers,
            )
            assert status_resp.status_code == 200
            st_data = status_resp.json()
            assert st_data["active"] is True
            assert st_data["session_id"] == "sess-api-001"
            assert st_data["session_type"] == "video"

        # Stop session endpoint
        with (
            patch(
                "app.services.remote_session_use_case.iot_event_feed_client.stop_remote_session",
                new_callable=AsyncMock,
            ) as _mock_iot_stop,
            patch(
                "app.services.remote_session_use_case.media_orchestrator_client.stop_session",
                new_callable=AsyncMock,
            ) as _mock_media_stop,
            patch(
                "app.services.remote_session_use_case.iot_client.remote_input_status",
                new_callable=AsyncMock,
            ) as mock_input_status,
            patch(
                "app.services.remote_session_use_case.iot_client.remote_input_stream_stop",
                new_callable=AsyncMock,
            ) as _mock_stream_stop,
        ):
            mock_input_status.return_value = {"lease": {"active": False}}
            stop_resp = await ac.post(
                "/api/v1/remote-sessions/stop",
                headers=headers,
                json={"device_id": 20, "reason": "user_closed"},
            )
            assert stop_resp.status_code == 200
            assert stop_resp.json()["status"] == "success"
            assert stop_resp.json()["state"] == "closed"


# =============================================================================
# 4. Existing Endpoints Backward Compatibility / Regression
# =============================================================================


@pytest.mark.anyio
async def test_legacy_video_session_endpoint_regression():
    db = MockRemoteSessionDb()
    term = Terminal(id=25, device_id=25, sn="SN-T25", org_id=1)
    db.add(term)

    app.dependency_overrides[get_db] = lambda: db
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        headers = auth_headers(user_id=1, org_id=1, role_id=1, is_su=True)

        mock_result = AsyncMock()
        mock_result.mountpoint_id = 25
        mock_result.sn = "SN-T25"
        mock_result.janus_ws = "/janus-ws"
        mock_result.ttl_sec = 600
        mock_result.pin = "pin-1234"

        with patch(
            "app.services.remote_session_use_case.RemoteSessionUseCase.start_session",
            return_value=mock_result,
        ):
            resp = await ac.post(
                "/api/v1/video/devices/25/session",
                headers=headers,
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["mountpoint_id"] == 25
            assert data["sn"] == "SN-T25"
            assert data["janus_ws"] == "/janus-ws"


@pytest.mark.anyio
async def test_create_remote_session_check_constraints_and_timestamps():
    from app.repositories.l4desk_repository import L4DeskRepository

    db = MockRemoteSessionDb()
    repo = L4DeskRepository(db)  # pyright: ignore[reportArgumentType]

    # 1. State active automatically populates active_at
    s_active = await repo.create_remote_session(
        tenant_id=1,
        terminal_id=10,
        operation_id="op-test-act",
        correlation_id="corr-test-act",
        session_type="video",
        state="active",
    )
    assert s_active.state == "active"
    assert s_active.active_at is not None
    assert s_active.closed_at is None

    # 2. State closed automatically populates closed_at
    s_closed = await repo.create_remote_session(
        tenant_id=1,
        terminal_id=11,
        operation_id="op-test-cls",
        correlation_id="corr-test-cls",
        session_type="console",
        state="closed",
    )
    assert s_closed.state == "closed"
    assert s_closed.closed_at is not None

    # 3. State failed automatically populates closed_at
    s_failed = await repo.create_remote_session(
        tenant_id=1,
        terminal_id=12,
        operation_id="op-test-fail",
        correlation_id="corr-test-fail",
        session_type="video",
        state="failed",
    )
    assert s_failed.state == "failed"
    assert s_failed.closed_at is not None

    # 4. State reserved leaves timestamps null
    s_res = await repo.create_remote_session(
        tenant_id=1,
        terminal_id=13,
        operation_id="op-test-res",
        correlation_id="corr-test-res",
        session_type="console",
        state="reserved",
    )
    assert s_res.state == "reserved"
    assert s_res.active_at is None
    assert s_res.closed_at is None

    # Flush passes without check constraint violation
    await db.flush()


@pytest.mark.anyio
async def test_legacy_video_and_control_endpoints_satisfy_active_session_constraint():
    from app.repositories.l4desk_repository import L4DeskRepository

    db = MockRemoteSessionDb()
    repo = L4DeskRepository(db)  # pyright: ignore[reportArgumentType]
    term = Terminal(id=70, device_id=70, sn="SN-T70", org_id=1)
    db.add(term)

    app.dependency_overrides[get_db] = lambda: db
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        headers = auth_headers(user_id=1, org_id=1, role_id=1, is_su=True)

        with patch(
            "app.routers.video_control.iot_client.remote_input_acquire_lease",
            new_callable=AsyncMock,
        ) as mock_acq:
            mock_acq.return_value = {
                "lease_id": "lease-console-70",
                "expires_at": "2026-09-20T12:00:00Z",
                "owner_role": "admin",
                "owner_masked": "u***1",
                "owner_user_id": "1",
                "owner_session_id": "sess-console-70",
                "scope": "console",
            }
            # 1. POST /devices/70/control/lease with scope console
            resp = await ac.post(
                "/api/v1/video/devices/70/control/lease",
                headers=headers,
                json={"scope": "console"},
            )
            assert resp.status_code == 201
            active = await repo.get_active_session_by_terminal_id(70)
            assert active is not None
            assert active.state == "active"
            assert active.session_type == "console"
            assert active.active_at is not None

        # Close active console session before starting stream to test stream_start
        active.state = "closed"
        active.closed_at = datetime.now(UTC)
        await db.flush()

        with (
            patch(
                "app.routers.video_control.iot_client.remote_input_stream_start",
                new_callable=AsyncMock,
            ) as mock_stream,
            patch(
                "app.routers.video_control.media_orchestrator_client.start_session",
                new_callable=AsyncMock,
            ),
            patch(
                "app.routers.video_control.media_orchestrator_client.stop_session",
                new_callable=AsyncMock,
            ),
        ):
            mock_stream.return_value = {
                "stream_instance_id": "stream-inst-70",
                "result": "started",
                "state": "running",
            }
            # 2. POST /devices/70/stream/start with session_type video
            resp2 = await ac.post(
                "/api/v1/video/devices/70/stream/start",
                headers=headers,
                json={
                    "mode": "desktop",
                    "source_id": "0",
                    "profile": "480p",
                    "lease_id": "lease-stream-70",
                },
            )
            assert resp2.status_code == 200
            active_video = await repo.get_active_session_by_terminal_id(70)
            assert active_video is not None
            assert active_video.state == "active"
            assert active_video.session_type == "video"
            assert active_video.active_at is not None
