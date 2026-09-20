from __future__ import annotations

from typing import Any, Protocol

from etranprocessing_db.models import Terminal
from pydantic import BaseModel
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings


class PolicyDecision(BaseModel):
    allowed: bool
    reason: str | None = None
    error_code: str | None = None
    entitlement_state: str = "active"  # "active" | "grace" | "blocked"


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
    """Commercial entitlement policy for L4Desk profile (L4D-08B-MB).

    In L4D-08B, billing and entitlement enforcement is disabled by feature flag
    (settings.l4desk_policy_enforcement_enabled = False).
    When flag is False, acts permissively.
    When flag is True (future L4D-12), will check balance, cycle, quotas and grace.
    """

    def __init__(self, enforcement_enabled: bool | None = None) -> None:
        self.enforcement_enabled = (
            enforcement_enabled
            if enforcement_enabled is not None
            else settings.l4desk_policy_enforcement_enabled
        )

    async def evaluate_session_request(
        self,
        tenant_id: int,
        terminal: Terminal,
        session_type: str,
        user: dict[str, Any],
        db: AsyncSession,
    ) -> PolicyDecision:
        if not self.enforcement_enabled:
            return PolicyDecision(allowed=True, entitlement_state="active")

        # Seam for future L4D-12-MB entitlement enforcement
        return PolicyDecision(allowed=True, entitlement_state="active")


def get_remote_session_policy(
    user: dict[str, Any] | None = None,
) -> RemoteSessionPolicy:
    role_id = int((user or {}).get("role_id", 3))
    # If user has role 5 (L4Desk self-registered tenant user) or is_l4desk flag
    if role_id == 5 or (user or {}).get("is_l4desk"):
        return L4DeskEntitlementPolicy()
    return PermissiveLegacyPolicy()
