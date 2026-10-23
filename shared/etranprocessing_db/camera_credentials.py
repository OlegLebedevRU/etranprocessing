"""Declarative camera vault schema. Crypto and state transitions belong to MB."""

from datetime import datetime
from uuid import UUID

from sqlalchemy import (
    BigInteger,
    CheckConstraint,
    DateTime,
    ForeignKey,
    Integer,
    LargeBinary,
    String,
    UniqueConstraint,
    Uuid,
)
from sqlalchemy.orm import Mapped, mapped_column

from etranprocessing_db.base import Base


class CameraCredentialProfile(Base):
    __tablename__ = "camera_credential_profiles"

    id: Mapped[UUID] = mapped_column(Uuid, primary_key=True)
    tenant_id: Mapped[int] = mapped_column(Integer, nullable=False)
    terminal_id: Mapped[int] = mapped_column(ForeignKey("terminals.id"), nullable=False)
    device_id: Mapped[int] = mapped_column(Integer, nullable=False)
    sn: Mapped[str] = mapped_column(String(64), nullable=False)
    camera_id: Mapped[str] = mapped_column(String(12), nullable=False)
    version: Mapped[int] = mapped_column(Integer, nullable=False)
    active_operation_id: Mapped[UUID | None] = mapped_column(Uuid, nullable=True)
    pending_operation_id: Mapped[UUID | None] = mapped_column(Uuid, nullable=True)

    __table_args__ = (
        UniqueConstraint(
            "terminal_id", "camera_id", name="uq_camera_credential_profile"
        ),
        CheckConstraint("version >= 0", name="ck_camera_profile_version"),
    )


class CameraCredentialReceipt(Base):
    __tablename__ = "camera_credential_receipts"

    operation_id: Mapped[UUID] = mapped_column(Uuid, primary_key=True)
    profile_id: Mapped[UUID] = mapped_column(
        ForeignKey("camera_credential_profiles.id"), nullable=False
    )
    version: Mapped[int] = mapped_column(Integer, nullable=False)
    kind: Mapped[str] = mapped_column(String(8), nullable=False)
    state: Mapped[str] = mapped_column(String(16), nullable=False)
    key_id: Mapped[str] = mapped_column(String(64), nullable=False)
    algorithm: Mapped[str] = mapped_column(String(16), nullable=False)
    nonce: Mapped[bytes] = mapped_column(LargeBinary, nullable=False)
    ciphertext: Mapped[bytes] = mapped_column(LargeBinary, nullable=False)
    wrap_nonce: Mapped[bytes] = mapped_column(LargeBinary, nullable=False)
    wrapped_dek: Mapped[bytes] = mapped_column(LargeBinary, nullable=False)
    expires_at_ms: Mapped[int] = mapped_column(BigInteger, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False
    )

    __table_args__ = (
        UniqueConstraint("profile_id", "version", name="uq_camera_credential_version"),
        CheckConstraint("version > 0", name="ck_camera_receipt_version"),
        CheckConstraint("kind IN ('replace', 'clear')", name="ck_camera_receipt_kind"),
        CheckConstraint(
            "state IN ('pending', 'active', 'superseded', 'failed', 'cleared')",
            name="ck_camera_receipt_state",
        ),
        CheckConstraint("algorithm = 'A256GCM-v1'", name="ck_camera_receipt_algorithm"),
    )
