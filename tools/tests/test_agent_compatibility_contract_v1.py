"""
test_agent_compatibility_contract_v1.py
Executable verification test suite for L4Desk Agent Compatibility Contract v1.
Validates schemas, golden vectors, baseline fixtures, and normative invariants.
"""

from __future__ import annotations

import json
from pathlib import Path
import struct
import pytest
import jsonschema

TOOLS_ROOT = Path(__file__).resolve().parent.parent
DOCS_L4DESK = TOOLS_ROOT / "docs" / "l4desk"
CONTRACTS_DIR = DOCS_L4DESK / "contracts"
SCHEMAS_DIR = CONTRACTS_DIR / "schemas"
FIXTURES_DIR = DOCS_L4DESK / "fixtures"


@pytest.fixture(scope="session")
def contract_spec():
    spec_path = CONTRACTS_DIR / "agent_compatibility_contract_v1.json"
    assert spec_path.exists(), f"Contract spec not found: {spec_path}"
    with open(spec_path, "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def golden_vectors():
    vec_path = FIXTURES_DIR / "golden_vectors_v1.json"
    assert vec_path.exists(), f"Golden vectors not found: {vec_path}"
    with open(vec_path, "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def schema_meta():
    with open(SCHEMAS_DIR / "agent_contract_v1.schema.json", "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def schema_presence():
    with open(SCHEMAS_DIR / "presence_event.schema.json", "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def schema_7000():
    with open(SCHEMAS_DIR / "rpc_7000_stream_control.schema.json", "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def schema_7001():
    with open(SCHEMAS_DIR / "rpc_7001_exec.schema.json", "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def schema_7002():
    with open(SCHEMAS_DIR / "rpc_7002_cancel.schema.json", "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def schema_l4rtp():
    with open(SCHEMAS_DIR / "l4rtp_wire_protocol.schema.json", "r", encoding="utf-8") as f:
        return json.load(f)


# ==============================================================================
# 1. Contract Specification & Meta-Schema Tests
# ==============================================================================


def test_contract_spec_meta_schema(contract_spec, schema_meta):
    """Validate that the contract spec adheres to the contract meta-schema."""
    jsonschema.validate(instance=contract_spec, schema=schema_meta)
    assert contract_spec["contract_version"] == "1.0.0"
    assert contract_spec["contract_status"] == "IMMUTABLE_SPECIFICATION"
    assert "1.7.6" in contract_spec["published_agent_versions"]
    assert "1.7.7" in contract_spec["published_agent_versions"]


def test_contract_spec_methods_and_topics(contract_spec):
    """Validate presence and RPC topic definitions and method codes."""
    assert "main_app" in contract_spec["presence_topology"]
    assert "extra_service" in contract_spec["presence_topology"]

    assert contract_spec["presence_topology"]["main_app"]["topic_pattern"] == "dev/{SN}/app"
    assert contract_spec["presence_topology"]["extra_service"]["topic_pattern"] == "dev/{SN}/svc"

    assert "7000" in contract_spec["methods"]
    assert "7001" in contract_spec["methods"]
    assert "7002" in contract_spec["methods"]


# ==============================================================================
# 2. Presence Lifecycle & Schema Tests
# ==============================================================================


def test_presence_golden_vectors(golden_vectors, schema_presence):
    """Validate all presence golden vectors against presence schema."""
    vectors = golden_vectors.get("presence_vectors", [])
    assert len(vectors) == 6

    for vec in vectors:
        jsonschema.validate(instance=vec["payload"], schema=schema_presence)
        assert vec["qos"] == 1
        assert vec["retain"] is True
        if vec["client_type"] == "main_app":
            assert vec["topic"].endswith("/app")
            assert vec["payload"] in ["app_online", "app_offline"]
        elif vec["client_type"] == "extra_service":
            assert vec["topic"].endswith("/svc")
            assert vec["payload"] in ["svc_online", "svc_offline"]


def test_presence_baseline_fixture_backward_compat(schema_presence):
    """Validate published 1.7.7 baseline presence fixture against schema."""
    fixture_path = FIXTURES_DIR / "mqtt_presence_lifecycle.json"
    with open(fixture_path, "r", encoding="utf-8") as f:
        fixture = json.load(f)

    for svc_key in ["extra_service", "main_app"]:
        svc_data = fixture["roles"][svc_key]
        for msg_key in ["connect_will", "connack_publish", "clean_shutdown_publish"]:
            msg = svc_data[msg_key]
            assert msg["qos"] == 1
            assert msg["retain"] is True
            jsonschema.validate(instance=msg["payload"], schema=schema_presence)


# ==============================================================================
# 3. Method 7000 (Stream Control) Tests
# ==============================================================================


def test_method_7000_golden_vectors(golden_vectors, schema_7000):
    """Validate Method 7000 golden vectors against request and response schemas."""
    req_schema = schema_7000["definitions"]["request"]
    resp_schema = schema_7000["definitions"]["response"]

    vectors = golden_vectors.get("method_7000_vectors", [])
    assert len(vectors) >= 9

    for vec in vectors:
        req_payload = vec["request"]["payload"]
        resp_payload = vec["expected_response"]["payload"]

        # Validate request
        jsonschema.validate(instance=req_payload, schema=req_schema)
        assert req_payload["method_code"] == 7000
        assert vec["request"]["topic"].startswith("srv/")

        # Validate response
        jsonschema.validate(instance=resp_payload, schema=resp_schema)
        assert vec["expected_response"]["topic"].startswith("dev/")
        assert resp_payload["command_id"] == req_payload["command_id"]


def test_method_7000_baseline_fixture_backward_compat(schema_7000):
    """Validate published baseline rpc_7000_stream_control.json against schema."""
    req_schema = schema_7000["definitions"]["request"]
    resp_schema = schema_7000["definitions"]["response"]

    fixture_path = FIXTURES_DIR / "rpc_7000_stream_control.json"
    with open(fixture_path, "r", encoding="utf-8") as f:
        fixture = json.load(f)

    for name, ex in fixture["examples"].items():
        req_payload = ex["request"]["payload"]
        resp_payload = ex["response"]["payload"]

        jsonschema.validate(instance=req_payload, schema=req_schema)
        jsonschema.validate(instance=resp_payload, schema=resp_schema)


# ==============================================================================
# 4. Method 7001 (Command Execution) Tests
# ==============================================================================


def test_method_7001_golden_vectors(golden_vectors, schema_7001):
    """Validate Method 7001 golden vectors (req, stream chunks, final resp)."""
    req_schema = schema_7001["definitions"]["request"]
    chunk_schema = schema_7001["definitions"]["stream_chunk"]
    resp_schema = schema_7001["definitions"]["response"]

    vectors = golden_vectors.get("method_7001_vectors", [])
    assert len(vectors) >= 4

    for vec in vectors:
        req_payload = vec["request"]["payload"]
        jsonschema.validate(instance=req_payload, schema=req_schema)
        assert req_payload["method_code"] == 7001
        assert req_payload["shell"] in ["cmd", "powershell"]

        # Validate chunks sequence
        chunks = vec["stream_chunks"]
        assert len(chunks) >= 1
        expected_seq = 1
        found_eof = False

        for chunk in chunks:
            chunk_payload = chunk["payload"]
            jsonschema.validate(instance=chunk_payload, schema=chunk_schema)
            assert chunk_payload["session_id"] == req_payload["session_id"]
            assert chunk_payload["seq"] == expected_seq
            expected_seq += 1
            if chunk_payload["eof"]:
                found_eof = True

        assert found_eof, f"Vector {vec['id']} missing EOF chunk"

        # Validate final response
        resp_payload = vec["final_response"]["payload"]
        jsonschema.validate(instance=resp_payload, schema=resp_schema)
        assert resp_payload["id"] == req_payload["id"]
        assert resp_payload["session_id"] == req_payload["session_id"]
        assert resp_payload["method_code"] == 7001


def test_method_7001_baseline_fixture_backward_compat(schema_7001):
    """Validate published baseline rpc_7001_exec.json against schema."""
    req_schema = schema_7001["definitions"]["request"]
    chunk_schema = schema_7001["definitions"]["stream_chunk"]
    resp_schema = schema_7001["definitions"]["response"]

    fixture_path = FIXTURES_DIR / "rpc_7001_exec.json"
    with open(fixture_path, "r", encoding="utf-8") as f:
        fixture = json.load(f)

    for name, ex in fixture["examples"].items():
        req_payload = ex["request"]["payload"]
        chunk1 = ex["stream_output_chunk"]["payload"]
        chunk_eof = ex["stream_output_eof"]["payload"]
        resp_payload = ex["response"]["payload"]

        jsonschema.validate(instance=req_payload, schema=req_schema)
        jsonschema.validate(instance=chunk1, schema=chunk_schema)
        jsonschema.validate(instance=chunk_eof, schema=chunk_schema)
        jsonschema.validate(instance=resp_payload, schema=resp_schema)


# ==============================================================================
# 5. Method 7002 (Task Cancellation) Tests
# ==============================================================================


def test_method_7002_golden_vectors(golden_vectors, schema_7002):
    """Validate Method 7002 golden vectors (cancel active and not found)."""
    req_schema = schema_7002["definitions"]["request"]
    resp_schema = schema_7002["definitions"]["response"]

    vectors = golden_vectors.get("method_7002_vectors", [])
    assert len(vectors) >= 2

    for vec in vectors:
        req_payload = vec["request"]["payload"]
        resp_payload = vec["expected_response"]["payload"]

        jsonschema.validate(instance=req_payload, schema=req_schema)
        assert req_payload["method_code"] == 7002

        jsonschema.validate(instance=resp_payload, schema=resp_schema)
        assert resp_payload["target_task_id"] == req_payload["target_task_id"]
        assert resp_payload["status"] in ["cancelled", "not_found", "already_finished"]


def test_method_7002_baseline_fixture_backward_compat(schema_7002):
    """Validate published baseline rpc_7002_cancel.json against schema."""
    req_schema = schema_7002["definitions"]["request"]
    resp_schema = schema_7002["definitions"]["response"]

    fixture_path = FIXTURES_DIR / "rpc_7002_cancel.json"
    with open(fixture_path, "r", encoding="utf-8") as f:
        fixture = json.load(f)

    for name, ex in fixture["examples"].items():
        req_payload = ex["request"]["payload"]
        resp_payload = ex["response"]["payload"]

        jsonschema.validate(instance=req_payload, schema=req_schema)
        jsonschema.validate(instance=resp_payload, schema=resp_schema)


# ==============================================================================
# 6. L4RTP Wire Protocol Tests
# ==============================================================================


def test_l4rtp_preamble_wire_parsing(golden_vectors):
    """Parse and verify L4RTP/1 preamble wire hex bytes."""
    vectors = golden_vectors.get("l4rtp_wire_vectors", [])
    preamble_vec = next(v for v in vectors if v["id"] == "vec-rtp-preamble")

    wire_bytes = bytes.fromhex(preamble_vec["wire_hex"])
    # Magic[4] + Version[1] + Reserved[1] + SN_Len[2] = 8 bytes header
    assert len(wire_bytes) >= 8
    magic, version, reserved, sn_len = struct.unpack("!4sBBH", wire_bytes[:8])

    assert magic == b"L4RT"
    assert version == 1
    assert reserved == 0
    assert sn_len == 9

    sn = wire_bytes[8 : 8 + sn_len].decode("ascii")
    assert sn == preamble_vec["expected_parsed"]["sn"]


def test_l4rtp_frame_headers(golden_vectors):
    """Parse and verify L4RTP/1 frame headers (channel and length)."""
    vectors = golden_vectors.get("l4rtp_wire_vectors", [])
    rtp_vec = next(v for v in vectors if v["id"] == "vec-rtp-video-frame")
    rtcp_vec = next(v for v in vectors if v["id"] == "vec-rtp-rtcp-frame")

    # RTP frame header: Channel 1, Reserved 0, Length 1400 (0x0578)
    rtp_hdr = bytes.fromhex(rtp_vec["header_hex"])
    channel, reserved, length = struct.unpack("!BBH", rtp_hdr)
    assert channel == 1
    assert reserved == 0
    assert length == 1400

    # RTCP frame header: Channel 2, Reserved 0, Length 72 (0x0048)
    rtcp_hdr = bytes.fromhex(rtcp_vec["header_hex"])
    channel, reserved, length = struct.unpack("!BBH", rtcp_hdr)
    assert channel == 2
    assert reserved == 0
    assert length == 72


# ==============================================================================
# 7. Normative Invariants & Isolation Auditing
# ==============================================================================


def test_no_financial_fields_in_contract_and_fixtures():
    """Verify that NO financial, billing, or organization fields exist in contract files."""
    forbidden_terms = {
        "balance", "payment", "invoice", "subledger", "tariff",
        "billing", "subscription", "pricing", "entitlement", "amount_rub", "fee"
    }

    files_to_check = [
        CONTRACTS_DIR / "agent_compatibility_contract_v1.json",
        SCHEMAS_DIR / "presence_event.schema.json",
        SCHEMAS_DIR / "rpc_7000_stream_control.schema.json",
        SCHEMAS_DIR / "rpc_7001_exec.schema.json",
        SCHEMAS_DIR / "rpc_7002_cancel.schema.json",
        SCHEMAS_DIR / "l4rtp_wire_protocol.schema.json",
        FIXTURES_DIR / "golden_vectors_v1.json",
    ]

    for file_path in files_to_check:
        assert file_path.exists(), f"File missing: {file_path}"
        content = file_path.read_text(encoding="utf-8").lower()
        for term in forbidden_terms:
            assert f'"{term}"' not in content, (
                f"Forbidden commercial term '{term}' found in {file_path.name}"
            )


def test_all_topics_have_strict_device_or_server_prefix(golden_vectors):
    """Verify all topics strictly follow dev/{SN}/... or srv/{SN}/... patterns."""
    all_topics = []

    for vec in golden_vectors.get("presence_vectors", []):
        all_topics.append(vec["topic"])

    for vec in golden_vectors.get("method_7000_vectors", []):
        all_topics.append(vec["request"]["topic"])
        all_topics.append(vec["expected_response"]["topic"])

    for vec in golden_vectors.get("method_7001_vectors", []):
        all_topics.append(vec["request"]["topic"])
        all_topics.append(vec["final_response"]["topic"])
        for chunk in vec["stream_chunks"]:
            all_topics.append(chunk["topic"])

    for vec in golden_vectors.get("method_7002_vectors", []):
        all_topics.append(vec["request"]["topic"])
        all_topics.append(vec["expected_response"]["topic"])

    for topic in all_topics:
        parts = topic.split("/")
        assert len(parts) >= 3
        assert parts[0] in ["dev", "srv"], f"Invalid topic prefix in: {topic}"
        assert parts[1] == "000100773" or len(parts[1]) > 0
        assert parts[2] in ["app", "svc", "tsk", "rsp", "out", "res"], f"Invalid leaf topic in: {topic}"
