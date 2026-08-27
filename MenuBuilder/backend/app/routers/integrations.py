import secrets
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Query, status
from pydantic import BaseModel, Field

from app.auth import get_current_user
from app.services.iot_client import iot_client, mask_api_key

router = APIRouter(prefix="/api/integrations", tags=["integrations"])


class ApiKeyProvisionRequest(BaseModel):
    org_id: int | None = Field(
        default=None, description="Organization ID (superuser only)"
    )
    api_key: str | None = Field(
        default=None,
        description="Custom API key string (if omitted, generated automatically)",
    )
    name: str | None = Field(
        default=None, description="Key description / partner service name"
    )
    is_active: bool = Field(default=True, description="Whether key is active")


class ApiKeyToggleActiveRequest(BaseModel):
    org_id: int | None = Field(
        default=None, description="Organization ID (superuser only)"
    )
    is_active: bool = Field(default=True, description="Target active state")


class ApiKeyResponse(BaseModel):
    has_key: bool
    org_id: int
    api_key: str | None = None
    name: str | None = None
    is_active: bool = False
    created_at: str | None = None
    updated_at: str | None = None


class DeleteApiKeyResponse(BaseModel):
    status: str
    org_id: int


def _resolve_target_org_id(
    user: dict[str, Any], requested_org_id: int | None = None
) -> int:
    """Resolve target organization ID while enforcing superuser vs tenant boundaries."""
    is_su = bool(user.get("is_superuser"))
    if requested_org_id is not None:
        if not is_su and user.get("org_id") != requested_org_id:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Недостаточно прав для управления API-ключом другой организации",
            )
        return requested_org_id

    user_org = user.get("org_id")
    if user_org is not None:
        return int(user_org)

    raise HTTPException(
        status_code=status.HTTP_400_BAD_REQUEST,
        detail="Организация не определена для текущего пользователя",
    )


@router.get("/api-key", response_model=ApiKeyResponse)
async def get_org_api_key(
    org_id: int | None = Query(
        default=None, description="Target Org ID (superuser only)"
    ),
    mask: bool = Query(default=True, description="Whether to mask key string"),
    user: dict = Depends(get_current_user),
) -> ApiKeyResponse:
    """Get current organization API key status (masked by default for UI display)."""
    target_org_id = _resolve_target_org_id(user, org_id)
    key_info = await iot_client.get_api_key(org_id=target_org_id, mask=mask)
    if not key_info:
        return ApiKeyResponse(
            has_key=False,
            org_id=target_org_id,
            api_key=None,
            name=None,
            is_active=False,
            created_at=None,
            updated_at=None,
        )

    return ApiKeyResponse(
        has_key=True,
        org_id=target_org_id,
        api_key=key_info.get("api_key"),
        name=key_info.get("name"),
        is_active=bool(key_info.get("is_active", True)),
        created_at=str(key_info.get("created_at"))
        if key_info.get("created_at")
        else None,
        updated_at=str(key_info.get("updated_at"))
        if key_info.get("updated_at")
        else None,
    )


@router.get("/api-key/reveal", response_model=ApiKeyResponse)
async def reveal_org_api_key(
    org_id: int | None = Query(
        default=None, description="Target Org ID (superuser only)"
    ),
    user: dict = Depends(get_current_user),
) -> ApiKeyResponse:
    """Get unmasked full API key string for secure copying/viewing."""
    target_org_id = _resolve_target_org_id(user, org_id)
    key_info = await iot_client.get_api_key(org_id=target_org_id, mask=False)
    if not key_info:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="API-ключ для данной организации не найден",
        )

    return ApiKeyResponse(
        has_key=True,
        org_id=target_org_id,
        api_key=key_info.get("api_key"),
        name=key_info.get("name"),
        is_active=bool(key_info.get("is_active", True)),
        created_at=str(key_info.get("created_at"))
        if key_info.get("created_at")
        else None,
        updated_at=str(key_info.get("updated_at"))
        if key_info.get("updated_at")
        else None,
    )


@router.post("/api-key/provision", response_model=ApiKeyResponse)
async def provision_org_api_key(
    req: ApiKeyProvisionRequest,
    user: dict = Depends(get_current_user),
) -> ApiKeyResponse:
    """Provision, generate, or rotate organization API key on iot-rpc platform."""
    target_org_id = _resolve_target_org_id(user, req.org_id)
    raw_key = req.api_key.strip() if req.api_key else ""
    if not raw_key:
        raw_key = f"sec_live_{secrets.token_hex(20)}"

    name = req.name or f"Основной ключ организации #{target_org_id}"
    key_info = await iot_client.provision_api_key(
        org_id=target_org_id,
        api_key=raw_key,
        name=name,
        is_active=req.is_active,
    )

    return ApiKeyResponse(
        has_key=True,
        org_id=target_org_id,
        api_key=key_info.get("api_key", raw_key),
        name=key_info.get("name", name),
        is_active=bool(key_info.get("is_active", True)),
        created_at=str(key_info.get("created_at"))
        if key_info.get("created_at")
        else None,
        updated_at=str(key_info.get("updated_at"))
        if key_info.get("updated_at")
        else None,
    )


@router.post("/api-key/toggle-active", response_model=ApiKeyResponse)
async def toggle_api_key_active(
    req: ApiKeyToggleActiveRequest,
    user: dict = Depends(get_current_user),
) -> ApiKeyResponse:
    """Enable or disable existing API key."""
    target_org_id = _resolve_target_org_id(user, req.org_id)
    current_info = await iot_client.get_api_key(org_id=target_org_id, mask=False)
    if not current_info or not current_info.get("api_key"):
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="API-ключ для данной организации не найден",
        )

    updated_info = await iot_client.provision_api_key(
        org_id=target_org_id,
        api_key=current_info["api_key"],
        name=current_info.get("name"),
        is_active=req.is_active,
    )

    return ApiKeyResponse(
        has_key=True,
        org_id=target_org_id,
        api_key=mask_api_key(updated_info.get("api_key", "")),
        name=updated_info.get("name"),
        is_active=bool(updated_info.get("is_active", req.is_active)),
        created_at=str(updated_info.get("created_at"))
        if updated_info.get("created_at")
        else None,
        updated_at=str(updated_info.get("updated_at"))
        if updated_info.get("updated_at")
        else None,
    )


@router.delete("/api-key", response_model=DeleteApiKeyResponse)
async def delete_org_api_key(
    org_id: int | None = Query(
        default=None, description="Target Org ID (superuser only)"
    ),
    user: dict = Depends(get_current_user),
) -> DeleteApiKeyResponse:
    """Delete / revoke organization API key on iot-rpc platform."""
    target_org_id = _resolve_target_org_id(user, org_id)
    res = await iot_client.delete_api_key(org_id=target_org_id)
    if not res:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="API-ключ для данной организации не найден",
        )
    return DeleteApiKeyResponse(status="deleted", org_id=target_org_id)
