"""Real033 snapshot →034 →033 →034 on an isolated PostgreSQL18 schema.

No historical data migrations or real credentials are imported into fixtures.
"""

import importlib.util
import json
import os
from pathlib import Path

import etranprocessing_db.file_manager
import etranprocessing_db.inventory
import etranprocessing_db.l4desk  # noqa: F401
import pytest
from alembic.migration import MigrationContext
from alembic.operations import Operations
from etranprocessing_db.base import Base
from etranprocessing_db.camera_credentials import (
    CameraCredentialProfile,
    CameraCredentialReceipt,
)
from sqlalchemy import inspect, text
from sqlalchemy.ext.asyncio import create_async_engine


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
@pytest.mark.skipif(
    not os.environ.get("TASK35_TEST_DATABASE_URL"),
    reason="isolated PostgreSQL not selected",
)
async def test_actual_033_expand_downgrade_reupgrade():
    root = Path(__file__).resolve().parents[3]
    baseline = json.loads((root / "shared/docs/l4desk/schema-v033.json").read_text())
    path = (
        root
        / "ProcessingBackend/backend/alembic/versions/034_camera_credential_vault.py"
    )
    spec = importlib.util.spec_from_file_location("task35_migration", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    assert module.revision == "034" and module.down_revision == "033"
    engine = create_async_engine(os.environ["TASK35_TEST_DATABASE_URL"])
    try:
        async with engine.begin() as db:
            assert (await db.scalar(text("SHOW server_version_num"))).startswith("18")
            await db.execute(text("CREATE SCHEMA task35_migration"))
            await db.execute(text("SET LOCAL search_path TO task35_migration"))
            for table in Base.metadata.sorted_tables:
                if table.name in baseline:
                    # Snapshot uses the default PostgreSQL DBAPI percent escaping.
                    # asyncpg SQL text requires the actual modulo operator.
                    await db.execute(
                        text(baseline[table.name]["ddl"].replace("%%", "%"))
                    )
                    for ddl in baseline[table.name]["indexes"]:
                        await db.execute(text(ddl))
            before = await db.run_sync(
                lambda sync: inspect(sync).get_table_names(schema="task35_migration")
            )
            assert set(before) == set(baseline)
            await db.execute(
                text("INSERT INTO terminal_types (id, name) VALUES (0, 'fixture')")
            )
            await db.execute(
                text(
                    "INSERT INTO terminals (id, device_id, sn, org_id, is_active) VALUES (101, 701, 'fixture-sn', 1, TRUE)"
                )
            )

            def apply(sync, upgrade):
                with Operations.context(MigrationContext.configure(sync)):
                    (module.upgrade if upgrade else module.downgrade)()

            await db.run_sync(lambda sync: apply(sync, True))
            assert await db.scalar(
                text(
                    "SELECT credential_recipient_pem IS NULL FROM terminals WHERE id=101"
                )
            )
            assert (
                await db.scalar(text("SELECT count(*) FROM camera_credential_profiles"))
                == 0
            )
            await db.run_sync(lambda sync: apply(sync, False))
            assert (
                await db.scalar(text("SELECT sn FROM terminals WHERE id=101"))
                == "fixture-sn"
            )
            assert set(
                await db.run_sync(
                    lambda sync: inspect(sync).get_table_names(
                        schema="task35_migration"
                    )
                )
            ) == set(before)
            await db.run_sync(lambda sync: apply(sync, True))
            for model in (CameraCredentialProfile, CameraCredentialReceipt):
                columns = await db.run_sync(
                    lambda sync, model=model: inspect(sync).get_columns(
                        model.__tablename__, schema="task35_migration"
                    )
                )
                assert {item["name"] for item in columns} == set(
                    model.__table__.columns.keys()
                )
            await db.execute(text("DROP SCHEMA task35_migration CASCADE"))
    finally:
        await engine.dispose()
