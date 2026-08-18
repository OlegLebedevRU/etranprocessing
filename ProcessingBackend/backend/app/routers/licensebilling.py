import logging

from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response

from app.dependencies import (
    TerminalLicenseState,
    get_terminal_license_state,
)

logger = logging.getLogger(__name__)

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
    license_state: TerminalLicenseState = Depends(get_terminal_license_state),
):
    """
    License billing check endpoint.
    Compatible with legacy licensebilling gate.ashx format.

    Returns Result=OK for all recognized terminals.
    State is computed: 'ok' if license is valid, 'error' otherwise.
    Standard license expiry is a business state, not an exception.
    """

    balance = license_state.license.balance if license_state.license else 0

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        f"<state>{license_state.state}</state>"
        f"<balance>{balance}</balance>"
        "</Response>"
    )
