from __future__ import annotations

from datetime import datetime

from sqlalchemy import (
    DateTime,
    ForeignKey,
    Integer,
    String,
    UniqueConstraint,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from etranprocessing_db.base import Base


class CatalogCategory(Base):
    __tablename__ = "catalog_categories"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    org_id: Mapped[int] = mapped_column(Integer, nullable=False, index=True)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    parent_id: Mapped[int | None] = mapped_column(
        Integer,
        ForeignKey("catalog_categories.id", ondelete="CASCADE"),
        nullable=True,
        index=True,
    )
    sort_order: Mapped[int] = mapped_column(
        Integer, nullable=False, default=0, server_default="0"
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    parent: Mapped[CatalogCategory | None] = relationship(
        "CatalogCategory", remote_side="CatalogCategory.id", back_populates="children"
    )
    children: Mapped[list[CatalogCategory]] = relationship(
        "CatalogCategory", back_populates="parent", cascade="all, delete-orphan"
    )
    items: Mapped[list[CatalogItem]] = relationship(
        "CatalogItem", back_populates="category", cascade="all, delete-orphan"
    )


class CatalogItem(Base):
    __tablename__ = "catalog_items"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    org_id: Mapped[int] = mapped_column(Integer, nullable=False, index=True)
    category_id: Mapped[int] = mapped_column(
        Integer,
        ForeignKey("catalog_categories.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    tsp_code: Mapped[int] = mapped_column(Integer, nullable=False)
    name: Mapped[str] = mapped_column(String(255), nullable=False)
    printname: Mapped[str | None] = mapped_column(String(255), nullable=True)
    price: Mapped[int] = mapped_column(
        Integer, nullable=False, default=0, server_default="0"
    )
    protypenumber: Mapped[int] = mapped_column(
        Integer, nullable=False, default=0, server_default="0"
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now()
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    category: Mapped[CatalogCategory] = relationship(
        "CatalogCategory", back_populates="items"
    )

    __table_args__ = (
        UniqueConstraint("org_id", "tsp_code", name="uq_catalog_org_tsp"),
    )
