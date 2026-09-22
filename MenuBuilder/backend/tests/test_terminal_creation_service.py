"""Canonical terminal identity use case tests (L4D-13-MB-FIX-01)."""

from __future__ import annotations

import pytest

from app.services.terminal_creation_service import (
    DEVICE_ID_MAX,
    DEVICE_ID_MIN,
    generate_device_sn,
    validate_new_device_id,
)


def test_generate_device_sn_matches_canonical_formula() -> None:
    sn = generate_device_sn(1000001)
    assert sn.startswith("a4b1000001c")
    assert sn[3:10] == "1000001"
    assert sn[10] == "c"
    assert sn[16] == "d"
    assert len(sn) == 23


def test_generate_device_sn_zero_pads_device_id() -> None:
    sn = generate_device_sn(1000005)
    assert sn.startswith("a4b1000005c")


def test_validate_new_device_id_accepts_range_bounds() -> None:
    validate_new_device_id(DEVICE_ID_MIN)
    validate_new_device_id(DEVICE_ID_MAX)


def test_validate_new_device_id_rejects_out_of_range() -> None:
    with pytest.raises(Exception) as exc:
        validate_new_device_id(10000005)
    assert "1000001" in str(exc.value.detail)

    with pytest.raises(Exception) as exc2:
        validate_new_device_id(1)
    assert "1000001" in str(exc2.value.detail)

    with pytest.raises(Exception) as exc3:
        validate_new_device_id(2000000)
    assert "1999999" in str(exc3.value.detail)


def test_user_supplied_sn_is_not_part_of_request_model() -> None:
    from app.services.terminal_onboarding_service import TerminalOnboardRequest

    req = TerminalOnboardRequest.model_validate(
        {"sn": "SN-USER-HACK", "name": "POS", "device_id": 1}
    )
    assert not hasattr(req, "sn") or getattr(req, "sn", None) is None
    assert req.name == "POS"
    # extra fields ignored: device_id must not be accepted as a request field
    assert not hasattr(req, "device_id") or getattr(req, "device_id", None) is None
