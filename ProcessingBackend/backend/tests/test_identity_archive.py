"""Real foreign keys: source session/financial snapshots survive repeated transfers."""

import importlib.util
from datetime import UTC, datetime, timedelta
from pathlib import Path
from uuid import uuid4

import pytest
from etranprocessing_db import Base
from etranprocessing_db.l4desk import (
    FinBillingCycle,
    FinBillingProfile,
    FinTariffVersion,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
    L4DeskRemoteSession,
    L4DeskTerminal,
)
from etranprocessing_db.models.device_events import TerminalIdentityChange
from sqlalchemy import create_engine, event, insert, select
from sqlalchemy.orm import Session

from app.services.identity_change import archive_technical_identity


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_historical_composite_owner_fks_and_money_survive_two_transfers():
    # Reuse the shared contract's SQLite JSON/default compilers and money fixtures.
    path = (
        Path(__file__).resolve().parents[3] / "shared/tests/test_l4desk_constraints.py"
    )
    spec = importlib.util.spec_from_file_location("identity_shared_constraints", path)
    assert spec and spec.loader
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    engine = create_engine("sqlite://")

    @event.listens_for(engine, "connect")
    def foreign_keys(connection, _record):
        connection.execute("PRAGMA foreign_keys=ON")

    names = {"orgs", "users", "terminal_types", "terminals"}
    tables = [
        table
        for table in Base.metadata.sorted_tables
        if table.name in names or table.name.startswith(("fin_", "l4desk_", "iot_"))
    ]
    Base.metadata.create_all(engine, tables=tables)
    now = datetime(2026, 11, 16, tzinfo=UTC)
    with engine.begin() as connection:
        for tenant in (1, 2):
            connection.execute(
                insert(Base.metadata.tables["orgs"]).values(
                    org_id=tenant,
                    org_name=f"fixture-{tenant}",
                    name=f"fixture-{tenant}",
                )
            )
        connection.execute(
            insert(Base.metadata.tables["terminal_types"]).values(id=0, name="fixture")
        )
        connection.execute(
            insert(Base.metadata.tables["terminals"]).values(
                id=1, device_id=773, sn="fixture-sn", org_id=1
            )
        )
        connection.execute(
            insert(L4DeskTerminal).values(
                terminal_id=1,
                tenant_id=1,
                runtime_terminal_id=1,
                ordinal=1,
                sn="fixture-sn",
                device_id=773,
                external_terminal_id="1",
                operation_id="fixture-original",
                correlation_id="fixture",
                created_at=now,
            )
        )
        connection.execute(insert(FinBillingProfile).values(tenant_id=1))
        connection.execute(
            insert(FinBillingCycle).values(
                id=1,
                tenant_id=1,
                sequence=0,
                starts_at=now,
                ends_at=now + timedelta(days=30),
                grace_deadline=now + timedelta(days=3),
                timezone="UTC",
            )
        )
        connection.execute(
            insert(FinTariffVersion).values(
                id=1,
                version="fixture",
                effective_from=now,
                terminal_month_kopecks=10000,
                hourly_rate_kopecks=100,
                free_daily_seconds=7200,
                actor="fixture",
                correlation_id="fixture",
            )
        )
        connection.execute(insert(FinUsageDaily).values(**fixtures.usage_values()))
        connection.execute(
            insert(FinTerminalMonthlyCharge).values(**fixtures.monthly_values())
        )
        connection.execute(
            insert(L4DeskRemoteSession).values(
                id=1,
                tenant_id=1,
                terminal_id=1,
                operation_id="fixture-session",
                correlation_id="fixture",
                session_type="console",
                state="closed",
                requested_at=now,
                active_at=now,
                closed_at=now,
            )
        )
        before_usage = dict(
            connection.execute(select(FinUsageDaily.__table__)).mappings().one()
        )
        before_monthly = dict(
            connection.execute(select(FinTerminalMonthlyCharge.__table__))
            .mappings()
            .one()
        )
        session = Session(connection)

        class Adapter:
            get_bind = session.get_bind
            add = session.add

            async def scalar(self, statement):
                return session.scalar(statement)

            async def execute(self, statement, *args):
                return session.execute(statement, *args)

            async def flush(self):
                session.flush()

        db = Adapter()
        current = session.get(L4DeskTerminal, 1)
        assert current
        first = TerminalIdentityChange(
            operation_id=uuid4(),
            terminal_id=1,
            source_org_id=1,
            target_org_id=2,
            old_serial="A",
            new_serial="B",
        )
        await archive_technical_identity(db, current, first)
        session.flush()
        archive_id = first.archived_technical_terminal_id
        assert archive_id and archive_id < 0
        archived = session.get(L4DeskTerminal, archive_id)
        assert (
            archived
            and archived.tenant_id == 1
            and archived.runtime_terminal_id is None
            and archived.deleted_at
        )
        assert archived.sn == current.sn == "fixture-sn"
        assert (
            current.terminal_id == current.runtime_terminal_id == 1
            and current.tenant_id == 2
        )
        assert session.scalar(select(L4DeskRemoteSession.tenant_id)) == 1
        assert session.scalar(select(L4DeskRemoteSession.terminal_id)) == archive_id
        for model, before in (
            (FinUsageDaily, before_usage),
            (FinTerminalMonthlyCharge, before_monthly),
        ):
            after = dict(connection.execute(select(model.__table__)).mappings().one())
            assert after == before | {"terminal_id": archive_id}
        second = TerminalIdentityChange(
            operation_id=uuid4(),
            terminal_id=1,
            source_org_id=2,
            target_org_id=1,
            old_serial="B",
            new_serial="C",
        )
        await archive_technical_identity(db, current, second)
        session.flush()
        assert second.archived_technical_terminal_id != archive_id
        assert current.terminal_id == 1 and current.tenant_id == 1
        assert session.scalar(select(L4DeskRemoteSession.terminal_id)) == archive_id
        assert connection.exec_driver_sql("PRAGMA foreign_key_check").all() == []
        session.close()
    engine.dispose()
