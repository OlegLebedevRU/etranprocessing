"""Pydantic schemas for certificate PIN contract (L4D-06A-PB)."""

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class IssueCertificatePinRequest(BaseModel):
    """Request payload to idempotently issue a one-time certificate PIN."""

    model_config = ConfigDict(extra="ignore")

    operation_id: str = Field(
        ...,
        min_length=1,
        max_length=128,
        description="Durable idempotency key from MenuBuilder onboarding saga",
    )
    correlation_id: str | None = Field(
        default=None,
        max_length=128,
        description="Distributed correlation ID for tracing across services",
    )
    tenant_id: int = Field(
        ...,
        description="Tenant / organization ID (terminals.org_id / l4desk_terminals.tenant_id)",
    )
    terminal_id: int = Field(
        ...,
        description="Business terminal ID (terminals.id / l4desk_terminals.terminal_id)",
    )
    sn: str = Field(
        ...,
        min_length=1,
        max_length=100,
        description="Hardware serial number of the terminal (terminals.sn)",
    )
    ttl_seconds: int = Field(
        default=86400,
        ge=60,
        le=2592000,
        description="PIN lifetime in seconds (default: 86400 / 24 hours)",
    )
    actor: str | None = Field(
        default="system",
        max_length=128,
        description="Actor or component requesting the PIN",
    )


class IssueCertificatePinResponse(BaseModel):
    """Response payload for certificate PIN issuance and idempotent replay."""

    model_config = ConfigDict(extra="ignore")

    operation_id: str = Field(
        ...,
        description="Operation idempotency key",
    )
    correlation_id: str | None = Field(
        default=None,
        description="Distributed correlation ID",
    )
    tenant_id: int = Field(
        ...,
        description="Tenant / organization ID",
    )
    terminal_id: int = Field(
        ...,
        description="Terminal ID",
    )
    sn: str = Field(
        ...,
        description="Terminal hardware serial number",
    )
    pin: str | None = Field(
        default=None,
        description="Plaintext 6-digit PIN. Returned on initial issuance and active replay; None once consumed or expired.",
    )
    pin_masked: str = Field(
        ...,
        description="Masked PIN (e.g. '***773') safe for logging, audit, and display.",
    )
    status: Literal["issued", "consumed", "expired"] = Field(
        ...,
        description="Current status of the PIN in the enrollment lifecycle.",
    )
    expires_at: datetime = Field(
        ...,
        description="PIN expiration timestamp (UTC)",
    )
    created_at: datetime = Field(
        ...,
        description="PIN creation timestamp (UTC)",
    )
    replayed: bool = Field(
        default=False,
        description="True if the result was returned from an idempotent replay of the operation_id.",
    )


class CertificatePinErrorDetail(BaseModel):
    """Structured error model for certificate PIN contract operations."""

    model_config = ConfigDict(extra="ignore")

    error: str
    message: str
    error_code: str
    operation_id: str | None = None
