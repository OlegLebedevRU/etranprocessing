"""User billing API — JSON endpoints for MenuBuilder.

All endpoints are tenant-scoped via JWT org_id claim.
"""

import logging
from datetime import UTC, datetime

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.dependencies import JwtUser, get_current_user_jwt
from app.models import (
    BillingOrder,
    BillingOrderItem,
    CertificatePin,
    License,
    OrgBillingSettings,
    Terminal,
)
from app.schemas.billing import (
    BillingForecastMonthRead,
    BillingOrderRead,
    BillingSummaryRead,
    BillingTerminalRead,
    CancelDeactivationResponse,
    CheckoutItemResponse,
    CheckoutRequest,
    CheckoutResponse,
    DeactivateTerminalResponse,
    ReactivationCheckoutRequest,
    ReactivationCheckoutResponse,
)
from app.schemas.certificate_pin import PaymentRequiredResponse, PinReadyResponse
from app.services.billing import (
    BillingStatus,
    TerminalBillingInfo,
    add_months_from_anchor,
    build_org_summary_data,
    compute_terminal_billing,
    is_license_lapsed,
    project_expiration_after_payment,
)
from app.services.cert_billing import (
    build_cert_policy_snapshot,
    compute_pin_expiry,
    generate_unique_pin,
    mask_pin,
    resolve_cert_policy,
    resolve_effective_price,
    resolve_operation_type,
)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/billing", tags=["billing"])


async def _get_org_settings(db: AsyncSession, org_id: int) -> OrgBillingSettings:
    """Get org billing settings or raise 409."""
    result = await db.execute(
        select(OrgBillingSettings).where(OrgBillingSettings.org_id == org_id)
    )
    settings_obj = result.scalar_one_or_none()
    if not settings_obj:
        raise HTTPException(
            status_code=409,
            detail="Billing settings are not configured for organization",
        )
    return settings_obj


async def _get_terminal_for_org(
    db: AsyncSession, terminal_id: int, org_id: int
) -> Terminal:
    """Get terminal belonging to the org or raise 404."""
    result = await db.execute(
        select(Terminal).where(
            Terminal.id == terminal_id,
            Terminal.org_id == org_id,
        )
    )
    terminal = result.scalar_one_or_none()
    if not terminal:
        raise HTTPException(status_code=404, detail="Terminal not found")
    return terminal


def _resolve_cert_pricing(
    org_settings: OrgBillingSettings, cert_serial: str | None
) -> tuple[int, str]:
    """Effective cert PIN price and operation type for a terminal's current state."""
    policy = resolve_cert_policy(org_settings)
    operation = resolve_operation_type(cert_serial)
    return resolve_effective_price(policy, operation), operation.value


async def _get_pending_cert_pin(
    db: AsyncSession, terminal_id: int, as_of: datetime
) -> CertificatePin | None:
    """The most recent paid-and-ready PIN still awaiting installation, if any.

    A pending PIN that has not expired means the certificate question is
    already resolved for this terminal — it just needs to be entered on the
    device. It cannot be bought again until it is used or expires.
    """
    result = await db.execute(
        select(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal_id,
            CertificatePin.status == "pending",
            CertificatePin.expires_at > as_of,
        )
        .order_by(CertificatePin.created_at.desc())
        .limit(1)
    )
    return result.scalar_one_or_none()


async def _get_terminal_billing_data(
    db: AsyncSession,
    terminal: Terminal,
    org_settings: OrgBillingSettings,
) -> TerminalBillingInfo:
    """Build TerminalBillingInfo from DB data."""
    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()
    cert_price, cert_operation = _resolve_cert_pricing(
        org_settings, terminal.cert_serial
    )
    as_of = datetime.now(UTC)
    pending_pin = await _get_pending_cert_pin(db, terminal.id, as_of)

    return TerminalBillingInfo(
        terminal_id=terminal.id,
        device_id=terminal.device_id,
        sn=terminal.sn,
        terminal_is_active=terminal.is_active,
        license_id=license_.id if license_ else None,
        license_expires_at=license_.expires_at if license_ else None,
        renewal_enabled=license_.renewal_enabled if license_ else True,
        deactivation_requested_at=license_.deactivation_requested_at
        if license_
        else None,
        billing_period_months=license_.billing_period_months if license_ else 1,
        monthly_price_override_minor=license_.monthly_price_override_minor
        if license_
        else None,
        org_monthly_price_minor=org_settings.monthly_price_minor,
        org_currency=org_settings.currency,
        cert_serial=terminal.cert_serial,
        cert_not_valid_after=terminal.cert_not_valid_after,
        tenant_pin_creation_enabled=org_settings.tenant_pin_creation_enabled,
        cert_pin_price_minor=cert_price,
        cert_operation=cert_operation,
        cert_pin_pending=pending_pin is not None,
        cert_pin_expires_at=pending_pin.expires_at if pending_pin else None,
    )


async def _get_all_terminal_billing(
    db: AsyncSession,
    org_id: int,
    org_settings: OrgBillingSettings,
    as_of: datetime,
) -> list[TerminalBillingInfo]:
    """Get billing info for all terminals of an organization.

    Uses a single query with LEFT JOIN instead of N+1 per-terminal queries.
    """
    from sqlalchemy.orm import aliased

    active_license = aliased(License)
    result = await db.execute(
        select(Terminal, active_license)
        .outerjoin(
            active_license,
            (active_license.terminal_id == Terminal.id)
            & (active_license.is_active == True),
        )
        .where(Terminal.org_id == org_id)
    )
    rows = result.all()

    # One extra query for the whole org instead of one per terminal.
    pending_pins_result = await db.execute(
        select(CertificatePin).where(
            CertificatePin.terminal_id.in_([t.id for t, _ in rows]),
            CertificatePin.status == "pending",
            CertificatePin.expires_at > as_of,
        )
    )
    pending_pin_by_terminal: dict[int, CertificatePin] = {}
    for pin in pending_pins_result.scalars():
        # Keep the most recently created one if there happens to be more than one.
        existing = pending_pin_by_terminal.get(pin.terminal_id)
        if existing is None or pin.created_at > existing.created_at:
            pending_pin_by_terminal[pin.terminal_id] = pin

    infos = []
    for terminal, license_ in rows:
        cert_price, cert_operation = _resolve_cert_pricing(
            org_settings, terminal.cert_serial
        )
        pending_pin = pending_pin_by_terminal.get(terminal.id)
        infos.append(
            TerminalBillingInfo(
                terminal_id=terminal.id,
                device_id=terminal.device_id,
                sn=terminal.sn,
                terminal_is_active=terminal.is_active,
                license_id=license_.id if license_ else None,
                license_expires_at=license_.expires_at if license_ else None,
                renewal_enabled=license_.renewal_enabled if license_ else True,
                deactivation_requested_at=license_.deactivation_requested_at
                if license_
                else None,
                billing_period_months=license_.billing_period_months if license_ else 1,
                monthly_price_override_minor=license_.monthly_price_override_minor
                if license_
                else None,
                org_monthly_price_minor=org_settings.monthly_price_minor,
                org_currency=org_settings.currency,
                cert_serial=terminal.cert_serial,
                cert_not_valid_after=terminal.cert_not_valid_after,
                tenant_pin_creation_enabled=org_settings.tenant_pin_creation_enabled,
                cert_pin_price_minor=cert_price,
                cert_operation=cert_operation,
                cert_pin_pending=pending_pin is not None,
                cert_pin_expires_at=pending_pin.expires_at if pending_pin else None,
            )
        )
    return infos


@router.get("/summary", response_model=BillingSummaryRead)
async def get_billing_summary(
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Get billing summary for the organization."""
    org_settings = await _get_org_settings(db, user.org_id)
    as_of = datetime.now(UTC)

    infos = await _get_all_terminal_billing(db, user.org_id, org_settings, as_of)
    results = [
        compute_terminal_billing(
            info,
            as_of,
            settings.billing_due_soon_days,
            settings.cert_expiring_soon_days,
        )
        for info in infos
    ]

    summary = build_org_summary_data(
        results, org_settings.currency, org_settings.monthly_price_minor, as_of
    )

    return BillingSummaryRead(
        as_of=as_of,
        currency=summary.currency,
        monthly_base_price_minor=summary.monthly_base_price_minor,
        overdue_amount_minor=summary.overdue_amount_minor,
        overdue_terminal_count=summary.overdue_terminal_count,
        active_terminal_count=summary.active_terminal_count,
        deactivation_scheduled_count=summary.deactivation_scheduled_count,
        disabled_terminal_count=summary.disabled_terminal_count,
        admin_disabled_terminal_count=summary.admin_disabled_terminal_count,
        nearest_required_payment_at=summary.nearest_required_payment_at,
        forecast=[
            BillingForecastMonthRead.model_validate(fm) for fm in summary.forecast
        ],
    )


@router.get("/terminals", response_model=list[BillingTerminalRead])
async def get_billing_terminals(
    status: str | None = None,
    search: str | None = None,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Get billing details for all terminals of the organization."""
    org_settings = await _get_org_settings(db, user.org_id)
    as_of = datetime.now(UTC)

    infos = await _get_all_terminal_billing(db, user.org_id, org_settings, as_of)
    results = [
        compute_terminal_billing(
            info,
            as_of,
            settings.billing_due_soon_days,
            settings.cert_expiring_soon_days,
        )
        for info in infos
    ]

    # Filter by status
    if status:
        try:
            target_status = BillingStatus(status)
        except ValueError:
            raise HTTPException(status_code=400, detail=f"Invalid status: {status}")
        results = [r for r in results if r.billing_status == target_status]

    # Filter by search
    if search:
        search_lower = search.lower()
        results = [
            r
            for r in results
            if search_lower in str(r.device_id) or search_lower in r.sn.lower()
        ]

    # Sort: overdue first, then by status priority, then by device_id
    status_order = {
        BillingStatus.OVERDUE: 0,
        BillingStatus.DUE_SOON: 1,
        BillingStatus.ACTIVE: 2,
        BillingStatus.DEACTIVATION_SCHEDULED: 3,
        BillingStatus.DISABLED: 4,
        BillingStatus.ADMIN_DISABLED: 5,
    }
    results.sort(key=lambda r: (status_order.get(r.billing_status, 99), r.device_id))

    return results


@router.post(
    "/terminals/{terminal_id}/deactivate", response_model=DeactivateTerminalResponse
)
async def deactivate_terminal(
    terminal_id: int,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Deactivate a terminal from future renewals. Idempotent."""
    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)

    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()
    if not license_:
        raise HTTPException(status_code=404, detail="No active license found")

    now = datetime.now(UTC)

    # Idempotent: already deactivated
    if not license_.renewal_enabled:
        org_settings = await _get_org_settings(db, user.org_id)
        info = await _get_terminal_billing_data(db, terminal, org_settings)
        billing = compute_terminal_billing(
            info, now, settings.billing_due_soon_days, settings.cert_expiring_soon_days
        )
        return DeactivateTerminalResponse(
            terminal_id=terminal.id,
            status=billing.billing_status,
            works_until=license_.expires_at if license_.expires_at > now else None,
            overdue_amount_minor=billing.overdue_amount_minor,
            included_in_forecast=billing.included_in_forecast,
        )

    # Perform deactivation
    license_.renewal_enabled = False
    license_.deactivation_requested_at = now
    await db.commit()

    logger.info(
        "billing.deactivate org=%d terminal=%d user=%s expires_at=%s",
        user.org_id,
        terminal_id,
        user.username,
        license_.expires_at,
    )

    org_settings = await _get_org_settings(db, user.org_id)
    info = await _get_terminal_billing_data(db, terminal, org_settings)
    billing = compute_terminal_billing(
        info, now, settings.billing_due_soon_days, settings.cert_expiring_soon_days
    )

    return DeactivateTerminalResponse(
        terminal_id=terminal.id,
        status=billing.billing_status,
        works_until=license_.expires_at if license_.expires_at > now else None,
        overdue_amount_minor=billing.overdue_amount_minor,
        included_in_forecast=billing.included_in_forecast,
    )


@router.post(
    "/terminals/{terminal_id}/cancel-deactivation",
    response_model=CancelDeactivationResponse,
)
async def cancel_deactivation(
    terminal_id: int,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Cancel a scheduled deactivation. Only allowed if license is still active."""
    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)

    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()
    if not license_:
        raise HTTPException(status_code=404, detail="No active license found")

    if license_.renewal_enabled:
        raise HTTPException(
            status_code=400, detail="Terminal is not scheduled for deactivation"
        )

    now = datetime.now(UTC)

    if license_.expires_at <= now:
        raise HTTPException(
            status_code=409,
            detail="License has expired. Use reactivation checkout to reconnect.",
        )

    if not terminal.is_active:
        raise HTTPException(
            status_code=403,
            detail="Terminal is administratively disabled",
        )

    # Cancel deactivation
    license_.renewal_enabled = True
    license_.deactivation_requested_at = None
    await db.commit()

    logger.info(
        "billing.cancel_deactivation org=%d terminal=%d user=%s",
        user.org_id,
        terminal_id,
        user.username,
    )

    org_settings = await _get_org_settings(db, user.org_id)
    info = await _get_terminal_billing_data(db, terminal, org_settings)
    billing = compute_terminal_billing(
        info, now, settings.billing_due_soon_days, settings.cert_expiring_soon_days
    )

    return CancelDeactivationResponse(
        terminal_id=terminal.id,
        status=billing.billing_status,
        renewal_enabled=True,
    )


@router.post("/checkout", response_model=CheckoutResponse)
async def create_checkout(
    body: CheckoutRequest,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Create a checkout for paying overdue and/or advance periods."""
    from app.services.payment_provider import get_payment_provider

    if not body.items:
        raise HTTPException(status_code=400, detail="No items in checkout")

    org_settings = await _get_org_settings(db, user.org_id)
    as_of = datetime.now(UTC)

    # Validate all terminals belong to org
    checkout_items = []
    total_amount = 0

    for item in body.items:
        if item.advance_periods < 0 or item.advance_periods > 2:
            raise HTTPException(
                status_code=400,
                detail=f"advance_periods must be 0, 1, or 2 for terminal {item.terminal_id}",
            )

        if not item.include_license and not item.include_cert_pin:
            raise HTTPException(
                status_code=400,
                detail=f"Nothing selected to pay for terminal {item.terminal_id}",
            )

        terminal = await _get_terminal_for_org(db, item.terminal_id, user.org_id)
        info = await _get_terminal_billing_data(db, terminal, org_settings)
        billing = compute_terminal_billing(
            info,
            as_of,
            settings.billing_due_soon_days,
            settings.cert_expiring_soon_days,
        )

        if billing.billing_status == BillingStatus.ADMIN_DISABLED:
            raise HTTPException(
                status_code=400,
                detail=f"Terminal {item.terminal_id} is administratively disabled.",
            )

        if item.include_license:
            # A lapsed license (expired, disabled or missing) always restarts today
            # for exactly one period; an active one extends from its expiry date.
            lapsed = is_license_lapsed(billing.license_expires_at, as_of)
            periods_due = 1 if lapsed else 0
            total_periods = periods_due + item.advance_periods

            if total_periods == 0:
                raise HTTPException(
                    status_code=400,
                    detail=f"No periods to pay for terminal {item.terminal_id}",
                )

            amount = total_periods * billing.period_price_minor
            total_amount += amount

            if lapsed:
                new_expires_at = add_months_from_anchor(
                    as_of, billing.billing_period_months * total_periods
                )
            else:
                assert billing.license_expires_at is not None
                new_expires_at = project_expiration_after_payment(
                    billing.license_expires_at,
                    billing.billing_period_months,
                    total_periods,
                    as_of,
                    mode="renewal",
                )

            checkout_items.append(
                {
                    "terminal_id": item.terminal_id,
                    "operation": "reactivation" if lapsed else "renewal",
                    "periods_due": periods_due,
                    "advance_periods": item.advance_periods,
                    "amount_minor": amount,
                    "new_expires_at": new_expires_at,
                    "billing_period_months": billing.billing_period_months,
                    "monthly_price_minor": billing.monthly_price_minor,
                    "old_expires_at": billing.license_expires_at,
                    "cert_policy_snapshot": None,
                }
            )

        if item.include_cert_pin:
            if not org_settings.tenant_pin_creation_enabled:
                raise HTTPException(
                    status_code=403,
                    detail="Tenant self-service PIN creation is not enabled "
                    "for this organization",
                )

            cert_price = billing.cert_pin_price_minor
            if cert_price <= 0:
                raise HTTPException(
                    status_code=400,
                    detail=f"Certificate PIN for terminal {item.terminal_id} is free — "
                    "request it directly instead of paying for it",
                )

            total_amount += cert_price
            policy = resolve_cert_policy(org_settings)
            operation = resolve_operation_type(billing.cert_serial)

            checkout_items.append(
                {
                    "terminal_id": item.terminal_id,
                    "operation": "cert_pin",
                    "periods_due": 0,
                    "advance_periods": 0,
                    "amount_minor": cert_price,
                    "new_expires_at": as_of,
                    "billing_period_months": 1,
                    "monthly_price_minor": cert_price,
                    "old_expires_at": as_of,
                    "cert_policy_snapshot": build_cert_policy_snapshot(
                        policy, operation, cert_price
                    ),
                }
            )

    # M3: Idempotency guard — check for existing pending orders
    from app.models import BillingOrderItem as BOItem

    terminal_ids = [ci["terminal_id"] for ci in checkout_items]
    existing = await db.execute(
        select(BillingOrder.id)
        .join(BOItem, BOItem.order_id == BillingOrder.id)
        .where(
            BillingOrder.org_id == user.org_id,
            BillingOrder.status == "pending",
            BOItem.terminal_id.in_(terminal_ids),
        )
        .limit(1)
    )
    if existing.scalar_one_or_none():
        raise HTTPException(
            status_code=409,
            detail="A pending order already exists for one of these terminals. "
            "Confirm or wait for it to expire before creating a new one.",
        )

    # Create order
    import uuid

    provider = get_payment_provider()
    order_id = uuid.uuid4()

    order = BillingOrder(
        id=order_id,
        org_id=user.org_id,
        status="pending",
        currency=org_settings.currency,
        amount_minor=total_amount,
    )
    db.add(order)
    await db.flush()

    # Create order items
    from app.models import BillingOrderItem

    for ci in checkout_items:
        item = BillingOrderItem(
            order_id=order_id,
            terminal_id=ci["terminal_id"],
            operation=ci["operation"],
            periods_due=ci["periods_due"],
            advance_periods=ci["advance_periods"],
            billing_period_months=ci["billing_period_months"],
            monthly_price_minor=ci["monthly_price_minor"],
            amount_minor=ci["amount_minor"],
            old_expires_at=ci["old_expires_at"] or as_of,
            new_expires_at=ci["new_expires_at"],
            cert_policy_snapshot=ci["cert_policy_snapshot"],
        )
        db.add(item)

    # Get payment URL from provider
    try:
        payment_url = await provider.create_checkout(
            amount_minor=total_amount,
            currency=org_settings.currency,
            order_id=str(order_id),
        )
        order.payment_url = payment_url
    except Exception:
        logger.exception("Payment provider error")
        raise HTTPException(status_code=502, detail="Payment provider error")

    await db.commit()

    logger.info(
        "billing.checkout org=%d user=%s order=%s amount=%d items=%d",
        user.org_id,
        user.username,
        order_id,
        total_amount,
        len(checkout_items),
    )

    return CheckoutResponse(
        order_id=str(order_id),
        currency=org_settings.currency,
        amount_minor=total_amount,
        payment_url=payment_url,
        items=[
            CheckoutItemResponse(
                terminal_id=ci["terminal_id"],
                operation=ci["operation"],
                periods_due=ci["periods_due"],
                advance_periods=ci["advance_periods"],
                amount_minor=ci["amount_minor"],
                new_expires_at=None
                if ci["operation"] == "cert_pin"
                else ci["new_expires_at"],
            )
            for ci in checkout_items
        ],
    )


@router.post(
    "/terminals/{terminal_id}/reactivation-checkout",
    response_model=ReactivationCheckoutResponse,
)
async def create_reactivation_checkout(
    terminal_id: int,
    body: ReactivationCheckoutRequest,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Create a checkout for reactivating a disabled terminal."""
    from app.services.payment_provider import get_payment_provider

    if body.advance_periods < 1 or body.advance_periods > 2:
        raise HTTPException(status_code=400, detail="advance_periods must be 1 or 2")

    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)

    # H5: Admin-disabled terminals cannot be reactivated by users
    if not terminal.is_active:
        raise HTTPException(
            status_code=403,
            detail="Terminal is administratively disabled. Contact support.",
        )

    org_settings = await _get_org_settings(db, user.org_id)

    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()

    now = datetime.now(UTC)

    # Terminal must be disabled (renewal_enabled=false and expired)
    if license_ and license_.renewal_enabled:
        raise HTTPException(
            status_code=400,
            detail="Terminal is not disabled. Use regular checkout.",
        )
    if license_ and license_.expires_at and license_.expires_at > now:
        raise HTTPException(
            status_code=400,
            detail="Terminal license is still active. Cancel deactivation instead.",
        )

    # Calculate price
    billing_period_months = license_.billing_period_months if license_ else 1
    monthly_price_override = license_.monthly_price_override_minor if license_ else None
    from app.services.billing import calculate_period_price, resolve_monthly_price

    monthly_price = resolve_monthly_price(
        monthly_price_override, org_settings.monthly_price_minor
    )
    period_price = calculate_period_price(monthly_price, billing_period_months)

    total_amount = body.advance_periods * period_price

    # New expiry starts from now (reactivation mode)
    from app.services.billing import add_months_from_anchor

    new_expires_at = add_months_from_anchor(
        now, billing_period_months * body.advance_periods
    )

    # Create order
    import uuid

    provider = get_payment_provider()
    order_id = uuid.uuid4()

    order = BillingOrder(
        id=order_id,
        org_id=user.org_id,
        status="pending",
        currency=org_settings.currency,
        amount_minor=total_amount,
    )
    db.add(order)
    await db.flush()

    from app.models import BillingOrderItem

    item = BillingOrderItem(
        order_id=order_id,
        terminal_id=terminal_id,
        operation="reactivation",
        periods_due=0,
        advance_periods=body.advance_periods,
        billing_period_months=billing_period_months,
        monthly_price_minor=monthly_price,
        amount_minor=total_amount,
        old_expires_at=license_.expires_at if license_ and license_.expires_at else now,
        new_expires_at=new_expires_at,
    )
    db.add(item)

    try:
        payment_url = await provider.create_checkout(
            amount_minor=total_amount,
            currency=org_settings.currency,
            order_id=str(order_id),
        )
        order.payment_url = payment_url
    except Exception:
        logger.exception("Payment provider error")
        raise HTTPException(status_code=502, detail="Payment provider error")

    await db.commit()

    return ReactivationCheckoutResponse(
        order_id=str(order_id),
        currency=org_settings.currency,
        amount_minor=total_amount,
        months=body.advance_periods * billing_period_months,
        new_expires_at=new_expires_at,
        payment_url=payment_url,
    )


async def _apply_cert_pin_item(
    db: AsyncSession, order: BillingOrder, item: BillingOrderItem
) -> None:
    """Create (or idempotently reuse) the CertificatePin for a paid cert_pin order item.

    Does NOT call the CA — that only ever happens in certificates.py `setup`.
    """
    # Idempotent: order_item already has a PIN (e.g. webhook retried).
    existing = await db.execute(
        select(CertificatePin).where(CertificatePin.order_item_id == item.id)
    )
    if existing.scalar_one_or_none() is not None:
        return

    # Idempotent: terminal already has an unexpired pending PIN — link it to this order item.
    result = await db.execute(
        select(CertificatePin).where(
            CertificatePin.terminal_id == item.terminal_id,
            CertificatePin.status == "pending",
        )
    )
    pending_pin = result.scalar_one_or_none()
    if pending_pin is not None:
        pending_pin.order_item_id = item.id
        return

    pin_value = await generate_unique_pin(db)
    expires_at = compute_pin_expiry()
    cert_pin = CertificatePin(
        pin=pin_value,
        terminal_id=item.terminal_id,
        org_id=order.org_id,
        order_item_id=item.id,
        creation_source="tenant",
        payment_required=True,
        status="pending",
        expires_at=expires_at,
    )
    db.add(cert_pin)

    logger.info(
        "cert_pin.created_after_payment org=%d terminal=%d order_item=%d pin=%s",
        order.org_id,
        item.terminal_id,
        item.id,
        mask_pin(pin_value),
    )


@router.post("/orders/{order_id}/confirm")
async def confirm_payment(
    order_id: str,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Confirm a pending payment. Updates license.expires_at for each item.

    With mock provider this is called directly by the frontend after checkout.
    With a real provider this would be called by the provider's webhook.
    """
    from uuid import UUID

    from app.models import BillingOrderItem
    from app.schemas.billing import ConfirmPaymentResponse, OrderStatus
    from app.services.payment_provider import get_payment_provider

    try:
        order_uuid = UUID(order_id)
    except ValueError:
        raise HTTPException(status_code=400, detail="Invalid order ID format")

    # Find the order
    result = await db.execute(
        select(BillingOrder).where(
            BillingOrder.id == order_uuid,
            BillingOrder.org_id == user.org_id,
        )
    )
    order = result.scalar_one_or_none()
    if not order:
        raise HTTPException(status_code=404, detail="Order not found")

    if order.status != OrderStatus.PENDING:
        return ConfirmPaymentResponse(
            order_id=str(order.id),
            status=OrderStatus(order.status),
            paid_at=order.paid_at,
            items_updated=0,
        )

    # Verify payment with provider
    provider = get_payment_provider()
    if not await provider.verify_payment(str(order.id)):
        raise HTTPException(status_code=402, detail="Payment verification failed")

    # Load order items
    result = await db.execute(
        select(BillingOrderItem).where(BillingOrderItem.order_id == order_uuid)
    )
    items = result.scalars().all()

    now = datetime.now(UTC)

    # Apply each item: renewal/reactivation update license; cert_pin creates a PIN.
    # NOTE: this must never call the CA (sign_csr) — that stays in certificates.py setup.
    for item in items:
        if item.operation == "cert_pin":
            await _apply_cert_pin_item(db, order, item)
            continue

        result = await db.execute(
            select(License).where(
                License.terminal_id == item.terminal_id,
                License.is_active == True,
            )
        )
        license_ = result.scalar_one_or_none()

        if license_ is None:
            # Create new license for terminal without one
            license_ = License(
                terminal_id=item.terminal_id,
                org_id=order.org_id,
                expires_at=item.new_expires_at,
                billing_period_months=item.billing_period_months,
                renewal_enabled=True,
            )
            db.add(license_)
        else:
            license_.expires_at = item.new_expires_at
            license_.renewal_enabled = True
            license_.deactivation_requested_at = None

    # Mark order as paid
    order.status = OrderStatus.PAID
    order.paid_at = now

    await db.commit()

    logger.info(
        "Payment confirmed: order=%s org=%d items=%d",
        order.id,
        order.org_id,
        len(items),
    )

    return ConfirmPaymentResponse(
        order_id=str(order.id),
        status=OrderStatus.PAID,
        paid_at=now,
        items_updated=len(items),
    )


@router.get("/orders/{order_id}", response_model=BillingOrderRead)
async def get_order(
    order_id: str,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
):
    """Get billing order status. Used by clients to poll after payment (e.g. cert-pin flow)."""
    from uuid import UUID

    try:
        order_uuid = UUID(order_id)
    except ValueError:
        raise HTTPException(status_code=400, detail="Invalid order ID format")

    result = await db.execute(
        select(BillingOrder).where(
            BillingOrder.id == order_uuid,
            BillingOrder.org_id == user.org_id,
        )
    )
    order = result.scalar_one_or_none()
    if not order:
        raise HTTPException(status_code=404, detail="Order not found")

    return BillingOrderRead.model_validate(order)


@router.post("/terminals/{terminal_id}/certificate-pin")
async def create_certificate_pin(
    terminal_id: int,
    user: JwtUser = Depends(get_current_user_jwt),
    db: AsyncSession = Depends(get_db),
) -> PinReadyResponse | PaymentRequiredResponse:
    """Request permission to create a PIN for a terminal (organizational cert billing).

    This endpoint never calls the CA. It only decides whether the operation is free
    (mode=none or price=0) — in which case a PIN is created immediately — or paid,
    in which case a billing order is created and payment_required is returned.
    The PIN is later consumed by the existing terminal check/setup flow
    (routers/certificates.py), which is the only place `sign_csr()` is invoked.
    """
    from app.models import BillingOrderItem
    from app.services.payment_provider import get_payment_provider

    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)
    org_settings = await _get_org_settings(db, user.org_id)

    if not org_settings.tenant_pin_creation_enabled:
        raise HTTPException(
            status_code=403,
            detail="Tenant self-service PIN creation is not enabled for this organization",
        )

    now = datetime.now(UTC)

    # Idempotency: an unexpired pending PIN already exists for this terminal.
    result = await db.execute(
        select(CertificatePin).where(
            CertificatePin.terminal_id == terminal.id,
            CertificatePin.status == "pending",
        )
    )
    existing_pin = result.scalar_one_or_none()
    if existing_pin is not None:
        if existing_pin.expires_at > now:
            return PinReadyResponse(
                terminal_id=terminal.id,
                pin=existing_pin.pin,
                expires_at=existing_pin.expires_at,
            )
        # Expired but not yet swept — mark it so a new one can be created below.
        existing_pin.status = "expired"

    # Idempotency: a pending order for a cert_pin operation on this terminal already exists.
    result = await db.execute(
        select(BillingOrder)
        .join(BillingOrderItem, BillingOrderItem.order_id == BillingOrder.id)
        .where(
            BillingOrder.org_id == user.org_id,
            BillingOrder.status == "pending",
            BillingOrderItem.terminal_id == terminal.id,
            BillingOrderItem.operation == "cert_pin",
        )
        .limit(1)
    )
    pending_order = result.scalar_one_or_none()
    if pending_order is not None:
        return PaymentRequiredResponse(
            terminal_id=terminal.id,
            order_id=str(pending_order.id),
            amount_minor=pending_order.amount_minor,
            currency=pending_order.currency,
            payment_url=pending_order.payment_url,
        )

    policy = resolve_cert_policy(org_settings)
    operation = resolve_operation_type(terminal.cert_serial)
    price_minor = resolve_effective_price(policy, operation)

    if price_minor <= 0:
        # Free: mode=none, or per_operation with price=0/not-billable operation.
        pin_value = await generate_unique_pin(db)
        expires_at = compute_pin_expiry(now)
        cert_pin = CertificatePin(
            pin=pin_value,
            terminal_id=terminal.id,
            org_id=user.org_id,
            creation_source="tenant",
            payment_required=False,
            status="pending",
            expires_at=expires_at,
        )
        db.add(cert_pin)
        await db.commit()

        logger.info(
            "cert_pin.request org=%d terminal=%d user=%s result=pin_ready "
            "mode=%s price=%d pin=%s",
            user.org_id,
            terminal_id,
            user.username,
            policy.mode.value,
            price_minor,
            mask_pin(pin_value),
        )
        return PinReadyResponse(
            terminal_id=terminal.id,
            pin=pin_value,
            expires_at=expires_at,
        )

    # Paid: create an order, defer PIN creation until payment is confirmed.
    import uuid

    provider = get_payment_provider()
    order_id = uuid.uuid4()
    snapshot = build_cert_policy_snapshot(policy, operation, price_minor)

    order = BillingOrder(
        id=order_id,
        org_id=user.org_id,
        status="pending",
        currency=policy.currency,
        amount_minor=price_minor,
    )
    db.add(order)
    await db.flush()

    item = BillingOrderItem(
        order_id=order_id,
        terminal_id=terminal.id,
        operation="cert_pin",
        periods_due=0,
        advance_periods=0,
        billing_period_months=1,
        monthly_price_minor=price_minor,
        amount_minor=price_minor,
        old_expires_at=now,
        new_expires_at=now,
        cert_policy_snapshot=snapshot,
    )
    db.add(item)

    try:
        payment_url = await provider.create_checkout(
            amount_minor=price_minor,
            currency=policy.currency,
            order_id=str(order_id),
        )
        order.payment_url = payment_url
    except Exception:
        logger.exception("Payment provider error")
        raise HTTPException(status_code=502, detail="Payment provider error")

    await db.commit()

    logger.info(
        "cert_pin.request org=%d terminal=%d user=%s result=payment_required "
        "order=%s amount=%d",
        user.org_id,
        terminal_id,
        user.username,
        order_id,
        price_minor,
    )

    return PaymentRequiredResponse(
        terminal_id=terminal.id,
        order_id=str(order_id),
        amount_minor=price_minor,
        currency=policy.currency,
        payment_url=payment_url,
    )
