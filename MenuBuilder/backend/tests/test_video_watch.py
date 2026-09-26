"""Consumer contract checks for the read-only browser video watch feed."""

import asyncio
import json
from typing import Self
from unittest.mock import AsyncMock, patch

import pytest
from starlette.testclient import TestClient
from starlette.websockets import WebSocketDisconnect

from app.auth import create_access_token
from app.main import app
from app.models import Terminal
from app.services.iot_client import iot_client

WATCH_PATH = "/api/v1/video/devices/773/watch/ws"


def _viewer_token(*, org_id: int = 1, permissions: list[str] | None = None) -> str:
    return create_access_token(
        {
            "sub": "watch-viewer",
            "userId": 101,
            "org_id": org_id,
            "role": "viewer",
            "roleId": 4,
            "token_type": "tenant",
            "permissions": permissions if permissions is not None else ["video:view"],
        }
    )


class _DbContext:
    def __init__(self) -> None:
        self.session = AsyncMock()
        self.session.scalar.return_value = Terminal(
            device_id=773, sn="watch-test-sn", org_id=1
        )

    async def __aenter__(self) -> AsyncMock:
        return self.session

    async def __aexit__(self, *_args: object) -> None:
        return None


class _UpstreamContext:
    def __init__(self) -> None:
        self.messages: asyncio.Queue[str] = asyncio.Queue()
        self.messages.put_nowait(
            json.dumps({"type": "snapshot", "data": {"sn": "watch-test-sn"}})
        )
        self.messages.put_nowait(json.dumps({"type": "invalidate", "kind": "stream"}))

    async def __aenter__(self) -> Self:
        return self

    async def __aexit__(self, *_args: object) -> None:
        return None

    async def recv(self) -> str:
        return await self.messages.get()

    async def send(self, _message: str) -> None:
        pytest.fail("Read-only watch forwarded a browser message upstream")


@pytest.mark.parametrize(
    ("path", "cookies", "headers", "expected_code"),
    [
        (WATCH_PATH, None, None, 4401),
        (WATCH_PATH + "?token=placeholder", None, None, 4401),
        (WATCH_PATH, {"accessToken": _viewer_token(permissions=[])}, None, 4403),
        (
            WATCH_PATH,
            {"accessToken": _viewer_token()},
            {"Origin": "https://foreign.example"},
            4403,
        ),
        (
            WATCH_PATH,
            {"accessToken": _viewer_token()},
            {"Origin": "http://testserver", "X-Forwarded-Proto": "https"},
            4403,
        ),
    ],
)
def test_watch_rejects_unauthorized_browser(
    path: str,
    cookies: dict[str, str] | None,
    headers: dict[str, str] | None,
    expected_code: int,
) -> None:
    client = TestClient(app)
    with (
        pytest.raises(WebSocketDisconnect) as error,
        client.websocket_connect(path, cookies=cookies, headers=headers or {}),
    ):
        pass
    assert error.value.code == expected_code


def test_watch_rejects_foreign_tenant_before_upstream() -> None:
    client = TestClient(app)
    with (
        patch("app.routers.video_control.async_session", return_value=_DbContext()),
        patch.object(iot_client, "service_token", "placeholder"),
        patch("app.routers.video_control.websockets.connect") as connect,
        pytest.raises(WebSocketDisconnect) as error,
        client.websocket_connect(
            WATCH_PATH, cookies={"accessToken": _viewer_token(org_id=2)}
        ),
    ):
        pass
    assert error.value.code == 4403
    connect.assert_not_called()


def test_watch_relays_only_invalidate_and_rejects_browser_input() -> None:
    client = TestClient(app)
    upstream = _UpstreamContext()
    keepalive = AsyncMock()
    release = AsyncMock()
    with (
        patch("app.routers.video_control.async_session", return_value=_DbContext()),
        patch.object(iot_client, "service_token", "placeholder"),
        patch.object(iot_client, "remote_input_keepalive", new=keepalive),
        patch.object(iot_client, "remote_input_release", new=release),
        patch(
            "app.routers.video_control.websockets.connect", return_value=upstream
        ) as connect,
        client.websocket_connect(
            WATCH_PATH,
            cookies={"accessToken": _viewer_token()},
            headers={"Origin": "https://testserver", "X-Forwarded-Proto": "https"},
        ) as socket,
    ):
        assert socket.receive_json() == {"type": "invalidate"}
        assert socket.receive_json() == {"type": "invalidate"}
        socket.send_text(json.dumps({"type": "keepalive"}))
        with pytest.raises(WebSocketDisconnect) as error:
            socket.receive_text()
        assert error.value.code == 4403

    assert connect.call_args.kwargs["additional_headers"]["X-Org-Id"] == "1"
    keepalive.assert_not_awaited()
    release.assert_not_awaited()
