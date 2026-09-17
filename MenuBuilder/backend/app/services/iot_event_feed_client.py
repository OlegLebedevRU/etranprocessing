from __future__ import annotations

import asyncio
import contextlib
import logging
from datetime import datetime
from typing import Any

import httpx
from pydantic import BaseModel, ConfigDict, Field

from app.config import settings

logger = logging.getLogger(__name__)

CONTRACT_VERSION = "1.0.0"
SCHEMA_REVISION = "2026-09-17-v1"


# --- Exceptions with Explicit Contract Mapping ---


class IotEventFeedError(Exception):
    """Base exception for all IoT event feed client operations."""

    def __init__(
        self, message: str, status_code: int | None = None, detail: Any = None
    ) -> None:
        super().__init__(message)
        self.message = message
        self.status_code = status_code
        self.detail = detail


class IotEventFeedAuthError(IotEventFeedError):
    """Authentication or authorization failure (HTTP 401 / 403)."""


class IotEventFeedValidationError(IotEventFeedError):
    """Contract query or payload validation failure (HTTP 400 / 422)."""


class IotEventFeedNotFoundError(IotEventFeedError):
    """Endpoint or resource not found (HTTP 404)."""


class IotEventFeedServerError(IotEventFeedError):
    """Provider internal server error (HTTP 500 / 502 / 503 / 504)."""


class IotEventFeedNetworkError(IotEventFeedError):
    """Network connection failure or socket error."""


class IotEventFeedTimeoutError(IotEventFeedNetworkError):
    """Request timed out."""


class IotEventFeedContractError(IotEventFeedError):
    """Response violated expected contract schema or invariant."""


# --- Contract Data Models (v1) ---


class RemoteSessionEventItem(BaseModel):
    """Immutable session fact event adhering to remote_session_event.schema.json."""

    model_config = ConfigDict(extra="ignore")

    cursor: int = Field(..., description="Monotonically increasing sequence cursor")
    event_id: str = Field(..., description="Unique event identifier")
    occurred_at: datetime = Field(..., description="UTC timestamp of occurrence")
    event_type: str = Field(..., description="Event type name")
    event_version: str = Field(default="1.0.0", description="Event contract version")
    tenant_id: int | None = Field(default=None, description="Tenant / Organization ID")
    terminal_id: str | None = Field(default=None, description="Terminal identifier")
    device_id: int | None = Field(default=None, description="Device internal ID")
    sn: str = Field(..., description="Device serial number")
    session_id: str | None = Field(default=None, description="Session ID if applicable")
    session_type: str | None = Field(default=None, description="console or video")
    lifecycle_state: str | None = Field(
        default=None, description="Lifecycle state of session"
    )
    reason: str | None = Field(default=None, description="Reason or status code")
    operation_id: str | None = Field(
        default=None, description="Operation ID for idempotency"
    )
    correlation_id: str | None = Field(
        default=None, description="End-to-end correlation ID"
    )
    payload: dict[str, Any] = Field(
        default_factory=dict, description="Immutable event payload"
    )
    created_at: datetime = Field(..., description="Database record creation timestamp")


class EventFeedPage(BaseModel):
    """Paginated event feed response from GET /api/internal/v1/remote-session-events."""

    model_config = ConfigDict(extra="ignore")

    items: list[RemoteSessionEventItem] = Field(default_factory=list)
    next_cursor: int = Field(default=0)
    has_more: bool = Field(default=False)
    total_count: int = Field(default=0)
    server_time: datetime


class ReconciliationResponse(BaseModel):
    """Reconciliation metrics response from GET /api/internal/v1/remote-session-events/reconciliation."""

    model_config = ConfigDict(extra="ignore")

    total_events: int = Field(default=0)
    min_cursor: int | None = None
    max_cursor: int | None = None
    events_by_type: dict[str, int] = Field(default_factory=dict)
    active_sessions_count: int = Field(default=0)
    sessions_by_status: dict[str, int] = Field(default_factory=dict)
    feed_sha256: str = Field(default="")
    server_time: datetime
    tenant_id: int | None = None
    from_cursor: int | None = None
    to_cursor: int | None = None


# --- Versioned Async Client ---


class IotEventFeedClient:
    """Versioned async client for IoT Event Feed Contract v1.

    Adheres strictly to iot_event_feed_contract_v1:
    - Version: 1.0.0
    - Transport: internal_rest_json
    - Endpoints:
        GET /api/internal/v1/remote-session-events
        GET /api/internal/v1/remote-session-events/reconciliation
        POST /api/internal/v1/remote-sessions
        GET /api/internal/v1/remote-sessions/{session_id}
        POST /api/internal/v1/remote-sessions/{session_id}/stop
    - Auth: X-Internal-Service-Key & Authorization Bearer
    - Bounded retries and timeouts
    - Explicit contract error mapping
    """

    def __init__(
        self,
        base_url: str | None = None,
        service_token: str | None = None,
        timeout: float | None = None,
        max_retries: int | None = None,
        retry_backoff_sec: float | None = None,
        client: httpx.AsyncClient | None = None,
    ) -> None:
        self.base_url = (
            base_url if base_url is not None else settings.event_feed_effective_url
        ).rstrip("/")
        self.service_token = (
            service_token
            if service_token is not None
            else settings.event_feed_effective_token
        )
        self.timeout = (
            timeout if timeout is not None else settings.iot_event_feed_timeout_seconds
        )
        self.max_retries = (
            max_retries
            if max_retries is not None
            else settings.iot_event_feed_max_retries
        )
        self.retry_backoff_sec = (
            retry_backoff_sec
            if retry_backoff_sec is not None
            else settings.iot_event_feed_retry_backoff_sec
        )
        self._external_client = client

    def _get_headers(self, correlation_id: str | None = None) -> dict[str, str]:
        headers: dict[str, str] = {
            "Accept": "application/json",
            "Content-Type": "application/json",
            "X-Contract-Version": CONTRACT_VERSION,
        }
        if self.service_token:
            headers["X-Internal-Service-Key"] = self.service_token
            headers["Authorization"] = f"Bearer {self.service_token}"
        if correlation_id:
            headers["X-Correlation-Id"] = correlation_id
        return headers

    def _map_http_error(self, resp: httpx.Response) -> IotEventFeedError:
        status_code = resp.status_code
        detail: Any = None
        with contextlib.suppress(Exception):
            detail = resp.json()
        if detail is None:
            detail = resp.text

        msg = f"IoT event feed error [{status_code}]: {detail}"
        if status_code in (401, 403):
            return IotEventFeedAuthError(msg, status_code=status_code, detail=detail)
        if status_code in (400, 422):
            return IotEventFeedValidationError(
                msg, status_code=status_code, detail=detail
            )
        if status_code == 404:
            return IotEventFeedNotFoundError(
                msg, status_code=status_code, detail=detail
            )
        if 500 <= status_code <= 599:
            return IotEventFeedServerError(msg, status_code=status_code, detail=detail)
        return IotEventFeedError(msg, status_code=status_code, detail=detail)

    async def _send_request(
        self,
        method: str,
        path: str,
        params: dict[str, Any] | None = None,
        json_data: dict[str, Any] | None = None,
        correlation_id: str | None = None,
    ) -> dict[str, Any]:
        url = f"{self.base_url}{path}"
        headers = self._get_headers(correlation_id=correlation_id)

        clean_params = {k: v for k, v in (params or {}).items() if v is not None}

        last_exc: Exception | None = None
        for attempt in range(self.max_retries + 1):
            try:
                if self._external_client is not None:
                    resp = await self._external_client.request(
                        method=method,
                        url=url,
                        params=clean_params,
                        json=json_data,
                        headers=headers,
                        timeout=self.timeout,
                    )
                else:
                    async with httpx.AsyncClient(timeout=self.timeout) as client:
                        resp = await client.request(
                            method=method,
                            url=url,
                            params=clean_params,
                            json=json_data,
                            headers=headers,
                        )

                if resp.is_success:
                    try:
                        return resp.json()
                    except Exception as err:
                        raise IotEventFeedContractError(
                            f"Failed to parse JSON response from {path}: {err}"
                        ) from err

                error = self._map_http_error(resp)

                # Do NOT retry 4xx errors
                if isinstance(
                    error,
                    (
                        IotEventFeedAuthError,
                        IotEventFeedValidationError,
                        IotEventFeedNotFoundError,
                    ),
                ):
                    raise error

                # Retry on 5xx if bounded attempts remain
                if attempt < self.max_retries and isinstance(
                    error, IotEventFeedServerError
                ):
                    delay = self.retry_backoff_sec * (2**attempt)
                    logger.warning(
                        "IoT event feed %s returned %d; retrying in %.2fs (attempt %d/%d)",
                        path,
                        resp.status_code,
                        delay,
                        attempt + 1,
                        self.max_retries,
                    )
                    await asyncio.sleep(delay)
                    last_exc = error
                    continue

                raise error

            except (
                httpx.ConnectTimeout,
                httpx.ReadTimeout,
                httpx.WriteTimeout,
                httpx.PoolTimeout,
            ) as err:
                last_exc = IotEventFeedTimeoutError(f"IoT request timed out: {err}")
                if attempt < self.max_retries:
                    delay = self.retry_backoff_sec * (2**attempt)
                    logger.warning(
                        "IoT request timed out on %s; retrying in %.2fs (attempt %d/%d)",
                        path,
                        delay,
                        attempt + 1,
                        self.max_retries,
                    )
                    await asyncio.sleep(delay)
                    continue
                raise last_exc from err

            except (httpx.ConnectError, httpx.NetworkError) as err:
                last_exc = IotEventFeedNetworkError(
                    f"IoT network error on {path}: {err}"
                )
                if attempt < self.max_retries:
                    delay = self.retry_backoff_sec * (2**attempt)
                    logger.warning(
                        "IoT network error on %s; retrying in %.2fs (attempt %d/%d)",
                        path,
                        delay,
                        attempt + 1,
                        self.max_retries,
                    )
                    await asyncio.sleep(delay)
                    continue
                raise last_exc from err

        if last_exc:
            raise last_exc
        raise IotEventFeedError("Unexpected failure executing IoT request")

    async def get_event_feed(
        self,
        after: int = 0,
        limit: int = 100,
        tenant_id: int | None = None,
        sn: str | None = None,
        session_id: str | None = None,
        event_type: str | None = None,
        correlation_id: str | None = None,
    ) -> EventFeedPage:
        """Fetch remote session event feed items strictly after `after` cursor."""
        params = {
            "after": after,
            "limit": limit,
            "tenant_id": tenant_id,
            "sn": sn,
            "session_id": session_id,
            "event_type": event_type,
        }
        data = await self._send_request(
            method="GET",
            path="/api/internal/v1/remote-session-events",
            params=params,
            correlation_id=correlation_id,
        )
        try:
            return EventFeedPage.model_validate(data)
        except Exception as err:
            raise IotEventFeedContractError(
                f"IoT event feed payload failed schema validation: {err}"
            ) from err

    async def get_reconciliation(
        self,
        tenant_id: int | None = None,
        from_cursor: int | None = None,
        to_cursor: int | None = None,
        from_time: datetime | str | None = None,
        to_time: datetime | str | None = None,
        correlation_id: str | None = None,
    ) -> ReconciliationResponse:
        """Fetch reconciliation metrics for auditing lag and completeness."""
        f_time = from_time.isoformat() if isinstance(from_time, datetime) else from_time
        t_time = to_time.isoformat() if isinstance(to_time, datetime) else to_time

        params = {
            "tenant_id": tenant_id,
            "from_cursor": from_cursor,
            "to_cursor": to_cursor,
            "from_time": f_time,
            "to_time": t_time,
        }
        data = await self._send_request(
            method="GET",
            path="/api/internal/v1/remote-session-events/reconciliation",
            params=params,
            correlation_id=correlation_id,
        )
        try:
            return ReconciliationResponse.model_validate(data)
        except Exception as err:
            raise IotEventFeedContractError(
                f"Reconciliation response failed schema validation: {err}"
            ) from err

    async def create_remote_session(
        self,
        operation_id: str,
        sn: str,
        session_type: str,
        tenant_id: int | None = None,
        terminal_id: str | None = None,
        requested_by_user_id: str | None = None,
        correlation_id: str | None = None,
        session_metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Create remote session idempotently by operation_id."""
        payload = {
            "operation_id": operation_id,
            "contract_version": CONTRACT_VERSION,
            "tenant_id": tenant_id,
            "terminal_id": terminal_id,
            "sn": sn,
            "session_type": session_type,
            "requested_by_user_id": requested_by_user_id,
            "correlation_id": correlation_id,
            "session_metadata": session_metadata or {},
        }
        return await self._send_request(
            method="POST",
            path="/api/internal/v1/remote-sessions",
            json_data=payload,
            correlation_id=correlation_id,
        )

    async def get_remote_session(
        self, session_id: str, correlation_id: str | None = None
    ) -> dict[str, Any]:
        """Get remote session state by session_id."""
        return await self._send_request(
            method="GET",
            path=f"/api/internal/v1/remote-sessions/{session_id}",
            correlation_id=correlation_id,
        )

    async def stop_remote_session(
        self,
        session_id: str,
        operation_id: str | None = None,
        reason: str = "user_requested",
        correlation_id: str | None = None,
    ) -> dict[str, Any]:
        """Stop remote session idempotently."""
        payload = {
            "operation_id": operation_id,
            "reason": reason,
            "correlation_id": correlation_id,
        }
        return await self._send_request(
            method="POST",
            path=f"/api/internal/v1/remote-sessions/{session_id}/stop",
            json_data=payload,
            correlation_id=correlation_id,
        )


iot_event_feed_client = IotEventFeedClient()
