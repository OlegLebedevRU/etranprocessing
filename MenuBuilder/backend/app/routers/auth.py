from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel
from sqlalchemy import select
from sqlalchemy.exc import SQLAlchemyError

from app.auth import (
    create_master_token,
    create_tenant_token,
    get_current_user,
)
from app.config import settings
from app.database import async_session
from app.models import Org
from app.user_store import get_user_store

router = APIRouter(prefix="/auth", tags=["auth"])


class LoginRequest(BaseModel):
    username: str
    password: str


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    expires_in: int
    master_token: str | None = None
    is_superuser: bool = False
    role: str = "user"
    org_id: int | None = None


class UserInfo(BaseModel):
    username: str
    org_id: int | None = None
    role: str = "user"
    is_superuser: bool = False
    can_switch_org: bool = False
    token_type: str = "tenant"
    is_impersonated: bool = False
    org_name: str | None = None


@router.post("/login", response_model=TokenResponse)
async def login(body: LoginRequest):
    store = get_user_store()
    user = await store.authenticate(body.username, body.password)
    if not user:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid credentials"
        )

    master_token: str | None = None
    if user.is_superuser:
        master_token = create_master_token(user)
        if user.org_id:
            access_token = create_tenant_token(user, user.org_id)
        else:
            access_token = master_token
    else:
        access_token = create_tenant_token(user, user.org_id or 0)

    return TokenResponse(
        access_token=access_token,
        master_token=master_token,
        expires_in=settings.jwt_expire_minutes * 60,
        is_superuser=user.is_superuser,
        role=user.role,
        org_id=user.org_id,
    )


@router.get("/me", response_model=UserInfo)
async def me(user: dict = Depends(get_current_user)):
    org_id = user.get("org_id")
    org_name = None
    if org_id:
        try:
            async with async_session() as session:
                result = await session.execute(
                    select(Org.org_name).where(Org.org_id == org_id)
                )
                org_name = result.scalar_one_or_none()
        except SQLAlchemyError:
            org_name = None

    is_su = bool(user.get("is_superuser") or user.get("role") in ("superuser", "admin"))
    return UserInfo(
        username=user["username"],
        org_id=org_id,
        role=user.get("role", "user"),
        is_superuser=is_su,
        can_switch_org=is_su,
        token_type=user.get("token_type", "tenant"),
        is_impersonated=bool(user.get("is_impersonated", False)),
        org_name=org_name,
    )
