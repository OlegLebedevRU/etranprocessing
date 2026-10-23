"""Release scripts must parse on the Ubuntu system Python, independently of dev Python."""

import ast
from pathlib import Path

import pytest


@pytest.mark.parametrize("script", ["activate_camera_reader.py", "scan-image.py"])
def test_release_script_supports_python_312(script):
    source = Path(__file__).resolve().parents[1] / script
    ast.parse(source.read_text(encoding="utf-8"), filename=str(source), feature_version=(3, 12))
