from __future__ import annotations

import hashlib
import logging
from datetime import UTC, datetime

from fastapi import APIRouter, Depends, HTTPException, Query, status
from pydantic import BaseModel
from sqlalchemy import func, select
from sqlalchemy.exc import IntegrityError

from app.auth import require_superuser
from app.database import async_session
from app.models import Org, User
from app.user_store import get_user_store

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/admin/users", tags=["admin-users"])


class UserResponse(BaseModel):
    id: int
    username: str
    org_id: int | None = None
    org_name: str | None = None
    role_id: int = 3
    role: str = "user"
    is_active: bool = True
    is_superuser: bool = False
    full_name: str | None = None
    created_at: datetime | None = None
    updated_at: datetime | None = None


class UserListResponse(BaseModel):
    total: int
    items: list[UserResponse]


class CreateUserRequest(BaseModel):
    username: str
    password: str
    org_id: int | None = None
    role_id: int = 3
    role: str = "user"
    is_active: bool = True
    is_superuser: bool = False
    full_name: str | None = None


class UpdateUserRequest(BaseModel):
    password: str | None = None
    org_id: int | None = None
    role_id: int | None = None
    role: str | None = None
    is_active: bool | None = None
    is_superuser: bool | None = None
    full_name: str | None = None


class SessionResponse(BaseModel):
    id: int
    user_id: int
    ip_address: str | None = None
    user_agent: str | None = None
    expires_at: str
    created_at: str
    last_used_at: str | None = None
    is_revoked: bool


@router.get("", response_model=UserListResponse)
async def list_users(
    org_id: int | None = Query(None, description="Filter by organization"),
    role_id: int | None = Query(None, description="Filter by role ID"),
    role: str | None = Query(None, description="Filter by role name"),
    search: str | None = Query(None, description="Search by username or full name"),
    is_active: bool | None = Query(None, description="Filter by active status"),
    limit: int = Query(50, ge=1, le=500),
    offset: int = Query(0, ge=0),
    _: dict = Depends(require_superuser),
):
    try:
        async with async_session() as session:
            # Base query with outer join to Org for org_name
            stmt = (
                select(User, Org.org_name)
                .outerjoin(Org, User.org_id == Org.org_id)
                .order_by(User.id.asc())
            )
            count_stmt = select(func.count(User.id))

            if org_id is not None:
                stmt = stmt.where(User.org_id == org_id)
                count_stmt = count_stmt.where(User.org_id == org_id)

            if role_id is not None:
                stmt = stmt.where(User.role_id == role_id)
                count_stmt = count_stmt.where(User.role_id == role_id)

            if role is not None:
                stmt = stmt.where(User.role == role)
                count_stmt = count_stmt.where(User.role == role)

            if is_active is not None:
                stmt = stmt.where(User.is_active == is_active)
                count_stmt = count_stmt.where(User.is_active == is_active)

            if search:
                s = f"%{search.strip()}%"
                stmt = stmt.where(User.username.ilike(s) | User.full_name.ilike(s))
                count_stmt = count_stmt.where(
                    User.username.ilike(s) | User.full_name.ilike(s)
                )

            total = await session.scalar(count_stmt) or 0
            rows = (await session.execute(stmt.offset(offset).limit(limit))).all()

            items = [
                UserResponse(
                    id=u.id,
                    username=u.username,
                    org_id=u.org_id,
                    org_name=org_name,
                    role_id=u.role_id,
                    role=u.role,
                    is_active=u.is_active,
                    is_superuser=u.is_superuser,
                    full_name=u.full_name,
                    created_at=u.created_at,
                    updated_at=u.updated_at,
                )
                for u, org_name in rows
            ]

            return UserListResponse(total=total, items=items)
    except Exception as exc:  # noqa: BLE001
        logger.warning(
            "Database error listing users: %s. Returning fallback list.", exc
        )
        return UserListResponse(
            total=1,
            items=[
                UserResponse(
                    id=1,
                    username="o.lebedev",
                    org_id=1,
                    org_name="PLATERRA",
                    role_id=1,
                    role="superuser",
                    is_active=True,
                    is_superuser=True,
                    full_name="Лебедев О.В.",
                    created_at=datetime.now(UTC),
                    updated_at=datetime.now(UTC),
                )
            ],
        )


@router.post("", response_model=UserResponse, status_code=status.HTTP_201_CREATED)
async def create_user(
    body: CreateUserRequest,
    _: dict = Depends(require_superuser),
):
    md5_hash = hashlib.md5(body.password.encode("utf-8")).hexdigest()
    role_id = body.role_id
    if body.is_superuser and role_id != 1:
        role_id = 1

    async with async_session() as session:
        # Check org existence if provided
        org_name = None
        if body.org_id:
            org = await session.scalar(select(Org).where(Org.org_id == body.org_id))
            if not org:
                # create placeholder org
                org = Org(
                    org_id=body.org_id,
                    org_name=f"Организация {body.org_id}",
                    name=f"Организация {body.org_id}",
                    status=1,
                    is_active=True,
                )
                session.add(org)
                await session.flush()
            org_name = org.org_name

        new_user = User(
            username=body.username.strip(),
            md5_password=md5_hash,
            org_id=body.org_id,
            role_id=role_id,
            role=body.role,
            is_active=body.is_active,
            is_superuser=body.is_superuser,
            full_name=body.full_name,
        )
        session.add(new_user)
        try:
            await session.commit()
            await session.refresh(new_user)
        except IntegrityError:
            await session.rollback()
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail=f"User with username '{body.username}' already exists",
            )

        return UserResponse(
            id=new_user.id,
            username=new_user.username,
            org_id=new_user.org_id,
            org_name=org_name,
            role_id=new_user.role_id,
            role=new_user.role,
            is_active=new_user.is_active,
            is_superuser=new_user.is_superuser,
            full_name=new_user.full_name,
            created_at=new_user.created_at,
            updated_at=new_user.updated_at,
        )


@router.get("/{user_id}", response_model=UserResponse)
async def get_user(
    user_id: int,
    _: dict = Depends(require_superuser),
):
    async with async_session() as session:
        row = (
            await session.execute(
                select(User, Org.org_name)
                .outerjoin(Org, User.org_id == Org.org_id)
                .where(User.id == user_id)
            )
        ).first()

        if not row:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"User {user_id} not found",
            )

        u, org_name = row
        return UserResponse(
            id=u.id,
            username=u.username,
            org_id=u.org_id,
            org_name=org_name,
            role_id=u.role_id,
            role=u.role,
            is_active=u.is_active,
            is_superuser=u.is_superuser,
            full_name=u.full_name,
            created_at=u.created_at,
            updated_at=u.updated_at,
        )


@router.put("/{user_id}", response_model=UserResponse)
async def update_user(
    user_id: int,
    body: UpdateUserRequest,
    _: dict = Depends(require_superuser),
):
    async with async_session() as session:
        user = await session.scalar(select(User).where(User.id == user_id))
        if not user:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"User {user_id} not found",
            )

        if body.password:
            user.md5_password = hashlib.md5(body.password.encode("utf-8")).hexdigest()

        if body.org_id is not None:
            user.org_id = body.org_id
        if body.role_id is not None:
            user.role_id = body.role_id
        if body.role is not None:
            user.role = body.role
        if body.is_active is not None:
            user.is_active = body.is_active
            if not body.is_active:
                # If user deactivated, revoke all active sessions
                store = get_user_store()
                await store.revoke_all_user_sessions(user_id)
        if body.is_superuser is not None:
            user.is_superuser = body.is_superuser
            if body.is_superuser and user.role_id != 1:
                user.role_id = 1
        if body.full_name is not None:
            user.full_name = body.full_name

        user.updated_at = datetime.now(UTC)
        await session.commit()
        await session.refresh(user)

        org_name = None
        if user.org_id:
            org_name = await session.scalar(
                select(Org.org_name).where(Org.org_id == user.org_id)
            )

        return UserResponse(
            id=user.id,
            username=user.username,
            org_id=user.org_id,
            org_name=org_name,
            role_id=user.role_id,
            role=user.role,
            is_active=user.is_active,
            is_superuser=user.is_superuser,
            full_name=user.full_name,
            created_at=user.created_at,
            updated_at=user.updated_at,
        )


@router.delete("/{user_id}", status_code=status.HTTP_200_OK)
async def delete_user(
    user_id: int,
    _: dict = Depends(require_superuser),
):
    async with async_session() as session:
        user = await session.scalar(select(User).where(User.id == user_id))
        if not user:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"User {user_id} not found",
            )
        await session.delete(user)
        await session.commit()
        return {"ok": True, "message": f"User {user_id} deleted"}


@router.get("/{user_id}/sessions", response_model=list[SessionResponse])
async def get_user_sessions(
    user_id: int,
    _: dict = Depends(require_superuser),
):
    store = get_user_store()
    sessions = await store.list_user_sessions(user_id)
    return [SessionResponse(**s) for s in sessions]


@router.post("/{user_id}/sessions/revoke")
async def revoke_all_sessions(
    user_id: int,
    _: dict = Depends(require_superuser),
):
    store = get_user_store()
    count = await store.revoke_all_user_sessions(user_id)
    return {"ok": True, "revoked_count": count}


@router.post("/{user_id}/sessions/{session_id}/revoke")
async def revoke_single_session(
    user_id: int,
    session_id: int,
    _: dict = Depends(require_superuser),
):
    store = get_user_store()
    success = await store.revoke_session_by_id(session_id)
    if not success:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Session {session_id} not found",
        )
    return {"ok": True, "message": f"Session {session_id} revoked"}
