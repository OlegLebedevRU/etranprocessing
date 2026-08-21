import logging
import sys

from fastmcp import Context, FastMCP
from fastmcp.server.lifespan import lifespan

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


# === Lifespan: DB pool lifecycle ===


@lifespan
async def app_lifespan(server):
    config = load_config()
    db = Database(config.database_url)
    await db.connect()
    logger.info("MCP server connected to database")
    try:
        yield {"db": db}
    finally:
        await db.close()
        logger.info("MCP server disconnected from database")


mcp = FastMCP("ProcessingBackend", lifespan=app_lifespan)


# === Terminal resolution ===


async def _resolve_terminal(
    db: Database, identifier: str, org_id: int | None = None
) -> dict:
    if identifier.isdigit():
        num = int(identifier)
        if org_id is not None:
            row = await db.fetchrow(
                "SELECT * FROM terminals WHERE id = $1 AND org_id = $2",
                num,
                org_id,
            )
            if row:
                return dict(row)
            row = await db.fetchrow(
                "SELECT * FROM terminals WHERE device_id = $1 AND org_id = $2",
                num,
                org_id,
            )
            if row:
                return dict(row)
        else:
            row = await db.fetchrow("SELECT * FROM terminals WHERE id = $1", num)
            if row:
                return dict(row)
            row = await db.fetchrow("SELECT * FROM terminals WHERE device_id = $1", num)
            if row:
                return dict(row)

    if org_id is not None:
        row = await db.fetchrow(
            "SELECT * FROM terminals WHERE sn = $1 AND org_id = $2",
            identifier,
            org_id,
        )
        if row:
            return dict(row)
        row = await db.fetchrow(
            "SELECT * FROM terminals WHERE sn ILIKE $1 AND org_id = $2 LIMIT 1",
            f"%{identifier}%",
            org_id,
        )
        if row:
            return dict(row)
    else:
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


@mcp.tool
async def generate_pin(
    terminal_identifier: str,
    pin_code: str | None = None,
    org_id: int | None = None,
    ctx: Context | None = None,
) -> dict:
    """Generate a PIN code for a terminal to install a certificate.

    Args:
        terminal_identifier: device_id (number), sn (serial string), or database id
        pin_code: Optional custom 6-digit PIN; auto-generated if not provided
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]
    terminal = await _resolve_terminal(db, terminal_identifier, org_id=org_id)

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
        "INSERT INTO certificate_pins "
        "(pin, terminal_id, org_id, status, creation_source) "
        "VALUES ($1, $2, $3, 'pending', 'global_admin') "
        "RETURNING id, pin, terminal_id, status, created_at",
        pin_code,
        terminal["id"],
        terminal["org_id"],
    )
    assert result is not None

    return {
        "pin": result["pin"],
        "terminal_id": result["terminal_id"],
        "device_id": terminal["device_id"],
        "sn": terminal["sn"],
        "status": result["status"],
        "created_at": str(result["created_at"]),
    }


@mcp.tool
async def list_terminals(
    search: str | None = None,
    org_id: int | None = None,
    include_pins: bool = True,
    ctx: Context | None = None,
) -> list:
    """List terminals, optionally filtered by search term and organization.

    Returns up to 50 terminals with their PIN status.

    Args:
        search: Filter by SN (ILIKE), device_id, or database id
        org_id: Optional organization ID filter for tenant isolation
        include_pins: Include PIN summary (default true)
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]

    conditions = ["1=1"]
    params = []
    idx = 1

    if org_id is not None:
        conditions.append(f"org_id = ${idx}")
        params.append(org_id)
        idx += 1

    if search and search.isdigit():
        num = int(search)
        conditions.append(f"(device_id = ${idx} OR id = ${idx})")
        params.append(num)
        idx += 1
    elif search:
        conditions.append(f"sn ILIKE ${idx}")
        params.append(f"%{search}%")
        idx += 1

    where = " AND ".join(conditions)
    rows = await db.fetch(
        f"SELECT * FROM terminals WHERE {where} ORDER BY id LIMIT 50",
        *params,
    )

    result = []
    for r in rows:
        terminal = {
            "id": r["id"],
            "device_id": r["device_id"],
            "sn": r["sn"],
            "org_id": r["org_id"],
            "is_active": r["is_active"],
            "address": r.get("address"),
            "note": r.get("note"),
            "terminal_type_id": r.get("terminal_type_id", 0),
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


@mcp.tool
async def terminal_status(
    terminal_identifier: str,
    org_id: int | None = None,
    ctx: Context | None = None,
) -> dict:
    """Get detailed status of a terminal including all PINs (pending and used).

    Args:
        terminal_identifier: device_id (number), sn (serial string), or database id
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]
    terminal = await _resolve_terminal(db, terminal_identifier, org_id=org_id)
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
        "address": terminal.get("address"),
        "note": terminal.get("note"),
        "terminal_type_id": terminal.get("terminal_type_id", 0),
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


@mcp.tool
async def revoke_pin(
    pin: str,
    org_id: int | None = None,
    ctx: Context | None = None,
) -> dict:
    """Revoke a pending PIN code. Used PINs cannot be revoked.

    Args:
        pin: The 6-digit PIN to revoke
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]
    _validate_pin_format(pin)

    if org_id is not None:
        existing = await db.fetchrow(
            "SELECT id, pin, terminal_id, org_id, status FROM certificate_pins WHERE pin = $1 AND org_id = $2",
            pin,
            org_id,
        )
    else:
        existing = await db.fetchrow(
            "SELECT id, pin, terminal_id, org_id, status FROM certificate_pins WHERE pin = $1",
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
    return {
        "pin": existing["pin"],
        "terminal_id": existing["terminal_id"],
        "status": "revoked",
    }


# === Report Tools ===


@mcp.tool
async def report_payments_tool(
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
    paym_state: int | None = None,
    top: int = 100,
    org_id: int | None = None,
    ctx: Context | None = None,
) -> dict:
    """Query payments report. Returns payments with details.

    All amounts in kopecks.

    Args:
        date_from: Start date (YYYY-MM-DD)
        date_to: End date (YYYY-MM-DD)
        device_ids: Comma-separated device IDs
        tsp_code: Filter by TSP code
        paym_state: Payment state (0=New, 1=Processing, 2=Paid, 3=Not paid, 4=Stopped, 5=Restart, 6=Quarantine)
        top: Max results (10-1000, default 100)
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]
    return await report_payments(
        db, date_from, date_to, device_ids, tsp_code, paym_state, top, org_id=org_id
    )


@mcp.tool
async def report_balance_by_terminal_tool(
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    tsp_code: int | None = None,
    org_id: int | None = None,
    ctx: Context | None = None,
) -> dict:
    """Balance report aggregated by terminal. All amounts in kopecks.

    Args:
        date_from: Start date (YYYY-MM-DD)
        date_to: End date (YYYY-MM-DD)
        device_ids: Comma-separated device IDs
        tsp_code: Filter by TSP code
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]
    return await report_balance_by_terminal(
        db, date_from, date_to, device_ids, tsp_code, org_id=org_id
    )


@mcp.tool
async def report_balance_by_tsp_tool(
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    org_id: int | None = None,
    ctx: Context | None = None,
) -> dict:
    """Balance report aggregated by TSP (service provider). All amounts in kopecks.

    Args:
        date_from: Start date (YYYY-MM-DD)
        date_to: End date (YYYY-MM-DD)
        device_ids: Comma-separated device IDs
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]
    return await report_balance_by_tsp(
        db, date_from, date_to, device_ids, org_id=org_id
    )


@mcp.tool
async def report_inkass_tool(
    date_from: str | None = None,
    date_to: str | None = None,
    device_ids: str | None = None,
    page: int = 1,
    size: int = 100,
    org_id: int | None = None,
    ctx: Context | None = None,
) -> dict:
    """Inkassation (cash collection) report. All amounts in kopecks.

    Args:
        date_from: Start date (YYYY-MM-DD)
        date_to: End date (YYYY-MM-DD)
        device_ids: Comma-separated device IDs
        page: Page number (default 1)
        size: Page size (10-500, default 100)
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    db: Database = ctx.lifespan_context["db"]
    return await report_inkass(
        db, date_from, date_to, device_ids, page, size, org_id=org_id
    )


# === Custom routes ===


@mcp.custom_route("/health", methods=["GET"])
async def health(request):
    from starlette.responses import JSONResponse

    return JSONResponse({"status": "ok", "tools": 8})


# === Entry point ===


def main():
    config = load_config()
    logger.info(f"Starting FastMCP server on port {config.mcp_port}")
    mcp.run(
        transport="http",
        host="0.0.0.0",
        port=config.mcp_port,
        stateless_http=True,
    )


if __name__ == "__main__":
    main()
