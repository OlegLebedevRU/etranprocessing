from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import check_license, check_org_status, get_current_terminal
from app.models import Terminal

router = APIRouter()


def xml_response(content: str) -> Response:
    return Response(
        content=f"<?xml version='1.0' encoding='UTF-8'?>\n{content}",
        media_type="application/xml",
    )


@router.get("")
@router.post("")
@router.get("/check")
@router.post("/check")
async def license_check(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    """
    License billing check endpoint.
    Compatible with legacy licensebilling gate.ashx format.
    """
    try:
        license_ = await check_license(terminal, db)
        await check_org_status(terminal, db)
        return xml_response(
            "<Response>"
            "<Result>OK</Result>"
            "<state>ok</state>"
            f"<balance>{license_.balance}</balance>"
            "</Response>"
        )
    except Exception as e:
        return xml_response(
            "<Response>"
            "<Result>ERROR</Result>"
            "<state>error</state>"
            f"<description>{str(e)}</description>"
            "</Response>"
        )
