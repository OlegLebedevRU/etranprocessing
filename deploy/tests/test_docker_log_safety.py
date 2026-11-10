"""Logging configuration must preserve interpolation and reject conflicting policies."""

import importlib.util
from pathlib import Path

import pytest

SOURCE = Path(__file__).resolve().parents[1] / "configure_docker_logs.py"
spec = importlib.util.spec_from_file_location("docker_log_config", SOURCE)
assert spec is not None and spec.loader is not None
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def test_rotation_preserves_source_and_is_idempotent():
    source = (
        "name: production\n\nservices:\n"
        "  broker:\n    environment:\n      TOKEN: ${TOKEN:?required}\n"
        "  api:\n    command: ['echo', '$$literal']\n"
        "\nvolumes:\n  data:\n"
    )
    updated = installer.with_rotation(source)
    assert updated.count("    logging: *etran-logging\n") == 2
    assert (
        updated.removeprefix(installer.HEADER).replace("    logging: *etran-logging\n", "")
        == source
    )
    assert installer.with_rotation(updated) == updated


@pytest.mark.parametrize(
    "source",
    [
        "services: {}\n",
        "services:\n  api:\n    logging:\n      driver: local\n",
        "x-etran-logging: {}\nservices:\n  api:\n    image: fixture\n",
    ],
)
def test_conflicting_or_unsupported_source_is_rejected(source):
    with pytest.raises(ValueError):
        installer.with_rotation(source)
