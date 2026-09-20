from __future__ import annotations

import logging
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

from sqlalchemy.ext.asyncio import AsyncSession

from app.repositories.l4desk_repository import L4DeskRepository
from app.services.financial_core.cycles import FinBillingCycleService
from app.services.financial_core.projection import FinProjectionService
from app.services.financial_core.terminals import FinTerminalService
from app.services.financial_core.timezones import resolve_timezone

logger = logging.getLogger(__name__)

ENTITLEMENT_FREE = "free"
ENTITLEMENT_ACTIVE = "active"
ENTITLEMENT_GRACE = "grace"
ENTITLEMENT_BLOCKED = "blocked"

REASON_ENTITLEMENT_BLOCKED = "entitlement_blocked"
REASON_FREE_QUOTA_EXCEEDED = "free_quota_exceeded"
REASON_UNPAID_SECONDARY_TERMINAL = "unpaid_secondary_terminal"
REASON_PAYMENT_REQUIRED = "payment_required"


@dataclass
class FinEntitlementStatus:
    tenant_id: int
    state: str  # "free" | "active" | "grace" | "blocked"
    balance_kopecks: int
    is_first_paid: bool
    cycle_id: int | None = None
    cycle_starts_at: datetime | None = None
    cycle_ends_at: datetime | None = None
    grace_deadline: datetime | None = None
    can_start_sessions: bool = True
    free_terminal_id: int | None = None
    today_usage_seconds: int = 0
    free_quota_seconds: int = 7200
    reason_code: str | None = None
    reason_message: str | None = None


class FinEntitlementService:
    """Normative commercial entitlement state machine (L4D-12-MB).

    Normative Logic:
    1. До первой успешной оплаты доступны только free terminal и его pooled 120 min/local day;
       grace отсутствует. При положительном балансе после квоты работа продолжается платно.
    2. После первой оплаты status определяется balance и границей cycle:
       active при balance>=0; grace при balance<0 и now < cycle_start+3 days; blocked иначе.
    3. Grace всегда привязан к cycle boundary, не к online/charge/payment.
       Online на пятый день создаёт charge текущего cycle и при недостатке средств немедленно blocked.
    4. Поздняя оплата погашает текущий период и не переносит anchor. Возврат balance>=0 разблокирует.
    5. При blocked новые sessions запрещены. Active video/remote stop при периодической проверке;
       console запрещает новые commands, ждёт текущий response или IoT timeout, затем stop.
    """

    @staticmethod
    async def get_tenant_entitlement_status(
        db: AsyncSession,
        tenant_id: int,
        as_of: datetime | None = None,
    ) -> FinEntitlementStatus:
        as_of_dt = as_of or datetime.now(UTC)
        if as_of_dt.tzinfo is None:
            as_of_dt = as_of_dt.replace(tzinfo=UTC)

        profile = await FinBillingCycleService.ensure_billing_profile(db, tenant_id)
        balance_kopecks = await FinProjectionService.get_balance_kopecks(db, tenant_id)
        is_first_paid = profile.anchor_at is not None

        free_term = await FinTerminalService.get_free_terminal(db, tenant_id)
        free_terminal_id = free_term.terminal_id if free_term else None

        effective_tz = profile.anchor_timezone or "UTC"
        tz_zone = resolve_timezone(effective_tz)
        local_date = as_of_dt.astimezone(tz_zone).date()

        today_usage_seconds = 0
        repo = L4DeskRepository(db)
        if free_terminal_id is not None:
            usage = await repo.get_usage_daily(free_terminal_id, local_date)
            if usage is not None:
                today_usage_seconds = usage.video_seconds + usage.console_seconds

        free_quota_seconds = 7200  # 120 minutes

        if not is_first_paid:
            # Free tier before first payment
            state = ENTITLEMENT_FREE
            cycle_id = None
            cycle_starts_at = None
            cycle_ends_at = None
            grace_deadline = None

            if free_terminal_id is None:
                can_start_sessions = False
                reason_code = "no_terminals"
                reason_message = "У тенанта нет зарегистрированных терминалов"
            elif today_usage_seconds >= free_quota_seconds and balance_kopecks <= 0:
                can_start_sessions = False
                reason_code = REASON_FREE_QUOTA_EXCEEDED
                reason_message = (
                    "Исчерпан суточный лимит 120 минут бесплатного использования"
                )
            else:
                can_start_sessions = True
                reason_code = None
                reason_message = None
        else:
            # Paid tier after first successful payment
            cycle = await FinBillingCycleService.get_or_create_cycle_for_timestamp(
                db, tenant_id, as_of_dt
            )
            if cycle is not None:
                cycle_id = cycle.id
                cycle_starts_at = cycle.starts_at
                cycle_ends_at = cycle.ends_at
                grace_deadline = cycle.grace_deadline
                is_within_grace = as_of_dt < cycle.grace_deadline
            else:
                cycle_id = None
                cycle_starts_at = None
                cycle_ends_at = None
                grace_deadline = None
                is_within_grace = False

            if balance_kopecks >= 0:
                state = ENTITLEMENT_ACTIVE
                can_start_sessions = True
                reason_code = None
                reason_message = None
            elif is_within_grace:
                state = ENTITLEMENT_GRACE
                can_start_sessions = True
                reason_code = None
                reason_message = None
            else:
                state = ENTITLEMENT_BLOCKED
                can_start_sessions = False
                reason_code = REASON_ENTITLEMENT_BLOCKED
                reason_message = (
                    "Оказание услуг приостановлено из-за задолженности "
                    "(льготный период истек)"
                )

        if profile.entitlement != state:
            logger.info(
                "Entitlement transition for tenant=%s: %s -> %s (balance=%s, as_of=%s)",
                tenant_id,
                profile.entitlement,
                state,
                balance_kopecks,
                as_of_dt.isoformat(),
            )
            profile.entitlement = state
            profile.entitlement_changed_at = as_of_dt
            await db.flush()

        return FinEntitlementStatus(
            tenant_id=tenant_id,
            state=state,
            balance_kopecks=balance_kopecks,
            is_first_paid=is_first_paid,
            cycle_id=cycle_id,
            cycle_starts_at=cycle_starts_at,
            cycle_ends_at=cycle_ends_at,
            grace_deadline=grace_deadline,
            can_start_sessions=can_start_sessions,
            free_terminal_id=free_terminal_id,
            today_usage_seconds=today_usage_seconds,
            free_quota_seconds=free_quota_seconds,
            reason_code=reason_code,
            reason_message=reason_message,
        )

    @staticmethod
    async def evaluate_session_request(
        db: AsyncSession,
        tenant_id: int,
        terminal_id: int,
        session_type: str,
        user: dict[str, Any] | None = None,
        as_of: datetime | None = None,
    ) -> dict[str, Any]:
        """Evaluate session request against entitlement rules.

        Returns dictionary compatible with PolicyDecision:
        {
            "allowed": bool,
            "reason": str | None,
            "error_code": str | None,
            "entitlement_state": str
        }
        """
        status = await FinEntitlementService.get_tenant_entitlement_status(
            db, tenant_id, as_of=as_of
        )

        if status.state == ENTITLEMENT_BLOCKED:
            return {
                "allowed": False,
                "reason": (
                    "Оказание услуг приостановлено из-за задолженности "
                    "(льготный период истек)"
                ),
                "error_code": REASON_ENTITLEMENT_BLOCKED,
                "entitlement_state": ENTITLEMENT_BLOCKED,
            }

        if status.state == ENTITLEMENT_FREE:
            # 1. Terminal check: free tier only allows the free terminal
            is_free_term = await FinTerminalService.is_terminal_free(
                db, tenant_id, terminal_id
            )
            if not is_free_term:
                return {
                    "allowed": False,
                    "reason": (
                        "В бесплатном режиме доступен только один основной терминал. "
                        "Пополните баланс для подключения дополнительных терминалов."
                    ),
                    "error_code": REASON_UNPAID_SECONDARY_TERMINAL,
                    "entitlement_state": ENTITLEMENT_FREE,
                }

            # 2. Daily pooled quota check
            if status.today_usage_seconds >= status.free_quota_seconds:
                if status.balance_kopecks > 0:
                    # Positive balance allows continued paid usage
                    return {
                        "allowed": True,
                        "reason": None,
                        "error_code": None,
                        "entitlement_state": ENTITLEMENT_FREE,
                    }
                return {
                    "allowed": False,
                    "reason": (
                        "Исчерпан суточный лимит 120 минут бесплатного использования. "
                        "Для продолжения работы пополните баланс."
                    ),
                    "error_code": REASON_FREE_QUOTA_EXCEEDED,
                    "entitlement_state": ENTITLEMENT_FREE,
                }

            return {
                "allowed": True,
                "reason": None,
                "error_code": None,
                "entitlement_state": ENTITLEMENT_FREE,
            }

        if status.state == ENTITLEMENT_GRACE:
            return {
                "allowed": True,
                "reason": None,
                "error_code": None,
                "entitlement_state": ENTITLEMENT_GRACE,
            }

        # ENTITLEMENT_ACTIVE
        return {
            "allowed": True,
            "reason": None,
            "error_code": None,
            "entitlement_state": ENTITLEMENT_ACTIVE,
        }
