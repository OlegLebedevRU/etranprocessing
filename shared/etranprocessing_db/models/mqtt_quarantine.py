from datetime import datetime
from uuid import UUID

from sqlalchemy import BigInteger, Boolean, DateTime, Integer, String, Uuid, func
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class MqttQuarantineOperation(Base):
    __tablename__ = "mqtt_quarantine_operations"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True)
    operation_id: Mapped[UUID] = mapped_column(Uuid, unique=True)
    ip: Mapped[str] = mapped_column(String(45), index=True)
    actor: Mapped[str] = mapped_column(String(160))
    reason: Mapped[str] = mapped_column(String(500))
    ttl: Mapped[int] = mapped_column(Integer)
    expiry: Mapped[int] = mapped_column(BigInteger)
    preview_token: Mapped[str] = mapped_column(String(64))
    acknowledged: Mapped[bool] = mapped_column(Boolean)
    status: Mapped[str] = mapped_column(String(32), index=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
