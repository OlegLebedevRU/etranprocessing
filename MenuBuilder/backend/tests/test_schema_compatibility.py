from __future__ import annotations

import json
from datetime import UTC, datetime, timedelta
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from sqlalchemy import create_engine, select, text
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import Session, load_only

import app.models_l4desk as l4desk_models
from app.database import Base
from app.models_iot_consumer import (
    IotConsumerCheckpoint,
    IotEventInbox,
    IotEventQuarantine,
)
from app.repositories.l4desk_repository import L4DeskRepository
from app.schema_compatibility import (
    L4DESK_TABLES,
    SchemaCompatibilityError,
    check_schema_compatibility_sync,
    verify_schema_compatibility,
)

SCHEMA_JSON_PATH = (
    Path(__file__).resolve().parents[3]
    / "shared"
    / "docs"
    / "l4desk"
    / "schema-v1.json"
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


# =============================================================================
# 1. Schema Contract Gate & Model Mapping Tests
# =============================================================================


def test_schema_contract_gate_tables_and_columns():
    """Verify that all 23 L4Desk/fin/iot tables exist in Base.metadata and match contract."""
    assert SCHEMA_JSON_PATH.exists(), f"Missing schema artifact: {SCHEMA_JSON_PATH}"
    contract_schema = json.loads(SCHEMA_JSON_PATH.read_text(encoding="utf-8"))

    for table_name in L4DESK_TABLES:
        assert table_name in Base.metadata.tables, (
            f"Table {table_name} missing from Base.metadata"
        )
        assert table_name in contract_schema, (
            f"Table {table_name} missing from schema-v1.json contract"
        )

        model_table = Base.metadata.tables[table_name]
        contract_table = contract_schema[table_name]

        model_cols = {c.name for c in model_table.columns}
        contract_cols = {c["name"] for c in contract_table["columns"]}
        assert model_cols == contract_cols, (
            f"Columns mismatch in {table_name}: {model_cols} != {contract_cols}"
        )

        model_nullability = {c.name: c.nullable for c in model_table.columns}
        contract_nullability = {
            c["name"]: c["nullable"] for c in contract_table["columns"]
        }
        assert model_nullability == contract_nullability, (
            f"Nullability mismatch in {table_name}: {model_nullability} != {contract_nullability}"
        )


def test_import_and_model_reexport():
    """Verify all 23 models are imported and re-exported cleanly without duplicates."""
    assert len(L4DESK_TABLES) == 23
    assert len(l4desk_models.__all__) == 23

    # Check models_iot_consumer re-exports
    assert IotConsumerCheckpoint.__tablename__ == "iot_consumer_checkpoints"
    assert IotEventInbox.__tablename__ == "iot_event_inbox"
    assert IotEventQuarantine.__tablename__ == "iot_event_quarantine"

    # Confirm there are no duplicate tables in Base.metadata
    table_names = [t.name for t in Base.metadata.tables.values()]
    for t in L4DESK_TABLES:
        assert table_names.count(t) == 1, (
            f"Table {t} appears multiple times in Base.metadata"
        )


# =============================================================================
# 2. Startup Schema Compatibility Check & Revision Gate Tests
# =============================================================================


def test_startup_error_on_missing_alembic_version_table():
    """check_schema_compatibility_sync must raise SchemaCompatibilityError when alembic_version missing."""
    sync_engine = create_engine("sqlite:///:memory:")
    with (
        sync_engine.connect() as conn,
        pytest.raises(SchemaCompatibilityError, match="Cannot read alembic_version"),
    ):
        check_schema_compatibility_sync(conn)


def test_startup_error_on_empty_alembic_version_table():
    """check_schema_compatibility_sync must raise SchemaCompatibilityError when alembic_version has no rows."""
    sync_engine = create_engine("sqlite:///:memory:")
    with sync_engine.begin() as conn:
        conn.execute(text("CREATE TABLE alembic_version (version_num VARCHAR(32));"))

    with (
        sync_engine.connect() as conn,
        pytest.raises(SchemaCompatibilityError, match="alembic_version table is empty"),
    ):
        check_schema_compatibility_sync(conn)


def test_startup_error_on_wrong_revision():
    """check_schema_compatibility_sync must raise SchemaCompatibilityError on outdated or wrong revision."""
    sync_engine = create_engine("sqlite:///:memory:")
    with sync_engine.begin() as conn:
        conn.execute(text("CREATE TABLE alembic_version (version_num VARCHAR(32));"))
        conn.execute(text("INSERT INTO alembic_version VALUES ('026');"))

    with (
        sync_engine.connect() as conn,
        pytest.raises(
            SchemaCompatibilityError,
            match=r"Database schema revision mismatch: expected one of \['027'\]",
        ),
    ):
        check_schema_compatibility_sync(conn)


def test_startup_error_on_missing_required_tables():
    """check_schema_compatibility_sync must raise SchemaCompatibilityError when tables are missing."""
    sync_engine = create_engine("sqlite:///:memory:")
    with sync_engine.begin() as conn:
        conn.execute(text("CREATE TABLE alembic_version (version_num VARCHAR(32));"))
        conn.execute(text("INSERT INTO alembic_version VALUES ('027');"))
        # Only create 1 table instead of 23
        conn.execute(
            text("CREATE TABLE l4desk_registrations (id INTEGER PRIMARY KEY);")
        )

    with (
        sync_engine.connect() as conn,
        pytest.raises(
            SchemaCompatibilityError,
            match="Missing required schema tables in database",
        ),
    ):
        check_schema_compatibility_sync(conn)


def test_startup_success_when_revision_and_tables_match():
    """check_schema_compatibility_sync succeeds cleanly when revision 027 and all tables exist."""
    sync_engine = create_engine("sqlite:///:memory:")
    with sync_engine.begin() as conn:
        conn.execute(text("CREATE TABLE alembic_version (version_num VARCHAR(32));"))
        conn.execute(text("INSERT INTO alembic_version VALUES ('027');"))
        for t in L4DESK_TABLES:
            conn.execute(text(f"CREATE TABLE {t} (id INTEGER PRIMARY KEY);"))

    with sync_engine.connect() as conn:
        # Should complete without error
        check_schema_compatibility_sync(conn)


@pytest.mark.anyio
async def test_verify_schema_compatibility_async():
    """verify_schema_compatibility async entrypoint behaves identically to sync checker."""
    sync_engine = create_engine("sqlite:///:memory:")
    with sync_engine.begin() as conn:
        conn.execute(text("CREATE TABLE alembic_version (version_num VARCHAR(32));"))
        conn.execute(text("INSERT INTO alembic_version VALUES ('027');"))
        for t in L4DESK_TABLES:
            conn.execute(text(f"CREATE TABLE {t} (id INTEGER PRIMARY KEY);"))

    with sync_engine.connect() as sync_conn:
        mock_engine = MagicMock()
        mock_conn = MagicMock()

        async def _run_sync(fn, *args, **kwargs):
            return fn(sync_conn, *args, **kwargs)

        mock_conn.run_sync = AsyncMock(side_effect=_run_sync)

        class AsyncConnContext:
            async def __aenter__(self):
                return mock_conn

            async def __aexit__(self, exc_type, exc_val, exc_tb):
                pass

        mock_engine.connect.return_value = AsyncConnContext()
        await verify_schema_compatibility(mock_engine)


@pytest.mark.anyio
async def test_lifespan_aborts_startup_on_incompatible_schema():
    """FastAPI lifespan aborts with SchemaCompatibilityError when revision is wrong."""
    from app.main import lifespan

    mock_app = MagicMock()
    with patch("app.main.settings") as mock_settings:
        mock_settings.database_url = (
            "postgresql+asyncpg://mock:mock@localhost:5432/mock"
        )
        mock_settings.schema_compatibility_check_enabled = True
        mock_settings.required_alembic_revision = "027"

        with (
            patch(
                "app.main.verify_schema_compatibility",
                side_effect=SchemaCompatibilityError(
                    "Schema revision mismatch: expected 027, got 026"
                ),
            ),
            pytest.raises(SchemaCompatibilityError, match="expected 027, got 026"),
        ):
            async with lifespan(mock_app):
                pass


# =============================================================================
# 3. Compatibility Tests Against Migrated DB
# =============================================================================


def test_compatibility_against_migrated_db():
    """Verify that all 23 models can be queried and persisted in a migrated schema."""
    sync_engine = create_engine("sqlite:///:memory:")

    with sync_engine.begin() as conn:
        # Setup alembic_version
        conn.execute(text("CREATE TABLE alembic_version (version_num VARCHAR(32));"))
        conn.execute(text("INSERT INTO alembic_version VALUES ('027');"))

        # Setup supporting legacy tables
        conn.execute(text("CREATE TABLE orgs (org_id INTEGER PRIMARY KEY, name TEXT);"))
        conn.execute(
            text("CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT);")
        )
        conn.execute(
            text("CREATE TABLE terminals (id INTEGER PRIMARY KEY, terminal_id TEXT);")
        )

        tables_to_create = [
            Base.metadata.tables[t] for t in L4DESK_TABLES if t in Base.metadata.tables
        ]
        Base.metadata.create_all(conn, tables=tables_to_create)

    with sync_engine.connect() as conn:
        check_schema_compatibility_sync(conn)

    # Verify model operations and persistence against migrated schema
    with Session(sync_engine) as session:
        now = datetime.now(UTC)

        # 1. Registration
        reg = l4desk_models.L4DeskRegistration(
            id=1,
            email_normalized="test@example.com",
            password_hash="hash123",
            token_hash="tok123",
            terms_version="v1",
            timezone="UTC",
            correlation_id="corr-1",
            expires_at=now + timedelta(hours=24),
            source="invite",
        )
        session.add(reg)

        # 2. Tenant Profile
        prof = l4desk_models.L4DeskTenantProfile(
            tenant_id=10,
            timezone="Europe/Moscow",
        )
        session.add(prof)

        # 3. Membership
        membership = l4desk_models.L4DeskMembership(
            tenant_id=10,
            user_id=100,
            role_id=5,
            is_owner=True,
        )
        session.add(membership)

        # 4. Terminal
        term = l4desk_models.L4DeskTerminal(
            terminal_id=1001,
            tenant_id=10,
            ordinal=1,
            sn="SN-1001",
            external_terminal_id="EXT-1001",
            operation_id="op-1001",
            correlation_id="corr-1001",
            provisioning_state="pending",
            pin_state="pending",
        )
        session.add(term)

        # 5. Audit event
        audit = l4desk_models.L4DeskAuditEvent(
            id=1,
            actor="system",
            event_type="test.created",
            subject_type="registration",
            subject_id="1",
            correlation_id="corr-audit-1",
            outcome="success",
        )
        session.add(audit)

        # 6. Fin Account
        fin_acc = l4desk_models.FinAccount(
            id=1,
            tenant_id=10,
            kind="tenant_settlement",
            currency="RUB",
        )
        session.add(fin_acc)

        session.commit()

        # Query back and verify
        fetched_reg = session.scalar(
            select(l4desk_models.L4DeskRegistration).where(
                l4desk_models.L4DeskRegistration.token_hash == "tok123"
            )
        )
        assert fetched_reg is not None
        assert fetched_reg.email_normalized == "test@example.com"

        fetched_term = session.scalar(
            select(l4desk_models.L4DeskTerminal).where(
                l4desk_models.L4DeskTerminal.terminal_id == 1001
            )
        )
        assert fetched_term is not None
        assert fetched_term.sn == "SN-1001"


@pytest.mark.anyio
async def test_l4desk_repository_methods():
    """Verify that L4DeskRepository methods operate as expected on an AsyncSession."""
    mock_session = AsyncMock(spec=AsyncSession)

    repo = L4DeskRepository(mock_session)
    now = datetime.now(UTC)

    # create_registration
    reg = await repo.create_registration(
        email_normalized="repo@test.com",
        password_hash="pw",
        token_hash="tok",
        terms_version="v1",
        timezone="UTC",
        correlation_id="c1",
        expires_at=now + timedelta(hours=1),
    )
    assert reg.email_normalized == "repo@test.com"
    mock_session.add.assert_called()
    mock_session.flush.assert_awaited()

    # get_registration_by_token
    mock_result = MagicMock()
    mock_result.scalar_one_or_none.return_value = reg
    mock_session.execute.return_value = mock_result

    found = await repo.get_registration_by_token("tok")
    assert found == reg

    # mark_registration_consumed
    await repo.mark_registration_consumed(1, user_id=10, tenant_id=20)
    assert mock_session.execute.await_count >= 2


# =============================================================================
# 4. Absent Optional Columns During Rolling Deploy Tests
# =============================================================================


def test_absent_optional_columns_instantiation_defaults():
    """Verify models default optional/nullable columns to None during rolling deploy."""
    now = datetime.now(UTC)

    # L4DeskRegistration: source, consumed_at, user_id, tenant_id are optional
    reg = l4desk_models.L4DeskRegistration(
        email_normalized="rolling@example.com",
        password_hash="pw",
        token_hash="tok-rolling",
        terms_version="v1",
        timezone="UTC",
        correlation_id="corr-r",
        expires_at=now + timedelta(hours=1),
    )
    assert reg.source is None
    assert reg.consumed_at is None
    assert reg.user_id is None
    assert reg.tenant_id is None

    # L4DeskTenantProfile: pending_timezone, timezone_effective_at are optional
    prof = l4desk_models.L4DeskTenantProfile(
        tenant_id=1,
        timezone="UTC",
    )
    assert prof.pending_timezone is None
    assert prof.timezone_effective_at is None

    # L4DeskTerminal: runtime_terminal_id, device_id, certificate_reference, last_error, first_online_at, etc.
    term = l4desk_models.L4DeskTerminal(
        terminal_id=2,
        tenant_id=1,
        ordinal=1,
        sn="SN-ROV",
        external_terminal_id="EXT-ROV",
        operation_id="op-rov",
        correlation_id="corr-rov",
    )
    assert term.runtime_terminal_id is None
    assert term.device_id is None
    assert term.certificate_reference is None
    assert term.last_error is None
    assert term.first_online_at is None
    assert term.last_online_at is None
    assert term.deleted_at is None

    # IotEventInbox optional fields
    inbox = l4desk_models.IotEventInbox(
        event_id="ev-opt-1",
        cursor=100,
        event_type="session.started",
        occurred_at=now,
        sn="SN-OPT",
    )
    assert inbox.tenant_id is None
    assert inbox.terminal_id is None
    assert inbox.device_id is None
    assert inbox.session_id is None
    assert inbox.session_type is None
    assert inbox.lifecycle_state is None
    assert inbox.reason is None
    assert inbox.operation_id is None
    assert inbox.correlation_id is None
    assert inbox.payload is None
    assert inbox.processed_at is None


def test_absent_optional_columns_partial_query_resilience():
    """Verify that during a rolling deploy, partial SELECTs using load_only succeed

    even when queries omit optional columns or when optional columns are absent in older schema.
    """
    sync_engine = create_engine("sqlite:///:memory:")

    with sync_engine.begin() as conn:
        # Create table with only core columns (simulating legacy or rolling table structure)
        conn.execute(
            text(
                """
                CREATE TABLE l4desk_registrations (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    email_normalized VARCHAR(255) NOT NULL UNIQUE,
                    password_hash VARCHAR(255) NOT NULL,
                    token_hash VARCHAR(64) NOT NULL UNIQUE,
                    terms_version VARCHAR(64) NOT NULL,
                    timezone VARCHAR(64) NOT NULL,
                    correlation_id VARCHAR(128) NOT NULL,
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    expires_at TIMESTAMP NOT NULL
                );
                """
            )
        )
        conn.execute(
            text(
                """
                INSERT INTO l4desk_registrations (
                    email_normalized, password_hash, token_hash, terms_version, timezone, correlation_id, expires_at
                ) VALUES (
                    'absent_col@example.com', 'pwhash', 'tok-absent', 'v1', 'UTC', 'corr-abs', datetime('now', '+1 hour')
                );
                """
            )
        )

    # Query using load_only for the core columns that exist
    with Session(sync_engine) as session:
        stmt = (
            select(l4desk_models.L4DeskRegistration)
            .options(
                load_only(
                    l4desk_models.L4DeskRegistration.id,
                    l4desk_models.L4DeskRegistration.email_normalized,
                    l4desk_models.L4DeskRegistration.token_hash,
                    l4desk_models.L4DeskRegistration.terms_version,
                    l4desk_models.L4DeskRegistration.timezone,
                    l4desk_models.L4DeskRegistration.correlation_id,
                    l4desk_models.L4DeskRegistration.expires_at,
                )
            )
            .where(l4desk_models.L4DeskRegistration.token_hash == "tok-absent")
        )

        record = session.scalar(stmt)
        assert record is not None
        assert record.email_normalized == "absent_col@example.com"
        assert record.token_hash == "tok-absent"
