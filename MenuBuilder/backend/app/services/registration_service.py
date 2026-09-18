from __future__ import annotations

import hashlib
import logging
import re
import secrets
import time
import urllib.parse
import uuid
from contextlib import suppress
from datetime import UTC, datetime, timedelta
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.models import Org, OrgBillingSettings, OrgStatus, User
from app.models_l4desk import (
    L4DeskAuditEvent,
    L4DeskMembership,
    L4DeskTenantProfile,
)
from app.repositories.l4desk_repository import L4DeskRepository
from app.services.email_service import ServerlessEmailClient

logger = logging.getLogger(__name__)

EMAIL_REGEX = re.compile(r"^[a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\.[a-zA-Z0-9-.]+$")


class RegistrationDisabledError(Exception):
    """Raised when self-registration feature flag is disabled."""


class RateLimitError(Exception):
    """Raised when request rate limit is exceeded."""

    def __init__(self, message: str, retry_after: int = 60) -> None:
        super().__init__(message)
        self.retry_after = retry_after


class InvalidTokenError(Exception):
    """Raised when verification token is invalid or missing."""


class TokenExpiredError(Exception):
    """Raised when verification token has expired."""


class RegistrationConflictError(Exception):
    """Raised on registration conflict or race condition."""


class ValidationError(Exception):
    """Raised when user input fails validation."""


def normalize_email(email: str) -> str:
    """Normalize and validate email address."""
    clean = email.strip().lower()
    if not clean or len(clean) > 254:
        raise ValidationError("Некорректный адрес электронной почты")
    if not EMAIL_REGEX.match(clean):
        raise ValidationError("Некорректный формат адреса электронной почты")
    return clean


def validate_password(password: str) -> None:
    """Validate password strength."""
    if not password or len(password) < 8:
        raise ValidationError("Пароль должен содержать не менее 8 символов")
    if len(password) > 128:
        raise ValidationError("Пароль слишком длинный (максимум 128 символов)")


def validate_safe_return_url(return_url: str | None) -> str:
    """Validate return URL to prevent open redirect vulnerabilities.

    Allows safe relative paths (e.g. /monitoring, /settings) or URLs matching
    allowed origins. Disallows protocol-relative URLs (//) and backslashes.
    """
    default_url = "/monitoring"
    if not return_url:
        return default_url

    trimmed = return_url.strip()
    if not trimmed:
        return default_url

    # Disallow protocol-relative URLs (e.g. //evil.com) and backslash tricks (/\evil.com, \evil.com)
    if trimmed.startswith(("//", "/\\", "\\")):
        return default_url

    # If it is a relative path starting with /
    if trimmed.startswith("/"):
        # Check for forbidden control characters or javascript: scheme injection
        if any(c in trimmed for c in ("\r", "\n", "\t", "\x00")):
            return default_url
        return trimmed

    # If it's an absolute URL, check against allowed hostnames
    with suppress(Exception):
        parsed = urllib.parse.urlparse(trimmed)
        if parsed.scheme in ("http", "https"):
            allowed_hosts: set[str] = set()
            if settings.frontend_base_url:
                base_host = urllib.parse.urlparse(settings.frontend_base_url).netloc
                if base_host:
                    allowed_hosts.add(base_host.lower())
            for origin in settings.cors_origins:
                ohost = urllib.parse.urlparse(origin).netloc
                if ohost:
                    allowed_hosts.add(ohost.lower())
            # Also allow localhost for development
            allowed_hosts.update({"localhost", "127.0.0.1", "leo4.ru"})

            if parsed.netloc.lower() in allowed_hosts:
                return trimmed

    return default_url


class RegistrationRateLimiter:
    """In-memory rate limiter for registration attempts and email resends."""

    def __init__(self) -> None:
        self._ip_attempts: dict[str, list[float]] = {}
        self._email_resend_attempts: dict[str, list[float]] = {}

    def check_ip(self, ip: str) -> None:
        now = time.time()
        window = settings.l4desk_rate_limit_ip_window_sec
        max_attempts = settings.l4desk_rate_limit_ip_max

        attempts = self._ip_attempts.get(ip, [])
        attempts = [t for t in attempts if now - t < window]
        if len(attempts) >= max_attempts:
            retry_after = int(window - (now - attempts[0])) if attempts else window
            raise RateLimitError(
                "Слишком много запросов на регистрацию с данного IP-адреса. Пожалуйста, повторите позже.",
                retry_after=max(retry_after, 1),
            )
        attempts.append(now)
        self._ip_attempts[ip] = attempts

    def check_email_resend(self, email_normalized: str) -> None:
        now = time.time()
        cooldown = settings.l4desk_rate_limit_resend_cooldown_sec
        max_per_hour = settings.l4desk_rate_limit_resend_max_per_hour

        attempts = self._email_resend_attempts.get(email_normalized, [])
        # Check cooldown
        if attempts and (now - attempts[-1]) < cooldown:
            remaining = int(cooldown - (now - attempts[-1]))
            raise RateLimitError(
                f"Повторная отправка письма возможна через {max(remaining, 1)} секунд.",
                retry_after=max(remaining, 1),
            )
        # Check hourly limit
        attempts = [t for t in attempts if now - t < 3600]
        if len(attempts) >= max_per_hour:
            retry_after = int(3600 - (now - attempts[0])) if attempts else 3600
            raise RateLimitError(
                "Превышен лимит отправки писем подтверждения в час. Пожалуйста, повторите позже.",
                retry_after=max(retry_after, 1),
            )
        attempts.append(now)
        self._email_resend_attempts[email_normalized] = attempts

    def reset(self) -> None:
        """Reset internal trackers (useful for unit testing)."""
        self._ip_attempts.clear()
        self._email_resend_attempts.clear()


rate_limiter = RegistrationRateLimiter()


class EmailDeliveryAdapter:
    """Adapter for sending registration confirmation emails."""

    def __init__(self, email_client: ServerlessEmailClient | None = None) -> None:
        self.email_client = email_client or ServerlessEmailClient()

    async def send_confirmation_email(
        self,
        *,
        email: str,
        token: str,
        frontend_base_url: str | None = None,
        return_url: str | None = None,
    ) -> None:
        base_url = (frontend_base_url or settings.frontend_base_url).rstrip("/")
        query_params = {"token": token}
        if return_url and return_url != "/monitoring":
            query_params["return_url"] = return_url
        confirm_url = (
            f"{base_url}/register/confirm?{urllib.parse.urlencode(query_params)}"
        )

        subject = "Подтверждение регистрации в L4Desk"
        body = (
            f"Здравствуйте!\n\n"
            f"Для завершения регистрации и создания вашей организации в L4Desk, "
            f"пожалуйста, перейдите по следующей ссылке:\n\n"
            f"{confirm_url}\n\n"
            f"Ссылка действительна в течение {settings.l4desk_registration_token_expire_hours} часов.\n"
            f"Если вы не запрашивали регистрацию, проигнорируйте это письмо.\n\n"
            f"---\n"
            f"Команда L4Desk"
        )
        try:
            await self.email_client.send_email(
                device_id="l4desk-auth",
                recipients=[email],
                subject=subject,
                message=body,
            )
        except Exception as exc:  # noqa: BLE001
            # Email delivery failure is logged; registration token remains valid for resend
            logger.warning("Failed to deliver registration email to %s: %s", email, exc)


email_delivery_adapter = EmailDeliveryAdapter()


class RegistrationService:
    """Service orchestrating L4Desk self-registration, verification, and tenant provisioning."""

    def __init__(
        self,
        session: AsyncSession,
        email_adapter: EmailDeliveryAdapter | None = None,
        limiter: RegistrationRateLimiter | None = None,
    ) -> None:
        self.session = session
        self.repo = L4DeskRepository(session)
        self.email_adapter = email_adapter or email_delivery_adapter
        self.limiter = limiter or rate_limiter

    async def register(
        self,
        *,
        email: str,
        password: str,
        terms_version: str = "v1",
        timezone: str = "Europe/Moscow",
        source: str | None = None,
        return_url: str | None = None,
        ip_address: str = "127.0.0.1",
    ) -> dict[str, Any]:
        """Initiate self-registration with rate limits and anti-enumeration."""
        if not settings.l4desk_registration_enabled:
            raise RegistrationDisabledError("Саморегистрация в данный момент отключена")

        self.limiter.check_ip(ip_address)
        email_normalized = normalize_email(email)
        validate_password(password)
        safe_return_url = validate_safe_return_url(return_url)

        # Generic anti-enumeration response template
        generic_response = {
            "status": "pending_confirmation",
            "message": "Если данный адрес не зарегистрирован, ссылка для подтверждения отправлена на вашу почту.",
            "email": email_normalized,
            "return_url": safe_return_url,
        }

        # 1. Check if user with this username already exists in `users`
        existing_user_stmt = select(User.id).where(User.username == email_normalized)
        res_user = await self.session.execute(existing_user_stmt)
        if res_user.scalar_one_or_none() is not None:
            # User already exists: return generic response without leaking existence
            logger.info(
                "Registration attempted for existing username: %s", email_normalized
            )
            return generic_response

        # 2. Check if registration already exists in `l4desk_registrations`
        existing_reg = await self.repo.get_registration_by_email(email_normalized)
        if existing_reg:
            if existing_reg.consumed_at is not None:
                # Already confirmed user: return generic response
                logger.info(
                    "Registration attempted for already consumed email: %s",
                    email_normalized,
                )
                return generic_response

            # Pending registration exists: apply resend rate limit
            self.limiter.check_email_resend(email_normalized)

            # Generate fresh token and update existing registration
            raw_token = secrets.token_urlsafe(32)
            token_hash = hashlib.sha256(raw_token.encode("utf-8")).hexdigest()
            password_hash = hashlib.md5(password.encode("utf-8")).hexdigest()
            expires_at = datetime.now(UTC) + timedelta(
                hours=settings.l4desk_registration_token_expire_hours
            )

            await self.repo.update_registration_token(
                existing_reg.id,
                token_hash=token_hash,
                password_hash=password_hash,
                expires_at=expires_at,
                timezone=timezone or "Europe/Moscow",
                terms_version=terms_version or settings.l4desk_terms_current_version,
            )
            await self.session.commit()

            # Deliver confirmation email
            await self.email_adapter.send_confirmation_email(
                email=email_normalized,
                token=raw_token,
                return_url=safe_return_url,
            )
            return generic_response

        # 3. New registration: generate token and insert record
        raw_token = secrets.token_urlsafe(32)
        token_hash = hashlib.sha256(raw_token.encode("utf-8")).hexdigest()
        password_hash = hashlib.md5(password.encode("utf-8")).hexdigest()
        correlation_id = str(uuid.uuid4())
        expires_at = datetime.now(UTC) + timedelta(
            hours=settings.l4desk_registration_token_expire_hours
        )

        reg = await self.repo.create_registration(
            email_normalized=email_normalized,
            password_hash=password_hash,
            token_hash=token_hash,
            terms_version=terms_version or settings.l4desk_terms_current_version,
            timezone=timezone or "Europe/Moscow",
            correlation_id=correlation_id,
            expires_at=expires_at,
            source=source,
        )

        # Audit event for registration creation
        await self.repo.record_audit_event(
            actor="anonymous",
            event_type="registration.created",
            subject_type="registration",
            subject_id=str(reg.id),
            correlation_id=correlation_id,
            outcome="success",
            details={
                "email": email_normalized,
                "timezone": timezone,
                "terms_version": terms_version,
                "source": source,
                "ip": ip_address,
            },
        )
        await self.session.commit()

        # Track email attempt for rate limiting
        self.limiter.check_email_resend(email_normalized)

        # Deliver email
        await self.email_adapter.send_confirmation_email(
            email=email_normalized,
            token=raw_token,
            return_url=safe_return_url,
        )
        return generic_response

    async def resend_confirmation(
        self,
        *,
        email: str,
        return_url: str | None = None,
        ip_address: str = "127.0.0.1",
    ) -> dict[str, Any]:
        """Resend confirmation link with rate limits and anti-enumeration."""
        if not settings.l4desk_registration_enabled:
            raise RegistrationDisabledError("Саморегистрация в данный момент отключена")

        self.limiter.check_ip(ip_address)
        email_normalized = normalize_email(email)
        safe_return_url = validate_safe_return_url(return_url)

        generic_response = {
            "status": "sent",
            "message": "Если данный адрес ожидает подтверждения, ссылка для активации отправлена на вашу почту.",
            "email": email_normalized,
            "return_url": safe_return_url,
        }

        # Anti-enumeration: check if user already active in users table
        existing_user_stmt = select(User.id).where(User.username == email_normalized)
        res_user = await self.session.execute(existing_user_stmt)
        if res_user.scalar_one_or_none() is not None:
            return generic_response

        reg = await self.repo.get_registration_by_email(email_normalized)
        if not reg or reg.consumed_at is not None:
            return generic_response

        # Check rate limits for resend
        self.limiter.check_email_resend(email_normalized)

        raw_token = secrets.token_urlsafe(32)
        token_hash = hashlib.sha256(raw_token.encode("utf-8")).hexdigest()
        expires_at = datetime.now(UTC) + timedelta(
            hours=settings.l4desk_registration_token_expire_hours
        )

        await self.repo.update_registration_token(
            reg.id,
            token_hash=token_hash,
            expires_at=expires_at,
        )
        await self.repo.record_audit_event(
            actor="anonymous",
            event_type="registration.token_resent",
            subject_type="registration",
            subject_id=str(reg.id),
            correlation_id=reg.correlation_id,
            outcome="success",
            details={"email": email_normalized, "ip": ip_address},
        )
        await self.session.commit()

        await self.email_adapter.send_confirmation_email(
            email=email_normalized,
            token=raw_token,
            return_url=safe_return_url,
        )
        return generic_response

    async def confirm_registration(
        self,
        *,
        token: str,
        return_url: str | None = None,
    ) -> dict[str, Any]:
        """Verify token and atomically provision User (role=5), Tenant, Owner Membership, and Audit."""
        if not token or not token.strip():
            raise InvalidTokenError("Неверный токен подтверждения")

        safe_return_url = validate_safe_return_url(return_url)
        clean_token = token.strip()
        token_hash = hashlib.sha256(clean_token.encode("utf-8")).hexdigest()

        # Step 1: Find registration by token hash
        reg = await self.repo.get_registration_by_token(token_hash)
        if not reg:
            raise InvalidTokenError("Неверный или несуществующий токен подтверждения")

        # Step 2: Idempotent replay check
        if reg.consumed_at is not None:
            return {
                "status": "already_confirmed",
                "message": "Регистрация уже была успешно подтверждена.",
                "tenant_id": reg.tenant_id,
                "user_id": reg.user_id,
                "email": reg.email_normalized,
                "return_url": safe_return_url,
            }

        # Step 3: Expiry check
        now = datetime.now(UTC)
        if reg.expires_at <= now:
            raise TokenExpiredError(
                "Срок действия ссылки подтверждения истек. Пожалуйста, запросите новое письмо."
            )

        # Step 4: Atomic Tenant & User provisioning in a single transaction
        # Check if user already exists
        user_check = await self.session.execute(
            select(User).where(User.username == reg.email_normalized)
        )
        existing_user = user_check.scalar_one_or_none()
        if existing_user:
            # Race condition / existing user conflict: mark consumed and return
            await self.repo.mark_registration_consumed(
                registration_id=reg.id,
                user_id=existing_user.id,
                tenant_id=existing_user.org_id or 0,
                consumed_at=now,
            )
            await self.session.commit()
            return {
                "status": "already_confirmed",
                "message": "Пользователь уже зарегистрирован.",
                "tenant_id": existing_user.org_id,
                "user_id": existing_user.id,
                "email": reg.email_normalized,
                "return_url": safe_return_url,
            }

        # Allocate next org_id
        res_orgs = await self.session.execute(select(Org.org_id).order_by(Org.org_id))
        existing_ids = set(res_orgs.scalars().all())
        new_org_id = 1
        while new_org_id in existing_ids:
            new_org_id += 1

        org_name = f"Организация {reg.email_normalized}"
        org = Org(
            org_id=new_org_id,
            org_name=org_name,
            name=org_name,
            status=1,
            is_active=True,
            timezone=reg.timezone or "Europe/Moscow",
            email=reg.email_normalized,
            notify_by_email=True,
            send_reports=True,
            is_email_verified=True,
            email_verified_at=now,
        )
        self.session.add(org)
        # Flush org first to ensure org_id exists for foreign key constraints
        await self.session.flush()

        # Free package billing defaults (prepaid, 0 cost until first payment)
        billing_settings = OrgBillingSettings(
            org_id=new_org_id,
            monthly_price_minor=0,
            currency="RUB",
            billing_mode="prepaid",
            min_billing_periods=1,
            allowed_billing_periods="1,3,6,12",
            default_selection_mode="all",
            cert_billing_mode="none",
            cert_price_minor=0,
            tenant_pin_creation_enabled=True,
            cert_charge_primary_issue=False,
            cert_charge_reissue=False,
        )
        self.session.add(billing_settings)

        # OrgStatus active
        status_entry = OrgStatus(org_id=new_org_id, status="active")
        self.session.add(status_entry)

        # L4DeskTenantProfile
        tenant_profile = L4DeskTenantProfile(
            tenant_id=new_org_id,
            timezone=reg.timezone or "Europe/Moscow",
        )
        self.session.add(tenant_profile)

        # User with role_id=5 (l4desk_owner)
        user = User(
            username=reg.email_normalized,
            md5_password=reg.password_hash,
            org_id=new_org_id,
            role_id=5,
            role="l4desk_owner",
            is_active=True,
            is_superuser=False,
            full_name=reg.email_normalized.split("@")[0],
        )
        self.session.add(user)
        await self.session.flush()

        # L4DeskMembership with is_owner=True
        membership = L4DeskMembership(
            tenant_id=new_org_id,
            user_id=user.id,
            role_id=5,
            is_owner=True,
        )
        self.session.add(membership)

        # Update registration consumed
        reg.consumed_at = now
        reg.user_id = user.id
        reg.tenant_id = new_org_id

        # Immutable Audit Event
        audit = L4DeskAuditEvent(
            actor=f"user:{user.id}",
            event_type="registration.confirmed",
            subject_type="user",
            subject_id=str(user.id),
            tenant_id=new_org_id,
            correlation_id=reg.correlation_id,
            outcome="success",
            details={
                "email": reg.email_normalized,
                "timezone": reg.timezone,
                "terms_version": reg.terms_version,
                "tenant_id": new_org_id,
                "user_id": user.id,
                "role_id": 5,
                "role": "l4desk_owner",
                "is_owner": True,
            },
        )
        self.session.add(audit)

        # Commit single atomic transaction
        await self.session.commit()

        logger.info(
            "L4Desk registration confirmed: user=%s (id=%d) tenant=%d correlation=%s",
            reg.email_normalized,
            user.id,
            new_org_id,
            reg.correlation_id,
        )

        return {
            "status": "confirmed",
            "message": "Email успешно подтвержден. Ваша учетная запись готова к работе.",
            "tenant_id": new_org_id,
            "user_id": user.id,
            "email": reg.email_normalized,
            "return_url": safe_return_url,
        }
