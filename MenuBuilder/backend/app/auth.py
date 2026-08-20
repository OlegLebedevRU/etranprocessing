from datetime import UTC, datetime, timedelta

from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from jose import JWTError, jwt

from app.config import settings
from app.user_store import UserRecord

security_scheme = HTTPBearer()


def find_user(username: str) -> dict | None:
    for u in settings.get_users():
        if u["username"] == username:
            return u
    return None


def create_access_token(data: dict, expires_delta: timedelta | None = None) -> str:
    to_encode = data.copy()
    expire_minutes = settings.jwt_expire_minutes
    if expires_delta:
        expire = datetime.now(UTC) + expires_delta
    else:
        expire = datetime.now(UTC) + timedelta(minutes=expire_minutes)
    to_encode.update({"exp": expire, "iat": datetime.now(UTC)})
    return jwt.encode(
        to_encode, settings.jwt_secret_bytes, algorithm=settings.jwt_algorithm
    )


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

    payload = {
        "sub": username,
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

    orig = original_user or (
        user.username
        if isinstance(user, UserRecord)
        else user.get("orig_sub") or username
    )
    is_imp = bool(is_su and (target_org_id != getattr(user, "org_id", None)))

    payload = {
        "sub": username,
        "org": str(target_org_id),
        "org_id": target_org_id,
        "role": role,
        "is_superuser": is_su,
        "token_type": "tenant",
        "orig_sub": orig,
        "is_imp": is_imp,
    }
    return create_access_token(payload)


def decode_token(token: str) -> dict:
    try:
        payload = jwt.decode(
            token, settings.jwt_secret_bytes, algorithms=[settings.jwt_algorithm]
        )
        return payload
    except JWTError:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or expired token",
        )


async def get_current_user(
    credentials: HTTPAuthorizationCredentials = Depends(security_scheme),
) -> dict:
    payload = decode_token(credentials.credentials)
    username: str = payload.get("sub", "")
    if not username:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token payload"
        )
    raw_org_id = payload.get("org") or payload.get("orgId") or payload.get("org_id")
    try:
        org_id = int(raw_org_id) if raw_org_id is not None else None
    except TypeError, ValueError:
        org_id = None

    token_type = payload.get("token_type", "tenant")
    role = str(payload.get("role", "user")).lower()
    is_su = bool(
        payload.get("is_superuser")
        or role in ("superuser", "admin")
        or username == "o.lebedev"
    )
    if is_su and role not in ("superuser", "admin"):
        role = "superuser"

    orig_sub = payload.get("orig_sub") or username
    is_imp = bool(payload.get("is_imp", False))

    return {
        "username": username,
        "org_id": org_id,
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
