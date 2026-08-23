import logging
from typing import Any

import httpx

from app.config import settings

logger = logging.getLogger(__name__)


class IotPlatformClient:
    def __init__(
        self,
        base_url: str | None = None,
        service_token: str | None = None,
        timeout: float | None = None,
    ):
        self.base_url = (base_url or settings.iot_rpc_base_url or "").rstrip("/")
        self.service_token = (
            service_token
            if service_token is not None
            else settings.iot_rpc_service_token
        )
        self.timeout = timeout or settings.iot_rpc_timeout_seconds

    def _get_headers(self) -> dict[str, str]:
        headers = {"Content-Type": "application/json"}
        if self.service_token:
            headers["X-Internal-Service-Key"] = self.service_token
            headers["x-api-key"] = self.service_token
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

        url = f"{self.base_url}/api/v1/provisioning/terminals"
        payload = {
            "device_id": device_id,
            "sn": sn,
            "org_id": org_id,
            "name": name,
            "tags": tags or {"source": "etranprocessing"},
        }
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(url, json=payload, headers=self._get_headers())
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

        url = f"{self.base_url}/api/v1/provisioning/terminals/batch"
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

        url = f"{self.base_url}/api/v1/provisioning/terminals/status"
        payload = {"device_ids": device_ids}
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(url, json=payload, headers=self._get_headers())
            resp.raise_for_status()
            return resp.json().get("statuses", [])


iot_client = IotPlatformClient()
