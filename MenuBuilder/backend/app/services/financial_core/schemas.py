from __future__ import annotations

from datetime import date, datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, computed_field

FinTransactionKind = Literal[
    "payment", "usage", "terminal_month", "adjustment", "reversal"
]
FinTransactionStatus = Literal["draft", "posted"]
FinAccountKind = Literal["tenant_settlement", "payment_clearing", "usage_revenue"]
FinReconciliationStatus = Literal["pending", "matched", "mismatch", "failed"]


class FinPostingEntryRequest(BaseModel):
    account_id: int = Field(..., description="Target fin_accounts.id")
    debit_kopecks: int = Field(default=0, ge=0, description="Debit amount in kopecks")
    credit_kopecks: int = Field(default=0, ge=0, description="Credit amount in kopecks")

    model_config = ConfigDict(extra="forbid")


class FinPostingRequest(BaseModel):
    tenant_id: int = Field(..., gt=0, description="Tenant organization ID")
    operation_id: str = Field(..., min_length=1, max_length=128)
    kind: FinTransactionKind
    source_project: str = Field(..., min_length=1, max_length=64)
    source_type: str = Field(..., min_length=1, max_length=64)
    source_id: str = Field(..., min_length=1, max_length=128)
    actor: str = Field(..., min_length=1, max_length=128)
    correlation_id: str = Field(..., min_length=1, max_length=128)
    entries: list[FinPostingEntryRequest] = Field(..., min_length=2)
    corrects_transaction_id: int | None = Field(default=None)
    source_event_id: str | None = Field(default=None, max_length=128)
    source_events_hash: str | None = Field(default=None, max_length=64)
    archive_batch_id: str | None = Field(default=None, max_length=128)
    calculation_snapshot: dict[str, Any] | None = Field(default=None)
    expected_projection_version: int | None = Field(default=None, ge=0)

    model_config = ConfigDict(extra="forbid")


class FinReversalRequest(BaseModel):
    tenant_id: int = Field(..., gt=0)
    transaction_id: int = Field(..., gt=0, description="ID of transaction to reverse")
    operation_id: str = Field(..., min_length=1, max_length=128)
    actor: str = Field(..., min_length=1, max_length=128)
    correlation_id: str = Field(..., min_length=1, max_length=128)
    reason: str = Field(..., min_length=1, max_length=500)
    source_project: str = Field(default="MenuBuilder", max_length=64)
    source_type: str = Field(default="reversal", max_length=64)
    source_id: str | None = Field(default=None, max_length=128)
    expected_projection_version: int | None = Field(default=None, ge=0)

    model_config = ConfigDict(extra="forbid")


class FinReconciliationRequest(BaseModel):
    period_start: datetime
    period_end: datetime
    tenant_id: int | None = Field(default=None, gt=0)
    actor: str = Field(default="reconciliation_worker", max_length=128)
    correlation_id: str | None = Field(default=None, max_length=128)
    operation_id: str | None = Field(default=None, max_length=128)
    auto_rebuild_projection: bool = Field(default=False)

    model_config = ConfigDict(extra="forbid")


class FinBalanceRead(BaseModel):
    tenant_id: int
    account_id: int
    balance_kopecks: int
    balance_rubles: float
    version: int
    last_transaction_id: int | None
    updated_at: datetime

    model_config = ConfigDict(from_attributes=True)


class FinLedgerEntryRead(BaseModel):
    id: int
    transaction_id: int
    line_number: int
    account_id: int
    debit_kopecks: int
    credit_kopecks: int

    model_config = ConfigDict(from_attributes=True)


class FinLedgerTransactionRead(BaseModel):
    id: int
    tenant_id: int
    operation_id: str
    kind: str
    status: str
    corrects_transaction_id: int | None
    debit_kopecks: int
    credit_kopecks: int
    source_project: str
    source_type: str
    source_id: str
    source_event_id: str | None
    source_events_hash: str
    archive_batch_id: str | None
    calculation_snapshot: dict[str, Any] | None
    actor: str
    correlation_id: str
    created_at: datetime
    posted_at: datetime | None
    entries: list[FinLedgerEntryRead] = []

    model_config = ConfigDict(from_attributes=True)


class FinReconciliationRunRead(BaseModel):
    id: int
    operation_id: str
    period_start: datetime
    period_end: datetime
    tenant_id: int | None
    status: str
    calculated_kopecks: int | None
    posted_kopecks: int | None
    discarded_kopecks: int | None
    debit_kopecks: int | None
    credit_kopecks: int | None
    balance_difference_kopecks: int | None
    mismatch_count: int | None
    details: dict[str, Any] | None
    actor: str
    correlation_id: str
    started_at: datetime
    finished_at: datetime | None

    model_config = ConfigDict(from_attributes=True)


class FinTariffVersionRead(BaseModel):
    id: int
    version: str
    effective_from: datetime
    terminal_month_kopecks: int
    hourly_rate_kopecks: int
    free_daily_seconds: int
    currency: str = "RUB"
    actor: str
    correlation_id: str
    created_at: datetime

    model_config = ConfigDict(from_attributes=True)


class FinTariffVersionCreate(BaseModel):
    version: str = Field(..., min_length=1, max_length=32)
    effective_from: datetime
    terminal_month_kopecks: int = Field(default=10000, ge=0)
    hourly_rate_kopecks: int = Field(default=100, ge=0)
    free_daily_seconds: int = Field(default=7200, ge=0)
    currency: str = Field(default="RUB", min_length=3, max_length=3)
    actor: str = Field(default="admin", max_length=128)
    correlation_id: str = Field(default="tariff-create", max_length=128)

    model_config = ConfigDict(extra="forbid")


class FinBillingProfileRead(BaseModel):
    tenant_id: int
    anchor_at: datetime | None
    anchor_day: int | None
    anchor_timezone: str | None
    first_payment_transaction_id: int | None
    entitlement: str
    entitlement_changed_at: datetime

    model_config = ConfigDict(from_attributes=True)


class FinBillingCycleRead(BaseModel):
    id: int
    tenant_id: int
    sequence: int
    starts_at: datetime
    ends_at: datetime
    grace_deadline: datetime
    timezone: str
    closed_at: datetime | None

    model_config = ConfigDict(from_attributes=True)


class FinUsageDailyRead(BaseModel):
    id: int
    tenant_id: int
    terminal_id: int
    local_date: date
    timezone: str
    tariff_version_id: int
    source_seconds: int
    video_seconds: int
    console_seconds: int
    free_seconds: int
    billable_seconds: int
    rounded_billable_hours: int
    rate_kopecks: int
    calculated_kopecks: int
    posted_kopecks: int
    discarded_kopecks: int
    source_project: str
    source_event_id: str | None
    source_events_hash: str
    archive_batch_id: str | None
    ledger_transaction_id: int | None
    actor: str
    correlation_id: str
    created_at: datetime
    posted_at: datetime | None

    model_config = ConfigDict(from_attributes=True)


class FinTerminalMonthlyChargeRead(BaseModel):
    id: int
    tenant_id: int
    terminal_id: int
    billing_cycle_id: int
    tariff_version_id: int
    is_free: bool
    first_online_at: datetime
    calculated_kopecks: int
    posted_kopecks: int
    discarded_kopecks: int
    source_project: str
    source_event_id: str
    source_events_hash: str
    archive_batch_id: str | None
    ledger_transaction_id: int | None
    actor: str
    correlation_id: str
    created_at: datetime
    posted_at: datetime | None

    model_config = ConfigDict(from_attributes=True)


class FinProcessOnlineEventRequest(BaseModel):
    tenant_id: int = Field(..., gt=0)
    terminal_id: int = Field(..., gt=0)
    event_id: str = Field(..., min_length=1, max_length=128)
    occurred_at: datetime
    actor: str = Field(default="metering_worker", max_length=128)
    correlation_id: str = Field(default="", max_length=128)

    model_config = ConfigDict(extra="forbid")


class FinRecordUsageRequest(BaseModel):
    tenant_id: int = Field(..., gt=0)
    terminal_id: int = Field(..., gt=0)
    session_type: Literal["console", "video"]
    start_utc: datetime
    end_utc: datetime
    event_id: str = Field(..., min_length=1, max_length=128)
    actor: str = Field(default="metering_worker", max_length=128)
    correlation_id: str = Field(default="", max_length=128)

    model_config = ConfigDict(extra="forbid")


class FinDailyCloseRequest(BaseModel):
    tenant_id: int = Field(..., gt=0)
    local_date: date
    actor: str = Field(default="metering_worker", max_length=128)
    correlation_id: str = Field(default="", max_length=128)

    model_config = ConfigDict(extra="forbid")


# =============================================================================
# YooKassa & Manual Payments Schemas (L4D-11-MB)
# =============================================================================


class FinPaymentCreateRequest(BaseModel):
    amount_rubles: int = Field(..., ge=1, description="Integer amount in rubles (>= 1)")
    return_url: str | None = Field(
        default=None,
        max_length=512,
        description="Optional safe return URL after payment",
    )
    idempotence_key: str | None = Field(
        default=None, max_length=128, description="Optional unique idempotence key"
    )

    model_config = ConfigDict(extra="forbid")


class FinPaymentRead(BaseModel):
    id: int
    tenant_id: int
    operation_id: str
    provider: str
    provider_payment_id: str | None
    status: str
    amount_kopecks: int
    currency: str
    confirmation_url: str | None
    provider_receipt_id: str | None
    receipt_status: str | None
    receipt_snapshot: dict[str, Any] | None
    verified_at: datetime | None
    succeeded_at: datetime | None
    ledger_transaction_id: int | None
    actor: str
    correlation_id: str
    created_at: datetime

    @computed_field
    @property
    def amount_rubles(self) -> int:
        return self.amount_kopecks // 100

    model_config = ConfigDict(from_attributes=True)


class FinYooKassaWebhookObject(BaseModel):
    id: str
    status: str
    amount: dict[str, Any]
    description: str | None = None
    recipient: dict[str, Any] | None = None
    metadata: dict[str, Any] | None = None
    cancellation_details: dict[str, Any] | None = None

    model_config = ConfigDict(extra="ignore")


class FinYooKassaWebhookPayload(BaseModel):
    type: str = "notification"
    event: str
    object: FinYooKassaWebhookObject

    model_config = ConfigDict(extra="ignore")


class FinManualPaymentCreate(BaseModel):
    tenant_id: int = Field(..., gt=0, description="Target tenant ID")
    amount_rubles: int = Field(..., ge=1, description="Integer rubles (>= 1)")
    received_on: date = Field(..., description="Date payment was received by bank")
    document_number: str = Field(
        ..., min_length=1, max_length=128, description="Bank order/doc reference"
    )
    payer: str = Field(
        ..., min_length=1, max_length=500, description="Payer legal entity name"
    )
    purpose: str = Field(
        ..., min_length=1, max_length=1000, description="Payment purpose"
    )
    comment: str | None = Field(
        default=None, max_length=1000, description="Optional comment"
    )
    evidence_reference: str | None = Field(
        default=None, max_length=500, description="Evidence URL or reference"
    )
    operation_id: str | None = Field(
        default=None, max_length=128, description="Optional operation ID"
    )

    model_config = ConfigDict(extra="forbid")


class FinManualPaymentStornoRequest(BaseModel):
    reversal_reason: str = Field(
        ..., min_length=1, max_length=500, description="Reason for reversing payment"
    )
    comment: str | None = Field(default=None, max_length=1000)

    model_config = ConfigDict(extra="forbid")


class FinManualPaymentRead(BaseModel):
    id: int
    tenant_id: int
    operation_id: str
    amount_kopecks: int
    received_on: date
    document_number: str
    payer: str
    purpose: str
    comment: str | None
    evidence_reference: str | None
    created_by_user_id: int
    ledger_transaction_id: int
    correlation_id: str
    created_at: datetime

    @computed_field
    @property
    def amount_rubles(self) -> int:
        return self.amount_kopecks // 100

    model_config = ConfigDict(from_attributes=True)


class FinEntitlementStatusRead(BaseModel):
    tenant_id: int
    entitlement: str  # "free" | "active" | "grace" | "blocked"
    balance_kopecks: int
    is_first_paid: bool
    cycle_id: int | None = None
    cycle_starts_at: datetime | None = None
    cycle_ends_at: datetime | None = None
    grace_deadline: datetime | None = None
    can_start_sessions: bool
    free_terminal_id: int | None = None
    today_usage_seconds: int = 0
    free_quota_seconds: int = 7200
    reason_code: str | None = None
    reason_message: str | None = None

    @computed_field
    @property
    def balance_rubles(self) -> float:
        return self.balance_kopecks / 100.0

    model_config = ConfigDict(from_attributes=True)


class FinNotificationDeliveryRead(BaseModel):
    id: int
    tenant_id: int
    billing_cycle_id: int
    notification_type: str
    scheduled_at: datetime
    status: str
    attempts: int
    sent_at: datetime | None = None
    provider_message_id: str | None = None
    last_error: str | None = None
    correlation_id: str

    model_config = ConfigDict(from_attributes=True)
