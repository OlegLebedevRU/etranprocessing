"""Generate reviewable metadata artifacts; not part of the installed package."""

import argparse
import json
from pathlib import Path

from schema_contract import describe_metadata

from etranprocessing_db import Base

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--expanded", action="store_true")
args = parser.parse_args()
if args.expanded:
    import etranprocessing_db.l4desk  # noqa: F401

args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(
    json.dumps(describe_metadata(Base.metadata), ensure_ascii=False, indent=2) + "\n",
    encoding="utf-8",
    newline="\n",
)
