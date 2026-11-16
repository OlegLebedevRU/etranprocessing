"""Declarative identity-change and superuser device incident contracts."""

from datetime import datetime
from uuid import UUID

from sqlalchemy import (
    JSON,
    CheckConstraint,
    DateTime,
    ForeignKey,
    Index,
    Integer,
    String,
    Text,
    Uuid,
    func,
    text,
)
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class TerminalIdentityChange(Base):
    __tablename__ = "terminal_identity_changes"

    terminal_id: Mapped[int] = mapped_column(
        ForeignKey("terminals.id"), nullable=False, index=True
    )
    operation_id: Mapped[UUID] = mapped_column(Uuid, primary_key=True)
    source_org_id: Mapped[int] = mapped_column(Integer)
    target_org_id: Mapped[int] = mapped_column(ForeignKey("orgs.org_id"))
    archived_technical_terminal_id: Mapped[int | None] = mapped_column(
        ForeignKey("l4desk_terminals.terminal_id")
    )
    old_serial: Mapped[str | None] = mapped_column(String(100))
    new_serial: Mapped[str | None] = mapped_column(String(100))
    status: Mapped[str] = mapped_column(String(20), nullable=False)
    requested_by: Mapped[str] = mapped_column(String(100))
    pin_id: Mapped[int | None] = mapped_column(ForeignKey("certificate_pins.id"))
    csr_sha256: Mapped[str | None] = mapped_column(String(64))
    certificate_response: Mapped[str | None] = mapped_column(Text)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    issued_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    completed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        CheckConstraint(
            "status IN ('pending', 'sync_pending', 'completed', 'cancelled')",
            name="ck_identity_change_status",
        ),
        CheckConstraint(
            "source_org_id <> target_org_id", name="ck_identity_change_different_orgs"
        ),
        Index(
            "uq_identity_change_active_terminal",
            "terminal_id",
            unique=True,
            postgresql_where=text("status IN ('pending', 'sync_pending')"),
            sqlite_where=text("status IN ('pending', 'sync_pending')"),
        ),
        Index("idx_identity_change_old_serial", "terminal_id", "old_serial"),
    )


class DeviceAdminEvent(Base):
    __tablename__ = "device_admin_events"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    event_key: Mapped[str] = mapped_column(String(160), unique=True, nullable=False)
    terminal_id: Mapped[int] = mapped_column(ForeignKey("terminals.id"), nullable=False)
    operation_id: Mapped[UUID] = mapped_column(Uuid, nullable=False)
    event_type: Mapped[str] = mapped_column(String(50), nullable=False)
    status: Mapped[str] = mapped_column(
        String(20), nullable=False, server_default="new"
    )
    details: Mapped[dict] = mapped_column(JSON, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_by: Mapped[str | None] = mapped_column(String(100))
    comment: Mapped[str | None] = mapped_column(Text)

    __table_args__ = (
        CheckConstraint(
            "status IN ('new', 'in_progress', 'closed')",
            name="ck_device_admin_event_status",
        ),
        Index("idx_device_admin_events_status_created", "status", "created_at"),
    )
