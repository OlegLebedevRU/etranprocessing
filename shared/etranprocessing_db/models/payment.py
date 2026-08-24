from __future__ import annotations

from datetime import datetime
from typing import TYPE_CHECKING

from sqlalchemy import (
    BigInteger,
    DateTime,
    ForeignKey,
    Index,
    Integer,
    String,
    UniqueConstraint,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from etranprocessing_db.base import Base

if TYPE_CHECKING:
    from etranprocessing_db.models.menu import MenuVariantSnapshot
    from etranprocessing_db.models.org import Org
    from etranprocessing_db.models.terminal import Terminal


class Tsp(Base):
    __tablename__ = "tsp"

    tsp_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    tsp_code: Mapped[int] = mapped_column(Integer, unique=True, nullable=False)
    tsp_name: Mapped[str] = mapped_column(String(255), nullable=False)

    __table_args__ = (Index("idx_tsp_code", "tsp_code"),)


class TspParameterCode(Base):
    __tablename__ = "tsp_parameter_codes"

    param_id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    prototypenumber: Mapped[int] = mapped_column(Integer, nullable=False)
    parameter_code: Mapped[int] = mapped_column(Integer, nullable=False)
    code_description: Mapped[str | None] = mapped_column(String(255), nullable=True)

    __table_args__ = (
        UniqueConstraint(
            "prototypenumber", "parameter_code", name="uq_prototype_param_code"
        ),
        Index("idx_tsp_param_prototype", "prototypenumber"),
    )


class Payment(Base):
    __tablename__ = "payments"

    paym_id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    paym_datetime: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    paym_amount: Mapped[int] = mapped_column(BigInteger, nullable=False)
    paym_ext_id: Mapped[str] = mapped_column(String(20), nullable=False)
    paym_tsp_code: Mapped[int] = mapped_column(Integer, nullable=False)
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id"), nullable=False
    )
    org_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("orgs.org_id"), nullable=False
    )
    paym_state: Mapped[int] = mapped_column(Integer, default=0)
    pay_type_id: Mapped[int] = mapped_column(Integer, default=0)
    menu_snapshot_id: Mapped[int | None] = mapped_column(
        Integer,
        ForeignKey("menu_variant_snapshots.id", ondelete="SET NULL"),
        nullable=True,
    )

    terminal: Mapped[Terminal] = relationship("Terminal")
    org: Mapped[Org] = relationship("Org")
    menu_snapshot: Mapped[MenuVariantSnapshot | None] = relationship(
        "MenuVariantSnapshot"
    )
    params: Mapped[list[PaymentParam]] = relationship(
        back_populates="payment", cascade="all, delete-orphan"
    )

    __table_args__ = (
        Index("idx_payments_terminal_id", "terminal_id"),
        Index("idx_payments_org_id", "org_id"),
        Index("idx_payments_ext_id", "paym_ext_id"),
        Index("idx_payments_tsp_code", "paym_tsp_code"),
        Index("idx_payments_datetime", "paym_datetime"),
        Index("idx_payments_menu_snapshot_id", "menu_snapshot_id"),
    )


class PaymentParam(Base):
    __tablename__ = "payment_params"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    paym_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("payments.paym_id", ondelete="CASCADE"), nullable=False
    )
    param_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("tsp_parameter_codes.param_id"), nullable=False
    )
    param_value: Mapped[str | None] = mapped_column(String(2000), nullable=True)

    payment: Mapped[Payment] = relationship(back_populates="params")
    param: Mapped[TspParameterCode] = relationship()

    __table_args__ = (
        Index("idx_payment_params_paym_id", "paym_id"),
        Index("idx_payment_params_param_id", "param_id"),
    )


class BalanceTerminalTsp(Base):
    __tablename__ = "balance_terminal_tsp"

    rec_id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    int_day: Mapped[int] = mapped_column(Integer, nullable=False)
    org_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("orgs.org_id"), nullable=False
    )
    terminal_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("terminals.id"), nullable=False
    )
    tsp_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("tsp.tsp_id"), nullable=False
    )
    menu_snapshot_id: Mapped[int | None] = mapped_column(
        Integer,
        ForeignKey("menu_variant_snapshots.id", ondelete="SET NULL"),
        nullable=True,
    )
    amount: Mapped[int] = mapped_column(BigInteger, default=0)
    count: Mapped[int] = mapped_column(Integer, default=0)
    update_datetime: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    menu_snapshot: Mapped[MenuVariantSnapshot | None] = relationship(
        "MenuVariantSnapshot"
    )

    __table_args__ = (
        UniqueConstraint(
            "int_day",
            "terminal_id",
            "tsp_id",
            "menu_snapshot_id",
            name="uq_balance_day_terminal_tsp_snap",
        ),
        Index("idx_balance_int_day", "int_day"),
        Index("idx_balance_terminal_id", "terminal_id"),
        Index("idx_balance_tsp_id", "tsp_id"),
        Index("idx_balance_org_id", "org_id"),
        Index("idx_balance_menu_snapshot_id", "menu_snapshot_id"),
    )
