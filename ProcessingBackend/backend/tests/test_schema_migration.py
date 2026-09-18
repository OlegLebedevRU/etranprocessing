import contextlib
import io
import json
from pathlib import Path
from unittest.mock import MagicMock

import etranprocessing_db.l4desk
import etranprocessing_db.models
from alembic.config import Config
from alembic.script import ScriptDirectory
from etranprocessing_db.base import Base
from sqlalchemy.dialects.postgresql import dialect

from alembic import command

# Ensure models are loaded
_ = (etranprocessing_db.l4desk.__all__, etranprocessing_db.models.__all__)

L4DESK_TABLES = [
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

SCHEMA_JSON_PATH = (
    Path(__file__).resolve().parents[3]
    / "shared"
    / "docs"
    / "l4desk"
    / "schema-v1.json"
)


def test_schema_contract_gate_tables_and_columns():
    """Verify that all 23 L4Desk/fin/iot tables and their columns in Base.metadata

    strictly match contract schema-v1.json.
    """
    assert SCHEMA_JSON_PATH.exists(), f"Missing schema artifact: {SCHEMA_JSON_PATH}"
    contract_schema = json.loads(SCHEMA_JSON_PATH.read_text(encoding="utf-8"))

    pg = dialect()

    for table_name in L4DESK_TABLES:
        assert table_name in Base.metadata.tables, (
            f"Table {table_name} missing from Base.metadata"
        )
        assert table_name in contract_schema, (
            f"Table {table_name} missing from schema-v1.json contract"
        )

        model_table = Base.metadata.tables[table_name]
        contract_table = contract_schema[table_name]

        model_cols = {c.name: c.type.compile(dialect=pg) for c in model_table.columns}
        contract_cols = {c["name"]: c["type"] for c in contract_table["columns"]}

        assert model_cols == contract_cols, (
            f"Column definitions mismatch in {table_name}: {model_cols} != {contract_cols}"
        )

        model_nullability = {c.name: c.nullable for c in model_table.columns}
        contract_nullability = {
            c["name"]: c["nullable"] for c in contract_table["columns"]
        }
        assert model_nullability == contract_nullability, (
            f"Nullability mismatch in {table_name}: {model_nullability} != {contract_nullability}"
        )


def test_schema_contract_gate_indexes_and_constraints():
    """Verify indexes and unique/check constraints match contract."""
    contract_schema = json.loads(SCHEMA_JSON_PATH.read_text(encoding="utf-8"))

    for table_name in L4DESK_TABLES:
        model_table = Base.metadata.tables[table_name]
        contract_indexes = contract_schema[table_name].get("indexes", [])

        # Check all non-constraint index names in contract exist on model
        for idx_sql in contract_indexes:
            parts = idx_sql.split()
            # CREATE [UNIQUE] INDEX index_name ON ...
            idx_name = parts[3] if parts[1] == "UNIQUE" else parts[2]
            model_index_names = {idx.name for idx in model_table.indexes}
            assert idx_name in model_index_names, (
                f"Index {idx_name} missing from table {table_name}"
            )


def test_alembic_linear_history():
    """Verify Alembic history is linear and 027 is the single head following 026."""
    config = Config("alembic.ini")
    script = ScriptDirectory.from_config(config)

    heads = script.get_heads()
    assert heads == ["027"], f"Expected single head '027', got {heads}"

    rev_027 = script.get_revision("027")
    assert rev_027 is not None
    assert rev_027.down_revision == "026", (
        f"Expected down_revision '026', got {rev_027.down_revision}"
    )


def test_clean_upgrade_sql_generation():
    """Verify that static SQL upgrade 026:027 generates all 23 CREATE TABLE statements in topological order."""
    config = Config("alembic.ini")
    buf = io.StringIO()

    with contextlib.redirect_stdout(buf):
        command.upgrade(config, "026:027", sql=True)

    sql = buf.getvalue()
    assert len(sql) > 0, "No SQL output produced"
    assert "BEGIN;" in sql
    assert "COMMIT;" in sql
    assert "UPDATE alembic_version SET version_num='027'" in sql

    # Verify all 23 tables are created
    for t in L4DESK_TABLES:
        assert f"CREATE TABLE {t}" in sql, f"CREATE TABLE {t} not found in upgrade SQL"

    # Verify topological order: fin_archive_batches before fin_ledger_transactions
    pos_batches = sql.find("CREATE TABLE fin_archive_batches")
    pos_tx = sql.find("CREATE TABLE fin_ledger_transactions")
    assert pos_batches < pos_tx, (
        "fin_archive_batches must precede fin_ledger_transactions"
    )

    # Verify l4desk_terminals before fin_usage_daily
    pos_terminals = sql.find("CREATE TABLE l4desk_terminals")
    pos_usage = sql.find("CREATE TABLE fin_usage_daily")
    assert pos_terminals < pos_usage, "l4desk_terminals must precede fin_usage_daily"


def test_downgrade_sql_generation():
    """Verify that static SQL downgrade 027:026 drops all 23 tables in reverse topological order."""
    config = Config("alembic.ini")
    buf = io.StringIO()

    with contextlib.redirect_stdout(buf):
        command.downgrade(config, "027:026", sql=True)

    sql = buf.getvalue()
    assert len(sql) > 0, "No SQL output produced"
    assert "BEGIN;" in sql
    assert "COMMIT;" in sql
    assert "UPDATE alembic_version SET version_num='026'" in sql

    # Verify all 23 tables are dropped
    for t in L4DESK_TABLES:
        assert f"DROP TABLE {t};" in sql, f"DROP TABLE {t} not found in downgrade SQL"

    # Verify reverse topological order: fin_usage_daily before l4desk_terminals
    pos_usage = sql.find("DROP TABLE fin_usage_daily;")
    pos_terminals = sql.find("DROP TABLE l4desk_terminals;")
    assert pos_usage < pos_terminals, (
        "fin_usage_daily must be dropped before l4desk_terminals"
    )


def test_upgrade_existing_db_and_repeated_deployment_idempotency(monkeypatch):
    """Test that upgrade logic handles existing tables (e.g. IoT tables) and is idempotent."""
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "migration_027",
        Path(__file__).resolve().parents[1]
        / "alembic"
        / "versions"
        / "027_add_l4desk_and_fin_ledger.py",
    )
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    # Simulate existing DB with iot_* tables already present
    existing_tables = {
        "iot_consumer_checkpoints",
        "iot_event_inbox",
        "iot_event_quarantine",
    }

    mock_inspector = MagicMock()
    mock_inspector.get_table_names.return_value = list(existing_tables)
    mock_inspector.get_indexes.return_value = []

    mock_bind = MagicMock()
    monkeypatch.setattr("sqlalchemy.inspect", lambda bind: mock_inspector)

    created_tables = []
    created_indexes = []

    mock_op = MagicMock()
    mock_op.get_bind.return_value = mock_bind
    mock_op_context = MagicMock()
    mock_op_context.as_sql = False
    mock_op.get_context.return_value = mock_op_context

    def fake_create_table(name, *args, **kwargs):
        created_tables.append(name)

    def fake_create_index(name, table_name, *args, **kwargs):
        created_indexes.append((name, table_name))

    mock_op.create_table = fake_create_table
    mock_op.create_index = fake_create_index
    monkeypatch.setattr(mod, "op", mock_op)

    # Run upgrade in existing DB scenario
    mod.upgrade()

    # The 3 existing tables must NOT be recreated
    assert "iot_consumer_checkpoints" not in created_tables
    assert "iot_event_inbox" not in created_tables
    assert "iot_event_quarantine" not in created_tables

    # The other 20 tables MUST be created
    for t in L4DESK_TABLES:
        if t not in existing_tables:
            assert t in created_tables, f"Table {t} should have been created"

    # Now simulate repeated deployment (all 23 tables and their indexes already present)
    mock_inspector.get_table_names.return_value = L4DESK_TABLES
    mock_inspector.get_indexes.side_effect = lambda tname: [
        {"name": idx.name} for idx in Base.metadata.tables[tname].indexes
    ]
    created_tables.clear()
    created_indexes.clear()

    mod.upgrade()

    # Zero tables and zero indexes created on repeated run (safe & idempotent)
    assert len(created_tables) == 0, (
        f"Expected 0 tables created on repeated upgrade, got {created_tables}"
    )
    assert len(created_indexes) == 0, (
        f"Expected 0 indexes created on repeated upgrade, got {created_indexes}"
    )


def test_specific_business_constraints():
    """Verify specific business logic constraints defined in contract 04A."""
    fin_usage = Base.metadata.tables["fin_usage_daily"]
    ck_names = {c.name for c in fin_usage.constraints if hasattr(c, "name")}
    assert "fin_usage_seconds_ck" in ck_names
    assert "fin_usage_rounding_ck" in ck_names
    assert "fin_usage_posted_ck" in ck_names
    assert "fin_usage_values_ck" in ck_names

    l4desk_term = Base.metadata.tables["l4desk_terminals"]
    ck_names_term = {c.name for c in l4desk_term.constraints if hasattr(c, "name")}
    assert "l4desk_terminal_ordinal_ck" in ck_names_term
    assert "l4desk_terminal_provisioning_ck" in ck_names_term
    assert "l4desk_terminal_pin_ck" in ck_names_term

    fin_archive = Base.metadata.tables["fin_archive_batches"]
    ck_names_archive = {c.name for c in fin_archive.constraints if hasattr(c, "name")}
    assert "fin_archive_purge_ck" in ck_names_archive
    assert "fin_archive_verified_ck" in ck_names_archive
    assert "fin_archive_status_ck" in ck_names_archive
    assert "fin_archive_interval_ck" in ck_names_archive

    fin_tx = Base.metadata.tables["fin_ledger_transactions"]
    ck_names_tx = {c.name for c in fin_tx.constraints if hasattr(c, "name")}
    assert "fin_transaction_correction_ck" in ck_names_tx
    assert "fin_transaction_posted_ck" in ck_names_tx
    assert "fin_transaction_kind_ck" in ck_names_tx
    assert "fin_transaction_status_ck" in ck_names_tx
    assert "fin_transaction_balance_ck" in ck_names_tx
