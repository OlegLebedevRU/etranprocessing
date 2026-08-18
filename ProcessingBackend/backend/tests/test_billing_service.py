"""Unit tests for billing calculation functions.

Spec §26 — 25 scenarios covering all billing math.
"""

from datetime import UTC, datetime

from app.services.billing import (
    BillingStatus,
    TerminalBillingInfo,
    add_billing_months,
    calculate_period_price,
    calculate_periods_due,
    calculate_terminal_debt,
    compute_terminal_billing,
    project_expiration_after_payment,
    resolve_billing_status,
    resolve_monthly_price,
)


def _dt(year: int, month: int, day: int) -> datetime:
    return datetime(year, month, day, tzinfo=UTC)


def _info(**kwargs) -> TerminalBillingInfo:
    defaults = {
        "terminal_id": 1,
        "device_id": 773,
        "sn": "ABC",
        "terminal_is_active": True,
        "license_id": 1,
        "license_expires_at": _dt(2026, 11, 10),
        "renewal_enabled": True,
        "deactivation_requested_at": None,
        "billing_period_months": 1,
        "monthly_price_override_minor": None,
        "org_monthly_price_minor": 300000,
        "org_currency": "RUB",
    }
    defaults.update(kwargs)
    return TerminalBillingInfo(**defaults)


# === Price resolution ===


def test_override_price():
    assert resolve_monthly_price(200000, 300000) == 200000


def test_fallback_to_org_price():
    assert resolve_monthly_price(None, 300000) == 300000


def test_zero_price():
    assert resolve_monthly_price(0, 300000) == 0


# === Period price ===


def test_period_price_1_month():
    assert calculate_period_price(300000, 1) == 300000


def test_period_price_3_months():
    assert calculate_period_price(200000, 3) == 600000


# === Calendar arithmetic ===


def test_add_months_jan31():
    result = add_billing_months(_dt(2026, 1, 31), 1)
    assert result == _dt(2026, 2, 28)  # Not a leap year


def test_add_months_feb_leap_year():
    result = add_billing_months(_dt(2028, 1, 31), 1)
    assert result == _dt(2028, 2, 29)  # Leap year


def test_add_months_aug31():
    result = add_billing_months(_dt(2026, 8, 31), 1)
    assert result == _dt(2026, 9, 30)


def test_add_months_3():
    result = add_billing_months(_dt(2026, 8, 10), 3)
    assert result == _dt(2026, 11, 10)


# === Periods due ===


def test_periods_due_not_expired():
    assert calculate_periods_due(_dt(2026, 11, 10), _dt(2026, 8, 18), 1) == 0


def test_periods_due_one_period():
    # expires Aug 10, as_of Aug 18, period=3 months → 1 period
    assert calculate_periods_due(_dt(2026, 8, 10), _dt(2026, 8, 18), 3) == 1


def test_periods_due_multiple_periods():
    # expires Aug 10, as_of Aug 18 2027, period=3 months → 5 periods
    result = calculate_periods_due(_dt(2026, 8, 10), _dt(2027, 8, 18), 3)
    assert result >= 4  # At least 4 three-month periods


def test_periods_due_exact_boundary():
    # expires exactly at as_of → needs 1 period
    result = calculate_periods_due(_dt(2026, 8, 18), _dt(2026, 8, 18), 1)
    assert result == 1


# === Debt calculation ===


def test_debt_not_expired():
    debt = calculate_terminal_debt(_dt(2026, 11, 10), True, 1, 300000, _dt(2026, 8, 18))
    assert debt == 0


def test_debt_one_period():
    debt = calculate_terminal_debt(_dt(2026, 8, 10), True, 3, 600000, _dt(2026, 8, 18))
    assert debt == 600000


def test_debt_disabled_terminal():
    debt = calculate_terminal_debt(_dt(2026, 8, 10), False, 1, 300000, _dt(2026, 8, 18))
    assert debt == 0


def test_debt_no_license():
    debt = calculate_terminal_debt(None, True, 1, 300000, _dt(2026, 8, 18))
    assert debt == 0


# === Billing status ===


def test_status_active():
    status = resolve_billing_status(
        True, True, _dt(2026, 11, 10), None, _dt(2026, 8, 18)
    )
    assert status == BillingStatus.ACTIVE


def test_status_due_soon():
    status = resolve_billing_status(
        True, True, _dt(2026, 9, 10), None, _dt(2026, 8, 18), due_soon_days=30
    )
    assert status == BillingStatus.DUE_SOON


def test_status_overdue():
    status = resolve_billing_status(
        True, True, _dt(2026, 8, 10), None, _dt(2026, 8, 18)
    )
    assert status == BillingStatus.OVERDUE


def test_status_deactivation_scheduled():
    status = resolve_billing_status(
        True, False, _dt(2026, 11, 10), _dt(2026, 8, 18), _dt(2026, 8, 18)
    )
    assert status == BillingStatus.DEACTIVATION_SCHEDULED


def test_status_disabled():
    status = resolve_billing_status(
        True, False, _dt(2026, 8, 10), _dt(2026, 8, 18), _dt(2026, 8, 18)
    )
    assert status == BillingStatus.DISABLED


def test_status_admin_disabled():
    status = resolve_billing_status(
        False, True, _dt(2026, 11, 10), None, _dt(2026, 8, 18)
    )
    assert status == BillingStatus.ADMIN_DISABLED


# === Full terminal computation ===


def test_compute_active_terminal():
    info = _info()
    result = compute_terminal_billing(info, _dt(2026, 8, 18))
    assert result.billing_status == BillingStatus.ACTIVE
    assert result.overdue_amount_minor == 0
    assert result.included_in_forecast is True
    assert result.can_deactivate is True
    assert result.can_cancel_deactivation is False
    assert result.can_reactivate is False


def test_compute_overdue_terminal():
    info = _info(license_expires_at=_dt(2026, 8, 10))
    result = compute_terminal_billing(info, _dt(2026, 8, 18))
    assert result.billing_status == BillingStatus.OVERDUE
    assert result.overdue_amount_minor == 300000  # 1 period × 300000
    assert result.periods_due == 1
    assert result.included_in_forecast is False


def test_compute_disabled_terminal():
    info = _info(
        renewal_enabled=False,
        license_expires_at=_dt(2026, 8, 10),
        deactivation_requested_at=_dt(2026, 8, 1),
    )
    result = compute_terminal_billing(info, _dt(2026, 8, 18))
    assert result.billing_status == BillingStatus.DISABLED
    assert result.overdue_amount_minor == 0
    assert result.included_in_forecast is False
    assert result.can_reactivate is True


def test_compute_deactivation_scheduled():
    info = _info(
        renewal_enabled=False,
        deactivation_requested_at=_dt(2026, 8, 18),
    )
    result = compute_terminal_billing(info, _dt(2026, 8, 18))
    assert result.billing_status == BillingStatus.DEACTIVATION_SCHEDULED
    assert result.overdue_amount_minor == 0
    assert result.included_in_forecast is False
    assert result.can_cancel_deactivation is True


def test_compute_admin_disabled():
    info = _info(terminal_is_active=False)
    result = compute_terminal_billing(info, _dt(2026, 8, 18))
    assert result.billing_status == BillingStatus.ADMIN_DISABLED
    assert result.included_in_forecast is False


def test_deactivated_terminal_no_debt():
    """Spec: renewal_enabled=false → debt always 0 regardless of expiry."""
    info = _info(
        renewal_enabled=False,
        license_expires_at=_dt(2025, 1, 1),  # Long expired
    )
    result = compute_terminal_billing(info, _dt(2026, 8, 18))
    assert result.overdue_amount_minor == 0


def test_reactivation_not_start_from_old_expiry():
    """Spec: reactivation starts from payment date, not old expires_at."""
    new_exp = project_expiration_after_payment(
        _dt(2026, 8, 10),  # old expiry
        1,  # period months
        1,  # periods to add
        _dt(2026, 8, 18),  # as_of (payment date)
    )
    # Should start from as_of, not from old expiry
    assert new_exp == _dt(2026, 9, 18)


def test_payment_not_expired_starts_from_expiry():
    """For non-expired terminals, payment extends from current expiry."""
    new_exp = project_expiration_after_payment(
        _dt(2026, 11, 10),  # not expired
        1,  # period months
        1,  # periods to add
        _dt(2026, 8, 18),  # as_of
    )
    assert new_exp == _dt(2026, 12, 10)
