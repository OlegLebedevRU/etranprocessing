"""Declarative file-manager capability registration owned by ProcessingBackend."""

from datetime import datetime
from uuid import UUID

from sqlalchemy import (
    BigInteger,
    Boolean,
    CheckConstraint,
    DateTime,
    ForeignKey,
    Index,
    Integer,
    String,
    Text,
    text,
)
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class FileManagerAgent(Base):
    __tablename__ = "fm_agents"

    terminal_id: Mapped[int] = mapped_column(
        ForeignKey("terminals.id", ondelete="CASCADE"), primary_key=True
    )
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False, index=True)
    agent_instance_id: Mapped[UUID] = mapped_column(nullable=False)
    agent_version: Mapped[str] = mapped_column(String(64), nullable=False)
    protocol_version: Mapped[int] = mapped_column(Integer, nullable=False)
    capabilities: Mapped[list[str]] = mapped_column(JSONB, nullable=False)
    filesystem_ready: Mapped[bool] = mapped_column(Boolean, nullable=False)
    cert_serial: Mapped[str] = mapped_column(String(100), nullable=False)
    last_seen_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )


class FileManagerOperation(Base):
    __tablename__ = "fm_operations"
    __table_args__ = (
        CheckConstraint(
            "kind IN ('session', 'list', 'upload', 'download')",
            name="ck_fm_operation_kind",
        ),
        CheckConstraint(
            "state IN ('created', 'active', 'running', 'verifying', 'committing', 'completed', 'failed', 'cancelled', 'cancelling')",
            name="ck_fm_operation_state",
        ),
        CheckConstraint(
            "size_bytes IS NULL OR size_bytes BETWEEN 0 AND 67108864",
            name="ck_fm_operation_size",
        ),
        Index(
            "ix_fm_operation_tenant_device", "tenant_id", "terminal_id", "created_at"
        ),
        Index(
            "uq_fm_active_transfer",
            "terminal_id",
            unique=True,
            postgresql_where=text(
                "kind IN ('upload', 'download') AND state IN ('created', 'running', 'verifying', 'committing', 'cancelling')"
            ),
        ),
    )

    id: Mapped[UUID] = mapped_column(primary_key=True)
    terminal_id: Mapped[int] = mapped_column(ForeignKey("terminals.id"), nullable=False)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    lease_id: Mapped[UUID] = mapped_column(nullable=False)
    owner_user_id: Mapped[str] = mapped_column(String(128), nullable=False)
    owner_session_id: Mapped[str] = mapped_column(String(128), nullable=False)
    owner_role: Mapped[str] = mapped_column(String(32), nullable=False)
    kind: Mapped[str] = mapped_column(String(16), nullable=False)
    state: Mapped[str] = mapped_column(String(24), nullable=False)
    path: Mapped[str] = mapped_column(Text, nullable=False)
    size_bytes: Mapped[int | None] = mapped_column(BigInteger)
    sha256: Mapped[str | None] = mapped_column(String(64))
    data: Mapped[dict] = mapped_column(JSONB, nullable=False, default=dict)
    error_code: Mapped[str | None] = mapped_column(String(64))
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )
