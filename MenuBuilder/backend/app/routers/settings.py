from __future__ import annotations

import hashlib
import re
import secrets
from datetime import UTC, datetime, timedelta
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Query, Response, status
from pydantic import BaseModel, Field
from sqlalchemy import String, cast, desc, func, or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.config import settings
from app.database import get_db
from app.models import EmailVerification, Org, Terminal, User
from app.security.permissions import (
    PERMISSION_SETTINGS_TERMINALS_VIEW,
    require_permission,
    require_readonly_guard,
)
from app.services.email_service import send_email_with_logging
from app.user_store import verify_password

router = APIRouter(prefix="/settings", tags=["settings"])

EMAIL_REGEX = re.compile(r"^[^@\s]+@[^@\s]+\.[^@\s]+$")


def _check_settings_access(user: dict[str, Any]) -> None:
    """Ensure user is either role_id == 3 or superuser."""
    is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)
    role_id = int(user.get("role_id", 0))
    if not is_su and role_id != 3:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Доступ к разделу Настройки разрешен только для пользователей с ролью 3 и суперпользователя",
        )


def _ensure_not_superuser(user: dict[str, Any]) -> None:
    """Ensure superuser cannot modify settings (readonly in this section)."""
    is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)
    if is_su:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Суперпользователь имеет доступ к разделу Настройки только для чтения. Для управления используйте раздел Администрирование.",
        )
    role_id = int(user.get("role_id", 0))
    if role_id != 3:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Изменение настроек разрешено только для пользователей с ролью 3",
        )


# --- Schemas ---


class ProfileResponse(BaseModel):
    username: str
    user_id: int
    role: str
    role_id: int
    is_superuser: bool
    is_readonly: bool
    org_id: int
    org_name: str
    email: str | None
    phone: str | None
    timezone: str
    notify_by_email: bool
    send_reports: bool
    is_email_verified: bool
    email_verified_at: str | None


class UpdateProfileRequest(BaseModel):
    phone: str | None = None
    timezone: str | None = None
    notify_by_email: bool | None = None
    send_reports: bool | None = None


class ChangePasswordRequest(BaseModel):
    old_password: str = Field(..., min_length=1)
    new_password: str = Field(..., min_length=6)


class RequestEmailVerificationRequest(BaseModel):
    email: str = Field(..., min_length=3, max_length=255)


class ConfirmEmailOtpRequest(BaseModel):
    code: str = Field(..., min_length=4, max_length=10)


class ConfirmEmailTokenRequest(BaseModel):
    token: str = Field(..., min_length=10)


class TerminalSettingsItem(BaseModel):
    id: int
    device_id: int
    sn: str
    address: str | None
    note: str | None
    timezone: str | None
    is_active: bool
    cert_serial: str | None
    cert_not_valid_after: str | None
    created_at: str | None
    updated_at: str | None


class TerminalSettingsListResponse(BaseModel):
    items: list[TerminalSettingsItem]
    total_count: int
    page: int
    page_size: int


class UpdateTerminalSettingsRequest(BaseModel):
    address: str | None = None
    note: str | None = None
    timezone: str | None = None


# --- Endpoints: Profile ---


@router.get("/profile", response_model=ProfileResponse)
async def get_profile(
    org_id: int | None = Query(None, description="Org ID for superuser viewing"),
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> ProfileResponse:
    """Get current user & organization profile settings."""
    _check_settings_access(user)
    is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)

    effective_org_id = int(user.get("org_id", 0))
    if is_su and org_id is not None and org_id > 0:
        effective_org_id = org_id

    org: Org | None = None
    if effective_org_id > 0:
        res = await db.execute(select(Org).where(Org.org_id == effective_org_id))
        org = res.scalar_one_or_none()

    org_name = org.org_name if org else "Не выбрана организация"
    email = org.email if org else None
    phone = org.phone if org else None
    tz = org.timezone if org else "Europe/Moscow"
    notify = org.notify_by_email if org else True
    send_rep = getattr(org, "send_reports", True) if org else True
    is_verified = getattr(org, "is_email_verified", False) if org else False
    org_verified_at = org.email_verified_at if org else None
    verified_at = org_verified_at.isoformat() if org_verified_at is not None else None

    return ProfileResponse(
        username=str(user.get("username", "")),
        user_id=int(user.get("id", 0)),
        role=str(user.get("role", "user")),
        role_id=int(user.get("role_id", 3)),
        is_superuser=is_su,
        is_readonly=is_su,
        org_id=effective_org_id,
        org_name=org_name,
        email=email,
        phone=phone,
        timezone=tz,
        notify_by_email=notify,
        send_reports=send_rep,
        is_email_verified=is_verified,
        email_verified_at=verified_at,
    )


@router.patch("/profile", response_model=ProfileResponse)
async def update_profile(
    req: UpdateProfileRequest,
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> ProfileResponse:
    """Update organization profile: phone, timezone, notify_by_email, send_reports (role 3 only)."""
    _check_settings_access(user)
    _ensure_not_superuser(user)

    org_id = int(user.get("org_id", 0))
    if org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Пользователь не привязан к организации",
        )

    res = await db.execute(select(Org).where(Org.org_id == org_id))
    org = res.scalar_one_or_none()
    if not org:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Организация не найдена",
        )

    if req.phone is not None:
        org.phone = req.phone.strip() or None
    if req.timezone is not None:
        tz = req.timezone.strip()
        if tz:
            org.timezone = tz
    if req.notify_by_email is not None:
        org.notify_by_email = req.notify_by_email
    if req.send_reports is not None:
        org.send_reports = req.send_reports

    org.updated_at = datetime.now(UTC)
    await db.commit()
    await db.refresh(org)

    org_verified_at = org.email_verified_at
    verified_at = org_verified_at.isoformat() if org_verified_at is not None else None

    return ProfileResponse(
        username=str(user.get("username", "")),
        user_id=int(user.get("id", 0)),
        role=str(user.get("role", "user")),
        role_id=int(user.get("role_id", 3)),
        is_superuser=False,
        is_readonly=False,
        org_id=org.org_id,
        org_name=org.org_name,
        email=org.email,
        phone=org.phone,
        timezone=org.timezone,
        notify_by_email=org.notify_by_email,
        send_reports=org.send_reports,
        is_email_verified=org.is_email_verified,
        email_verified_at=verified_at,
    )


@router.post("/profile/change-password")
async def change_password(
    req: ChangePasswordRequest,
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Change own password. Allowed ONLY if the organization's email is already verified."""
    _check_settings_access(user)
    _ensure_not_superuser(user)

    org_id = int(user.get("org_id", 0))
    if org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Пользователь не привязан к организации",
        )

    res = await db.execute(select(Org).where(Org.org_id == org_id))
    org = res.scalar_one_or_none()
    if not org:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Организация не найдена",
        )

    # Check that email is confirmed
    if not getattr(org, "is_email_verified", False) or not org.email:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Смена пароля возможна только при ранее подтвержденном email организации. Сначала подтвердите email в профиле.",
        )

    user_id = int(user.get("id", 0))
    u_res = await db.execute(select(User).where(User.id == user_id))
    db_user = u_res.scalar_one_or_none()
    if not db_user:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Пользователь не найден",
        )

    # Verify old password
    if not verify_password(req.old_password, db_user.md5_password):
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Неверный текущий пароль",
        )

    # Hash new password
    new_hash = hashlib.md5(req.new_password.encode("utf-8")).hexdigest()
    db_user.md5_password = new_hash
    db_user.updated_at = datetime.now(UTC)

    await db.commit()

    return {"ok": True, "message": "Пароль успешно изменен"}


@router.post("/profile/request-email-verification")
async def request_email_verification(
    req: RequestEmailVerificationRequest,
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Request verification code & link for setting or updating organization email."""
    _check_settings_access(user)
    _ensure_not_superuser(user)

    email_clean = str(req.email).strip().lower()
    if not EMAIL_REGEX.match(email_clean):
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Указан некорректный адрес электронной почты",
        )

    org_id = int(user.get("org_id", 0))
    if org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Пользователь не привязан к организации",
        )

    res = await db.execute(select(Org).where(Org.org_id == org_id))
    org = res.scalar_one_or_none()
    if not org:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Организация не найдена",
        )

    # Cooldown check: max 1 request every 60 seconds
    recent_check = await db.execute(
        select(EmailVerification)
        .where(
            EmailVerification.org_id == org_id,
            EmailVerification.created_at > datetime.now(UTC) - timedelta(seconds=60),
        )
        .order_by(desc(EmailVerification.created_at))
    )
    recent = recent_check.scalar_one_or_none()
    if recent:
        diff_sec = int(60 - (datetime.now(UTC) - recent.created_at).total_seconds())
        if diff_sec > 0:
            raise HTTPException(
                status_code=status.HTTP_429_TOO_MANY_REQUESTS,
                detail=f"Повторная отправка кода возможна через {diff_sec} сек.",
            )

    # Generate token & OTP
    raw_token = secrets.token_urlsafe(32)
    token_hash = hashlib.sha256(raw_token.encode("utf-8")).hexdigest()
    otp_code = f"{secrets.randbelow(900000) + 100000:06d}"
    expires_at = datetime.now(UTC) + timedelta(hours=24)
    user_id = int(user.get("id", 0)) or None

    verification = EmailVerification(
        org_id=org_id,
        user_id=user_id,
        email=email_clean,
        token_hash=token_hash,
        otp_code=otp_code,
        attempts_left=5,
        expires_at=expires_at,
        is_used=False,
    )
    db.add(verification)
    await db.commit()

    # Build email body and link
    frontend_base = settings.frontend_base_url.rstrip("/")
    verification_link = f"{frontend_base}/settings/verify-email?token={raw_token}"

    message = (
        f"Здравствуйте!\n\n"
        f"Для подтверждения адреса электронной почты организации «{org.org_name}» в системе etranprocessing используйте код:\n\n"
        f"    {otp_code}\n\n"
        f"Или перейдите по ссылке:\n"
        f"{verification_link}\n\n"
        f"Код и ссылка действительны в течение 24 часов.\n"
        f"Если вы не запрашивали подтверждение email, проигнорируйте это письмо.\n"
    )

    device_id = f"sys-verify-org-{org_id}"
    try:
        await send_email_with_logging(
            db,
            device_id=device_id,
            recipients=[email_clean],
            subject="Подтверждение адреса электронной почты - etranprocessing",
            message=message,
            org_id=org_id,
            user_id=user_id,
        )
    except Exception as exc:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail=f"Не удалось отправить письмо с кодом подтверждения: {exc}",
        ) from exc

    return {
        "ok": True,
        "message": "Код подтверждения отправлен на указанный email",
        "email": email_clean,
    }


@router.post("/profile/confirm-email-otp")
async def confirm_email_otp(
    req: ConfirmEmailOtpRequest,
    user: dict[str, Any] = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Confirm email using 6-digit OTP code (role 3 only)."""
    _check_settings_access(user)
    _ensure_not_superuser(user)

    org_id = int(user.get("org_id", 0))
    if org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Пользователь не привязан к организации",
        )

    res = await db.execute(select(Org).where(Org.org_id == org_id))
    org = res.scalar_one_or_none()
    if not org:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Организация не найдена",
        )

    # Find latest unexpired, unused verification for this org
    v_res = await db.execute(
        select(EmailVerification)
        .where(
            EmailVerification.org_id == org_id,
            EmailVerification.is_used.is_(False),
            EmailVerification.expires_at > datetime.now(UTC),
        )
        .order_by(desc(EmailVerification.created_at))
    )
    verification = v_res.scalar_one_or_none()
    if not verification:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Запрос на подтверждение email не найден или срок его действия истек. Запросите новый код.",
        )

    if verification.attempts_left <= 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Превышено количество попыток ввода кода. Запросите новый код.",
        )

    if verification.otp_code != req.code.strip():
        verification.attempts_left -= 1
        await db.commit()
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Неверный код подтверждения. Осталось попыток: {verification.attempts_left}",
        )

    # Success
    verification.is_used = True
    org.email = verification.email
    org.is_email_verified = True
    org.email_verified_at = datetime.now(UTC)
    org.updated_at = datetime.now(UTC)

    await db.commit()
    await db.refresh(org)

    org_verified_at = org.email_verified_at
    return {
        "ok": True,
        "message": "Email успешно подтвержден и привязан к организации",
        "email": org.email,
        "is_email_verified": True,
        "email_verified_at": (
            org_verified_at.isoformat() if org_verified_at is not None else None
        ),
    }


@router.post("/profile/confirm-email-token")
@router.get("/profile/confirm-email-token")
@router.post("/verify-email")
@router.get("/verify-email")
async def confirm_email_token(
    token: str | None = Query(None),
    body: ConfirmEmailTokenRequest | None = None,
    db: AsyncSession = Depends(get_db),
) -> dict[str, Any]:
    """Confirm email using token link (can be called publicly from email link)."""
    raw_token = token or (body.token if body else None)
    if not raw_token:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Токен подтверждения не передан",
        )

    token_hash = hashlib.sha256(raw_token.strip().encode("utf-8")).hexdigest()
    res = await db.execute(
        select(EmailVerification).where(
            EmailVerification.token_hash == token_hash,
            EmailVerification.is_used.is_(False),
            EmailVerification.expires_at > datetime.now(UTC),
        )
    )
    verification = res.scalar_one_or_none()
    if not verification:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Ссылка подтверждения недействительна или срок ее действия истек. Запросите подтверждение повторно.",
        )

    org_res = await db.execute(select(Org).where(Org.org_id == verification.org_id))
    org = org_res.scalar_one_or_none()
    if not org:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Связанная организация не найдена",
        )

    verification.is_used = True
    org.email = verification.email
    org.is_email_verified = True
    org.email_verified_at = datetime.now(UTC)
    org.updated_at = datetime.now(UTC)

    await db.commit()

    return {
        "ok": True,
        "message": "Email успешно подтвержден и привязан к организации",
        "email": org.email,
        "is_email_verified": True,
    }


# --- Endpoints: Terminals ---


@router.get(
    "/terminals",
    response_model=TerminalSettingsListResponse,
)
async def list_terminals_settings(
    org_id: int | None = Query(None, description="Org ID for superuser viewing"),
    search: str | None = Query(
        None,
        description="Search by device_id (single or comma-separated), sn, address, note",
    ),
    sort_by: str = Query(
        "device_id", description="Sort by field: device_id, created_at, sn, address"
    ),
    sort_order: str = Query("asc", description="Sort order: asc, desc"),
    page: int = Query(1, ge=1, description="Page number"),
    page_size: int = Query(50, ge=1, le=500, description="Items per page"),
    fetch_all: bool = Query(
        False, alias="all", description="Return all terminals without pagination"
    ),
    user: dict[str, Any] = Depends(
        require_permission(PERMISSION_SETTINGS_TERMINALS_VIEW)
    ),
    db: AsyncSession = Depends(get_db),
    response: Response = None,  # type: ignore[assignment]
) -> TerminalSettingsListResponse:
    """List terminals for current organization with search, sorting and pagination."""
    is_su = bool(user.get("is_superuser") or user.get("role_id") == 1)

    effective_org_id = int(user.get("org_id", 0))
    passed_org_id = org_id if isinstance(org_id, int) else None
    if is_su and passed_org_id is not None and passed_org_id > 0:
        effective_org_id = passed_org_id

    cur_page = page if isinstance(page, int) else 1
    cur_page_size = page_size if isinstance(page_size, int) else 50
    should_fetch_all = bool(fetch_all) if isinstance(fetch_all, bool) else False

    if effective_org_id <= 0:
        return TerminalSettingsListResponse(
            items=[], total_count=0, page=cur_page, page_size=cur_page_size
        )

    query = select(Terminal).where(Terminal.org_id == effective_org_id)

    if search and isinstance(search, str):
        raw_search = search.strip()
        search_pattern = f"%{raw_search}%"
        comma_ids = [
            int(part.strip())
            for part in raw_search.split(",")
            if part.strip().isdigit()
        ]

        conditions: list[Any] = [
            Terminal.sn.ilike(search_pattern),
            Terminal.address.ilike(search_pattern),
            Terminal.note.ilike(search_pattern),
            Terminal.cert_serial.ilike(search_pattern),
        ]
        if len(comma_ids) > 1:
            conditions.append(Terminal.device_id.in_(comma_ids))
        elif len(comma_ids) == 1:
            conditions.append(Terminal.device_id == comma_ids[0])
            conditions.append(cast(Terminal.device_id, String).ilike(search_pattern))
        else:
            conditions.append(cast(Terminal.device_id, String).ilike(search_pattern))

        query = query.where(or_(*conditions))

    count_query = select(func.count()).select_from(query.subquery())
    total_count = (await db.execute(count_query)).scalar_one()

    # Sorting
    order_str = sort_order if isinstance(sort_order, str) else "asc"
    field_str = sort_by if isinstance(sort_by, str) else "device_id"

    sort_col: Any = Terminal.device_id
    if field_str == "created_at":
        sort_col = Terminal.created_at
    elif field_str == "sn":
        sort_col = Terminal.sn
    elif field_str == "address":
        sort_col = Terminal.address
    elif field_str == "id":
        sort_col = Terminal.id

    if order_str.lower() == "desc":
        query = query.order_by(sort_col.desc())
    else:
        query = query.order_by(sort_col.asc())

    if not should_fetch_all:
        query = query.offset((cur_page - 1) * cur_page_size).limit(cur_page_size)

    res = await db.execute(query)
    terminals = res.scalars().all()

    items = []
    for t in terminals:
        items.append(
            TerminalSettingsItem(
                id=t.id,
                device_id=t.device_id,
                sn=t.sn,
                address=t.address,
                note=t.note,
                timezone=t.timezone,
                is_active=t.is_active,
                cert_serial=t.cert_serial,
                cert_not_valid_after=(
                    t.cert_not_valid_after.isoformat()
                    if t.cert_not_valid_after
                    else None
                ),
                created_at=t.created_at.isoformat() if t.created_at else None,
                updated_at=t.updated_at.isoformat() if t.updated_at else None,
            )
        )

    if response is not None:
        response.headers["X-Total-Count"] = str(total_count)

    return TerminalSettingsListResponse(
        items=items,
        total_count=total_count,
        page=cur_page if not should_fetch_all else 1,
        page_size=cur_page_size if not should_fetch_all else total_count,
    )


@router.patch("/terminals/{terminal_id}", response_model=TerminalSettingsItem)
@router.put("/terminals/{terminal_id}", response_model=TerminalSettingsItem)
async def update_terminal_settings(
    terminal_id: int,
    req: UpdateTerminalSettingsRequest,
    user: dict[str, Any] = Depends(require_readonly_guard),
    db: AsyncSession = Depends(get_db),
) -> TerminalSettingsItem:
    """Update terminal address, note, and timezone only (role 3 only, superuser and role 4 are blocked)."""
    _check_settings_access(user)
    _ensure_not_superuser(user)

    org_id = int(user.get("org_id", 0))
    if org_id <= 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Пользователь не привязан к организации",
        )

    res = await db.execute(
        select(Terminal).where(Terminal.id == terminal_id, Terminal.org_id == org_id)
    )
    terminal = res.scalar_one_or_none()
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Терминал не найден или не принадлежит вашей организации",
        )

    # Strictly update ONLY address, note, and timezone
    if req.address is not None:
        terminal.address = req.address.strip() or None
    if req.note is not None:
        terminal.note = req.note.strip() or None
    if req.timezone is not None:
        terminal.timezone = req.timezone.strip() or None

    terminal.updated_at = datetime.now(UTC)
    await db.commit()
    await db.refresh(terminal)

    return TerminalSettingsItem(
        id=terminal.id,
        device_id=terminal.device_id,
        sn=terminal.sn,
        address=terminal.address,
        note=terminal.note,
        timezone=terminal.timezone,
        is_active=terminal.is_active,
        cert_serial=terminal.cert_serial,
        cert_not_valid_after=(
            terminal.cert_not_valid_after.isoformat()
            if terminal.cert_not_valid_after
            else None
        ),
        created_at=(terminal.created_at.isoformat() if terminal.created_at else None),
        updated_at=(terminal.updated_at.isoformat() if terminal.updated_at else None),
    )
