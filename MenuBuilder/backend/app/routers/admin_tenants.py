from fastapi import APIRouter, Depends, HTTPException, Request, Response, status
from pydantic import BaseModel
from sqlalchemy import select

from app.auth import require_superuser
from app.config import settings
from app.database import async_session
from app.models import Org
from app.routers.auth import _is_secure_request, _set_access_cookie
from app.services.jwt_issuer import jwt_issuer_client
from app.user_store import get_user_store

router = APIRouter(prefix="/admin/tenants", tags=["admin-tenants"])
# Alias under /api/auth/*: the refreshToken cookie has path=/api/auth and the browser
# sends it ONLY to that prefix. Nginx has auth_jwt off there; backend verifies the
# accessToken cookie itself via require_superuser.
auth_alias_router = APIRouter(prefix="/auth", tags=["auth"])


class SwitchTenantRequest(BaseModel):
    org_id: int
    refresh_token: str | None = None


class SwitchTenantResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    org_id: int
    org_name: str
    timezone: str = "Europe/Moscow"
    expires_in: int
    is_superuser: bool = True
    is_impersonated: bool = False


class OrgItem(BaseModel):
    org_id: int
    org_name: str
    name: str | None = None
    is_active: bool
    timezone: str = "Europe/Moscow"


async def _resolve_current_session(
    request: Request,
    body_refresh_token: str | None,
    user: dict,
):
    """Find the caller's server-side session.

    Priority:
      1. `sid` claim from the access token (issuer v2) — works on any path.
      2. refresh token from request body or `refreshToken` cookie
         (cookie arrives only under /api/auth/* because of its path).
    """
    store = get_user_store()

    sid = user.get("sid")
    if sid is not None and str(sid).isdigit():
        session = await store.get_session_by_id(int(sid))
        if (
            session is not None
            and not session.is_revoked
            and session.user_id == user.get("user_id")
        ):
            return store, session

    refresh_token_val = body_refresh_token or request.cookies.get("refreshToken")
    if not refresh_token_val:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Session not found. Use /api/auth/switch-tenant or pass refresh_token.",
        )

    session = await store.get_session_by_refresh_token(refresh_token_val)
    if not session:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or expired session",
        )
    return store, session


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
                timezone=getattr(o, "timezone", "Europe/Moscow") or "Europe/Moscow",
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
    store, session = await _resolve_current_session(request, body.refresh_token, user)

    if body.org_id == 0:
        target_org_id = 0
        target_org_name = "Платформа"
        target_timezone = "Europe/Moscow"
    else:
        async with async_session() as db_session:
            result = await db_session.execute(
                select(Org).where(Org.org_id == body.org_id, Org.is_active == True)
            )
            org = result.scalar_one_or_none()
            if not org:
                raise HTTPException(
                    status_code=status.HTTP_404_NOT_FOUND,
                    detail=f"Organization with org_id {body.org_id} not found or inactive",
                )
            target_org_id = org.org_id
            target_org_name = org.org_name
            target_timezone = (
                getattr(org, "timezone", "Europe/Moscow") or "Europe/Moscow"
            )

    user_id = user.get("user_id") or session.user_id or 1
    role_id = user.get("role_id") or 1
    role = user.get("role") or "superuser"
    username = user.get("username") or "superuser"
    is_superuser = user.get("is_superuser", True)

    token_data = await jwt_issuer_client.issue_tokens(
        user_id=user_id,
        org_id=target_org_id,
        role_id=role_id,
        username=username,
        role=role,
        is_superuser=is_superuser,
        sid=session.id,
        orig_sub=user.get("orig_sub") or username,
    )

    access_token = token_data.get("accessToken") or token_data.get("access_token") or ""
    expires_in = int(
        token_data.get("expiresIn")
        or token_data.get("expires_in")
        or settings.jwt_expire_minutes * 60
    )

    # Update session active_org_id and user last_org_id
    await store.set_session_active_org(session.id, target_org_id)
    await store.set_user_last_org(user_id, target_org_id)

    # DO NOT create a new session, DO NOT rewrite the refreshToken cookie.
    # Update ONLY the accessToken cookie.
    is_secure = _is_secure_request(request)
    _set_access_cookie(
        response=response,
        access_token=access_token,
        expires_in=expires_in,
        is_secure=is_secure,
    )

    is_imp = bool(is_superuser and target_org_id > 0)

    return SwitchTenantResponse(
        access_token=access_token,
        token_type="bearer",
        org_id=target_org_id,
        org_name=target_org_name,
        timezone=target_timezone,
        expires_in=expires_in,
        is_superuser=is_superuser,
        is_impersonated=is_imp,
    )


@auth_alias_router.post("/switch-tenant", response_model=SwitchTenantResponse)
async def switch_tenant_via_auth_path(
    body: SwitchTenantRequest,
    request: Request,
    response: Response,
    user: dict = Depends(require_superuser),
):
    """Browser entry point: same handler, but under /api/auth so the refreshToken cookie is sent."""
    return await switch_tenant(body, request, response, user)
