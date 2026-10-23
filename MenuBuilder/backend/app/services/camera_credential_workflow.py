"""Authorized BFF adapter: encrypted durable intent before opaque RPC166 dispatch."""

from __future__ import annotations

import json
import re
import time
from dataclasses import dataclass, field
from typing import Any, NoReturn
from uuid import UUID

from etranprocessing_db.camera_credentials import CameraCredentialReceipt
from fastapi import HTTPException
from pydantic import SecretStr
from sqlalchemy import select

from app.models_l4desk import L4DeskAuditEvent
from app.services.camera_contract import CAMERA_REVISION, validate_camera_rpc
from app.services.camera_credential_recipient import (
    RecipientUnavailable,
    fetch_credential_recipient,
)
from app.services.camera_credential_vault import (
    CameraCredentials,
    CameraCredentialScope,
    CameraCredentialVault,
    ConfirmedCameraOutcome,
    VaultError,
)
from app.services.camera_platform import camera_request, camera_snapshot


@dataclass(frozen=True)
class CredentialChange:
    action: str
    operation_id: UUID
    expected_revision: str
    expires_at_ms: int
    credentials: CameraCredentials | None = field(default=None, repr=False)
    source_operation_id: UUID | None = None

    def safe_details(self):
        return {
            "action": self.action,
            "expected_revision": self.expected_revision,
            "expires_at_ms": self.expires_at_ms,
            "source_operation_id": str(self.source_operation_id)
            if self.source_operation_id
            else None,
        }


def parse_credential_change(raw: bytes | bytearray) -> CredentialChange:
    """Flat bounded write-only input. Never delegate raw values to validation errors."""

    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError
            value[key] = item
        return value

    try:
        if len(raw) > 4096:
            raise ValueError
        value = json.loads(raw, object_pairs_hook=unique)
        if not isinstance(value, dict) or value.get("allow_disruptive") is not True:
            raise ValueError
        action = value["action"]
        keys = {
            "action",
            "operation_id",
            "expected_revision",
            "expires_at_ms",
            "allow_disruptive",
        }
        if action == "replace":
            keys.update(("username", "password"))
        elif action == "redeliver":
            keys.add("source_operation_id")
        elif action != "clear":
            raise ValueError
        if set(value) != keys or type(value["expires_at_ms"]) is not int:
            raise ValueError
        if not isinstance(value["expected_revision"], str) or not re.fullmatch(
            "[0-9a-f]{64}", value["expected_revision"]
        ):
            raise ValueError
        operation = UUID(value["operation_id"])
        credentials = None
        if action == "replace":
            if type(value["username"]) is not str or type(value["password"]) is not str:
                raise ValueError
            credentials = CameraCredentials(
                SecretStr(value["username"]), SecretStr(value["password"])
            )
            credentials.internal_plaintext()
        source = UUID(value["source_operation_id"]) if action == "redeliver" else None
        if source == operation:
            raise ValueError
        return CredentialChange(
            action,
            operation,
            value["expected_revision"],
            value["expires_at_ms"],
            credentials,
            source,
        )
    except ValueError, TypeError, KeyError, AttributeError, RecursionError:
        raise HTTPException(
            422, detail={"code": "invalid_camera_credentials"}
        ) from None


def scope_for(terminal) -> CameraCredentialScope:
    return CameraCredentialScope(
        terminal.org_id, terminal.id, terminal.device_id, terminal.sn, "cam:ip000001"
    )


def unavailable(error: ValueError) -> NoReturn:
    status = error.status_code if isinstance(error, VaultError) else 409
    code = (
        error.code
        if isinstance(error, VaultError)
        else "camera_credentials_unavailable"
    )
    raise HTTPException(status, detail={"code": code}) from None


async def dispatch_credentials(
    db, *, terminal, user, change: CredentialChange, camera: dict
) -> dict:
    """Caller must enforce membership/operator, idle lease and fresh camera166 capability."""
    vault = CameraCredentialVault()
    scope = scope_for(terminal)
    now = int(time.time() * 1000)
    try:
        recipient = await fetch_credential_recipient(
            tenant_id=terminal.org_id, device_id=terminal.device_id, sn=terminal.sn
        )
        credentials = change.credentials
        if change.action == "redeliver":
            if change.source_operation_id is None:
                raise VaultError("camera_credential_redelivery_source_unavailable")
            credentials = await vault.load_for_redelivery_internal(
                db, scope=scope, source_operation_id=change.source_operation_id
            )
        receipt = await vault.prepare_pending(
            db,
            scope=scope,
            operation_id=change.operation_id,
            credentials=credentials,
            recipient=recipient,
            expires_at_ms=change.expires_at_ms,
            task_deadline_ms=change.expires_at_ms,
            now_ms=now,
        )
        # prepare_pending holds the terminal/profile lock. Bind the non-secret
        # command context in the existing audit table within that same transaction.
        audit = await db.scalar(
            select(L4DeskAuditEvent).where(
                L4DeskAuditEvent.tenant_id == terminal.org_id,
                L4DeskAuditEvent.subject_type == "terminal",
                L4DeskAuditEvent.subject_id == str(terminal.id),
                L4DeskAuditEvent.operation_id == str(change.operation_id),
                L4DeskAuditEvent.event_type == "camera.credentials.intent",
            )
        )
        details = change.safe_details()
        if audit is not None and audit.details != details:
            raise VaultError("camera_credential_operation_conflict")
        if audit is None:
            db.add(
                L4DeskAuditEvent(
                    tenant_id=terminal.org_id,
                    actor=str(user.get("sub") or user.get("user_id") or "operator"),
                    event_type="camera.credentials.intent",
                    subject_type="terminal",
                    subject_id=str(terminal.id),
                    operation_id=str(change.operation_id),
                    correlation_id=str(change.operation_id),
                    outcome="pending",
                    details=details,
                )
            )
        await (
            db.commit()
        )  # Release DB locks before network I/O. Only ciphertext was persisted.
    except (VaultError, RecipientUnavailable, ValueError) as error:
        await db.rollback()
        unavailable(error)
    public = {
        "id": str(change.operation_id),
        "state": receipt.state,
        "version": receipt.version,
        "expires_at_ms": receipt.expires_at_ms,
    }
    if not receipt.delivery_allowed:
        return public
    # A retry observes the existing task first; it never produces a new envelope.
    try:
        existing = await camera_request(
            "GET", f"device-tasks/{change.operation_id}", terminal.org_id, user
        )
    except HTTPException as error:
        if error.status_code != 404:
            raise
        existing = None
    if existing is not None:
        header = existing.get("header") or {}
        if (
            header.get("device_id") != terminal.device_id
            or header.get("method_code") != 166
        ):
            raise HTTPException(
                409, detail={"code": "camera_credential_operation_conflict"}
            )
        return public
    try:
        if camera.get("revision") != change.expected_revision:
            raise HTTPException(409, detail={"code": "camera_revision_changed"})
        if int(time.time() * 1000) >= receipt.expires_at_ms:
            raise HTTPException(
                409, detail={"code": "camera_credential_deadline_expired"}
            )
        item: dict[str, Any] = {
            "camera_id": scope.camera_id,
            "operation_id": str(change.operation_id),
            "expected_revision": change.expected_revision,
            "timeout_ms": 30000,
            "action": "clear_credentials"
            if change.action == "clear"
            else "set_credentials",
        }
        if receipt.envelope is not None:
            item["sealed_credentials"] = receipt.envelope
        payload = validate_camera_rpc(166, {"dt": [item]}, str(change.operation_id))
        result = await camera_request(
            "POST",
            "device-tasks/",
            terminal.org_id,
            user,
            body={
                "ext_task_id": str(change.operation_id),
                "device_id": terminal.device_id,
                "method_code": 166,
                "ttl": 1,
                "camera_contract_revision": CAMERA_REVISION,
                "payload": payload,
            },
        )
        if str(result.get("id")) != str(change.operation_id):
            raise HTTPException(
                502, detail={"code": "camera_credential_dispatch_unconfirmed"}
            )
    except HTTPException as error:
        if 400 <= error.status_code < 500:
            await vault.fail_pending(db, scope=scope, operation_id=change.operation_id)
            await db.commit()
        raise
    return public


async def reconcile_credentials(
    db, *, terminal, user, operation_id: UUID, task: dict
) -> None:
    """Promote/clear only from independently correlated, server-read RES and gauge84."""
    receipt = await db.get(CameraCredentialReceipt, operation_id)
    if receipt is None:
        return
    header = task.get("header") or {}
    if (
        header.get("device_id") != terminal.device_id
        or header.get("method_code") != 166
    ):
        return
    vault = CameraCredentialVault()
    scope = scope_for(terminal)
    results = task.get("results") or []
    try:
        if not results:
            if task.get("status") in (4, 5, 6, 7):
                await vault.fail_pending(db, scope=scope, operation_id=operation_id)
                await db.commit()
            return
        result = results[0]
        body = result.get("result")
        if isinstance(body, str):
            body = json.loads(body)
        if (
            not isinstance(body, dict)
            or body.get("operation_id") != str(operation_id)
            or not body.get("snapshot_id")
        ):
            return
        gauges = await camera_request(
            "GET",
            "gauges/",
            terminal.org_id,
            user,
            params={"device_id": terminal.device_id, "type": "84"},
        )
        snapshot = camera_snapshot(gauges, terminal.device_id) or {}
        if (
            snapshot.get("operation_id") != str(operation_id)
            or snapshot.get("snapshot_id") != body["snapshot_id"]
        ):
            return
        camera = next(
            (
                item
                for item in snapshot.get("cameras", [])
                if item.get("camera_id") == scope.camera_id
            ),
            {},
        )
        expected_mode = "native" if receipt.kind == "clear" else "custom"
        outcome = ConfirmedCameraOutcome(
            scope,
            operation_id,
            UUID(snapshot["operation_id"]),
            result.get("status_code") == 200,
            camera.get("camera_contract_revision") == CAMERA_REVISION
            and camera.get("auth_source") == expected_mode
            and not camera.get("stale"),
            "stock" if receipt.kind == "clear" else "custom",
        )
        await vault.confirm(db, outcome=outcome, now_ms=int(time.time() * 1000))
        await db.commit()
    except VaultError, ValueError, TypeError, KeyError:
        await db.rollback()
        raise HTTPException(
            409, detail={"code": "camera_credential_confirmation_unavailable"}
        ) from None
