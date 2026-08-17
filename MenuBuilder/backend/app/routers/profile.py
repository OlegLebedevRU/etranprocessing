import uuid
from datetime import UTC, datetime, timedelta

from fastapi import APIRouter, Depends, HTTPException
from jose import jwt
from pydantic import BaseModel
from sqlalchemy import text

from app.auth import get_current_user
from app.config import settings
from app.database import async_session

router = APIRouter()


class CreateTokenRequest(BaseModel):
    name: str = "API Token"
    expires_days: int = 30


class TokenResponse(BaseModel):
    jti: str
    name: str | None
    expires_at: str
    created_at: str
    last_used_at: str | None
    revoked_at: str | None


@router.post("/profile/tokens")
async def create_token(
    req: CreateTokenRequest,
    user: dict = Depends(get_current_user),
):
    """Create a long-lived API token (returned once as JWT)."""
    jti = str(uuid.uuid4())
    expires_at = datetime.now(UTC) + timedelta(days=req.expires_days)

    async with async_session() as session:
        await session.execute(
            text(
                "INSERT INTO api_tokens (jti, user_id, name, expires_at) VALUES (:jti, :user_id, :name, :expires_at)"
            ),
            {
                "jti": jti,
                "user_id": user["username"],
                "name": req.name,
                "expires_at": expires_at,
            },
        )
        await session.commit()

    payload = {
        "sub": user["username"],
        "org_id": user["org_id"],
        "jti": jti,
        "exp": expires_at,
        "iat": datetime.now(UTC),
    }
    token = jwt.encode(
        payload, settings.jwt_secret_bytes, algorithm=settings.jwt_algorithm
    )

    return {
        "token": token,
        "jti": jti,
        "name": req.name,
        "expires_at": expires_at.isoformat(),
        "expires_days": req.expires_days,
    }


@router.get("/profile/tokens")
async def list_tokens(
    user: dict = Depends(get_current_user),
):
    """List API tokens for the current user."""
    async with async_session() as session:
        result = await session.execute(
            text(
                "SELECT jti, name, expires_at, created_at, last_used_at, revoked_at FROM api_tokens WHERE user_id = :user_id ORDER BY created_at DESC"
            ),
            {"user_id": user["username"]},
        )
        rows = result.fetchall()

    tokens = []
    for r in rows:
        tokens.append(
            TokenResponse(
                jti=r[0],
                name=r[1],
                expires_at=str(r[2]),
                created_at=str(r[3]),
                last_used_at=str(r[4]) if r[4] else None,
                revoked_at=str(r[5]) if r[5] else None,
            )
        )

    return {"tokens": tokens}


@router.delete("/profile/tokens/{jti}")
async def revoke_token(
    jti: str,
    user: dict = Depends(get_current_user),
):
    """Revoke an API token."""
    async with async_session() as session:
        result = await session.execute(
            text(
                "UPDATE api_tokens SET revoked_at = NOW() WHERE jti = :jti AND user_id = :user_id AND revoked_at IS NULL RETURNING jti"
            ),
            {"jti": jti, "user_id": user["username"]},
        )
        row = result.fetchone()
        await session.commit()

    if not row:
        raise HTTPException(
            status_code=404, detail="Token not found or already revoked"
        )

    return {"jti": jti, "status": "revoked"}


@router.get("/profile/me")
async def get_profile(
    user: dict = Depends(get_current_user),
):
    """Get current user profile."""
    return {
        "username": user["username"],
        "org_id": user["org_id"],
    }
