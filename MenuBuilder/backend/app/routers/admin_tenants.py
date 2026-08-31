from fastapi import APIRouter, Depends, HTTPException, Request, Response, status
from pydantic import BaseModel
from sqlalchemy import select

from app.auth import require_superuser
from app.config import settings
from app.database import async_session
from app.models import Org
from app.routers.auth import _set_auth_cookies
from app.services.jwt_issuer import jwt_issuer_client
from app.user_store import get_user_store

router = APIRouter(prefix="/admin/tenants", tags=["admin-tenants"])


class SwitchTenantRequest(BaseModel):
    org_id: int


class SwitchTenantResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    org_id: int
    org_name: str
    expires_in: int


class OrgItem(BaseModel):
    org_id: int
    org_name: str
    name: str | None = None
    is_active: bool


@router.get("/available", response_model=list[OrgItem])
async def list_available_tenants(
    user: dict = Depends(require_superuser),
):
    """List all active organizations for tenant selection. Strictly restricted to superusers."""
    async with async_session() as session:
        result = await session.execute(
            select(Org).where(Org.is_active == True).order_by(Org.org_name, Org.org_id)
        )
        orgs = result.scalars().all()
        return [
            OrgItem(
                org_id=o.org_id,
                org_name=o.org_name,
                name=o.name,
                is_active=o.is_active,
            )
            for o in orgs
        ]


@router.post("/switch", response_model=SwitchTenantResponse)
async def switch_tenant(
    body: SwitchTenantRequest,
    request: Request,
    response: Response,
    user: dict = Depends(require_superuser),
):
    """Switch tenant context and issue a tenant-scoped JWT. Strictly restricted to superusers."""
    async with async_session() as session:
        result = await session.execute(
            select(Org).where(Org.org_id == body.org_id, Org.is_active == True)
        )
        org = result.scalar_one_or_none()
        if not org:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Organization with org_id {body.org_id} not found or inactive",
            )

    user_id = user.get("user_id") or 1
    role_id = user.get("role_id") or 1
    role = user.get("role") or "superuser"
    username = user.get("username") or "superuser"
    is_superuser = user.get("is_superuser", True)

    token_data = await jwt_issuer_client.issue_tokens(
        user_id=user_id,
        org_id=org.org_id,
        role_id=role_id,
        username=username,
        role=role,
        is_superuser=is_superuser,
    )

    access_token = token_data.get("accessToken") or token_data.get("access_token") or ""
    refresh_token = token_data.get("refreshToken") or token_data.get("refresh_token")
    expires_in = int(
        token_data.get("expiresIn")
        or token_data.get("expires_in")
        or settings.jwt_expire_minutes * 60
    )
    refresh_expires_in = int(
        token_data.get("refreshExpiresIn")
        or token_data.get("refresh_expires_in")
        or settings.jwt_refresh_expire_days * 86400
    )

    if refresh_token:
        store = get_user_store()
        client_ip = (
            request.headers.get("X-Real-IP")
            or request.headers.get("X-Forwarded-For")
            or (request.client.host if request.client else None)
        )
        user_agent = request.headers.get("User-Agent")
        await store.create_session(
            user_id=user_id,
            refresh_token=refresh_token,
            ip_address=client_ip,
            user_agent=user_agent,
            expires_in_seconds=refresh_expires_in,
        )

    is_secure = request.url.scheme == "https"
    _set_auth_cookies(
        response=response,
        access_token=access_token,
        refresh_token=refresh_token,
        expires_in=expires_in,
        refresh_expires_in=refresh_expires_in,
        is_secure=is_secure,
    )

    return SwitchTenantResponse(
        access_token=access_token,
        token_type="bearer",
        org_id=org.org_id,
        org_name=org.org_name,
        expires_in=expires_in,
    )
