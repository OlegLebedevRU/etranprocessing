"""Contract checks for the read-only IoT watch relay."""

import asyncio
from contextlib import asynccontextmanager
from types import SimpleNamespace
from unittest.mock import AsyncMock

from fastapi import HTTPException

from app.routers import video_control


class BrowserSocket:
    def __init__(self) -> None:
        self.query_params: dict[str, str] = {}
        self.headers = {
            "host": "portal.example",
            "origin": "https://portal.example",
            "x-forwarded-proto": "https",
        }
        self.sent: list[dict[str, str]] = []
        self.inbound: asyncio.Queue[str] = asyncio.Queue()
        self.accepted = False
        self.close_code: int | None = None

    async def accept(self) -> None:
        self.accepted = True

    async def close(self, code: int = 1000) -> None:
        self.close_code = code

    async def send_json(self, payload: dict[str, str]) -> None:
        self.sent.append(payload)

    async def receive_text(self) -> str:
        return await self.inbound.get()


def test_watch_relay_invalidates_without_exposing_snapshot(monkeypatch):
    asyncio.run(_watch_relay_scenario(monkeypatch))


async def _watch_relay_scenario(monkeypatch):
    browser = BrowserSocket()
    upstream_messages: asyncio.Queue[str] = asyncio.Queue()
    await upstream_messages.put(
        '{"type":"snapshot","data":{"sn":"SN123","lease":{"owner_user_id":"other"}}}'
    )

    class Upstream:
        async def recv(self) -> str:
            return await upstream_messages.get()

    @asynccontextmanager
    async def connect(url, additional_headers, open_timeout):
        assert url.endswith("/ws/watch/SN123")
        assert additional_headers == {
            "X-Internal-Service-Key": "test-key",
            "X-Org-Id": "7",
        }
        assert open_timeout > 0
        yield Upstream()

    @asynccontextmanager
    async def session():
        yield object()

    monkeypatch.setattr(
        video_control,
        "get_ws_user",
        AsyncMock(
            return_value={
                "org_id": 7,
                "permissions": [video_control.PERMISSION_VIDEO_VIEW],
            }
        ),
    )
    monkeypatch.setattr(
        video_control,
        "_verify_device_access",
        AsyncMock(return_value=SimpleNamespace(sn="SN123", org_id=7)),
    )
    monkeypatch.setattr(video_control, "async_session", session)
    monkeypatch.setattr(video_control.iot_client, "service_token", "test-key")
    monkeypatch.setattr(video_control.iot_client, "base_url", "http://app1:8000")
    monkeypatch.setattr(video_control.websockets, "connect", connect)

    relay = asyncio.create_task(video_control.watch_device_status_ws(browser, 10))
    try:
        for _ in range(100):
            if browser.sent:
                break
            await asyncio.sleep(0.01)
        assert browser.accepted
        assert browser.sent == [{"type": "invalidate"}]

        await upstream_messages.put(
            '{"type":"invalidate","kind":"stream","reason":"ended"}'
        )
        for _ in range(100):
            if len(browser.sent) == 2:
                break
            await asyncio.sleep(0.01)
        assert browser.sent == [{"type": "invalidate"}, {"type": "invalidate"}]

        await browser.inbound.put('{"type":"release"}')
        await asyncio.wait_for(relay, 1)
        assert browser.close_code == 4403
    finally:
        relay.cancel()


def test_watch_rejects_cross_origin_and_url_token():
    asyncio.run(_watch_rejection_scenario())


async def _watch_rejection_scenario():
    browser = BrowserSocket()
    browser.headers["origin"] = "https://untrusted.example"
    await video_control.watch_device_status_ws(browser, 10)
    assert browser.close_code == 4403
    assert not browser.accepted

    browser = BrowserSocket()
    browser.query_params["token"] = "not-accepted"
    await video_control.watch_device_status_ws(browser, 10)
    assert browser.close_code == 4401
    assert not browser.accepted


def test_watch_rejects_foreign_device(monkeypatch):
    async def scenario():
        @asynccontextmanager
        async def session():
            yield object()

        monkeypatch.setattr(
            video_control,
            "get_ws_user",
            AsyncMock(
                return_value={
                    "org_id": 7,
                    "permissions": [video_control.PERMISSION_VIDEO_VIEW],
                }
            ),
        )
        monkeypatch.setattr(video_control, "async_session", session)
        monkeypatch.setattr(
            video_control,
            "_verify_device_access",
            AsyncMock(side_effect=HTTPException(status_code=403)),
        )
        browser = BrowserSocket()
        await video_control.watch_device_status_ws(browser, 10)
        assert browser.close_code == 4403
        assert not browser.accepted

    asyncio.run(scenario())
