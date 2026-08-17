import asyncio
import json
import logging
import sys
from typing import Any, Callable

import uvicorn
from starlette.applications import Starlette
from starlette.requests import Request
from starlette.responses import JSONResponse
from starlette.routing import Route

from pin_server.config import load_config
from pin_server.db import Database
from pin_server.pin_generator import generate_unique_pin
from pin_server.reports import (
    report_balance_by_terminal,
    report_balance_by_tsp,
    report_inkass,
    report_payments,
)

logging.basicConfig(level=logging.INFO, stream=sys.stderr)
logger = logging.getLogger("pin-server")

db: Database = None  # type: ignore[assignment]

# === Tool Registry ===

TOOLS: dict[str, dict] = {}
TOOL_HANDLERS: dict[str, Callable] = {}


def tool(name: str, description: str, input_schema: dict):
    """Decorator to register an MCP tool."""

    def decorator(func):
        TOOLS[name] = {"name": name, "description": description, "inputSchema": input_schema}
        TOOL_HANDLERS[name] = func
        return func

    return decorator


# === Terminal resolution ===


async def _resolve_terminal(identifier: str) -> dict:
    if identifier.isdigit():
        num = int(identifier)
        row = await db.fetchrow("SELECT * FROM terminals WHERE id = $1", num)
        if row:
            return dict(row)
        row = await db.fetchrow("SELECT * FROM terminals WHERE device_id = $1", num)
        if row:
            return dict(row)

    row = await db.fetchrow("SELECT * FROM terminals WHERE sn = $1", identifier)
    if row:
        return dict(row)

    row = await db.fetchrow(
        "SELECT * FROM terminals WHERE sn ILIKE $1 LIMIT 1",
        f"%{identifier}%",
    )
    if row:
        return dict(row)

    raise ValueError(f"Terminal not found: {identifier}")


def _validate_pin_format(pin: str) -> None:
    if not pin.isdigit() or len(pin) != 6:
        raise ValueError(f"PIN must be exactly 6 digits, got: {pin!r}")


# === PIN Tools ===


@tool(
    name="generate_pin",
    description="Generate a PIN code for a terminal to install a certificate. terminal_identifier: device_id (number), sn (serial string), or database id. pin_code: optional custom 6-digit PIN; auto-generated if not provided.",
    input_schema={
        "type": "object",
        "properties": {
            "terminal_identifier": {"type": "string", "description": "device_id, sn, or database id"},
            "pin_code": {"type": "string", "description": "Optional custom 6-digit PIN"},
        },
        "required": ["terminal_identifier"],
    },
)
async def handle_generate_pin(terminal_identifier: str, pin_code: str = None) -> dict:
    terminal = await _resolve_terminal(terminal_identifier)

    if pin_code:
        _validate_pin_format(pin_code)
        exists = await db.fetchval(
            "SELECT EXISTS(SELECT 1 FROM certificate_pins WHERE pin = $1)", pin_code
        )
        if exists:
            raise ValueError(f"PIN {pin_code} already exists")
    else:
        pin_code = await generate_unique_pin(db)

    result = await db.fetchrow(
        "INSERT INTO certificate_pins (pin, terminal_id, status) "
        "VALUES ($1, $2, 'pending') "
        "RETURNING id, pin, terminal_id, status, created_at",
        pin_code,
        terminal["id"],
    )

    return {
        "pin": result["pin"],
        "terminal_id": result["terminal_id"],
        "device_id": terminal["device_id"],
        "sn": terminal["sn"],
        "status": result["status"],
        "created_at": str(result["created_at"]),
    }


@tool(
    name="list_terminals",
    description="List terminals, optionally filtered by search term. Returns up to 50 terminals with their PIN status.",
    input_schema={
        "type": "object",
        "properties": {
            "search": {"type": "string", "description": "Filter by SN (ILIKE), device_id, or org_id"},
            "include_pins": {"type": "boolean", "description": "Include PIN summary (default true)"},
        },
    },
)
async def handle_list_terminals(search: str = None, include_pins: bool = True) -> list:
    if search and search.isdigit():
        num = int(search)
        rows = await db.fetch(
            "SELECT * FROM terminals WHERE device_id = $1 OR org_id = $1 ORDER BY id LIMIT 50",
            num,
        )
    elif search:
        rows = await db.fetch(
            "SELECT * FROM terminals WHERE sn ILIKE $1 ORDER BY id LIMIT 50",
            f"%{search}%",
        )
    else:
        rows = await db.fetch("SELECT * FROM terminals ORDER BY id LIMIT 50")

    result = []
    for r in rows:
        terminal = {
            "id": r["id"],
            "device_id": r["device_id"],
            "sn": r["sn"],
            "org_id": r["org_id"],
            "is_active": r["is_active"],
        }
        if include_pins:
            pins = await db.fetch(
                "SELECT pin, status, created_at, used_at "
                "FROM certificate_pins WHERE terminal_id = $1 ORDER BY created_at DESC",
                r["id"],
            )
            terminal["pins"] = [
                {
                    "pin": p["pin"],
                    "status": p["status"],
                    "created_at": str(p["created_at"]),
                    "used_at": str(p["used_at"]) if p["used_at"] else None,
                }
                for p in pins
            ]
        result.append(terminal)
    return result


@tool(
    name="terminal_status",
    description="Get detailed status of a terminal including all PINs (pending and used).",
    input_schema={
        "type": "object",
        "properties": {
            "terminal_identifier": {"type": "string", "description": "device_id, sn, or database id"},
        },
        "required": ["terminal_identifier"],
    },
)
async def handle_terminal_status(terminal_identifier: str) -> dict:
    terminal = await _resolve_terminal(terminal_identifier)
    pins = await db.fetch(
        "SELECT pin, status, created_at, used_at "
        "FROM certificate_pins WHERE terminal_id = $1 ORDER BY created_at DESC",
        terminal["id"],
    )
    return {
        "id": terminal["id"],
        "device_id": terminal["device_id"],
        "sn": terminal["sn"],
        "org_id": terminal["org_id"],
        "is_active": terminal["is_active"],
        "cert_serial": terminal["cert_serial"],
        "created_at": str(terminal["created_at"]),
        "pins": [
            {
                "pin": p["pin"],
                "status": p["status"],
                "created_at": str(p["created_at"]),
                "used_at": str(p["used_at"]) if p["used_at"] else None,
            }
            for p in pins
        ],
    }


@tool(
    name="revoke_pin",
    description="Revoke a pending PIN code. Used PINs cannot be revoked.",
    input_schema={
        "type": "object",
        "properties": {
            "pin": {"type": "string", "description": "The 6-digit PIN to revoke"},
        },
        "required": ["pin"],
    },
)
async def handle_revoke_pin(pin: str) -> dict:
    _validate_pin_format(pin)
    existing = await db.fetchrow(
        "SELECT id, pin, terminal_id, status FROM certificate_pins WHERE pin = $1",
        pin,
    )
    if not existing:
        raise ValueError(f"PIN {pin} not found")
    if existing["status"] != "pending":
        raise ValueError(f"PIN {pin} has status '{existing['status']}', cannot revoke")
    await db.execute(
        "UPDATE certificate_pins SET status = 'revoked' WHERE pin = $1 AND status = 'pending'",
        pin,
    )
    return {"pin": existing["pin"], "terminal_id": existing["terminal_id"], "status": "revoked"}


# === Report Tools ===


@tool(
    name="report_payments",
    description="Query payments report. Returns payments with details. All amounts in kopecks.",
    input_schema={
        "type": "object",
        "properties": {
            "date_from": {"type": "string", "description": "Start date (YYYY-MM-DD)"},
            "date_to": {"type": "string", "description": "End date (YYYY-MM-DD)"},
            "device_ids": {"type": "string", "description": "Comma-separated device IDs"},
            "tsp_code": {"type": "integer", "description": "Filter by TSP code"},
            "paym_state": {"type": "integer", "description": "Payment state (0=New, 1=Processing, 2=Paid, 3=Not paid, 4=Stopped, 5=Restart, 6=Quarantine)"},
            "top": {"type": "integer", "description": "Max results (10-1000, default 100)"},
        },
    },
)
async def handle_report_payments(
    date_from: str = None, date_to: str = None, device_ids: str = None,
    tsp_code: int = None, paym_state: int = None, top: int = 100,
) -> dict:
    return await report_payments(db, date_from, date_to, device_ids, tsp_code, paym_state, top)


@tool(
    name="report_balance_by_terminal",
    description="Balance report aggregated by terminal. All amounts in kopecks.",
    input_schema={
        "type": "object",
        "properties": {
            "date_from": {"type": "string", "description": "Start date (YYYY-MM-DD)"},
            "date_to": {"type": "string", "description": "End date (YYYY-MM-DD)"},
            "device_ids": {"type": "string", "description": "Comma-separated device IDs"},
            "tsp_code": {"type": "integer", "description": "Filter by TSP code"},
        },
    },
)
async def handle_report_balance_by_terminal(
    date_from: str = None, date_to: str = None, device_ids: str = None, tsp_code: int = None,
) -> dict:
    return await report_balance_by_terminal(db, date_from, date_to, device_ids, tsp_code)


@tool(
    name="report_balance_by_tsp",
    description="Balance report aggregated by TSP (service provider). All amounts in kopecks.",
    input_schema={
        "type": "object",
        "properties": {
            "date_from": {"type": "string", "description": "Start date (YYYY-MM-DD)"},
            "date_to": {"type": "string", "description": "End date (YYYY-MM-DD)"},
            "device_ids": {"type": "string", "description": "Comma-separated device IDs"},
        },
    },
)
async def handle_report_balance_by_tsp(
    date_from: str = None, date_to: str = None, device_ids: str = None,
) -> dict:
    return await report_balance_by_tsp(db, date_from, date_to, device_ids)


@tool(
    name="report_inkass",
    description="Inkassation (cash collection) report. All amounts in kopecks.",
    input_schema={
        "type": "object",
        "properties": {
            "date_from": {"type": "string", "description": "Start date (YYYY-MM-DD)"},
            "date_to": {"type": "string", "description": "End date (YYYY-MM-DD)"},
            "device_ids": {"type": "string", "description": "Comma-separated device IDs"},
            "page": {"type": "integer", "description": "Page number (default 1)"},
            "size": {"type": "integer", "description": "Page size (10-500, default 100)"},
        },
    },
)
async def handle_report_inkass(
    date_from: str = None, date_to: str = None, device_ids: str = None,
    page: int = 1, size: int = 100,
) -> dict:
    return await report_inkass(db, date_from, date_to, device_ids, page, size)


# === HTTP Handler ===


async def handle_mcp(request: Request) -> JSONResponse:
    body = await request.json()
    method = body.get("method")
    params = body.get("params", {})
    msg_id = body.get("id")

    try:
        if method == "initialize":
            result = {
                "protocolVersion": "2025-03-26",
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "processing-pin-manager", "version": "0.1.0"},
            }
        elif method == "tools/list":
            result = {"tools": list(TOOLS.values())}
        elif method == "tools/call":
            tool_name = params.get("name")
            arguments = params.get("arguments", {})
            handler = TOOL_HANDLERS.get(tool_name)
            if not handler:
                return JSONResponse(
                    {"jsonrpc": "2.0", "id": msg_id, "error": {"code": -32601, "message": f"Tool not found: {tool_name}"}}
                )
            call_result = await handler(**arguments)
            if isinstance(call_result, (dict, list)):
                content = [{"type": "text", "text": json.dumps(call_result, ensure_ascii=False, default=str)}]
            else:
                content = [{"type": "text", "text": str(call_result)}]
            result = {"content": content}
        elif method == "ping":
            result = {}
        else:
            return JSONResponse(
                {"jsonrpc": "2.0", "id": msg_id, "error": {"code": -32601, "message": f"Unknown method: {method}"}}
            )

        return JSONResponse({"jsonrpc": "2.0", "id": msg_id, "result": result})

    except Exception as e:
        logger.exception("Error handling MCP request")
        return JSONResponse(
            {"jsonrpc": "2.0", "id": msg_id, "error": {"code": -32000, "message": str(e)}}
        )


async def health(request: Request) -> JSONResponse:
    return JSONResponse({"status": "ok", "tools": len(TOOLS)})


# === Main ===


async def main():
    global db
    config = load_config()
    db = Database(config.database_url)
    await db.connect()
    logger.info(f"MCP server connected to database, {len(TOOLS)} tools registered")

    app = Starlette(
        routes=[
            Route("/mcp", handle_mcp, methods=["POST"]),
            Route("/health", health, methods=["GET"]),
        ]
    )

    logger.info(f"Starting MCP HTTP server on port {config.mcp_port}")
    uv_config = uvicorn.Config(app, host="0.0.0.0", port=config.mcp_port, log_level="info")
    server = uvicorn.Server(uv_config)
    await server.serve()


if __name__ == "__main__":
    asyncio.run(main())
