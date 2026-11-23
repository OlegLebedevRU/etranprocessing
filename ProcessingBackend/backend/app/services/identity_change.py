"""PIN-directed identity change; no work is added to successful terminal auth."""

from collections import OrderedDict
from datetime import UTC, datetime
from time import monotonic
from uuid import UUID

import httpx
from etranprocessing_db.models.device_events import (
    DeviceAdminEvent,
    TerminalIdentityChange,
)
from etranprocessing_db.models.finance import FinTerminalMonthlyCharge, FinUsageDaily
from fastapi import HTTPException
from sqlalchemy import delete, func, select, text, update
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.logging_config import cert_logger
from app.models import (
    CertificatePin,
    L4DeskRemoteSession,
    L4DeskTerminal,
    License,
    Org,
    Terminal,
    TerminalMenuBinding,
)

# Failed authentications only. Bounded across arbitrary rejected serials; negative
# entries expire so a later identity operation can still produce its incident.
_rejected_samples: OrderedDict[tuple[int, str], float] = OrderedDict()


def normalized_serial(value: str | None) -> str:
    return (value or "").upper().lstrip("0") or "0"


async def pending_change(
    db: AsyncSession, terminal: Terminal
) -> TerminalIdentityChange | None:
    change = (
        await db.execute(
            select(TerminalIdentityChange)
            .where(
                TerminalIdentityChange.terminal_id == terminal.id,
                TerminalIdentityChange.status.in_(["pending", "sync_pending"]),
            )
            .execution_options(populate_existing=True)
        )
    ).scalar_one_or_none()
    if change is not None and change.status == "sync_pending":
        raise HTTPException(409, "identity_iot_sync_required")
    if change is None or change.status != "pending":
        return None
    if terminal.org_id != change.source_org_id:
        raise HTTPException(409, "identity_source_changed")
    locked_orgs = {}
    for org_id in sorted((change.source_org_id, change.target_org_id)):
        locked_orgs[org_id] = await db.get(Org, org_id, with_for_update=True)
    target = locked_orgs[change.target_org_id]
    source = locked_orgs[change.source_org_id]
    if (
        source is None
        or target is None
        or not target.is_active
        or source.site_mode != "classic"
        or target.site_mode != "classic"
        or terminal.l4desk_subscription_enabled
    ):
        raise HTTPException(409, "identity_target_not_available")
    if not settings.identity_change_iot_url or not settings.identity_change_iot_key:
        raise HTTPException(503, "identity_iot_not_configured")
    return change


async def add_incident(
    db: AsyncSession, change: TerminalIdentityChange, event_type: str, message: str
) -> None:
    inserted = await db.scalar(
        insert(DeviceAdminEvent)
        .values(
            event_key=f"{change.operation_id}:{event_type}",
            terminal_id=change.terminal_id,
            operation_id=change.operation_id,
            event_type=event_type,
            status="new",
            details={
                "source_org_id": change.source_org_id,
                "target_org_id": change.target_org_id,
                "message": message,
            },
        )
        .on_conflict_do_nothing(index_elements=[DeviceAdminEvent.event_key])
        .returning(DeviceAdminEvent.id)
    )
    if inserted is not None:
        await db.execute(select(func.pg_notify("portal_views_v1", "admin-events")))


async def record_old_certificate(
    db: AsyncSession, terminal: Terminal, serial: str
) -> None:
    key = (terminal.id, normalized_serial(serial))
    now = monotonic()
    if _rejected_samples.get(key, 0) > now:
        return
    change = await db.scalar(
        select(TerminalIdentityChange)
        .where(
            TerminalIdentityChange.terminal_id == terminal.id,
            TerminalIdentityChange.old_serial == key[1],
            TerminalIdentityChange.status.in_(["sync_pending", "completed"]),
        )
        .order_by(TerminalIdentityChange.issued_at.desc())
        .limit(1)
    )
    if (
        change
        and change.status in ("sync_pending", "completed")
        and normalized_serial(change.old_serial) == key[1]
        and normalized_serial(change.new_serial) != key[1]
    ):
        await add_incident(
            db,
            change,
            "identity_old_certificate",
            "После смены тенанта устройство обратилось с прежним сертификатом; требуется установка нового сертификата.",
        )
        await db.commit()
    _rejected_samples[key] = now + 300
    _rejected_samples.move_to_end(key)
    while len(_rejected_samples) > 2048:
        _rejected_samples.popitem(last=False)


async def archive_technical_identity(
    db: AsyncSession, technical: L4DeskTerminal, change: TerminalIdentityChange
) -> None:
    """Preserve tenant-composite FKs without changing historical tenant or money.

    Negative IDs are reserved for archived technical records. Allocation is
    serialized only among identity changes; successful terminal traffic is untouched.
    Source and target org locks are already held by the enrollment transaction.
    """
    if technical.tenant_id != change.source_org_id:
        raise HTTPException(409, "identity_technical_owner_mismatch")
    if db.get_bind().dialect.name == "postgresql":
        await db.execute(
            text("SELECT pg_advisory_xact_lock(:key)"), {"key": 0x4944454E54495459}
        )
    minimum = await db.scalar(select(func.min(L4DeskTerminal.terminal_id)))
    archive_id = min(minimum or 0, 0) - 1
    if archive_id < -2147483648:
        raise HTTPException(409, "identity_archive_id_exhausted")
    source_ordinal = await db.scalar(
        select(func.coalesce(func.max(L4DeskTerminal.ordinal), 0) + 1).where(
            L4DeskTerminal.tenant_id == change.source_org_id
        )
    )
    target_ordinal = await db.scalar(
        select(func.coalesce(func.max(L4DeskTerminal.ordinal), 0) + 1).where(
            L4DeskTerminal.tenant_id == change.target_org_id
        )
    )
    if source_ordinal is None or target_ordinal is None:
        raise HTTPException(409, "identity_technical_ordinal_unavailable")
    values = {
        column.name: getattr(technical, column.name)
        for column in L4DeskTerminal.__table__.columns
    }
    values.update(
        terminal_id=archive_id,
        runtime_terminal_id=None,
        ordinal=source_ordinal,
        external_terminal_id=f"identity-archive:{change.operation_id}",
        certificate_reference=change.old_serial,
        deleted_at=datetime.now(UTC),
    )
    # Preserve the source provisioning operation on its archived identity; free
    # the global operation key before inserting that snapshot.
    technical.operation_id = f"identity-current:{change.operation_id}"
    await db.flush()
    db.add(L4DeskTerminal(**values))
    await db.flush()
    for model in (L4DeskRemoteSession, FinUsageDaily, FinTerminalMonthlyCharge):
        await db.execute(
            update(model)
            .where(
                model.terminal_id == technical.terminal_id,
                model.tenant_id == change.source_org_id,
            )
            .values(terminal_id=archive_id)
        )
    change.archived_technical_terminal_id = archive_id
    technical.tenant_id = change.target_org_id
    technical.ordinal = target_ordinal
    technical.external_terminal_id = str(technical.terminal_id)
    technical.correlation_id = str(change.operation_id)
    technical.paid_until = None
    technical.user_disabled_at = None
    technical.first_online_at = None
    technical.last_online_at = None
    technical.certificate_reference = change.new_serial
    technical.pin_state = "consumed"
    technical.provisioning_state = "pending"


async def apply_issued_identity(
    db: AsyncSession,
    terminal: Terminal,
    change: TerminalIdentityChange,
    pin: CertificatePin,
    csr_hash: str,
    response: str,
) -> None:
    # Called under Terminal + PIN locks, in the certificate issuance transaction.
    change.old_serial = change.old_serial or terminal.cert_serial
    change.new_serial = terminal.cert_serial
    change.pin_id = pin.id
    change.csr_sha256 = csr_hash
    change.certificate_response = response
    change.issued_at = datetime.now(UTC)
    change.status = "sync_pending"
    await add_incident(
        db,
        change,
        "identity_iot_sync_failed",
        "Требуется завершить передачу в IoT. При ошибке повторите синхронизацию.",
    )
    terminal.org_id = change.target_org_id
    terminal.credential_recipient_pem = None
    technical = await db.scalar(
        select(L4DeskTerminal)
        .where(L4DeskTerminal.runtime_terminal_id == terminal.id)
        .with_for_update()
    )
    if technical is not None:
        await archive_technical_identity(db, technical, change)
    await db.execute(
        delete(TerminalMenuBinding).where(
            TerminalMenuBinding.device_id == terminal.device_id
        )
    )
    # Preserve paid time; old ledgers/payment orgs are immutable. Tenant-specific
    # balance and tariff override must not cross the ownership boundary.
    await db.execute(
        update(License)
        .where(License.terminal_id == terminal.id)
        .values(
            org_id=change.target_org_id, balance=0, monthly_price_override_minor=None
        )
    )
    await db.execute(
        update(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal.id,
            CertificatePin.purpose == "setup",
            CertificatePin.status == "pending",
        )
        .values(status="cancelled")
    )


async def sync_iot(db: AsyncSession, terminal_id: int, operation_id: UUID) -> bool:
    terminal = await db.scalar(
        select(Terminal)
        .where(Terminal.id == terminal_id)
        .with_for_update(of=Terminal)
        .execution_options(populate_existing=True)
    )
    change = await db.get(TerminalIdentityChange, operation_id, populate_existing=True)
    if terminal is None or change is None or change.terminal_id != terminal_id:
        raise HTTPException(404, "identity_change_not_found")
    if change.status == "completed":
        return True
    if change.status != "sync_pending" or terminal.org_id != change.target_org_id:
        raise HTTPException(409, "identity_change_not_issued")
    try:
        if not settings.identity_change_iot_url or not settings.identity_change_iot_key:
            raise ValueError("identity_iot_not_configured")
        async with httpx.AsyncClient(timeout=10) as client:
            result = await client.post(
                settings.identity_change_iot_url.rstrip("/")
                + "/api/internal/v1/provisioning/identity-transfers",
                headers={"X-Internal-Service-Key": settings.identity_change_iot_key},
                json={
                    "operation_id": str(change.operation_id),
                    "terminal_id": terminal.id,
                    "device_id": terminal.device_id,
                    "sn": terminal.sn,
                    "source_org_id": change.source_org_id,
                    "target_org_id": change.target_org_id,
                },
            )
            result.raise_for_status()
            payload = result.json()
            if (
                not isinstance(payload, dict)
                or payload.get("operation_id") != str(change.operation_id)
                or payload.get("org_id") != change.target_org_id
                or payload.get("sn") != terminal.sn
                or payload.get("status") != "completed"
            ):
                raise ValueError("identity_iot_response_mismatch")
    except (httpx.HTTPError, ValueError) as exc:
        # Persist an actionable incident, never raw provider bodies or credentials.
        error_code = "iot_invalid_response"
        status_code = None
        if isinstance(exc, httpx.HTTPStatusError):
            status_code = exc.response.status_code
            error_code = f"iot_http_{status_code}"
            allowed_codes = {
                "identity_device_not_found",
                "identity_target_not_found",
                "identity_operation_mismatch",
                "identity_source_mismatch",
                "identity_provisioning_mismatch",
                "identity_lease_revoke_failed",
                "identity_service_auth_required",
                "identity_service_auth_not_configured",
            }
            try:
                body = exc.response.json()
                if isinstance(body, dict) and body.get("detail") in allowed_codes:
                    error_code = body["detail"]
            except ValueError, TypeError:
                # Raw/unstructured provider bodies are deliberately not persisted.
                error_code = f"iot_http_{status_code}"
        elif isinstance(exc, httpx.TimeoutException):
            error_code = "iot_timeout"
        elif isinstance(exc, httpx.RequestError):
            error_code = "iot_unavailable"
        elif str(exc) in {
            "identity_iot_not_configured",
            "identity_iot_response_mismatch",
        }:
            error_code = str(exc)
        cert_logger.warning(
            "Identity IoT sync failed: terminal=%s operation=%s",
            terminal.id,
            change.operation_id,
        )
        await add_incident(
            db,
            change,
            "identity_iot_sync_failed",
            "Не завершена передача в IoT. Проверьте активные сессии и повторите синхронизацию.",
        )
        await db.execute(
            update(DeviceAdminEvent)
            .where(
                DeviceAdminEvent.event_key
                == f"{change.operation_id}:identity_iot_sync_failed"
            )
            .values(
                details={
                    "source_org_id": change.source_org_id,
                    "target_org_id": change.target_org_id,
                    "message": "Не завершена передача в IoT. Повторите синхронизацию после устранения причины.",
                    "error_code": error_code,
                    "http_status": status_code,
                },
                updated_at=datetime.now(UTC),
            )
        )
        await db.commit()
        return False
    await db.execute(
        update(DeviceAdminEvent)
        .where(
            DeviceAdminEvent.event_key
            == f"{change.operation_id}:identity_iot_sync_failed",
            DeviceAdminEvent.status != "closed",
        )
        .values(
            status="closed",
            updated_by="processing-backend",
            updated_at=datetime.now(UTC),
            comment="Синхронизация с IoT подтверждена.",
        )
    )
    if change.archived_technical_terminal_id is not None:
        await db.execute(
            update(L4DeskRemoteSession)
            .where(
                L4DeskRemoteSession.terminal_id
                == change.archived_technical_terminal_id,
                L4DeskRemoteSession.tenant_id == change.source_org_id,
                L4DeskRemoteSession.state.in_(
                    ["reserved", "start_requested", "active", "stop_requested"]
                ),
            )
            .values(
                state="closed", closed_at=datetime.now(UTC), reason="identity_transfer"
            )
        )
        await db.execute(
            update(L4DeskTerminal)
            .where(
                L4DeskTerminal.runtime_terminal_id == terminal.id,
                L4DeskTerminal.tenant_id == change.target_org_id,
            )
            .values(provisioning_state="ready")
        )
    change.status = "completed"
    change.completed_at = datetime.now(UTC)
    terminal.iot_provisioned = True
    terminal.iot_last_sync_at = datetime.now(UTC)
    await db.commit()
    return True
