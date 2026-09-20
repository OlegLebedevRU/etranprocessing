from __future__ import annotations

import logging
import uuid
from datetime import UTC, datetime
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models_l4desk import FinPayment
from app.services.financial_core.accounts import FinAccountService
from app.services.financial_core.cycles import FinBillingCycleService
from app.services.financial_core.exceptions import (
    FinConcurrencyError,
    FinTenantIsolationError,
    FinValidationError,
)
from app.services.financial_core.posting import FinPostingService
from app.services.financial_core.schemas import (
    FinPostingEntryRequest,
    FinPostingRequest,
)
from app.services.financial_core.yookassa import (
    YooKassaApiError,
    YooKassaNetworkError,
    get_yookassa_client,
    is_ip_trusted,
    is_safe_return_url,
)

logger = logging.getLogger(__name__)


class FinPaymentService:
    """Service managing YooKassa top-ups, idempotency, webhook processing, and ledger posting."""

    @staticmethod
    async def create_payment(
        db: AsyncSession,
        *,
        tenant_id: int,
        user_id: int | None = None,
        amount_rubles: int,
        return_url: str | None = None,
        idempotence_key: str | None = None,
        customer_email: str | None = None,
        actor: str = "tenant_user",
        correlation_id: str = "",
    ) -> FinPayment:
        """Create a new payment record and initiate provider payment with YooKassa."""
        if not isinstance(amount_rubles, int) or amount_rubles < 1:
            raise FinValidationError(
                "Payment amount must be a positive integer in rubles (>= 1)"
            )

        amount_kopecks = amount_rubles * 100
        effective_op_id = idempotence_key or str(uuid.uuid4())

        # Check existing payment with same operation_id (idempotency)
        stmt_exist = select(FinPayment).where(
            FinPayment.tenant_id == tenant_id,
            FinPayment.operation_id == effective_op_id,
        )
        res_exist = await db.execute(stmt_exist)
        existing_payment = res_exist.scalar_one_or_none()
        if existing_payment:
            if existing_payment.amount_kopecks != amount_kopecks:
                raise FinConcurrencyError(
                    f"Idempotence key {effective_op_id} already used with different amount"
                )
            logger.info(
                "Idempotent replay for payment id=%s (operation_id=%s)",
                existing_payment.id,
                effective_op_id,
            )
            return existing_payment

        # Validate return_url
        effective_return_url = return_url or (
            f"{settings.yookassa_return_url_base.rstrip('/')}/finance?payment=done"
            if settings.yookassa_return_url_base
            else "/finance?payment=done"
        )
        if not is_safe_return_url(
            effective_return_url, settings.yookassa_return_url_base
        ):
            raise FinValidationError(f"Unsafe return_url: {effective_return_url}")

        now_dt = datetime.now(UTC)

        payment = FinPayment(
            tenant_id=tenant_id,
            operation_id=effective_op_id,
            provider="yookassa",
            provider_payment_id=None,
            status="pending",
            amount_kopecks=amount_kopecks,
            currency="RUB",
            confirmation_url=None,
            provider_receipt_id=None,
            receipt_status=None,
            receipt_snapshot=None,
            verified_at=None,
            succeeded_at=None,
            ledger_transaction_id=None,
            actor=actor,
            correlation_id=correlation_id or effective_op_id,
            created_at=now_dt,
        )
        db.add(payment)
        await db.flush()

        desc = f"Пополнение баланса L4Desk (tenant {tenant_id})"
        metadata = {
            "tenant_id": str(tenant_id),
            "payment_id": str(payment.id),
            "operation_id": effective_op_id,
            "correlation_id": correlation_id or str(payment.id),
        }

        yoo_client = get_yookassa_client()
        try:
            yoo_resp = await yoo_client.create_payment(
                amount_kopecks=amount_kopecks,
                idempotence_key=effective_op_id,
                return_url=effective_return_url,
                description=desc,
                customer_email=customer_email,
                metadata=metadata,
            )
        except (YooKassaNetworkError, YooKassaApiError) as err:
            logger.error(
                "Failed to create YooKassa payment for fin_payment id=%s: %s",
                payment.id,
                err,
            )
            payment.status = "canceled"
            await db.flush()
            raise FinValidationError(f"YooKassa provider error: {err}") from err

        payment.provider_payment_id = yoo_resp.get("id")
        conf = yoo_resp.get("confirmation", {})
        payment.confirmation_url = conf.get("confirmation_url")
        payment.receipt_snapshot = yoo_resp.get("receipt")
        await db.flush()

        logger.info(
            "Created FinPayment id=%s provider_payment_id=%s amount=%s RUB",
            payment.id,
            payment.provider_payment_id,
            amount_rubles,
        )
        return payment

    @staticmethod
    async def sync_payment_status(
        db: AsyncSession,
        *,
        payment_id: int | None = None,
        provider_payment_id: str | None = None,
        trigger_source: str = "webhook",
        actor: str = "yookassa_sync",
        correlation_id: str = "",
    ) -> FinPayment:
        """Authoritatively verify and sync payment status from YooKassa and post to ledger if succeeded."""
        if payment_id is None and provider_payment_id is None:
            raise FinValidationError(
                "Either payment_id or provider_payment_id must be provided"
            )

        # 1. Lock payment row
        stmt = select(FinPayment).with_for_update()
        if payment_id is not None:
            stmt = stmt.where(FinPayment.id == payment_id)
        else:
            stmt = stmt.where(FinPayment.provider_payment_id == provider_payment_id)

        res = await db.execute(stmt)
        payment = res.scalar_one_or_none()
        if not payment:
            raise FinValidationError(
                f"FinPayment not found for payment_id={payment_id}, provider_id={provider_payment_id}"
            )

        # 2. Replay-safe check: already succeeded and posted?
        if payment.status == "succeeded" and payment.ledger_transaction_id is not None:
            logger.info(
                "Payment id=%s is already succeeded (tx=%s), skipping duplicate posting",
                payment.id,
                payment.ledger_transaction_id,
            )
            return payment

        target_provider_id = payment.provider_payment_id or provider_payment_id
        if not target_provider_id:
            raise FinValidationError(
                f"FinPayment id={payment.id} has no provider_payment_id"
            )

        # 3. Query authoritative provider state
        yoo_client = get_yookassa_client()
        try:
            remote_payment = await yoo_client.get_payment(target_provider_id)
        except (YooKassaNetworkError, YooKassaApiError) as err:
            logger.error("Failed to query payment status from YooKassa: %s", err)
            raise FinValidationError(
                f"Failed to query YooKassa payment: {err}"
            ) from err

        remote_status = remote_payment.get("status")
        remote_amount = remote_payment.get("amount", {})
        currency = remote_amount.get("currency")
        val_str = remote_amount.get("value", "0")
        try:
            remote_kopecks = round(float(val_str) * 100)
        except ValueError, TypeError:
            remote_kopecks = 0

        # 4. Strict mismatch checks
        if currency != payment.currency or remote_kopecks != payment.amount_kopecks:
            logger.error(
                "CRITICAL: Payment mismatch! id=%s expected %s %s, got %s %s from provider",
                payment.id,
                payment.amount_kopecks,
                payment.currency,
                remote_kopecks,
                currency,
            )
            payment.status = "canceled"
            await db.flush()
            raise FinValidationError(
                "Security mismatch between local payment and provider response"
            )

        # Check metadata tenant match
        meta = remote_payment.get("metadata") or {}
        meta_tenant = meta.get("tenant_id")
        if meta_tenant is not None and str(meta_tenant) != str(payment.tenant_id):
            logger.error(
                "CRITICAL: Tenant mismatch! payment.tenant_id=%s, remote metadata.tenant_id=%s",
                payment.tenant_id,
                meta_tenant,
            )
            payment.status = "canceled"
            await db.flush()
            raise FinTenantIsolationError(
                "Metadata tenant_id does not match payment tenant"
            )

        # Check shop_id if configured
        if settings.yookassa_shop_id:
            recipient = remote_payment.get("recipient") or {}
            recip_account = recipient.get("account_id")
            if recip_account and str(recip_account) != str(settings.yookassa_shop_id):
                logger.error(
                    "CRITICAL: Merchant mismatch! expected=%s, got=%s",
                    settings.yookassa_shop_id,
                    recip_account,
                )
                payment.status = "canceled"
                await db.flush()
                raise FinValidationError(
                    "Merchant mismatch between configuration and payment receipt"
                )

        now_dt = datetime.now(UTC)

        # 5. Handle Succeeded State -> Post to Ledger
        if remote_status == "succeeded":
            system_accs = await FinAccountService.ensure_system_accounts(db)
            clearing_acc = system_accs["payment_clearing"]
            (
                settlement_acc,
                _,
            ) = await FinAccountService.ensure_tenant_settlement_account(
                db, payment.tenant_id
            )

            op_id = f"yookassa_pay_{payment.id}_{target_provider_id}"
            posting_req = FinPostingRequest(
                tenant_id=payment.tenant_id,
                operation_id=op_id,
                kind="payment",
                source_project="MenuBuilder",
                source_type="yookassa_payment",
                source_id=str(payment.id),
                actor=actor,
                correlation_id=correlation_id or op_id,
                entries=[
                    FinPostingEntryRequest(
                        account_id=clearing_acc.id,
                        debit_kopecks=payment.amount_kopecks,
                        credit_kopecks=0,
                    ),
                    FinPostingEntryRequest(
                        account_id=settlement_acc.id,
                        debit_kopecks=0,
                        credit_kopecks=payment.amount_kopecks,
                    ),
                ],
                calculation_snapshot={
                    "payment_id": payment.id,
                    "provider": "yookassa",
                    "provider_payment_id": target_provider_id,
                    "amount_kopecks": payment.amount_kopecks,
                    "amount_rubles": payment.amount_kopecks // 100,
                    "trigger_source": trigger_source,
                },
            )

            tx = await FinPostingService.post_transaction(db, posting_req)

            payment.status = "succeeded"
            payment.provider_payment_id = target_provider_id
            payment.ledger_transaction_id = tx.id
            payment.verified_at = now_dt
            payment.succeeded_at = now_dt

            # Atomic cycle anchor fixation on first successful payment
            await FinBillingCycleService.initialize_anchor_from_payment(
                db,
                tenant_id=payment.tenant_id,
                payment_tx_id=tx.id,
                paid_at=now_dt,
            )

            await db.flush()
            logger.info("Payment id=%s succeeded! Posted tx=%s", payment.id, tx.id)
            return payment

        # 6. Handle Canceled State
        if remote_status == "canceled":
            payment.status = "canceled"
            await db.flush()
            logger.info("Payment id=%s marked as canceled", payment.id)
            return payment

        # 7. Other states (pending, waiting_for_capture)
        payment.status = (
            "waiting_for_capture"
            if remote_status == "waiting_for_capture"
            else "pending"
        )
        await db.flush()
        return payment

    @staticmethod
    async def process_webhook(
        db: AsyncSession,
        payload: dict[str, Any],
        *,
        client_ip: str | None = None,
        webhook_secret: str | None = None,
        correlation_id: str = "",
    ) -> FinPayment:
        """Process incoming YooKassa webhook with signature/source controls and server confirmation."""
        # Source control 1: IP whitelist check if enabled
        if settings.yookassa_ip_filter_enabled and not is_ip_trusted(
            client_ip, settings.yookassa_trusted_ips
        ):
            logger.warning("Rejected webhook from untrusted IP: %s", client_ip)
            raise FinValidationError(f"Untrusted webhook client IP: {client_ip}")

        # Source control 2: Webhook secret check if configured
        if (
            settings.yookassa_webhook_secret
            and webhook_secret != settings.yookassa_webhook_secret
        ):
            logger.warning("Rejected webhook due to invalid webhook secret")
            raise FinValidationError("Invalid webhook secret")

        event_type = payload.get("event")
        obj = payload.get("object") or {}
        provider_payment_id = obj.get("id")

        if not provider_payment_id:
            raise FinValidationError("Webhook payload object has no payment id")

        logger.info(
            "Processing YooKassa webhook event=%s provider_payment_id=%s",
            event_type,
            provider_payment_id,
        )

        return await FinPaymentService.sync_payment_status(
            db,
            provider_payment_id=provider_payment_id,
            trigger_source=f"webhook_{event_type}",
            actor="yookassa_webhook",
            correlation_id=correlation_id,
        )
