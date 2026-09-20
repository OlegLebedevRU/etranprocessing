from __future__ import annotations

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field

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
