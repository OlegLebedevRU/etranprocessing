"""Pydantic schemas for the tenant certificate-PIN billing API."""

from datetime import datetime
from typing import Literal

from pydantic import BaseModel


class CertificatePinRequest(BaseModel):
    """Body for POST /api/terminals/{terminal_id}/certificate-pin.

    Currently no fields are required — reserved for future use (e.g. an explicit
    idempotency key), matching the analysis's idempotency key concept.
    """

    idempotency_key: str | None = None


class PinReadyResponse(BaseModel):
    status: Literal["pin_ready"] = "pin_ready"
    payment_required: Literal[False] = False
    terminal_id: int
    pin: str
    expires_at: datetime


class PaymentRequiredResponse(BaseModel):
    status: Literal["payment_required"] = "payment_required"
    payment_required: Literal[True] = True
    terminal_id: int
    order_id: str
    amount_minor: int
    currency: str
    payment_url: str | None
