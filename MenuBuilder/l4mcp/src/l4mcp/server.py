import logging
import sys
from datetime import date

from fastmcp import Context, FastMCP
from fastmcp.server.lifespan import lifespan

from l4mcp.access import current_principal
from l4mcp.config import load_config
from l4mcp.console_tools import register_console_tools
from l4mcp.db import Database
from l4mcp.pin_tools import register_pin_tools
from l4mcp.reports import (
    report_balance_by_terminal,
    report_balance_by_tsp,
    report_inkass,
    report_payments,
)

logging.basicConfig(level=logging.INFO, stream=sys.stderr)
logger = logging.getLogger("l4mcp")


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


mcp = FastMCP("L4mcp", lifespan=app_lifespan)
register_pin_tools(mcp)
register_console_tools(mcp)


def _report_scope_required(
    date_from: str | None, date_to: str | None, device_ids: str | None
) -> dict | None:
    if device_ids:
        ids = [part.strip() for part in device_ids.split(",")]
        if not 1 <= len(ids) <= 20 or any(not part.isdigit() for part in ids):
            raise ValueError("Specify 1–20 exact numeric device IDs")
        return None
    if date_from and date_to:
        start, end = date.fromisoformat(date_from), date.fromisoformat(date_to)
        if not 0 <= (end - start).days <= 30:
            raise ValueError("Date range must be 0–30 days")
        return None
    return {
        "status": "filters_required",
        "next_step": "Ask the user for 1–20 exact device IDs or both dates within 30 days before querying reports.",
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
        top: Max results (1-100, default 100)
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    if missing := _report_scope_required(date_from, date_to, device_ids):
        return missing
    if not 1 <= top <= 100:
        raise ValueError("top must be 1–100")
    db: Database = ctx.lifespan_context["db"]
    org_id = (await current_principal()).scoped_org(org_id)
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
    if missing := _report_scope_required(date_from, date_to, device_ids):
        return missing
    db: Database = ctx.lifespan_context["db"]
    org_id = (await current_principal()).scoped_org(org_id)
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
    if missing := _report_scope_required(date_from, date_to, device_ids):
        return missing
    db: Database = ctx.lifespan_context["db"]
    org_id = (await current_principal()).scoped_org(org_id)
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
        size: Page size (1-100, default 100)
        org_id: Optional organization ID filter for tenant isolation
    """
    assert ctx is not None
    if missing := _report_scope_required(date_from, date_to, device_ids):
        return missing
    if page < 1 or not 1 <= size <= 100:
        raise ValueError("page must be positive and size must be 1–100")
    db: Database = ctx.lifespan_context["db"]
    org_id = (await current_principal()).scoped_org(org_id)
    return await report_inkass(
        db, date_from, date_to, device_ids, page, size, org_id=org_id
    )


# === Custom routes ===


@mcp.custom_route("/health", methods=["GET"])
async def health(request):
    from starlette.responses import JSONResponse

    return JSONResponse({"status": "ok"})


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
