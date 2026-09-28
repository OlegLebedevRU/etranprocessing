from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

import pytest
from starlette.requests import Request

from app.routers.mcp_proxy import console_preflight
from app.services.iot_client import iot_client


def _device(*, sys: str | None, online: bool, svc: bool) -> dict:
    return {
        "device_id": 773,
        "sn": "SN773",
        "device_tags": [{"tag": "sys", "value": sys}] if sys else [],
        "connection": {"last_checked_result": online, "svc_connect": svc},
    }


@pytest.mark.anyio
@pytest.mark.parametrize(
    ("sys", "online", "svc", "reason"),
    [
        (None, True, True, "sys_tag_missing"),
        ("esp32", True, True, "unsupported_sys"),
        ("windows", False, True, "terminal_offline"),
        ("windows", True, False, "l4con_offline"),
        ("windows", True, True, None),
    ],
)
async def test_console_preflight_fails_closed(sys, online, svc, reason):
    request = Request(
        {
            "type": "http",
            "method": "GET",
            "path": "/api/mcp/console/preflight/773",
            "headers": [(b"authorization", b"Bearer test")],
        }
    )
    user = {"token_type": "api_token", "role_id": 5, "org_id": 1000}
    terminal = SimpleNamespace(sn="SN773", org_id=1000)
    with (
        patch(
            "app.routers.mcp_proxy._verify_device_access",
            new=AsyncMock(return_value=terminal),
        ),
        patch.object(
            iot_client,
            "get_console_device",
            new=AsyncMock(return_value=_device(sys=sys, online=online, svc=svc)),
        ) as fetch,
    ):
        result = await console_preflight(773, request, user, AsyncMock())
    assert result["reason"] == reason
    assert result["ready"] is (reason is None)
    fetch.assert_awaited_once_with(773, 1000)
