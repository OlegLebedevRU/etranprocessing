"""Internal transactional camera vault. No public plaintext readback or dispatch.

Caller authenticates/authorizes scope and commits or rolls back the transaction.
Recipient must be freshly fetched from PB for every prepare/replay/delivery.
"""

import base64
import hmac
import json
import os
import re
from dataclasses import dataclass, field
from datetime import UTC, datetime
from typing import Literal
from uuid import UUID, uuid4

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from etranprocessing_db.camera_credentials import (
    CameraCredentialProfile,
    CameraCredentialReceipt,
)
from etranprocessing_db.models import Terminal
from pydantic import SecretStr
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.services.camera_credential_recipient import CredentialRecipient
from app.services.camera_credential_vault_config import CameraCredentialVaultSettings

ALGORITHM = "A256GCM-v1"
TRANSPORT_ALGORITHM = "RSA-OAEP-256+A256GCM"
HEADER = (
    "v",
    "alg",
    "sn",
    "operation_id",
    "recipient_cert_sha256",
    "expires_at_ms",
    "wrapped_key",
)


class VaultError(ValueError):
    def __init__(
        self, code: str = "camera_credential_vault_unavailable", status_code: int = 409
    ) -> None:
        self.code = code
        self.status_code = status_code
        super().__init__(code)


def _json(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")


@dataclass(frozen=True)
class CameraCredentialScope:
    tenant_id: int
    terminal_id: int
    device_id: int
    sn: str
    camera_id: str

    def __post_init__(self) -> None:
        if (
            min(self.tenant_id, self.terminal_id, self.device_id) <= 0
            or not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", self.sn)
            or not re.fullmatch(r"cam:ip[0-9]{6}", self.camera_id)
        ):
            raise VaultError("camera_credential_scope_invalid")


@dataclass(frozen=True, repr=False)
class CameraCredentials:
    username: SecretStr
    password: SecretStr

    def __repr__(self) -> str:
        return "CameraCredentials(<redacted>)"

    def internal_plaintext(self) -> dict[str, str]:
        value = {
            "username": self.username.get_secret_value(),
            "password": self.password.get_secret_value(),
        }
        for name, limit in (("username", 64), ("password", 128)):
            text = value[name]
            if (
                not 0 < len(text.encode("utf-8")) <= limit
                or any(ord(char) < 32 or ord(char) == 127 for char in text)
                or name == "username"
                and ":" in text
            ):
                raise VaultError("camera_credential_invalid", 422)
        return value


@dataclass(frozen=True)
class VaultReceipt:
    operation_id: UUID
    version: int
    kind: str
    state: str
    expires_at_ms: int
    # Internal only; do not serialize this dataclass into a public API.
    envelope: dict | None = field(repr=False)
    delivery_allowed: bool = True


@dataclass(frozen=True)
class ConfirmedCameraOutcome:
    """Trusted adapter evidence, NOT a user request model or raw MQTT parser."""

    scope: CameraCredentialScope
    res_operation_id: UUID
    gauge84_operation_id: UUID
    res_success: bool
    gauge84_success: bool
    credential_mode: Literal["custom", "stock"]


def _aad(
    profile: CameraCredentialProfile, receipt: CameraCredentialReceipt, purpose: str
) -> bytes:
    return _json(
        {
            "namespace": "camera-vault/1",
            "tenant_id": profile.tenant_id,
            "terminal_id": profile.terminal_id,
            "device_id": profile.device_id,
            "sn": profile.sn,
            "camera_id": profile.camera_id,
            "profile_id": str(profile.id),
            "operation_id": str(receipt.operation_id),
            "version": receipt.version,
            "kind": receipt.kind,
            "expires_at_ms": receipt.expires_at_ms,
            "key_id": receipt.key_id,
            "algorithm": receipt.algorithm,
            "purpose": purpose,
        }
    )


def _seal_transport(
    credentials: CameraCredentials,
    recipient: CredentialRecipient,
    operation_id: UUID,
    expires_at_ms: int,
) -> dict:
    key = os.urandom(32)
    result = {
        "v": 1,
        "alg": TRANSPORT_ALGORITHM,
        "sn": recipient.sn,
        "operation_id": str(operation_id),
        "recipient_cert_sha256": recipient.recipient_cert_sha256,
        "expires_at_ms": expires_at_ms,
        "wrapped_key": base64.b64encode(
            recipient.public_key.encrypt(
                key,
                padding.OAEP(
                    mgf=padding.MGF1(hashes.SHA256()),
                    algorithm=hashes.SHA256(),
                    label=b"siplite-camera-credentials/v1",
                ),
            )
        ).decode("ascii"),
    }
    nonce = os.urandom(12)
    raw = json.dumps(
        credentials.internal_plaintext(), separators=(",", ":"), ensure_ascii=False
    ).encode()
    encrypted = AESGCM(key).encrypt(
        nonce, raw, _json({key: result[key] for key in HEADER})
    )
    return dict(
        result,
        nonce=base64.b64encode(nonce).decode(),
        ciphertext=base64.b64encode(encrypted[:-16]).decode(),
        tag=base64.b64encode(encrypted[-16:]).decode(),
    )


class CameraCredentialVault:
    def __init__(self, settings: CameraCredentialVaultSettings | None = None) -> None:
        self._settings = settings or CameraCredentialVaultSettings()

    def _encrypt(
        self,
        profile: CameraCredentialProfile,
        receipt: CameraCredentialReceipt,
        value: dict,
    ) -> None:
        dek = os.urandom(32)
        receipt.nonce, receipt.wrap_nonce = os.urandom(12), os.urandom(12)
        receipt.wrapped_dek = AESGCM(self._settings.key()).encrypt(
            receipt.wrap_nonce, dek, _aad(profile, receipt, "wrap")
        )
        receipt.ciphertext = AESGCM(dek).encrypt(
            receipt.nonce, _json(value), _aad(profile, receipt, "data")
        )

    def _decrypt(
        self, profile: CameraCredentialProfile, receipt: CameraCredentialReceipt
    ) -> dict:
        try:
            if (
                receipt.algorithm != ALGORITHM
                or receipt.key_id != self._settings.kek_id
            ):
                raise ValueError
            dek = AESGCM(self._settings.key()).decrypt(
                receipt.wrap_nonce, receipt.wrapped_dek, _aad(profile, receipt, "wrap")
            )
            value = json.loads(
                AESGCM(dek).decrypt(
                    receipt.nonce, receipt.ciphertext, _aad(profile, receipt, "data")
                )
            )
            if not isinstance(value, dict) or set(value) != {"intent", "envelope"}:
                raise ValueError
            return value
        except ValueError, TypeError, InvalidTag, UnicodeError:
            raise VaultError() from None

    async def _profile(
        self,
        db: AsyncSession,
        scope: CameraCredentialScope,
        *,
        create: bool = False,
        recipient: CredentialRecipient | None = None,
    ) -> CameraCredentialProfile:
        terminal = await db.scalar(
            select(Terminal)
            .where(
                Terminal.id == scope.terminal_id,
                Terminal.org_id == scope.tenant_id,
                Terminal.device_id == scope.device_id,
                Terminal.sn == scope.sn,
            )
            .with_for_update(of=Terminal)
            .execution_options(populate_existing=True)
        )
        if terminal is None:
            raise VaultError("camera_credential_scope_mismatch", 404)
        if recipient is not None:
            try:
                if not terminal.is_active or int(
                    terminal.cert_serial or "0", 16
                ) != int(recipient.cert_serial, 16):
                    raise ValueError
            except ValueError:
                raise VaultError("camera_credential_recipient_changed") from None
        profile = await db.scalar(
            select(CameraCredentialProfile)
            .where(
                CameraCredentialProfile.terminal_id == scope.terminal_id,
                CameraCredentialProfile.camera_id == scope.camera_id,
            )
            .with_for_update()
            .execution_options(populate_existing=True)
        )
        if profile is None:
            if not create:
                raise VaultError("camera_credential_profile_not_found", 404)
            profile = CameraCredentialProfile(
                id=uuid4(),
                tenant_id=scope.tenant_id,
                terminal_id=scope.terminal_id,
                device_id=scope.device_id,
                sn=scope.sn,
                camera_id=scope.camera_id,
                version=0,
            )
            db.add(profile)
            await db.flush()
        if (profile.tenant_id, profile.device_id, profile.sn) != (
            scope.tenant_id,
            scope.device_id,
            scope.sn,
        ):
            raise VaultError("camera_credential_scope_mismatch")
        return profile

    async def _receipt(
        self, db: AsyncSession, profile: CameraCredentialProfile, operation_id: UUID
    ) -> CameraCredentialReceipt:
        receipt = await db.get(
            CameraCredentialReceipt, operation_id, populate_existing=True
        )
        if receipt is None or receipt.profile_id != profile.id:
            raise VaultError("camera_credential_operation_conflict")
        return receipt

    def _delivery(
        self,
        profile: CameraCredentialProfile,
        receipt: CameraCredentialReceipt,
        value: dict,
        recipient: CredentialRecipient,
        now_ms: int,
    ) -> VaultReceipt:
        recipient.require_current(
            device_id=profile.device_id, sn=profile.sn, now_ms=now_ms
        )
        if (
            receipt.state != "pending"
            or profile.pending_operation_id != receipt.operation_id
            or now_ms >= receipt.expires_at_ms
        ):
            raise VaultError("camera_credential_receipt_not_deliverable")
        envelope = value["envelope"]
        if receipt.kind == "replace":
            if not isinstance(envelope, dict) or set(envelope) != {
                *HEADER,
                "nonce",
                "ciphertext",
                "tag",
            }:
                raise VaultError()
            if (
                envelope["recipient_cert_sha256"] != recipient.recipient_cert_sha256
                or envelope["sn"] != profile.sn
                or envelope["operation_id"] != str(receipt.operation_id)
                or envelope["expires_at_ms"] != receipt.expires_at_ms
                or envelope["alg"] != TRANSPORT_ALGORITHM
                or envelope["v"] != 1
            ):
                raise VaultError("camera_credential_recipient_changed")
        return VaultReceipt(
            receipt.operation_id,
            receipt.version,
            receipt.kind,
            receipt.state,
            receipt.expires_at_ms,
            envelope,
        )

    async def prepare_pending(
        self,
        db: AsyncSession,
        *,
        scope: CameraCredentialScope,
        operation_id: UUID,
        credentials: CameraCredentials | None,
        recipient: CredentialRecipient,
        expires_at_ms: int,
        task_deadline_ms: int,
        now_ms: int,
    ) -> VaultReceipt:
        """None credentials prepares a scoped clear, never clears active immediately."""
        recipient.require_current(device_id=scope.device_id, sn=scope.sn, now_ms=now_ms)
        intent = credentials.internal_plaintext() if credentials is not None else None
        if expires_at_ms > int(
            recipient.certificate.not_valid_after_utc.timestamp() * 1000
        ):
            raise VaultError("camera_credential_deadline_invalid")
        profile = await self._profile(db, scope, create=True, recipient=recipient)
        existing = await db.get(CameraCredentialReceipt, operation_id)
        if existing is not None:
            if (
                existing.profile_id != profile.id
                or existing.expires_at_ms != expires_at_ms
            ):
                raise VaultError("camera_credential_operation_conflict")
            value = self._decrypt(profile, existing)
            if not hmac.compare_digest(_json(value["intent"]), _json(intent)):
                raise VaultError("camera_credential_operation_conflict")
            if (
                existing.state != "pending"
                or profile.pending_operation_id != operation_id
                or now_ms >= existing.expires_at_ms
                or existing.expires_at_ms > task_deadline_ms
                or isinstance(value["envelope"], dict)
                and value["envelope"]["recipient_cert_sha256"]
                != recipient.recipient_cert_sha256
            ):
                return VaultReceipt(
                    existing.operation_id,
                    existing.version,
                    existing.kind,
                    existing.state,
                    existing.expires_at_ms,
                    None,
                    False,
                )
            return self._delivery(profile, existing, value, recipient, now_ms)
        if not now_ms < expires_at_ms <= min(now_ms + 60000, task_deadline_ms):
            raise VaultError("camera_credential_deadline_invalid")
        profile.version += 1
        receipt = CameraCredentialReceipt(
            operation_id=operation_id,
            profile_id=profile.id,
            version=profile.version,
            kind="replace" if credentials is not None else "clear",
            state="pending",
            key_id=self._settings.kek_id,
            algorithm=ALGORITHM,
            expires_at_ms=expires_at_ms,
            created_at=datetime.fromtimestamp(now_ms / 1000, UTC),
        )
        envelope = (
            _seal_transport(credentials, recipient, operation_id, expires_at_ms)
            if credentials is not None
            else None
        )
        value = {"intent": intent, "envelope": envelope}
        self._encrypt(profile, receipt, value)
        if profile.pending_operation_id is not None:
            old = await self._receipt(db, profile, profile.pending_operation_id)
            old.state = "superseded"
        profile.pending_operation_id = operation_id
        db.add(receipt)
        try:
            await db.flush()
        except IntegrityError:
            # A global operation UUID may race across different locked terminals.
            # The caller must rollback this transaction before retrying.
            raise VaultError("camera_credential_operation_conflict") from None
        return self._delivery(profile, receipt, value, recipient, now_ms)

    async def original_envelope(
        self,
        db: AsyncSession,
        *,
        scope: CameraCredentialScope,
        operation_id: UUID,
        recipient: CredentialRecipient,
        now_ms: int,
    ) -> VaultReceipt:
        profile = await self._profile(db, scope, recipient=recipient)
        receipt = await self._receipt(db, profile, operation_id)
        return self._delivery(
            profile, receipt, self._decrypt(profile, receipt), recipient, now_ms
        )

    async def confirm(
        self,
        db: AsyncSession,
        *,
        outcome: ConfirmedCameraOutcome,
        now_ms: int,
    ) -> bool:
        profile = await self._profile(db, outcome.scope)
        operation_id = outcome.res_operation_id
        if (
            outcome.gauge84_operation_id != operation_id
            or profile.pending_operation_id != operation_id
        ):
            return False
        receipt = await self._receipt(db, profile, operation_id)
        self._decrypt(
            profile, receipt
        )  # authenticate metadata/ciphertext before any state change
        if receipt.state != "pending":
            return False
        if (
            not outcome.res_success
            or not outcome.gauge84_success
            or now_ms >= receipt.expires_at_ms
        ):
            receipt.state = "failed"
            profile.pending_operation_id = None
            await db.flush()
            return False
        if outcome.credential_mode != (
            "custom" if receipt.kind == "replace" else "stock"
        ):
            return False
        if profile.active_operation_id is not None:
            old = await self._receipt(db, profile, profile.active_operation_id)
            old.state = "superseded" if receipt.kind == "replace" else "cleared"
        profile.active_operation_id = (
            operation_id if receipt.kind == "replace" else None
        )
        profile.pending_operation_id = None
        receipt.state = "active" if receipt.kind == "replace" else "cleared"
        await db.flush()
        return True

    async def fail_pending(
        self, db: AsyncSession, *, scope: CameraCredentialScope, operation_id: UUID
    ) -> bool:
        """Cancel/failed/expired adapter path; active credentials remain intact."""
        profile = await self._profile(db, scope)
        if profile.pending_operation_id != operation_id:
            return False
        receipt = await self._receipt(db, profile, operation_id)
        self._decrypt(profile, receipt)
        receipt.state = "failed"
        profile.pending_operation_id = None
        await db.flush()
        return True

    async def load_active_internal(
        self, db: AsyncSession, *, scope: CameraCredentialScope
    ) -> CameraCredentials:
        """Only for a NEW manual delivery operation; never expose via public GET."""
        profile = await self._profile(db, scope)
        if profile.active_operation_id is None:
            raise VaultError("camera_credential_active_not_found", 404)
        receipt = await self._receipt(db, profile, profile.active_operation_id)
        if receipt.state != "active" or receipt.kind != "replace":
            raise VaultError()
        value = self._decrypt(profile, receipt)["intent"]
        if not isinstance(value, dict) or set(value) != {"username", "password"}:
            raise VaultError()
        credentials = CameraCredentials(
            SecretStr(value["username"]), SecretStr(value["password"])
        )
        credentials.internal_plaintext()
        return credentials

    async def load_for_redelivery_internal(
        self,
        db: AsyncSession,
        *,
        scope: CameraCredentialScope,
        source_operation_id: UUID,
    ) -> CameraCredentials:
        """Explicit NEW manual operation only; recover current pending/latest failed too.

        The original expired/rotated envelope is never returned by this method.
        Cleared/superseded historical profiles cannot be recovered through it.
        """
        profile = await self._profile(db, scope)
        receipt = await self._receipt(db, profile, source_operation_id)
        allowed = (
            receipt.state == "active"
            and profile.active_operation_id == source_operation_id
            or receipt.state == "pending"
            and profile.pending_operation_id == source_operation_id
            or receipt.state == "failed"
            and receipt.version == profile.version
        )
        if receipt.kind != "replace" or not allowed:
            raise VaultError("camera_credential_redelivery_source_unavailable")
        value = self._decrypt(profile, receipt)["intent"]
        if not isinstance(value, dict) or set(value) != {"username", "password"}:
            raise VaultError()
        credentials = CameraCredentials(
            SecretStr(value["username"]), SecretStr(value["password"])
        )
        credentials.internal_plaintext()
        return credentials

    async def safe_metadata(
        self, db: AsyncSession, *, scope: CameraCredentialScope
    ) -> dict[str, object]:
        profile = await self._profile(db, scope)
        return {
            "version": profile.version,
            "has_active": profile.active_operation_id is not None,
            "active_operation_id": profile.active_operation_id,
            "pending_operation_id": profile.pending_operation_id,
        }
