from __future__ import annotations

import logging
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Request, status
from pydantic import BaseModel, Field
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.services.registration_service import (
    InvalidTokenError,
    RateLimitError,
    RegistrationConflictError,
    RegistrationDisabledError,
    RegistrationService,
    TokenExpiredError,
    ValidationError,
)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/auth", tags=["registration"])


class RegisterRequest(BaseModel):
    email: str = Field(..., description="Email пользователя для регистрации")
    password: str = Field(
        ..., min_length=8, max_length=128, description="Пароль пользователя"
    )
    terms_version: str = Field(
        default="v1", description="Версия принятых условий оферты"
    )
    timezone: str = Field(
        default="Europe/Moscow", description="Часовой пояс организации"
    )
    source: str | None = Field(default=None, description="Источник перехода / реферер")
    return_url: str | None = Field(
        default=None, description="Безопасный URL возврата после подтверждения"
    )


class ResendConfirmationRequest(BaseModel):
    email: str = Field(
        ..., description="Email пользователя для повторной отправки ссылки"
    )
    return_url: str | None = Field(default=None, description="Безопасный URL возврата")


class ConfirmRegistrationRequest(BaseModel):
    token: str = Field(..., description="Одноразовый токен подтверждения email")
    return_url: str | None = Field(default=None, description="Безопасный URL возврата")


class GenericRegistrationResponse(BaseModel):
    status: str
    message: str
    email: str | None = None
    return_url: str = "/monitoring"


class ConfirmRegistrationResponse(BaseModel):
    status: str
    message: str
    tenant_id: int | None = None
    user_id: int | None = None
    email: str | None = None
    return_url: str = "/monitoring"


class RegistrationStatusResponse(BaseModel):
    enabled: bool
    terms_version: str
    token_expire_hours: int


def _get_client_ip(request: Request) -> str:
    """Extract client IP respecting reverse proxy headers."""
    forwarded = request.headers.get("X-Forwarded-For")
    if forwarded:
        return forwarded.split(",")[0].strip()
    real_ip = request.headers.get("X-Real-IP")
    if real_ip:
        return real_ip.strip()
    if request.client:
        return request.client.host
    return "127.0.0.1"


@router.get("/register/status", response_model=RegistrationStatusResponse)
@router.get("/registration/status", response_model=RegistrationStatusResponse)
async def get_registration_status() -> RegistrationStatusResponse:
    """Return whether L4Desk public self-registration is enabled and current terms version."""
    return RegistrationStatusResponse(
        enabled=settings.l4desk_registration_enabled,
        terms_version=settings.l4desk_terms_current_version,
        token_expire_hours=settings.l4desk_registration_token_expire_hours,
    )


@router.post(
    "/register",
    response_model=GenericRegistrationResponse,
    status_code=status.HTTP_200_OK,
)
async def register_account(
    request: Request,
    body: RegisterRequest,
    db: AsyncSession = Depends(get_db),
) -> Any:
    """Public self-registration endpoint for L4Desk tenant owners.

    Returns generic anti-enumeration response to protect user privacy.
    """
    client_ip = _get_client_ip(request)
    service = RegistrationService(db)

    try:
        result = await service.register(
            email=body.email,
            password=body.password,
            terms_version=body.terms_version,
            timezone=body.timezone,
            source=body.source,
            return_url=body.return_url,
            ip_address=client_ip,
        )
        return GenericRegistrationResponse(**result)
    except RegistrationDisabledError as exc:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail=str(exc),
        ) from exc
    except RateLimitError as exc:
        headers = {"Retry-After": str(exc.retry_after)}
        raise HTTPException(
            status_code=status.HTTP_429_TOO_MANY_REQUESTS,
            detail=str(exc),
            headers=headers,
        ) from exc
    except ValidationError as exc:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_CONTENT,
            detail=str(exc),
        ) from exc


@router.post(
    "/register/resend",
    response_model=GenericRegistrationResponse,
    status_code=status.HTTP_200_OK,
)
async def resend_confirmation_email(
    request: Request,
    body: ResendConfirmationRequest,
    db: AsyncSession = Depends(get_db),
) -> Any:
    """Resend email verification link with rate limits and anti-enumeration."""
    client_ip = _get_client_ip(request)
    service = RegistrationService(db)

    try:
        result = await service.resend_confirmation(
            email=body.email,
            return_url=body.return_url,
            ip_address=client_ip,
        )
        return GenericRegistrationResponse(**result)
    except RegistrationDisabledError as exc:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail=str(exc),
        ) from exc
    except RateLimitError as exc:
        headers = {"Retry-After": str(exc.retry_after)}
        raise HTTPException(
            status_code=status.HTTP_429_TOO_MANY_REQUESTS,
            detail=str(exc),
            headers=headers,
        ) from exc
    except ValidationError as exc:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_CONTENT,
            detail=str(exc),
        ) from exc


@router.post(
    "/register/confirm",
    response_model=ConfirmRegistrationResponse,
    status_code=status.HTTP_200_OK,
)
@router.post(
    "/confirm-email",
    response_model=ConfirmRegistrationResponse,
    status_code=status.HTTP_200_OK,
)
async def confirm_email_and_provision_tenant(
    body: ConfirmRegistrationRequest,
    db: AsyncSession = Depends(get_db),
) -> Any:
    """Verify email token and atomically create User (role=5), Tenant, Owner Membership, and Audit."""
    service = RegistrationService(db)
    try:
        result = await service.confirm_registration(
            token=body.token,
            return_url=body.return_url,
        )
        return ConfirmRegistrationResponse(**result)
    except InvalidTokenError as exc:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=str(exc),
        ) from exc
    except TokenExpiredError as exc:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=str(exc),
        ) from exc
    except RegistrationConflictError as exc:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=str(exc),
        ) from exc
