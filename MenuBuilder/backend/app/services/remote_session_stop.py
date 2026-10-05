from __future__ import annotations

import asyncio
import logging
from datetime import UTC, datetime
from typing import Any

from fastapi import HTTPException
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models_l4desk import L4DeskAuditEvent, L4DeskRemoteSession
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.iot_client import iot_client
from app.services.media_orchestrator_client import media_orchestrator_client

logger = logging.getLogger(__name__)


class RemoteSessionStopService:
    """Outbox and retry coordinator for terminating active sessions upon tenant blockage (L4D-12-MB).

    Normative Logic:
    5. При blocked новые sessions запрещены. Active video/remote stop при периодической проверке;
       console запрещает новые commands, ждёт текущий response или IoT timeout, затем stop.
    IoT executes command-aware graceful stop for console and media teardown for video per H-L4D-07-IOT-v1.
    """

    @staticmethod
    async def _commercial_allows(
        db: AsyncSession, tenant_id: int, runtime: Any
    ) -> bool:
        from app.services.subscriptions import check_terminal

        if not runtime.is_active:
            return False
        try:
            current = await check_terminal(db, tenant_id, runtime)
        except HTTPException as exc:
            if exc.status_code != 403:
                raise
            return (
                False  # Invalid/missing enrollment metadata must not prevent cleanup.
            )
        return current is None or current.allowed

    @staticmethod
    async def _bounded_stop(
        db: AsyncSession, session: L4DeskRemoteSession, iot_adapter: Any = None
    ) -> bool:
        async with asyncio.timeout(settings.remote_session_stop_timeout_sec):
            return await RemoteSessionStopService._stop_provider_session(
                db, session, iot_adapter
            )

    @staticmethod
    async def _stop_provider_session(
        db: AsyncSession,
        session: L4DeskRemoteSession,
        iot_adapter: Any = None,
    ) -> bool:
        """Stop the current lease/stream epoch; return whether its end was confirmed."""
        if iot_adapter is not None:
            await iot_adapter.stop_remote_session(
                session_id=session.provider_session_id or str(session.id),
                operation_id=f"stop-outbox-{session.operation_id}",
                reason=session.reason or "entitlement_blocked",
                correlation_id=session.correlation_id,
            )
            return True

        terminal = await L4DeskRepository(db).get_terminal(
            session.terminal_id, session.tenant_id
        )
        if terminal is None or not terminal.sn or not session.provider_session_id:
            raise ValueError("Active session has no terminal SN or provider epoch")

        # app1 requires a session header. This fixed service identity is trusted
        # only together with the internal service key; tenant and SN come from DB.
        worker_user = {
            "sub": "entitlement_stop_worker",
            "role": "superuser",
            "role_id": 1,
            "session_id": "entitlement_stop_worker",
        }
        status = await iot_client.remote_input_status(
            terminal.sn, org_id=session.tenant_id, user=worker_user
        )
        lease = status.get("lease") or {}
        if lease.get("active") is not True:
            return False
        lease_id = str(lease.get("lease_id") or "")
        if not lease_id:
            raise ValueError("Provider reports active lease without lease_id")

        if session.session_type == "console":
            if lease_id != session.provider_session_id:
                return False  # A newer lease must remain untouched.
            await iot_client.remote_input_release(
                lease_id, org_id=session.tenant_id, user=worker_user
            )
            return True

        if session.session_type != "video":
            raise ValueError(f"Unsupported session type: {session.session_type}")
        stream = (status.get("agent") or {}).get("stream") or {}
        stream_id = str(
            lease.get("stream_instance_id") or stream.get("stream_instance_id") or ""
        )
        if stream_id != session.provider_session_id:
            return False  # Old epoch is gone; do not stop a replacement stream.

        await iot_client.remote_input_stream_stop(
            lease_id, org_id=session.tenant_id, user=worker_user
        )
        try:
            await media_orchestrator_client.stop_session_for_sn(
                sn=terminal.sn,
                lease_id=lease_id,
                stream_instance_id=stream_id,
                reason="entitlement_blocked",
            )
        except Exception:
            logger.exception(
                "Media cleanup failed after confirmed stop for %s", terminal.sn
            )
        try:
            await iot_client.remote_input_release(
                lease_id, org_id=session.tenant_id, user=worker_user
            )
        except Exception:
            logger.exception(
                "Lease release failed after confirmed stop for %s", terminal.sn
            )
        return True

    @staticmethod
    async def _close_after_stop(
        db: AsyncSession,
        session: L4DeskRemoteSession,
        confirmed_end: bool,
    ) -> None:
        from app.services.remote_session_metering import close_remote_session

        closed = await close_remote_session(
            db,
            terminal_id=session.terminal_id,
            session_type=session.session_type,
            reason=session.reason or "entitlement_blocked",
            at=datetime.now(UTC),
            confirmed_end=confirmed_end,
            expected_provider_session_id=session.provider_session_id,
        )
        if closed is None:
            raise RuntimeError("Session changed while recording provider stop")

    @staticmethod
    async def stop_sessions_for_blocked_tenant(
        db: AsyncSession,
        tenant_id: int,
        iot_adapter: Any = None,
        actor: str = "stop_worker",
        terminal_id: int | None = None,
    ) -> list[dict[str, Any]]:
        """Stop all active sessions for a blocked tenant and queue retries in outbox if failed."""
        repo = L4DeskRepository(db)
        if terminal_id is not None:
            from app.models import Terminal

            runtime = await db.get(Terminal, terminal_id)
            if (
                runtime is not None
                and await RemoteSessionStopService._commercial_allows(
                    db, tenant_id, runtime
                )
            ):
                return []
        sessions = await repo.get_active_sessions_for_tenant(tenant_id)
        if terminal_id is not None:
            sessions = [s for s in sessions if s.terminal_id == terminal_id]
        if not sessions:
            return []

        results: list[dict[str, Any]] = []

        for session in sessions:
            session.state = "stop_requested"
            session.reason = (
                "subscription_blocked"
                if terminal_id is not None
                else "entitlement_blocked"
            )
            await db.flush()

            op_id = f"stop-outbox-{session.operation_id}"
            corr_id = session.correlation_id

            try:
                confirmed = await RemoteSessionStopService._bounded_stop(
                    db, session, iot_adapter
                )
                await RemoteSessionStopService._close_after_stop(db, session, confirmed)

                audit = L4DeskAuditEvent(
                    tenant_id=tenant_id,
                    actor=actor,
                    event_type="remote_session_stopped",
                    subject_type="l4desk_remote_session",
                    subject_id=str(session.id),
                    operation_id=op_id,
                    correlation_id=corr_id,
                    outcome="success" if confirmed else "waived",
                    details={
                        "terminal_id": session.terminal_id,
                        "session_type": session.session_type,
                        "provider_session_id": session.provider_session_id,
                        "reason": "entitlement_blocked",
                        "confirmed_end": confirmed,
                    },
                    occurred_at=datetime.now(UTC),
                )
                db.add(audit)
                await db.flush()
                results.append(
                    {
                        "session_id": session.id,
                        "status": session.state,
                        "error": None,
                    }
                )
                logger.info(
                    "Successfully stopped session %s (type=%s, provider_id=%s) for blocked tenant=%s",
                    session.id,
                    session.session_type,
                    session.provider_session_id,
                    tenant_id,
                )
            except Exception as exc:  # noqa: BLE001
                logger.warning(
                    "Failed to stop session %s for blocked tenant=%s (will retry in outbox): %s",
                    session.id,
                    tenant_id,
                    exc,
                )
                # Remains in stop_requested state for outbox retry!
                audit = L4DeskAuditEvent(
                    tenant_id=tenant_id,
                    actor=actor,
                    event_type="remote_session_stop_failed",
                    subject_type="l4desk_remote_session",
                    subject_id=str(session.id),
                    operation_id=op_id,
                    correlation_id=corr_id,
                    outcome="failed",
                    details={
                        "terminal_id": session.terminal_id,
                        "session_type": session.session_type,
                        "provider_session_id": session.provider_session_id,
                        "error": str(exc)[:500],
                    },
                    occurred_at=datetime.now(UTC),
                )
                db.add(audit)
                await db.flush()
                results.append(
                    {
                        "session_id": session.id,
                        "status": "stop_requested",
                        "error": str(exc)[:500],
                    }
                )

        return results

    @staticmethod
    async def process_stop_outbox(
        db: AsyncSession,
        iot_adapter: Any = None,
        actor: str = "stop_outbox_retry_worker",
        tenant_id: int | None = None,
    ) -> list[dict[str, Any]]:
        """Retry pending stop requests in the outbox."""
        repo = L4DeskRepository(db)
        pending_sessions = await repo.get_stop_requested_sessions(
            limit=50, tenant_id=tenant_id
        )
        if not pending_sessions:
            return []

        results: list[dict[str, Any]] = []

        for session in pending_sessions:
            if session.reason and (
                session.reason.startswith("subscription_")
                or session.reason == "entitlement_blocked"
            ):
                from app.models import Terminal

                runtime = await db.get(Terminal, session.terminal_id)
                if (
                    runtime is not None
                    and await RemoteSessionStopService._commercial_allows(
                        db, session.tenant_id, runtime
                    )
                ):
                    session.state = "active"
                    session.reason = None
                    continue
            op_id = f"retry-stop-outbox-{session.operation_id}"
            corr_id = session.correlation_id

            try:
                confirmed = await RemoteSessionStopService._bounded_stop(
                    db, session, iot_adapter
                )
                await RemoteSessionStopService._close_after_stop(db, session, confirmed)

                audit = L4DeskAuditEvent(
                    tenant_id=session.tenant_id,
                    actor=actor,
                    event_type="remote_session_stop_retry_succeeded",
                    subject_type="l4desk_remote_session",
                    subject_id=str(session.id),
                    operation_id=op_id,
                    correlation_id=corr_id,
                    outcome="success" if confirmed else "waived",
                    details={
                        "terminal_id": session.terminal_id,
                        "session_type": session.session_type,
                        "provider_session_id": session.provider_session_id,
                        "confirmed_end": confirmed,
                    },
                    occurred_at=datetime.now(UTC),
                )
                db.add(audit)
                await db.flush()
                results.append(
                    {
                        "session_id": session.id,
                        "status": session.state,
                        "error": None,
                    }
                )
                logger.info(
                    "Outbox retry successfully closed session %s (provider_id=%s)",
                    session.id,
                    session.provider_session_id,
                )
            except Exception as exc:  # noqa: BLE001
                logger.warning(
                    "Outbox retry failed to stop session %s: %s",
                    session.id,
                    exc,
                )
                results.append(
                    {
                        "session_id": session.id,
                        "status": "stop_requested",
                        "error": str(exc)[:500],
                    }
                )

        return results
