import hashlib
import hmac
from abc import ABC, abstractmethod
from dataclasses import dataclass

from app.config import settings


@dataclass(slots=True)
class UserRecord:
    username: str
    md5_password: str
    org_id: int | None = None
    role: str = "user"  # "superuser" | "admin" | "user"
    is_superuser: bool = False
    is_active: bool = True
    full_name: str | None = None


def verify_md5_password(plain_password: str, md5_hash: str) -> bool:
    calculated = hashlib.md5(plain_password.encode("utf-8")).hexdigest()
    return hmac.compare_digest(calculated, md5_hash.lower())


class AbstractUserStore(ABC):
    """Abstract user store interface.

    Allows plugging in config-based users, database-backed users,
    or a dedicated external authentication microservice.
    """

    @abstractmethod
    async def get_by_username(self, username: str) -> UserRecord | None:
        """Find a user by username."""
        ...

    @abstractmethod
    async def authenticate(
        self, username: str, plain_password: str
    ) -> UserRecord | None:
        """Authenticate user credentials."""
        ...


class ConfigUserStore(AbstractUserStore):
    """User store that reads users from application configuration (settings.auth_users).

    Designated superusers (such as 'o.lebedev' or users with role 'superuser'/'admin')
    are automatically granted superuser privileges.
    """

    async def get_by_username(self, username: str) -> UserRecord | None:
        for u in settings.get_users():
            if u.get("username") == username:
                raw_org = u.get("org_id")
                try:
                    org_id = int(raw_org) if raw_org is not None else None
                except ValueError, TypeError:
                    org_id = None

                role = str(u.get("role", "user")).lower()
                is_su = bool(
                    u.get("is_superuser")
                    or role in ("superuser", "admin")
                    or username == "o.lebedev"
                )
                if is_su and role not in ("superuser", "admin"):
                    role = "superuser"

                return UserRecord(
                    username=u["username"],
                    md5_password=u.get("md5_password", ""),
                    org_id=org_id,
                    role=role,
                    is_superuser=is_su,
                    is_active=bool(u.get("is_active", True)),
                    full_name=u.get("full_name"),
                )
        return None

    async def authenticate(
        self, username: str, plain_password: str
    ) -> UserRecord | None:
        user = await self.get_by_username(username)
        if not user or not user.is_active:
            return None
        if not verify_md5_password(plain_password, user.md5_password):
            return None
        return user


class DatabaseUserStore(AbstractUserStore):
    """Database-backed user store adapter (for future migration to PostgreSQL users table)."""

    async def get_by_username(self, username: str) -> UserRecord | None:
        # Ready for future implementation when PostgreSQL `users` table is provisioned
        return None

    async def authenticate(
        self, username: str, plain_password: str
    ) -> UserRecord | None:
        # Ready for future implementation
        return None


class RemoteAuthUserStore(AbstractUserStore):
    """Adapter for delegating authentication to an external dedicated auth microservice."""

    async def get_by_username(self, username: str) -> UserRecord | None:
        # Ready for future external auth service HTTP/gRPC integration
        return None

    async def authenticate(
        self, username: str, plain_password: str
    ) -> UserRecord | None:
        # Ready for future external auth service HTTP/gRPC integration
        return None


# Default singleton instance
_default_user_store: AbstractUserStore = ConfigUserStore()


def get_user_store() -> AbstractUserStore:
    """Dependency / accessor for the active UserStore."""
    return _default_user_store
