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

    def _get_headers(self, org_id: int | None = None) -> dict[str, str]:
        headers = {"Content-Type": "application/json"}
        if self.service_token:
            headers["X-Internal-Service-Key"] = self.service_token
        if org_id is not None:
            headers["X-Org-Id"] = str(org_id)
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


iot_client = IotPlatformClient()
