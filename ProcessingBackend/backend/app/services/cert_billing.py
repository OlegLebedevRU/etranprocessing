"""Certificate PIN billing — org-level tariff policy + PIN issuance helpers.

Billing subject is the *permission to create a PIN* for a terminal (not CA issuance).
CA issuance (`sign_csr`) only ever happens in `routers/certificates.py` `setup` handler —
this module and the billing router must never call it.
"""

import secrets
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from enum import StrEnum

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import CertificatePin, OrgBillingSettings

PIN_LENGTH = 6
_MAX_PIN_GENERATION_ATTEMPTS = 20


class CertBillingMode(StrEnum):
    NONE = "none"
    PER_OPERATION = "per_operation"


class CertOperationType(StrEnum):
    """Whether this PIN request is for a first-time cert or a re-issue."""

    PRIMARY_ISSUE = "primary_issue"
    REISSUE = "reissue"


@dataclass(frozen=True, slots=True)
class CertPolicy:
    """Org-level cert billing policy (source of truth — no per-terminal override)."""

    mode: CertBillingMode
    price_minor: int | None
    currency: str
    tenant_pin_creation_enabled: bool
    charge_primary_issue: bool
    charge_reissue: bool


def resolve_cert_policy(org_settings: OrgBillingSettings) -> CertPolicy:
    """Build a CertPolicy from org_billing_settings row."""
    return CertPolicy(
        mode=CertBillingMode(org_settings.cert_billing_mode),
        price_minor=org_settings.cert_price_minor,
        currency=org_settings.currency,
        tenant_pin_creation_enabled=org_settings.tenant_pin_creation_enabled,
        charge_primary_issue=org_settings.cert_charge_primary_issue,
        charge_reissue=org_settings.cert_charge_reissue,
    )


def resolve_operation_type(terminal_cert_serial: str | None) -> CertOperationType:
    """A terminal with no cert_serial yet is a primary issue; otherwise a reissue."""
    return (
        CertOperationType.PRIMARY_ISSUE
        if terminal_cert_serial is None
        else CertOperationType.REISSUE
    )


def is_operation_billable(policy: CertPolicy, operation: CertOperationType) -> bool:
    """Whether this specific operation is subject to the org cert tariff at all."""
    if policy.mode != CertBillingMode.PER_OPERATION:
        return False
    if operation == CertOperationType.PRIMARY_ISSUE:
        return policy.charge_primary_issue
    return policy.charge_reissue


def resolve_effective_price(policy: CertPolicy, operation: CertOperationType) -> int:
    """Effective price for the operation. 0 if not billable or price is unset."""
    if not is_operation_billable(policy, operation):
        return 0
    return policy.price_minor or 0


def build_cert_policy_snapshot(
    policy: CertPolicy, operation: CertOperationType, price_minor: int
) -> dict:
    """Immutable snapshot stored on billing_order_items.cert_policy_snapshot.

    Ensures a later change to the org tariff cannot retroactively alter an
    already-created order.
    """
    return {
        "cert_billing_mode": policy.mode.value,
        "cert_price_minor": price_minor,
        "currency": policy.currency,
        "operation": operation.value,
    }


def generate_pin(length: int = PIN_LENGTH) -> str:
    """Generate a cryptographically random numeric PIN of the given length."""
    return "".join(secrets.choice("0123456789") for _ in range(length))


async def generate_unique_pin(db: AsyncSession, length: int = PIN_LENGTH) -> str:
    """Generate a PIN guaranteed unique against certificate_pins.pin.

    Retries on collision; raises RuntimeError if unable to find one quickly
    (would indicate the PIN space is nearly exhausted).
    """
    for _ in range(_MAX_PIN_GENERATION_ATTEMPTS):
        candidate = generate_pin(length)
        existing = await db.execute(
            select(CertificatePin.id).where(CertificatePin.pin == candidate)
        )
        if existing.scalar_one_or_none() is None:
            return candidate
    raise RuntimeError("Unable to generate a unique certificate PIN")


def compute_pin_expiry(now: datetime | None = None) -> datetime:
    """TTL-based expiry for a newly created PIN (default 24h, configurable)."""
    now = now or datetime.now(UTC)
    return now + timedelta(hours=settings.cert_pin_ttl_hours)


def mask_pin(pin: str) -> str:
    """Mask a PIN for safe logging/audit, e.g. '773773' -> '***773'."""
    if not pin:
        return pin
    tail = pin[-3:] if len(pin) > 3 else pin
    return f"***{tail}"
