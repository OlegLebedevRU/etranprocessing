from __future__ import annotations

import ast
import importlib
import json
import subprocess
import sys
from pathlib import Path

import pytest
from schema_contract import describe_metadata
from sqlalchemy import BigInteger, DateTime, Integer
from sqlalchemy.orm import configure_mappers

from etranprocessing_db import Base

HERE = Path(__file__).parent
BASELINE = json.loads(
    (HERE / "fixtures" / "baseline_schema_v010.json").read_text(encoding="utf-8")
)


def test_root_import_keeps_legacy_metadata():
    script = "import etranprocessing_db as db; import json; print(json.dumps(sorted(db.Base.metadata.tables)))"
    result = subprocess.run(
        [sys.executable, "-c", script], check=True, capture_output=True, text=True
    )
    assert json.loads(result.stdout) == sorted(BASELINE)


def test_expand_does_not_modify_existing_tables():
    importlib.import_module("etranprocessing_db.l4desk")
    configure_mappers()
    current = describe_metadata(Base.metadata)
    for name, expected in BASELINE.items():
        assert current[name] == expected


def test_all_new_models_have_keys_and_named_financial_metadata():
    module = importlib.import_module("etranprocessing_db.l4desk")
    tables = [
        table for name, table in Base.metadata.tables.items() if name not in BASELINE
    ]
    assert len(tables) >= 22
    for table in tables:
        assert table.primary_key.columns
        for column in table.columns:
            if isinstance(column.type, DateTime):
                assert column.type.timezone
            if column.name.endswith("_kopecks"):
                assert isinstance(column.type, (Integer, BigInteger))
            for foreign_key in column.foreign_keys:
                assert foreign_key.column is not None
        if table.name.startswith("fin_"):
            for item in [*table.constraints, *table.indexes]:
                assert item.name is not None and item.name.startswith("fin_")
                assert len(item.name) <= 63
    assert len(module.__all__) == len(tables)


def test_iot_baseline_contract():
    module = importlib.import_module("etranprocessing_db.l4desk")
    inbox = module.IotEventInbox.__table__
    assert list(inbox.primary_key.columns.keys()) == ["event_id"]
    assert isinstance(inbox.c.cursor.type, BigInteger)
    assert not any(index.unique for index in inbox.indexes)
    for name in (
        "event_id",
        "terminal_id",
        "session_id",
        "operation_id",
        "correlation_id",
    ):
        assert inbox.c[name].type.length == 128
    assert str(inbox.c.payload.type) == "JSON"
    assert module.IotEventQuarantine.__table__.primary_key.columns.keys() == ["id"]
    for cls in (
        module.IotEventInbox,
        module.IotEventQuarantine,
        module.IotConsumerCheckpoint,
    ):
        assert all(column.server_default is None for column in cls.__table__.columns)
    assert inbox.c.status.default.arg == "processed"
    assert module.IotConsumerCheckpoint.__table__.c.last_cursor.default.arg == 0


def test_model_modules_are_declarative_only():
    package = Path(__file__).parents[1] / "etranprocessing_db"
    for name in ("l4desk.py", "models/l4desk.py", "models/finance.py", "models/iot.py"):
        tree = ast.parse((package / name).read_text(encoding="utf-8"))
        assert not any(
            isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            for node in ast.walk(tree)
        )
        imports = [
            node
            for node in ast.walk(tree)
            if isinstance(node, (ast.Import, ast.ImportFrom))
        ]
        for node in imports:
            modules = (
                [alias.name for alias in node.names]
                if isinstance(node, ast.Import)
                else [node.module or ""]
            )
            assert all(
                item.split(".")[0]
                in {
                    "__future__",
                    "datetime",
                    "typing",
                    "sqlalchemy",
                    "etranprocessing_db",
                }
                for item in modules
            )


def test_published_schema_matches_models():
    importlib.import_module("etranprocessing_db.l4desk")
    path = HERE.parent / "docs" / "l4desk" / "schema-v1.json"
    assert describe_metadata(Base.metadata) == json.loads(
        path.read_text(encoding="utf-8")
    )


@pytest.mark.parametrize(
    "name",
    ["fin_usage_daily", "fin_terminal_monthly_charges", "fin_ledger_transactions"],
)
def test_financial_facts_do_not_depend_on_hot_events(name):
    importlib.import_module("etranprocessing_db.l4desk")
    table = Base.metadata.tables[name]
    assert {
        "source_project",
        "source_event_id",
        "source_events_hash",
        "archive_batch_id",
    } <= set(table.columns.keys())
    assert all(not fk.target_fullname.startswith("iot_") for fk in table.foreign_keys)
