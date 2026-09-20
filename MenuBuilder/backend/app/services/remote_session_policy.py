from __future__ import annotations

import logging
from typing import Any, Protocol

from etranprocessing_db.models import Terminal
from pydantic import BaseModel
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings

logger = logging.getLogger(__name__)


class PolicyDecision(BaseModel):
    allowed: bool
    reason: str | None = None
    error_code: str | None = None
    entitlement_state: str = "active"  # "active" | "grace" | "blocked" | "free"


class RemoteSessionPolicy(Protocol):
    async def evaluate_session_request(
        self,
        tenant_id: int,
        terminal: Terminal,
        session_type: str,  # "console" | "video"
        user: dict[str, Any],
        db: AsyncSession,
    ) -> PolicyDecision: ...


class PermissiveLegacyPolicy:
    """Permissive policy for existing MenuBuilder users and backward compatibility.

    Always grants session requests unconditionally.
    """

    async def evaluate_session_request(
        self,
        tenant_id: int,
        terminal: Terminal,
        session_type: str,
        user: dict[str, Any],
        db: AsyncSession,
    ) -> PolicyDecision:
        return PolicyDecision(allowed=True, entitlement_state="active")


class L4DeskEntitlementPolicy:
    """Commercial entitlement policy for L4Desk profile (L4D-12-MB).

    Connects policy seam 08B to FinEntitlementService.
    Supports shadow mode and disabled policy flags.
    """

    def __init__(
        self,
        enforcement_enabled: bool | None = None,
        shadow_mode: bool | None = None,
    ) -> None:
        self.enforcement_enabled = (
            enforcement_enabled
            if enforcement_enabled is not None
            else settings.l4desk_policy_enforcement_enabled
        )
        self.shadow_mode = (
            shadow_mode
            if shadow_mode is not None
            else settings.l4desk_policy_shadow_mode
        )

    async def evaluate_session_request(
        self,
        tenant_id: int,
        terminal: Terminal,
        session_type: str,
        user: dict[str, Any],
        db: AsyncSession,
    ) -> PolicyDecision:
        from app.services.financial_core.entitlement import FinEntitlementService

        real_decision = await FinEntitlementService.evaluate_session_request(
            db=db,
            tenant_id=tenant_id,
            terminal_id=terminal.id,
            session_type=session_type,
            user=user,
        )

        decision = PolicyDecision(
            allowed=bool(real_decision["allowed"]),
            reason=real_decision.get("reason"),
            error_code=real_decision.get("error_code"),
            entitlement_state=str(real_decision.get("entitlement_state", "active")),
        )

        if not self.enforcement_enabled:
            # Shadow mode or disabled: log decisions, but do not block un-enforced tenants
            if not decision.allowed:
                logger.warning(
                    "Shadow policy would have DENIED session for tenant=%s terminal=%s (%s): %s (%s)",
                    tenant_id,
                    terminal.id,
                    session_type,
                    decision.reason,
                    decision.error_code,
                )
            return PolicyDecision(
                allowed=True,
                entitlement_state=decision.entitlement_state,
                reason=decision.reason,
                error_code=decision.error_code,
            )

        return decision


def get_remote_session_policy(
    user: dict[str, Any] | None = None,
) -> RemoteSessionPolicy:
    role_id = int((user or {}).get("role_id", 3))
    # If user has role 5 (L4Desk self-registered tenant user) or is_l4desk flag
    if role_id == 5 or (user or {}).get("is_l4desk"):
        return L4DeskEntitlementPolicy()
    return PermissiveLegacyPolicy()
