"""User billing API for MenuBuilder.

All endpoints are tenant-scoped via JWT org_id claim.
"""

import logging
from dataclasses import dataclass
from datetime import UTC, datetime
from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import aliased

from app.auth import get_current_user
from app.config import settings
from app.database import get_db
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
    ConfirmPaymentResponse,
    DeactivateTerminalResponse,
    OrderStatus,
    ReactivationCheckoutRequest,
    ReactivationCheckoutResponse,
)
from app.schemas.certificate_pin import PaymentRequiredResponse, PinReadyResponse
from app.services.billing import (
    BillingStatus,
    TerminalBillingInfo,
    add_months_from_anchor,
    build_org_summary_data,
    calculate_period_price,
    compute_terminal_billing,
    is_license_lapsed,
    project_expiration_after_payment,
    resolve_monthly_price,
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
from app.services.payment_provider import get_payment_provider

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/billing", tags=["billing"])


@dataclass(frozen=True, slots=True)
class BillingUser:
    username: str
    org_id: int
    is_superuser: bool = False


async def get_current_billing_user(
    user: dict = Depends(get_current_user),
) -> BillingUser:
    """Dependency: require active tenant context (org_id > 0)."""
    org_id = user.get("org_id")
    if org_id is None or org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    return BillingUser(
        username=user.get("username", ""),
        org_id=int(org_id),
        is_superuser=bool(user.get("is_superuser", False)),
    )


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
    """The most recent paid-and-ready PIN still awaiting installation, if any."""
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
    type_name = (
        getattr(getattr(terminal, "terminal_type", None), "name", None) or "Стандартный"
    )

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
        address=getattr(terminal, "address", None),
        note=getattr(terminal, "note", None),
        terminal_type_id=getattr(terminal, "terminal_type_id", 0) or 0,
        terminal_type_name=type_name,
        created_at=getattr(terminal, "created_at", None),
    )


async def _get_all_terminal_billing(
    db: AsyncSession,
    org_id: int,
    org_settings: OrgBillingSettings,
    as_of: datetime,
) -> list[TerminalBillingInfo]:
    """Get billing info for all terminals of an organization."""
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
        # Keep the most recent per terminal
        if (
            pin.terminal_id not in pending_pin_by_terminal
            or pin.created_at > pending_pin_by_terminal[pin.terminal_id].created_at
        ):
            pending_pin_by_terminal[pin.terminal_id] = pin

    infos = []
    for terminal, license_ in rows:
        cert_price, cert_operation = _resolve_cert_pricing(
            org_settings, terminal.cert_serial
        )
        pending_pin = pending_pin_by_terminal.get(terminal.id)
        type_name = (
            getattr(getattr(terminal, "terminal_type", None), "name", None)
            or "Стандартный"
        )
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
                address=getattr(terminal, "address", None),
                note=getattr(terminal, "note", None),
                terminal_type_id=getattr(terminal, "terminal_type_id", 0) or 0,
                terminal_type_name=type_name,
                created_at=getattr(terminal, "created_at", None),
            )
        )
    return infos


@router.get("/summary", response_model=BillingSummaryRead)
async def get_billing_summary(
    user: BillingUser = Depends(get_current_billing_user),
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
    user: BillingUser = Depends(get_current_billing_user),
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
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Schedule deactivation of a terminal's license at expiration."""
    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)
    org_settings = await _get_org_settings(db, user.org_id)

    info = await _get_terminal_billing_data(db, terminal, org_settings)
    as_of = datetime.now(UTC)
    billing = compute_terminal_billing(
        info,
        as_of,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )

    if not billing.can_deactivate:
        raise HTTPException(
            status_code=400,
            detail="Terminal cannot be deactivated in its current state",
        )

    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one()
    license_.renewal_enabled = False
    license_.deactivation_requested_at = as_of

    await db.commit()

    updated_info = await _get_terminal_billing_data(db, terminal, org_settings)
    updated_billing = compute_terminal_billing(
        updated_info,
        as_of,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )

    return DeactivateTerminalResponse(
        terminal_id=terminal.id,
        status=updated_billing.billing_status,
        works_until=license_.expires_at,
        overdue_amount_minor=updated_billing.overdue_amount_minor,
        included_in_forecast=updated_billing.included_in_forecast,
    )


@router.post(
    "/terminals/{terminal_id}/cancel-deactivation",
    response_model=CancelDeactivationResponse,
)
async def cancel_deactivation(
    terminal_id: int,
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Cancel scheduled deactivation, restoring automatic renewal."""
    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)
    org_settings = await _get_org_settings(db, user.org_id)

    info = await _get_terminal_billing_data(db, terminal, org_settings)
    as_of = datetime.now(UTC)
    billing = compute_terminal_billing(
        info,
        as_of,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )

    if not billing.can_cancel_deactivation:
        raise HTTPException(
            status_code=400,
            detail="Deactivation cannot be cancelled in current state",
        )

    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one()
    license_.renewal_enabled = True
    license_.deactivation_requested_at = None

    await db.commit()

    updated_info = await _get_terminal_billing_data(db, terminal, org_settings)
    updated_billing = compute_terminal_billing(
        updated_info,
        as_of,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )

    return CancelDeactivationResponse(
        terminal_id=terminal.id,
        status=updated_billing.billing_status,
        renewal_enabled=True,
    )


@router.post("/checkout", response_model=CheckoutResponse)
async def create_checkout(
    body: CheckoutRequest,
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Create a checkout for paying overdue and/or advance periods."""
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
            if billing.billing_status in (
                BillingStatus.DISABLED,
                BillingStatus.ADMIN_DISABLED,
            ):
                raise HTTPException(
                    status_code=400,
                    detail=f"Terminal {item.terminal_id} is disabled.",
                )

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
                    "new_expires_at": billing.license_expires_at or as_of,
                    "billing_period_months": billing.billing_period_months,
                    "monthly_price_minor": billing.monthly_price_minor,
                    "old_expires_at": billing.license_expires_at or as_of,
                    "cert_policy_snapshot": build_cert_policy_snapshot(
                        policy, operation, cert_price
                    ),
                }
            )

    provider = get_payment_provider()
    import uuid

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

    order_item_responses = []
    for ci in checkout_items:
        db_item = BillingOrderItem(
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
        db.add(db_item)
        order_item_responses.append(
            CheckoutItemResponse(
                terminal_id=ci["terminal_id"],
                operation=ci["operation"],
                periods_due=ci["periods_due"],
                advance_periods=ci["advance_periods"],
                amount_minor=ci["amount_minor"],
                new_expires_at=ci["new_expires_at"],
            )
        )

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

    return CheckoutResponse(
        order_id=str(order_id),
        currency=org_settings.currency,
        amount_minor=total_amount,
        payment_url=payment_url,
        items=order_item_responses,
    )


@router.post(
    "/terminals/{terminal_id}/reactivation-checkout",
    response_model=ReactivationCheckoutResponse,
)
async def reactivation_checkout(
    terminal_id: int,
    body: ReactivationCheckoutRequest,
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Create a checkout for reactivating a disabled terminal."""
    if body.advance_periods not in (1, 2):
        raise HTTPException(
            status_code=400,
            detail="advance_periods must be 1 or 2 for reactivation",
        )

    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)
    org_settings = await _get_org_settings(db, user.org_id)

    info = await _get_terminal_billing_data(db, terminal, org_settings)
    as_of = datetime.now(UTC)
    billing = compute_terminal_billing(
        info,
        as_of,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )

    if not billing.can_reactivate:
        raise HTTPException(
            status_code=400,
            detail="Terminal cannot be reactivated in its current state",
        )

    result = await db.execute(
        select(License).where(
            License.terminal_id == terminal.id,
            License.is_active == True,
        )
    )
    license_ = result.scalar_one_or_none()

    billing_period_months = license_.billing_period_months if license_ else 1
    monthly_price = resolve_monthly_price(
        license_.monthly_price_override_minor if license_ else None,
        org_settings.monthly_price_minor,
    )
    period_price = calculate_period_price(monthly_price, billing_period_months)
    total_amount = body.advance_periods * period_price

    now = datetime.now(UTC)
    new_expires_at = add_months_from_anchor(
        now, billing_period_months * body.advance_periods
    )

    provider = get_payment_provider()
    import uuid

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
    """Create (or idempotently reuse) the CertificatePin for a paid cert_pin order item."""
    existing = await db.execute(
        select(CertificatePin).where(CertificatePin.order_item_id == item.id)
    )
    if existing.scalar_one_or_none() is not None:
        return

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
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Confirm a pending payment. Updates license.expires_at for each item."""
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

    if order.status != OrderStatus.PENDING:
        return ConfirmPaymentResponse(
            order_id=str(order.id),
            status=OrderStatus(order.status),
            paid_at=order.paid_at,
            items_updated=0,
        )

    provider = get_payment_provider()
    if not await provider.verify_payment(str(order.id)):
        raise HTTPException(status_code=402, detail="Payment verification failed")

    result = await db.execute(
        select(BillingOrderItem).where(BillingOrderItem.order_id == order_uuid)
    )
    items = result.scalars().all()

    now = datetime.now(UTC)

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
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Get billing order status. Used by clients to poll after payment."""
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
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
) -> PinReadyResponse | PaymentRequiredResponse:
    """Request permission to create a PIN for a terminal (organizational cert billing)."""
    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)
    org_settings = await _get_org_settings(db, user.org_id)

    if not org_settings.tenant_pin_creation_enabled:
        raise HTTPException(
            status_code=403,
            detail="Tenant self-service PIN creation is not enabled for this organization",
        )

    now = datetime.now(UTC)

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
        existing_pin.status = "expired"

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
            terminal.id,
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

    provider = get_payment_provider()
    import uuid

    order_id = uuid.uuid4()
    order = BillingOrder(
        id=order_id,
        org_id=user.org_id,
        status="pending",
        currency=policy.currency,
        amount_minor=price_minor,
    )
    db.add(order)
    await db.flush()

    info = await _get_terminal_billing_data(db, terminal, org_settings)
    billing = compute_terminal_billing(
        info,
        now,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )
    item = BillingOrderItem(
        order_id=order_id,
        terminal_id=terminal.id,
        operation="cert_pin",
        periods_due=0,
        advance_periods=0,
        billing_period_months=billing.billing_period_months,
        monthly_price_minor=billing.monthly_price_minor,
        amount_minor=price_minor,
        old_expires_at=billing.license_expires_at or now,
        new_expires_at=billing.license_expires_at or now,
        cert_policy_snapshot=build_cert_policy_snapshot(policy, operation, price_minor),
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
        "mode=%s price=%d order=%s",
        user.org_id,
        terminal.id,
        user.username,
        policy.mode.value,
        price_minor,
        order_id,
    )
    return PaymentRequiredResponse(
        terminal_id=terminal.id,
        order_id=str(order_id),
        amount_minor=price_minor,
        currency=policy.currency,
        payment_url=payment_url,
    )
