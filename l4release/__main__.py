"""Run from the repository root: uv run python -m l4release ..."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .common import Redactor, ReleaseError, load_env
from .dependencies import import_openh264
from .keys import init_key
from .pipeline import Pipeline


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="L4 Tools release control; no terminal changes by default"
    )
    commands = parser.add_subparsers(dest="command", required=True)
    assets = commands.add_parser("import-openh264")
    assets.add_argument("--source", type=Path, required=True)
    for command in ("plan", "prepare", "release", "verify"):
        child = commands.add_parser(command)
        child.add_argument("--version", required=True)
        child.add_argument("--env-file", type=Path)
        if command == "release":
            child.add_argument("--no-publish", action="store_true")
            child.add_argument("--with-terminal-gate", action="store_true")
            child.add_argument("--from-version")
    keys = commands.add_parser("keys").add_subparsers(dest="key_command", required=True)
    keys.add_parser("init").add_argument("--env-file", type=Path)
    promote = commands.add_parser("promote")
    promote.add_argument("--version", required=True)
    promote.add_argument("--channel", choices=["stable"], default="stable")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    env_path = (getattr(args, "env_file", None) or root / "sw_sign.env").resolve()
    env: dict[str, str] = {}
    try:
        if args.command == "import-openh264":
            import_openh264(root, args.source.resolve())
            print("Locked OpenH264 headers and x86/x64 libraries imported and verified")
            return 0
        if (
            args.command == "promote"
            or getattr(args, "with_terminal_gate", False)
            or getattr(args, "from_version", None)
        ):
            raise ReleaseError(
                "Terminal gate/admission/promotion belong to implementation stage 5; "
                "not implemented and no operation was started"
            )
        env = load_env(env_path, required=args.command in ("release", "keys", "verify"))
        if args.command == "keys":
            key_id = init_key(root, env, env_path)
            print(f"Metadata key created and self-tested. key_id={key_id}")
        else:
            pipeline = Pipeline(root, args.version, env, env_path)
            if args.command == "plan":
                print(json.dumps(pipeline.plan(), indent=2))
            elif args.command == "verify":
                # Read-only verifier: no rebuilding, signing or publication.
                from .pipeline import publisher_module

                pipeline.runner.run(
                    "verify-signed",
                    "powershell",
                    root,
                    [
                        str(root / "tools/release/Verify-SignedRelease.ps1"),
                        "-Version",
                        args.version,
                        "-PfxPath",
                        pipeline.env.get("SW_SIGN_PFX", ""),
                    ],
                    300,
                )
                publisher_module(root).verify_artifacts(pipeline.dist)
                print("Signed artifacts verified")
            else:
                signed = args.command == "release"
                pipeline.execute(signed=signed, publish=signed and not args.no_publish)
                print(f"Result: {pipeline.report['status']}; report: {pipeline.report_path}")
        return 0
    except (ReleaseError, OSError, ValueError) as error:
        print(Redactor(env)(f"Release stopped: {error}"), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
