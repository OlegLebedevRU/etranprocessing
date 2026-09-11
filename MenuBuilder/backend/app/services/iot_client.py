import contextlib
import logging
from datetime import UTC, datetime
from typing import Any

import httpx
from fastapi import HTTPException, status

from app.config import settings

logger = logging.getLogger(__name__)


def mask_api_key(api_key: str) -> str:
    """Mask sensitive API key for safe UI presentation while preserving prefix/suffix hints."""
    if not api_key:
        return ""
    length = len(api_key)
    if length <= 8:
        return "*" * length
    prefix = api_key[:4]
    suffix = api_key[-4:]
    mask_len = max(4, length - 8)
    return f"{prefix}{'*' * mask_len}{suffix}"


class IotPlatformClient:
    def __init__(
        self,
        base_url: str | None = None,
        service_token: str | None = None,
        timeout: float | None = None,
    ):
        self.base_url = (
            base_url if base_url is not None else settings.internal_api_base_url
        ).rstrip("/")
        self.service_token = (
            service_token
            if service_token is not None
            else settings.internal_service_key_value
        )
        self.timeout = timeout or settings.iot_rpc_timeout_seconds
        self._simulated_api_keys: dict[int, dict[str, Any]] = {}

    def _get_headers(
        self, org_id: int | None = None, *, user: dict[str, Any] | None = None
    ) -> dict[str, str]:
        headers = {"Content-Type": "application/json"}
        if self.service_token:
            headers["X-Internal-Service-Key"] = self.service_token
        if org_id is not None:
            headers["X-Org-Id"] = str(org_id)
        if user:
            if user.get("role"):
                headers["X-Role"] = str(user["role"])
            if "role_id" in user and user["role_id"] is not None:
                headers["X-Role-Id"] = str(user["role_id"])
            user_sub = user.get("sub") or user.get("username") or user.get("user_id")
            if user_sub is not None:
                headers["X-User-Id"] = str(user_sub)
            session_id = user.get("session_id") or user.get("sid") or user.get("jti")
            if session_id:
                headers["X-Session-Id"] = str(session_id)
        return headers

    async def provision_terminal(
        self,
        device_id: int,
        sn: str,
        org_id: int,
        name: str | None = None,
        tags: dict[str, str] | None = None,
    ) -> dict[str, Any]:
        """Call Leo4 IoT Platform provisioning API for a single terminal."""
        if not self.base_url:
            logger.info(
                "iot_rpc_base_url is not configured; returning simulated success for terminal %d",
                device_id,
            )
            return {
                "device_id": device_id,
                "sn": sn,
                "org_id": org_id,
                "success": True,
                "rmq_user_status": "simulated",
                "is_online": False,
                "connected_at": None,
                "error": None,
            }

        url = f"{self.base_url}/api/internal/v1/provisioning/terminals"
        payload = {
            "device_id": device_id,
            "sn": sn,
            "org_id": org_id,
            "name": name,
            "tags": tags or {"source": "etranprocessing"},
        }
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(
                url, json=payload, headers=self._get_headers(org_id=org_id)
            )
            resp.raise_for_status()
            return resp.json()

    async def provision_batch_terminals(
        self,
        terminals: list[dict[str, Any]],
    ) -> list[dict[str, Any]]:
        """Call Leo4 IoT Platform batch provisioning API."""
        if not terminals:
            return []
        if not self.base_url:
            return [
                {
                    "device_id": t["device_id"],
                    "sn": t["sn"],
                    "org_id": t["org_id"],
                    "success": True,
                    "rmq_user_status": "simulated",
                    "is_online": False,
                    "connected_at": None,
                    "error": None,
                }
                for t in terminals
            ]

        url = f"{self.base_url}/api/internal/v1/provisioning/terminals/batch"
        payload = {"terminals": terminals}
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(url, json=payload, headers=self._get_headers())
            resp.raise_for_status()
            return resp.json().get("results", [])

    async def get_terminals_status(
        self,
        device_ids: list[int],
    ) -> list[dict[str, Any]]:
        """Query status for a list of device IDs from Leo4 IoT Platform."""
        if not device_ids:
            return []
        if not self.base_url:
            return []

        url = f"{self.base_url}/api/internal/v1/provisioning/terminals/status"
        payload = {"device_ids": device_ids}
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(url, json=payload, headers=self._get_headers())
            resp.raise_for_status()
            return resp.json().get("statuses", [])

    async def provision_api_key(
        self,
        org_id: int,
        api_key: str,
        name: str | None = None,
        is_active: bool = True,
    ) -> dict[str, Any]:
        """Call Leo4 IoT Platform API key provisioning endpoint (Upsert / Rotate)."""
        now_iso = datetime.now(UTC).isoformat()
        if not self.base_url:
            logger.info(
                "iot_rpc_base_url is not configured; storing simulated API key for org %d",
                org_id,
            )
            created_at = self._simulated_api_keys.get(org_id, {}).get(
                "created_at", now_iso
            )
            record = {
                "org_id": org_id,
                "api_key": api_key,
                "name": name,
                "is_active": is_active,
                "created_at": created_at,
                "updated_at": now_iso,
            }
            self._simulated_api_keys[org_id] = record
            return record

        url = f"{self.base_url}/api/internal/v1/provisioning/api-keys"
        payload = {
            "org_id": org_id,
            "api_key": api_key,
            "name": name,
            "is_active": is_active,
        }
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(
                url, json=payload, headers=self._get_headers(org_id=org_id)
            )
            if resp.status_code == 409:
                err_detail = "API key is already assigned to another organization"
                with contextlib.suppress(Exception):
                    err_detail = resp.json().get("detail", err_detail)
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=err_detail,
                )
            if resp.status_code == 403:
                raise HTTPException(
                    status_code=status.HTTP_403_FORBIDDEN,
                    detail="Internal service authentication failed on IoT platform",
                )
            resp.raise_for_status()
            return resp.json()

    async def get_api_key(
        self,
        org_id: int,
        mask: bool = False,
    ) -> dict[str, Any] | None:
        """Fetch API key details for an organization from Leo4 IoT Platform."""
        if not self.base_url:
            rec = self._simulated_api_keys.get(org_id)
            if not rec:
                return None
            res = dict(rec)
            if mask and res.get("api_key"):
                res["api_key"] = mask_api_key(str(res["api_key"]))
            return res

        url = f"{self.base_url}/api/internal/v1/provisioning/api-keys/{org_id}"
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(
                url,
                params={"mask": "true" if mask else "false"},
                headers=self._get_headers(org_id=org_id),
            )
            if resp.status_code == 404:
                return None
            resp.raise_for_status()
            return resp.json()

    async def delete_api_key(
        self,
        org_id: int,
    ) -> dict[str, Any] | None:
        """Delete / revoke API key for an organization on Leo4 IoT Platform."""
        if not self.base_url:
            removed = self._simulated_api_keys.pop(org_id, None)
            if not removed:
                return None
            return {"status": "deleted", "org_id": org_id}

        url = f"{self.base_url}/api/internal/v1/provisioning/api-keys/{org_id}"
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.delete(url, headers=self._get_headers(org_id=org_id))
            if resp.status_code == 404:
                return None
            resp.raise_for_status()
            return resp.json()

    @staticmethod
    def _handle_app1_http_error(exc: httpx.HTTPStatusError) -> None:
        status_code = exc.response.status_code
        body: Any = None
        with contextlib.suppress(Exception):
            body = exc.response.json()

        if status_code in (
            status.HTTP_403_FORBIDDEN,
            status.HTTP_409_CONFLICT,
            status.HTTP_504_GATEWAY_TIMEOUT,
        ):
            if isinstance(body, dict):
                inner_detail = body.get("detail")
                if isinstance(inner_detail, dict):
                    detail = inner_detail
                elif isinstance(inner_detail, str):
                    detail = {"code": inner_detail, "detail": inner_detail}
                elif "code" in body:
                    detail = body
                else:
                    detail = body
            elif isinstance(body, str):
                detail = {"code": body, "detail": body}
            else:
                detail = exc.response.text or "Error from iot-rpc-rest-app"
            raise HTTPException(
                status_code=status_code,
                detail=detail,
            )

        if status_code == status.HTTP_404_NOT_FOUND:
            if isinstance(body, dict):
                code = body.get("code") or "lease_not_found"
                inner_detail = body.get("detail")
                msg = (
                    inner_detail
                    if isinstance(inner_detail, str)
                    else "Resource or lease not found on app1"
                )
                detail = {"code": code, "detail": msg}
                if "lease_id" in body:
                    detail["lease_id"] = body["lease_id"]
                if "generation" in body:
                    detail["generation"] = body["generation"]
            else:
                detail = {
                    "code": "lease_not_found",
                    "detail": "Resource or lease not found on app1",
                }
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=detail,
            )

        if status_code == status.HTTP_429_TOO_MANY_REQUESTS:
            detail = "Слишком частые действия"
            if isinstance(body, dict) and "detail" in body:
                detail = body["detail"]
            raise HTTPException(
                status_code=status.HTTP_429_TOO_MANY_REQUESTS,
                detail=detail,
            )

        if status_code >= 500:
            logger.error("app1 internal error: %d %s", status_code, exc.response.text)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Ошибка сервиса управления iot-rpc-rest-app: {status_code}",
            )

        detail = body if body is not None else exc.response.text
        if isinstance(body, dict) and "detail" in body:
            detail = body["detail"]
        raise HTTPException(status_code=status_code, detail=detail)

    async def remote_input_status(
        self,
        sn: str,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Fetch remote input status (agent presence & lease) from app1."""
        if not self.base_url or not settings.remote_control_enabled:
            return {
                "sn": sn,
                "agent": {
                    "online": False,
                    "desktop_available": False,
                    "screen": None,
                    "last_seen_at": None,
                    "stale": True,
                },
                "lease": {
                    "active": False,
                    "lease_id": None,
                    "owner_user_id": None,
                    "expires_at": None,
                },
            }

        url = f"{self.base_url}/api/internal/v1/remote-input/devices/{sn}/status"
        headers = self._get_headers(org_id=org_id, user=user)
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.get(url, headers=headers)
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to get remote input status for %s: %s", sn, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_acquire_lease(
        self,
        sn: str,
        scope: str = "input",
        ttl_sec: int | None = None,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Acquire a remote control lease on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = f"{self.base_url}/api/internal/v1/remote-input/devices/{sn}/lease"
        headers = self._get_headers(org_id=org_id, user=user)
        owner_user_id = str(user.get("sub", "") if user else "")
        owner_role = str(user.get("role", "") if user else "")
        payload: dict[str, Any] = {
            "scope": scope,
            "owner_user_id": owner_user_id,
            "owner_role": owner_role,
        }
        if ttl_sec is not None:
            payload["ttl_sec"] = ttl_sec
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(url, json=payload, headers=headers)
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to acquire lease for %s: %s", sn, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_change_scope(
        self,
        lease_id: str,
        scope: str,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Change lease scope on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}/scope"
        headers = self._get_headers(org_id=org_id, user=user)
        payload = {"scope": scope}
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(url, json=payload, headers=headers)
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to change lease scope %s: %s", lease_id, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_release_by_owner(
        self,
        user_id: str,
        session_id: str | None = None,
    ) -> dict[str, Any]:
        """Release all leases owned by user on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            return {"released": 0}

        url = f"{self.base_url}/api/internal/v1/remote-input/leases/by-owner"
        headers = {"Content-Type": "application/json"}
        if self.service_token:
            headers["X-Internal-Service-Key"] = self.service_token
        payload: dict[str, Any] = {"user_id": str(user_id)}
        if session_id:
            payload["session_id"] = str(session_id)
        timeout = min(self.timeout, 2.0)
        try:
            async with httpx.AsyncClient(timeout=timeout) as client:
                resp = await client.request(
                    "DELETE", url, json=payload, headers=headers
                )
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.warning(
                "Failed to release leases by owner %s (best-effort): %s",
                user_id,
                exc,
            )
            return {"released": 0}

    async def remote_input_inventory(
        self,
        sn: str,
        refresh: int = 0,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Fetch device inventory from app1."""
        if not self.base_url or not settings.remote_control_enabled:
            return {"displays": [], "cameras": []}

        url = f"{self.base_url}/api/internal/v1/remote-input/devices/{sn}/inventory"
        headers = self._get_headers(org_id=org_id, user=user)
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.get(
                    url, params={"refresh": refresh}, headers=headers
                )
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to get inventory for %s: %s", sn, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_stream_start(
        self,
        lease_id: str,
        mode: str,
        source_id: str,
        profile: str = "default",
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Start or switch stream on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}/stream/start"
        headers = self._get_headers(org_id=org_id, user=user)
        payload = {"mode": mode, "source_id": str(source_id), "profile": profile}
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(url, json=payload, headers=headers)
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to start stream for lease %s: %s", lease_id, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_stream_stop(
        self,
        lease_id: str,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Stop stream on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = (
            f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}/stream/stop"
        )
        headers = self._get_headers(org_id=org_id, user=user)
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(url, headers=headers)
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to stop stream for lease %s: %s", lease_id, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_key(
        self,
        lease_id: str,
        kind: str,
        vk: int,
        text: str | None = None,
        client_ref: str | None = None,
        desktop_id: str | None = None,
        stream_instance_id: str | None = None,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Send keyboard event on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}/key"
        headers = self._get_headers(org_id=org_id, user=user)
        payload: dict[str, Any] = {
            "kind": kind,
            "vk": vk,
        }
        if text is not None:
            payload["text"] = text
        if client_ref is not None:
            payload["client_ref"] = client_ref
        if desktop_id is not None:
            payload["desktop_id"] = desktop_id
        if stream_instance_id is not None:
            payload["stream_instance_id"] = stream_instance_id

        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(url, json=payload, headers=headers)
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to send key for lease %s: %s", lease_id, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_keepalive(
        self,
        lease_id: str,
        generation: int | None = None,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Send keepalive for lease on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}/keepalive"
        headers = self._get_headers(org_id=org_id, user=user)
        payload = {}
        if generation is not None:
            payload["generation"] = generation
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(
                    url, json=payload if payload else None, headers=headers
                )
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to keepalive lease %s: %s", lease_id, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_release(
        self,
        lease_id: str,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> None:
        """Release a lease on app1 (idempotent, 404 ignored)."""
        if not self.base_url or not settings.remote_control_enabled:
            return

        url = f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}"
        headers = self._get_headers(org_id=org_id, user=user)
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.delete(url, headers=headers)
                if resp.status_code == status.HTTP_404_NOT_FOUND:
                    return
                resp.raise_for_status()
        except httpx.HTTPStatusError as exc:
            if exc.response.status_code == status.HTTP_404_NOT_FOUND:
                return
            self._handle_app1_http_error(exc)
        except httpx.RequestError as exc:
            logger.warning(
                "Failed to release lease %s (best-effort): %s", lease_id, exc
            )

    async def remote_input_move(
        self,
        lease_id: str,
        x: int,
        y: int,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> None:
        """Send pointer-move via REST fallback on app1 (returns 202)."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}/pointer-move"
        headers = self._get_headers(org_id=org_id, user=user)
        payload = {"x": x, "y": y}
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(url, json=payload, headers=headers)
                resp.raise_for_status()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to send pointer move for lease %s: %s", lease_id, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    async def remote_input_click(
        self,
        lease_id: str,
        x: int,
        y: int,
        client_ref: str | None = None,
        org_id: int | None = None,
        user: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Send mouse-click via REST fallback on app1."""
        if not self.base_url or not settings.remote_control_enabled:
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="remote control unavailable",
            )

        url = (
            f"{self.base_url}/api/internal/v1/remote-input/lease/{lease_id}/mouse-click"
        )
        headers = self._get_headers(org_id=org_id, user=user)
        payload: dict[str, Any] = {
            "x": x,
            "y": y,
            "button": "left",
        }
        if client_ref:
            payload["client_ref"] = client_ref
        timeout = settings.remote_control_click_timeout_sec
        try:
            async with httpx.AsyncClient(timeout=timeout) as client:
                resp = await client.post(url, json=payload, headers=headers)
                resp.raise_for_status()
                return resp.json()
        except httpx.HTTPStatusError as exc:
            self._handle_app1_http_error(exc)
            raise
        except httpx.RequestError as exc:
            logger.error("Failed to send mouse click for lease %s: %s", lease_id, exc)
            raise HTTPException(
                status_code=status.HTTP_502_BAD_GATEWAY,
                detail=f"Недоступен сервис управления iot-rpc-rest-app: {exc}",
            ) from exc

    def remote_input_ws_url(self, lease_id: str, session_id: str | None = None) -> str:
        """Convert internal HTTP(S) base URL to WS(S) URL for app1 lease WS endpoint."""
        base = self.base_url
        if not base:
            base = "http://app1:8000"
        if base.startswith("https://"):
            ws_base = "wss://" + base[8:]
        elif base.startswith("http://"):
            ws_base = "ws://" + base[7:]
        else:
            ws_base = f"ws://{base}"
        url = f"{ws_base}/api/internal/v1/remote-input/ws/lease/{lease_id}"
        if session_id:
            url += f"?session_id={session_id}"
        return url


iot_client = IotPlatformClient()
