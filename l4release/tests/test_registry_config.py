from pathlib import Path

import pytest

from l4release.common import ReleaseError
from l4release.pipeline import read_config


def test_registry_producer_matches_native_consumer():
    root = Path(__file__).resolve().parents[2]
    assert read_config(root)["registry"] == "https://l4tools-generic.ar.cloud.ru"


def test_mismatched_registry_refuses_before_release(tmp_path):
    root = Path(__file__).resolve().parents[2]
    for name in ("l4release/config.toml", "tools/l4common/registry_target.h"):
        destination = tmp_path / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((root / name).read_bytes())
    config = tmp_path / "l4release/config.toml"
    config.write_text(
        config.read_text().replace(
            "https://l4tools-generic.ar.cloud.ru", "https://registry.example"
        ),
        encoding="utf-8",
    )
    with pytest.raises(ReleaseError, match="Registry authority differs"):
        read_config(tmp_path)
