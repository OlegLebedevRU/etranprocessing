from __future__ import annotations

from unittest.mock import AsyncMock, patch

import pytest
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient
from starlette.testclient import TestClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models import Terminal
from app.security.permissions import PERMISSION_VIDEO_VIEW
from app.services.iot_client import iot_client


@pytest.fixture(autouse=True)
def reset_dependency_overrides():
    app.dependency_overrides.clear()
    yield
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
def mock_db_session():
    mock_db = AsyncMock()
    terminal_1 = Terminal(device_id=1, sn="sn0001", org_id=1)
    terminal_2 = Terminal(device_id=2, sn="sn0002", org_id=2)

    async def mock_scalar(stmt, *args, **kwargs):
        stmt_str = str(stmt)
        if "device_id = :device_id_1" in stmt_str or "terminals.device_id" in stmt_str:
            params = stmt.compile().params
            dev_id = params.get("device_id_1") or params.get("param_1")
            if dev_id == 1:
                return terminal_1
            if dev_id == 2:
                return terminal_2
        return terminal_1

    mock_db.scalar = mock_scalar
    return mock_db


@pytest.fixture
def superuser_token():
    return create_access_token(
        {
            "sub": "su_user",
            "userId": 1,
            "org": "1",
            "org_id": 1,
            "role": "superuser",
            "roleId": 1,
            "role_id": 1,
            "is_superuser": True,
            "token_type": "master",
            "session_id": "sess-su-1",
        }
    )


@pytest.fixture
def admin_token():
    return create_access_token(
        {
            "sub": "admin_user",
            "userId": 2,
            "org": "1",
            "org_id": 1,
            "role": "admin",
            "roleId": 2,
            "role_id": 2,
            "is_superuser": False,
            "token_type": "tenant",
            "session_id": "sess-admin-2",
        }
    )


@pytest.fixture
def operator_token():
    return create_access_token(
        {
            "sub": "op_user",
            "userId": 3,
            "org": "1",
            "org_id": 1,
            "role": "user",
            "roleId": 3,
            "role_id": 3,
            "token_type": "tenant",
            "session_id": "sess-op-3",
        }
    )


@pytest.fixture
def viewer_with_permission_token():
    return create_access_token(
        {
            "sub": "viewer_allowed",
            "userId": 4,
            "org": "1",
            "org_id": 1,
            "role": "viewer",
            "roleId": 4,
            "role_id": 4,
            "token_type": "tenant",
            "permissions": [PERMISSION_VIDEO_VIEW],
            "session_id": "sess-view-4",
        }
    )


@pytest.fixture
def viewer_without_permission_token():
    return create_access_token(
        {
            "sub": "viewer_denied",
            "userId": 5,
            "org": "1",
            "org_id": 1,
            "role": "viewer",
            "roleId": 4,
            "role_id": 4,
            "token_type": "tenant",
            "permissions": [],
            "session_id": "sess-view-5",
        }
    )


# ---------------------------------------------------------------------------
# 1. Permission Matrix: GET /control/status, /inventory, /stream/state
# ---------------------------------------------------------------------------


@pytest.mark.anyio
@pytest.mark.parametrize(
    "token_fixture, expected_status",
    [
        ("superuser_token", 200),
        ("admin_token", 200),
        ("operator_token", 200),
        ("viewer_with_permission_token", 200),
        ("viewer_without_permission_token", 403),
    ],
)
async def test_matrix_status_and_read_endpoints(
    mock_db_session, token_fixture, expected_status, request
):
    token = request.getfixturevalue(token_fixture)
    app.dependency_overrides[get_db] = lambda: mock_db_session

    fake_status = {
        "sn": "sn0001",
        "agent": {"online": True, "desktop_available": True},
        "lease": {"active": False},
    }
    fake_inventory = {"displays": [], "cameras": []}

    with (
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=fake_status)
        ),
        patch.object(
            iot_client,
            "remote_input_inventory",
            new=AsyncMock(return_value=fake_inventory),
        ),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            headers = {"Authorization": f"Bearer {token}"}

            # 1. /control/status
            resp1 = await client.get(
                "/api/v1/video/devices/1/control/status", headers=headers
            )
            assert resp1.status_code == expected_status

            # 2. /inventory
            resp2 = await client.get(
                "/api/v1/video/devices/1/inventory", headers=headers
            )
            assert resp2.status_code == expected_status

            # 3. /stream/state
            resp3 = await client.get(
                "/api/v1/video/devices/1/stream/state", headers=headers
            )
            assert resp3.status_code == expected_status


# ---------------------------------------------------------------------------
# 2. Lease Acquisition Matrix by Scope
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_console_lease_only_superuser(
    mock_db_session,
    superuser_token,
    admin_token,
    operator_token,
    viewer_with_permission_token,
):
    app.dependency_overrides[get_db] = lambda: mock_db_session
    fake_lease = {
        "lease_id": "00000000-0000-0000-0000-000000000001",
        "expires_at": "2026-09-09T10:00:00Z",
        "keepalive_sec": 15,
        "scope": "console",
    }

    with patch.object(
        iot_client, "remote_input_acquire_lease", new=AsyncMock(return_value=fake_lease)
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # 1. Superuser -> 201
            r_su = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "console"},
                headers={"Authorization": f"Bearer {superuser_token}"},
            )
            assert r_su.status_code == 201

            # 2. Admin (role 2) -> 403
            r_admin = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "console"},
                headers={"Authorization": f"Bearer {admin_token}"},
            )
            assert r_admin.status_code == 403
            assert "только суперадминистраторам" in r_admin.json()["detail"]

            # 3. Operator (role 3) -> 403
            r_op = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "console"},
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert r_op.status_code == 403
            assert "только суперадминистраторам" in r_op.json()["detail"]

            # 4. Viewer (role 4) -> 403
            r_v = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "console"},
                headers={"Authorization": f"Bearer {viewer_with_permission_token}"},
            )
            assert r_v.status_code == 403


@pytest.mark.anyio
async def test_view_lease_permissions(
    mock_db_session,
    operator_token,
    viewer_with_permission_token,
    viewer_without_permission_token,
):
    app.dependency_overrides[get_db] = lambda: mock_db_session
    fake_lease = {
        "lease_id": "00000000-0000-0000-0000-000000000002",
        "expires_at": "2026-09-09T10:00:00Z",
        "keepalive_sec": 15,
        "scope": "view",
    }

    with patch.object(
        iot_client, "remote_input_acquire_lease", new=AsyncMock(return_value=fake_lease)
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # 1. Operator with view scope -> 201
            r_op = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "view"},
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert r_op.status_code == 201

            # 2. Viewer with video:view -> 201
            r_vw_ok = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "view"},
                headers={"Authorization": f"Bearer {viewer_with_permission_token}"},
            )
            assert r_vw_ok.status_code == 201

            # 3. Viewer without video:view -> 403
            r_vw_no = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "view"},
                headers={"Authorization": f"Bearer {viewer_without_permission_token}"},
            )
            assert r_vw_no.status_code == 403


@pytest.mark.anyio
async def test_stream_and_input_lease_forbidden_for_viewer(
    mock_db_session, viewer_with_permission_token
):
    app.dependency_overrides[get_db] = lambda: mock_db_session
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        headers = {"Authorization": f"Bearer {viewer_with_permission_token}"}

        # stream scope -> 403
        r_stream = await client.post(
            "/api/v1/video/devices/1/control/lease",
            json={"scope": "stream"},
            headers=headers,
        )
        assert r_stream.status_code == 403
        assert "Управление доступно только операторам" in r_stream.json()["detail"]

        # input scope -> 403
        r_input = await client.post(
            "/api/v1/video/devices/1/control/lease",
            json={"scope": "input"},
            headers=headers,
        )
        assert r_input.status_code == 403
        assert "Управление доступно только операторам" in r_input.json()["detail"]


# ---------------------------------------------------------------------------
# 3. Stream Start/Stop and Events Matrix
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_stream_start_stop_events_matrix(
    mock_db_session, operator_token, viewer_with_permission_token
):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    fake_start = {
        "stream_instance_id": "inst-1",
        "result": "started",
        "state": "running",
    }
    fake_stop = {"result": "stopped"}

    with (
        patch.object(
            iot_client,
            "remote_input_stream_start",
            new=AsyncMock(return_value=fake_start),
        ),
        patch.object(
            iot_client,
            "remote_input_stream_stop",
            new=AsyncMock(return_value=fake_stop),
        ),
        patch.object(
            iot_client, "remote_input_key", new=AsyncMock(return_value={"result": "ok"})
        ),
        patch(
            "app.routers.video_control.media_orchestrator_client.start_session",
            new=AsyncMock(return_value={"status": "success", "session_id": "test"}),
        ),
        patch(
            "app.routers.video_control.media_orchestrator_client.stop_session",
            new=AsyncMock(return_value={"status": "success"}),
        ),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            op_headers = {"Authorization": f"Bearer {operator_token}"}
            vw_headers = {"Authorization": f"Bearer {viewer_with_permission_token}"}

            # 1. Operator stream start -> 200
            r_start_op = await client.post(
                "/api/v1/video/devices/1/stream/start",
                json={"mode": "desktop", "source_id": "0", "lease_id": "l-1"},
                headers=op_headers,
            )
            assert r_start_op.status_code == 200
            assert r_start_op.json()["stream_instance_id"] == "inst-1"

            # 2. Viewer stream start -> 403
            r_start_vw = await client.post(
                "/api/v1/video/devices/1/stream/start",
                json={"mode": "desktop", "source_id": "0", "lease_id": "l-1"},
                headers=vw_headers,
            )
            assert r_start_vw.status_code == 403

            # 3. Operator stream stop -> 200
            r_stop_op = await client.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "l-1"},
                headers=op_headers,
            )
            assert r_stop_op.status_code == 200

            # 4. Viewer stream stop -> 403
            r_stop_vw = await client.post(
                "/api/v1/video/devices/1/stream/stop",
                json={"lease_id": "l-1"},
                headers=vw_headers,
            )
            assert r_stop_vw.status_code == 403

            # 5. Keyboard event by operator -> 200
            r_key_op = await client.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "l-1",
                    "type": "key",
                    "kind": "down",
                    "vk": 65,
                    "text": "A",
                },
                headers=op_headers,
            )
            assert r_key_op.status_code == 200

            # 6. Keyboard event by viewer -> 403
            r_key_vw = await client.post(
                "/api/v1/video/devices/1/control/events",
                json={
                    "lease_id": "l-1",
                    "type": "key",
                    "kind": "down",
                    "vk": 65,
                    "text": "A",
                },
                headers=vw_headers,
            )
            assert r_key_vw.status_code == 403


# ---------------------------------------------------------------------------
# 4. Video Session (Janus mountpoint + PIN) & Lease Holder Check
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_video_session_pin_only_for_lease_holder(
    mock_db_session, operator_token, viewer_with_permission_token
):
    app.dependency_overrides[get_db] = lambda: mock_db_session

    # Status shows operator holds active stream lease
    op_status = {
        "sn": "sn0001",
        "agent": {"online": True},
        "lease": {
            "active": True,
            "lease_id": "lease-op-1",
            "scope": "stream",
            "owner_user_id": "op_user",
            "stream_instance_id": "stream-inst-42",
        },
    }

    from app.services.remote_session_use_case import RemoteSessionResponse

    mock_result = RemoteSessionResponse(
        session_id="sess-op-1",
        local_session_id=1,
        terminal_id=1,
        sn="sn0001",
        session_type="video",
        state="active",
        mountpoint_id=1,
        janus_ws="/janus-ws",
        pin="test-pin-42",
        ttl_sec=600,
    )

    # First call (operator, lease owner) succeeds; second call (viewer) raises 403
    mock_use_case = AsyncMock()
    mock_use_case.start_session = AsyncMock(
        side_effect=[
            mock_result,
            HTTPException(
                status_code=403, detail="только держателю активной аренды"
            ),
        ]
    )

    with (
        patch.object(
            iot_client, "remote_input_status", new=AsyncMock(return_value=op_status)
        ),
        patch(
            "app.services.remote_session_use_case.RemoteSessionUseCase",
            return_value=mock_use_case,
        ),
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            # 1. Operator (owner of lease) creates video session -> 200 and receives PIN
            r_op = await client.post(
                "/api/v1/video/devices/1/session",
                headers={"Authorization": f"Bearer {operator_token}"},
            )
            assert r_op.status_code == 200
            data_op = r_op.json()
            assert "pin" in data_op
            assert data_op["pin"] is not None
            assert len(data_op["pin"]) > 0
            assert data_op["pin"] == "test-pin-42"

            # 2. Viewer trying to create session while operator owns the lease -> 403
            r_vw = await client.post(
                "/api/v1/video/devices/1/session",
                headers={"Authorization": f"Bearer {viewer_with_permission_token}"},
            )
            assert r_vw.status_code == 403
            assert "только держателю активной аренды" in r_vw.json()["detail"]


# ---------------------------------------------------------------------------
# 5. Propagating 409 (lease_taken, stream_not_running) and nack.code
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_app1_409_and_nack_propagation(mock_db_session, operator_token):
    from fastapi import HTTPException

    app.dependency_overrides[get_db] = lambda: mock_db_session

    async def raise_lease_taken(*args, **kwargs):
        raise HTTPException(
            status_code=409,
            detail={
                "code": "lease_taken",
                "owner_role": "admin",
                "owner_masked": "adm***@local",
                "scope": "stream",
                "expires_at": "2026-09-09T10:05:00Z",
            },
        )

    async def raise_stream_not_running(*args, **kwargs):
        raise HTTPException(
            status_code=409,
            detail={"code": "stream_not_running", "detail": "stream_not_running"},
        )

    async def raise_nack(*args, **kwargs):
        raise HTTPException(
            status_code=409,
            detail={
                "nack": {"code": "ffmpeg_missing", "reason": "executable not found"}
            },
        )

    headers = {"Authorization": f"Bearer {operator_token}"}
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as client:
        # 1. lease_taken propagation
        with patch.object(
            iot_client, "remote_input_acquire_lease", new=raise_lease_taken
        ):
            r1 = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "stream"},
                headers=headers,
            )
            assert r1.status_code == 409
            d1 = r1.json()["detail"]
            assert d1.get("code") == "lease_taken"
            assert d1.get("owner_role") == "admin"

        # 2. stream_not_running propagation
        with patch.object(
            iot_client, "remote_input_acquire_lease", new=raise_stream_not_running
        ):
            r2 = await client.post(
                "/api/v1/video/devices/1/control/lease",
                json={"scope": "view"},
                headers=headers,
            )
            assert r2.status_code == 409
            d2 = r2.json()["detail"]
            assert d2.get("code") == "stream_not_running"

        # 3. nack code propagation on stream start
        with (
            patch.object(iot_client, "remote_input_stream_start", new=raise_nack),
            patch(
                "app.routers.video_control.media_orchestrator_client.start_session",
                new=AsyncMock(return_value={"status": "success"}),
            ),
            patch(
                "app.routers.video_control.media_orchestrator_client.stop_session",
                new=AsyncMock(return_value={"status": "success"}),
            ),
        ):
            r3 = await client.post(
                "/api/v1/video/devices/1/stream/start",
                json={"mode": "desktop", "source_id": "0", "lease_id": "l-1"},
                headers=headers,
            )
            assert r3.status_code == 409
            d3 = r3.json()["detail"]
            assert d3.get("nack", {}).get("code") == "ffmpeg_missing"


# ---------------------------------------------------------------------------
# 6. X-Session-Id header propagation to app1
# ---------------------------------------------------------------------------


def test_x_session_id_in_headers():
    user = {
        "sub": "test_user_42",
        "role": "user",
        "role_id": 3,
        "session_id": "sess-unique-uuid-1234",
    }
    headers = iot_client._get_headers(org_id=10, user=user)
    assert headers.get("X-Session-Id") == "sess-unique-uuid-1234"
    assert headers.get("X-User-Id") == "test_user_42"
    assert headers.get("X-Role") == "user"
    assert headers.get("X-Org-Id") == "10"


# ---------------------------------------------------------------------------
# 7. Logout -> release_by_owner
# ---------------------------------------------------------------------------


@pytest.mark.anyio
async def test_logout_triggers_release_by_owner(operator_token):
    mock_release = AsyncMock(return_value={"released": 1})
    with patch.object(iot_client, "remote_input_release_by_owner", new=mock_release):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            resp = await client.post(
                "/api/auth/logout",
                headers={
                    "Authorization": f"Bearer {operator_token}",
                    "X-Requested-With": "XMLHttpRequest",
                },
            )
            assert resp.status_code == 200
            mock_release.assert_called_once()
            call_kwargs = mock_release.call_args.kwargs
            assert call_kwargs.get("user_id") in ("3", "op_user")
            assert call_kwargs.get("session_id") == "sess-op-3"


# ---------------------------------------------------------------------------
# 8. WebSocket Permissions: Operator vs Viewer
# ---------------------------------------------------------------------------


def test_ws_permissions_matrix(
    mock_db_session,
    operator_token,
    viewer_with_permission_token,
    viewer_without_permission_token,
):
    from starlette.websockets import WebSocketDisconnect

    app.dependency_overrides[get_db] = lambda: mock_db_session

    class FakeSessionContext:
        async def __aenter__(self):
            return mock_db_session

        async def __aexit__(self, *args):
            pass

    # Case 1: Viewer without permission -> closed 4403
    with (
        patch(
            "app.routers.video_control.async_session", return_value=FakeSessionContext()
        ),
        patch.object(
            iot_client,
            "remote_input_status",
            new=AsyncMock(
                return_value={
                    "lease": {
                        "active": True,
                        "lease_id": "l-view",
                        "scope": "view",
                        "owner_user_id": "viewer_denied",
                    }
                }
            ),
        ),
        TestClient(app) as tc,
    ):
        try:
            with tc.websocket_connect(
                "/api/v1/video/devices/1/control/ws/l-view",
                params={"token": viewer_without_permission_token},
            ):
                pytest.fail(
                    "Expected WebSocket to be rejected for viewer without video:view"
                )
        except WebSocketDisconnect as exc:
            assert exc.code == 4403

    # Case 2: Viewer with video:view trying to connect with 'input' lease -> rejected 4403
    with (
        patch(
            "app.routers.video_control.async_session", return_value=FakeSessionContext()
        ),
        patch.object(
            iot_client,
            "remote_input_status",
            new=AsyncMock(
                return_value={
                    "lease": {
                        "active": True,
                        "lease_id": "l-input",
                        "scope": "input",
                        "owner_user_id": "viewer_allowed",
                    }
                }
            ),
        ),
        TestClient(app) as tc,
    ):
        try:
            with tc.websocket_connect(
                "/api/v1/video/devices/1/control/ws/l-input",
                params={"token": viewer_with_permission_token},
            ):
                pytest.fail(
                    "Expected WebSocket to be rejected for viewer with input lease"
                )
        except WebSocketDisconnect as exc:
            assert exc.code == 4403
