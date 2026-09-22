from __future__ import annotations

from datetime import UTC, datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator

# Supported owner projects according to Archive Manifest Contract v1
SUPPORTED_OWNER_PROJECTS: set[str] = {
    "iot-rpc-rest-app",
    "l4media",
    "MenuBuilder",
}

# Tables strictly protected against purge (No-Financial-Purge invariant)
PROTECTED_FINANCIAL_TABLES: set[str] = {
    "fin_accounts",
    "fin_balance_projections",
    "fin_billing_cycles",
    "fin_billing_profiles",
    "fin_ledger_entries",
    "fin_ledger_transactions",
    "fin_manual_payments",
    "fin_notification_deliveries",
    "fin_payments",
    "fin_reconciliation_runs",
    "fin_tariff_versions",
    "fin_terminal_monthly_charges",
    "fin_usage_daily",
    "l4desk_remote_sessions",
    "fin_archive_batches",
}


def is_older_than_three_full_months(
    source_month_str: str, reference_date: datetime | None = None
) -> bool:
    """Validate 3-month hot retention rule:
    Target source month must be older than 3 full calendar months.
    E.g. reference_date in Sep 2026 (month 9) -> diff must be > 3 (May 2026 or earlier).
    """
    if reference_date is None:
        reference_date = datetime.now(UTC)
    parts = source_month_str.split("-")
    if len(parts) != 2:
        return False
    s_year, s_month = int(parts[0]), int(parts[1])
    diff_months = (reference_date.year - s_year) * 12 + (reference_date.month - s_month)
    return diff_months > 3


class ArchiveManifestFile(BaseModel):
    model_config = ConfigDict(extra="forbid")

    path: str = Field(..., min_length=1, max_length=256)
    size_bytes: int = Field(..., ge=0)
    sha256: str = Field(..., pattern=r"^[0-9a-f]{64}$")
    record_count: int = Field(..., ge=0)
    compression: Literal["gzip", "none"]


class ArchiveManifestTimeRange(BaseModel):
    model_config = ConfigDict(extra="forbid")

    min_occurred_at: datetime
    max_occurred_at: datetime

    @model_validator(mode="after")
    def validate_range(self) -> ArchiveManifestTimeRange:
        if self.max_occurred_at < self.min_occurred_at:
            raise ValueError("max_occurred_at must be >= min_occurred_at")
        return self


class ArchiveManifestCursorBounds(BaseModel):
    model_config = ConfigDict(extra="forbid")

    min_cursor: int | None = Field(default=None, ge=0)
    max_cursor: int | None = Field(default=None, ge=0)
    through_cursor: int | None = Field(default=None, ge=0)
    consumers_passed_cursor: int | None = Field(default=None, ge=0)


class ArchiveManifestVerification(BaseModel):
    model_config = ConfigDict(extra="forbid")

    verified_at_utc: datetime | None = None
    verifier: str = Field(..., min_length=1, max_length=128)
    reread_records_count: int = Field(..., ge=0)
    reread_checksum_sha256: str = Field(..., pattern=r"^[0-9a-f]{64}$")
    restore_sample_status: Literal["passed", "failed", "skipped"]
    cursor_guard_passed: bool


class ArchiveManifestPurge(BaseModel):
    model_config = ConfigDict(extra="forbid")

    purged_at_utc: datetime | None = None
    purged_records_count: int = Field(..., ge=0)
    purge_status: Literal["completed", "pending", "failed", "skipped"]


class ArchiveManifestError(BaseModel):
    model_config = ConfigDict(extra="forbid")

    code: str = Field(..., min_length=1, max_length=64)
    message: str = Field(..., min_length=1)
    occurred_at_utc: datetime


class ArchiveManifestRetention(BaseModel):
    model_config = ConfigDict(extra="forbid")

    retain_until_utc: datetime
    retention_years: int = Field(..., ge=3)
    backup_required: bool = Field(..., description="Must be true for archive contract")

    @field_validator("backup_required")
    @classmethod
    def validate_backup_required(cls, v: bool) -> bool:
        if not v:
            raise ValueError("backup_required must be true")
        return v


class ArchiveManifestStorageLayout(BaseModel):
    model_config = ConfigDict(extra="forbid")

    volume_root: str = Field(..., min_length=1)
    relative_path: str = Field(
        ...,
        pattern=r"^[0-9]{4}/(0[1-9]|1[0-2])/[a-zA-Z0-9_-]+/[a-z0-9_-]+$",
    )


class ArchiveManifestV1(BaseModel):
    model_config = ConfigDict(extra="ignore")

    archive_manifest_version: Literal["1.0.0"] = "1.0.0"
    archive_batch_id: str = Field(..., pattern=r"^[a-z0-9_-]{8,128}$")
    owner_project: Literal["iot-rpc-rest-app", "l4media", "MenuBuilder"]
    schema_version: str = Field(..., pattern=r"^[0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9.]+)?$")
    created_at_utc: datetime
    source_month: str = Field(..., pattern=r"^[0-9]{4}-(0[1-9]|1[0-2])$")
    time_range: ArchiveManifestTimeRange
    cursor_bounds: ArchiveManifestCursorBounds | None = None
    record_types: list[str] = Field(..., min_length=1)
    record_counts: dict[str, int]
    files: list[ArchiveManifestFile] = Field(..., min_length=1)
    compression: Literal["gzip"] = "gzip"
    state: Literal["prepared", "verified", "purged", "failed"]
    verification: ArchiveManifestVerification | None = None
    purge: ArchiveManifestPurge | None = None
    error: ArchiveManifestError | None = None
    retention: ArchiveManifestRetention
    storage_layout: ArchiveManifestStorageLayout

    @model_validator(mode="after")
    def validate_manifest_invariants(self) -> ArchiveManifestV1:
        # 1. Total records check
        total_records = self.record_counts.get("total_records")
        if total_records is None:
            raise ValueError("record_counts must contain 'total_records'")
        if total_records < 0:
            raise ValueError("total_records must be >= 0")

        # Sum of files record_count must match total_records
        sum_files = sum(f.record_count for f in self.files)
        if sum_files != total_records:
            raise ValueError(
                f"Sum of file record_count ({sum_files}) does not match total_records ({total_records})"
            )

        # 2. State specific requirements
        if self.state == "verified":
            if not self.verification:
                raise ValueError("verification object is required for state 'verified'")
            if self.verification.verified_at_utc is None:
                raise ValueError(
                    "verified_at_utc is required for verification in state 'verified'"
                )
            if self.verification.restore_sample_status != "passed":
                raise ValueError(
                    "restore_sample_status must be 'passed' for state 'verified'"
                )
            if not self.verification.cursor_guard_passed:
                raise ValueError(
                    "cursor_guard_passed must be true for state 'verified'"
                )

        if self.state == "purged":
            if not self.verification:
                raise ValueError("verification object is required for state 'purged'")
            if not self.purge:
                raise ValueError("purge object is required for state 'purged'")
            if self.purge.purged_at_utc is None:
                raise ValueError(
                    "purged_at_utc is required for purge in state 'purged'"
                )
            if self.purge.purge_status != "completed":
                raise ValueError("purge_status must be 'completed' for state 'purged'")

        if self.state == "failed" and not self.error:
            raise ValueError("error object is required for state 'failed'")

        # 3. Checksum & record count verification matching
        if self.verification and self.state in ("verified", "purged"):
            primary_file = self.files[0]
            if (
                self.verification.reread_checksum_sha256.lower()
                != primary_file.sha256.lower()
            ):
                raise ValueError(
                    f"CHECKSUM_MISMATCH: reread sha256 ({self.verification.reread_checksum_sha256}) "
                    f"does not match file sha256 ({primary_file.sha256})"
                )
            if self.verification.reread_records_count != total_records:
                raise ValueError(
                    f"COUNT_MISMATCH: reread records ({self.verification.reread_records_count}) "
                    f"does not match total_records ({total_records})"
                )

        # 4. Cursor lag check
        if self.cursor_bounds and self.cursor_bounds.through_cursor is not None:
            through_c = self.cursor_bounds.through_cursor
            passed_c = self.cursor_bounds.consumers_passed_cursor
            if self.state == "purged" and (passed_c is None or passed_c < through_c):
                raise ValueError(
                    f"CURSOR_LAG_DETECTED: consumers_passed_cursor ({passed_c}) < through_cursor ({through_c})"
                )

        return self


# Hub & API Schemas
class HubArchiveBatchItem(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    archive_batch_id: str
    owner_project: str
    schema_version: str
    source_month: str
    state: str  # prepared, verified, purged, failed
    status: str  # pending, verified, failed
    record_types: list[str]
    row_count: int
    checksum_sha256: str
    min_occurred_at: datetime | None = None
    max_occurred_at: datetime | None = None
    through_cursor: int | None = None
    consumers_passed_cursor: int | None = None
    location_reference: str  # Masked reference, e.g. vol://<relative_path>
    storage_reference: str | None = None  # None for unprivileged users
    verified_at: datetime | None = None
    purged_at: datetime | None = None
    retain_until: datetime
    created_at: datetime
    actor: str
    correlation_id: str
    has_checksum_mismatch: bool = False
    has_count_mismatch: bool = False
    issues: list[str] = Field(default_factory=list)
    linked_records_count: int = 0
    manifest: dict[str, Any] | None = None


class HubArchiveBatchesResponse(BaseModel):
    items: list[HubArchiveBatchItem]
    total: int
    page: int
    page_size: int


class ArchiveManifestImportRequest(BaseModel):
    manifest: dict[str, Any]
    actor: str | None = None
    correlation_id: str | None = None
    check_volume_availability: bool = False


class ArchiveManifestImportResponse(BaseModel):
    archive_batch_id: str
    owner_project: str
    status: str
    state: str
    linked_records_count: int
    linked_tables: dict[str, int]
    location_reference: str
    message: str


class ArchiveRetentionCheckRequest(BaseModel):
    archive_batch_id: str
    owner_project: str | None = None
    volume_root: str | None = None


class ArchiveRetentionCheckResponse(BaseModel):
    archive_batch_id: str
    owner_project: str
    source_month: str
    is_hot_retention_valid: bool
    is_retention_period_valid: bool
    is_volume_available: bool
    is_backup_evidence_valid: bool
    can_purge: bool
    issues: list[str]


class ArchivePurgeRequest(BaseModel):
    target_tables: list[str]
    archive_batch_id: str | None = None
    dry_run: bool = True
