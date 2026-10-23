"""Actual PostgreSQL row-lock/idempotency tests; isolated task database only."""

import asyncio
import os
from uuid import uuid4

import pytest
from etranprocessing_db.camera_credentials import (
    CameraCredentialProfile,
    CameraCredentialReceipt,
)
from etranprocessing_db.models import Terminal
from sqlalchemy import select, update

from app.services.camera_credential_vault import VaultError
from tests import test_camera_credential_vault as vault_fixtures
from tests.test_camera_credential_vault import (
    NOW,
    SCOPE,
    outcome,
    recipient,
    secret,
)

store = vault_fixtures.store

pytestmark = [
    pytest.mark.anyio,
    pytest.mark.skipif(
        not os.environ.get("TASK35_TEST_DATABASE_URL"),
        reason="isolated PostgreSQL not selected",
    ),
]


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.parametrize("conflicting", [False, True])
async def test_concurrent_same_uuid(store, conflicting):
    sessions, vault = store
    target, credentials, operation_id = recipient()[1], secret(), uuid4()
    async with sessions.begin() as db:
        await db.execute(
            update(Terminal)
            .where(Terminal.id == SCOPE.terminal_id)
            .values(cert_serial=target.cert_serial)
        )
    gate = asyncio.Event()

    async def worker(value):
        await gate.wait()
        try:
            async with sessions.begin() as db:
                return await vault.prepare_pending(
                    db,
                    scope=SCOPE,
                    operation_id=operation_id,
                    credentials=value,
                    recipient=target,
                    expires_at_ms=NOW + 50000,
                    task_deadline_ms=NOW + 55000,
                    now_ms=NOW,
                )
        except VaultError as error:
            return error.code

    tasks = [
        asyncio.create_task(worker(credentials)),
        asyncio.create_task(worker(secret() if conflicting else credentials)),
    ]
    gate.set()
    results = await asyncio.gather(*tasks)
    if conflicting:
        assert (
            sum(item == "camera_credential_operation_conflict" for item in results) == 1
        )
    else:
        assert results[0] == results[1]
    async with sessions.begin() as db:
        assert len((await db.scalars(select(CameraCredentialReceipt))).all()) == 1
        profile = await db.scalar(select(CameraCredentialProfile))
        assert profile is not None and profile.version == 1


async def test_concurrent_new_operations_and_stale_outcome(store):
    sessions, vault = store
    target = recipient()[1]
    async with sessions.begin() as db:
        await db.execute(
            update(Terminal)
            .where(Terminal.id == SCOPE.terminal_id)
            .values(cert_serial=target.cert_serial)
        )

    async def worker():
        async with sessions.begin() as db:
            return await vault.prepare_pending(
                db,
                scope=SCOPE,
                operation_id=uuid4(),
                credentials=secret(),
                recipient=target,
                expires_at_ms=NOW + 50000,
                task_deadline_ms=NOW + 55000,
                now_ms=NOW,
            )

    results = await asyncio.gather(worker(), worker())
    assert {item.version for item in results} == {1, 2}
    stale, current = sorted(results, key=lambda item: item.version)
    async with sessions.begin() as db:
        assert not await vault.confirm(
            db, outcome=outcome(stale.operation_id), now_ms=NOW + 1
        )
        assert await vault.confirm(
            db, outcome=outcome(current.operation_id), now_ms=NOW + 1
        )
