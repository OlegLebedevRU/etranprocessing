from typing import Any, Protocol

from etranprocessing_db.models import Terminal
from pydantic import BaseModel
from sqlalchemy.ext.asyncio import AsyncSession

from app.services.subscriptions import check_terminal


class PolicyDecision(BaseModel):
    allowed: bool
    reason: str | None = None
    error_code: str | None = None
    entitlement_state: str = "active"


class RemoteSessionPolicy(Protocol):
    async def evaluate_session_request(
        self,
        tenant_id: int,
        terminal: Terminal,
        session_type: str,
        user: dict[str, Any],
        db: AsyncSession,
    ) -> PolicyDecision: ...


class L4DeskEntitlementPolicy:
    """Server-owned tenant scope; every role uses the same terminal subscription."""

    async def evaluate_session_request(
        self,
        tenant_id: int,
        terminal: Terminal,
        session_type: str,
        user: dict[str, Any],
        db: AsyncSession,
    ) -> PolicyDecision:
        state = await check_terminal(db, tenant_id, terminal)
        if state is None:
            return PolicyDecision(
                allowed=True
            )  # Classic has a separate product policy.
        return PolicyDecision(
            allowed=state.allowed,
            reason=state.reason,
            error_code=None if state.allowed else "subscription_" + state.state,
            entitlement_state=state.state,
        )


class PermissiveLegacyPolicy:
    """Explicit compatibility adapter for consumers of the old policy class."""

    async def evaluate_session_request(
        self,
        tenant_id: int,
        terminal: Terminal,
        session_type: str,
        user: dict[str, Any],
        db: AsyncSession,
    ) -> PolicyDecision:
        return await L4DeskEntitlementPolicy().evaluate_session_request(
            tenant_id, terminal, session_type, user, db
        )


def get_remote_session_policy(
    user: dict[str, Any] | None = None,
) -> RemoteSessionPolicy:
    return L4DeskEntitlementPolicy()
