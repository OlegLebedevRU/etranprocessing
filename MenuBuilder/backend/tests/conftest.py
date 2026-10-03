import os
from types import SimpleNamespace

import pytest

os.environ["JWT_ISSUER_MOCK_ENABLED"] = "true"
# Existing auth tests exercise credentials independently; CAPTCHA has dedicated tests.
os.environ["SMARTCAPTCHA_ENABLED"] = "false"
os.environ.setdefault("JWT_SECRET", "test-secret-key-12345678901234567890")
os.environ.setdefault(
    "JWT_SECRET_HEX",
    "176b79312cfae3cf5e6c0200548f32c7c47e7b06b933eb11c9b2936bfe2644bc",
)
os.environ.setdefault("SERVICE_TO_YC_SERVICE_SECRET", "test-yc-service-secret-123456")
os.environ.setdefault(
    "AUTH_USERS",
    '[{"username":"o.lebedev","md5_password":"eaf21fcabcffeb1f97f01a4fc02ece63","org_id":1,"role":"superuser","is_superuser":true},{"username":"test","md5_password":"cc03e747a6afbbcbf8be7668acfebee5","org_id":1,"role":"user","is_superuser":false}]',
)
os.environ.setdefault(
    "DATABASE_URL", "postgresql+asyncpg://test:test@localhost:5432/test"
)

from app.config import settings
from app.services.jwt_issuer import jwt_issuer_client
from app.user_store import get_user_store

settings.jwt_issuer_mock_enabled = True
settings.session_cleanup_enabled = False
settings.schema_compatibility_check_enabled = False
jwt_issuer_client.mock_enabled = True
get_user_store()._db_available = False


@pytest.fixture
def mock_auth_org_policy(monkeypatch):
    """Supply an existing organization for auth tests unrelated to DB availability."""

    class Session:
        async def __aenter__(self):
            return self

        async def __aexit__(self, *_args):
            return None

        async def execute(self, _query):
            row = SimpleNamespace(
                org_name="Test Org",
                timezone="Europe/Moscow",
                site_mode="both",
                default_site=None,
                classic_licenses_enabled=True,
                l4desk_licenses_enabled=True,
            )
            return SimpleNamespace(first=lambda: row)

    monkeypatch.setattr("app.routers.auth.async_session", Session)
