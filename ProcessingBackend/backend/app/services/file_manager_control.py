"""Authoritative lease checks; deliberately carries metadata only."""

from datetime import UTC, datetime
from typing import Any
from uuid import UUID

import httpx
from etranprocessing_db.file_manager import FileManagerOperation
from fastapi import HTTPException

from app.config import settings


async def lease_request(
    lease_id: UUID,
    actor: dict[str, str],
    *,
    action: str | None = None,
    operation_id: UUID | None = None,
) -> dict[str, Any]:
    if not settings.file_manager_iot_url or not settings.file_manager_iot_key:
        raise HTTPException(503, detail={"code": "fm_control_unavailable"})
    headers = {**actor, "X-Internal-Service-Key": settings.file_manager_iot_key}
    url = f"{settings.file_manager_iot_url.rstrip('/')}/api/internal/v1/file-manager/sessions/{lease_id}"
    try:
        async with httpx.AsyncClient(timeout=5, follow_redirects=False) as client:
            if action:
                body: dict[str, Any] = {"action": action}
                if operation_id:
                    body["operation_id"] = str(operation_id)
                response = await client.post(
                    f"{url}/signals", headers=headers, json=body
                )
            else:
                response = await client.get(url, headers=headers)
        if response.status_code != 200:
            raise HTTPException(409, detail={"code": "fm_lease_unavailable"})
        result = response.json()
        deadline = datetime.fromisoformat(result["expires_at"])
        if result.get("scope") != "files" or deadline <= datetime.now(UTC):
            raise HTTPException(409, detail={"code": "fm_lease_unavailable"})
        return result
    except (httpx.RequestError, ValueError, KeyError, TypeError) as exc:
        raise HTTPException(503, detail={"code": "fm_control_unavailable"}) from exc


def operation_actor(operation: FileManagerOperation) -> dict[str, str]:
    return {
        "X-Org-Id": str(operation.tenant_id),
        "X-User-Id": operation.owner_user_id,
        "X-Session-Id": operation.owner_session_id,
        "X-Role": operation.owner_role,
    }
