"""Short, one-command console sessions using the existing billed lease flow."""

from __future__ import annotations

import asyncio
import json
from contextlib import suppress
from urllib.parse import quote, urlencode
from uuid import uuid4

import httpx
from fastmcp import FastMCP
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed

from l4mcp.access import Principal, current_principal
from l4mcp.config import load_config
from l4mcp.user_events import (
    GENERATED_COMMAND_LIMIT,
    build_event_command,
    validate_correlation_id,
)

MAX_OUTPUT = 32768
MAX_COMMAND = 2048


async def _preflight(principal: Principal, device_id: int | None) -> dict:
    if device_id is None or device_id <= 0:
        return {
            "ready": False,
            "reason": "device_id_required",
            "next_step": "Ask the user for one exact terminal device_id before checking or executing commands.",
        }
    base = load_config().menubuilder_url.rstrip("/")
    if not base:
        raise RuntimeError("MENUBUILDER_API_URL is required")
    async with httpx.AsyncClient(timeout=5.0) as client:
        response = await client.get(
            f"{base}/api/mcp/console/preflight/{device_id}",
            headers={"Authorization": principal.bearer},
        )
    if response.status_code == 404:
        return {"ready": False, "reason": "terminal_not_found", "device_id": device_id}
    if response.status_code == 403:
        return {
            "ready": False,
            "reason": "terminal_access_denied",
            "device_id": device_id,
        }
    response.raise_for_status()
    result = response.json()
    if not result.get("ready"):
        result["next_step"] = (
            "Check terminal sys tag, online status, and l4con svc_online before retrying."
        )
    return result


async def _execute_console(
    principal: Principal,
    command: str,
    device_id: int | None = None,
    shell: str = "cmd",
    timeout_sec: int = 20,
    correlation_id: str | None = None,
    *,
    command_limit: int = MAX_COMMAND,
    tenant_only: bool = False,
) -> dict:
    """Run one bounded command after exact-device preflight; release the billed lease."""
    correlation_id = validate_correlation_id(correlation_id)
    checked = await _preflight(principal, device_id)
    if not checked.get("ready"):
        return {**checked, "correlation_id": correlation_id, "console_session_id": None}
    if tenant_only and checked.get("org_id") != principal.org_id:
        return {
            "ready": False,
            "reason": "terminal_access_denied",
            "correlation_id": correlation_id,
            "console_session_id": None,
        }
    if not command.strip() or len(command) > command_limit:
        raise ValueError(f"Command length must be 1–{command_limit} characters")
    if shell not in ("cmd", "powershell"):
        raise ValueError("shell must be cmd or powershell")
    if timeout_sec < 1 or timeout_sec > 20:
        raise ValueError("timeout_sec must be 1–20")
    config = load_config()
    if not config.iot_url or not config.iot_service_key:
        raise RuntimeError("IoT service connection is unavailable")
    base = config.menubuilder_url.rstrip("/")
    assert device_id is not None
    lease_id = None
    async with httpx.AsyncClient(timeout=5.0) as client:
        response = await client.post(
            f"{base}/api/v1/video/devices/{device_id}/control/lease",
            json={"scope": "console", "session_id": principal.session_id},
            headers={"Authorization": principal.bearer},
        )
        if response.status_code != 201:
            return {
                "status": "lease_refused",
                "correlation_id": correlation_id,
                "console_session_id": None,
                "http_status": response.status_code,
                "detail": response.json().get("detail"),
            }
        lease = response.json()
        lease_id = lease["lease_id"]
        session_id = str(uuid4())
        metadata = {"console_session_id": session_id, "correlation_id": correlation_id}
        try:
            params = urlencode(
                {
                    "org_id": checked["org_id"],
                    "lease_id": lease_id,
                    "session_id": lease.get("owner_session_id") or principal.session_id,
                }
            )
            ws_base = (
                config.iot_url.rstrip("/")
                .replace("http://", "ws://", 1)
                .replace("https://", "wss://", 1)
            )
            url = f"{ws_base}/api/internal/v1/diagnostics/ws/devices/{quote(str(checked['sn']), safe='')}?{params}"
            headers = {
                "X-Internal-Service-Key": config.iot_service_key,
                "X-Role": "superuser",
                "X-Role-Id": "1",
                "X-User-Id": principal.username,
                "X-Session-Id": lease.get("owner_session_id") or principal.session_id,
            }
            output: list[str] = []
            size = 0
            async with connect(url, additional_headers=headers, open_timeout=5) as ws:
                await ws.send(
                    json.dumps(
                        {
                            "type": "exec",
                            "session_id": session_id,
                            "command_id": "raw_cmd",
                            "command_line": command,
                            "shell": shell,
                            "ttl_sec": timeout_sec,
                            "max_output_bytes": MAX_OUTPUT,
                            "sn": checked["sn"],
                        }
                    )
                )
                try:
                    async with asyncio.timeout(timeout_sec + 5):
                        while True:
                            message = json.loads(await ws.recv())
                            if message.get("type") == "error":
                                return {
                                    **metadata,
                                    "status": "error",
                                    "detail": message.get("error"),
                                }
                            if message.get("type") != "output":
                                continue
                            chunk = str(
                                message.get("data") or message.get("text") or ""
                            )
                            remaining = MAX_OUTPUT - size
                            output.append(chunk[:remaining])
                            size += len(chunk[:remaining])
                            if message.get("eof") or size >= MAX_OUTPUT:
                                if size >= MAX_OUTPUT and not message.get("eof"):
                                    with suppress(Exception):
                                        await ws.send(
                                            json.dumps(
                                                {
                                                    "type": "cancel",
                                                    "session_id": session_id,
                                                    "reason": "L4mcp output limit",
                                                    "sn": checked["sn"],
                                                }
                                            )
                                        )
                                return {
                                    **metadata,
                                    "status": "completed",
                                    "device_id": device_id,
                                    "output": "".join(output),
                                    "exit_code": message.get("exit_code"),
                                    "truncated": size >= MAX_OUTPUT
                                    or bool(message.get("truncated")),
                                }
                except TimeoutError:
                    with suppress(Exception):
                        await ws.send(
                            json.dumps(
                                {
                                    "type": "cancel",
                                    "session_id": session_id,
                                    "reason": "L4mcp command timeout",
                                    "sn": checked["sn"],
                                }
                            )
                        )
                    return {
                        **metadata,
                        "status": "timeout",
                        "device_id": device_id,
                        "output": "".join(output),
                    }
        except ConnectionClosed, OSError, ValueError:
            return {**metadata, "status": "error", "detail": "Console transport failed"}
        finally:
            try:
                released = await client.delete(
                    f"{base}/api/v1/video/devices/{device_id}/control/lease/{lease_id}",
                    headers={"Authorization": principal.bearer},
                )
            except httpx.HTTPError as exc:
                raise RuntimeError("Console lease release is unconfirmed") from exc
            if released.status_code not in (200, 204):
                raise RuntimeError(
                    f"Console lease release is unconfirmed (HTTP {released.status_code})"
                )


def register_console_tools(mcp: FastMCP) -> None:
    @mcp.tool(annotations={"readOnlyHint": True})
    async def console_preflight(device_id: int | None = None) -> dict:
        """Check one exact terminal before console use. Ask user for device_id if missing."""
        return await _preflight(await current_principal(), device_id)

    @mcp.tool
    async def console_run(
        command: str,
        device_id: int | None = None,
        shell: str = "cmd",
        timeout_sec: int = 20,
        correlation_id: str | None = None,
    ) -> dict:
        """Run an explicit bounded command. UUID is returned, never inserted into command."""
        return await _execute_console(
            await current_principal(),
            command,
            device_id,
            shell,
            timeout_sec,
            correlation_id,
        )

    @mcp.tool(annotations={"readOnlyHint": False})
    async def console_send_event(
        device_id: int,
        event_code: int = 999,
        payload: str | None = None,
        event_exit_code: int | str = 0,
        correlation_id: str | None = None,
        timeout_sec: int = 20,
    ) -> dict:
        """Send one custom 900–999 event via l4con and the billed console lease.

        No code has a fixed success/error meaning. Payload is untrusted text;
        over 1024 UTF-8 bytes it is omitted and event_exit_code becomes -2.
        CLI exit_code is transport acceptance, not event_exit_code or stored history.
        Respect the terminal-wide one-event-per-second limit; no automatic retries.
        """
        command, event = build_event_command(
            event_code, payload, event_exit_code, correlation_id
        )
        result = await _execute_console(
            await current_principal(),
            command,
            device_id,
            "powershell",
            timeout_sec,
            correlation_id,
            command_limit=GENERATED_COMMAND_LIMIT,
            tenant_only=True,
        )
        return {**result, **event}
