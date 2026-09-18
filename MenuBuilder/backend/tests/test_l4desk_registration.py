from __future__ import annotations

import hashlib
import time
from datetime import UTC, datetime, timedelta
from typing import Any, cast
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastapi.security import HTTPAuthorizationCredentials
from httpx import ASGITransport, AsyncClient
from starlette.requests import Request

from app.auth import get_current_user
from app.config import settings
from app.database import get_db
from app.main import app
from app.models import Org, OrgBillingSettings, OrgStatus, User
from app.models_l4desk import (
    L4DeskAuditEvent,
    L4DeskMembership,
    L4DeskRegistration,
    L4DeskTenantProfile,
)
from app.security.permissions import (
    ROLE_L4DESK_OWNER,
    require_tenant_admin,
)
from app.services.registration_service import (
    EmailDeliveryAdapter,
    InvalidTokenError,
    RateLimitError,
    RegistrationDisabledError,
    RegistrationRateLimiter,
    RegistrationService,
    TokenExpiredError,
    ValidationError,
    normalize_email,
    validate_password,
    validate_safe_return_url,
)


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture(autouse=True)
def cleanup_overrides_and_flags():
    original_reg_flag = settings.l4desk_registration_enabled
    app.dependency_overrides.clear()
    yield
    settings.l4desk_registration_enabled = original_reg_flag
    app.dependency_overrides.clear()


# =============================================================================
# 1. Validation and URL Sanitization Unit Tests
# =============================================================================


def test_normalize_email_valid():
    assert normalize_email("Test.User@example.com") == "test.user@example.com"
    assert normalize_email("  ADMIN@LEO4.RU  ") == "admin@leo4.ru"


def test_normalize_email_invalid():
    with pytest.raises(ValidationError):
        normalize_email("")
    with pytest.raises(ValidationError):
        normalize_email("invalid-email")
    with pytest.raises(ValidationError):
        normalize_email("test@")
    with pytest.raises(ValidationError):
        normalize_email("@example.com")
    with pytest.raises(ValidationError):
        normalize_email("test@example")


def test_validate_password_rules():
    validate_password("secure_pass_123")  # >= 8 chars OK
    with pytest.raises(ValidationError, match="не менее 8 символов"):
        validate_password("short")
    with pytest.raises(ValidationError, match="слишком длинный"):
        validate_password("a" * 129)


def test_validate_safe_return_url():
    # Valid relative paths
    assert validate_safe_return_url("/monitoring") == "/monitoring"
    assert validate_safe_return_url("/settings/profile") == "/settings/profile"
    assert validate_safe_return_url("/billing?tab=orders") == "/billing?tab=orders"

    # None or empty
    assert validate_safe_return_url(None) == "/monitoring"
    assert validate_safe_return_url("") == "/monitoring"
    assert validate_safe_return_url("   ") == "/monitoring"

    # Open redirect attacks
    assert validate_safe_return_url("//evil.com") == "/monitoring"
    assert validate_safe_return_url("//evil.com/path") == "/monitoring"
    assert validate_safe_return_url("/\\evil.com") == "/monitoring"
    assert validate_safe_return_url("\\evil.com") == "/monitoring"
    assert validate_safe_return_url("http://attacker.com") == "/monitoring"
    assert validate_safe_return_url("javascript:alert(1)") == "/monitoring"

    # Trusted hosts
    assert (
        validate_safe_return_url("https://leo4.ru/monitoring")
        == "https://leo4.ru/monitoring"
    )


# =============================================================================
# 2. Rate Limiter Unit Tests
# =============================================================================


def test_rate_limiter_ip_limits():
    limiter = RegistrationRateLimiter()
    ip = "192.168.1.100"

    # Default is 5 attempts allowed
    for _ in range(5):
        limiter.check_ip(ip)

    # 6th attempt should raise RateLimitError
    with pytest.raises(RateLimitError) as exc_info:
        limiter.check_ip(ip)
    assert exc_info.value.retry_after > 0


def test_rate_limiter_email_resend_cooldown_and_max():
    limiter = RegistrationRateLimiter()
    email = "tenant@example.com"

    # 1st attempt OK
    limiter.check_email_resend(email)

    # Immediate second attempt blocked by cooldown (60s)
    with pytest.raises(RateLimitError, match="Повторная отправка"):
        limiter.check_email_resend(email)

    # Simulate waiting past cooldown
    limiter._email_resend_attempts[email] = [time.time() - 70]
    limiter.check_email_resend(email)  # 2nd attempt OK

    limiter._email_resend_attempts[email] = [time.time() - 140, time.time() - 70]
    limiter.check_email_resend(email)  # 3rd attempt OK

    # 4th attempt in an hour blocked by hourly limit
    limiter._email_resend_attempts[email] = [
        time.time() - 200,
        time.time() - 140,
        time.time() - 70,
    ]
    with pytest.raises(RateLimitError, match="Превышен лимит"):
        limiter.check_email_resend(email)


# =============================================================================
# 3. RegistrationService & Atomic Confirmation Tests (Core, Negative, Replay, Race)
# =============================================================================


class FakeAsyncSession:
    """In-memory AsyncSession simulator for atomic multi-entity tests."""

    def __init__(self) -> None:
        self.added: list[Any] = []
        self.committed = False
        self.flushed = False
        self._next_id = 100

        # In-memory stores
        self.users: dict[str, User] = {}
        self.registrations_by_email: dict[str, L4DeskRegistration] = {}
        self.registrations_by_token: dict[str, L4DeskRegistration] = {}
        self.orgs: dict[int, Org] = {}
        self.org_billing: dict[int, OrgBillingSettings] = {}
        self.org_statuses: dict[int, OrgStatus] = {}
        self.tenant_profiles: dict[int, L4DeskTenantProfile] = {}
        self.memberships: list[L4DeskMembership] = []
        self.audits: list[L4DeskAuditEvent] = []

    def add(self, entity: Any) -> None:
        self.added.append(entity)
        if isinstance(entity, User):
            if not getattr(entity, "id", None):
                self._next_id += 1
                entity.id = self._next_id
            self.users[entity.username] = entity
        elif isinstance(entity, Org):
            self.orgs[entity.org_id] = entity
        elif isinstance(entity, OrgBillingSettings):
            self.org_billing[entity.org_id] = entity
        elif isinstance(entity, OrgStatus):
            self.org_statuses[entity.org_id] = entity
        elif isinstance(entity, L4DeskTenantProfile):
            self.tenant_profiles[entity.tenant_id] = entity
        elif isinstance(entity, L4DeskMembership):
            self.memberships.append(entity)
        elif isinstance(entity, L4DeskAuditEvent):
            self.audits.append(entity)
        elif isinstance(entity, L4DeskRegistration):
            if not getattr(entity, "id", None):
                self._next_id += 1
                entity.id = self._next_id
            self.registrations_by_email[entity.email_normalized] = entity
            self.registrations_by_token[entity.token_hash] = entity

    async def flush(self) -> None:
        self.flushed = True
        for entity in self.added:
            if isinstance(entity, User) and not getattr(entity, "id", None):
                self._next_id += 1
                entity.id = self._next_id

    async def commit(self) -> None:
        self.committed = True

    async def execute(self, stmt: Any) -> Any:
        stmt_str = str(stmt)
        result_mock = MagicMock()

        # Query user existence
        if (
            "FROM users" in stmt_str
            or "users.id" in stmt_str
            or "users.username" in stmt_str
        ):
            # Check if any user matches
            matched_user = None
            for u in self.users.values():
                matched_user = u
                break
            result_mock.scalar_one_or_none.return_value = matched_user
            return result_mock

        # Query registration by token
        if "FROM l4desk_registrations" in stmt_str and "token_hash" in stmt_str:
            matched_reg = None
            for reg in self.registrations_by_token.values():
                matched_reg = reg
                break
            result_mock.scalar_one_or_none.return_value = matched_reg
            return result_mock

        # Query registration by email
        if "FROM l4desk_registrations" in stmt_str and "email_normalized" in stmt_str:
            matched_reg = None
            for reg in self.registrations_by_email.values():
                matched_reg = reg
                break
            result_mock.scalar_one_or_none.return_value = matched_reg
            return result_mock

        # Query Org.org_id
        if "FROM orgs" in stmt_str:
            existing_ids = list(self.orgs.keys())
            scalars_mock = MagicMock()
            scalars_mock.all.return_value = existing_ids
            result_mock.scalars.return_value = scalars_mock
            return result_mock

        result_mock.scalar_one_or_none.return_value = None
        result_mock.scalars.return_value.all.return_value = []
        return result_mock


@pytest.mark.anyio
async def test_registration_disabled_raises_error():
    settings.l4desk_registration_enabled = False
    fake_session = FakeAsyncSession()
    service = RegistrationService(cast(Any, fake_session))

    with pytest.raises(RegistrationDisabledError, match="отключена"):
        await service.register(email="user@test.com", password="password123")

    with pytest.raises(RegistrationDisabledError, match="отключена"):
        await service.resend_confirmation(email="user@test.com")


@pytest.mark.anyio
async def test_registration_anti_enumeration_for_existing_user():
    settings.l4desk_registration_enabled = True
    fake_session = FakeAsyncSession()
    # Pre-populate existing user
    existing = User(
        id=1, username="existing@example.com", md5_password="hash", org_id=10
    )
    fake_session.users["existing@example.com"] = existing

    email_adapter = MagicMock(spec=EmailDeliveryAdapter)
    email_adapter.send_confirmation_email = AsyncMock()
    service = RegistrationService(cast(Any, fake_session), email_adapter=email_adapter)

    res = await service.register(
        email="Existing@Example.com",
        password="secret_password_123",
    )
    # Generic anti-enumeration response
    assert res["status"] == "pending_confirmation"
    assert res["email"] == "existing@example.com"
    # No email should be sent for already active user
    email_adapter.send_confirmation_email.assert_not_called()


@pytest.mark.anyio
async def test_registration_happy_path_creates_token_and_dispatches_email():
    settings.l4desk_registration_enabled = True
    fake_session = FakeAsyncSession()
    email_adapter = MagicMock(spec=EmailDeliveryAdapter)
    email_adapter.send_confirmation_email = AsyncMock()
    limiter = RegistrationRateLimiter()
    service = RegistrationService(
        cast(Any, fake_session), email_adapter=email_adapter, limiter=limiter
    )

    res = await service.register(
        email="new.owner@example.com",
        password="owner_password_88",
        timezone="Asia/Yekaterinburg",
    )

    assert res["status"] == "pending_confirmation"
    assert res["email"] == "new.owner@example.com"

    # Verify registration stored
    reg = fake_session.registrations_by_email["new.owner@example.com"]
    assert reg.email_normalized == "new.owner@example.com"
    assert reg.timezone == "Asia/Yekaterinburg"
    # Token in DB is hashed with SHA256
    assert len(reg.token_hash) == 64
    # Password in DB is MD5 hashed
    assert reg.password_hash == hashlib.md5(b"owner_password_88").hexdigest()
    assert reg.consumed_at is None

    # Verify audit event
    created_audit = next(
        (a for a in fake_session.audits if a.event_type == "registration.created"),
        None,
    )
    assert created_audit is not None
    assert created_audit.details is not None
    assert created_audit.details["email"] == "new.owner@example.com"

    # Verify confirmation email sent
    email_adapter.send_confirmation_email.assert_called_once()
    call_kwargs = email_adapter.send_confirmation_email.call_args.kwargs
    assert call_kwargs["email"] == "new.owner@example.com"
    # Raw token sent to user hashes to the token_hash in DB
    raw_token = call_kwargs["token"]
    assert hashlib.sha256(raw_token.encode("utf-8")).hexdigest() == reg.token_hash


@pytest.mark.anyio
async def test_confirm_registration_happy_path_provisions_tenant_and_user_role_5():
    settings.l4desk_registration_enabled = True
    fake_session = FakeAsyncSession()
    now = datetime.now(UTC)

    raw_token = "secure_raw_token_xyz"
    token_hash = hashlib.sha256(raw_token.encode("utf-8")).hexdigest()
    pw_hash = hashlib.md5(b"mypassword123").hexdigest()

    reg = L4DeskRegistration(
        id=42,
        email_normalized="owner@company.ru",
        password_hash=pw_hash,
        token_hash=token_hash,
        terms_version="v1",
        timezone="Europe/Samara",
        correlation_id="corr-reg-42",
        expires_at=now + timedelta(hours=24),
    )
    fake_session.registrations_by_token[token_hash] = reg
    fake_session.registrations_by_email["owner@company.ru"] = reg

    service = RegistrationService(cast(Any, fake_session))
    res = await service.confirm_registration(
        token=raw_token,
        return_url="/billing",
    )

    assert res["status"] == "confirmed"
    assert res["email"] == "owner@company.ru"
    assert res["return_url"] == "/billing"
    tenant_id = res["tenant_id"]
    user_id = res["user_id"]
    assert tenant_id is not None
    assert user_id is not None

    # Verify User created
    user = fake_session.users["owner@company.ru"]
    assert user.id == user_id
    assert user.role_id == ROLE_L4DESK_OWNER
    assert user.role == "l4desk_owner"
    assert user.org_id == tenant_id
    assert user.md5_password == pw_hash
    assert user.is_active is True

    # Verify Org created
    org = fake_session.orgs[tenant_id]
    assert org.is_active is True
    assert org.is_email_verified is True
    assert org.timezone == "Europe/Samara"
    assert org.email == "owner@company.ru"

    # Verify OrgBillingSettings (free tier default)
    bs = fake_session.org_billing[tenant_id]
    assert bs.monthly_price_minor == 0
    assert bs.billing_mode == "prepaid"
    assert bs.cert_billing_mode == "none"
    assert bs.allowed_billing_periods == "1,3,6,12"
    assert bs.tenant_pin_creation_enabled is True

    # Verify L4DeskTenantProfile
    profile = fake_session.tenant_profiles[tenant_id]
    assert profile.timezone == "Europe/Samara"

    # Verify L4DeskMembership
    membership = fake_session.memberships[0]
    assert membership.tenant_id == tenant_id
    assert membership.user_id == user_id
    assert membership.role_id == 5
    assert membership.is_owner is True

    # Verify Registration marked consumed
    assert reg.consumed_at is not None
    assert reg.user_id == user_id
    assert reg.tenant_id == tenant_id

    # Verify Immutable Audit Event
    audit = next(
        (a for a in fake_session.audits if a.event_type == "registration.confirmed"),
        None,
    )
    assert audit is not None
    assert audit.actor == f"user:{user_id}"
    assert audit.tenant_id == tenant_id
    assert audit.correlation_id == "corr-reg-42"
    assert audit.details is not None
    assert audit.details["role_id"] == 5
    assert audit.details["is_owner"] is True


@pytest.mark.anyio
async def test_confirm_registration_idempotency_replay():
    """Replaying the same token returns 200 already_confirmed without duplicate creation."""
    fake_session = FakeAsyncSession()
    now = datetime.now(UTC)
    token_hash = hashlib.sha256(b"replay_token").hexdigest()

    consumed_reg = L4DeskRegistration(
        id=99,
        email_normalized="replayer@test.com",
        password_hash="pw",
        token_hash=token_hash,
        terms_version="v1",
        timezone="Europe/Moscow",
        correlation_id="corr-99",
        expires_at=now + timedelta(hours=10),
        consumed_at=now - timedelta(minutes=5),
        user_id=888,
        tenant_id=777,
    )
    fake_session.registrations_by_token[token_hash] = consumed_reg

    service = RegistrationService(cast(Any, fake_session))
    res = await service.confirm_registration(token="replay_token")

    assert res["status"] == "already_confirmed"
    assert res["tenant_id"] == 777
    assert res["user_id"] == 888

    # No new entities added
    assert len(fake_session.orgs) == 0
    assert len(fake_session.users) == 0
    assert len(fake_session.memberships) == 0


@pytest.mark.anyio
async def test_confirm_registration_expired_token():
    fake_session = FakeAsyncSession()
    now = datetime.now(UTC)
    token_hash = hashlib.sha256(b"expired_token").hexdigest()

    expired_reg = L4DeskRegistration(
        id=12,
        email_normalized="expired@test.com",
        password_hash="pw",
        token_hash=token_hash,
        terms_version="v1",
        timezone="UTC",
        correlation_id="corr-12",
        expires_at=now - timedelta(seconds=10),  # expired
    )
    fake_session.registrations_by_token[token_hash] = expired_reg

    service = RegistrationService(cast(Any, fake_session))
    with pytest.raises(TokenExpiredError, match="истек"):
        await service.confirm_registration(token="expired_token")


@pytest.mark.anyio
async def test_confirm_registration_invalid_token():
    fake_session = FakeAsyncSession()
    service = RegistrationService(cast(Any, fake_session))

    with pytest.raises(InvalidTokenError, match="Неверный"):
        await service.confirm_registration(token="completely_unknown_token")


# =============================================================================
# 4. HTTP API Endpoints Integration Tests (AsyncClient)
# =============================================================================


@pytest.mark.anyio
async def test_api_status_endpoint():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        settings.l4desk_registration_enabled = False
        resp = await client.get("/api/auth/register/status")
        assert resp.status_code == 200
        assert resp.json()["enabled"] is False

        settings.l4desk_registration_enabled = True
        resp2 = await client.get("/api/auth/register/status")
        assert resp2.status_code == 200
        assert resp2.json()["enabled"] is True


@pytest.mark.anyio
async def test_api_register_endpoint_disabled():
    settings.l4desk_registration_enabled = False
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/auth/register",
            json={"email": "test@test.ru", "password": "secure_pass_123"},
        )
        assert resp.status_code == 403
        assert "отключена" in resp.json()["detail"]


@pytest.mark.anyio
async def test_api_register_validation_errors():
    settings.l4desk_registration_enabled = True
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        # Invalid email
        resp1 = await client.post(
            "/api/auth/register",
            json={"email": "not-an-email", "password": "secure_pass_123"},
        )
        assert resp1.status_code == 422

        # Short password (< 8 chars)
        resp2 = await client.post(
            "/api/auth/register",
            json={"email": "valid@test.ru", "password": "123"},
        )
        assert resp2.status_code == 422


@pytest.mark.anyio
async def test_api_register_and_confirm_flow_with_mocked_db():
    settings.l4desk_registration_enabled = True
    fake_session = FakeAsyncSession()

    async def override_get_db():
        yield cast(Any, fake_session)

    app.dependency_overrides[get_db] = override_get_db

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        with patch(
            "app.services.registration_service.email_delivery_adapter.send_confirmation_email",
            new_callable=AsyncMock,
        ) as mock_send:
            # 1. Register
            resp = await client.post(
                "/api/auth/register",
                json={
                    "email": "api.owner@example.com",
                    "password": "strong_owner_pass_123",
                    "timezone": "Europe/Kaliningrad",
                    "return_url": "/monitoring",
                },
            )
            assert resp.status_code == 200
            data = resp.json()
            assert data["status"] == "pending_confirmation"
            assert data["email"] == "api.owner@example.com"
            assert mock_send.called

            # Capture token sent in email
            raw_token = mock_send.call_args.kwargs["token"]

            # 2. Confirm email
            confirm_resp = await client.post(
                "/api/auth/register/confirm",
                json={"token": raw_token, "return_url": "/monitoring"},
            )
            assert confirm_resp.status_code == 200
            confirm_data = confirm_resp.json()
            assert confirm_data["status"] == "confirmed"
            assert confirm_data["tenant_id"] is not None
            assert confirm_data["user_id"] is not None

            # 3. Idempotent re-confirm
            reconfirm_resp = await client.post(
                "/api/auth/register/confirm",
                json={"token": raw_token},
            )
            assert reconfirm_resp.status_code == 200
            assert reconfirm_resp.json()["status"] == "already_confirmed"


# =============================================================================
# 5. JWT org_id Boundary & Tenant Isolation Tests for Role 5 (l4desk_owner)
# =============================================================================


@pytest.mark.anyio
async def test_jwt_org_id_boundary_casts_string_and_normalizes_role_5():
    """Verify that JWT org_id string is cast to int and role=5 gets l4desk_owner permissions."""
    request = Request(
        {
            "type": "http",
            "method": "GET",
            "path": "/api/monitoring",
            "headers": [],
        }
    )

    # Simulated JWT payload with string org_id and numeric role "5"
    mock_payload = {
        "sub": "owner@company.ru",
        "userId": 55,
        "orgId": "345",  # String org_id claim
        "roleId": 5,
        "role": "5",
    }

    with patch("app.auth.decode_token", return_value=mock_payload):
        creds = HTTPAuthorizationCredentials(scheme="Bearer", credentials="mock_token")
        user = await get_current_user(request, creds)

        # Verify int conversion
        assert isinstance(user["org_id"], int)
        assert user["org_id"] == 345
        assert user["role_id"] == 5
        assert user["role"] == "l4desk_owner"

        # Verify permission granting (role 5 has full tenant access)
        assert len(user["permissions"]) > 0

        # Verify require_tenant_admin passes for role 5
        admin_user = await require_tenant_admin(user)
        assert admin_user == user


@pytest.mark.anyio
async def test_email_delivery_adapter_constructs_safe_link():
    mock_email_client = AsyncMock()
    mock_email_client.send_email = AsyncMock(return_value={"status": "sent"})
    adapter = EmailDeliveryAdapter(email_client=mock_email_client)

    await adapter.send_confirmation_email(
        email="test@leo4.ru",
        token="tok_12345",
        frontend_base_url="https://leo4.ru",
        return_url="/custom_dashboard",
    )

    mock_email_client.send_email.assert_called_once()
    kwargs = mock_email_client.send_email.call_args.kwargs
    assert kwargs["recipients"] == ["test@leo4.ru"]
    assert "tok_12345" in kwargs["message"]
    assert "https://leo4.ru/register/confirm?" in kwargs["message"]
    assert "return_url=%2Fcustom_dashboard" in kwargs["message"]
