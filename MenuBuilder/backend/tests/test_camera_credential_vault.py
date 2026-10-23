"""Production vault helpers, durable SQL transactions and frozen oracle interop.

Fixture keys/secrets are random and stay in RAM. SQLite is the portable path;
TASK35_TEST_DATABASE_URL selects an isolated PostgreSQL database for the same tests.
"""

import base64
import importlib.util
import os
from dataclasses import replace
from datetime import UTC, datetime, timedelta
from pathlib import Path
from uuid import uuid4

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from etranprocessing_db.base import Base
from etranprocessing_db.camera_credentials import (
    CameraCredentialProfile,
    CameraCredentialReceipt,
)
from etranprocessing_db.models import Terminal, TerminalType
from pydantic import SecretStr
from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine

from app.services.camera_credential_recipient import CredentialRecipient
from app.services.camera_credential_vault import (
    CameraCredentials,
    CameraCredentialScope,
    CameraCredentialVault,
    ConfirmedCameraOutcome,
    VaultError,
)
from app.services.camera_credential_vault_config import CameraCredentialVaultSettings

SCOPE = CameraCredentialScope(1, 101, 701, "fixture-camera-terminal", "cam:ip000001")
NOW = int(datetime.now(UTC).timestamp() * 1000)
TABLES = [
    TerminalType.__table__,
    Terminal.__table__,
    CameraCredentialProfile.__table__,
    CameraCredentialReceipt.__table__,
]


@pytest.fixture
def anyio_backend():
    return "asyncio"


def recipient():
    key = rsa.generate_private_key(65537, 2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, SCOPE.sn)])
    cert = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.now(UTC) - timedelta(days=1))
        .not_valid_after(datetime.now(UTC) + timedelta(days=1))
        .sign(key, hashes.SHA256())
    )
    return key, CredentialRecipient(
        SCOPE.sn,
        SCOPE.device_id,
        format(cert.serial_number, "X"),
        cert.fingerprint(hashes.SHA256()).hex(),
        cert,
    )


def secret():
    return CameraCredentials(
        SecretStr(os.urandom(8).hex()), SecretStr(os.urandom(20).hex())
    )


@pytest.fixture
async def store():
    url = os.environ.get("TASK35_TEST_DATABASE_URL", "sqlite+aiosqlite:///:memory:")
    engine = create_async_engine(url)
    async with engine.begin() as connection:
        await connection.run_sync(
            lambda sync: Base.metadata.create_all(sync, tables=TABLES)
        )
    sessions = async_sessionmaker(engine, expire_on_commit=False)
    async with sessions.begin() as db:
        db.add(TerminalType(id=0, name="fixture"))
        db.add(
            Terminal(
                id=SCOPE.terminal_id,
                device_id=SCOPE.device_id,
                sn=SCOPE.sn,
                org_id=SCOPE.tenant_id,
                terminal_type_id=0,
            )
        )
    settings = CameraCredentialVaultSettings(
        kek_b64=SecretStr(base64.b64encode(os.urandom(32)).decode()),
        kek_id="fixture-key",
    )
    try:
        yield sessions, CameraCredentialVault(settings)
    finally:
        async with engine.begin() as connection:
            await connection.run_sync(
                lambda sync: Base.metadata.drop_all(sync, tables=list(reversed(TABLES)))
            )
        await engine.dispose()


def oracle():
    path = (
        Path(__file__).resolve().parents[3]
        / "contracts"
        / "siplite-camera-v1"
        / "credential_reference.py"
    )
    spec = importlib.util.spec_from_file_location("frozen_camera_oracle", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


async def prepare(
    db, vault, *, operation_id=None, credentials=None, target=None, now=NOW
):
    target = target or recipient()[1]
    await db.execute(
        update(Terminal)
        .where(Terminal.id == SCOPE.terminal_id)
        .values(cert_serial=target.cert_serial)
    )
    return await vault.prepare_pending(
        db,
        scope=SCOPE,
        operation_id=operation_id or uuid4(),
        credentials=credentials or secret(),
        recipient=target,
        expires_at_ms=NOW + 50000,
        task_deadline_ms=NOW + 55000,
        now_ms=now,
    )


def outcome(operation_id, *, success=True, mode="custom"):
    return ConfirmedCameraOutcome(
        SCOPE, operation_id, operation_id, success, success, mode
    )


@pytest.mark.anyio
async def test_durable_dedup_conflicts_and_oracle(store):
    sessions, vault = store
    key, target = recipient()
    credentials, operation_id = secret(), uuid4()
    async with sessions.begin() as db:
        first = await prepare(
            db, vault, operation_id=operation_id, credentials=credentials, target=target
        )
    async with sessions.begin() as db:
        second = await prepare(
            db,
            vault,
            operation_id=operation_id,
            credentials=credentials,
            target=target,
            now=NOW + 1,
        )
        assert second == first
        assert first.envelope is not None
        opened = oracle().unseal(
            key,
            first.envelope,
            sn=SCOPE.sn,
            operation_id=str(operation_id),
            recipient_cert_sha256=target.recipient_cert_sha256,
            now_ms=NOW,
        )
        assert opened == credentials.internal_plaintext()
        row = await db.get(CameraCredentialReceipt, operation_id)
        assert row is not None
        assert credentials.password.get_secret_value().encode() not in row.ciphertext
        assert "wrapped_key" not in repr(first)
        assert credentials.password.get_secret_value() not in repr(credentials)
    async with sessions.begin() as db:
        with pytest.raises(VaultError, match="operation_conflict"):
            await prepare(
                db,
                vault,
                operation_id=operation_id,
                credentials=secret(),
                target=target,
            )


@pytest.mark.anyio
async def test_active_pending_stale_failure_and_clear(store):
    sessions, vault = store
    target, credentials = recipient()[1], secret()
    async with sessions.begin() as db:
        first = await prepare(db, vault, target=target, credentials=credentials)
        assert await vault.confirm(
            db, outcome=outcome(first.operation_id), now_ms=NOW + 1
        )
    async with sessions.begin() as db:
        second = await prepare(db, vault, target=target)
        third = await prepare(db, vault, target=target)
        assert not await vault.confirm(
            db, outcome=outcome(second.operation_id), now_ms=NOW + 2
        )
        assert not await vault.confirm(
            db, outcome=outcome(third.operation_id, success=False), now_ms=NOW + 3
        )
        assert (
            await vault.load_active_internal(db, scope=SCOPE)
        ).internal_plaintext() == credentials.internal_plaintext()
        clear = await vault.prepare_pending(
            db,
            scope=SCOPE,
            operation_id=uuid4(),
            credentials=None,
            recipient=target,
            expires_at_ms=NOW + 50000,
            task_deadline_ms=NOW + 55000,
            now_ms=NOW + 4,
        )
        assert not await vault.confirm(
            db,
            outcome=replace(
                outcome(clear.operation_id, mode="stock"), gauge84_operation_id=uuid4()
            ),
            now_ms=NOW + 5,
        )
        assert not await vault.confirm(
            db, outcome=outcome(clear.operation_id), now_ms=NOW + 5
        )
        assert (await vault.safe_metadata(db, scope=SCOPE))["has_active"]
        assert await vault.confirm(
            db, outcome=outcome(clear.operation_id, mode="stock"), now_ms=NOW + 6
        )
        assert not (await vault.safe_metadata(db, scope=SCOPE))["has_active"]


@pytest.mark.anyio
async def test_rotation_stable_kek_recovers_new_envelope(store):
    sessions, vault = store
    key_a, target_a = recipient()
    key_b, target_b = recipient()
    credentials = secret()
    async with sessions.begin() as db:
        first = await prepare(db, vault, target=target_a, credentials=credentials)
        with pytest.raises(VaultError, match="recipient_changed"):
            await vault.original_envelope(
                db,
                scope=SCOPE,
                operation_id=first.operation_id,
                recipient=target_b,
                now_ms=NOW + 1,
            )
        assert await vault.confirm(
            db, outcome=outcome(first.operation_id), now_ms=NOW + 2
        )
    async with sessions.begin() as db:
        recovered = await vault.load_active_internal(db, scope=SCOPE)
        second = await prepare(db, vault, target=target_b, credentials=recovered)
        assert second.envelope is not None
        arguments = {
            "sn": SCOPE.sn,
            "operation_id": str(second.operation_id),
            "recipient_cert_sha256": target_b.recipient_cert_sha256,
            "now_ms": NOW,
        }
        assert (
            oracle().unseal(key_b, second.envelope, **arguments)
            == credentials.internal_plaintext()
        )
        with pytest.raises(ValueError):
            oracle().unseal(key_a, second.envelope, **arguments)


@pytest.mark.anyio
@pytest.mark.parametrize(
    "column",
    [
        "nonce",
        "ciphertext",
        "wrap_nonce",
        "wrapped_dek",
        "key_id",
        "algorithm",
        "expires_at_ms",
        "version",
        "kind",
    ],
)
async def test_database_tamper_fails_closed(store, column):
    sessions, vault = store
    target = recipient()[1]
    async with sessions.begin() as db:
        receipt = await prepare(db, vault, target=target)
        row = await db.get(CameraCredentialReceipt, receipt.operation_id)
        assert row is not None
        profile = await db.scalar(select(CameraCredentialProfile))
        assert profile is not None
        value = getattr(row, column)
        setattr(
            row,
            column,
            bytes([value[0] ^ 1]) + value[1:]
            if isinstance(value, bytes)
            else value + 1
            if isinstance(value, int)
            else "tampered",
        )
        # Direct crypto check avoids persistence constraints masking AEAD verification.
        with pytest.raises(VaultError):
            vault._decrypt(profile, row)
        await db.rollback()


@pytest.mark.anyio
@pytest.mark.parametrize(
    "column,value",
    [
        ("tenant_id", 2),
        ("sn", "other"),
        ("device_id", 702),
        ("camera_id", "cam:ip000002"),
        ("terminal_id", 102),
    ],
)
async def test_profile_aad_swap_fails(store, column, value):
    sessions, vault = store
    async with sessions.begin() as db:
        receipt = await prepare(db, vault)
        row = await db.get(CameraCredentialReceipt, receipt.operation_id)
        profile = await db.scalar(select(CameraCredentialProfile))
        assert row is not None and profile is not None
        setattr(profile, column, value)
        with pytest.raises(VaultError):
            vault._decrypt(profile, row)
        await db.rollback()


@pytest.mark.anyio
async def test_wrong_kek_expiry_and_scope(store):
    sessions, vault = store
    target = recipient()[1]
    async with sessions.begin() as db:
        receipt = await prepare(db, vault, target=target)
        row = await db.get(CameraCredentialReceipt, receipt.operation_id)
        profile = await db.scalar(select(CameraCredentialProfile))
        assert row is not None and profile is not None
        wrong = CameraCredentialVault(
            CameraCredentialVaultSettings(
                kek_b64=SecretStr(base64.b64encode(os.urandom(32)).decode()),
                kek_id="fixture-key",
            )
        )
        with pytest.raises(VaultError):
            wrong._decrypt(profile, row)
        with pytest.raises(VaultError, match="not_deliverable"):
            await vault.original_envelope(
                db,
                scope=SCOPE,
                operation_id=receipt.operation_id,
                recipient=target,
                now_ms=NOW + 50001,
            )
        with pytest.raises(VaultError, match="scope_mismatch"):
            await vault.safe_metadata(db, scope=replace(SCOPE, tenant_id=2))
        assert not await vault.confirm(
            db, outcome=outcome(receipt.operation_id), now_ms=NOW + 50001
        )
        assert not (await vault.safe_metadata(db, scope=SCOPE))["has_active"]


@pytest.mark.parametrize(
    "username,password",
    [("a:b", "x"), ("a", "\n"), ("", "x"), ("x", "я" * 65), ("я" * 33, "x")],
)
def test_plaintext_rejected_without_echo(username, password):
    with pytest.raises(VaultError, match="^camera_credential_invalid$"):
        CameraCredentials(SecretStr(username), SecretStr(password)).internal_plaintext()


def test_key_config_fail_closed():
    with pytest.raises(ValueError, match="key_unavailable"):
        CameraCredentialVaultSettings(kek_b64=SecretStr(""), kek_id="").key()


@pytest.mark.anyio
async def test_completed_replay_and_cancel_never_redeliver_or_destroy_active(store):
    sessions, vault = store
    target, credentials, operation_id = recipient()[1], secret(), uuid4()
    async with sessions.begin() as db:
        first = await prepare(
            db, vault, target=target, credentials=credentials, operation_id=operation_id
        )
        assert await vault.confirm(db, outcome=outcome(operation_id), now_ms=NOW + 1)
    async with sessions.begin() as db:
        replay = await prepare(
            db,
            vault,
            target=target,
            credentials=credentials,
            operation_id=operation_id,
            now=NOW + 60001,
        )
        assert (
            replay.operation_id == first.operation_id
            and replay.expires_at_ms == first.expires_at_ms
        )
        assert replay.envelope is None and not replay.delivery_allowed
        pending = await prepare(db, vault, target=target)
        assert await vault.fail_pending(
            db, scope=SCOPE, operation_id=pending.operation_id
        )
        assert (
            await vault.load_active_internal(db, scope=SCOPE)
        ).internal_plaintext() == credentials.internal_plaintext()


@pytest.mark.anyio
async def test_cached_old_recipient_rejected_by_current_serial(store):
    sessions, vault = store
    first_target, next_target = recipient()[1], recipient()[1]
    async with sessions.begin() as db:
        first = await prepare(db, vault, target=first_target)
        await db.execute(
            update(Terminal)
            .where(Terminal.id == SCOPE.terminal_id)
            .values(cert_serial=next_target.cert_serial)
        )
        with pytest.raises(VaultError, match="recipient_changed"):
            await vault.original_envelope(
                db,
                scope=SCOPE,
                operation_id=first.operation_id,
                recipient=first_target,
                now_ms=NOW + 1,
            )


@pytest.mark.anyio
async def test_expired_pending_recovery_requires_new_operation(store):
    sessions, vault = store
    target_a = recipient()[1]
    credentials = secret()
    async with sessions.begin() as db:
        first = await prepare(db, vault, target=target_a, credentials=credentials)
    async with sessions.begin() as db:
        recovered = await vault.load_for_redelivery_internal(
            db, scope=SCOPE, source_operation_id=first.operation_id
        )
        assert recovered.internal_plaintext() == credentials.internal_plaintext()
        second = await vault.prepare_pending(
            db,
            scope=SCOPE,
            operation_id=uuid4(),
            credentials=recovered,
            recipient=target_a,
            expires_at_ms=NOW + 100000,
            task_deadline_ms=NOW + 110000,
            now_ms=NOW + 60001,
        )
        assert second.operation_id != first.operation_id and second.delivery_allowed
        with pytest.raises(VaultError, match="redelivery_source_unavailable"):
            await vault.load_for_redelivery_internal(
                db, scope=SCOPE, source_operation_id=first.operation_id
            )
        assert await vault.fail_pending(
            db, scope=SCOPE, operation_id=second.operation_id
        )
        assert (
            await vault.load_for_redelivery_internal(
                db, scope=SCOPE, source_operation_id=second.operation_id
            )
        ).internal_plaintext() == credentials.internal_plaintext()


@pytest.mark.anyio
@pytest.mark.parametrize(
    "field,value",
    [
        ("sn", "other"),
        ("operation_id", "other"),
        ("expires_at_ms", 1),
        ("alg", "other"),
        ("v", 2),
        ("unknown", True),
    ],
)
async def test_production_envelope_metadata_checks(store, field, value):
    sessions, vault = store
    target = recipient()[1]
    async with sessions.begin() as db:
        first = await prepare(db, vault, target=target)
        row = await db.get(CameraCredentialReceipt, first.operation_id)
        profile = await db.scalar(select(CameraCredentialProfile))
        assert row is not None and profile is not None
        payload = vault._decrypt(profile, row)
        payload["envelope"][field] = value
        vault._encrypt(profile, row, payload)
        with pytest.raises(VaultError):
            await vault.original_envelope(
                db,
                scope=SCOPE,
                operation_id=first.operation_id,
                recipient=target,
                now_ms=NOW + 1,
            )
