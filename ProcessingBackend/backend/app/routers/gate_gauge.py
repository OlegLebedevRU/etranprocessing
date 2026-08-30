from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import Terminal
from app.services.gauge_bus import gauge_mqtt_bus, gauge_store
from app.services.gauge_engine import create_or_update_snapshot, parse_gauge_pack

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
    Maintains operational retain snapshot in RabbitMQ MQTT / in-memory store.
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
    existing_snapshot = gauge_store.get_by_device_id(terminal.device_id)

    snapshot = create_or_update_snapshot(
        device_id=terminal.device_id,
        sn=terminal.sn,
        gauge_data=gauge_data,
        existing_snapshot=existing_snapshot,
    )

    gauge_mqtt_bus.publish_snapshot(terminal.sn, snapshot)

    return xml_response("<Response><Result>OK</Result></Response>")
