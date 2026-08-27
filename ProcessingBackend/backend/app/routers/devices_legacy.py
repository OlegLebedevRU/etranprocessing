"""Legacy device compatibility router for companion PFX certificate generation."""

from datetime import UTC, datetime

from fastapi import APIRouter, Depends

from app.dependencies import get_current_terminal
from app.models import Terminal
from app.schemas.devices_legacy import LegacyPfxResponse
from app.services.companion_cert import issue_companion_pfx_certificate

router = APIRouter()


@router.get(
    "/api/devices/map_legacy_crt/",
    response_model=LegacyPfxResponse,
    summary="Issue companion PFX certificate for authenticated terminal",
    description=(
        "Generates a secondary/companion PKCS#12 (PFX) certificate bundle for an authenticated terminal. "
        "Stateless operation: does not mutate or persist cert_serial in the database. "
        "Validity duration defaults to the remaining validity of the terminal's active certificate."
    ),
)
@router.get(
    "/api/v1/devices/map_legacy_crt/",
    response_model=LegacyPfxResponse,
    include_in_schema=False,
)
async def map_legacy_crt(
    terminal: Terminal = Depends(get_current_terminal),
) -> LegacyPfxResponse:
    """Issue companion PFX certificate for the authenticated terminal."""
    exp_days = 365
    if terminal.cert_not_valid_after:
        now_utc = datetime.now(UTC)
        dt = terminal.cert_not_valid_after
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=UTC)
        remaining = (dt - now_utc).days
        if remaining > 0:
            exp_days = remaining

    return await issue_companion_pfx_certificate(
        terminal=terminal,
        exp_days=exp_days,
        sign="temporary",
    )
