from __future__ import annotations

import hashlib
import hmac
import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta

from sqlalchemy import select, update

from app.config import settings
from app.database import async_session
from app.models import User, UserSession

logger = logging.getLogger(__name__)


@dataclass(slots=True)
class UserRecord:
    id: int = 0
    username: str = ""
    md5_password: str = ""
    org_id: int | None = None
    role_id: int = 3
    role: str = "user"  # "superuser" | "admin" | "user"
    is_superuser: bool = False
    is_active: bool = True
    full_name: str | None = None
    last_org_id: int | None = None


def verify_md5_password(plain_password: str, md5_hash: str) -> bool:
    calculated = hashlib.md5(plain_password.encode("utf-8")).hexdigest()
    return hmac.compare_digest(calculated.lower(), md5_hash.lower())


def hash_refresh_token(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


class AbstractUserStore(ABC):
    """Abstract user store interface."""

    @abstractmethod
    async def get_by_username(self, username: str) -> UserRecord | None:
        """Find a user by username."""
        ...

    @abstractmethod
    async def get_by_id(self, user_id: int) -> UserRecord | None:
        """Find a user by ID."""
        ...

    @abstractmethod
    async def authenticate(
        self, username: str, plain_password: str
    ) -> UserRecord | None:
        """Authenticate user credentials."""
        ...


class DatabaseUserStore(AbstractUserStore):
    """Database-backed user store with fallback to config and in-memory cache for bootstrap and offline tests."""

    def __init__(self) -> None:
        self._in_memory_sessions: dict[str, dict] = {}
        self._session_seq: int = 1
        self._db_available: bool = True
        self._in_memory_user_last_org: dict[int, int] = {}

    async def get_by_username(self, username: str) -> UserRecord | None:
        if self._db_available:
            try:
                async with async_session() as session:
                    result = await session.execute(
                        select(User).where(User.username == username)
                    )
                    user = result.scalar_one_or_none()
                    if user:
                        return self._to_record(user)
            except Exception as exc:  # noqa: BLE001
                logger.warning("Database error fetching user %s: %s", username, exc)
                self._db_available = False

        # Fallback to config users
        rec = await ConfigUserStore().get_by_username(username)
        if rec and rec.id in self._in_memory_user_last_org:
            rec.last_org_id = self._in_memory_user_last_org[rec.id]
        return rec

    async def get_by_id(self, user_id: int) -> UserRecord | None:
        if self._db_available:
            try:
                async with async_session() as session:
                    result = await session.execute(
                        select(User).where(User.id == user_id)
                    )
                    user = result.scalar_one_or_none()
                    if user:
                        return self._to_record(user)
            except Exception as exc:  # noqa: BLE001
                logger.warning("Database error fetching user id %s: %s", user_id, exc)
                self._db_available = False

        # Fallback to config users
        rec = await ConfigUserStore().get_by_id(user_id)
        if rec and rec.id in self._in_memory_user_last_org:
            rec.last_org_id = self._in_memory_user_last_org[rec.id]
        return rec

    async def authenticate(
        self, username: str, plain_password: str
    ) -> UserRecord | None:
        user = await self.get_by_username(username)
        if not user or not user.is_active:
            return None
        # Plain password verification against MD5 hash
        if verify_md5_password(plain_password, user.md5_password):
            return user
        # Direct MD5 hash matching (if client sent MD5 directly)
        if hmac.compare_digest(plain_password.lower(), user.md5_password.lower()):
            return user
        return None

    @staticmethod
    def _to_record(user: User) -> UserRecord:
        return UserRecord(
            id=user.id,
            username=user.username,
            md5_password=user.md5_password,
            org_id=user.org_id,
            role_id=user.role_id,
            role=user.role,
            is_superuser=user.is_superuser,
            is_active=user.is_active,
            full_name=user.full_name,
            last_org_id=getattr(user, "last_org_id", None),
        )

    # --- Session Management ---

    async def create_session(
        self,
        user_id: int,
        refresh_token: str,
        ip_address: str | None = None,
        user_agent: str | None = None,
        expires_in_seconds: int = 604800,
        active_org_id: int | None = None,
    ) -> UserSession:
        token_hash = hash_refresh_token(refresh_token)
        expires_at = datetime.now(UTC) + timedelta(seconds=expires_in_seconds)

        if self._db_available:
            try:
                async with async_session() as session:
                    db_session = UserSession(
                        user_id=user_id,
                        refresh_token=refresh_token[:250],  # truncated if long
                        refresh_token_hash=token_hash,
                        ip_address=ip_address,
                        user_agent=user_agent[:500] if user_agent else None,
                        expires_at=expires_at,
                        is_revoked=False,
                        active_org_id=active_org_id,
                    )
                    session.add(db_session)
                    await session.commit()
                    await session.refresh(db_session)
                    return db_session
            except Exception as exc:  # noqa: BLE001
                logger.warning("Database error creating session: %s", exc)
                self._db_available = False

        # In-memory fallback for offline test environments
        sess_id = self._session_seq
        self._session_seq += 1
        sess_dict = {
            "id": sess_id,
            "user_id": user_id,
            "refresh_token": refresh_token[:250],
            "refresh_token_hash": token_hash,
            "ip_address": ip_address,
            "user_agent": user_agent[:500] if user_agent else None,
            "expires_at": expires_at,
            "created_at": datetime.now(UTC),
            "last_used_at": datetime.now(UTC),
            "is_revoked": False,
            "active_org_id": active_org_id,
        }
        self._in_memory_sessions[token_hash] = sess_dict
        return UserSession(
            id=sess_id,
            user_id=user_id,
            refresh_token=refresh_token[:250],
            refresh_token_hash=token_hash,
            ip_address=ip_address,
            user_agent=user_agent[:500] if user_agent else None,
            expires_at=expires_at,
            is_revoked=False,
            active_org_id=active_org_id,
        )

    async def get_session_by_refresh_token(
        self, refresh_token: str
    ) -> UserSession | None:
        token_hash = hash_refresh_token(refresh_token)
        now = datetime.now(UTC)
        if self._db_available:
            try:
                async with async_session() as session:
                    result = await session.execute(
                        select(UserSession).where(
                            UserSession.refresh_token_hash == token_hash,
                            UserSession.is_revoked.is_(False),
                            UserSession.expires_at > now,
                        )
                    )
                    return result.scalar_one_or_none()
            except Exception as exc:  # noqa: BLE001
                logger.warning("Database error fetching session: %s", exc)
                self._db_available = False

        s = self._in_memory_sessions.get(token_hash)
        if s and not s["is_revoked"] and s["expires_at"] > now:
            return UserSession(
                id=s["id"],
                user_id=s["user_id"],
                refresh_token=s["refresh_token"],
                refresh_token_hash=s["refresh_token_hash"],
                ip_address=s["ip_address"],
                user_agent=s["user_agent"],
                expires_at=s["expires_at"],
                is_revoked=s["is_revoked"],
                active_org_id=s.get("active_org_id"),
            )
        return None

    async def get_session_by_id(self, session_id: int) -> UserSession | None:
        """Fetch a session by primary key (used with the `sid` claim of issuer v2 tokens)."""
        now = datetime.now(UTC)
        if self._db_available:
            try:
                async with async_session() as session:
                    result = await session.execute(
                        select(UserSession).where(
                            UserSession.id == session_id,
                            UserSession.is_revoked.is_(False),
                            UserSession.expires_at > now,
                        )
                    )
                    return result.scalar_one_or_none()
            except Exception as exc:  # noqa: BLE001
                logger.warning(
                    "Database error fetching session by id %s: %s", session_id, exc
                )
                self._db_available = False

        for s in self._in_memory_sessions.values():
            if s["id"] == session_id and not s["is_revoked"] and s["expires_at"] > now:
                return UserSession(
                    id=s["id"],
                    user_id=s["user_id"],
                    refresh_token=s["refresh_token"],
                    refresh_token_hash=s["refresh_token_hash"],
                    ip_address=s["ip_address"],
                    user_agent=s["user_agent"],
                    expires_at=s["expires_at"],
                    is_revoked=s["is_revoked"],
                    active_org_id=s.get("active_org_id"),
                )
        return None

    async def set_session_active_org(self, session_id: int, org_id: int) -> None:
        if self._db_available:
            try:
                async with async_session() as session:
                    await session.execute(
                        update(UserSession)
                        .where(UserSession.id == session_id)
                        .values(active_org_id=org_id, last_used_at=datetime.now(UTC))
                    )
                    await session.commit()
                    return
            except Exception as exc:  # noqa: BLE001
                logger.warning("Database error updating session active org: %s", exc)
                self._db_available = False

        for s in self._in_memory_sessions.values():
            if s["id"] == session_id:
                s["active_org_id"] = org_id
                s["last_used_at"] = datetime.now(UTC)
                break

    async def set_user_last_org(self, user_id: int, org_id: int) -> None:
        self._in_memory_user_last_org[user_id] = org_id
        if self._db_available:
            try:
                async with async_session() as session:
                    await session.execute(
                        update(User)
                        .where(User.id == user_id)
                        .values(last_org_id=org_id, updated_at=datetime.now(UTC))
                    )
                    await session.commit()
                    return
            except Exception as exc:  # noqa: BLE001
                logger.warning("Database error updating user last org: %s", exc)
                self._db_available = False

    async def touch_session(self, session_id: int) -> None:
        try:
            async with async_session() as session:
                await session.execute(
                    update(UserSession)
                    .where(UserSession.id == session_id)
                    .values(last_used_at=datetime.now(UTC))
                )
                await session.commit()
        except Exception as exc:  # noqa: BLE001
            logger.warning("Database error touching session %s: %s", session_id, exc)
            for s in self._in_memory_sessions.values():
                if s["id"] == session_id:
                    s["last_used_at"] = datetime.now(UTC)

    async def rotate_session(
        self,
        session_id: int,
        new_refresh_token: str,
        expires_in_seconds: int,
    ) -> None:
        token_hash = hash_refresh_token(new_refresh_token)
        now = datetime.now(UTC)
        expires_at = now + timedelta(seconds=expires_in_seconds)

        if self._db_available:
            try:
                async with async_session() as session:
                    await session.execute(
                        update(UserSession)
                        .where(UserSession.id == session_id)
                        .values(
                            refresh_token=new_refresh_token[:250],
                            refresh_token_hash=token_hash,
                            expires_at=expires_at,
                            last_used_at=now,
                        )
                    )
                    await session.commit()
                    return
            except Exception as exc:  # noqa: BLE001
                logger.warning(
                    "Database error rotating session %s: %s", session_id, exc
                )
                self._db_available = False

        old_hash = None
        target_dict = None
        for h, s in self._in_memory_sessions.items():
            if s["id"] == session_id:
                old_hash = h
                target_dict = s
                break

        if target_dict and old_hash:
            del self._in_memory_sessions[old_hash]
            target_dict["refresh_token"] = new_refresh_token[:250]
            target_dict["refresh_token_hash"] = token_hash
            target_dict["expires_at"] = expires_at
            target_dict["last_used_at"] = now
            self._in_memory_sessions[token_hash] = target_dict

    async def cleanup_expired_sessions(self) -> int:
        cutoff = datetime.now(UTC) - timedelta(days=7)
        if self._db_available:
            try:
                from sqlalchemy import delete

                async with async_session() as session:
                    result = await session.execute(
                        delete(UserSession).where(
                            (
                                UserSession.is_revoked.is_(True)
                                & (UserSession.created_at < cutoff)
                            )
                            | (UserSession.expires_at < cutoff)
                        )
                    )
                    await session.commit()
                    return int(getattr(result, "rowcount", 0) or 0)
            except Exception as exc:  # noqa: BLE001
                logger.warning("Database error cleaning up expired sessions: %s", exc)
                self._db_available = False

        to_del = [
            h
            for h, s in self._in_memory_sessions.items()
            if (
                s.get("is_revoked") and s.get("created_at") and s["created_at"] < cutoff
            )
            or (s.get("expires_at") and s["expires_at"] < cutoff)
        ]
        for h in to_del:
            self._in_memory_sessions.pop(h, None)
        return len(to_del)

    async def revoke_session_by_token(self, refresh_token: str) -> bool:
        token_hash = hash_refresh_token(refresh_token)
        try:
            async with async_session() as session:
                result = await session.execute(
                    update(UserSession)
                    .where(UserSession.refresh_token_hash == token_hash)
                    .values(is_revoked=True)
                )
                await session.commit()
                rowcount = int(getattr(result, "rowcount", 0) or 0)
                return rowcount > 0
        except Exception as exc:  # noqa: BLE001
            logger.warning("Database error revoking session: %s", exc)
            if token_hash in self._in_memory_sessions:
                self._in_memory_sessions[token_hash]["is_revoked"] = True
                return True
            return False

    async def revoke_session_by_id(self, session_id: int) -> bool:
        try:
            async with async_session() as session:
                result = await session.execute(
                    update(UserSession)
                    .where(UserSession.id == session_id)
                    .values(is_revoked=True)
                )
                await session.commit()
                rowcount = int(getattr(result, "rowcount", 0) or 0)
                return rowcount > 0
        except Exception as exc:  # noqa: BLE001
            logger.warning("Database error revoking session %s: %s", session_id, exc)
            for s in self._in_memory_sessions.values():
                if s["id"] == session_id:
                    s["is_revoked"] = True
                    return True
            return False

    async def revoke_all_user_sessions(self, user_id: int) -> int:
        try:
            async with async_session() as session:
                result = await session.execute(
                    update(UserSession)
                    .where(
                        UserSession.user_id == user_id,
                        UserSession.is_revoked.is_(False),
                    )
                    .values(is_revoked=True)
                )
                await session.commit()
                return int(getattr(result, "rowcount", 0) or 0)
        except Exception as exc:  # noqa: BLE001
            logger.warning(
                "Database error revoking sessions for user %s: %s", user_id, exc
            )
            count = 0
            for s in self._in_memory_sessions.values():
                if s["user_id"] == user_id and not s["is_revoked"]:
                    s["is_revoked"] = True
                    count += 1
            return count

    async def list_user_sessions(self, user_id: int) -> list[dict]:
        try:
            async with async_session() as session:
                result = await session.execute(
                    select(UserSession)
                    .where(UserSession.user_id == user_id)
                    .order_by(UserSession.created_at.desc())
                )
                sessions = result.scalars().all()
                return [
                    {
                        "id": s.id,
                        "user_id": s.user_id,
                        "ip_address": s.ip_address,
                        "user_agent": s.user_agent,
                        "expires_at": s.expires_at.isoformat(),
                        "created_at": s.created_at.isoformat(),
                        "last_used_at": s.last_used_at.isoformat()
                        if s.last_used_at
                        else None,
                        "is_revoked": s.is_revoked,
                    }
                    for s in sessions
                ]
        except Exception as exc:  # noqa: BLE001
            logger.warning(
                "Database error listing sessions for user %s: %s", user_id, exc
            )
            return [
                {
                    "id": s["id"],
                    "user_id": s["user_id"],
                    "ip_address": s["ip_address"],
                    "user_agent": s["user_agent"],
                    "expires_at": s["expires_at"].isoformat(),
                    "created_at": s["created_at"].isoformat(),
                    "last_used_at": s["last_used_at"].isoformat()
                    if s["last_used_at"]
                    else None,
                    "is_revoked": s["is_revoked"],
                }
                for s in self._in_memory_sessions.values()
                if s["user_id"] == user_id
            ]


class ConfigUserStore(AbstractUserStore):
    """User store that reads users from application configuration (settings.auth_users)."""

    async def get_by_username(self, username: str) -> UserRecord | None:
        for u in settings.get_users():
            if u.get("username") == username:
                raw_org = u.get("org_id")
                try:
                    org_id = int(raw_org) if raw_org is not None else None
                except ValueError, TypeError:
                    org_id = None

                role = str(u.get("role", "user")).lower()
                is_su = bool(u.get("is_superuser") or role in ("superuser", "admin"))
                if is_su and role not in ("superuser", "admin"):
                    role = "superuser"

                role_id = 1 if is_su else int(u.get("role_id", 3))

                return UserRecord(
                    id=int(u.get("id", 1 if is_su else 0)),
                    username=u["username"],
                    md5_password=u.get("md5_password", ""),
                    org_id=org_id,
                    role_id=role_id,
                    role=role,
                    is_superuser=is_su,
                    is_active=bool(u.get("is_active", True)),
                    full_name=u.get("full_name"),
                    last_org_id=u.get("last_org_id"),
                )
        return None

    async def get_by_id(self, user_id: int) -> UserRecord | None:
        for u in settings.get_users():
            role = str(u.get("role", "user")).lower()
            is_su = bool(u.get("is_superuser") or role in ("superuser", "admin"))
            uid = int(u.get("id", 1 if is_su else 0))
            if uid == user_id:
                return await self.get_by_username(u["username"])
        return None

    async def authenticate(
        self, username: str, plain_password: str
    ) -> UserRecord | None:
        user = await self.get_by_username(username)
        if not user or not user.is_active:
            return None
        if verify_md5_password(plain_password, user.md5_password):
            return user
        if hmac.compare_digest(plain_password.lower(), user.md5_password.lower()):
            return user
        return None


# Default singleton instance
_default_user_store: DatabaseUserStore = DatabaseUserStore()


def get_user_store() -> DatabaseUserStore:
    """Dependency / accessor for the active DatabaseUserStore."""
    return _default_user_store
