from __future__ import annotations

import logging
from collections.abc import Sequence

from sqlalchemy import Connection, inspect, text
from sqlalchemy.ext.asyncio import AsyncEngine

logger = logging.getLogger(__name__)

REQUIRED_ALEMBIC_REVISION = "027"
COMPATIBLE_ALEMBIC_REVISIONS: set[str] = {"027"}

L4DESK_TABLES: list[str] = [
    "fin_accounts",
    "fin_archive_batches",
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
    "iot_consumer_checkpoints",
    "iot_event_inbox",
    "iot_event_quarantine",
    "l4desk_audit_events",
    "l4desk_memberships",
    "l4desk_registrations",
    "l4desk_remote_sessions",
    "l4desk_tenant_profiles",
    "l4desk_terminals",
]


class SchemaCompatibilityError(RuntimeError):
    """Raised when database schema is incompatible with application expectations."""


def check_schema_compatibility_sync(
    conn: Connection,
    expected_revision: str = REQUIRED_ALEMBIC_REVISION,
    compatible_revisions: set[str] | None = None,
    required_tables: Sequence[str] = L4DESK_TABLES,
) -> None:
    """Verify that the database has the expected Alembic revision and required tables.

    STRICTLY READ-ONLY: Executes NO DDL and makes no schema alterations.
    """
    if compatible_revisions is None:
        compatible_revisions = COMPATIBLE_ALEMBIC_REVISIONS

    # 1. Check alembic_version table
    try:
        result = conn.execute(text("SELECT version_num FROM alembic_version"))
        rows = result.fetchall()
    except Exception as exc:
        raise SchemaCompatibilityError(
            f"Cannot read alembic_version table: {exc}. Ensure Alembic migrations have been applied."
        ) from exc

    if not rows:
        raise SchemaCompatibilityError(
            "alembic_version table is empty. Database has not been migrated."
        )

    current_revisions = {row[0] for row in rows}
    if not (current_revisions & compatible_revisions):
        raise SchemaCompatibilityError(
            f"Database schema revision mismatch: expected one of {sorted(compatible_revisions)} "
            f"(required '{expected_revision}'), got {sorted(current_revisions)}. "
            "Please apply Alembic migrations before starting MenuBuilder."
        )

    # 2. Check required tables presence
    inspector = inspect(conn)
    existing_tables = set(inspector.get_table_names())
    missing_tables = set(required_tables) - existing_tables
    if missing_tables:
        raise SchemaCompatibilityError(
            f"Missing required schema tables in database: {sorted(missing_tables)}. "
            "Database schema is not fully migrated."
        )

    logger.info(
        "Schema compatibility check PASSED: revision in %s, all %d required tables present.",
        sorted(current_revisions),
        len(required_tables),
    )


async def verify_schema_compatibility(
    engine: AsyncEngine,
    expected_revision: str = REQUIRED_ALEMBIC_REVISION,
    compatible_revisions: set[str] | None = None,
    required_tables: Sequence[str] = L4DESK_TABLES,
) -> None:
    """Async entrypoint to verify database schema compatibility at startup."""
    async with engine.connect() as conn:
        await conn.run_sync(
            check_schema_compatibility_sync,
            expected_revision=expected_revision,
            compatible_revisions=compatible_revisions,
            required_tables=required_tables,
        )
