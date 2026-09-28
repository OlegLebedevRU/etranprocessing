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

from l4mcp.access import Principal, current_principal
from l4mcp.config import load_config

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


def register_console_tools(mcp: FastMCP) -> None:
    @mcp.tool
    async def console_preflight(device_id: int | None = None) -> dict:
        """Check one exact terminal before console use. Ask user for device_id if missing."""
        return await _preflight(await current_principal(), device_id)

    @mcp.tool
    async def console_run(
        command: str,
        device_id: int | None = None,
        shell: str = "cmd",
        timeout_sec: int = 20,
    ) -> dict:
        """Run one bounded command after exact-device preflight; release the billed lease."""
        principal = await current_principal()
        checked = await _preflight(principal, device_id)
        if not checked.get("ready"):
            return checked
        if not command.strip() or len(command) > MAX_COMMAND:
            raise ValueError(f"Command length must be 1–{MAX_COMMAND} characters")
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
                    "http_status": response.status_code,
                    "detail": response.json().get("detail"),
                }
            lease = response.json()
            lease_id = lease["lease_id"]
            try:
                params = urlencode(
                    {
                        "org_id": checked["org_id"],
                        "lease_id": lease_id,
                        "session_id": lease.get("owner_session_id")
                        or principal.session_id,
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
                    "X-Session-Id": lease.get("owner_session_id")
                    or principal.session_id,
                }
                session_id = str(uuid4())
                output: list[str] = []
                size = 0
                async with connect(
                    url, additional_headers=headers, open_timeout=5
                ) as ws:
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
                            "status": "timeout",
                            "device_id": device_id,
                            "output": "".join(output),
                        }
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
