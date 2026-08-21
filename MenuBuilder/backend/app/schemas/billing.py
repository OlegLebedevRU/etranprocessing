"""Pydantic schemas for the user billing API."""

from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel, ConfigDict

from app.services.billing import BillingStatus


class OrderStatus(StrEnum):
    PENDING = "pending"
    PAID = "paid"
    CANCELLED = "cancelled"
    FAILED = "failed"


class BillingForecastMonthRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    month: str
    amount_minor: int
    terminal_count: int


class BillingSummaryRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    as_of: datetime
    currency: str
    monthly_base_price_minor: int
    overdue_amount_minor: int
    overdue_terminal_count: int
    active_terminal_count: int
    deactivation_scheduled_count: int
    disabled_terminal_count: int
    admin_disabled_terminal_count: int
    nearest_required_payment_at: datetime | None
    forecast: list[BillingForecastMonthRead]


class BillingTerminalRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

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
    cert_pin_pending: bool = False
    cert_pin_expires_at: datetime | None = None
    address: str | None = None
    note: str | None = None
    terminal_type_id: int = 0
    terminal_type_name: str | None = None
    created_at: datetime | None = None


class DeactivateTerminalResponse(BaseModel):
    terminal_id: int
    status: BillingStatus
    works_until: datetime | None
    overdue_amount_minor: int
    included_in_forecast: bool


class CancelDeactivationResponse(BaseModel):
    terminal_id: int
    status: BillingStatus
    renewal_enabled: bool


class CheckoutItemRequest(BaseModel):
    terminal_id: int
    advance_periods: int = 0  # 0, 1, or 2
    include_license: bool = True
    include_cert_pin: bool = False


class CheckoutRequest(BaseModel):
    items: list[CheckoutItemRequest]


class CheckoutItemResponse(BaseModel):
    terminal_id: int
    operation: str = "renewal"
    periods_due: int
    advance_periods: int
    amount_minor: int
    new_expires_at: datetime | None = None


class CheckoutResponse(BaseModel):
    order_id: str
    currency: str
    amount_minor: int
    payment_url: str | None
    items: list[CheckoutItemResponse]


class ReactivationCheckoutRequest(BaseModel):
    advance_periods: int = 1  # 1 or 2


class ReactivationCheckoutResponse(BaseModel):
    order_id: str
    currency: str
    amount_minor: int
    months: int
    new_expires_at: datetime
    payment_url: str | None


class BillingOrderRead(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: str
    org_id: int
    status: OrderStatus
    currency: str
    amount_minor: int
    provider: str | None
    provider_order_id: str | None
    payment_url: str | None
    created_at: datetime
    paid_at: datetime | None
    expires_at: datetime | None


class ConfirmPaymentResponse(BaseModel):
    order_id: str
    status: OrderStatus
    paid_at: datetime | None
    items_updated: int
