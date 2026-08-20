from datetime import datetime

from sqlalchemy import (
    Boolean,
    DateTime,
    ForeignKey,
    Integer,
    String,
    UniqueConstraint,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.database import Base


class Org(Base):
    __tablename__ = "orgs"

    org_id: Mapped[int] = mapped_column(Integer, primary_key=True)
    org_name: Mapped[str] = mapped_column(String(150), nullable=False)
    name: Mapped[str] = mapped_column(String(150), nullable=False)
    status: Mapped[int] = mapped_column(Integer, default=1)
    is_active: Mapped[bool] = mapped_column(Boolean, default=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )


class MenuVariant(Base):
    __tablename__ = "menu_variants"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    name: Mapped[str] = mapped_column(String(255), unique=True, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    groups: Mapped[list[Group]] = relationship(
        back_populates="menu_variant", cascade="all, delete-orphan"
    )
    bindings: Mapped[list[TerminalMenuBinding]] = relationship(
        back_populates="menu_variant", cascade="all, delete-orphan"
    )


class Group(Base):
    __tablename__ = "groups"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    menu_variant_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("menu_variants.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    org_id: Mapped[int] = mapped_column(Integer, nullable=False, index=True)
    number: Mapped[int] = mapped_column(Integer, nullable=False)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    parent_id: Mapped[int | None] = mapped_column(
        Integer, ForeignKey("groups.id", ondelete="SET NULL"), nullable=True, index=True
    )

    menu_variant: Mapped[MenuVariant] = relationship(back_populates="groups")
    parent: Mapped[Group | None] = relationship(
        "Group", remote_side="Group.id", back_populates="children"
    )
    children: Mapped[list[Group]] = relationship("Group", back_populates="parent")
    services: Mapped[list[Service]] = relationship(
        "Service", back_populates="group", cascade="all, delete-orphan"
    )


class Service(Base):
    __tablename__ = "services"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    menu_variant_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("menu_variants.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    group_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("groups.id", ondelete="CASCADE"), nullable=False, index=True
    )
    tsp_code: Mapped[int] = mapped_column(Integer, nullable=False)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    printname: Mapped[str | None] = mapped_column(String(255), nullable=True)
    price: Mapped[int] = mapped_column(Integer, default=0)
    protypenumber: Mapped[int] = mapped_column(Integer, default=0)

    group: Mapped[Group] = relationship("Group", back_populates="services")

    __table_args__ = (
        UniqueConstraint("menu_variant_id", "tsp_code", name="uq_service_variant_tsp"),
    )


class TerminalMenuBinding(Base):
    __tablename__ = "terminal_menu_bindings"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[int] = mapped_column(
        Integer, unique=True, nullable=False, index=True
    )
    menu_variant_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("menu_variants.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )

    menu_variant: Mapped[MenuVariant] = relationship(back_populates="bindings")
