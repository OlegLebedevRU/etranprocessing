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
    catalog_plan = commands.add_parser("catalog-plan")
    catalog_plan.add_argument("--evidence", type=Path, action="append", required=True)
    catalog_plan.add_argument("--env-file", type=Path)
    catalog_publish = commands.add_parser("catalog-publish")
    catalog_publish.add_argument("--plan", type=Path, required=True)
    catalog_publish.add_argument("--env-file", type=Path)
    promotion_plan = commands.add_parser("promotion-plan")
    promotion_plan.add_argument("--evidence", type=Path, action="append", required=True)
    promotion_plan.add_argument("--env-file", type=Path)
    acceptance_plan = commands.add_parser("acceptance-plan")
    acceptance_plan.add_argument("--source", required=True)
    acceptance_plan.add_argument("--target", required=True)
    acceptance_plan.add_argument("--arch", choices=("x86", "x64"), default="x86")
    acceptance_plan.add_argument("--env-file", type=Path)
    acceptance_run = commands.add_parser("acceptance-run")
    acceptance_run.add_argument("--plan", type=Path, required=True)
    acceptance_run.add_argument("--env-file", type=Path)
    for command in ("plan", "prepare", "release", "verify"):
        child = commands.add_parser(command)
        child.add_argument("--version", required=True)
        child.add_argument("--env-file", type=Path)
        if command == "release":
            child.add_argument("--no-publish", action="store_true")
            child.add_argument("--with-terminal-gate", action="store_true")
            child.add_argument("--from-version")
    kit = commands.add_parser("install-kit")
    kit.add_argument("--version", required=True)
    kit.add_argument("--env-file", type=Path, required=True)
    kit.add_argument("--output", type=Path, required=True)
    seal = commands.add_parser("bootstrap-seal")
    seal.add_argument("--version", required=True)
    seal.add_argument("--env-file", type=Path, required=True)
    seal.add_argument("--output", type=Path, required=True)
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
        env = load_env(
            env_path,
            required=args.command
            in (
                "release",
                "keys",
                "verify",
                "install-kit",
                "bootstrap-seal",
                "catalog-plan",
                "catalog-publish",
                "promotion-plan",
                "acceptance-plan",
                "acceptance-run",
            ),
        )
        if args.command in ("acceptance-plan", "acceptance-run"):
            from .acceptance_pipeline import AcceptancePipeline, _elevated

            if args.command == "acceptance-run" and not _elevated():
                from .acceptance_windows import elevate

                return elevate(root, sys.argv[1:] if argv is None else argv)
            acceptance = AcceptancePipeline(root, env, env_path)
            result = (
                acceptance.plan(args.source, args.target, args.arch)
                if args.command == "acceptance-plan"
                else acceptance.run(args.plan)
            )
            print(json.dumps(result, indent=2))
        elif args.command in ("catalog-plan", "catalog-publish", "promotion-plan"):
            from .catalog_publisher import CatalogPublisher

            publisher = CatalogPublisher(root, env, env_path)
            result = (
                publisher.plan(args.evidence, promote=args.command == "promotion-plan")
                if args.command != "catalog-publish"
                else publisher.publish(args.plan)
            )
            print(json.dumps(result, indent=2))
        elif args.command == "keys":
            key_id = init_key(root, env, env_path)
            print(f"Metadata key created and self-tested. key_id={key_id}")
        else:
            pipeline = Pipeline(root, args.version, env, env_path)
            if args.command == "bootstrap-seal":
                from .bootstrap import seal_bootstrap
                from .metadata import load_signing_key
                from .pipeline import git, input_snapshot, publisher_module
                from .runner import workspace_lock

                output = args.output.resolve()
                if output.is_relative_to(root):
                    raise ReleaseError("Immutable bootstrap seal must be outside the repository")
                pfx = Path(pipeline.env.get("SW_SIGN_PFX", ""))
                if not pfx.is_file() or pfx.resolve().is_relative_to(root):
                    raise ReleaseError("SW_SIGN_PFX must locate an external signing certificate")
                with workspace_lock(pipeline.dist / ".release" / "workspace.lock"):
                    if git(root, "status", "--porcelain", "--", *pipeline.config["source_paths"]):
                        raise ReleaseError(
                            "Bootstrap sealing requires a clean reviewed source checkpoint"
                        )
                    before = input_snapshot(root, pipeline.config)
                    key = load_signing_key(root, env, env_path)
                    manifest, _, _ = publisher_module(root).verify_artifacts(
                        pipeline.dist, trusted_public=key.public_key(), require_metadata=True
                    )
                    if manifest["version"] != args.version:
                        raise ReleaseError("Bootstrap reference release version differs")

                    def checkpoint() -> None:
                        if before != input_snapshot(root, pipeline.config):
                            raise ReleaseError("Bootstrap source checkpoint changed while sealing")

                    seal_bootstrap(
                        root,
                        output,
                        key,
                        manifest["publisher_certificate_sha256"],
                        pipeline.runner,
                        checkpoint,
                    )
                print(f"Initial immutable signed bootstrap seal: {output}")
            elif args.command == "install-kit":
                from .install_kit import create_install_kit

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
                result = create_install_kit(
                    root, pipeline.dist, args.version, args.output.resolve(), env, env_path
                )
                print(f"Verified offline fresh installation kit: {result}")
            elif args.command == "plan":
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
                from .metadata import load_public_key

                publisher_module(root).verify_artifacts(
                    pipeline.dist,
                    trusted_public=load_public_key(env, env_path),
                    require_metadata=True,
                )
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
