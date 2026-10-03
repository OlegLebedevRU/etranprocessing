"""Compute transport permissions without overwriting administrative activation."""

from datetime import UTC, datetime, timedelta
from zoneinfo import ZoneInfo

from etranprocessing_db.l4desk import (
    L4DeskAuditEvent,
    L4DeskTenantProfile,
    L4DeskTerminal,
)
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import Terminal
from app.schemas.leo4proxy import Leo4ProxyPolicy


def get_leo4proxy_policy(
    terminal: Terminal, subscription_allowed: bool = True
) -> Leo4ProxyPolicy:
    allowed = terminal.is_active and subscription_allowed
    return Leo4ProxyPolicy(
        sn=terminal.sn,
        mqtt_rtp_allowed=allowed,
        outgoing_https_allowed=True,
        stop_facts=[] if allowed else ["terminal_inactive"],
    )


async def subscription_allowance(db: AsyncSession, terminal: Terminal) -> bool:
    profile = await db.get(L4DeskTenantProfile, terminal.org_id)
    if profile is None:
        return True  # Classic session records are not a subscription enrollment.
    subscription = await db.scalar(
        select(L4DeskTerminal).where(
            L4DeskTerminal.terminal_id == terminal.id,
            L4DeskTerminal.tenant_id == terminal.org_id,
        )
    )
    if subscription is None or subscription.deleted_at is not None:
        return False
    free_id = await db.scalar(
        select(L4DeskTerminal.terminal_id)
        .where(
            L4DeskTerminal.tenant_id == terminal.org_id,
            L4DeskTerminal.deleted_at.is_(None),
        )
        .order_by(L4DeskTerminal.ordinal)
        .limit(1)
    )
    if free_id == terminal.id:
        return True
    if not settings.yookassa_enabled or subscription.paid_until is None:
        return False
    paid = subscription.paid_until
    if paid.tzinfo is None:
        paid = paid.replace(tzinfo=UTC)
    grace = (
        paid.astimezone(ZoneInfo(profile.timezone)) + timedelta(days=3)
    ).astimezone(UTC)
    terms = await db.scalar(
        select(L4DeskAuditEvent)
        .where(
            L4DeskAuditEvent.tenant_id == terminal.org_id,
            L4DeskAuditEvent.event_type == "subscription.terms",
            L4DeskAuditEvent.subject_id == str(terminal.id),
        )
        .order_by(L4DeskAuditEvent.id.desc())
        .limit(1)
    )
    details = terms.details if terms else None
    if details and details.get("paid_until") == paid.isoformat():
        grace = datetime.fromisoformat(details["grace_until"])
    return datetime.now(UTC) < grace
