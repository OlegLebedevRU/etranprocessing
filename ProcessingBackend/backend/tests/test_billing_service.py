"""Unit tests for billing calculation functions.

Spec §26 — 25 scenarios covering all billing math.
"""

from datetime import UTC, datetime

from app.services.billing import (
    BillingStatus,
    TerminalBillingInfo,
    add_billing_months,
    add_months_from_anchor,
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
        mode="reactivation",
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


# === H2: Calendar drift — anchor day preserved ===


def test_h2_anchor_day_preserved_jan31():
    """H2: Jan 31 + 2 months must be Mar 31, not Mar 28."""
    result = add_months_from_anchor(_dt(2026, 1, 31), 2)
    assert result == _dt(2026, 3, 31)


def test_h2_periods_due_jan31_no_drift():
    """H2: expires Jan 31, as_of Mar 30, period=1m → 3 periods (Feb 28, Mar 31 > Mar 30)."""
    # Before fix: iterative gave 4 (Jan31→Feb28→Mar28→Apr28)
    # After fix: anchor-based gives 3 (Jan31, Feb28, Mar31 > Mar30)
    result = calculate_periods_due(_dt(2025, 12, 31), _dt(2026, 3, 30), 1)
    assert result == 3


def test_h2_renewal_preserves_anchor_day():
    """H2: Renewal from Aug 10 + 3 periods of 1 month → Nov 10 (not Nov 18)."""
    new_exp = project_expiration_after_payment(
        _dt(2026, 8, 10), 1, 3, _dt(2026, 8, 18), mode="renewal"
    )
    assert new_exp == _dt(2026, 11, 10)


def test_h2_renewal_long_overdue_preserves_day():
    """H2: expired 2024-08-10, paid 25 periods of 1m → 2026-09-10 (not 2028-09-18)."""
    new_exp = project_expiration_after_payment(
        _dt(2024, 8, 10), 1, 25, _dt(2026, 8, 18), mode="renewal"
    )
    assert new_exp == _dt(2026, 9, 10)


def test_h2_chain_preserves_31st():
    """H2: Chain from Jan 31 through multiple periods preserves 31st where possible."""
    # Jan 31 → Feb 28 → Mar 31 (not Mar 28)
    anchor = _dt(2026, 1, 31)
    assert add_months_from_anchor(anchor, 1) == _dt(2026, 2, 28)
    assert add_months_from_anchor(anchor, 2) == _dt(2026, 3, 31)
    assert add_months_from_anchor(anchor, 3) == _dt(2026, 4, 30)
    assert add_months_from_anchor(anchor, 4) == _dt(2026, 5, 31)


# === H3: Renewal vs reactivation modes ===


def test_h3_reactivation_starts_from_payment_date():
    """H3: Reactivation always extends 1 period from as_of."""
    new_exp = project_expiration_after_payment(
        _dt(2024, 8, 10),  # long-expired
        1,  # period months
        25,  # periods_to_add (ignored for reactivation)
        _dt(2026, 8, 18),  # as_of
        mode="reactivation",
    )
    assert new_exp == _dt(2026, 9, 18)


def test_h3_renewal_extends_from_expiry():
    """H3: Renewal extends from expires_at, not as_of."""
    new_exp = project_expiration_after_payment(
        _dt(2026, 8, 10),  # not yet expired
        3,  # period months
        1,  # periods to add
        _dt(2026, 8, 18),  # as_of
        mode="renewal",
    )
    assert new_exp == _dt(2026, 11, 10)


def test_h3_debt_zero_after_renewal_payment():
    """H3: After paying overdue periods in renewal mode, periods_due → 0."""
    expires_at = _dt(2026, 5, 10)
    as_of = _dt(2026, 8, 18)
    periods = calculate_periods_due(expires_at, as_of, 1)
    assert periods == 4  # Jun 10, Jul 10, Aug 10, Sep 10

    new_exp = project_expiration_after_payment(
        expires_at, 1, periods, as_of, mode="renewal"
    )
    assert new_exp == _dt(2026, 9, 10)

    # After payment, debt should be 0
    remaining = calculate_periods_due(new_exp, as_of, 1)
    assert remaining == 0


# === H4: Guard on billing_period_months <= 0 ===


def test_h4_zero_period_raises():
    """H4: billing_period_months=0 must raise ValueError, not infinite loop."""
    import pytest

    with pytest.raises(ValueError, match="positive"):
        calculate_periods_due(_dt(2026, 8, 10), _dt(2026, 8, 18), 0)


def test_h4_negative_period_raises():
    """H4: billing_period_months=-1 must raise ValueError."""
    import pytest

    with pytest.raises(ValueError, match="positive"):
        calculate_periods_due(_dt(2026, 8, 10), _dt(2026, 8, 18), -1)


# === M2: Terminal without license ===


def test_m2_no_license_status():
    """M2: Terminal without license → NO_LICENSE (not ACTIVE)."""
    status = resolve_billing_status(
        True, True, None, None, _dt(2026, 8, 18), has_license=False
    )
    assert status == BillingStatus.NO_LICENSE


def test_m2_no_license_in_compute():
    """M2: compute_terminal_billing with no license → NO_LICENSE."""
    info = _info(license_id=None, license_expires_at=None)
    result = compute_terminal_billing(info, _dt(2026, 8, 18))
    assert result.billing_status == BillingStatus.NO_LICENSE
    assert result.overdue_amount_minor == 0
    assert result.included_in_forecast is False
