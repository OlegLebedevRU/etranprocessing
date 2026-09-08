from datetime import UTC, datetime

from etranprocessing_gauge import create_or_update_snapshot, parse_gauge_pack
from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response
from sqlalchemy import select
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import Terminal, TerminalGaugeState

router = APIRouter()


def xml_response(content: str) -> Response:
    return Response(
        content=f"<?xml version='1.0' encoding='UTF-8'?>\n{content}",
        media_type="application/xml",
    )


@router.post("")
async def post_gauge(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    """
    GateGauge endpoint - receives telemetry data from terminals.
    Compatible with legacy GateGauge main.ashx format.
    Persists operational retain snapshot in PostgreSQL terminal_gauge_states table.
    """
    body = await request.body()
    raw_text = body.decode("windows-1251", errors="replace")

    # Parse GaugePack from form data
    gauge_raw = ""
    for param in raw_text.split("&"):
        if param.startswith("GaugePack="):
            gauge_raw = param[len("GaugePack=") :]
            break

    if not gauge_raw:
        return xml_response(
            "<Response>"
            "<Result>ERROR</Result>"
            "<description>Missing GaugePack parameter</description>"
            "</Response>"
        )

    gauge_data = parse_gauge_pack(gauge_raw)

    curr_state = await db.scalar(
        select(TerminalGaugeState).where(
            TerminalGaugeState.device_id == terminal.device_id
        )
    )
    existing_snapshot = None
    if curr_state:
        existing_snapshot = {
            "device_id": curr_state.device_id,
            "sn": curr_state.sn,
            "updated_at": curr_state.updated_at.isoformat()
            if curr_state.updated_at
            else None,
            "last_tick_epoch": curr_state.last_tick_epoch,
            "slots_bitmask": curr_state.slots_bitmask,
            "gauge": curr_state.gauge_data or {},
            "internal_enrichment": curr_state.internal_enrichment or {},
        }

    now_ts = datetime.now(UTC)
    snapshot = create_or_update_snapshot(
        device_id=terminal.device_id,
        sn=terminal.sn,
        gauge_data=gauge_data,
        existing_snapshot=existing_snapshot,
        now_ts=now_ts,
    )

    stmt = (
        insert(TerminalGaugeState)
        .values(
            device_id=terminal.device_id,
            sn=terminal.sn,
            updated_at=now_ts,
            last_tick_epoch=snapshot["last_tick_epoch"],
            slots_bitmask=snapshot["slots_bitmask"],
            gauge_data=snapshot["gauge"],
            internal_enrichment=snapshot.get("internal_enrichment", {}),
        )
        .on_conflict_do_update(
            index_elements=["device_id"],
            set_={
                "sn": terminal.sn,
                "updated_at": now_ts,
                "last_tick_epoch": snapshot["last_tick_epoch"],
                "slots_bitmask": snapshot["slots_bitmask"],
                "gauge_data": snapshot["gauge"],
            },
        )
    )
    await db.execute(stmt)
    await db.commit()

    return xml_response("<Response><Result>OK</Result></Response>")
