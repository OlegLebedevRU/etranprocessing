from contextlib import suppress

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.testclient import TestClient

from app.services.portal_socket import serve


def test_independent_channels_share_transport_and_fail_closed():
    app = FastAPI()

    @app.websocket("/tab")
    async def tab(ws: WebSocket):
        async def echo(child):
            await child.accept()
            with suppress(WebSocketDisconnect):
                while True:
                    await child.send_text(await child.receive_text())

        def resolve(message):
            return echo if message["channel"] == "control" else None

        await serve(ws, resolve, 10)

    with TestClient(app).websocket_connect("/tab") as socket:
        for key in (1, 2):
            socket.send_json({"id": key, "op": "open", "channel": "control"})
            assert socket.receive_json() == {"id": key, "op": "open"}
        socket.send_json({"id": 3, "op": "open", "channel": "unknown"})
        assert socket.receive_json()["code"] == 4403
        socket.send_json({"id": 1, "op": "data", "data": "first"})
        assert socket.receive_json() == {"id": 1, "op": "data", "data": "first"}
        socket.send_json({"id": 1, "op": "close"})
        assert socket.receive_json()["id"] == 1
        socket.send_json({"id": 2, "op": "data", "data": "second"})
        assert socket.receive_json() == {"id": 2, "op": "data", "data": "second"}


def test_console_admission_requires_explicit_lease_for_both_roles():
    from uuid import uuid4

    from app.auth import get_current_user
    from app.routers.video_control import router

    app = FastAPI()
    app.include_router(router)
    user = {
        "sub": "alice",
        "org_id": 7,
        "session_id": "verified-session",
        "exp": 2000000000,
        "role_id": 5,
    }
    app.dependency_overrides[get_current_user] = lambda: user
    client = TestClient(app)
    path = router.prefix + "/console-admission"
    uri = f"/api/internal/v1/diagnostics/ws/devices/SN?org_id=7&lease_id={uuid4()}"
    headers = {
        "origin": "http://testserver",
        "x-forwarded-proto": "http",
        "x-original-uri": uri,
    }
    for role in (1, 5):
        user["role_id"] = role
        assert (
            client.get(path, headers=headers).headers["x-console-session"]
            == "verified-session"
        )
        assert (
            client.get(
                path, headers={**headers, "x-original-uri": uri.split("&")[0]}
            ).status_code
            == 403
        )
    for role in (2, 3, 4):
        user["role_id"] = role
        assert client.get(path, headers=headers).status_code == 403
