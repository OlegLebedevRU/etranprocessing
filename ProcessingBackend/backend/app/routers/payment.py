"""Payment router - handles payment requests from terminals."""

import logging

from fastapi import APIRouter, Depends, Request
from fastapi.responses import Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.logging_config import payment_logger
from app.models import Terminal
from app.services.payment_service import PaymentService, parse_params_string

logger = logging.getLogger(__name__)

router = APIRouter()


def xml_response(content: str) -> Response:
    """Create XML response with proper encoding."""
    return Response(
        content=f"<?xml version='1.0' encoding='UTF-8'?>\n{content}",
        media_type="application/xml",
    )


def success_response(paym_id: int, paym_ext_id: str, paym_state: int = 2) -> Response:
    """Create success XML response."""
    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        f"<PaymNumb>{paym_id}</PaymNumb>"
        f"<PaymState>{paym_state}</PaymState>"
        f"<PaymExtId>{paym_ext_id}</PaymExtId>"
        "<Description>Payment accepted.</Description>"
        "</Response>"
    )


def error_response(paym_ext_id: str, description: str) -> Response:
    """Create error XML response."""
    return xml_response(
        "<Response>"
        "<Result>ERROR</Result>"
        f"<PaymExtId>{paym_ext_id}</PaymExtId>"
        f"<Description>{description}</Description>"
        "</Response>"
    )


@router.post("/etran.ashx")
@router.post("")
async def payment(
    request: Request,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    """
    Handle payment request from terminal.

    Expected parameters:
    - function: "payment", "check", "update", "addparams"
    - PaymExtId: External payment ID (format: KKKK_DDMMYY_NNNNNNNN)
    - PaymSubjTp: TSP code
    - Amount: Amount in kopeks
    - Params: Parameter string (format: "1 value1;2 value2")
    - TotalSum: Display sum (optional)
    - Signature: Signature (optional)
    - PayTypeId: Payment type (optional)
    """
    body = await request.body()
    params = _parse_request_params(request, body)

    function = params.get("function", "").lower()
    paym_ext_id = params.get("PaymExtId", "")

    logger.info(
        f"Request: function={function}, ext_id={paym_ext_id}, terminal={terminal.sn}"
    )

    # Route by function
    if function == "payment":
        return await _handle_payment(params, terminal, db)
    elif function == "check":
        return await _handle_check(params, terminal, db)
    elif function in ("update", "addparams"):
        return await _handle_update(params, terminal, db)
    else:
        return error_response(paym_ext_id, f"Unknown function: {function}")


async def _handle_payment(
    params: dict, terminal: Terminal, db: AsyncSession
) -> Response:
    """Handle payment function."""
    paym_ext_id = params.get("PaymExtId", "")

    if not paym_ext_id:
        return error_response("", "PaymExtId is required")

    if len(paym_ext_id) > 20:
        return error_response(paym_ext_id, "PaymExtId too long (max 20 chars)")

    try:
        tsp_code = int(params.get("PaymSubjTp", "0"))
    except ValueError:
        return error_response(paym_ext_id, "Invalid PaymSubjTp")

    try:
        amount = int(params.get("Amount", "0"))
    except ValueError:
        return error_response(paym_ext_id, "Invalid Amount")

    if amount <= 0:
        return error_response(paym_ext_id, "Amount must be positive")

    try:
        pay_type_id = int(params.get("PayTypeId", "0"))
    except ValueError:
        pay_type_id = 0

    params_str = params.get("Params", "")
    parsed_params = parse_params_string(params_str)

    payment_logger.info(
        f"REQUEST: ext_id={paym_ext_id}, tsp={tsp_code}, "
        f"amount={amount}, pay_type={pay_type_id}, terminal={terminal.sn}, org={terminal.org_id}, "
        f"params_raw={params_str}"
    )

    try:
        service = PaymentService(db)
        payment = await service.create_payment(
            terminal=terminal,
            tsp_code=tsp_code,
            amount=amount,
            paym_ext_id=paym_ext_id,
            params=parsed_params,
            pay_type_id=pay_type_id,
        )

        payment_logger.info(f"SUCCESS: paym_id={payment.paym_id}, ext_id={paym_ext_id}")
        return success_response(payment.paym_id, paym_ext_id)

    except ValueError as e:
        payment_logger.warning(f"VALIDATION_ERROR: ext_id={paym_ext_id}, error={e}")
        return error_response(paym_ext_id, str(e))
    except Exception as e:  # noqa: BLE001
        payment_logger.error(f"ERROR: ext_id={paym_ext_id}, error={e}", exc_info=True)
        return error_response(paym_ext_id, "Internal server error")


async def _handle_check(params: dict, terminal: Terminal, db: AsyncSession) -> Response:
    """Handle check function."""
    paym_ext_id = params.get("PaymExtId", "")
    tsp_code = params.get("PaymSubjTp", "")

    logger.info(f"Check: ext_id={paym_ext_id}, tsp={tsp_code}, terminal={terminal.sn}")

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        f"<PaymExtId>{paym_ext_id}</PaymExtId>"
        "<Description>Check passed.</Description>"
        "</Response>"
    )


async def _handle_update(
    params: dict, terminal: Terminal, db: AsyncSession
) -> Response:
    """Handle update/addparams function."""
    paym_ext_id = params.get("PaymExtId", "")

    logger.info(f"Update: ext_id={paym_ext_id}, terminal={terminal.sn}")

    return xml_response(
        "<Response>"
        "<Result>OK</Result>"
        f"<PaymExtId>{paym_ext_id}</PaymExtId>"
        "<Description>Payment updated.</Description>"
        "</Response>"
    )


def _parse_request_params(request: Request, body: bytes) -> dict:
    """Extract params from query string and/or POST body."""
    from urllib.parse import unquote_plus

    params = dict(request.query_params)
    if body:
        raw = body.decode("windows-1251", errors="replace")
        # Decode URL encoding (+ = space, %XX = char)
        raw = unquote_plus(raw)
        for param in raw.split("&"):
            if "=" in param:
                k, v = param.split("=", 1)
                params[k] = v
    return params
