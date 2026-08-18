"""Pure billing calculation functions.

All functions take explicit `as_of` parameter for testability.
No DB access, no global time, no side effects.
Money values in minor units (kopecks), integer only.
"""

from dataclasses import dataclass
from datetime import UTC, datetime
from enum import StrEnum

from dateutil.relativedelta import relativedelta


class BillingStatus(StrEnum):
    ACTIVE = "active"
    DUE_SOON = "due_soon"
    OVERDUE = "overdue"
    DEACTIVATION_SCHEDULED = "deactivation_scheduled"
    DISABLED = "disabled"
    ADMIN_DISABLED = "admin_disabled"
    NO_LICENSE = "no_license"


@dataclass(frozen=True, slots=True)
class ForecastMonth:
    month: str  # "2026-08"
    amount_minor: int
    terminal_count: int


@dataclass(frozen=True, slots=True)
class OrgSummary:
    currency: str
    monthly_base_price_minor: int
    overdue_amount_minor: int
    overdue_terminal_count: int
    active_terminal_count: int
    deactivation_scheduled_count: int
    disabled_terminal_count: int
    admin_disabled_terminal_count: int
    nearest_required_payment_at: datetime | None
    forecast: list[ForecastMonth]


@dataclass(frozen=True, slots=True)
class TerminalBillingInfo:
    """Input data for a single terminal's billing calculations."""

    terminal_id: int
    device_id: int
    sn: str
    terminal_is_active: bool
    license_id: int | None
    license_expires_at: datetime | None
    renewal_enabled: bool
    deactivation_requested_at: datetime | None
    billing_period_months: int
    monthly_price_override_minor: int | None
    org_monthly_price_minor: int
    org_currency: str
    cert_serial: str | None = None
    cert_not_valid_after: datetime | None = None
    tenant_pin_creation_enabled: bool = False
    cert_pin_price_minor: int = 0
    cert_operation: str = "primary_issue"


@dataclass(frozen=True, slots=True)
class TerminalBillingResult:
    """Computed billing data for a single terminal."""

    terminal_id: int
    device_id: int
    sn: str
    terminal_is_active: bool
    license_id: int | None
    license_expires_at: datetime | None
    renewal_enabled: bool
    deactivation_requested_at: datetime | None
    billing_status: BillingStatus
    monthly_price_minor: int
    billing_period_months: int
    period_price_minor: int
    periods_due: int
    overdue_amount_minor: int
    next_payment_at: datetime | None
    next_payment_amount_minor: int
    projected_expires_at_after_debt_payment: datetime | None
    can_deactivate: bool
    can_cancel_deactivation: bool
    can_reactivate: bool
    included_in_forecast: bool
    cert_serial: str | None = None
    cert_not_valid_after: datetime | None = None
    tenant_pin_creation_enabled: bool = False
    cert_pin_price_minor: int = 0
    cert_operation: str = "primary_issue"
    cert_expiring_soon: bool = False


def add_billing_months(expires_at: datetime, months: int) -> datetime:
    """Add calendar months to a datetime, handling end-of-month edge cases."""
    return expires_at + relativedelta(months=months)


def add_months_from_anchor(anchor: datetime, total_months: int) -> datetime:
    """Add total_months to anchor in a single step, preserving the anchor day.

    Unlike iterative add_billing_months, this never loses the anchor day:
    add_months_from_anchor(2026-01-31, 2) → 2026-03-31 (not 2026-03-28).
    """
    return anchor + relativedelta(months=total_months)


def resolve_monthly_price(
    monthly_price_override_minor: int | None,
    org_monthly_price_minor: int,
) -> int:
    """Determine the effective monthly price for a terminal."""
    if monthly_price_override_minor is not None:
        return monthly_price_override_minor
    return org_monthly_price_minor


def calculate_period_price(monthly_price: int, billing_period_months: int) -> int:
    """Calculate the price of one billing period."""
    return monthly_price * billing_period_months


def calculate_periods_due(
    expires_at: datetime,
    as_of: datetime,
    period_months: int,
) -> int:
    """Calculate minimum number of billing periods needed to move expiry past as_of.

    Uses add_months_from_anchor to preserve the anchor day (H2 fix).
    """
    if period_months <= 0:
        raise ValueError(f"billing_period_months must be positive, got {period_months}")
    if expires_at > as_of:
        return 0
    count = 0
    # Use anchor-based arithmetic: add_months_from_anchor(expires_at, period_months * k)
    # preserves the original day (e.g., Jan 31 + 2 months → Mar 31, not Mar 28)
    while add_months_from_anchor(expires_at, period_months * count) <= as_of:
        count += 1
    return count


def calculate_terminal_debt(
    expires_at: datetime | None,
    renewal_enabled: bool,
    billing_period_months: int,
    period_price: int,
    as_of: datetime,
) -> int:
    """Calculate the amount required to bring a terminal back into service.

    A lapsed license is always charged exactly one billing period starting from
    `as_of` — missed periods are never billed retroactively.
    """
    if not renewal_enabled:
        return 0
    if expires_at is None:
        return 0
    if expires_at > as_of:
        return 0
    return period_price


def project_expiration_after_payment(
    expires_at: datetime,
    billing_period_months: int,
    periods_to_add: int,
    as_of: datetime,
    *,
    mode: str = "renewal",
) -> datetime:
    """Project new expiration date after paying periods.

    mode="renewal": extends from expires_at (continuous subscription).
      New date = expires_at + periods * months. Anchor day preserved.
    mode="reactivation": the license has lapsed, so the new term starts today.
      New date = as_of + periods * months (periods_to_add is at least 1).
    """
    if mode == "reactivation":
        periods = max(periods_to_add, 1)
        return add_months_from_anchor(as_of, billing_period_months * periods)
    # renewal: extend from original expires_at, preserving anchor day
    return add_months_from_anchor(expires_at, billing_period_months * periods_to_add)


def is_license_lapsed(expires_at: datetime | None, as_of: datetime) -> bool:
    """Whether the license is absent or already expired (needs a fresh term)."""
    return expires_at is None or expires_at <= as_of


def resolve_payment_mode(expires_at: datetime | None, as_of: datetime) -> str:
    """Pick the expiry projection mode for a terminal's current license state."""
    return "reactivation" if is_license_lapsed(expires_at, as_of) else "renewal"


def is_cert_expiring_soon(
    cert_serial: str | None,
    cert_not_valid_after: datetime | None,
    as_of: datetime,
    days: int,
) -> bool:
    """Whether the certificate should be offered for (re)issue by default.

    True when no certificate has ever been issued, or when the known expiry is
    within `days`. A legacy certificate with an unknown expiry is not flagged —
    there is no evidence it is about to lapse.
    """
    if cert_serial is None:
        return True
    if cert_not_valid_after is None:
        return False
    return cert_not_valid_after <= as_of + relativedelta(days=days)


def _forecast_end(forecast_months: list[str]) -> datetime:
    """Get the end date of the last forecast month."""
    if not forecast_months:
        return datetime.now(UTC)
    last = forecast_months[-1]
    year, month = map(int, last.split("-"))
    if month == 12:
        return datetime(year + 1, 1, 1, tzinfo=UTC)
    return datetime(year, month + 1, 1, tzinfo=UTC)


def resolve_billing_status(
    terminal_is_active: bool,
    renewal_enabled: bool,
    expires_at: datetime | None,
    deactivation_requested_at: datetime | None,
    as_of: datetime,
    due_soon_days: int = 30,
    has_license: bool = True,
) -> BillingStatus:
    """Determine the billing status for a terminal."""
    if not terminal_is_active:
        return BillingStatus.ADMIN_DISABLED

    if not has_license:
        return BillingStatus.NO_LICENSE

    if not renewal_enabled:
        if expires_at is not None and expires_at > as_of:
            return BillingStatus.DEACTIVATION_SCHEDULED
        return BillingStatus.DISABLED

    # renewal_enabled = True
    if expires_at is None:
        return BillingStatus.ACTIVE

    if expires_at <= as_of:
        return BillingStatus.OVERDUE

    # Check if due soon
    due_soon_threshold = as_of + relativedelta(days=due_soon_days)
    if expires_at <= due_soon_threshold:
        return BillingStatus.DUE_SOON

    return BillingStatus.ACTIVE


def compute_terminal_billing(
    info: TerminalBillingInfo,
    as_of: datetime,
    due_soon_days: int = 30,
    cert_expiring_soon_days: int = 30,
) -> TerminalBillingResult:
    """Compute all billing data for a single terminal."""
    monthly_price = resolve_monthly_price(
        info.monthly_price_override_minor,
        info.org_monthly_price_minor,
    )
    period_price = calculate_period_price(monthly_price, info.billing_period_months)

    billing_status = resolve_billing_status(
        terminal_is_active=info.terminal_is_active,
        renewal_enabled=info.renewal_enabled,
        expires_at=info.license_expires_at,
        deactivation_requested_at=info.deactivation_requested_at,
        as_of=as_of,
        due_soon_days=due_soon_days,
        has_license=info.license_id is not None,
    )

    # A lapsed license always costs exactly one period, starting from today.
    periods_due = 0
    overdue_amount = 0
    if billing_status == BillingStatus.OVERDUE:
        periods_due = 1
        overdue_amount = period_price

    # Next payment
    next_payment_at: datetime | None = None
    next_payment_amount = 0
    if (
        billing_status in (BillingStatus.ACTIVE, BillingStatus.DUE_SOON)
        and info.license_expires_at is not None
    ):
        next_payment_at = info.license_expires_at
        next_payment_amount = period_price

    # Projected expiry after paying the outstanding period: the term restarts today.
    projected_expires_at: datetime | None = None
    if billing_status == BillingStatus.OVERDUE:
        projected_expires_at = add_months_from_anchor(as_of, info.billing_period_months)

    # Capability flags
    can_deactivate = (
        info.renewal_enabled and info.license_id is not None and info.terminal_is_active
    )
    can_cancel_deactivation = (
        not info.renewal_enabled
        and info.license_expires_at is not None
        and info.license_expires_at > as_of
        and info.terminal_is_active
    )
    can_reactivate = (
        not info.renewal_enabled
        and (info.license_expires_at is None or info.license_expires_at <= as_of)
        and info.terminal_is_active
    )

    included_in_forecast = (
        info.renewal_enabled
        and info.terminal_is_active
        and billing_status not in (BillingStatus.OVERDUE, BillingStatus.NO_LICENSE)
    )

    return TerminalBillingResult(
        terminal_id=info.terminal_id,
        device_id=info.device_id,
        sn=info.sn,
        terminal_is_active=info.terminal_is_active,
        license_id=info.license_id,
        license_expires_at=info.license_expires_at,
        renewal_enabled=info.renewal_enabled,
        deactivation_requested_at=info.deactivation_requested_at,
        billing_status=billing_status,
        monthly_price_minor=monthly_price,
        billing_period_months=info.billing_period_months,
        period_price_minor=period_price,
        periods_due=periods_due,
        overdue_amount_minor=overdue_amount,
        next_payment_at=next_payment_at,
        next_payment_amount_minor=next_payment_amount,
        projected_expires_at_after_debt_payment=projected_expires_at,
        can_deactivate=can_deactivate,
        can_cancel_deactivation=can_cancel_deactivation,
        can_reactivate=can_reactivate,
        included_in_forecast=included_in_forecast,
        cert_serial=info.cert_serial,
        cert_not_valid_after=info.cert_not_valid_after,
        tenant_pin_creation_enabled=info.tenant_pin_creation_enabled,
        cert_pin_price_minor=info.cert_pin_price_minor,
        cert_operation=info.cert_operation,
        cert_expiring_soon=is_cert_expiring_soon(
            info.cert_serial,
            info.cert_not_valid_after,
            as_of,
            cert_expiring_soon_days,
        ),
    )


def build_org_forecast(
    terminals: list[TerminalBillingResult],
    as_of: datetime,
) -> list[ForecastMonth]:
    """Build 3-month forecast for the organization."""
    # Generate the 3 forecast month keys
    forecast_months: list[str] = []
    for i in range(3):
        d = as_of + relativedelta(months=i)
        forecast_months.append(f"{d.year}-{d.month:02d}")

    # Aggregate by month
    totals: dict[str, int] = {m: 0 for m in forecast_months}
    counts: dict[str, int] = {m: 0 for m in forecast_months}

    for t in terminals:
        if not t.included_in_forecast:
            continue
        if t.license_expires_at is None:
            continue

        # Simulate renewals for this terminal using anchor-based arithmetic
        period_price = t.period_price_minor
        anchor = t.license_expires_at

        for k in range(24):
            current = add_months_from_anchor(anchor, t.billing_period_months * k)
            if current > _forecast_end(forecast_months):
                break
            month_key = f"{current.year}-{current.month:02d}"
            if month_key in totals:
                totals[month_key] += period_price
                counts[month_key] += 1

    return [
        ForecastMonth(
            month=m,
            amount_minor=totals[m],
            terminal_count=counts[m],
        )
        for m in forecast_months
    ]


def build_org_summary_data(
    terminals: list[TerminalBillingResult],
    org_currency: str,
    org_monthly_price_minor: int,
    as_of: datetime,
) -> OrgSummary:
    """Build organization summary from computed terminal data."""
    overdue_amount = sum(t.overdue_amount_minor for t in terminals)
    overdue_count = sum(
        1 for t in terminals if t.billing_status == BillingStatus.OVERDUE
    )
    active_count = sum(
        1
        for t in terminals
        if t.billing_status in (BillingStatus.ACTIVE, BillingStatus.DUE_SOON)
    )
    deactivation_count = sum(
        1 for t in terminals if t.billing_status == BillingStatus.DEACTIVATION_SCHEDULED
    )
    disabled_count = sum(
        1 for t in terminals if t.billing_status == BillingStatus.DISABLED
    )
    admin_count = sum(
        1 for t in terminals if t.billing_status == BillingStatus.ADMIN_DISABLED
    )

    # Nearest required payment
    nearest: datetime | None = None
    for t in terminals:
        if (
            t.next_payment_at is not None
            and t.next_payment_amount_minor > 0
            and (nearest is None or t.next_payment_at < nearest)
        ):
            nearest = t.next_payment_at

    forecast = build_org_forecast(terminals, as_of)

    return OrgSummary(
        currency=org_currency,
        monthly_base_price_minor=org_monthly_price_minor,
        overdue_amount_minor=overdue_amount,
        overdue_terminal_count=overdue_count,
        active_terminal_count=active_count,
        deactivation_scheduled_count=deactivation_count,
        disabled_terminal_count=disabled_count,
        admin_disabled_terminal_count=admin_count,
        nearest_required_payment_at=nearest,
        forecast=forecast,
    )
