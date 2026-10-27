"""Narrow bridge to existing tenant-scoped IoT camera contracts."""

from __future__ import annotations

import json
from typing import Any

import httpx
from fastapi import HTTPException

from app.services.camera_contract import CAMERA_MEDIA_REVISION
from app.services.iot_client import iot_client


async def camera_main_app_online(
    sn: str, org_id: int, user: dict[str, Any]
) -> bool | None:
    try:
        value = await iot_client.remote_video_v2(
            sn, "inventory", org_id=org_id, user=user, camera_contract=True
        )
    except HTTPException:
        return None
    if value.get("camera_contract_revision") != CAMERA_MEDIA_REVISION:
        return None
    return value.get("main_app_online") is True


async def camera_request(
    method: str,
    path: str,
    org_id: int,
    user: dict[str, Any],
    *,
    params: dict[str, Any] | None = None,
    body: dict[str, Any] | None = None,
    headers: dict[str, str] | None = None,
) -> dict[str, Any]:
    if not iot_client.base_url or not iot_client.service_token:
        raise HTTPException(503, "Сервис камеры не настроен")
    try:
        async with httpx.AsyncClient(timeout=iot_client.timeout) as client:
            response = await client.request(
                method,
                f"{iot_client.base_url}/api/internal/v1/{path}",
                params=params,
                json=body,
                headers={
                    **iot_client._get_headers(org_id=org_id, user=user),
                    **(headers or {}),
                },
            )
            response.raise_for_status()
            data = response.json()
    except httpx.HTTPStatusError as exc:
        iot_client._handle_app1_http_error(exc)
        raise
    except (httpx.RequestError, ValueError) as exc:
        # A lost POST response does not establish that the task was not created.
        raise HTTPException(
            502,
            {
                "code": "camera_request_unconfirmed",
                "message": "Ответ сервиса не получен; команда автоматически не повторяется",
            },
        ) from exc
    if not isinstance(data, dict):
        raise HTTPException(502, "Неверный ответ сервиса камеры")
    return data


def camera_snapshot(gauges: dict[str, Any], device_id: int) -> dict[str, Any] | None:
    for gauge in gauges.get("items", []):
        if gauge.get("device_id") != device_id or str(gauge.get("type")) != "84":
            continue
        payload = gauge.get("gauges")
        if isinstance(payload, str):
            try:
                payload = json.loads(payload)
            except ValueError:
                continue
        if not isinstance(payload, dict):
            continue
        for item in payload.get("300", []):
            snapshot = item.get("450") if isinstance(item, dict) else None
            if isinstance(snapshot, dict):
                # BFF metadata: the gauge receipt is timed by IoT, not by the
                # camera's optional UTC clock. Never refresh this on an HTTP read.
                return {**snapshot, "received_at": gauge.get("updated_at")}
    return None
