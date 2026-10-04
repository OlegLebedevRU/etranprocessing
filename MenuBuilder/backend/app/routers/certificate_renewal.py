"""Native renewal ordering. PB owns PIN; IoT owns task_id; no creation outbox."""

import asyncio
import logging
from datetime import UTC, datetime
from typing import Any
from uuid import UUID

import httpx
from fastapi import APIRouter, Depends, HTTPException, Response
from pydantic import AwareDatetime, BaseModel, ConfigDict, Field
from sqlalchemy import select
from sqlalchemy.exc import SQLAlchemyError
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import resolve_org_id
from app.database import get_db
from app.models import CertificatePin, Terminal, TerminalCertDiscovery
from app.models_l4desk import L4DeskAuditEvent
from app.routers.video import _verify_device_access
from app.security.permissions import require_readonly_guard, require_tenant_admin
from app.services.iot_client import iot_client
from app.services.subscriptions import check_terminal
from app.services.terminal_onboarding_service import ProcessingBackendPinClient

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/devices", tags=["certificate-renewal"])


class RenewalOrderRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")
    pin_id: int | None = Field(default=None, gt=0)


class ProviderPin(BaseModel):
    pin_id: int
    tenant_id: int
    terminal_id: int
    sn: str
    pin: str = Field(pattern=r"^[0-9]{6}$", repr=False)
    expires_at: AwareDatetime


class RenewalOrderResponse(BaseModel):
    task_id: UUID
    pin_id: int
    expires_at: AwareDatetime
    status: str = "queued"


async def owned_terminal(device_id: int, user: dict, db: AsyncSession):
    terminal = await _verify_device_access(device_id, user, db)
    if terminal.org_id != resolve_org_id(user):
        raise HTTPException(403, "Терминал вне активной организации")
    if not terminal.sn:
        raise HTTPException(409, "Серийный номер отсутствует")
    return terminal


async def admission(terminal: Terminal, db: AsyncSession) -> str | None:
    if not terminal.is_active:
        return "Терминал отключён администратором"
    subscription = await check_terminal(db, terminal.org_id, terminal)
    if subscription is not None and not subscription.allowed:
        return subscription.reason
    if (
        not terminal.cert_serial
        or not 20 < len(terminal.cert_serial) <= 40
        or any(c not in "0123456789abcdefABCDEF" for c in terminal.cert_serial)
    ):
        return "Требуется действующий сертификат нового CA"
    if (
        not terminal.cert_not_valid_after
        or terminal.cert_not_valid_after <= datetime.now(UTC)
    ):
        return "Сертификат истёк: используйте обычную выдачу PIN"
    return None


def queue_body(device_id: int, pin: ProviderPin, now: datetime) -> dict:
    remaining = (pin.expires_at - now).total_seconds()
    ttl = min(44639, int((remaining - 5) // 60))
    if remaining <= 120 or ttl < 1:
        raise HTTPException(410, "Срок PIN недостаточен для продления")
    return {
        "ext_task_id": f"certificate-renewal:{pin.pin_id}",
        "device_id": device_id,
        "method_code": 7011,
        "priority": 0,
        "ttl": ttl,
        "payload": {
            "dt": [
                {
                    "pin": pin.pin,
                    "pin_expires_at": int(pin.expires_at.timestamp()),
                    "ttl_sec": 120,
                }
            ]
        },
    }


@router.post(
    "/{device_id}/certificate-renewal",
    response_model=RenewalOrderResponse,
    dependencies=[Depends(require_readonly_guard)],
)
async def order_renewal(
    device_id: int,
    body: RenewalOrderRequest,
    response: Response,
    user: dict = Depends(require_tenant_admin),
    db: AsyncSession = Depends(get_db),
):
    response.headers["Cache-Control"] = "no-store"
    terminal = await owned_terminal(device_id, user, db)
    reason = await admission(terminal, db)
    if reason:
        raise HTTPException(403, reason)
    if not iot_client.base_url or not iot_client.service_token:
        raise HTTPException(503, "Сервис очереди недоступен")
    provider = ProcessingBackendPinClient()
    if not provider.base_url or not provider.service_token:
        raise HTTPException(503, "Сервис PIN недоступен")
    pin = None
    try:
        async with asyncio.timeout(25):
            # This lookup establishes SN/tenant binding and administrative block; offline is accepted.
            device = await iot_client.get_console_device(device_id, terminal.org_id)
            if not device or device.get("sn") != terminal.sn:
                raise HTTPException(409, "Несовпадение устройства IoT")
            if (device.get("connection") or {}).get("is_blocked") is True:
                raise HTTPException(403, "Устройство заблокировано в IoT")
            async with httpx.AsyncClient(timeout=10) as client:
                issued = await client.post(
                    f"{provider.base_url}/api/certificates/renewal-pins",
                    headers=provider._headers(),
                    json={
                        "tenant_id": terminal.org_id,
                        "terminal_id": terminal.id,
                        "sn": terminal.sn,
                        "pin_id": body.pin_id,
                    },
                )
                if issued.status_code not in (200, 201):
                    raise HTTPException(
                        issued.status_code
                        if issued.status_code in (403, 409, 410)
                        else 503,
                        "Выпуск PIN продления недоступен",
                    )
                pin = ProviderPin.model_validate(issued.json())
                if (pin.tenant_id, pin.terminal_id, pin.sn) != (
                    terminal.org_id,
                    terminal.id,
                    terminal.sn,
                ):
                    pin = None
                    raise HTTPException(502, "Несовпадение ответа сервиса PIN")
                queued = await client.post(
                    f"{iot_client.base_url}/api/internal/v1/device-tasks/",
                    headers=iot_client._get_headers(org_id=terminal.org_id),
                    params={"org_id": terminal.org_id},
                    json=queue_body(device_id, pin, datetime.now(UTC)),
                )
                if queued.status_code not in (200, 201):
                    raise RuntimeError("queue rejected")
                task_id = UUID(str(queued.json()["id"]))
            db.add(
                L4DeskAuditEvent(
                    tenant_id=terminal.org_id,
                    actor=str(
                        user.get("user_id") or user.get("sub") or user.get("username")
                    ),
                    event_type="terminal.certificate_renewal_queued",
                    subject_type="terminal",
                    subject_id=str(terminal.id),
                    operation_id=str(task_id),
                    correlation_id=str(task_id),
                    outcome="queued",
                    details={
                        "task_id": str(task_id),
                        "pin_id": pin.pin_id,
                        "previous_serial": terminal.cert_serial,
                    },
                )
            )
            try:
                await db.commit()
            except SQLAlchemyError:
                await db.rollback()
                logger.warning(
                    "Renewal queued but audit persistence failed device=%s task=%s",
                    device_id,
                    task_id,
                )
            return RenewalOrderResponse(
                task_id=task_id, pin_id=pin.pin_id, expires_at=pin.expires_at
            )
    except HTTPException:
        raise
    except (httpx.HTTPError, ValueError, KeyError, RuntimeError, TimeoutError) as exc:
        logger.warning(
            "Renewal order unconfirmed device=%s failure=%s",
            device_id,
            type(exc).__name__,
        )
        detail: dict[str, Any] = {
            "code": "queue_unconfirmed",
            "message": "Постановка не подтверждена. Повторите вручную.",
        }
        if pin is not None:
            detail.update(pin_id=pin.pin_id, expires_at=pin.expires_at.isoformat())
        raise HTTPException(502, detail) from exc


@router.get("/{device_id}/certificate-renewal")
async def renewal_status(
    device_id: int,
    response: Response,
    user: dict = Depends(require_tenant_admin),
    db: AsyncSession = Depends(get_db),
):
    response.headers["Cache-Control"] = "no-store"
    terminal = await owned_terminal(device_id, user, db)
    reason = await admission(terminal, db)
    latest = await db.scalar(
        select(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal.id,
            CertificatePin.org_id == terminal.org_id,
            CertificatePin.purpose == "renew",
        )
        .order_by(CertificatePin.id.desc())
        .limit(1)
    )
    observed = None
    if latest is not None and latest.status == "used" and latest.used_at is not None:
        observed = await db.scalar(
            select(TerminalCertDiscovery.last_seen_at)
            .where(
                TerminalCertDiscovery.terminal_id == terminal.id,
                TerminalCertDiscovery.sn == terminal.sn,
                TerminalCertDiscovery.cert_serial == terminal.cert_serial,
                TerminalCertDiscovery.is_valid.is_(True),
                TerminalCertDiscovery.last_seen_at >= latest.used_at,
            )
            .order_by(TerminalCertDiscovery.last_seen_at.desc())
            .limit(1)
        )
    state = (
        "none"
        if latest is None
        else "confirmed"
        if observed
        else "issued_waiting_identity"
        if latest.status == "used"
        else "expired"
        if latest.expires_at <= datetime.now(UTC)
        else latest.status
    )
    task_id = None
    task_status = None
    result_code = None
    if latest is not None:
        task_id = await db.scalar(
            select(L4DeskAuditEvent.correlation_id)
            .where(
                L4DeskAuditEvent.tenant_id == terminal.org_id,
                L4DeskAuditEvent.subject_id == str(terminal.id),
                L4DeskAuditEvent.event_type == "terminal.certificate_renewal_queued",
                L4DeskAuditEvent.details["pin_id"].as_integer() == latest.id,
            )
            .order_by(L4DeskAuditEvent.id.desc())
            .limit(1)
        )
    if task_id and iot_client.base_url and iot_client.service_token:
        try:
            async with httpx.AsyncClient(timeout=3) as client:
                task = await client.get(
                    f"{iot_client.base_url}/api/internal/v1/device-tasks/{UUID(task_id)}",
                    params={"org_id": terminal.org_id},
                    headers=iot_client._get_headers(org_id=terminal.org_id),
                )
                task.raise_for_status()
                data = task.json()
                if (data.get("header") or {}).get("device_id") == device_id:
                    task_status = data.get("status")
                    results = data.get("results") or []
                    result_code = results[0].get("status_code") if results else None
                    if latest is not None and latest.status == "pending":
                        if task_status == 4:
                            state = "expired"
                        elif task_status == 5:
                            state = "cancelled"
                        elif result_code is not None and result_code != 200:
                            state = "failed"
        except httpx.HTTPError, ValueError, TypeError, KeyError:
            logger.info(
                "Renewal task status temporarily unavailable device=%s", device_id
            )
    return {
        "task_id": task_id,
        "task_status": task_status,
        "result_code": result_code,
        "allowed": reason is None,
        "reason": reason,
        "status": state,
        "observed_at": observed,
        "expires_at": latest.expires_at if latest else None,
        "cert_not_valid_after": terminal.cert_not_valid_after,
    }
