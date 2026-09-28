import pytest
from fastapi import HTTPException
from sqlalchemy import create_engine, func, select, text

from app.models import Terminal
from app.routers.settings import (
    _device_filter_condition,
    _latest_pin_subquery,
    _visible_terminals_query,
)


def test_deleted_terminal_is_absent_from_rows_and_pagination_count():
    engine = create_engine("sqlite://")
    with engine.begin() as db:
        db.execute(text("CREATE TABLE terminals (id INTEGER, org_id INTEGER)"))
        db.execute(
            text(
                "CREATE TABLE l4desk_terminals (terminal_id INTEGER, tenant_id INTEGER, deleted_at TEXT)"
            )
        )
        db.execute(
            text("INSERT INTO terminals VALUES (1,1000),(2,1000),(3,1000),(4,10000)")
        )
        db.execute(
            text(
                "INSERT INTO l4desk_terminals VALUES (1,1000,NULL),(2,1000,'2026-01-01'),(4,10000,NULL)"
            )
        )
        query = _visible_terminals_query(1000).with_only_columns(Terminal.id)
        # Active L4Desk and an ordinary terminal without an L4Desk row remain visible.
        assert db.execute(query.order_by(Terminal.id)).scalars().all() == [1, 3]
        assert (
            db.execute(select(func.count()).select_from(query.subquery())).scalar_one()
            == 2
        )


def test_exact_device_ids_and_inclusive_ranges_can_be_combined():
    engine = create_engine("sqlite://")
    with engine.begin() as db:
        db.execute(text("CREATE TABLE terminals (id INTEGER, device_id INTEGER)"))
        db.execute(
            text(
                "INSERT INTO terminals VALUES (1,773),(2,1000008),(3,1000009),(4,1000010)"
            )
        )
        query = (
            select(Terminal.device_id)
            .where(_device_filter_condition("773, 1000008-1000009"))
            .order_by(Terminal.device_id)
        )
        assert db.execute(query).scalars().all() == [773, 1000008, 1000009]

    with pytest.raises(HTTPException) as error:
        _device_filter_condition("1000010-1000008")
    assert error.value.status_code == 422


def test_latest_pin_sort_uses_issue_time_and_keeps_unissued_last():
    engine = create_engine("sqlite://")
    with engine.begin() as db:
        db.execute(
            text(
                "CREATE TABLE terminals (id INTEGER, org_id INTEGER, device_id INTEGER)"
            )
        )
        db.execute(
            text(
                "CREATE TABLE certificate_pins (terminal_id INTEGER, org_id INTEGER, created_at TEXT)"
            )
        )
        db.execute(
            text("INSERT INTO terminals VALUES (1,1000,773),(2,1000,774),(3,1000,775)")
        )
        db.execute(
            text(
                "INSERT INTO certificate_pins VALUES (1,1000,'2026-01-01'),(1,1000,'2026-02-01'),(3,1000,'2026-03-01')"
            )
        )
        latest = _latest_pin_subquery(1000)
        query = (
            select(Terminal.device_id)
            .outerjoin(latest, latest.c.terminal_id == Terminal.id)
            .order_by(
                latest.c.last_pin_issued_at.desc().nulls_last(), Terminal.id.asc()
            )
        )
        assert db.execute(query).scalars().all() == [775, 773, 774]
