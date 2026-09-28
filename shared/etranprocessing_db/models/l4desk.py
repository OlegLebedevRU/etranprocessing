from __future__ import annotations

from datetime import datetime
from typing import Any

from sqlalchemy import (
    JSON,
    BigInteger,
    Boolean,
    CheckConstraint,
    DateTime,
    ForeignKey,
    ForeignKeyConstraint,
    Index,
    Integer,
    String,
    UniqueConstraint,
    func,
    text,
)
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class L4DeskRegistration(Base):
    __tablename__ = "l4desk_registrations"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    email_normalized: Mapped[str] = mapped_column(
        String(255), nullable=False, unique=True
    )
    password_hash: Mapped[str] = mapped_column(String(255), nullable=False)
    token_hash: Mapped[str] = mapped_column(String(64), nullable=False, unique=True)
    terms_version: Mapped[str] = mapped_column(String(64), nullable=False)
    timezone: Mapped[str] = mapped_column(String(64), nullable=False)
    source: Mapped[str | None] = mapped_column(String(128))
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    consumed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    user_id: Mapped[int | None] = mapped_column(
        ForeignKey("users.id", ondelete="SET NULL")
    )
    tenant_id: Mapped[int | None] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="SET NULL")
    )

    __table_args__ = (
        CheckConstraint(
            "expires_at > created_at", name="l4desk_registration_expiry_ck"
        ),
        CheckConstraint(
            "consumed_at IS NULL OR consumed_at >= created_at",
            name="l4desk_registration_consumed_ck",
        ),
        Index("l4desk_registration_expiry_ix", "expires_at"),
    )


class L4DeskTenantProfile(Base):
    __tablename__ = "l4desk_tenant_profiles"

    tenant_id: Mapped[int] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT"), primary_key=True
    )
    timezone: Mapped[str] = mapped_column(String(64), nullable=False)
    pending_timezone: Mapped[str | None] = mapped_column(String(64))
    timezone_effective_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True)
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        CheckConstraint(
            "(pending_timezone IS NULL) = (timezone_effective_at IS NULL)",
            name="l4desk_timezone_pending_ck",
        ),
    )


class L4DeskMembership(Base):
    __tablename__ = "l4desk_memberships"

    tenant_id: Mapped[int] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT"), primary_key=True
    )
    user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="RESTRICT"), primary_key=True
    )
    role_id: Mapped[int] = mapped_column(Integer, server_default="5")
    is_owner: Mapped[bool] = mapped_column(Boolean, server_default="false")
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        CheckConstraint("role_id = 5", name="l4desk_membership_role_ck"),
        Index("l4desk_membership_user_ix", "user_id"),
        Index(
            "l4desk_membership_owner_uq",
            "tenant_id",
            unique=True,
            postgresql_where=text("is_owner"),
            sqlite_where=text("is_owner"),
        ),
    )


class L4DeskTerminal(Base):
    __tablename__ = "l4desk_terminals"

    terminal_id: Mapped[int] = mapped_column(
        Integer, primary_key=True, autoincrement=False
    )
    tenant_id: Mapped[int] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="RESTRICT")
    )
    runtime_terminal_id: Mapped[int | None] = mapped_column(
        ForeignKey("terminals.id", ondelete="SET NULL"), unique=True
    )
    ordinal: Mapped[int] = mapped_column(BigInteger, nullable=False)
    sn: Mapped[str] = mapped_column(String(128), nullable=False)
    external_terminal_id: Mapped[str] = mapped_column(String(128), nullable=False)
    device_id: Mapped[int | None] = mapped_column(Integer)
    operation_id: Mapped[str] = mapped_column(String(128), nullable=False, unique=True)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    provisioning_state: Mapped[str] = mapped_column(
        String(32), server_default="pending"
    )
    pin_state: Mapped[str] = mapped_column(String(32), server_default="pending")
    certificate_reference: Mapped[str | None] = mapped_column(String(128))
    last_error: Mapped[str | None] = mapped_column(String(500))
    first_online_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    last_online_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    deleted_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        UniqueConstraint("terminal_id", "tenant_id", name="l4desk_terminal_tenant_uq"),
        UniqueConstraint("tenant_id", "ordinal", name="l4desk_terminal_ordinal_uq"),
        UniqueConstraint(
            "tenant_id", "external_terminal_id", name="l4desk_terminal_external_uq"
        ),
        CheckConstraint("ordinal > 0", name="l4desk_terminal_ordinal_ck"),
        CheckConstraint(
            "provisioning_state IN ('pending', 'ready', 'failed')",
            name="l4desk_terminal_provisioning_ck",
        ),
        CheckConstraint(
            "pin_state IN ('pending', 'issued', 'consumed', 'expired', 'failed')",
            name="l4desk_terminal_pin_ck",
        ),
        Index("l4desk_terminal_correlation_ix", "correlation_id"),
    )


class L4DeskAuditEvent(Base):
    __tablename__ = "l4desk_audit_events"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int | None] = mapped_column(
        ForeignKey("orgs.org_id", ondelete="SET NULL")
    )
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    event_type: Mapped[str] = mapped_column(String(64), nullable=False)
    subject_type: Mapped[str] = mapped_column(String(64), nullable=False)
    subject_id: Mapped[str] = mapped_column(String(128), nullable=False)
    operation_id: Mapped[str | None] = mapped_column(String(128))
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    outcome: Mapped[str] = mapped_column(String(32), nullable=False)
    details: Mapped[dict[str, Any] | None] = mapped_column(JSON)
    occurred_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    __table_args__ = (
        Index("l4desk_audit_tenant_time_ix", "tenant_id", "occurred_at"),
        Index("l4desk_audit_correlation_ix", "correlation_id"),
    )


class L4DeskRemoteSession(Base):
    __tablename__ = "l4desk_remote_sessions"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    terminal_id: Mapped[int] = mapped_column(Integer, nullable=False)
    operation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(128), nullable=False)
    provider_session_id: Mapped[str | None] = mapped_column(String(128), unique=True)
    requested_by_user_id: Mapped[int | None] = mapped_column(
        ForeignKey("users.id", ondelete="SET NULL")
    )
    session_type: Mapped[str] = mapped_column(String(32), nullable=False)
    state: Mapped[str] = mapped_column(String(32), server_default="reserved")
    requested_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    active_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    closed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    reason: Mapped[str | None] = mapped_column(String(128))
    last_event_id: Mapped[str | None] = mapped_column(String(128))
    last_cursor: Mapped[int] = mapped_column(BigInteger, server_default="0")
    source_events_hash: Mapped[str | None] = mapped_column(String(64))

    __table_args__ = (
        ForeignKeyConstraint(
            ["terminal_id", "tenant_id"],
            ["l4desk_terminals.terminal_id", "l4desk_terminals.tenant_id"],
            ondelete="RESTRICT",
            name="l4desk_session_terminal_fk",
        ),
        UniqueConstraint(
            "tenant_id", "operation_id", name="l4desk_session_operation_uq"
        ),
        CheckConstraint(
            "session_type IN ('console', 'video')", name="l4desk_session_type_ck"
        ),
        CheckConstraint(
            "state IN ('reserved', 'start_requested', 'active', 'stop_requested', 'closed', 'failed')",
            name="l4desk_session_state_ck",
        ),
        CheckConstraint("last_cursor >= 0", name="l4desk_session_cursor_ck"),
        CheckConstraint(
            "closed_at IS NULL OR active_at IS NULL OR closed_at >= active_at",
            name="l4desk_session_interval_ck",
        ),
        CheckConstraint(
            "state NOT IN ('closed', 'failed') OR closed_at IS NOT NULL",
            name="l4desk_session_closed_ck",
        ),
        CheckConstraint(
            "state != 'active' OR active_at IS NOT NULL",
            name="l4desk_session_active_ck",
        ),
        Index(
            "l4desk_session_reservation_uq",
            "terminal_id",
            unique=True,
            postgresql_where=text(
                "state IN ('reserved', 'start_requested', 'active', 'stop_requested')"
            ),
            sqlite_where=text(
                "state IN ('reserved', 'start_requested', 'active', 'stop_requested')"
            ),
        ),
        Index("l4desk_session_tenant_time_ix", "tenant_id", "requested_at"),
        Index("l4desk_session_state_ix", "state"),
        Index("l4desk_session_correlation_ix", "correlation_id"),
    )
