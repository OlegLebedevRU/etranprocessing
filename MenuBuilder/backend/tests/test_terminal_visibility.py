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


@pytest.mark.anyio
@pytest.mark.parametrize(
    "status_filter,expected",
    [("all", [773, 4623, 4624]), ("online", [773, 4623]), ("disabled", [4624])],
)
@pytest.mark.parametrize("provider", ["complete", "partial", "failed"])
async def test_l4desk_list_keeps_unenrolled_devices_and_filters_before_pagination(
    monkeypatch, status_filter, expected, provider
):
    from types import SimpleNamespace
    from unittest.mock import AsyncMock, MagicMock

    from app.config import settings
    from app.models import CertificatePin
    from app.routers import settings as router

    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    engine = create_engine("sqlite://")
    with engine.begin() as conn:
        Terminal.__table__.create(conn)
        CertificatePin.__table__.create(conn)
        conn.execute(
            text(
                "CREATE TABLE l4desk_terminals (terminal_id INTEGER, tenant_id INTEGER, deleted_at TEXT)"
            )
        )
        rows = [
            Terminal(
                id=i,
                device_id=i,
                org_id=1000,
                sn=f"fixture{i}",
                is_active=i != 4624,
                l4desk_subscription_enabled=i == 773,
                iot_provisioned=True,
            )
            for i in [773, 4623, 4624]
        ]
        for item in rows:
            conn.execute(
                Terminal.__table__.insert().values(
                    id=item.id,
                    org_id=item.org_id,
                    device_id=item.device_id,
                    sn=item.sn,
                    is_active=item.is_active,
                    l4desk_subscription_enabled=item.l4desk_subscription_enabled,
                )
            )
        conn.execute(
            Terminal.__table__.insert().values(
                id=9,
                org_id=2000,
                device_id=9,
                sn="foreign",
                is_active=True,
                l4desk_subscription_enabled=True,
            )
        )
        by_id = {item.id: item for item in rows}

        async def execute(stmt):
            if stmt.column_descriptions[0].get("entity") is Terminal:
                ids = conn.execute(stmt.with_only_columns(Terminal.id)).scalars().all()
                return SimpleNamespace(all=lambda: [(by_id[i], None) for i in ids])
            if "count(" in str(stmt):
                count = conn.execute(stmt).scalar_one()
                return SimpleNamespace(scalar_one=lambda: count)
            result = MagicMock()
            result.scalars.return_value.all.return_value = []
            return result

        async def scalars(stmt):
            if stmt.column_descriptions[0].get("entity") is Terminal:
                ids = conn.execute(stmt).scalars().all()
                return SimpleNamespace(all=lambda: ids)
            return SimpleNamespace(all=list)

        async def presence(tenant, ids):
            assert tenant == 1000 and set(ids) == {773, 4623, 4624}
            if provider == "failed":
                raise TimeoutError("fixture provider unavailable")
            return {
                i: {"is_online": True, "is_blocked": False}
                for i in ids
                if provider != "partial" or i == 773
            }

        monkeypatch.setattr(router, "list_subscriptions", AsyncMock(return_value=[]))
        monkeypatch.setattr(router, "presence_snapshot", presence)
        repo = MagicMock()
        repo.get_earliest_active_terminal_id = AsyncMock(return_value=None)
        monkeypatch.setattr(router, "L4DeskRepository", lambda db: repo)
        db = SimpleNamespace(execute=execute, scalars=scalars)
        if status_filter == "online" and provider != "complete":
            expected = [773] if provider == "partial" else []
        for page, identifier in enumerate(expected or [None], 1):
            result = await router.list_terminals_settings(
                org_id=None,
                search=None,
                device_filter=None,
                sort_by="device_id",
                sort_order="asc",
                page=page,
                page_size=1,
                fetch_all=False,
                user={"org_id": 1000, "role_id": 5},
                db=db,
                response=None,
                l4desk=True,
                status_filter=status_filter,
            )
            assert result.total_count == len(expected)
            assert [item.device_id for item in result.items] == (
                [] if identifier is None else [identifier]
            )
            assert result.presence_unavailable == (provider == "failed")
            if provider == "partial" and identifier == 4623:
                assert result.items[0].presence == "unknown"
