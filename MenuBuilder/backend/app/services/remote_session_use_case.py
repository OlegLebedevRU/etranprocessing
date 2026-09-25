from __future__ import annotations

import contextlib
import logging
import uuid
from typing import Any

from etranprocessing_db.models import Terminal
from fastapi import HTTPException, status
from pydantic import BaseModel
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import resolve_org_id
from app.config import settings
from app.repositories.l4desk_repository import L4DeskRepository
from app.routers.video import (
    get_device_ports,
    get_or_create_mountpoint_pin,
    set_mountpoint_stream_instance,
)
from app.security.permissions import PERMISSION_VIDEO_VIEW
from app.services.iot_client import IotPlatformClient, iot_client
from app.services.iot_event_feed_client import (
    IotEventFeedClient,
    IotEventFeedContractError,
    IotEventFeedError,
    iot_event_feed_client,
)
from app.services.media_orchestrator_client import (
    MediaOrchestratorClient,
    MediaSessionConflictError,
    media_orchestrator_client,
)
from app.services.remote_session_policy import (
    RemoteSessionPolicy,
    get_remote_session_policy,
)
from app.services.remote_session_stop import (
    RemoteSessionStopPending,
    confirm_remote_session_stop,
)

logger = logging.getLogger(__name__)


class RemoteSessionResponse(BaseModel):
    session_id: str
    local_session_id: int
    terminal_id: int
    sn: str
    session_type: str
    state: str
    mountpoint_id: int | None = None
    janus_ws: str | None = None
    pin: str | None = None
    lease_id: str | None = None
    ws_path: str | None = None
    ttl_sec: int = 600
    stream_instance_id: str | None = None


class RemoteSessionStatusResponse(BaseModel):
    active: bool
    session_id: str | None = None
    local_session_id: int | None = None
    terminal_id: int
    sn: str
    session_type: str | None = None
    state: str | None = None
    lease_id: str | None = None
    media_state: str | None = None
    streaming: bool = False
    transport_connected: bool | None = None
    fresh_rtp: bool | None = None
    rtp_packets: int = 0
    bytes: int = 0
    idle_sec: float | None = None


class RemoteSessionUseCase:
    """Unified Remote Session Use Case for both legacy MenuBuilder users and L4Desk profile.

    Orchestrates:
    - Tenant boundary and role-based access verification
    - RemoteSessionPolicy evaluation seam (legacy permissive + disabled L4Desk policy flag)
    - Local DB session reservation (l4desk_remote_sessions)
    - Mutual exclusion enforcement (IoT session lock, reject 409 session_busy, no auto-switch)
    - IoT session adapter & control lease synchronization
    - Media orchestration (l4media-ingress on-demand start/health/stop)
    - Compensating stop / rollback on partial failures
    """

    def __init__(
        self,
        db: AsyncSession | Any,
        policy: RemoteSessionPolicy | None = None,
        iot_adapter: IotEventFeedClient | None = None,
        iot_control: IotPlatformClient | None = None,
        media_orchestrator: MediaOrchestratorClient | None = None,
    ) -> None:
        self.db = db
        self.policy = policy
        self.iot_adapter = iot_adapter or iot_event_feed_client
        self.iot_control = iot_control or iot_client
        self.media_orchestrator = media_orchestrator or media_orchestrator_client
        self.repo = L4DeskRepository(db)

    async def _verify_terminal_access(
        self,
        device_id: int,
        user: dict[str, Any],
        session_type: str,
    ) -> Terminal:
        stmt = select(Terminal).where(Terminal.device_id == device_id)
        res = await self.db.execute(stmt)
        terminal = res.scalar_one_or_none()
        if not terminal:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Терминал с ID {device_id} не найден",
            )

        role_id = int(user.get("role_id", 3))
        is_su = bool(
            user.get("is_superuser")
            or user.get("role") in ("superuser", "admin")
            or role_id == 1
        )
        user_org_id = resolve_org_id(user)

        # Tenant isolation
        if not is_su and terminal.org_id != user_org_id:
            logger.warning(
                "Tenant isolation violation: user %s (tenant %s) attempted access to terminal %d (tenant %s)",
                user.get("sub"),
                user_org_id,
                terminal.id,
                terminal.org_id,
            )
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Доступ к терминалу другой организации запрещён",
            )

        # Role matrix for session_type
        if session_type == "console":
            if role_id == 4:
                raise HTTPException(
                    status_code=status.HTTP_403_FORBIDDEN,
                    detail="Доступ к консоли запрещён для роли наблюдателя",
                )
            # Allowed for superusers (role 1) and L4Desk users (role 5 in own tenant)
            if not is_su and role_id != 5:
                user_perms = user.get("permissions") or []
                if "console" not in user_perms and "*" not in user_perms:
                    raise HTTPException(
                        status_code=status.HTTP_403_FORBIDDEN,
                        detail="Доступ к консоли разрешён только администраторам и пользователям L4Desk",
                    )
        elif session_type == "video":
            if role_id == 4:
                user_perms = user.get("permissions") or []
                if PERMISSION_VIDEO_VIEW not in user_perms and "*" not in user_perms:
                    raise HTTPException(
                        status_code=status.HTTP_403_FORBIDDEN,
                        detail="Доступ к видеопотоку не предоставлен",
                    )
        else:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail=f"Недопустимый тип удалённой сессии: {session_type}",
            )

        return terminal

    async def start_session(
        self,
        *,
        device_id: int,
        session_type: str,
        user: dict[str, Any],
        operation_id: str | None = None,
        correlation_id: str | None = None,
        mode: str = "desktop",
        source_id: str = "0",
        profile: str = "480p",
        start_terminal_stream: bool = True,
    ) -> RemoteSessionResponse:
        """Start or replay a remote session (console or video)."""
        terminal = await self._verify_terminal_access(device_id, user, session_type)

        effective_policy = self.policy or get_remote_session_policy(user)
        decision = await effective_policy.evaluate_session_request(
            tenant_id=terminal.org_id,
            terminal=terminal,
            session_type=session_type,
            user=user,
            db=self.db,
        )
        if not decision.allowed:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail={
                    "code": decision.error_code or "policy_denied",
                    "message": decision.reason
                    or "Сессия отклонена политикой лицензирования/тарификации",
                },
            )

        op_id = operation_id or f"op-mb-{uuid.uuid4()}"
        corr_id = correlation_id or f"corr-mb-{uuid.uuid4()}"

        # 1. Local mutual exclusion check
        existing = await self.repo.get_active_session_by_terminal_id(terminal.id)
        if existing is not None:
            if existing.state == "stop_requested":
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail={"code": "session_stop_pending"},
                )
            # Deterministic replay if same operation_id
            if existing.operation_id == op_id:
                logger.info(
                    "Deterministic replay of session %s (operation %s)",
                    existing.provider_session_id,
                    op_id,
                )
                mountpoint_id = (
                    terminal.id if existing.session_type == "video" else None
                )
                pin = (
                    get_or_create_mountpoint_pin(device_id)
                    if existing.session_type == "video"
                    else None
                )
                return RemoteSessionResponse(
                    session_id=existing.provider_session_id or str(existing.id),
                    local_session_id=existing.id,
                    terminal_id=terminal.id,
                    sn=terminal.sn,
                    session_type=existing.session_type,
                    state=existing.state,
                    mountpoint_id=mountpoint_id,
                    janus_ws="/janus-ws" if existing.session_type == "video" else None,
                    pin=pin,
                    lease_id=None,
                    ws_path=None,
                    ttl_sec=settings.remote_session_watchdog_ttl_sec,
                )

            # Cross-session conflict between console and video: auto-switch forbidden
            if existing.session_type != session_type:
                logger.warning(
                    "Conflict: terminal %s has active %s session (state=%s). Rejecting %s start.",
                    terminal.sn,
                    existing.session_type,
                    existing.state,
                    session_type,
                )
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail={
                        "code": "session_busy",
                        "message": (
                            f"Терминал {terminal.sn} занят активной сессией "
                            f"({existing.session_type}). Автоматическое переключение "
                            f"между консолью и видео запрещено."
                        ),
                    },
                )

            # Same session type (e.g. video -> video): gracefully stop stale prior session
            logger.info(
                "Terminal %s has prior %s session %s. Gracefully stopping before starting new session.",
                terminal.sn,
                existing.session_type,
                existing.provider_session_id,
            )
            await self.stop_session(
                device_id=device_id,
                reason="superseded_by_new_session",
                user=user,
            )

        # Ensure L4DeskTerminal record exists for foreign key constraint
        await self.repo.ensure_l4desk_terminal(
            terminal_id=terminal.id,
            tenant_id=terminal.org_id,
            sn=terminal.sn,
            correlation_id=corr_id,
        )

        user_db_id = None
        with contextlib.suppress(Exception):
            user_db_id = (
                int(user["id"])
                if "id" in user
                else int(user["user_id"])
                if "user_id" in user
                else None
            )

        # Create local reservation
        local_session = await self.repo.create_remote_session(
            tenant_id=terminal.org_id,
            terminal_id=terminal.id,
            operation_id=op_id,
            correlation_id=corr_id,
            session_type=session_type,
            requested_by_user_id=user_db_id,
            state="reserved",
        )
        await self.db.flush()

        # 2. IoT Session Adapter: acquire technical session lock
        provider_session_id: str | None = None
        try:
            iot_res = await self.iot_adapter.create_remote_session(
                operation_id=op_id,
                sn=terminal.sn,
                session_type=session_type,
                tenant_id=terminal.org_id,
                terminal_id=str(terminal.id),
                requested_by_user_id=str(user.get("sub") or user.get("username")),
                correlation_id=corr_id,
                session_metadata={
                    "mode": mode,
                    "profile": profile,
                }
                if session_type == "video"
                else {},
            )
            raw_session_id = iot_res.get("session_id")
            if not isinstance(raw_session_id, str) or not raw_session_id:
                raise IotEventFeedContractError(
                    "IoT create response omitted the exact session_id"
                )
            provider_session_id = raw_session_id
        except IotEventFeedContractError as contract_err:
            local_session.state = "stop_requested"
            local_session.reason = "provider_identity_missing"
            await self.db.flush()
            await self.db.commit()
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail={"code": "provider_identity_missing"},
            ) from contract_err
        except IotEventFeedError as iot_err:
            local_session.state = "failed"
            local_session.closed_at = func.now()
            if iot_err.status_code == 409 or "busy" in str(iot_err).lower():
                local_session.reason = "session_busy"
                await self.db.flush()
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail={
                        "code": "session_busy",
                        "message": f"Терминал {terminal.sn} занят другой сессией на уровне IoT платформы.",
                    },
                ) from iot_err
            local_session.reason = f"iot_error_{iot_err.status_code}"
            await self.db.flush()
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Ошибка взаимодействия с IoT платформой: {iot_err.message}",
            ) from iot_err
        except Exception as exc:
            local_session.state = "failed"
            local_session.reason = f"iot_transport_error: {exc}"
            local_session.closed_at = func.now()
            await self.db.flush()
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Сетевая ошибка обращения к IoT платформе: {exc}",
            ) from exc

        local_session.provider_session_id = provider_session_id
        local_session.state = "start_requested"
        await self.db.flush()
        await self.db.commit()

        async def compensate_start_failure(reason: str) -> None:
            local_session.state = "stop_requested"
            local_session.reason = reason
            await self.db.flush()
            await self.db.commit()
            try:
                await confirm_remote_session_stop(
                    session_id=provider_session_id,
                    session_type=session_type,
                    tenant_id=terminal.org_id,
                    sn=terminal.sn,
                    operation_id=f"stop-mb-{local_session.id}",
                    reason=reason,
                    correlation_id=corr_id,
                    iot_adapter=self.iot_adapter,
                    media_orchestrator=self.media_orchestrator,
                )
            except RemoteSessionStopPending:
                return
            local_session.state = "closed"
            local_session.closed_at = func.now()
            await self.db.flush()
            await self.db.commit()

        # Synchronize control lease for terminal for WebSocket/control compatibility
        role_id = int(user.get("role_id", 3))
        lease_scope = (
            "console"
            if session_type == "console"
            else ("view" if role_id == 4 else "stream")
        )
        custom_user = dict(user)
        custom_user["session_id"] = provider_session_id
        lease_id: str | None = None

        # Check if user already holds a lease — reuse it instead of acquiring a new one
        existing_lease_id: str | None = None
        with contextlib.suppress(Exception):
            status_data = await self.iot_control.remote_input_status(
                terminal.sn, org_id=terminal.org_id, user=user
            )
            lease_info = status_data.get("lease") or {}
            if lease_info.get("active") and lease_info.get("lease_id"):
                owner = str(lease_info.get("owner_user_id") or "")
                user_sub = str(user.get("sub") or "")
                user_id_str = str(user.get("user_id") or "")
                is_su = bool(
                    user.get("is_superuser")
                    or user.get("role") in ("superuser", "admin")
                    or role_id == 1
                )
                if is_su or owner in (user_sub, user_id_str):
                    existing_lease_id = str(lease_info["lease_id"])
                    logger.info(
                        "Reusing existing lease %s for terminal %d",
                        existing_lease_id,
                        terminal.id,
                    )

        if existing_lease_id:
            lease_id = existing_lease_id
        else:
            try:
                lease_res = await self.iot_control.remote_input_acquire_lease(
                    sn=terminal.sn,
                    scope=lease_scope,
                    ttl_sec=settings.remote_session_watchdog_ttl_sec,
                    org_id=terminal.org_id,
                    user=custom_user,
                )
                lease_id = str(lease_res.get("lease_id") or "")
                if not lease_id:
                    raise ValueError("IoT did not return a control lease ID")
            except Exception as lease_err:
                logger.warning(
                    "Could not acquire control lease for terminal %d: %s",
                    terminal.id,
                    lease_err,
                )
                # If lease fails due to lease_taken, treat as conflict
                if (
                    isinstance(lease_err, HTTPException)
                    and lease_err.status_code == 409
                ):
                    await compensate_start_failure("lease_conflict")
                    raise HTTPException(
                        status_code=status.HTTP_409_CONFLICT,
                        detail={
                            "code": "session_busy",
                            "message": f"Терминал {terminal.sn} занят арендой другого пользователя.",
                        },
                    ) from lease_err
                await compensate_start_failure("lease_acquire_failed")
                raise HTTPException(
                    status_code=status.HTTP_502_BAD_GATEWAY,
                    detail={"code": "lease_acquire_failed"},
                ) from lease_err

        # 3. Media Orchestration (for video)
        mountpoint_id: int | None = None
        pin: str | None = None
        ws_path: str | None = None
        stream_inst_id: str | None = None

        if session_type == "video":
            mountpoint_id = device_id
            pin = get_or_create_mountpoint_pin(
                device_id,
                lease_id=lease_id,
                owner_user_id=str(user.get("sub") or user.get("user_id") or ""),
            )
            rtp_port, rtcp_port = get_device_ports(device_id)

            media_session_id = provider_session_id
            media_started = False
            last_media_err: Exception | None = None

            try:
                media_res = await self.media_orchestrator.start_session(
                    session_id=media_session_id,
                    operation_id=op_id,
                    sn=terminal.sn,
                    device_id=device_id,
                    pin=pin,
                    rtp_port=rtp_port,
                    rtcp_port=rtcp_port,
                    ttl_sec=settings.remote_session_watchdog_ttl_sec,
                )
                # Use mountpoint_id from media orchestrator (matches Janus)
                mountpoint_id = int(media_res.get("mountpoint_id") or device_id)
                media_pin = media_res.get("pin")
                if media_pin:
                    pin = str(media_pin)
                media_started = True
            except MediaSessionConflictError as conflict_err:
                # Never replace the IoT session ID with a media-only ID or stop by SN.
                logger.warning(
                    "Media session conflict for IoT session %s (sn=%s)",
                    media_session_id,
                    terminal.sn,
                )
                last_media_err = conflict_err
            except Exception as media_err:  # noqa: BLE001
                last_media_err = media_err

            if not media_started:
                media_err = last_media_err or Exception(
                    "Неизвестная ошибка медиаоркестратора"
                )
                logger.error(
                    "Media orchestrator start failed for terminal %d (%s), executing compensating stop: %s",
                    device_id,
                    terminal.sn,
                    media_err,
                )
                await compensate_start_failure("media_start_failed")

                if isinstance(media_err, MediaSessionConflictError):
                    raise HTTPException(
                        status_code=status.HTTP_409_CONFLICT,
                        detail={
                            "code": "session_busy",
                            "message": "Медиаканал устройства занят",
                        },
                    ) from media_err
                raise HTTPException(
                    status_code=status.HTTP_502_BAD_GATEWAY,
                    detail=f"Ошибка подготовки медиаканала: {media_err}",
                ) from media_err

            # Start video stream on terminal if requested (operator flow)
            if start_terminal_stream and role_id != 4 and lease_id:
                try:
                    stream_res = await self.iot_control.remote_input_stream_start(
                        lease_id=lease_id,
                        mode=mode,
                        source_id=source_id,
                        profile=profile,
                        org_id=terminal.org_id,
                        user=custom_user,
                    )
                    stream_inst_id = str(stream_res.get("stream_instance_id", ""))
                    if stream_inst_id:
                        set_mountpoint_stream_instance(device_id, stream_inst_id)
                except Exception as stream_err:
                    logger.error(
                        "Stream start failed on terminal %d, executing compensating stop: %s",
                        device_id,
                        stream_err,
                    )
                    await compensate_start_failure("stream_start_failed")
                    raise HTTPException(
                        status_code=status.HTTP_502_BAD_GATEWAY,
                        detail=f"Ошибка запуска видеопотока на терминале: {stream_err}",
                    ) from stream_err

        if lease_id:
            ws_path = f"/api/v1/video/devices/{device_id}/control/ws/{lease_id}"

        # 4. Activation
        local_session.state = "active"
        local_session.active_at = func.now()

        # Audit
        actor_name = str(
            user.get("username") or user.get("sub") or user.get("email") or "system"
        )
        await self.repo.record_audit_event(
            actor=actor_name,
            event_type=f"remote_{session_type}_session_started",
            subject_type="terminal",
            subject_id=str(device_id),
            operation_id=op_id,
            correlation_id=corr_id,
            outcome="success",
            tenant_id=terminal.org_id,
            details={
                "session_id": provider_session_id,
                "session_type": session_type,
                "lease_id": lease_id,
            },
        )
        await self.db.flush()
        with contextlib.suppress(Exception):
            await self.db.commit()

        return RemoteSessionResponse(
            session_id=provider_session_id,
            local_session_id=local_session.id,
            terminal_id=terminal.id,
            sn=terminal.sn,
            session_type=session_type,
            state="active",
            mountpoint_id=mountpoint_id,
            janus_ws="/janus-ws" if session_type == "video" else None,
            pin=pin,
            lease_id=lease_id,
            ws_path=ws_path,
            ttl_sec=settings.remote_session_watchdog_ttl_sec,
            stream_instance_id=stream_inst_id,
        )

    async def stop_session(
        self,
        *,
        device_id: int | None = None,
        session_id: str | None = None,
        reason: str = "user_closed",
        user: dict[str, Any],
    ) -> dict[str, Any]:
        """Stop an active remote session gracefully and release all resources."""
        active_session = None
        terminal = None

        if session_id:
            active_session = await self.repo.get_session_by_provider_id(session_id)
            if active_session:
                bound_result = await self.db.execute(
                    select(Terminal).where(
                        Terminal.id == active_session.terminal_id,
                        Terminal.org_id == active_session.tenant_id,
                    )
                )
                bound_terminal = bound_result.scalar_one_or_none()
                if bound_terminal is None:
                    raise HTTPException(
                        status_code=status.HTTP_409_CONFLICT,
                        detail={"code": "session_terminal_mismatch"},
                    )
                terminal = await self._verify_terminal_access(
                    bound_terminal.device_id, user, active_session.session_type
                )
                if terminal.id != active_session.terminal_id:
                    raise HTTPException(
                        status_code=status.HTTP_409_CONFLICT,
                        detail={"code": "session_terminal_mismatch"},
                    )
        elif device_id:
            terminal = await self._verify_terminal_access(device_id, user, "video")
            active_session = await self.repo.get_active_session_by_terminal_id(
                terminal.id
            )
            if active_session:
                terminal = await self._verify_terminal_access(
                    device_id, user, active_session.session_type
                )

        if not active_session:
            if session_id:
                raise HTTPException(
                    status_code=status.HTTP_404_NOT_FOUND,
                    detail={"code": "session_not_found"},
                )
            if terminal is not None:
                latest = await self.repo.get_latest_session_by_terminal_id(terminal.id)
                if latest is not None and latest.state == "closed":
                    return {
                        "status": "success",
                        "session_id": latest.provider_session_id,
                        "state": "closed",
                    }
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail={"code": "session_not_found"},
            )

        if active_session.state in ("closed", "failed"):
            return {
                "status": "success",
                "session_id": active_session.provider_session_id,
                "state": active_session.state,
            }

        if terminal is None:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail={"code": "session_terminal_mismatch"},
            )
        prov_id = active_session.provider_session_id
        if not prov_id:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail={"code": "session_identity_unavailable"},
            )
        stop_operation_id = f"stop-mb-{active_session.id}"
        effective_reason = (
            active_session.reason
            if active_session.state == "stop_requested" and active_session.reason
            else reason
        )
        active_session.state = "stop_requested"
        active_session.reason = effective_reason
        await self.db.flush()
        # The intent and exact provider ID must survive a process restart.
        await self.db.commit()

        try:
            await confirm_remote_session_stop(
                session_id=prov_id,
                session_type=active_session.session_type,
                tenant_id=active_session.tenant_id,
                sn=terminal.sn,
                operation_id=stop_operation_id,
                reason=effective_reason,
                correlation_id=active_session.correlation_id,
                iot_adapter=self.iot_adapter,
                media_orchestrator=self.media_orchestrator,
            )
        except RemoteSessionStopPending as exc:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail={"code": "session_stop_pending", "session_id": prov_id},
            ) from exc

        # Both required providers confirmed the exact session ID.
        active_session.state = "closed"
        active_session.closed_at = func.now()
        active_session.reason = effective_reason

        actor_name = str(
            user.get("username") or user.get("sub") or user.get("email") or "system"
        )
        if terminal:
            await self.repo.record_audit_event(
                actor=actor_name,
                event_type=f"remote_{active_session.session_type}_session_stopped",
                subject_type="terminal",
                subject_id=str(terminal.id),
                operation_id=active_session.operation_id,
                correlation_id=active_session.correlation_id,
                outcome="success",
                tenant_id=terminal.org_id,
                details={"session_id": prov_id, "reason": effective_reason},
            )
        await self.db.flush()
        await self.db.commit()

        return {
            "status": "success",
            "session_id": prov_id,
            "state": "closed",
        }

    async def get_session_status(
        self,
        device_id: int,
        user: dict[str, Any],
    ) -> RemoteSessionStatusResponse:
        """Get unified status of remote session on device."""
        stmt = select(Terminal).where(Terminal.device_id == device_id)
        res = await self.db.execute(stmt)
        terminal = res.scalar_one_or_none()
        if not terminal:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Терминал с ID {device_id} не найден",
            )

        user_org_id = resolve_org_id(user)
        role_id = int(user.get("role_id", 3))
        is_su = bool(
            user.get("is_superuser")
            or user.get("role") in ("superuser", "admin")
            or role_id == 1
        )
        if not is_su and terminal.org_id != user_org_id:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Доступ к терминалу другой организации запрещён",
            )

        active_session = await self.repo.get_active_session_by_terminal_id(terminal.id)
        if not active_session:
            return RemoteSessionStatusResponse(
                active=False,
                terminal_id=terminal.id,
                sn=terminal.sn,
            )

        prov_id = active_session.provider_session_id
        media_state = None
        streaming = False
        transport_connected = None
        fresh_rtp = None
        rtp_packets = 0
        bytes_count = 0
        idle_sec = None

        if active_session.session_type == "video":
            with contextlib.suppress(Exception):
                if not prov_id:
                    raise ValueError("Exact media session identity unavailable")
                health = await self.media_orchestrator.get_session_health(prov_id)
                media_state = health.get("media_state")
                transport_connected = health.get("transport_connected")
                fresh_rtp = health.get("fresh_rtp")
                rtp_packets = int(health.get("rtp_packets") or 0)
                bytes_count = int(health.get("bytes") or 0)
                idle_sec = (
                    float(health["idle_sec"])
                    if health.get("idle_sec") is not None
                    else None
                )
                streaming = bool(transport_connected and fresh_rtp is True)

        return RemoteSessionStatusResponse(
            active=True,
            session_id=prov_id,
            local_session_id=active_session.id,
            terminal_id=terminal.id,
            sn=terminal.sn,
            session_type=active_session.session_type,
            state=active_session.state,
            media_state=media_state,
            streaming=streaming,
            transport_connected=transport_connected,
            fresh_rtp=fresh_rtp,
            rtp_packets=rtp_packets,
            bytes=bytes_count,
            idle_sec=idle_sec,
        )
