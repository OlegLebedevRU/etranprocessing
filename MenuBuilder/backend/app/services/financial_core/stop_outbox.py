from __future__ import annotations

import logging
from datetime import UTC, datetime
from typing import Any

from sqlalchemy.ext.asyncio import AsyncSession

from app.models_l4desk import L4DeskAuditEvent
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.iot_event_feed_client import iot_event_feed_client

logger = logging.getLogger(__name__)


class FinStopOutboxService:
    """Outbox and retry coordinator for terminating active sessions upon tenant blockage (L4D-12-MB).

    Normative Logic:
    5. При blocked новые sessions запрещены. Active video/remote stop при периодической проверке;
       console запрещает новые commands, ждёт текущий response или IoT timeout, затем stop.
    IoT executes command-aware graceful stop for console and media teardown for video per H-L4D-07-IOT-v1.
    """

    @staticmethod
    async def stop_sessions_for_blocked_tenant(
        db: AsyncSession,
        tenant_id: int,
        iot_adapter: Any = None,
        actor: str = "stop_worker",
    ) -> list[dict[str, Any]]:
        """Stop all active sessions for a blocked tenant and queue retries in outbox if failed."""
        repo = L4DeskRepository(db)
        sessions = await repo.get_active_sessions_for_tenant(tenant_id)
        if not sessions:
            return []

        client = iot_adapter or iot_event_feed_client
        results: list[dict[str, Any]] = []

        for session in sessions:
            session.state = "stop_requested"
            session.reason = "entitlement_blocked"
            await db.flush()

            session_id_to_stop = session.provider_session_id or str(session.id)
            op_id = f"stop-outbox-{session.operation_id}"
            corr_id = session.correlation_id

            try:
                await client.stop_remote_session(
                    session_id=session_id_to_stop,
                    operation_id=op_id,
                    reason="entitlement_blocked",
                    correlation_id=corr_id,
                )
                session.state = "closed"
                session.closed_at = datetime.now(UTC)
                await db.flush()

                audit = L4DeskAuditEvent(
                    tenant_id=tenant_id,
                    actor=actor,
                    event_type="remote_session_stopped",
                    subject_type="l4desk_remote_session",
                    subject_id=str(session.id),
                    operation_id=op_id,
                    correlation_id=corr_id,
                    outcome="success",
                    details={
                        "terminal_id": session.terminal_id,
                        "session_type": session.session_type,
                        "provider_session_id": session.provider_session_id,
                        "reason": "entitlement_blocked",
                    },
                    occurred_at=datetime.now(UTC),
                )
                db.add(audit)
                await db.flush()
                results.append(
                    {
                        "session_id": session.id,
                        "status": "closed",
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
    ) -> list[dict[str, Any]]:
        """Retry pending stop requests in the outbox."""
        repo = L4DeskRepository(db)
        pending_sessions = await repo.get_stop_requested_sessions(limit=50)
        if not pending_sessions:
            return []

        client = iot_adapter or iot_event_feed_client
        results: list[dict[str, Any]] = []

        for session in pending_sessions:
            session_id_to_stop = session.provider_session_id or str(session.id)
            op_id = f"retry-stop-outbox-{session.operation_id}"
            corr_id = session.correlation_id

            try:
                await client.stop_remote_session(
                    session_id=session_id_to_stop,
                    operation_id=op_id,
                    reason=session.reason or "entitlement_blocked",
                    correlation_id=corr_id,
                )
                session.state = "closed"
                session.closed_at = datetime.now(UTC)
                await db.flush()

                audit = L4DeskAuditEvent(
                    tenant_id=session.tenant_id,
                    actor=actor,
                    event_type="remote_session_stop_retry_succeeded",
                    subject_type="l4desk_remote_session",
                    subject_id=str(session.id),
                    operation_id=op_id,
                    correlation_id=corr_id,
                    outcome="success",
                    details={
                        "terminal_id": session.terminal_id,
                        "session_type": session.session_type,
                        "provider_session_id": session.provider_session_id,
                    },
                    occurred_at=datetime.now(UTC),
                )
                db.add(audit)
                await db.flush()
                results.append(
                    {
                        "session_id": session.id,
                        "status": "closed",
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
