"""Verify additive FM migration on an isolated disposable PostgreSQL 18."""

import argparse
import subprocess
import time
from uuid import uuid4

PROGRAM = r"""
import asyncio
import etranprocessing_db.models
import etranprocessing_db.l4desk
import etranprocessing_db.file_manager
from etranprocessing_db.base import Base
from sqlalchemy import inspect, text
from sqlalchemy.ext.asyncio import create_async_engine
from app.config import settings
from alembic.config import Config
from alembic import command

async def prepare():
    engine = create_async_engine(settings.database_url)
    async with engine.begin() as conn:
        tables = [t for t in Base.metadata.sorted_tables if not t.name.startswith('fm_')]
        await conn.run_sync(lambda c: Base.metadata.create_all(c, tables=tables))
    await engine.dispose()
asyncio.run(prepare())
config = Config('alembic.ini')
command.stamp(config, '031')
command.upgrade(config, '032')
async def verify():
    engine = create_async_engine(settings.database_url)
    async with engine.connect() as conn:
        assert (await conn.execute(text('select version_num from alembic_version'))).scalar_one() == '032'
        tables = await conn.run_sync(lambda c: inspect(c).get_table_names())
        assert {'fm_agents','fm_operations'} <= set(tables)
        indexes = await conn.run_sync(lambda c: inspect(c).get_indexes('fm_operations'))
        assert any(i['unique'] and 'committing' in str(i['dialect_options']) for i in indexes)
        constraints = await conn.run_sync(lambda c: inspect(c).get_check_constraints('fm_operations'))
        assert any('67108864' in i['sqltext'] for i in constraints)
        assert set(Base.metadata.tables) <= set(tables)
    await engine.dispose()
asyncio.run(verify())
print('PASS PostgreSQL18: 031->032, full prior ORM schema, FM indexes/constraints verified')
"""


def docker(*args, capture=False):
    result = subprocess.run(
        ["sudo", "-n", "docker", *args],
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
    )
    return result.stdout.strip() if capture else ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    args = parser.parse_args()
    name = "fm-migration-" + uuid4().hex[:12]
    network = name + "-net"
    docker("pull", args.image)
    docker("pull", "postgres:18")
    docker("network", "create", "--internal", network)
    try:
        docker(
            "run",
            "-d",
            "--name",
            name,
            "--network",
            network,
            "--memory",
            "256m",
            "-e",
            "POSTGRES_HOST_AUTH_METHOD=trust",
            "-e",
            "POSTGRES_DB=fm",
            "postgres:18",
        )
        for _ in range(30):
            if (
                subprocess.run(
                    [
                        "sudo",
                        "-n",
                        "docker",
                        "exec",
                        name,
                        "pg_isready",
                        "-U",
                        "postgres",
                    ],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                ).returncode
                == 0
            ):
                break
            time.sleep(1)
        else:
            raise RuntimeError("Disposable PostgreSQL not ready")
        docker(
            "run",
            "--rm",
            "--network",
            "container:" + name,
            "--memory",
            "256m",
            "-e",
            "DATABASE_URL=postgresql+asyncpg://postgres@localhost/fm",
            "--entrypoint",
            "python",
            args.image,
            "-c",
            PROGRAM,
        )
    finally:
        # Exact owned names only; no shared volumes or production database connection.
        subprocess.run(["sudo", "-n", "docker", "rm", "-f", "-v", name], check=False)
        subprocess.run(["sudo", "-n", "docker", "network", "rm", network], check=False)


if __name__ == "__main__":
    main()
