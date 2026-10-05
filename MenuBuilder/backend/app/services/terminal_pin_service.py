"""Tenant-authorized PIN renewal through the certificate provider.

Only operation ownership is persisted here. Plain PINs remain provider-owned.
"""

from __future__ import annotations

import logging
from datetime import UTC, datetime
from typing import Any
from uuid import UUID

from fastapi import HTTPException
from sqlalchemy import select, text
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import Terminal
from app.models_l4desk import L4DeskAuditEvent, L4DeskTerminal
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.certificate_permission import certificate_permission
from app.services.terminal_onboarding_service import (
    IssueCertificatePinRequest,
    IssueCertificatePinResponse,
    ProcessingBackendPinClient,
)

logger = logging.getLogger(__name__)


class TerminalPinService:
    def __init__(
        self, db: AsyncSession, provider: ProcessingBackendPinClient | None = None
    ) -> None:
        self.db = db
        self.provider = provider or ProcessingBackendPinClient()

    async def _owned_terminal(self, terminal_id: int, user: dict[str, Any]) -> Terminal:
        query = select(Terminal).where(
            Terminal.id == terminal_id,
            Terminal.org_id == int(user.get("org_id") or 0),
        )
        terminal = (await self.db.execute(query)).scalar_one_or_none()
        if terminal is None:
            raise HTTPException(404, "Терминал не найден")
        if not terminal.sn:
            raise HTTPException(409, "У терминала отсутствует серийный номер")
        l4 = await self.db.get(L4DeskTerminal, terminal_id)
        if (
            l4 is not None
            and l4.deleted_at is not None
            and (
                not settings.product_scope_split_enabled
                or terminal.l4desk_subscription_enabled
            )
        ):
            raise HTTPException(404, "Терминал удалён")
        return terminal

    @staticmethod
    def _validate_response(
        response: IssueCertificatePinResponse, terminal: Terminal, operation_id: str
    ) -> IssueCertificatePinResponse:
        if (
            response.operation_id != operation_id
            or response.tenant_id != terminal.org_id
            or response.terminal_id != terminal.id
            or response.sn != terminal.sn
        ):
            raise HTTPException(502, "Несовпадение идентификаторов ответа PIN")
        # Never leak stale plaintext if a provider violates its lifecycle contract.
        if response.status == "issued" and response.expires_at <= datetime.now(UTC):
            response.status = "expired"
        if response.status != "issued":
            response.pin = None
        return response

    async def current(
        self, terminal_id: int, user: dict[str, Any]
    ) -> IssueCertificatePinResponse | None:
        terminal = await self._owned_terminal(terminal_id, user)
        operation_id = (
            await self.db.execute(
                select(L4DeskAuditEvent.operation_id)
                .where(
                    L4DeskAuditEvent.tenant_id == terminal.org_id,
                    L4DeskAuditEvent.subject_id == str(terminal.id),
                    L4DeskAuditEvent.event_type == "terminal.pin_requested",
                )
                .order_by(L4DeskAuditEvent.id.desc())
                .limit(1)
            )
        ).scalar_one_or_none()
        if operation_id is None:
            l4 = await self.db.get(L4DeskTerminal, terminal_id)
            operation_id = l4.operation_id if l4 is not None else None
        if operation_id is None:
            return None
        try:
            response = await self.provider.get_by_operation(operation_id)
        except Exception as exc:
            logger.warning("PIN lookup unavailable: %s", type(exc).__name__)
            raise HTTPException(503, "Сервис PIN временно недоступен") from exc
        return (
            self._validate_response(response, terminal, operation_id)
            if response is not None
            else None
        )

    async def issue(
        self, terminal_id: int, user: dict[str, Any], request_id: UUID
    ) -> IssueCertificatePinResponse:
        terminal = await self._owned_terminal(terminal_id, user)
        operation_id = str(request_id)
        order_item_id = await certificate_permission(
            self.db,
            terminal,
            purpose="setup",
            is_superuser=bool(user.get("is_superuser")),
        )
        lock = text("SELECT pg_advisory_xact_lock(hashtextextended(:identity, 0))")
        await self.db.execute(lock, {"identity": f"mb-pin-operation:{operation_id}"})
        await self.db.execute(lock, {"identity": f"mb-terminal-pin:{terminal.id}"})
        intent = (
            await self.db.execute(
                select(L4DeskAuditEvent).where(
                    L4DeskAuditEvent.operation_id == operation_id,
                    L4DeskAuditEvent.event_type == "terminal.pin_requested",
                )
            )
        ).scalar_one_or_none()
        if intent is not None and (
            intent.tenant_id != terminal.org_id
            or intent.subject_id != str(terminal.id)
            or (intent.details or {}).get("sn") != terminal.sn
        ):
            raise HTTPException(409, "PIN operation_id уже используется")
        if intent is None:
            await L4DeskRepository(self.db).record_audit_event(
                actor=str(user.get("sub") or user.get("id")),
                event_type="terminal.pin_requested",
                subject_type="terminal",
                subject_id=str(terminal.id),
                tenant_id=terminal.org_id,
                correlation_id=operation_id,
                operation_id=operation_id,
                outcome="requested",
                details={"sn": terminal.sn},
            )
            # Persist ownership before an ambiguous remote outcome; retries reuse this ID.
            await self.db.commit()
        # Serialize calls from this consumer: provider first-POST concurrency is not guaranteed.
        await self.db.execute(lock, {"identity": f"mb-terminal-pin:{terminal.id}"})
        latest = (
            await self.db.execute(
                select(L4DeskAuditEvent.operation_id)
                .where(
                    L4DeskAuditEvent.tenant_id == terminal.org_id,
                    L4DeskAuditEvent.subject_id == str(terminal.id),
                    L4DeskAuditEvent.event_type == "terminal.pin_requested",
                )
                .order_by(L4DeskAuditEvent.id.desc())
                .limit(1)
            )
        ).scalar_one_or_none()
        if latest != operation_id:
            await self.db.rollback()
            raise HTTPException(409, "Запрос PIN заменён более новым запросом")
        try:
            response = await self.provider.get_by_operation(operation_id)
            if response is None:
                response = await self.provider.issue_pin(
                    IssueCertificatePinRequest(
                        operation_id=operation_id,
                        correlation_id=operation_id,
                        tenant_id=terminal.org_id,
                        terminal_id=terminal.id,
                        sn=terminal.sn,
                        actor=str(user.get("sub") or user.get("id")),
                        order_item_id=order_item_id,
                    )
                )
            response = self._validate_response(response, terminal, operation_id)
        except HTTPException:
            await self.db.rollback()
            raise
        except Exception as exc:
            await self.db.rollback()
            logger.warning("PIN issuance unavailable: %s", type(exc).__name__)
            raise HTTPException(
                503, "Сервис PIN временно недоступен; повторите запрос"
            ) from exc
        if response.status == "issued":
            await L4DeskRepository(self.db).record_audit_event(
                actor=str(user.get("sub") or user.get("id")),
                event_type="terminal.pin_renewed",
                subject_type="terminal",
                subject_id=str(terminal.id),
                tenant_id=terminal.org_id,
                correlation_id=operation_id,
                operation_id=operation_id,
                outcome="success",
                details={"sn": terminal.sn},
            )
        await self.db.commit()
        return response
