"""Real034→035 and concurrent single-use proof, isolated from production."""

import asyncio
import importlib.util
import json
import os
from datetime import UTC, datetime, timedelta
from pathlib import Path
from uuid import uuid4

import pytest
from alembic.migration import MigrationContext
from alembic.operations import Operations
from etranprocessing_db.base import Base
from etranprocessing_db.file_manager import FileManagerAgent, FileManagerReadinessProbe
from fastapi import HTTPException
from sqlalchemy import inspect, select, text
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine

from app.models import Terminal
from app.routers.file_manager_probe import consume_probe


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.skipif(
    not os.environ.get("FM_TEST_DATABASE_URL"),
    reason="isolated PostgreSQL not selected",
)
async def test_034_035_roundtrip_and_concurrent_proof():
    root = Path(__file__).resolve().parents[3]
    baseline = json.loads((root / "shared/docs/l4desk/schema-v034.json").read_text())
    path = (
        root / "ProcessingBackend/backend/alembic/versions/035_fm_readiness_probes.py"
    )
    spec = importlib.util.spec_from_file_location("fm_migration", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    schema = "fm_demand_" + uuid4().hex
    engine = create_async_engine(
        os.environ["FM_TEST_DATABASE_URL"],
        connect_args={"server_settings": {"search_path": schema}},
    )
    try:
        async with engine.begin() as conn:
            await conn.execute(text('CREATE SCHEMA "' + schema + '"'))
            assert int(await conn.scalar(text("SHOW server_version_num"))) >= 180000
            for table in Base.metadata.sorted_tables:
                if table.name in baseline:
                    await conn.execute(
                        text(baseline[table.name]["ddl"].replace("%%", "%"))
                    )
                    for index in baseline[table.name]["indexes"]:
                        await conn.execute(text(index))
            await conn.execute(
                text("INSERT INTO terminal_types (id,name) VALUES (0,'fixture')")
            )
            await conn.execute(
                text(
                    "INSERT INTO terminals (id,device_id,sn,org_id,is_active,cert_serial) VALUES (101,701,'fixture',7,TRUE,'AB')"
                )
            )

            def apply(sync, up):
                with Operations.context(MigrationContext.configure(sync)):
                    (module.upgrade if up else module.downgrade)()

            await conn.run_sync(lambda sync: apply(sync, True))
            assert (
                await conn.scalar(text("SELECT sn FROM terminals WHERE id=101"))
                == "fixture"
            )
            assert "fm_readiness_probes" in await conn.run_sync(
                lambda sync: inspect(sync).get_table_names()
            )
            await conn.run_sync(lambda sync: apply(sync, False))
            assert "fm_readiness_probes" not in await conn.run_sync(
                lambda sync: inspect(sync).get_table_names()
            )
            await conn.run_sync(lambda sync: apply(sync, True))
        factory = async_sessionmaker(engine, expire_on_commit=False)
        actor = {
            "X-Org-Id": "7",
            "X-User-Id": "fixture",
            "X-Session-Id": "fixture:view",
            "X-Role": "user",
        }
        identifier = uuid4()
        instance = uuid4()
        now = datetime.now(UTC)
        async with factory() as db:
            db.add(
                FileManagerAgent(
                    terminal_id=101,
                    tenant_id=7,
                    agent_instance_id=instance,
                    agent_version="fixture",
                    protocol_version=2,
                    capabilities=[],
                    filesystem_ready=True,
                    cert_serial="AB",
                    last_seen_at=now,
                )
            )
            db.add(
                FileManagerReadinessProbe(
                    terminal_id=101,
                    tenant_id=7,
                    request_id=identifier,
                    actor=actor,
                    cert_serial="AB",
                    expires_at=now + timedelta(seconds=10),
                    proof_until=now + timedelta(seconds=15),
                    agent_instance_id=instance,
                    result={"available": True},
                    response_hash="a" * 64,
                )
            )
            await db.commit()

        async def consume(session):
            async with factory() as db:
                terminal = await db.scalar(
                    select(Terminal)
                    .where(Terminal.id == 101)
                    .with_for_update(of=Terminal)
                )
                agent = await db.get(FileManagerAgent, 101)
                assert terminal and agent
                try:
                    await consume_probe(db, terminal, actor, identifier, session, agent)
                    await db.commit()
                    return "accepted"
                except HTTPException:
                    await db.rollback()
                    return "rejected"

        assert sorted(await asyncio.gather(consume(uuid4()), consume(uuid4()))) == [
            "accepted",
            "rejected",
        ]
    finally:
        async with engine.begin() as conn:
            await conn.execute(text('DROP SCHEMA IF EXISTS "' + schema + '" CASCADE'))
        await engine.dispose()
