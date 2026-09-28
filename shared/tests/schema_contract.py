from __future__ import annotations

from typing import Any

from sqlalchemy import ColumnDefault, DefaultClause, MetaData
from sqlalchemy.dialects import postgresql
from sqlalchemy.schema import CreateIndex, CreateTable


def describe_metadata(metadata: MetaData) -> dict[str, Any]:
    dialect = postgresql.dialect()
    result = {}
    for table in sorted(metadata.tables.values(), key=lambda item: item.name):
        result[table.name] = {
            "columns": [
                {
                    "name": column.name,
                    "type": column.type.compile(dialect=dialect),
                    "nullable": column.nullable,
                    "primary_key": column.primary_key,
                    "server_default": str(column.server_default.arg)
                    if isinstance(column.server_default, DefaultClause)
                    else None,
                    "client_default": (
                        getattr(column.default.arg, "__name__", str(column.default.arg))
                        if isinstance(column.default, ColumnDefault)
                        else None
                    ),
                    "onupdate": (
                        getattr(
                            column.onupdate.arg, "__name__", str(column.onupdate.arg)
                        )
                        if isinstance(column.onupdate, ColumnDefault)
                        else None
                    ),
                }
                for column in table.columns
            ],
            "ddl": str(CreateTable(table).compile(dialect=dialect)).strip(),
            "indexes": sorted(
                str(CreateIndex(index).compile(dialect=dialect))
                for index in table.indexes
            ),
        }
    return result
