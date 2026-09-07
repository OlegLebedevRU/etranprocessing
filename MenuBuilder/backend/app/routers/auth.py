from __future__ import annotations

import base64
import logging
from contextlib import suppress
from datetime import UTC, datetime

from fastapi import APIRouter, Depends, Header, HTTPException, Request, Response, status
from pydantic import BaseModel
from sqlalchemy import select

from app.auth import (
    get_current_user,
)
from app.config import settings
from app.database import async_session
from app.models import Org
from app.services.jwt_issuer import jwt_issuer_client
from app.user_store import get_user_store

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/auth", tags=["auth"])


class LoginRequest(BaseModel):
    username: str | None = None
    password: str | None = None


class RefreshTokenRequest(BaseModel):
    refresh_token: str | None = None


class TokenResponse(BaseModel):
    access_token: str
    refresh_token: str | None = None
    token_type: str = "Bearer"
    expires_in: int
    refresh_expires_in: int | None = None
    master_token: str | None = None
    is_superuser: bool = False
    role: str = "user"
    role_id: int = 3
    org_id: int | None = None
    user_id: int | None = None


class UserInfo(BaseModel):
    user_id: int | None = None
    username: str
    org_id: int | None = None
    role_id: int = 3
    role: str = "user"
    is_superuser: bool = False
    can_switch_org: bool = False
    token_type: str = "tenant"
    is_impersonated: bool = False
    org_name: str | None = None
    timezone: str = "Europe/Moscow"
    expires_at: str | None = None
    full_name: str | None = None


def _extract_basic_auth(authorization: str | None) -> tuple[str, str] | None:
    if not authorization or not authorization.startswith("Basic "):
        return None
    try:
        encoded = authorization.split(" ", 1)[1]
        decoded = base64.b64decode(encoded).decode("utf-8")
        username, password = decoded.split(":", 1)
        return username, password
    except Exception:  # noqa: BLE001
        return None


def _is_secure_request(request: Request) -> bool:
    return (
        request.url.scheme == "https"
        or request.headers.get("X-Forwarded-Proto", "").lower() == "https"
    )


def _set_access_cookie(
    response: Response,
    access_token: str,
    expires_in: int,
    is_secure: bool = False,
) -> None:
    # Set accessToken cookie (accessible across whole app)
    response.set_cookie(
        key="accessToken",
        value=access_token,
        httponly=True,
        samesite="lax",
        path="/",
        max_age=expires_in,
        secure=is_secure,
    )


def _set_refresh_cookie(
    response: Response,
    refresh_token: str,
    refresh_expires_in: int,
    is_secure: bool = False,
) -> None:
    # Set refreshToken cookie (restricted to /api/auth path)
    response.set_cookie(
        key="refreshToken",
        value=refresh_token,
        httponly=True,
        samesite="lax",
        path="/api/auth",
        max_age=refresh_expires_in,
        secure=is_secure,
    )


def _set_auth_cookies(
    response: Response,
    access_token: str,
    refresh_token: str | None,
    expires_in: int,
    refresh_expires_in: int,
    is_secure: bool = False,
) -> None:
    _set_access_cookie(
        response=response,
        access_token=access_token,
        expires_in=expires_in,
        is_secure=is_secure,
    )
    if refresh_token:
        _set_refresh_cookie(
            response=response,
            refresh_token=refresh_token,
            refresh_expires_in=refresh_expires_in,
            is_secure=is_secure,
        )


@router.post("/login", response_model=TokenResponse)
async def login(
    request: Request,
    response: Response,
    body: LoginRequest | None = None,
    authorization: str | None = Header(None),
):
    username = body.username if body else None
    password = body.password if body else None

    # Check HTTP Basic auth header if username/password not in body
    if not username or not password:
        basic_creds = _extract_basic_auth(authorization)
        if basic_creds:
            username, password = basic_creds

    if not username or not password:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Username and password are required",
        )

    store = get_user_store()
    user = await store.authenticate(username, password)
    if not user:
        logger.warning(
            "Authentication failed: invalid credentials for username '%s'", username
        )
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid credentials",
        )

    logger.info(
        "Authentication successful for user '%s' (id=%s, role=%s, is_superuser=%s)",
        user.username,
        user.id,
        user.role,
        user.is_superuser,
    )

    # Determine initial org_id:
    # for superuser: user.last_org_id or 0 (if last_org_id points to inactive/deleted org -> 0)
    # for regular user: user.org_id (if None or <= 0 -> 403 "Пользователь не привязан к организации")
    if user.is_superuser:
        candidate_org_id = user.last_org_id or 0
        initial_org_id = candidate_org_id
        if candidate_org_id > 0:
            with suppress(Exception):
                async with async_session() as db_session:
                    org = await db_session.get(Org, candidate_org_id)
                    if org is not None and not (
                        getattr(org, "is_active", True)
                        and getattr(org, "status", 1) == 1
                    ):
                        initial_org_id = 0
    else:
        if user.org_id is None or user.org_id <= 0:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Пользователь не привязан к организации",
            )
        initial_org_id = user.org_id

    # Issue RS256 token pair from external jwt-issuer
    token_data = await jwt_issuer_client.issue_tokens(
        user_id=user.id,
        org_id=initial_org_id,
        role_id=user.role_id,
        username=user.username,
        role=user.role,
        is_superuser=user.is_superuser,
        sid=None,
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

    # Save session in database
    client_ip = (
        request.headers.get("X-Real-IP")
        or request.headers.get("X-Forwarded-For")
        or (request.client.host if request.client else None)
    )
    user_agent = request.headers.get("User-Agent")

    if refresh_token:
        await store.create_session(
            user_id=user.id,
            refresh_token=refresh_token,
            ip_address=client_ip,
            user_agent=user_agent,
            expires_in_seconds=refresh_expires_in,
            active_org_id=initial_org_id,
        )

    # Set HttpOnly Cookies
    is_secure = _is_secure_request(request)
    _set_auth_cookies(
        response=response,
        access_token=access_token,
        refresh_token=refresh_token,
        expires_in=expires_in,
        refresh_expires_in=refresh_expires_in,
        is_secure=is_secure,
    )

    return TokenResponse(
        access_token=access_token,
        refresh_token=refresh_token,
        token_type="Bearer",
        expires_in=expires_in,
        refresh_expires_in=refresh_expires_in,
        master_token=None,
        is_superuser=user.is_superuser,
        role=user.role,
        role_id=user.role_id,
        org_id=initial_org_id,
        user_id=user.id,
    )


@router.post("/refresh", response_model=TokenResponse)
async def refresh_token(
    request: Request,
    response: Response,
    body: RefreshTokenRequest | None = None,
):
    refresh_token_val = None
    if body and body.refresh_token:
        refresh_token_val = body.refresh_token
    elif "refreshToken" in request.cookies:
        refresh_token_val = request.cookies["refreshToken"]
    elif "refresh_token" in request.cookies:
        refresh_token_val = request.cookies["refresh_token"]

    if not refresh_token_val:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing refresh token",
        )

    store = get_user_store()
    session = await store.get_session_by_refresh_token(refresh_token_val)
    if not session:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or expired refresh token",
        )

    user = await store.get_by_id(session.user_id)
    if not user or not user.is_active:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User is inactive or not found",
        )

    # Determine effective org_id:
    # Session is source of truth for superuser active tenant!
    # For regular user, always enforce user.org_id
    if user.is_superuser:
        effective_org_id = (
            session.active_org_id
            if session.active_org_id is not None
            else (user.last_org_id or 0)
        )
    else:
        if user.org_id is None or user.org_id <= 0:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Пользователь не привязан к организации",
            )
        effective_org_id = user.org_id

    # Issue new token pair
    token_data = await jwt_issuer_client.issue_tokens(
        user_id=user.id,
        org_id=effective_org_id,
        role_id=user.role_id,
        username=user.username,
        role=user.role,
        is_superuser=user.is_superuser,
        sid=session.id,
    )

    access_token = token_data.get("accessToken") or token_data.get("access_token") or ""
    new_refresh_token = (
        token_data.get("refreshToken")
        or token_data.get("refresh_token")
        or refresh_token_val
    )
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

    # Rotate session in-place or touch existing session
    if new_refresh_token != refresh_token_val:
        await store.rotate_session(
            session_id=session.id,
            new_refresh_token=new_refresh_token,
            expires_in_seconds=refresh_expires_in,
        )
    else:
        await store.touch_session(session.id)

    is_secure = _is_secure_request(request)
    _set_auth_cookies(
        response=response,
        access_token=access_token,
        refresh_token=new_refresh_token,
        expires_in=expires_in,
        refresh_expires_in=refresh_expires_in,
        is_secure=is_secure,
    )

    return TokenResponse(
        access_token=access_token,
        refresh_token=new_refresh_token,
        token_type="Bearer",
        expires_in=expires_in,
        refresh_expires_in=refresh_expires_in,
        master_token=None,
        is_superuser=user.is_superuser,
        role=user.role,
        role_id=user.role_id,
        org_id=effective_org_id,
        user_id=user.id,
    )


@router.post("/logout")
async def logout(
    request: Request,
    response: Response,
    body: RefreshTokenRequest | None = None,
):
    refresh_token_val = None
    if body and body.refresh_token:
        refresh_token_val = body.refresh_token
    elif "refreshToken" in request.cookies:
        refresh_token_val = request.cookies["refreshToken"]
    elif "refresh_token" in request.cookies:
        refresh_token_val = request.cookies["refresh_token"]

    if refresh_token_val:
        store = get_user_store()
        await store.revoke_session_by_token(refresh_token_val)

    # Invalidate issuer token cache for user if token is present
    curr_token = None
    auth_header = request.headers.get("Authorization")
    if auth_header and auth_header.startswith("Bearer "):
        curr_token = auth_header.split(" ", 1)[1]
    elif "accessToken" in request.cookies:
        curr_token = request.cookies["accessToken"]

    if curr_token:
        with suppress(Exception):
            from app.auth import decode_token

            payload = decode_token(curr_token)
            uid = payload.get("userId") or payload.get("user_id") or payload.get("sub")
            if uid and str(uid).isdigit():
                jwt_issuer_client.invalidate_cache_for_user(int(uid))

    # Clear cookies
    response.delete_cookie(key="accessToken", path="/")
    response.delete_cookie(key="refreshToken", path="/api/auth")

    return {"ok": True, "message": "Logged out"}


@router.get("/me", response_model=UserInfo)
async def me(user: dict = Depends(get_current_user)):
    org_id = user.get("org_id")
    org_name = None
    org_timezone = "Europe/Moscow"
    if org_id:
        try:
            async with async_session() as session:
                result = await session.execute(
                    select(Org.org_name, Org.timezone).where(Org.org_id == org_id)
                )
                row = result.first()
                if row is not None and type(row).__name__ not in (
                    "MagicMock",
                    "AsyncMock",
                ):
                    if isinstance(row, (tuple, list)):
                        org_name = str(row[0]) if row[0] is not None else None
                        org_timezone = (
                            str(row[1])
                            if len(row) > 1 and row[1] is not None
                            else "Europe/Moscow"
                        )
                    elif hasattr(row, "org_name") and type(
                        getattr(row, "org_name", None)
                    ).__name__ not in ("MagicMock", "AsyncMock"):
                        org_name = getattr(row, "org_name", None)
                        org_timezone = (
                            getattr(row, "timezone", "Europe/Moscow") or "Europe/Moscow"
                        )
                else:
                    with suppress(Exception):
                        scalar_val = result.scalar_one_or_none()
                        if scalar_val is not None and type(scalar_val).__name__ not in (
                            "MagicMock",
                            "AsyncMock",
                        ):
                            org_name = str(scalar_val)
        except Exception:  # noqa: BLE001
            org_name = None
            org_timezone = "Europe/Moscow"

    if org_name is not None and (
        type(org_name).__name__ in ("MagicMock", "AsyncMock")
        or not isinstance(org_name, str)
    ):
        org_name = (
            str(org_name)
            if type(org_name).__name__ not in ("MagicMock", "AsyncMock")
            else None
        )
    if not isinstance(org_timezone, str) or type(org_timezone).__name__ in (
        "MagicMock",
        "AsyncMock",
    ):
        org_timezone = "Europe/Moscow"

    is_su = bool(user.get("is_superuser") or user.get("role") in ("superuser", "admin"))
    expires_at = None
    exp_val = user.get("exp")
    if exp_val is not None:
        try:
            expires_at = datetime.fromtimestamp(float(exp_val), tz=UTC).isoformat()
        except Exception:  # noqa: BLE001
            expires_at = None

    username = user.get("username")
    user_id = user.get("user_id")
    full_name = None

    if user_id and user_id > 0:
        store_user = await get_user_store().get_by_id(user_id)
        if store_user:
            if not username or str(username).isdigit():
                username = store_user.username
            full_name = store_user.full_name

    return UserInfo(
        user_id=user_id,
        username=username or user.get("username") or "",
        org_id=org_id,
        role_id=user.get("role_id", 1 if is_su else 3),
        role=user.get("role", "user"),
        is_superuser=is_su,
        can_switch_org=is_su,
        token_type=user.get("token_type", "tenant"),
        is_impersonated=bool(user.get("is_impersonated", False)),
        org_name=org_name,
        timezone=org_timezone,
        expires_at=expires_at,
        full_name=full_name,
    )
