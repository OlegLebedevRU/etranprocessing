from __future__ import annotations

import contextlib
import logging
from typing import Any

import httpx

from app.config import settings

logger = logging.getLogger(__name__)


class MediaOrchestratorError(Exception):
    def __init__(
        self,
        message: str,
        status_code: int = 500,
        code: str = "media_error",
        detail: Any = None,
    ) -> None:
        super().__init__(message)
        self.message = message
        self.status_code = status_code
        self.code = code
        self.detail = detail


class MediaSessionConflictError(MediaOrchestratorError):
    def __init__(
        self,
        message: str = "Device busy or media session conflict",
        detail: Any = None,
    ) -> None:
        super().__init__(message, status_code=409, code="session_busy", detail=detail)


class MediaSessionNotFoundError(MediaOrchestratorError):
    def __init__(
        self,
        message: str = "Media session not found",
        detail: Any = None,
    ) -> None:
        super().__init__(
            message, status_code=404, code="session_not_found", detail=detail
        )


class MediaJanusError(MediaOrchestratorError):
    def __init__(
        self,
        message: str = "Janus WebRTC gateway error",
        detail: Any = None,
    ) -> None:
        super().__init__(message, status_code=502, code="janus_error", detail=detail)


class MediaPortExhaustionError(MediaOrchestratorError):
    def __init__(
        self,
        message: str = "Media session table or port pool exhausted",
        detail: Any = None,
    ) -> None:
        super().__init__(
            message, status_code=503, code="port_exhaustion", detail=detail
        )


class MediaOrchestratorClient:
    """Client for on-demand media session lifecycle API on l4media-ingress port 9100

    Conforms to H-L4D-08A-MEDIA-v1 OpenAPI specification:
    - POST /api/v1/media/sessions/start
    - GET /api/v1/media/sessions/{session_id}
    - POST /api/v1/media/sessions/{session_id}/stop
    - POST /api/v1/media/reconcile
    - GET /api/v1/media/metrics
    """

    def __init__(
        self,
        base_url: str | None = None,
        service_token: str | None = None,
        timeout: float = 10.0,
        client: httpx.AsyncClient | None = None,
    ) -> None:
        self.base_url = (
            base_url if base_url is not None else settings.l4media_ingress_url
        ).rstrip("/")
        self.service_token = (
            service_token
            if service_token is not None
            else settings.l4media_effective_token
        )
        self.timeout = timeout
        self._external_client = client

    def _get_headers(self) -> dict[str, str]:
        headers: dict[str, str] = {
            "Accept": "application/json",
            "Content-Type": "application/json",
        }
        if self.service_token:
            headers["X-Media-Service-Token"] = self.service_token
            headers["Authorization"] = f"Bearer {self.service_token}"
        return headers

    def _map_http_error(self, resp: httpx.Response) -> MediaOrchestratorError:
        status_code = resp.status_code
        detail: Any = None
        with contextlib.suppress(Exception):
            detail = resp.json()
        if detail is None:
            detail = resp.text

        msg = f"Media orchestrator error [{status_code}]: {detail}"
        if status_code == 409:
            return MediaSessionConflictError(msg, detail=detail)
        if status_code == 404:
            return MediaSessionNotFoundError(msg, detail=detail)
        if status_code == 502:
            return MediaJanusError(msg, detail=detail)
        if status_code == 503:
            return MediaPortExhaustionError(msg, detail=detail)
        return MediaOrchestratorError(msg, status_code=status_code, detail=detail)

    async def _send_request(
        self,
        method: str,
        path: str,
        params: dict[str, Any] | None = None,
        json_data: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        url = f"{self.base_url}{path}"
        headers = self._get_headers()
        clean_params = {k: v for k, v in (params or {}).items() if v is not None}

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
                    raise MediaOrchestratorError(
                        f"Failed to parse JSON response from {path}: {err}"
                    ) from err

            raise self._map_http_error(resp)
        except httpx.RequestError as exc:
            raise MediaOrchestratorError(
                f"Network transport error calling {path}: {exc}",
                status_code=502,
                code="network_error",
            ) from exc

    async def start_session(
        self,
        session_id: str,
        operation_id: str,
        sn: str,
        device_id: int,
        pin: str | None = None,
        rtp_port: int | None = None,
        rtcp_port: int | None = None,
        ttl_sec: int = 600,
    ) -> dict[str, Any]:
        """Start or replay on-demand media session idempotently."""
        payload = {
            "session_id": session_id,
            "operation_id": operation_id,
            "sn": sn,
            "device_id": device_id,
            "ttl_sec": ttl_sec,
        }
        if pin:
            payload["pin"] = pin
        if rtp_port is not None:
            payload["rtp_port"] = rtp_port
        if rtcp_port is not None:
            payload["rtcp_port"] = rtcp_port

        return await self._send_request(
            method="POST",
            path="/api/v1/media/sessions/start",
            json_data=payload,
        )

    async def get_session_health(self, session_id: str) -> dict[str, Any]:
        """Get media session state, freshness, and streaming counters."""
        return await self._send_request(
            method="GET",
            path=f"/api/v1/media/sessions/{session_id}",
        )

    async def renew_session(self, sn: str) -> dict[str, Any]:
        """Refresh the media watchdog after a verified control lease keepalive."""
        return await self._send_request(
            method="POST",
            path="/api/v1/media/sessions/renew",
            json_data={"sn": sn},
        )

    async def stop_session(
        self,
        session_id: str,
        operation_id: str | None = None,
        reason: str = "user_closed",
    ) -> dict[str, Any]:
        """Stop media session idempotently, pruning mountpoint and route."""
        payload: dict[str, Any] = {"reason": reason}
        if operation_id:
            payload["operation_id"] = operation_id

        return await self._send_request(
            method="POST",
            path=f"/api/v1/media/sessions/{session_id}/stop",
            json_data=payload,
        )

    async def reconcile(self) -> dict[str, Any]:
        """Trigger reconciliation of orphan Janus mountpoints and Ingress routes."""
        return await self._send_request(
            method="POST",
            path="/api/v1/media/reconcile",
        )

    async def get_metrics(self) -> dict[str, Any]:
        """Get aggregate technical and audit metrics."""
        return await self._send_request(
            method="GET",
            path="/api/v1/media/metrics",
        )


media_orchestrator_client = MediaOrchestratorClient()
