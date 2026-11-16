"""Exercise additive036 DDL, constraints, history and downgrade without services."""

import importlib.util
from pathlib import Path
from uuid import uuid4

import pytest
from alembic.migration import MigrationContext
from alembic.operations import Operations
from sqlalchemy import create_engine, inspect, text
from sqlalchemy.exc import IntegrityError


def test_identity_migration_history_constraints_and_roundtrip():
    path = (
        Path(__file__).parents[1] / "alembic/versions/036_terminal_identity_changes.py"
    )
    spec = importlib.util.spec_from_file_location("identity_migration", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    engine = create_engine("sqlite:///:memory:")
    with engine.begin() as conn:
        for table, column in [
            ("terminals", "id"),
            ("orgs", "org_id"),
            ("certificate_pins", "id"),
        ]:
            conn.exec_driver_sql(f"CREATE TABLE {table} ({column} INTEGER PRIMARY KEY)")
        conn.exec_driver_sql(
            "CREATE TABLE balance_terminal_tsp (rec_id INTEGER PRIMARY KEY, int_day INTEGER, org_id INTEGER, terminal_id INTEGER, tsp_id INTEGER, menu_version INTEGER)"
        )
        conn.exec_driver_sql(
            "CREATE UNIQUE INDEX uq_balance_day_terminal_tsp_ver ON balance_terminal_tsp (int_day, terminal_id, tsp_id, COALESCE(menu_version, 0))"
        )
        context = MigrationContext.configure(conn)
        with Operations.context(context):
            module.upgrade()
        assert {"terminal_identity_changes", "device_admin_events"} <= set(
            inspect(conn).get_table_names()
        )

        def operation(status):
            conn.execute(
                text(
                    "INSERT INTO terminal_identity_changes (terminal_id, operation_id, source_org_id, target_org_id, status, requested_by) VALUES (1, :operation, 1, 2, :status, 'fixture')"
                ),
                {"operation": str(uuid4()), "status": status},
            )

        operation("completed")
        operation("completed")
        operation("pending")
        with pytest.raises(IntegrityError), conn.begin_nested():
            operation("pending")
        with pytest.raises(IntegrityError), conn.begin_nested():
            operation("unknown")
        assert conn.scalar(text("SELECT count(*) FROM terminal_identity_changes")) == 3
        conn.exec_driver_sql(
            "INSERT INTO balance_terminal_tsp VALUES (1, 20261116, 1, 1, 7, 1), (2, 20261116, 2, 1, 7, 1)"
        )
        with (
            Operations.context(context),
            pytest.raises(RuntimeError, match="cross-tenant"),
        ):
            module.downgrade()
        conn.exec_driver_sql("DELETE FROM balance_terminal_tsp WHERE rec_id=2")
        with (
            Operations.context(context),
            pytest.raises(RuntimeError, match="identity operations"),
        ):
            module.downgrade()
        conn.exec_driver_sql("UPDATE terminal_identity_changes SET status='cancelled'")
        with Operations.context(context):
            module.downgrade()
            module.upgrade()
        assert conn.scalar(text("SELECT count(*) FROM terminal_identity_changes")) == 0
    engine.dispose()
