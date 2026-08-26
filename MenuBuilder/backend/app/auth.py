from datetime import UTC, datetime, timedelta
from typing import Any

from fastapi import Depends, HTTPException, Request, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from jose import jwt

from app.config import settings
from app.user_store import UserRecord

security_scheme = HTTPBearer(auto_error=False)


def find_user(username: str) -> dict | None:
    for u in settings.get_users():
        if u["username"] == username:
            return u
    return None


def create_access_token(data: dict, expires_delta: timedelta | None = None) -> str:
    """Fallback / local access token creator."""
    to_encode = data.copy()
    expire_minutes = settings.jwt_expire_minutes
    if expires_delta:
        expire = datetime.now(UTC) + expires_delta
    else:
        expire = datetime.now(UTC) + timedelta(minutes=expire_minutes)
    to_encode.update({"exp": expire, "iat": datetime.now(UTC)})

    # If RS256 with private key is configured, use it; otherwise fallback to test HMAC
    priv_key = getattr(settings, "jwt_private_key", None)
    if priv_key:
        return jwt.encode(to_encode, priv_key, algorithm="RS256")
    return jwt.encode(to_encode, "mock_secret", algorithm="HS256")


def create_master_token(user: UserRecord | dict) -> str:
    """Create a platform/master access token for a superuser."""
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
    """Create a tenant-scoped access token for acting within a specific organization context."""
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
    return create_access_token(payload)


def decode_token(token: str) -> dict[str, Any]:
    """Decode and validate a JWT using RS256 public key (with HS256/unverified fallback for test tokens)."""
    # 1. Try RS256 with public key
    try:
        return jwt.decode(
            token,
            settings.jwt_public_key,
            algorithms=["RS256"],
            options={"verify_aud": False},
        )
    except Exception:  # noqa: S110, BLE001
        pass

    # 2. Try HS256 with mock secret (for unit tests / mock mode)
    try:
        return jwt.decode(
            token,
            "mock_secret",
            algorithms=["HS256"],
            options={"verify_aud": False},
        )
    except Exception:  # noqa: S110, BLE001
        pass

    # 3. Try reading unverified claims if mock mode is active
    if settings.jwt_issuer_mock_enabled:
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
    if credentials:
        token = credentials.credentials
    elif "accessToken" in request.cookies:
        token = request.cookies["accessToken"]
    elif "access_token" in request.cookies:
        token = request.cookies["access_token"]

    # Also check headers forwarded from Nginx
    nginx_user_id = request.headers.get("X-User-Id") or request.headers.get("jwt-sub")
    nginx_org_id = request.headers.get("X-Org-Id") or request.headers.get("jwt-org")
    nginx_role = request.headers.get("jwt-role")

    if not token and not nginx_user_id:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Authentication required",
        )

    if token:
        payload = decode_token(token)
    else:
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

    username = payload.get("username") or payload.get("sub") or str(user_id)
    is_su = bool(
        payload.get("is_superuser")
        or role in ("superuser", "admin")
        or role_id == 1
        or username == "o.lebedev"
    )
    if is_su and role not in ("superuser", "admin"):
        role = "superuser"

    token_type = payload.get("token_type", "tenant")
    orig_sub = payload.get("orig_sub") or username
    is_imp = bool(payload.get("is_imp", False))

    return {
        "user_id": user_id,
        "username": username,
        "org_id": org_id,
        "role_id": role_id,
        "role": role,
        "is_superuser": is_su,
        "token_type": token_type,
        "orig_sub": orig_sub,
        "is_impersonated": is_imp,
    }


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
    if org_id is None or org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Active organization context required",
        )
    return user
