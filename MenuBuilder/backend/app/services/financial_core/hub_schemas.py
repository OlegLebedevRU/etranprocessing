from __future__ import annotations

from datetime import date, datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class HubRegistrationItem(BaseModel):
    id: int
    tenant_id: int | None = None
    tenant_name: str | None = None
    email_normalized: str
    status: str
    terms_version: str
    timezone: str
    source: str | None = None
    created_at: datetime
    expires_at: datetime
    consumed_at: datetime | None = None
    correlation_id: str
    is_first_paid: bool = False

    model_config = ConfigDict(from_attributes=True)


class HubRegistrationsResponse(BaseModel):
    items: list[HubRegistrationItem]
    total: int
    page: int
    page_size: int


class HubTerminalItem(BaseModel):
    terminal_id: int
    tenant_id: int
    tenant_name: str | None = None
    sn: str
    ordinal: int
    external_terminal_id: str | None = None
    provisioning_state: str
    pin_state: str
    certificate_reference: str | None = None
    first_online_at: datetime | None = None
    last_online_at: datetime | None = None
    is_online: bool = False
    is_free: bool = False
    last_error: str | None = None
    correlation_id: str
    created_at: datetime

    model_config = ConfigDict(from_attributes=True)


class HubTerminalsResponse(BaseModel):
    items: list[HubTerminalItem]
    total: int
    page: int
    page_size: int


class HubSessionItem(BaseModel):
    id: int
    tenant_id: int
    terminal_id: int
    terminal_sn: str | None = None
    operation_id: str
    correlation_id: str
    session_type: str
    state: str
    requested_at: datetime
    active_at: datetime | None = None
    closed_at: datetime | None = None
    duration_seconds: int = 0
    reason: str | None = None
    source_events_hash: str | None = None

    model_config = ConfigDict(from_attributes=True)


class HubSessionsResponse(BaseModel):
    items: list[HubSessionItem]
    total: int
    page: int
    page_size: int


class HubUsageItem(BaseModel):
    id: int
    tenant_id: int
    terminal_id: int
    terminal_sn: str | None = None
    local_date: date
    source_seconds: int
    video_seconds: int
    console_seconds: int
    free_seconds: int
    billable_seconds: int
    calculated_kopecks: int
    posted_kopecks: int
    discarded_kopecks: int
    is_reconciled: bool = True
    ledger_transaction_id: int | None = None
    source_events_hash: str
    correlation_id: str
    posted_at: datetime | None = None

    model_config = ConfigDict(from_attributes=True)


class HubUsageResponse(BaseModel):
    items: list[HubUsageItem]
    total: int
    page: int
    page_size: int


class HubTenantFinanceItem(BaseModel):
    tenant_id: int
    tenant_name: str | None = None
    balance_kopecks: int
    balance_rubles: float
    entitlement: str
    anchor_day: int | None = None
    current_cycle_ends_at: datetime | None = None
    grace_deadline: datetime | None = None
    terminal_count: int = 0
    has_failed_notifications: bool = False


class HubFinanceOverviewResponse(BaseModel):
    total_tenants: int
    active_tenants: int
    grace_tenants: int
    blocked_tenants: int
    total_balance_rubles: float
    tenants: list[HubTenantFinanceItem]
    total: int
    page: int
    page_size: int


class HubPaymentItem(BaseModel):
    id: int
    tenant_id: int
    tenant_name: str | None = None
    source: str
    amount_rubles: float
    amount_kopecks: int
    status: str
    reference: str
    details: str | None = None
    payer: str | None = None
    purpose: str | None = None
    comment: str | None = None
    ledger_transaction_id: int | None = None
    created_at: datetime
    correlation_id: str
    storno_by_id: int | None = None


class HubPaymentsResponse(BaseModel):
    items: list[HubPaymentItem]
    total: int
    page: int
    page_size: int


class HubNotificationItem(BaseModel):
    id: int
    tenant_id: int
    tenant_name: str | None = None
    billing_cycle_id: int
    notification_type: str
    scheduled_at: datetime
    status: str
    attempts: int
    sent_at: datetime | None = None
    last_error: str | None = None
    correlation_id: str

    model_config = ConfigDict(from_attributes=True)


class HubNotificationsResponse(BaseModel):
    items: list[HubNotificationItem]
    total: int
    page: int
    page_size: int


class HubAuditEventItem(BaseModel):
    id: int
    tenant_id: int | None = None
    actor: str
    event_type: str
    subject_type: str
    subject_id: str | None = None
    outcome: str
    details: dict[str, Any] | None = None
    correlation_id: str
    occurred_at: datetime

    model_config = ConfigDict(from_attributes=True)


class HubAuditEventsResponse(BaseModel):
    items: list[HubAuditEventItem]
    total: int
    page: int
    page_size: int


class CorrelationFactNode(BaseModel):
    node_type: str
    label: str
    present: bool
    mismatch: bool
    mismatch_code: str | None = None
    details: str | None = None
    fact: dict[str, Any] | None = None


class CorrelationDrilldownResponse(BaseModel):
    correlation_id: str | None = None
    query_params: dict[str, Any] = Field(default_factory=dict)
    overall_status: str
    mismatch_codes: list[str] = Field(default_factory=list)
    nodes: dict[str, CorrelationFactNode] = Field(default_factory=dict)


class HubManualPaymentCreateRequest(BaseModel):
    tenant_id: int = Field(..., gt=0)
    amount_rubles: int = Field(..., gt=0)
    received_on: date
    document_number: str = Field(..., min_length=1, max_length=128)
    purpose: str = Field(..., min_length=1)
    payer: str = Field(..., min_length=1, max_length=500)
    comment: str | None = None
    evidence_reference: str | None = None
    confirmation_code: str = Field(..., min_length=1, max_length=16)
    correlation_id: str | None = None


class HubManualPaymentStornoRequest(BaseModel):
    reversal_reason: str = Field(..., min_length=1)
    comment: str | None = None
    confirmation_code: str = Field(..., min_length=1, max_length=16)
    correlation_id: str | None = None
