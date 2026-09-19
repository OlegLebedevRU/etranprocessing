from __future__ import annotations

import contextlib
import logging
import secrets
import uuid
from datetime import UTC, datetime
from typing import Any, Literal

import httpx
from fastapi import HTTPException, status
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import Terminal
from app.models_l4desk import L4DeskTerminal
from app.repositories.l4desk_repository import L4DeskRepository

logger = logging.getLogger(__name__)


# =============================================================================
# Schemas
# =============================================================================


class TerminalReadiness(BaseModel):
    """Four readiness indicators for L4Desk terminal enrollment."""

    model_config = ConfigDict(extra="ignore")

    record: Literal["ready", "pending", "failed"] = Field(
        default="ready",
        description="Local database business terminal persistence state",
    )
    certificate: Literal["pending", "issued", "consumed", "expired", "failed"] = Field(
        default="pending",
        description="ProcessingBackend certificate PIN lifecycle state",
    )
    iot: Literal["pending", "ready", "failed"] = Field(
        default="pending",
        description="Leo4 IoT platform device provisioning state",
    )
    online: Literal["online", "offline"] = Field(
        default="offline",
        description="Real-time terminal network presence",
    )


class TerminalOnboardRequest(BaseModel):
    model_config = ConfigDict(extra="ignore")

    sn: str | None = Field(
        default=None,
        max_length=100,
        description="Optional terminal serial number. Auto-generated if omitted.",
    )
    address: str | None = Field(default=None, max_length=500)
    note: str | None = Field(default=None, max_length=500)
    timezone: str | None = Field(default="Europe/Moscow", max_length=64)
    operation_id: str | None = Field(
        default=None,
        max_length=128,
        description="Durable saga idempotency key (UUID)",
    )
    correlation_id: str | None = Field(
        default=None,
        max_length=128,
        description="Distributed correlation ID",
    )


class TerminalOnboardResponse(BaseModel):
    model_config = ConfigDict(extra="ignore")

    terminal_id: int
    tenant_id: int
    ordinal: int
    sn: str
    device_id: int | None = None
    is_free: bool
    operation_id: str
    correlation_id: str
    readiness: TerminalReadiness
    pin: str | None = Field(
        default=None,
        description="Plaintext 6-digit PIN. Returned on initial issuance and active replay; None once consumed or expired.",
    )
    pin_masked: str | None = Field(
        default=None,
        description="Masked PIN (e.g. '***773') safe for logging and display",
    )
    pin_expires_at: datetime | None = None
    agent_release_url: str
    agent_version: str
    created_at: datetime
    last_error: str | None = None


# =============================================================================
# Provider Contracts Schemas
# =============================================================================


class DeviceProvisionRequest(BaseModel):
    """Payload for IoT Platform provisioning (H-L4D-06B-IOT-v1)."""

    model_config = ConfigDict(extra="ignore")

    operation_id: str
    contract_version: str = "1.0.0"
    tenant_id: int
    terminal_id: int
    sn: str
    device_id: int | None = None
    correlation_id: str | None = None
    requested_by_user_id: str | None = None
    metadata: dict[str, Any] | None = None


class DeviceProvisionResponse(BaseModel):
    """Response from IoT Platform provisioning (H-L4D-06B-IOT-v1)."""

    model_config = ConfigDict(extra="ignore")

    operation_id: str
    status: Literal["requested", "provisioned", "failed"]
    tenant_id: int
    terminal_id: int
    device_id: int
    sn: str
    contract_version: str = "1.0.0"
    correlation_id: str | None = None
    replayed_flag: bool = False
    created_at: str | datetime
    provisioned_at: str | datetime | None = None
    error_code: str | None = None
    error_message: str | None = None


class IssueCertificatePinRequest(BaseModel):
    """Payload for ProcessingBackend PIN issuance (H-L4D-06A-PB-v1)."""

    model_config = ConfigDict(extra="ignore")

    operation_id: str
    correlation_id: str | None = None
    tenant_id: int
    terminal_id: int
    sn: str
    ttl_seconds: int = 86400
    actor: str | None = "menubuilder"


class IssueCertificatePinResponse(BaseModel):
    """Response from ProcessingBackend PIN issuance (H-L4D-06A-PB-v1)."""

    model_config = ConfigDict(extra="ignore")

    operation_id: str
    correlation_id: str | None = None
    tenant_id: int
    terminal_id: int
    sn: str
    pin: str | None = None
    pin_masked: str
    status: Literal["issued", "consumed", "expired"]
    expires_at: datetime
    created_at: datetime
    replayed: bool = False


# =============================================================================
# Provider HTTP Clients
# =============================================================================


class IotProvisioningClient:
    """HTTP client for Leo4 IoT platform device provisioning (H-L4D-06B-IOT-v1)."""

    def __init__(
        self,
        base_url: str | None = None,
        service_token: str | None = None,
        timeout: float = 10.0,
    ) -> None:
        self.base_url = (
            base_url if base_url is not None else settings.internal_api_base_url
        ).rstrip("/")
        self.service_token = (
            service_token
            if service_token is not None
            else settings.internal_service_key_value
        )
        self.timeout = timeout

    def _headers(self, tenant_id: int | None = None) -> dict[str, str]:
        headers = {"Content-Type": "application/json"}
        if self.service_token:
            headers["X-Internal-Key"] = self.service_token
            headers["X-Internal-Service-Key"] = self.service_token
            headers["Authorization"] = f"Bearer {self.service_token}"
        if tenant_id is not None:
            headers["X-Org-Id"] = str(tenant_id)
        return headers

    async def provision_device(
        self, req: DeviceProvisionRequest
    ) -> DeviceProvisionResponse:
        url = f"{self.base_url}/api/internal/v1/devices/provision"
        payload = req.model_dump(exclude_none=True)
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(
                url, json=payload, headers=self._headers(req.tenant_id)
            )
            if resp.status_code in (status.HTTP_201_CREATED, status.HTTP_200_OK):
                return DeviceProvisionResponse.model_validate(resp.json())
            if resp.status_code == status.HTTP_409_CONFLICT:
                data = resp.json().get("detail", {})
                err_code = (
                    data.get("error_code")
                    if isinstance(data, dict)
                    else "OPERATION_ID_CONFLICT"
                )
                err_msg = (
                    data.get("message")
                    if isinstance(data, dict)
                    else "Provision conflict"
                )
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=f"{err_code}: {err_msg}",
                )
            error_text = resp.text[:200]
            raise RuntimeError(
                f"IoT provisioning returned HTTP {resp.status_code}: {error_text}"
            )

    async def get_by_operation(
        self, operation_id: str, tenant_id: int | None = None
    ) -> DeviceProvisionResponse | None:
        url = f"{self.base_url}/api/internal/v1/devices/provision/by-operation/{operation_id}"
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(url, headers=self._headers(tenant_id))
            if resp.status_code == status.HTTP_200_OK:
                return DeviceProvisionResponse.model_validate(resp.json())
            if resp.status_code == status.HTTP_404_NOT_FOUND:
                return None
            raise RuntimeError(f"IoT get_by_operation returned HTTP {resp.status_code}")


class ProcessingBackendPinClient:
    """HTTP client for ProcessingBackend certificate PIN issuance (H-L4D-06A-PB-v1)."""

    def __init__(
        self,
        base_url: str | None = None,
        service_token: str | None = None,
        timeout: float = 10.0,
    ) -> None:
        self.base_url = (
            base_url
            if base_url is not None
            else settings.processing_backend_effective_url
        ).rstrip("/")
        self.service_token = (
            service_token
            if service_token is not None
            else settings.processing_backend_effective_token
        )
        self.timeout = timeout

    def _headers(self) -> dict[str, str]:
        headers = {"Content-Type": "application/json"}
        if self.service_token:
            headers["X-Internal-Service-Key"] = self.service_token
            headers["X-Service-Token"] = self.service_token
            headers["Authorization"] = f"Bearer {self.service_token}"
        return headers

    async def issue_pin(
        self, req: IssueCertificatePinRequest
    ) -> IssueCertificatePinResponse:
        url = f"{self.base_url}/api/certificates/pins/issue"
        payload = req.model_dump(exclude_none=True)
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(url, json=payload, headers=self._headers())
            if resp.status_code in (status.HTTP_201_CREATED, status.HTTP_200_OK):
                return IssueCertificatePinResponse.model_validate(resp.json())
            if resp.status_code == status.HTTP_409_CONFLICT:
                data = resp.json().get("detail", {})
                err_code = (
                    data.get("error_code")
                    if isinstance(data, dict)
                    else "OPERATION_ID_CONFLICT"
                )
                err_msg = (
                    data.get("message")
                    if isinstance(data, dict)
                    else "PIN operation conflict"
                )
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=f"{err_code}: {err_msg}",
                )
            error_text = resp.text[:200]
            raise RuntimeError(
                f"ProcessingBackend PIN issue returned HTTP {resp.status_code}: {error_text}"
            )

    async def get_by_operation(
        self, operation_id: str
    ) -> IssueCertificatePinResponse | None:
        url = f"{self.base_url}/api/certificates/pins/by-operation/{operation_id}"
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(url, headers=self._headers())
            if resp.status_code == status.HTTP_200_OK:
                return IssueCertificatePinResponse.model_validate(resp.json())
            if resp.status_code == status.HTTP_404_NOT_FOUND:
                return None
            raise RuntimeError(
                f"ProcessingBackend get_by_operation returned HTTP {resp.status_code}"
            )


# =============================================================================
# Service Implementation
# =============================================================================


class TerminalOnboardingService:
    """Orchestrates L4Desk terminal business lifecycle, monotonic ordering, and outbox saga."""

    def __init__(
        self,
        db: AsyncSession,
        iot_client: IotProvisioningClient | None = None,
        pin_client: ProcessingBackendPinClient | None = None,
    ) -> None:
        self.db = db
        self.repo = L4DeskRepository(db)
        self.iot_client = iot_client or IotProvisioningClient()
        self.pin_client = pin_client or ProcessingBackendPinClient()

    async def onboard_terminal(
        self,
        *,
        user: dict[str, Any],
        req: TerminalOnboardRequest,
        target_org_id: int | None = None,
    ) -> TerminalOnboardResponse:
        """Onboard a new terminal in a single local transaction, then execute outbox saga."""
        if not settings.is_terminal_onboarding_enabled:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="L4Desk terminal onboarding is currently disabled by feature flag",
            )

        is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)
        tenant_id = int(user.get("org_id", 0))
        if is_su and target_org_id is not None and target_org_id > 0:
            tenant_id = target_org_id

        if tenant_id <= 0:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Пользователь не привязан к организации",
            )

        operation_id = req.operation_id or str(uuid.uuid4())
        correlation_id = req.correlation_id or f"corr-onboard-{uuid.uuid4()}"
        actor = str(user.get("username") or user.get("sub") or f"user_{user.get('id')}")

        # ---------------------------------------------------------------------
        # 1. Idempotency Check on operation_id
        # ---------------------------------------------------------------------
        existing_l4 = await self.repo.get_terminal_by_operation_id(operation_id)
        if existing_l4:
            if existing_l4.tenant_id != tenant_id:
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=f"OPERATION_ID_CONFLICT: Operation ID '{operation_id}' belongs to another tenant",
                )
            # Replay existing terminal
            return await self._build_onboard_response(
                existing_l4,
                actor=actor,
                plain_pin=None,
            )

        # ---------------------------------------------------------------------
        # 2. Serial Number Resolution & Uniqueness
        # ---------------------------------------------------------------------
        next_ordinal = await self.repo.get_next_ordinal_for_tenant(tenant_id)
        raw_sn = (req.sn or "").strip()
        if raw_sn:
            # Check for conflict in active terminals
            existing_by_sn = await self.repo.get_terminal_by_sn(raw_sn)
            if existing_by_sn:
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=f"SN_ALREADY_EXISTS: Terminal with SN '{raw_sn}' already exists",
                )
            sn = raw_sn
        else:
            # Generate deterministic, clean hardware serial number
            rand_suffix = secrets.token_hex(2).upper()
            sn = f"SN-L4D-{tenant_id}-{next_ordinal:04d}-{rand_suffix}"

        # ---------------------------------------------------------------------
        # 3. Local DB Transaction: Create Terminal + L4DeskTerminal + Audit
        # ---------------------------------------------------------------------
        # Determine next device_id for runtime Terminal model
        dev_id_res = await self.db.execute(
            select(func.coalesce(func.max(Terminal.device_id), 0) + 1)
        )
        next_device_id = int(dev_id_res.scalar_one())

        runtime_terminal = Terminal(
            device_id=next_device_id,
            sn=sn,
            org_id=tenant_id,
            address=req.address.strip() if req.address else None,
            note=req.note.strip() if req.note else None,
            timezone=req.timezone.strip() if req.timezone else "Europe/Moscow",
            is_active=True,
            terminal_type_id=0,
        )
        self.db.add(runtime_terminal)
        await self.db.flush()

        terminal_id = runtime_terminal.id

        l4_terminal = L4DeskTerminal(
            terminal_id=terminal_id,
            tenant_id=tenant_id,
            runtime_terminal_id=terminal_id,
            ordinal=next_ordinal,
            sn=sn,
            external_terminal_id=f"term_{terminal_id}",
            device_id=next_device_id,
            operation_id=operation_id,
            correlation_id=correlation_id,
            provisioning_state="pending",
            pin_state="pending",
        )
        self.db.add(l4_terminal)

        # Audit Event for creation (CRITICAL: NEVER save plain PIN)
        await self.repo.record_audit_event(
            actor=actor,
            event_type="terminal.created",
            subject_type="terminal",
            subject_id=str(terminal_id),
            correlation_id=correlation_id,
            tenant_id=tenant_id,
            operation_id=operation_id,
            outcome="success",
            details={
                "ordinal": next_ordinal,
                "sn": sn,
                "device_id": next_device_id,
            },
        )

        await self.db.commit()
        await self.db.refresh(l4_terminal)

        # ---------------------------------------------------------------------
        # 4. Post-Commit Saga: IoT Provisioning & PIN Issuance
        # Partial failures do not delete the committed terminal record.
        # ---------------------------------------------------------------------
        plain_pin, _ = await self._run_saga_steps(
            l4_terminal=l4_terminal,
            actor=actor,
            correlation_id=correlation_id,
        )

        await self.db.commit()
        await self.db.refresh(l4_terminal)

        return await self._build_onboard_response(
            l4_terminal,
            actor=actor,
            plain_pin=plain_pin,
        )

    async def retry_terminal_saga(
        self,
        *,
        user: dict[str, Any],
        terminal_id: int,
    ) -> TerminalOnboardResponse:
        """Retry any pending or failed saga steps for an existing terminal."""
        is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)
        tenant_id = int(user.get("org_id", 0))

        l4_terminal = await self.repo.get_terminal(terminal_id)
        if not l4_terminal or l4_terminal.deleted_at is not None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Terminal {terminal_id} not found or deleted",
            )

        if not is_su and l4_terminal.tenant_id != tenant_id:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Access denied: Terminal belongs to another tenant",
            )

        actor = str(user.get("username") or user.get("sub") or f"user_{user.get('id')}")

        plain_pin, _ = await self._run_saga_steps(
            l4_terminal=l4_terminal,
            actor=actor,
            correlation_id=l4_terminal.correlation_id,
        )

        await self.db.commit()
        await self.db.refresh(l4_terminal)

        return await self._build_onboard_response(
            l4_terminal,
            actor=actor,
            plain_pin=plain_pin,
        )

    async def delete_terminal(
        self,
        *,
        user: dict[str, Any],
        terminal_id: int,
    ) -> dict[str, Any]:
        """Soft-delete terminal and automatically transfer free quota to next earliest terminal."""
        is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)
        tenant_id = int(user.get("org_id", 0))

        l4_terminal = await self.repo.get_terminal(terminal_id)
        if not l4_terminal or l4_terminal.deleted_at is not None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Terminal {terminal_id} not found or already deleted",
            )

        if not is_su and l4_terminal.tenant_id != tenant_id:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Access denied: Terminal belongs to another tenant",
            )

        # Soft delete L4DeskTerminal
        l4_terminal.deleted_at = datetime.now(UTC)

        # Deactivate runtime terminal
        if l4_terminal.runtime_terminal_id:
            runtime_term = await self.db.get(Terminal, l4_terminal.runtime_terminal_id)
            if runtime_term:
                runtime_term.is_active = False

        actor = str(user.get("username") or user.get("sub") or f"user_{user.get('id')}")
        await self.repo.record_audit_event(
            actor=actor,
            event_type="terminal.deleted",
            subject_type="terminal",
            subject_id=str(terminal_id),
            correlation_id=l4_terminal.correlation_id,
            tenant_id=l4_terminal.tenant_id,
            operation_id=l4_terminal.operation_id,
            outcome="success",
            details={"ordinal": l4_terminal.ordinal, "sn": l4_terminal.sn},
        )

        await self.db.commit()

        # Find new earliest active terminal
        new_earliest_id = await self.repo.get_earliest_active_terminal_id(
            l4_terminal.tenant_id
        )

        return {
            "ok": True,
            "message": f"Terminal {l4_terminal.sn} deleted successfully",
            "earliest_free_terminal_id": new_earliest_id,
        }

    async def get_terminal_readiness(
        self,
        *,
        user: dict[str, Any],
        terminal_id: int,
    ) -> TerminalOnboardResponse:
        """Fetch exact 4 readiness states and enrollment metadata for a terminal."""
        is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)
        tenant_id = int(user.get("org_id", 0))

        l4_terminal = await self.repo.get_terminal(terminal_id)
        if not l4_terminal:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Terminal {terminal_id} not found",
            )

        if not is_su and l4_terminal.tenant_id != tenant_id:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Access denied: Terminal belongs to another tenant",
            )

        actor = str(user.get("username") or user.get("sub") or f"user_{user.get('id')}")
        return await self._build_onboard_response(
            l4_terminal,
            actor=actor,
            plain_pin=None,
        )

    # -------------------------------------------------------------------------
    # Internal Saga Orchestration & Response Construction
    # -------------------------------------------------------------------------

    async def _run_saga_steps(
        self,
        *,
        l4_terminal: L4DeskTerminal,
        actor: str,
        correlation_id: str,
    ) -> tuple[str | None, str | None]:
        """Execute or retry IoT provisioning and Certificate PIN issuance.

        Returns (plain_pin, last_error).
        """
        plain_pin: str | None = None
        errors: list[str] = []

        # ---------------------------------------------------------------------
        # Step A: IoT Device Provisioning (if not already ready)
        # ---------------------------------------------------------------------
        if l4_terminal.provisioning_state != "ready":
            try:
                prov_req = DeviceProvisionRequest(
                    operation_id=l4_terminal.operation_id,
                    tenant_id=l4_terminal.tenant_id,
                    terminal_id=l4_terminal.terminal_id,
                    sn=l4_terminal.sn,
                    device_id=None,
                    correlation_id=correlation_id,
                    requested_by_user_id=actor,
                )
                prov_res = await self.iot_client.provision_device(prov_req)
                l4_terminal.provisioning_state = "ready"
                if prov_res.device_id and prov_res.device_id != l4_terminal.device_id:
                    l4_terminal.device_id = prov_res.device_id
                    if l4_terminal.runtime_terminal_id:
                        rt = await self.db.get(
                            Terminal, l4_terminal.runtime_terminal_id
                        )
                        if rt:
                            rt.device_id = prov_res.device_id

                await self.repo.record_audit_event(
                    actor=actor,
                    event_type="terminal.provisioned",
                    subject_type="terminal",
                    subject_id=str(l4_terminal.terminal_id),
                    correlation_id=correlation_id,
                    tenant_id=l4_terminal.tenant_id,
                    operation_id=l4_terminal.operation_id,
                    outcome="success",
                    details={"device_id": l4_terminal.device_id},
                )
            except Exception as exc:  # noqa: BLE001
                err_str = f"IoT provisioning failed: {type(exc).__name__}: {exc}"
                logger.warning(err_str)
                errors.append(err_str)
                l4_terminal.provisioning_state = "failed"
                l4_terminal.last_error = err_str[:490]
                with contextlib.suppress(Exception):
                    await self.repo.record_audit_event(
                        actor=actor,
                        event_type="terminal.provisioned",
                        subject_type="terminal",
                        subject_id=str(l4_terminal.terminal_id),
                        correlation_id=correlation_id,
                        tenant_id=l4_terminal.tenant_id,
                        operation_id=l4_terminal.operation_id,
                        outcome="failed",
                        details={"error": str(exc)[:200]},
                    )

        # ---------------------------------------------------------------------
        # Step B: Certificate PIN Issuance (if not already issued or consumed)
        # ---------------------------------------------------------------------
        if l4_terminal.pin_state not in ("issued", "consumed"):
            try:
                pin_req = IssueCertificatePinRequest(
                    operation_id=l4_terminal.operation_id,
                    correlation_id=correlation_id,
                    tenant_id=l4_terminal.tenant_id,
                    terminal_id=l4_terminal.terminal_id,
                    sn=l4_terminal.sn,
                    ttl_seconds=86400,
                    actor=actor,
                )
                pin_res = await self.pin_client.issue_pin(pin_req)
                l4_terminal.pin_state = pin_res.status
                l4_terminal.certificate_reference = pin_res.pin_masked
                plain_pin = pin_res.pin

                # CRITICAL: Record audit event with masked PIN only
                await self.repo.record_audit_event(
                    actor=actor,
                    event_type="terminal.pin_issued",
                    subject_type="terminal",
                    subject_id=str(l4_terminal.terminal_id),
                    correlation_id=correlation_id,
                    tenant_id=l4_terminal.tenant_id,
                    operation_id=l4_terminal.operation_id,
                    outcome="success",
                    details={
                        "pin_masked": pin_res.pin_masked,
                        "status": pin_res.status,
                    },
                )
            except Exception as exc:  # noqa: BLE001
                err_str = f"PIN issuance failed: {type(exc).__name__}: {exc}"
                logger.warning(err_str)
                errors.append(err_str)
                l4_terminal.pin_state = "failed"
                l4_terminal.last_error = err_str[:490]
                with contextlib.suppress(Exception):
                    await self.repo.record_audit_event(
                        actor=actor,
                        event_type="terminal.pin_issued",
                        subject_type="terminal",
                        subject_id=str(l4_terminal.terminal_id),
                        correlation_id=correlation_id,
                        tenant_id=l4_terminal.tenant_id,
                        operation_id=l4_terminal.operation_id,
                        outcome="failed",
                        details={"error": str(exc)[:200]},
                    )
        elif l4_terminal.pin_state == "issued" and plain_pin is None:
            # Replay lookup of active PIN via provider query endpoint
            with contextlib.suppress(Exception):
                q_res = await self.pin_client.get_by_operation(l4_terminal.operation_id)
                if q_res and q_res.status == "issued":
                    plain_pin = q_res.pin

        last_error = "; ".join(errors) if errors else None
        if not errors:
            l4_terminal.last_error = None

        return plain_pin, last_error

    async def _build_onboard_response(
        self,
        l4_terminal: L4DeskTerminal,
        actor: str,
        plain_pin: str | None,
    ) -> TerminalOnboardResponse:
        # Check free marker
        earliest_active_id = await self.repo.get_earliest_active_terminal_id(
            l4_terminal.tenant_id
        )
        is_free = (
            l4_terminal.terminal_id == earliest_active_id
            if l4_terminal.deleted_at is None
            else False
        )

        # Check online presence
        is_online = bool(
            l4_terminal.last_online_at is not None
            and (datetime.now(UTC) - l4_terminal.last_online_at).total_seconds() < 300
        )
        if not is_online and l4_terminal.runtime_terminal_id:
            rt = await self.db.get(Terminal, l4_terminal.runtime_terminal_id)
            if rt and rt.iot_is_online:
                is_online = True

        # Build readiness model
        rec_state: Literal["ready", "pending", "failed"] = (
            "ready" if l4_terminal.terminal_id else "pending"
        )
        cert_state: Literal["pending", "issued", "consumed", "expired", "failed"] = (
            l4_terminal.pin_state
            if l4_terminal.pin_state
            in ("pending", "issued", "consumed", "expired", "failed")
            else "pending"
        )
        iot_state: Literal["pending", "ready", "failed"] = (
            l4_terminal.provisioning_state
            if l4_terminal.provisioning_state in ("pending", "ready", "failed")
            else "pending"
        )
        online_state: Literal["online", "offline"] = (
            "online" if is_online else "offline"
        )

        readiness = TerminalReadiness(
            record=rec_state,
            certificate=cert_state,
            iot=iot_state,
            online=online_state,
        )

        # Provider PIN semantics: if pin_state is consumed or expired, never return plain PIN
        effective_pin = plain_pin if cert_state == "issued" else None

        return TerminalOnboardResponse(
            terminal_id=l4_terminal.terminal_id,
            tenant_id=l4_terminal.tenant_id,
            ordinal=l4_terminal.ordinal,
            sn=l4_terminal.sn,
            device_id=l4_terminal.device_id,
            is_free=is_free,
            operation_id=l4_terminal.operation_id,
            correlation_id=l4_terminal.correlation_id,
            readiness=readiness,
            pin=effective_pin,
            pin_masked=l4_terminal.certificate_reference,
            pin_expires_at=None,
            agent_release_url=settings.agent_release_url,
            agent_version=settings.agent_release_version,
            created_at=l4_terminal.created_at or datetime.now(UTC),
            last_error=l4_terminal.last_error,
        )
