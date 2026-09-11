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
    CalculateItemResponse,
    CalculateResponse,
    CancelDeactivationResponse,
    CheckoutItemRequest,
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
from app.security.permissions import (
    PERMISSION_BILLING_VIEW,
    require_permission,
    require_readonly_guard,
)
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
    validate_order_item_periods,
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

router = APIRouter(
    prefix="/api/billing",
    tags=["billing"],
    dependencies=[Depends(require_readonly_guard)],
)


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
    operation = resolve_operation_type(cert_serial)
    if getattr(org_settings, "billing_mode", None) == "master":
        return 0, operation.value
    policy = resolve_cert_policy(org_settings)
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
        license_id=license_.terminal_id if license_ else None,
        license_expires_at=license_.expires_at if license_ else None,
        renewal_enabled=terminal.is_active,
        deactivation_requested_at=None,
        billing_period_months=license_.billing_period_months if license_ else 1,
        monthly_price_override_minor=license_.monthly_price_override_minor
        if license_
        else None,
        org_monthly_price_minor=org_settings.monthly_price_minor,
        org_currency=org_settings.currency,
        billing_mode=getattr(org_settings, "billing_mode", "standard") or "standard",
        cert_serial=terminal.cert_serial,
        cert_not_valid_after=terminal.cert_not_valid_after,
        tenant_pin_creation_enabled=(
            True
            if getattr(org_settings, "billing_mode", None) == "master"
            else org_settings.tenant_pin_creation_enabled
        ),
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
            active_license.terminal_id == Terminal.id,
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
                license_id=license_.terminal_id if license_ else None,
                license_expires_at=license_.expires_at if license_ else None,
                renewal_enabled=terminal.is_active,
                deactivation_requested_at=None,
                billing_period_months=license_.billing_period_months if license_ else 1,
                monthly_price_override_minor=license_.monthly_price_override_minor
                if license_
                else None,
                org_monthly_price_minor=org_settings.monthly_price_minor,
                org_currency=org_settings.currency,
                billing_mode=getattr(org_settings, "billing_mode", "standard")
                or "standard",
                cert_serial=terminal.cert_serial,
                cert_not_valid_after=terminal.cert_not_valid_after,
                tenant_pin_creation_enabled=(
                    True
                    if getattr(org_settings, "billing_mode", None) == "master"
                    else org_settings.tenant_pin_creation_enabled
                ),
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
    _perm: dict = Depends(require_permission(PERMISSION_BILLING_VIEW)),
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
        results,
        org_settings.currency,
        org_settings.monthly_price_minor,
        as_of,
        billing_mode=getattr(org_settings, "billing_mode", "standard") or "standard",
        min_billing_periods=getattr(org_settings, "min_billing_periods", 1) or 1,
        allowed_billing_periods=getattr(org_settings, "allowed_billing_periods", None),
        default_selection_mode=getattr(
            org_settings, "default_selection_mode", "all_due"
        )
        or "all_due",
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
        billing_mode=summary.billing_mode,
        min_billing_periods=summary.min_billing_periods,
        allowed_billing_periods=summary.allowed_billing_periods,
        default_selection_mode=summary.default_selection_mode,
    )


@router.get("/terminals", response_model=list[BillingTerminalRead])
async def get_billing_terminals(
    status: str | None = None,
    search: str | None = None,
    user: BillingUser = Depends(get_current_billing_user),
    _perm: dict = Depends(require_permission(PERMISSION_BILLING_VIEW)),
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
        BillingStatus.DISABLED: 3,
        BillingStatus.NO_LICENSE: 4,
    }
    results.sort(key=lambda r: (status_order.get(r.billing_status, 99), r.device_id))

    return results


@router.post(
    "/terminals/{terminal_id}/disable", response_model=DeactivateTerminalResponse
)
@router.post(
    "/terminals/{terminal_id}/deactivate", response_model=DeactivateTerminalResponse
)
async def deactivate_terminal(
    terminal_id: int,
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Disable a terminal operationally and exclude from billing."""
    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)
    terminal.is_active = False
    await db.commit()

    org_settings = await _get_org_settings(db, user.org_id)
    info = await _get_terminal_billing_data(db, terminal, org_settings)
    as_of = datetime.now(UTC)
    billing = compute_terminal_billing(
        info,
        as_of,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )

    return DeactivateTerminalResponse(
        terminal_id=terminal.id,
        status=billing.billing_status,
        works_until=info.license_expires_at,
        overdue_amount_minor=billing.overdue_amount_minor,
        included_in_forecast=billing.included_in_forecast,
    )


@router.post(
    "/terminals/{terminal_id}/enable", response_model=CancelDeactivationResponse
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
    """Enable terminal operationally. If expired, reset expires_at to now."""
    terminal = await _get_terminal_for_org(db, terminal_id, user.org_id)
    terminal.is_active = True
    now = datetime.now(UTC)

    result = await db.execute(select(License).where(License.terminal_id == terminal.id))
    license_ = result.scalar_one_or_none()

    if license_:
        license_.expires_at = max(license_.expires_at, now)
    else:
        license_ = License(
            terminal_id=terminal.id,
            org_id=user.org_id,
            expires_at=now,
            billing_period_months=1,
        )
        db.add(license_)

    await db.commit()

    org_settings = await _get_org_settings(db, user.org_id)
    info = await _get_terminal_billing_data(db, terminal, org_settings)
    billing = compute_terminal_billing(
        info,
        now,
        settings.billing_due_soon_days,
        settings.cert_expiring_soon_days,
    )

    return CancelDeactivationResponse(
        terminal_id=terminal.id,
        status=billing.billing_status,
        renewal_enabled=True,
        works_until=license_.expires_at,
        overdue_amount_minor=billing.overdue_amount_minor,
        included_in_forecast=billing.included_in_forecast,
    )


async def _prepare_checkout_items(
    db: AsyncSession,
    user_org_id: int,
    org_settings: OrgBillingSettings,
    items: list[CheckoutItemRequest],
    as_of: datetime,
) -> tuple[list[dict], list[CalculateItemResponse], int, int, int]:
    """Validate items and compute order items, calculate response items, and totals."""
    db_order_items = []
    calculate_items = []
    total_amount = 0
    license_total = 0
    cert_total = 0

    for item in items:
        if item.advance_periods < 0 or item.advance_periods > 120:
            raise HTTPException(
                status_code=400,
                detail=f"advance_periods must be between 0 and 120 for terminal {item.terminal_id}",
            )

        if not item.include_license and not item.include_cert_pin:
            raise HTTPException(
                status_code=400,
                detail=f"Nothing selected to pay for terminal {item.terminal_id}",
            )

        terminal = await _get_terminal_for_org(db, item.terminal_id, user_org_id)
        info = await _get_terminal_billing_data(db, terminal, org_settings)
        billing = compute_terminal_billing(
            info,
            as_of,
            settings.billing_due_soon_days,
            settings.cert_expiring_soon_days,
        )

        if billing.billing_status in (
            BillingStatus.DISABLED,
            BillingStatus.ADMIN_DISABLED,
        ):
            raise HTTPException(
                status_code=400,
                detail=f"Terminal {item.terminal_id} is disabled. Enable it first.",
            )

        item_license_amount = 0
        item_cert_amount = 0
        license_operation = "renewal"
        periods_due = 0
        total_periods = 0
        new_expires_at: datetime | None = None

        if item.include_license:
            lapsed = is_license_lapsed(billing.license_expires_at, as_of)
            license_operation = "reactivation" if lapsed else "renewal"
            periods_due = 1 if lapsed else 0
            total_periods = periods_due + item.advance_periods

            if total_periods == 0:
                raise HTTPException(
                    status_code=400,
                    detail=f"No periods to pay for terminal {item.terminal_id}",
                )

            try:
                validate_order_item_periods(
                    billing_mode=getattr(org_settings, "billing_mode", "standard")
                    or "standard",
                    total_periods=total_periods,
                    advance_periods=item.advance_periods,
                    billing_period_months=billing.billing_period_months,
                    min_billing_periods=getattr(org_settings, "min_billing_periods", 1)
                    or 1,
                    allowed_billing_periods=getattr(
                        org_settings, "allowed_billing_periods", None
                    ),
                )
            except ValueError as e:
                raise HTTPException(status_code=400, detail=str(e))

            if org_settings.billing_mode in ("cert_linked", "master"):
                item_license_amount = 0
            else:
                item_license_amount = total_periods * billing.period_price_minor

            license_total += item_license_amount
            total_amount += item_license_amount

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

            db_order_items.append(
                {
                    "terminal_id": item.terminal_id,
                    "operation": license_operation,
                    "periods_due": periods_due,
                    "advance_periods": item.advance_periods,
                    "amount_minor": item_license_amount,
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

            pending = await _get_pending_cert_pin(db, item.terminal_id, as_of)
            if pending:
                raise HTTPException(
                    status_code=409,
                    detail=f"A pending PIN already exists for terminal {item.terminal_id}",
                )

            item_cert_amount = cert_price
            cert_total += item_cert_amount
            total_amount += item_cert_amount

            policy = resolve_cert_policy(org_settings)
            operation = resolve_operation_type(billing.cert_serial)

            db_order_items.append(
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

        calculate_items.append(
            CalculateItemResponse(
                terminal_id=item.terminal_id,
                operation=license_operation if item.include_license else "cert_pin",
                periods_due=periods_due,
                advance_periods=item.advance_periods if item.include_license else 0,
                total_periods=total_periods,
                license_amount_minor=item_license_amount,
                cert_amount_minor=item_cert_amount,
                total_item_amount_minor=item_license_amount + item_cert_amount,
                new_expires_at=new_expires_at,
            )
        )

    return db_order_items, calculate_items, total_amount, license_total, cert_total


@router.post("/calculate", response_model=CalculateResponse)
async def calculate_billing(
    body: CheckoutRequest,
    user: BillingUser = Depends(get_current_billing_user),
    db: AsyncSession = Depends(get_db),
):
    """Calculate billing amounts and preview changes without creating an order."""
    if not body.items:
        raise HTTPException(status_code=400, detail="No items in request")

    org_settings = await _get_org_settings(db, user.org_id)
    as_of = datetime.now(UTC)

    (
        _,
        calculate_items,
        total_amount,
        license_total,
        cert_total,
    ) = await _prepare_checkout_items(db, user.org_id, org_settings, body.items, as_of)

    return CalculateResponse(
        currency=org_settings.currency,
        total_amount_minor=total_amount,
        license_amount_minor=license_total,
        cert_amount_minor=cert_total,
        items=calculate_items,
    )


@router.post("/checkout", response_model=CheckoutResponse)
@router.post("/orders", response_model=CheckoutResponse)
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

    checkout_items, _, total_amount, _, _ = await _prepare_checkout_items(
        db, user.org_id, org_settings, body.items, as_of
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
    if body.advance_periods < 1 or body.advance_periods > 120:
        raise HTTPException(
            status_code=400,
            detail="advance_periods must be between 1 and 120 for reactivation",
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
        )
    )
    license_ = result.scalar_one_or_none()

    billing_period_months = license_.billing_period_months if license_ else 1
    monthly_price = resolve_monthly_price(
        license_.monthly_price_override_minor if license_ else None,
        org_settings.monthly_price_minor,
        billing_mode=getattr(org_settings, "billing_mode", "standard") or "standard",
    )
    period_price = calculate_period_price(monthly_price, billing_period_months)

    try:
        validate_order_item_periods(
            billing_mode=getattr(org_settings, "billing_mode", "standard")
            or "standard",
            total_periods=body.advance_periods,
            advance_periods=body.advance_periods,
            billing_period_months=billing_period_months,
            min_billing_periods=getattr(org_settings, "min_billing_periods", 1) or 1,
            allowed_billing_periods=getattr(
                org_settings, "allowed_billing_periods", None
            ),
        )
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))

    if org_settings.billing_mode in ("cert_linked", "master"):
        total_amount = 0
    else:
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
            )
        )
        license_ = result.scalar_one_or_none()

        if license_ is None:
            license_ = License(
                terminal_id=item.terminal_id,
                org_id=order.org_id,
                expires_at=item.new_expires_at,
                billing_period_months=item.billing_period_months,
            )
            db.add(license_)
        else:
            license_.expires_at = item.new_expires_at

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
    _perm: dict = Depends(require_permission(PERMISSION_BILLING_VIEW)),
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
    is_master = getattr(org_settings, "billing_mode", None) == "master"

    if not org_settings.tenant_pin_creation_enabled and not is_master:
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

    if not is_master:
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
    price_minor = 0 if is_master else resolve_effective_price(policy, operation)

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
