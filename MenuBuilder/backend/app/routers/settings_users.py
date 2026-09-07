from __future__ import annotations

import hashlib
from datetime import UTC, datetime
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Query, status
from pydantic import BaseModel, Field
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import User
from app.security.permissions import (
    ROLE_SUPERUSER,
    ROLE_VIEWER,
    VALID_PERMISSION_CODES,
    require_tenant_admin,
)
from app.services.jwt_issuer import jwt_issuer_client
from app.user_store import get_user_store

router = APIRouter(prefix="/settings/users", tags=["settings-users"])


class TenantUserRead(BaseModel):
    id: int
    username: str
    full_name: str | None = None
    role_id: int
    role: str
    is_active: bool
    permissions: list[str] = []
    created_at: str | None = None


class CreateTenantUserRequest(BaseModel):
    username: str = Field(..., min_length=3, max_length=100)
    password: str = Field(..., min_length=6)
    full_name: str | None = None
    permissions: list[str] = Field(default_factory=list)


class UpdateTenantUserRequest(BaseModel):
    full_name: str | None = None
    permissions: list[str] | None = None
    is_active: bool | None = None


class ChangeTenantUserPasswordRequest(BaseModel):
    password: str = Field(..., min_length=6)


class ToggleActiveRequest(BaseModel):
    is_active: bool | None = None


def _get_effective_org_id(user: dict[str, Any], query_org_id: int | None = None) -> int:
    is_su = bool(user.get("is_superuser") or user.get("role_id") == ROLE_SUPERUSER)
    if is_su and query_org_id is not None and query_org_id > 0:
        return query_org_id
    org_id = int(user.get("org_id", 0) or 0)
    if org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Выберите организацию",
        )
    return org_id


@router.get("", response_model=list[TenantUserRead])
@router.get("/", response_model=list[TenantUserRead])
async def list_tenant_users(
    org_id: int | None = Query(None, description="Org ID for superuser viewing"),
    user: dict[str, Any] = Depends(require_tenant_admin),
    db: AsyncSession = Depends(get_db),
) -> list[TenantUserRead]:
    """List all users of current tenant (role 3 and role 4)."""
    effective_org = _get_effective_org_id(user, org_id)

    result = await db.execute(
        select(User)
        .where(User.org_id == effective_org)
        .order_by(User.role_id.asc(), User.id.asc())
    )
    users = result.scalars().all()

    return [
        TenantUserRead(
            id=u.id,
            username=u.username,
            full_name=u.full_name,
            role_id=u.role_id,
            role="viewer" if u.role_id == ROLE_VIEWER else u.role,
            is_active=u.is_active,
            permissions=u.permissions or [],
            created_at=u.created_at.isoformat() if u.created_at else None,
        )
        for u in users
    ]


@router.post("", response_model=TenantUserRead, status_code=status.HTTP_201_CREATED)
@router.post("/", response_model=TenantUserRead, status_code=status.HTTP_201_CREATED)
async def create_tenant_user(
    req: CreateTenantUserRequest,
    user: dict[str, Any] = Depends(require_tenant_admin),
    db: AsyncSession = Depends(get_db),
) -> TenantUserRead:
    """Create a new role 4 (viewer) user in current tenant context."""
    effective_org = _get_effective_org_id(user)

    username_clean = req.username.strip()
    if not username_clean:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Логин не может быть пустым",
        )

    # Check unique username
    existing_user_res = await db.execute(
        select(User).where(func.lower(User.username) == username_clean.lower())
    )
    if existing_user_res.scalar_one_or_none():
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"Пользователь с логином '{username_clean}' уже существует",
        )

    # Validate permissions against whitelist
    invalid_perms = [p for p in req.permissions if p not in VALID_PERMISSION_CODES]
    if invalid_perms:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Недопустимые разрешения: {', '.join(invalid_perms)}",
        )

    md5_hash = hashlib.md5(req.password.encode("utf-8")).hexdigest()

    new_user = User(
        username=username_clean,
        md5_password=md5_hash,
        org_id=effective_org,
        role_id=ROLE_VIEWER,
        role="viewer",
        is_superuser=False,
        is_active=True,
        full_name=req.full_name.strip() if req.full_name else None,
        permissions=list(dict.fromkeys(req.permissions)),
    )
    db.add(new_user)
    await db.commit()
    await db.refresh(new_user)

    return TenantUserRead(
        id=new_user.id,
        username=new_user.username,
        full_name=new_user.full_name,
        role_id=new_user.role_id,
        role=new_user.role,
        is_active=new_user.is_active,
        permissions=new_user.permissions or [],
        created_at=new_user.created_at.isoformat() if new_user.created_at else None,
    )


@router.put("/{user_id}", response_model=TenantUserRead)
async def update_tenant_user(
    user_id: int,
    req: UpdateTenantUserRequest,
    user: dict[str, Any] = Depends(require_tenant_admin),
    db: AsyncSession = Depends(get_db),
) -> TenantUserRead:
    """Update role 4 user permissions, full name, or active status."""
    effective_org = _get_effective_org_id(user)

    target_res = await db.execute(
        select(User).where(User.id == user_id, User.org_id == effective_org)
    )
    target_user = target_res.scalar_one_or_none()
    if not target_user:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Пользователь не найден в вашей организации",
        )

    if target_user.role_id != ROLE_VIEWER:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Редактирование разрешено только для пользователей с ролью Наблюдатель (viewer)",
        )

    if req.permissions is not None:
        invalid_perms = [p for p in req.permissions if p not in VALID_PERMISSION_CODES]
        if invalid_perms:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail=f"Недопустимые разрешения: {', '.join(invalid_perms)}",
            )
        target_user.permissions = list(dict.fromkeys(req.permissions))

    if req.full_name is not None:
        target_user.full_name = req.full_name.strip() or None

    deactivated = False
    if req.is_active is not None:
        if target_user.is_active and not req.is_active:
            deactivated = True
        target_user.is_active = req.is_active

    target_user.updated_at = datetime.now(UTC)
    await db.commit()
    await db.refresh(target_user)

    if deactivated:
        store = get_user_store()
        await store.revoke_all_user_sessions(target_user.id)
        jwt_issuer_client.invalidate_cache_for_user(target_user.id)

    return TenantUserRead(
        id=target_user.id,
        username=target_user.username,
        full_name=target_user.full_name,
        role_id=target_user.role_id,
        role=target_user.role,
        is_active=target_user.is_active,
        permissions=target_user.permissions or [],
        created_at=target_user.created_at.isoformat()
        if target_user.created_at
        else None,
    )


@router.post("/{user_id}/change-password")
async def change_tenant_user_password(
    user_id: int,
    req: ChangeTenantUserPasswordRequest,
    user: dict[str, Any] = Depends(require_tenant_admin),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Change password for a role 4 user and revoke all active sessions."""
    effective_org = _get_effective_org_id(user)

    target_res = await db.execute(
        select(User).where(User.id == user_id, User.org_id == effective_org)
    )
    target_user = target_res.scalar_one_or_none()
    if not target_user:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Пользователь не найден в вашей организации",
        )

    if target_user.role_id != ROLE_VIEWER:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Смена пароля через этот раздел разрешена только для пользователей роли Наблюдатель (viewer)",
        )

    target_user.md5_password = hashlib.md5(req.password.encode("utf-8")).hexdigest()
    target_user.updated_at = datetime.now(UTC)
    await db.commit()

    # Revoke sessions and token cache
    store = get_user_store()
    await store.revoke_all_user_sessions(target_user.id)
    jwt_issuer_client.invalidate_cache_for_user(target_user.id)

    return {"ok": True, "message": "Пароль успешно изменен"}


@router.post("/{user_id}/toggle-active")
async def toggle_tenant_user_active(
    user_id: int,
    req: ToggleActiveRequest | None = None,
    user: dict[str, Any] = Depends(require_tenant_admin),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Toggle or set active status for a role 4 user."""
    effective_org = _get_effective_org_id(user)

    target_res = await db.execute(
        select(User).where(User.id == user_id, User.org_id == effective_org)
    )
    target_user = target_res.scalar_one_or_none()
    if not target_user:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Пользователь не найден в вашей организации",
        )

    if target_user.role_id != ROLE_VIEWER:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Изменение статуса разрешено только для пользователей роли Наблюдатель (viewer)",
        )

    new_active = (
        req.is_active
        if (req and req.is_active is not None)
        else (not target_user.is_active)
    )
    target_user.is_active = new_active
    target_user.updated_at = datetime.now(UTC)
    await db.commit()

    if not new_active:
        store = get_user_store()
        await store.revoke_all_user_sessions(target_user.id)
        jwt_issuer_client.invalidate_cache_for_user(target_user.id)

    return {
        "ok": True,
        "is_active": new_active,
        "message": "Статус активности пользователя успешно изменен",
    }
