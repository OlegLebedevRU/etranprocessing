from sqlalchemy import create_engine, func, select, text

from app.models import Terminal
from app.routers.settings import _visible_terminals_query


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
