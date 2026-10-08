"""Private trial reservations model owner-approved plans, never actual stand runs."""

import pytest

from l4release import acceptance_trial as trial
from l4release.catalog import catalog_bytes
from l4release.catalog_evidence import canonical
from l4release.catalog_registry import digest
from l4release.common import ReleaseError
from l4release.metadata import key_id, sign_bytes
from l4release.tests.test_catalog_publisher import NOW  # pytest fixtures
from l4release.tests.test_catalog_publisher import key as key
from l4release.tests.test_catalog_publisher import publisher as publisher


def reserve(instance, plan, key, revision=5):
    document = {**plan["catalog"], "revision": revision}
    data, signature = catalog_bytes(document, key, now=NOW)
    reviewed = {
        "schema": 1,
        "kind": "l4tools-local-acceptance",
        "key_id": key_id(key.public_key()),
        "catalog": document,
    }
    reviewed_bytes = canonical(reviewed)
    directory = instance.state / f"trial-{revision}"
    directory.mkdir(parents=True)
    for name, value in (
        ("plan.json", reviewed_bytes),
        ("plan.json.sig", sign_bytes(reviewed_bytes, key)),
        ("catalog.json", data),
        ("catalog.json.sig", signature),
    ):
        (directory / name).write_bytes(value)
    (instance.state / "trial-reservation.json").write_bytes(
        canonical(
            {
                "schema": 1,
                "key_id": key_id(key.public_key()),
                "revision": revision,
                "catalog_sha256": digest(data),
                "plan_sha256": digest(reviewed_bytes),
            }
        )
    )
    return directory


def test_private_floor_never_claims_publication(publisher, key, monkeypatch, tmp_path):
    instance, report = publisher
    monkeypatch.setattr(
        trial, "protect_state", lambda path: None
    )  # Explicit modeled ACL authority.
    plan = instance.plan([report], now=NOW)
    reserve(instance, plan, key)
    assert instance._floor()["revision"] == 0
    proposed = instance.plan([report], now=NOW)
    assert proposed["base_revision"] == 0 and proposed["catalog"]["revision"] == 6
    path = tmp_path / "reviewed.json"
    path.write_bytes(canonical(proposed))
    assert instance.publish(path, now=NOW)["revision"] == 6
    assert instance._floor()["revision"] == 6


@pytest.mark.parametrize("fault", ["plan", "catalog", "signature", "revision", "schema", "key"])
def test_trial_reservation_refuses_drift(publisher, key, monkeypatch, fault):
    instance, report = publisher
    monkeypatch.setattr(trial, "protect_state", lambda path: None)
    directory = reserve(instance, instance.plan([report], now=NOW), key)
    if fault == "plan":
        (directory / "plan.json").write_bytes(b"caller PASS")
    elif fault == "catalog":
        (directory / "catalog.json").write_bytes(b"changed")
    elif fault == "signature":
        (directory / "catalog.json.sig").write_bytes(b"x" * 384)
    else:
        import json

        path = instance.state / "trial-reservation.json"
        value = json.loads(path.read_bytes())
        if fault == "revision":
            value["revision"] = 4
        elif fault == "schema":
            value["schema"] = True
        elif fault == "key":
            value["key_id"] = "1" * 64
        path.write_bytes(canonical(value))
    with pytest.raises((ReleaseError, OSError)):
        instance.plan([report], now=NOW)
