import logging
import uuid
from contextlib import suppress
from datetime import UTC, datetime, timedelta
from typing import Any

from fastapi import Depends, HTTPException, Request, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from jose import jwt

from app.config import settings
from app.user_store import UserRecord

logger = logging.getLogger(__name__)

security_scheme = HTTPBearer(auto_error=False)


ROLE_SUPERUSER = 1
ROLE_ADMIN = 2
ROLE_USER = 3
ROLE_VIEWER = 4
ROLE_L4DESK_OWNER = 5
ROLE_ID_TO_NAME: dict[int, str] = {
    1: "superuser",
    2: "admin",
    3: "user",
    4: "viewer",
    5: "l4desk_owner",
}
NAME_TO_ROLE_ID: dict[str, int] = {v: k for k, v in ROLE_ID_TO_NAME.items()}


def find_user(username: str) -> dict | None:
    for u in settings.get_users():
        if isinstance(u, dict) and u.get("username") == username:
            return u
    return None


def create_access_token(data: dict, expires_delta: timedelta | None = None) -> str:
    """DEPRECATED: Fallback / local access token creator. Kept for test harnesses only."""
    to_encode = data.copy()
    expire_minutes = settings.jwt_expire_minutes
    if expires_delta:
        expire = datetime.now(UTC) + expires_delta
    else:
        expire = datetime.now(UTC) + timedelta(minutes=expire_minutes)
    to_encode.update({"exp": expire, "iat": datetime.now(UTC)})
    to_encode.setdefault("jti", str(uuid.uuid4()))
    to_encode.setdefault("session_id", to_encode["jti"])

    # If RS256 with private key is configured, use it; otherwise fallback to test HMAC
    priv_key = getattr(settings, "jwt_private_key", None)
    if priv_key:
        return jwt.encode(to_encode, priv_key, algorithm="RS256")
    return jwt.encode(to_encode, "mock_secret", algorithm="HS256")


def create_master_token(user: UserRecord | dict) -> str:
    """DEPRECATED: Create a platform/master access token for a superuser. Kept for legacy tests only."""
    username = user.username if isinstance(user, UserRecord) else user["username"]
    role = user.role if isinstance(user, UserRecord) else user.get("role", "superuser")
    is_su = (
        user.is_superuser
        if isinstance(user, UserRecord)
        else user.get("is_superuser", True)
    )
    org_id = user.org_id if isinstance(user, UserRecord) else user.get("org_id")
    user_id = user.id if isinstance(user, UserRecord) else user.get("id", 1)

    payload = {
        "sub": username,
        "userId": user_id,
        "roleId": 1,
        "role": role,
        "is_superuser": is_su,
        "token_type": "master",
        "org": str(org_id) if org_id is not None else "0",
        "org_id": org_id,
        "can_switch_org": True,
    }
    return create_access_token(payload)


def create_tenant_token(
    user: UserRecord | dict,
    target_org_id: int,
    original_user: str | None = None,
) -> str:
    """DEPRECATED: Create a tenant-scoped access token for acting within a specific organization context."""
    username = user.username if isinstance(user, UserRecord) else user["username"]
    role = user.role if isinstance(user, UserRecord) else user.get("role", "user")
    is_su = (
        user.is_superuser
        if isinstance(user, UserRecord)
        else user.get("is_superuser", False)
    )
    user_id = user.id if isinstance(user, UserRecord) else user.get("id", 0)
    role_id = user.role_id if isinstance(user, UserRecord) else user.get("role_id", 3)

    orig = original_user or (
        user.username
        if isinstance(user, UserRecord)
        else user.get("orig_sub") or username
    )
    is_imp = bool(is_su and (target_org_id != getattr(user, "org_id", None)))

    payload = {
        "sub": username,
        "userId": user_id,
        "roleId": role_id,
        "org": str(target_org_id),
        "org_id": target_org_id,
        "role": role,
        "is_superuser": is_su,
        "token_type": "tenant",
        "orig_sub": orig,
        "is_imp": is_imp,
    }
    perms = (
        user.permissions if isinstance(user, UserRecord) else user.get("permissions")
    )
    if perms is not None:
        payload["permissions"] = list(perms)
    return create_access_token(payload)


def decode_token(token: str) -> dict[str, Any]:
    """Decode and validate a JWT.

    Production path: RS256 with the external issuer's public key, aud/iss verified.
    Mock/HS256/unverified paths are available ONLY when jwt_issuer_mock_enabled is on
    (unit tests, local dev) — never in production.
    """
    # 1. RS256 from the external issuer
    try:
        decode_kwargs: dict[str, Any] = {"options": {"verify_aud": False}}
        if settings.jwt_verify_audience:
            decode_kwargs = {
                "audience": settings.jwt_issuer_aud,
                "issuer": settings.jwt_issuer_iss,
                "options": {"verify_aud": True},
            }
        return jwt.decode(
            token,
            settings.jwt_public_key,
            algorithms=["RS256"],
            **decode_kwargs,
        )
    except Exception:  # noqa: S110, BLE001
        pass

    # 2/3. Test-only fallbacks, gated by mock mode
    if settings.jwt_issuer_mock_enabled:
        try:
            return jwt.decode(
                token,
                "mock_secret",
                algorithms=["HS256"],
                options={"verify_aud": False},
            )
        except Exception:  # noqa: S110, BLE001
            pass
        try:
            return jwt.get_unverified_claims(token)
        except Exception:  # noqa: S110, BLE001
            pass

    raise HTTPException(
        status_code=status.HTTP_401_UNAUTHORIZED,
        detail="Invalid or expired token",
    )


async def get_current_user(
    request: Request,
    credentials: HTTPAuthorizationCredentials | None = Depends(security_scheme),
) -> dict[str, Any]:
    token: str | None = None
    if isinstance(credentials, HTTPAuthorizationCredentials):
        token = credentials.credentials
    elif "accessToken" in request.cookies:
        token = request.cookies["accessToken"]

    # Also check headers forwarded from Nginx (only trusted if configured)
    nginx_user_id = request.headers.get("X-User-Id")
    nginx_org_id = request.headers.get("X-Org-Id")
    nginx_role = request.headers.get("X-User-Role")

    if not token:
        if settings.trust_proxy_identity_headers and nginx_user_id:
            payload = {
                "sub": nginx_user_id,
                "userId": int(nginx_user_id)
                if nginx_user_id and nginx_user_id.isdigit()
                else 0,
                "orgId": int(nginx_org_id)
                if nginx_org_id and nginx_org_id.isdigit()
                else 0,
                "role": nginx_role or "user",
            }
        else:
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Authentication required",
            )
    else:
        payload = decode_token(token)

    raw_user_id = payload.get("userId") or payload.get("user_id") or payload.get("sub")
    raw_org_id = payload.get("orgId") or payload.get("org_id") or payload.get("org")
    raw_role_id = payload.get("roleId") or payload.get("role_id")
    role = str(payload.get("role", "user")).lower()

    try:
        user_id = (
            int(raw_user_id)
            if raw_user_id is not None and str(raw_user_id).isdigit()
            else 0
        )
    except TypeError, ValueError:
        user_id = 0

    try:
        org_id = (
            int(raw_org_id)
            if raw_org_id is not None and str(raw_org_id).isdigit()
            else None
        )
    except TypeError, ValueError:
        org_id = None

    try:
        role_id = (
            int(raw_role_id)
            if raw_role_id is not None and str(raw_role_id).isdigit()
            else 3
        )
    except TypeError, ValueError:
        role_id = 3

    # External issuer emits role as a numeric string ("1"/"2"/"3"/"4"/"5"); normalize to names
    role_names = {1: "superuser", 2: "admin", 3: "user", 4: "viewer", 5: "l4desk_owner"}
    if role.isdigit():
        role = role_names.get(int(role), "user")

    if role == "viewer" or role_id == 4:
        role = "viewer"
        role_id = 4
    elif role == "l4desk_owner" or role_id == 5:
        role = "l4desk_owner"
        role_id = 5

    username = payload.get("username") or payload.get("sub") or str(user_id)
    is_su = bool(
        (payload.get("is_superuser") or role in ("superuser", "admin") or role_id == 1)
        and role_id not in (4, 5)
    )
    if is_su and role not in ("superuser", "admin"):
        role = "superuser"

    token_type = payload.get("token_type", "tenant")
    orig_sub = payload.get("orig_sub") or username
    # v2 issuer sends is_imp explicitly; for older tokens derive it: superuser inside a tenant
    if role_id in (4, 5):
        is_imp = False
    elif "is_imp" in payload:
        is_imp = bool(payload.get("is_imp"))
    else:
        is_imp = bool(is_su and org_id is not None and org_id > 0)

    # Permissions resolution: roles 1, 2, 3, 5 and superusers always have full access
    if role_id in (1, 2, 3, 5) or is_su:
        from app.security.permissions import ALL_PERMISSIONS

        user_perms = list(ALL_PERMISSIONS)
    elif "permissions" in payload and payload["permissions"] is not None:
        user_perms = list(payload["permissions"])
    elif role_id == 4:
        if user_id and user_id > 0:
            from app.user_store import get_user_store

            store_user = await get_user_store().get_by_id(user_id)
            user_perms = list(store_user.permissions) if store_user else []
        else:
            user_perms = []
    else:
        user_perms = []

    req_method = (
        request.scope.get("method")
        if hasattr(request, "scope") and isinstance(request.scope, dict)
        else getattr(request, "method", None)
    )
    if is_imp and req_method in ("POST", "PUT", "PATCH", "DELETE"):
        req_path = (
            request.url.path
            if hasattr(request, "url") and hasattr(request.url, "path")
            else ""
        )
        logger.info(
            "audit impersonated action user=%s orig_sub=%s org=%s method=%s path=%s",
            username,
            orig_sub,
            org_id,
            req_method,
            req_path,
        )

    return {
        "sub": str(payload.get("sub") or username or user_id),
        "user_id": user_id,
        "username": username,
        "org_id": org_id,
        "role_id": role_id,
        "role": role,
        "is_superuser": is_su,
        "can_switch_org": False if role_id == 4 else is_su,
        "token_type": token_type,
        "orig_sub": orig_sub,
        "is_impersonated": is_imp,
        "exp": payload.get("exp"),
        "sid": payload.get("sid"),
        "session_id": str(
            payload.get("session_id")
            or payload.get("sid")
            or payload.get("jti")
            or f"sess-{user_id}"
        ),
        "jti": payload.get("jti"),
        "permissions": user_perms,
    }


async def get_current_user_optional(
    request: Request,
    credentials: HTTPAuthorizationCredentials | None = Depends(security_scheme),
) -> dict[str, Any] | None:
    """Optional user dependency: returns None if unauthenticated, otherwise returns current user dict."""
    app_instance = getattr(request, "app", None)
    if app_instance and hasattr(app_instance, "dependency_overrides"):
        override = app_instance.dependency_overrides.get(get_current_user)
        if override:
            res = override()
            return await res if hasattr(res, "__await__") else res

    token: str | None = None
    if credentials:
        token = credentials.credentials
    elif "accessToken" in request.cookies:
        token = request.cookies["accessToken"]

    nginx_user_id = request.headers.get("X-User-Id")
    if not token and not (settings.trust_proxy_identity_headers and nginx_user_id):
        return None

    with suppress(HTTPException):
        return await get_current_user(request, credentials)

    return None


async def require_superuser(
    user: dict = Depends(get_current_user),
) -> dict:
    """Dependency: strictly require superuser/admin privileges."""
    if not user.get("is_superuser") and user.get("role") not in ("superuser", "admin"):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Superuser privilege required for this operation",
        )
    return user


async def require_tenant_context(
    user: dict = Depends(get_current_user),
) -> dict:
    """Dependency: require active tenant context (org_id > 0)."""
    org_id = user.get("org_id")
    if org_id in (None, 0) or (isinstance(org_id, int) and org_id <= 0):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Выберите организацию",
        )
    return user


def resolve_org_id(user: dict[str, Any], requested: int | None = None) -> int:
    """Resolve and enforce effective org_id according to authorization Rule #2.

    - Token/session is the source of truth for org_id.
    - If user has no active tenant context (org_id in (None, 0)), raises 403 'Выберите организацию'.
    - If client provided requested org_id and it does NOT match user's active org_id:
      raises 403 (both for regular users and for impersonated superusers).
    """
    token_org = user.get("org_id")
    if token_org in (None, 0) or (isinstance(token_org, int) and token_org <= 0):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Выберите организацию",
        )
    effective_org_id = int(token_org)
    if requested is not None and requested != effective_org_id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail=f"Access denied: requested org_id {requested} does not match active tenant {effective_org_id}",
        )
    return effective_org_id
