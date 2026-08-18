from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel

from app.auth import (
    create_access_token,
    find_user,
    get_current_user,
    verify_md5_password,
)

router = APIRouter(prefix="/auth", tags=["auth"])


class LoginRequest(BaseModel):
    username: str
    password: str


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    expires_in: int


class UserInfo(BaseModel):
    username: str
    org_id: int | None = None


@router.post("/login", response_model=TokenResponse)
async def login(body: LoginRequest):
    user = find_user(body.username)
    if not user or not verify_md5_password(body.password, user["md5_password"]):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid credentials"
        )

    token = create_access_token({"sub": user["username"], "org": str(user.get("org_id", 0))})
    from app.config import settings

    return TokenResponse(
        access_token=token, expires_in=settings.jwt_expire_minutes * 60
    )


@router.get("/me", response_model=UserInfo)
async def me(user: dict = Depends(get_current_user)):
    return UserInfo(username=user["username"], org_id=user.get("org_id"))
