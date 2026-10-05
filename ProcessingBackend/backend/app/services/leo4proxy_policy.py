"""Compute transport permissions without overwriting administrative activation."""

import json
import logging
from datetime import UTC, datetime, timedelta
from typing import Literal
from urllib.parse import urlsplit
from zoneinfo import ZoneInfo

from etranprocessing_access import subscription_allowed, subscription_state
from etranprocessing_db.l4desk import (
    L4DeskAuditEvent,
    L4DeskTenantProfile,
    L4DeskTerminal,
)
from pydantic import ValidationError
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import Terminal
from app.schemas.leo4proxy import Leo4ProxyEndpoint, Leo4ProxyPolicy

logger = logging.getLogger(__name__)


def configured_endpoints() -> dict | None:
    """Keep invalid routing configuration independent of admission decisions."""
    if not settings.leo4proxy_endpoints:
        return None
    try:
        raw = json.loads(settings.leo4proxy_endpoints)
    except ValueError, TypeError:
        logger.warning("Invalid leo4proxy endpoints JSON; omitting routing extension")
        return None
    if not isinstance(raw, dict):
        return None
    result = {}
    for channel in ("mqtt", "https", "l4rtp", "l4stream"):
        entry = raw.get(channel)
        if not isinstance(entry, dict):
            continue
        try:
            result[channel] = Leo4ProxyEndpoint.model_validate(entry)
        except ValidationError:
            # A malformed IP list must not discard valid host/port or permissions.
            try:
                result[channel] = Leo4ProxyEndpoint.model_validate(
                    {"host": entry.get("host"), "port": entry.get("port")}
                )
            except ValidationError:
                logger.warning(
                    "Invalid leo4proxy endpoint for %s; omitting channel", channel
                )
    return result or None


def get_leo4proxy_policy(
    terminal: Terminal,
    subscription_allowed: bool = True,
    *,
    now: datetime | None = None,
) -> Leo4ProxyPolicy:
    expires = terminal.cert_not_valid_after
    if expires is not None and expires.tzinfo is None:
        expires = expires.replace(tzinfo=UTC)
    certificate_expired = expires is not None and expires <= (now or datetime.now(UTC))
    allowed = terminal.is_active and subscription_allowed and not certificate_expired
    stop_facts: list[Literal["terminal_inactive", "certificate_expired"]] = (
        [] if terminal.is_active and subscription_allowed else ["terminal_inactive"]
    )
    if certificate_expired:
        stop_facts.append("certificate_expired")
    endpoints = configured_endpoints()
    ttl = settings.leo4proxy_endpoints_ttl_seconds
    fm_endpoint = None
    if settings.file_manager_service_key and settings.file_manager_s3_bucket:
        try:
            storage = urlsplit(settings.file_manager_s3_endpoint)
            if (
                storage.scheme == "https"
                and storage.hostname
                and not storage.username
                and not storage.password
                and storage.path in ("", "/")
                and not storage.query
                and not storage.fragment
            ):
                fm_endpoint = Leo4ProxyEndpoint(
                    host=storage.hostname, port=storage.port or 443
                )
        except ValueError, ValidationError:
            logger.warning("Invalid FM storage endpoint; transport remains disabled")
    return Leo4ProxyPolicy(
        sn=terminal.sn,
        mqtt_rtp_allowed=allowed,
        fm_allowed=allowed and fm_endpoint is not None,
        fm_storage_endpoint=fm_endpoint if allowed else None,
        outgoing_https_allowed=True,
        stop_facts=stop_facts,
        endpoints=endpoints,
        endpoints_ttl_seconds=max(300, min(ttl, 604800)) if endpoints else None,
    )


async def subscription_allowance(
    db: AsyncSession, terminal: Terminal, *, now: datetime | None = None
) -> bool:
    if (
        settings.product_scope_split_enabled
        and not terminal.l4desk_subscription_enabled
    ):
        return True
    profile = await db.get(L4DeskTenantProfile, terminal.org_id)
    if profile is None:
        return not settings.product_scope_split_enabled
    subscription = await db.scalar(
        select(L4DeskTerminal).where(
            L4DeskTerminal.terminal_id == terminal.id,
            L4DeskTerminal.tenant_id == terminal.org_id,
        )
    )
    if subscription is None or subscription.deleted_at is not None:
        return False
    stmt = (
        select(L4DeskTerminal.terminal_id)
        .where(
            L4DeskTerminal.tenant_id == terminal.org_id,
            L4DeskTerminal.deleted_at.is_(None),
        )
        .order_by(L4DeskTerminal.ordinal)
        .limit(1)
    )
    if settings.product_scope_split_enabled:
        stmt = stmt.join(
            Terminal, Terminal.id == L4DeskTerminal.runtime_terminal_id
        ).where(
            Terminal.org_id == terminal.org_id,
            Terminal.l4desk_subscription_enabled.is_(True),
        )
    free_id = await db.scalar(stmt)
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
    return subscription_allowed(
        subscription_state(
            deleted=False,
            admin_active=True,
            is_free=False,
            payments_enabled=settings.yookassa_enabled,
            paid_until=paid,
            grace_until=grace,
            now=now or datetime.now(UTC),
        )
    )
