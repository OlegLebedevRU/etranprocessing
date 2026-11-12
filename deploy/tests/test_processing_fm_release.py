"""Policy preservation checks for the scoped PB release helper."""

import copy
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from processing_fm_release import verify_runtime


def runtime():
    return {
        "Image": "old",
        "Config": {"Env": ["MODE=fixture"]},
        "Mounts": [{"Type": "bind", "Source": "/fixture", "Destination": "/app/log", "RW": True}],
        "HostConfig": {"NetworkMode": "fixture-network"},
    }


def test_only_image_may_change():
    before = runtime()
    after = copy.deepcopy(before)
    after["Image"] = "new"
    verify_runtime(before, after, "new")


@pytest.mark.parametrize("field", ["image", "environment", "mounts", "network"])
def test_policy_drift_refuses_release(field):
    before = runtime()
    after = copy.deepcopy(before)
    after["Image"] = "new"
    if field == "image":
        after["Image"] = "wrong"
    elif field == "environment":
        after["Config"]["Env"] = []
    elif field == "mounts":
        after["Mounts"] = []
    else:
        after["HostConfig"]["NetworkMode"] = "wrong"
    with pytest.raises(RuntimeError):
        verify_runtime(before, after, "new")
