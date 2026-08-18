import hashlib
import hmac
from datetime import UTC, datetime, timedelta

from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from jose import JWTError, jwt

from app.config import settings

security_scheme = HTTPBearer()


def verify_md5_password(plain_password: str, md5_hash: str) -> bool:
    calculated = hashlib.md5(plain_password.encode("utf-8")).hexdigest()
    return hmac.compare_digest(calculated, md5_hash.lower())


def find_user(username: str) -> dict | None:
    for u in settings.get_users():
        if u["username"] == username:
            return u
    return None


def create_access_token(data: dict) -> str:
    to_encode = data.copy()
    expire = datetime.now(UTC) + timedelta(minutes=settings.jwt_expire_minutes)
    to_encode.update({"exp": expire, "iat": datetime.now(UTC)})
    return jwt.encode(
        to_encode, settings.jwt_secret_bytes, algorithm=settings.jwt_algorithm
    )


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
        raise HTTPException(status_code=401, detail="Invalid token payload")
    raw_org_id = payload.get("org") or payload.get("orgId") or payload.get("org_id")
    try:
        org_id = int(raw_org_id) if raw_org_id is not None else None
    except TypeError, ValueError:
        org_id = None
    return {
        "username": username,
        "org_id": org_id,
    }
