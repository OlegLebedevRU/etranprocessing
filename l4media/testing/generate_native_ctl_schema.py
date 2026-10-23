# /// script
# requires-python = ">=3.14"
# dependencies = ["pydantic==2.13.4"]
# ///
"""Generate test-only validators from accepted pure IoT CTLv1 model definitions."""

from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
import sys
from pathlib import Path


def generate(root: Path) -> dict:
    source = root / "app-service/core/remote_input/schemas.py"
    spec = importlib.util.spec_from_file_location("native_ctl_reference", source)
    if spec is None or spec.loader is None:
        raise ValueError("native_ctl_reference_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    # This standalone model has no settings/network dependency. Compile only its
    # class node; never import the application or execute its service wiring.
    producer = root / "app-service/core/camera/producer.py"
    declaration = next(
        node
        for node in ast.parse(producer.read_text()).body
        if isinstance(node, ast.ClassDef) and node.name == "CameraRenewCommand"
    )
    exec(compile(ast.Module(body=[declaration], type_ignores=[]), str(producer), "exec"), module.__dict__)
    types = {
        "inventory_get": module.InventoryGetCommand,
        "stream_start": module.StreamStartCommand,
        "stream_stop": module.StreamStopCommand,
        "lease_renew": module.CameraRenewCommand,
        "ack": module.CtlAck,
        "nack": module.CtlNack,
        "presence": module.CtlPresence,
        "stream_event": module.CtlStreamEvent,
    }
    definitions = {}
    for name, model in types.items():
        schema = model.model_json_schema()
        for key, value in schema.pop("$defs", {}).items():
            assert key not in definitions or definitions[key] == value
            definitions[key] = value
        definitions[name] = schema
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "description": "Acceptance implementation mirror, not a new wire contract; CTLv1 remains unchanged.",
        "x-reference-sha256": {
            "schemas.py": hashlib.sha256(source.read_bytes()).hexdigest(),
            "CameraRenewCommand": hashlib.sha256(ast.dump(declaration).encode()).hexdigest(),
        },
        "$defs": definitions,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iot-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    output = Path(__file__).with_name("native_ctl_v1.schema.json")
    data = (json.dumps(generate(args.iot_root), indent=2, sort_keys=True) + "\n").encode()
    if args.check:
        assert output.read_bytes() == data, "Native CTLv1 model mirror changed"
    else:
        output.write_bytes(data)
    print("Native CTLv1 reference model mirror verified")
