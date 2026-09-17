from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

from app.config import settings
from app.services.iot_consumer_storage import (
    IotConsumerStorage,
    get_iot_consumer_storage,
)
from app.services.iot_event_feed_client import (
    IotEventFeedAuthError,
    IotEventFeedClient,
    IotEventFeedError,
    IotEventFeedNetworkError,
    IotEventFeedTimeoutError,
    RemoteSessionEventItem,
    iot_event_feed_client,
)

logger = logging.getLogger(__name__)

# Forbidden commercial keywords in event payloads per invariant:
# "Payloads are validated against commercial field injection (no financial data in IoT feed)"
FORBIDDEN_COMMERCIAL_KEYS = {
    "amount",
    "rubles",
    "kopecks",
    "price",
    "tariff",
    "balance",
    "billing_id",
    "payment_id",
    "invoice_id",
}


@dataclass
class BatchProcessResult:
    processed_count: int
    duplicate_count: int
    quarantine_count: int
    last_cursor: int
    lag: int


@dataclass
class PollResult:
    events_received: int
    processed_count: int
    duplicate_count: int
    quarantine_count: int
    has_more: bool
    checkpoint_cursor: int
    remote_latest_cursor: int
    cursor_lag: int
    last_event_occurred_at: datetime | None


class IotEventConsumer:
    """Consumer for IoT Remote Session Event Feed v1.

    Guarantees:
    - Monotonically increasing cursor checkpointing.
    - Idempotent inbox deduplication by event_id.
    - Transactional processing semantics.
    - Quarantine strictly for contract-defined anomalies (schema mismatch, commercial injection, out-of-order cursor).
    - Real-time lag observability (cursor lag and time lag).
    - Shadow mode: technical ingest only, zero financial or entitlement mutation.
    """

    def __init__(
        self,
        client: IotEventFeedClient | None = None,
        storage: IotConsumerStorage | None = None,
        consumer_id: str | None = None,
    ) -> None:
        self.client = client or iot_event_feed_client
        self.storage = storage or get_iot_consumer_storage()
        self.consumer_id = consumer_id or settings.iot_consumer_id

        # Observability state
        self.last_poll_time: datetime | None = None
        self.remote_latest_cursor: int = 0
        self.total_processed: int = 0
        self.total_duplicates: int = 0
        self.total_quarantined: int = 0
        self.last_error: str | None = None
        self._is_running: bool = False

    def _validate_contract_payload(
        self, event: RemoteSessionEventItem
    ) -> tuple[bool, str]:
        """Check for schema invariants and forbidden commercial fields."""
        if not event.event_id or not event.sn or event.cursor <= 0:
            return (
                False,
                "Missing mandatory contract fields (event_id, sn, or non-positive cursor)",
            )

        # Check payload keys recursively for commercial injection
        def has_commercial_key(obj: Any) -> str | None:
            if isinstance(obj, dict):
                for k, v in obj.items():
                    if str(k).lower() in FORBIDDEN_COMMERCIAL_KEYS:
                        return str(k)
                    sub = has_commercial_key(v)
                    if sub:
                        return sub
            elif isinstance(obj, list):
                for item in obj:
                    sub = has_commercial_key(item)
                    if sub:
                        return sub
            return None

        bad_key = has_commercial_key(event.payload)
        if bad_key:
            return False, f"Forbidden commercial field detected in payload: {bad_key}"

        return True, ""

    async def process_batch(
        self,
        items: list[RemoteSessionEventItem],
        feed_latest_cursor: int | None = None,
    ) -> BatchProcessResult:
        """Process a batch of events with deduplication, checkpoint advancement, and quarantine."""
        cp = await self.storage.get_checkpoint(self.consumer_id)
        current_cursor = cp.last_cursor
        last_occurred_at = cp.last_event_occurred_at
        last_event_id = cp.last_event_id

        processed_count = 0
        duplicate_count = 0
        quarantine_count = 0

        prev_item_cursor = current_cursor

        for item in items:
            raw_event = item.model_dump(mode="json")

            # 1. Contract schema & commercial field validation
            valid, reason = self._validate_contract_payload(item)
            if not valid:
                logger.warning(
                    "Quarantining event %s (cursor %d): %s",
                    item.event_id,
                    item.cursor,
                    reason,
                )
                await self.storage.save_quarantine(
                    event_id=item.event_id,
                    cursor=item.cursor,
                    error_code="CONTRACT_VALIDATION_FAILED",
                    error_detail=reason,
                    raw_event=raw_event,
                )
                quarantine_count += 1
                self.total_quarantined += 1
                continue

            # 2. Sequence & duplicate checking
            if item.cursor <= current_cursor:
                # Check if this exact event_id is already in inbox
                if await self.storage.has_event(item.event_id):
                    # Idempotent duplicate delivery (safe to ignore)
                    duplicate_count += 1
                    self.total_duplicates += 1
                    logger.debug(
                        "Idempotent duplicate event %s (cursor %d) ignored",
                        item.event_id,
                        item.cursor,
                    )
                    continue

                # Not in inbox, but cursor <= current_cursor -> Out of order cursor anomaly!
                detail = (
                    f"Out-of-order cursor anomaly: item cursor {item.cursor} <= "
                    f"checkpoint cursor {current_cursor} for unknown event {item.event_id}"
                )
                logger.error(detail)
                await self.storage.save_quarantine(
                    event_id=item.event_id,
                    cursor=item.cursor,
                    error_code="OUT_OF_ORDER_CURSOR",
                    error_detail=detail,
                    raw_event=raw_event,
                )
                quarantine_count += 1
                self.total_quarantined += 1
                continue

            # 3. Check for non-monotonic sequence within current batch
            if item.cursor <= prev_item_cursor:
                detail = (
                    f"Batch non-monotonic anomaly: cursor {item.cursor} <= "
                    f"previous item cursor {prev_item_cursor}"
                )
                logger.error(detail)
                await self.storage.save_quarantine(
                    event_id=item.event_id,
                    cursor=item.cursor,
                    error_code="OUT_OF_ORDER_CURSOR",
                    error_detail=detail,
                    raw_event=raw_event,
                )
                quarantine_count += 1
                self.total_quarantined += 1
                continue

            # 4. Check for duplicate event_id with future cursor
            if await self.storage.has_event(item.event_id):
                duplicate_count += 1
                self.total_duplicates += 1
                logger.info(
                    "Duplicate event_id %s received with advanced cursor %d; skipping duplicate insert",
                    item.event_id,
                    item.cursor,
                )
                # Advance cursor since stream progressed
                current_cursor = item.cursor
                prev_item_cursor = item.cursor
                last_event_id = item.event_id
                last_occurred_at = item.occurred_at
                await self.storage.update_checkpoint(
                    self.consumer_id, current_cursor, last_event_id, last_occurred_at
                )
                continue

            # 5. Save technical projection to inbox
            saved = await self.storage.save_inbox_event(item, status="processed")
            if saved:
                processed_count += 1
                self.total_processed += 1
                current_cursor = item.cursor
                prev_item_cursor = item.cursor
                last_event_id = item.event_id
                last_occurred_at = item.occurred_at

                # Transactionally update checkpoint
                await self.storage.update_checkpoint(
                    self.consumer_id, current_cursor, last_event_id, last_occurred_at
                )

        if feed_latest_cursor is not None:
            self.remote_latest_cursor = max(
                self.remote_latest_cursor, feed_latest_cursor
            )

        lag = max(0, self.remote_latest_cursor - current_cursor)

        return BatchProcessResult(
            processed_count=processed_count,
            duplicate_count=duplicate_count,
            quarantine_count=quarantine_count,
            last_cursor=current_cursor,
            lag=lag,
        )

    async def poll_once(
        self,
        batch_size: int | None = None,
        correlation_id: str | None = None,
    ) -> PollResult:
        """Execute one polling cycle against the IoT event feed."""
        limit = batch_size or settings.iot_consumer_batch_size
        self.last_poll_time = datetime.now(UTC)

        cp = await self.storage.get_checkpoint(self.consumer_id)
        start_cursor = cp.last_cursor

        try:
            page = await self.client.get_event_feed(
                after=start_cursor,
                limit=limit,
                correlation_id=correlation_id,
            )
            self.last_error = None
        except (
            IotEventFeedAuthError,
            IotEventFeedTimeoutError,
            IotEventFeedNetworkError,
            IotEventFeedError,
        ) as exc:
            self.last_error = str(exc)
            logger.warning("IoT event consumer poll failed: %s", exc)
            raise

        remote_max = page.next_cursor
        if page.items:
            remote_max = max(remote_max, max(it.cursor for it in page.items))
        if page.total_count > 0:
            remote_max = max(remote_max, page.total_count)

        self.remote_latest_cursor = max(self.remote_latest_cursor, remote_max)

        batch_res = await self.process_batch(
            items=page.items,
            feed_latest_cursor=self.remote_latest_cursor,
        )

        lag = max(0, self.remote_latest_cursor - batch_res.last_cursor)

        updated_cp = await self.storage.get_checkpoint(self.consumer_id)

        return PollResult(
            events_received=len(page.items),
            processed_count=batch_res.processed_count,
            duplicate_count=batch_res.duplicate_count,
            quarantine_count=batch_res.quarantine_count,
            has_more=page.has_more,
            checkpoint_cursor=updated_cp.last_cursor,
            remote_latest_cursor=self.remote_latest_cursor,
            cursor_lag=lag,
            last_event_occurred_at=updated_cp.last_event_occurred_at,
        )

    async def poll_until_caught_up(
        self,
        max_pages: int = 50,
        batch_size: int | None = None,
    ) -> list[PollResult]:
        """Poll continuously until feed reports has_more == False or max_pages reached."""
        results: list[PollResult] = []
        for _ in range(max_pages):
            res = await self.poll_once(batch_size=batch_size)
            results.append(res)
            if not res.has_more or res.events_received == 0:
                break
        return results

    async def get_metrics(self) -> dict[str, Any]:
        """Produce real-time consumer and lag observability metrics."""
        cp = await self.storage.get_checkpoint(self.consumer_id)
        counts = await self.storage.get_counts()

        now = datetime.now(UTC)
        time_lag_sec: float | None = None
        if cp.last_event_occurred_at:
            time_lag_sec = max(0.0, (now - cp.last_event_occurred_at).total_seconds())

        cursor_lag = max(0, self.remote_latest_cursor - cp.last_cursor)

        return {
            "consumer_id": self.consumer_id,
            "enabled": settings.iot_consumer_enabled,
            "shadow_mode": settings.iot_consumer_shadow_mode,
            "checkpoint_cursor": cp.last_cursor,
            "remote_latest_cursor": self.remote_latest_cursor,
            "cursor_lag": cursor_lag,
            "last_event_id": cp.last_event_id,
            "last_event_occurred_at": (
                cp.last_event_occurred_at.isoformat()
                if cp.last_event_occurred_at
                else None
            ),
            "time_lag_seconds": time_lag_sec,
            "last_poll_time": (
                self.last_poll_time.isoformat() if self.last_poll_time else None
            ),
            "total_processed": self.total_processed,
            "total_duplicates": self.total_duplicates,
            "total_quarantined": self.total_quarantined,
            "inbox_count": counts.get("inbox", 0),
            "quarantine_count": counts.get("quarantine", 0),
            "last_error": self.last_error,
        }

    async def run_worker(self) -> None:
        """Background worker loop honoring feature flags and graceful shutdown."""
        if not settings.iot_consumer_enabled:
            logger.info(
                "IoT event consumer is disabled (dark consumer flag); background worker exiting"
            )
            return

        logger.info(
            "Starting IoT event consumer background worker (consumer_id=%s, shadow_mode=%s)",
            self.consumer_id,
            settings.iot_consumer_shadow_mode,
        )
        self._is_running = True

        while self._is_running:
            try:
                if not settings.event_feed_effective_url:
                    await asyncio.sleep(settings.iot_consumer_poll_interval_sec)
                    continue

                res = await self.poll_once()
                if res.events_received > 0:
                    logger.debug(
                        "IoT consumer processed %d events (duplicates=%d, quarantine=%d, lag=%d)",
                        res.processed_count,
                        res.duplicate_count,
                        res.quarantine_count,
                        res.cursor_lag,
                    )
                # Sleep between polling cycles
                await asyncio.sleep(settings.iot_consumer_poll_interval_sec)
            except asyncio.CancelledError:
                logger.info(
                    "IoT consumer worker received cancellation request; shutting down"
                )
                self._is_running = False
                break
            except Exception as exc:  # noqa: BLE001
                self.last_error = str(exc)
                logger.warning(
                    "IoT consumer worker iteration error: %s; backing off", exc
                )
                await asyncio.sleep(settings.iot_consumer_poll_interval_sec)


iot_event_consumer = IotEventConsumer()
