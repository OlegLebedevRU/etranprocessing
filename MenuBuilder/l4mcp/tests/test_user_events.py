import base64
import json
import re
import subprocess
import sys
from types import SimpleNamespace
from unittest.mock import AsyncMock

import httpx
import pytest

from l4mcp import console_tools, event_tools
from l4mcp.access import Principal
from l4mcp.user_events import (
    GENERATED_COMMAND_LIMIT,
    _quote_windows_arg,
    build_event_command,
    validate_correlation_id,
)

UUID = "12345678-1234-1234-1234-123456789abc"
PRINCIPAL = Principal("Bearer fixture", 1, "user", 1, 1, "owner-session")


@pytest.mark.parametrize("value", ["", "bad", UUID.replace("-", ""), "{" + UUID + "}"])
def test_uuid_invalid(value):
    with pytest.raises(ValueError):
        validate_correlation_id(value)


def test_event_argument_semantics_and_encoding():
    payload = '" & %PATH% $x ` \n ё \\'
    command, meta = build_event_command(991, payload, 7, UUID.upper())
    data = base64.b64decode(
        re.search(r"FromBase64String\('([^']+)'\)", command)[1]
    ).decode()
    assert _quote_windows_arg("-event-payload=" + payload) in data
    assert "-event-correlation-id=" + UUID in data
    assert meta == {"event_code": 991, "event_exit_code": 7, "payload_omitted": False}
    assert payload not in command
    assert len(command) <= GENERATED_COMMAND_LIMIT
    # Worst-case quote expansion at the full UTF-8 boundary still fits the API4096 cap.
    assert (
        len(build_event_command(999, '"' * 1024, 0, UUID)[0]) <= GENERATED_COMMAND_LIMIT
    )
    assert build_event_command(900, "ё" * 512, -(2**31), None)[1][
        "event_exit_code"
    ] == -(2**31)
    assert build_event_command(999, "ё" * 513, 42, UUID)[1] == {
        "event_code": 999,
        "event_exit_code": -2,
        "payload_omitted": True,
    }
    assert build_event_command(999, None, "not-int", None)[1]["event_exit_code"] == -1
    assert build_event_command(999, None, 2**31, None)[1]["event_exit_code"] == -1
    with pytest.raises(ValueError):
        build_event_command(899, None, 0, None)
    with pytest.raises(ValueError):
        build_event_command(999, "\0", 0, None)


@pytest.mark.skipif(sys.platform != "win32", reason="Windows CRT argv roundtrip")
def test_windows_argv_roundtrip():
    values = ["", "plain", "line\nsecond", '" & %PATH% $x ` ё \\', '\\"']
    script = "import json,sys;print(json.dumps(sys.argv[1:],ensure_ascii=True))"
    command = " ".join(
        _quote_windows_arg(x) for x in [sys.executable, "-c", script, *values]
    )
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    assert json.loads(result.stdout) == values


@pytest.mark.asyncio
async def test_history_only_uses_menubuilder_bearer_and_preserves_payload(monkeypatch):
    seen = []
    raw = {"300": [{"446": '{"result":"ok"}', "447": -2, "448": UUID}]}

    def handle(request):
        seen.append(request)
        return httpx.Response(
            200,
            json={
                "items": [
                    {"id": 10, "device_id": 773, "event_type_code": 991, "payload": raw}
                ],
                "next_after_event_id": 10,
                "has_more": False,
            },
        )

    original = httpx.AsyncClient
    monkeypatch.setattr(
        event_tools,
        "load_config",
        lambda: SimpleNamespace(menubuilder_url="http://fixture"),
    )
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kw: original(transport=httpx.MockTransport(handle), **kw),
    )
    result = await event_tools.search_events(
        PRINCIPAL, 773, UUID.upper(), [991, 999], 9, limit=2
    )
    req = seen[0]
    assert req.url.path == "/api/mcp/events/773"
    assert req.url.params.get_list("events_include") == ["991", "999"]
    assert (
        "org_id" not in req.url.params and "X-Internal-Service-Key" not in req.headers
    )
    assert req.headers["Authorization"] == PRINCIPAL.bearer
    assert result["items"][0]["payload"] == raw
    assert result["items"][0]["event_exit_code"] == -2
    assert result["items"][0]["correlation_id"] == UUID


@pytest.mark.asyncio
@pytest.mark.parametrize("status", [401, 403, 503])
async def test_history_failure_is_not_empty_success(monkeypatch, status):
    original = httpx.AsyncClient
    monkeypatch.setattr(
        event_tools,
        "load_config",
        lambda: SimpleNamespace(menubuilder_url="http://fixture"),
    )
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kw: original(
            transport=httpx.MockTransport(
                lambda request: httpx.Response(status, json={})
            ),
            **kw,
        ),
    )
    with pytest.raises((PermissionError, httpx.HTTPStatusError)):
        await event_tools.search_events(PRINCIPAL, 773)


@pytest.mark.asyncio
@pytest.mark.parametrize("outcome", ["completed", "timeout", "error"])
async def test_console_correlation_and_release(monkeypatch, outcome):
    requests = []

    def handle(request):
        requests.append(request)
        if request.method == "POST":
            return httpx.Response(
                201, json={"lease_id": "lease", "owner_session_id": "owner-session"}
            )
        return httpx.Response(204)

    original = httpx.AsyncClient
    monkeypatch.setattr(
        httpx,
        "AsyncClient",
        lambda **kw: original(transport=httpx.MockTransport(handle), **kw),
    )
    monkeypatch.setattr(
        console_tools,
        "load_config",
        lambda: SimpleNamespace(
            menubuilder_url="http://fixture",
            iot_url="http://iot",
            iot_service_key="fixture-key",
        ),
    )
    monkeypatch.setattr(
        console_tools,
        "_preflight",
        AsyncMock(return_value={"ready": True, "org_id": 1, "sn": "SN773"}),
    )

    class WS:
        def __init__(self):
            self.sent = []

        async def __aenter__(self):
            return self

        async def __aexit__(self, *args):
            return None

        async def send(self, data):
            self.sent.append(json.loads(data))

        async def recv(self):
            if outcome == "timeout":
                raise TimeoutError
            if outcome == "error":
                return json.dumps({"type": "error", "error": "refused"})
            return json.dumps(
                {"type": "output", "data": "done", "eof": True, "exit_code": 0}
            )

    ws = WS()
    monkeypatch.setattr(console_tools, "connect", lambda *args, **kw: ws)
    result = await console_tools._execute_console(
        PRINCIPAL, "echo done", 773, correlation_id=UUID
    )
    assert result["status"] == outcome
    assert result["correlation_id"] == UUID
    assert result["console_session_id"] == ws.sent[0]["session_id"]
    assert ws.sent[0]["command_line"] == "echo done"  # no hidden suffix
    assert requests[-1].method == "DELETE"


@pytest.mark.asyncio
async def test_structured_send_cannot_cross_tenant(monkeypatch):
    monkeypatch.setattr(
        console_tools,
        "_preflight",
        AsyncMock(return_value={"ready": True, "org_id": 2, "sn": "other"}),
    )
    result = await console_tools._execute_console(
        PRINCIPAL, "echo", 773, tenant_only=True
    )
    assert result["reason"] == "terminal_access_denied"
    assert result["console_session_id"] is None
