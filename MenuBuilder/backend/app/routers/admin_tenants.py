from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel
from sqlalchemy import select

from app.auth import create_tenant_token, require_superuser
from app.config import settings
from app.database import async_session
from app.models import Org

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

    tenant_token = create_tenant_token(
        user=user,
        target_org_id=org.org_id,
        original_user=user.get("orig_sub") or user.get("username"),
    )

    return SwitchTenantResponse(
        access_token=tenant_token,
        token_type="bearer",
        org_id=org.org_id,
        org_name=org.org_name,
        expires_in=settings.jwt_expire_minutes * 60,
    )
