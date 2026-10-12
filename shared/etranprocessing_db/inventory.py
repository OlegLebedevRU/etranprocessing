"""Read projection of authenticated IoT event 75; never certificate authority."""

from datetime import datetime
from uuid import UUID

from sqlalchemy import JSON, BigInteger, DateTime, ForeignKey, Integer, String, Uuid
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class TerminalToolsInventory(Base):
    __tablename__ = "terminal_tools_inventory"

    terminal_id: Mapped[int] = mapped_column(
        ForeignKey("terminals.id", ondelete="CASCADE"), primary_key=True
    )
    tenant_id: Mapped[int] = mapped_column(Integer, index=True)
    sn: Mapped[str] = mapped_column(String(128))
    cert_thumbprint: Mapped[str] = mapped_column(String(40))
    cert_serial: Mapped[str] = mapped_column(String(40))
    cert_expires_at: Mapped[datetime] = mapped_column(DateTime(timezone=True))
    payload_sha256: Mapped[str] = mapped_column(String(64))
    suite_version: Mapped[str] = mapped_column(String(64))
    components: Mapped[list[dict]] = mapped_column(
        JSON().with_variant(JSONB(), "postgresql")
    )
    event_uuid: Mapped[UUID] = mapped_column(Uuid)
    source_revision: Mapped[int] = mapped_column(BigInteger)
    published_at: Mapped[datetime] = mapped_column(DateTime(timezone=True))
    received_at: Mapped[datetime] = mapped_column(DateTime(timezone=True))


class TerminalToolsInventoryInbox(Base):
    __tablename__ = "terminal_tools_inventory_inbox"

    event_uuid: Mapped[UUID] = mapped_column(Uuid, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(Integer, index=True)
    terminal_id: Mapped[int] = mapped_column(
        ForeignKey("terminals.id", ondelete="CASCADE")
    )
    payload_sha256: Mapped[str] = mapped_column(String(64))
    source_revision: Mapped[int] = mapped_column(BigInteger)
    outcome: Mapped[str] = mapped_column(String(24))
    received_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
