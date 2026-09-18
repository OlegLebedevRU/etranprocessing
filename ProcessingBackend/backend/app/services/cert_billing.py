"""PIN utility functions for certificate management in ProcessingBackend."""

import secrets
from datetime import UTC, datetime, timedelta

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import CertificatePin

PIN_LENGTH = 6
_MAX_PIN_GENERATION_ATTEMPTS = 20


def mask_pin(pin: str) -> str:
    """Mask a PIN for safe logging/audit, e.g. '773773' -> '***773'."""
    if not pin:
        return pin
    tail = pin[-3:] if len(pin) > 3 else pin
    return f"***{tail}"


def generate_pin(length: int = PIN_LENGTH) -> str:
    """Generate a cryptographically random numeric PIN of the given length."""
    return "".join(secrets.choice("0123456789") for _ in range(length))


async def generate_unique_pin(db: AsyncSession, length: int = PIN_LENGTH) -> str:
    """Generate a PIN guaranteed unique against active certificate_pins.

    Retries on collision; raises RuntimeError if unable to find one quickly.
    """
    for _ in range(_MAX_PIN_GENERATION_ATTEMPTS):
        candidate = generate_pin(length)
        existing = await db.execute(
            select(CertificatePin.id).where(CertificatePin.pin == candidate)
        )
        if existing.scalar_one_or_none() is None:
            return candidate
    raise RuntimeError("Unable to generate a unique certificate PIN")


def compute_pin_expiry(
    now: datetime | None = None, ttl_seconds: int | None = None
) -> datetime:
    """Compute expiration timestamp for a PIN."""
    now = now or datetime.now(UTC)
    ttl = (
        ttl_seconds
        if ttl_seconds is not None
        else getattr(settings, "cert_pin_ttl_seconds", 86400)
    )
    return now + timedelta(seconds=ttl)
