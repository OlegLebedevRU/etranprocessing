import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import deploy
from components import COMPONENTS, select_components
from deploy import compose_command, image_reference, publish_static, update_override


class MatrixTests(unittest.TestCase):
    def selected(self, paths=(), event="push", component="all", bootstrap=""):
        return {
            item["name"]
            for item in select_components(paths, event, component, bootstrap)["include"]
            if item["trigger"]
        }

    def test_independent_components(self):
        for name, spec in COMPONENTS.items():
            with self.subTest(name=name):
                self.assertEqual(
                    self.selected([spec["directory"] + "/Dockerfile"]), {name}
                )

    def test_shared_selects_both_backends(self):
        self.assertEqual(
            self.selected(["shared/etranprocessing_db/models.py"]),
            {"menubuilder-backend", "processingbackend"},
        )

    def test_ci_changes_select_all(self):
        for path in [
            ".github/workflows/build-image.yml",
            ".github/ci/deploy.py",
            ".dockerignore",
        ]:
            self.assertEqual(self.selected([path]), set(COMPONENTS))

    def test_docs_and_unrelated_changes_do_not_deploy(self):
        self.assertEqual(
            self.selected(
                [
                    "docs/ci.md",
                    "MenuBuilder/backend/README.md",
                    "l4media/janus/janus.jcfg",
                ]
            ),
            set(),
        )
        self.assertEqual(self.selected(), set())

    def test_dispatch_and_bootstrap(self):
        self.assertEqual(self.selected(event="workflow_dispatch"), set(COMPONENTS))
        for name in COMPONENTS:
            self.assertEqual(
                self.selected(event="workflow_dispatch", component=name), {name}
            )
        self.assertEqual(
            self.selected([".dockerignore"], bootstrap="menubuilder-backend"),
            {"menubuilder-backend"},
        )

    def test_invalid_inputs_are_rejected(self):
        for kwargs in [
            {"component": "../bad"},
            {"event": "pull_request"},
            {"bootstrap": "all"},
        ]:
            with self.assertRaises(ValueError):
                self.selected(**kwargs)

    def test_linux_case_is_preserved(self):
        self.assertEqual(self.selected(["menubuilder/backend/app.py"]), set())


class DeploymentTests(unittest.TestCase):
    def test_processing_health_uses_api_prefix(self):
        with patch.object(
            deploy, "docker", return_value='{"Running": true}'
        ) as command:
            deploy.health("processingbackend", "container")
        self.assertIn("/api/health", command.call_args_list[0].args[-1])

    def test_failed_health_restores_previous_image_and_does_not_promote_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = Path(tmp)
            calls = []

            def execute(*args, capture=False):
                calls.append(args)
                return "container-id" if capture else ""

            with (
                patch.object(deploy, "STATE", state),
                patch.object(deploy, "run", side_effect=execute),
                patch.object(deploy, "docker", return_value="previous-image"),
                patch.object(
                    deploy,
                    "wait_healthy",
                    side_effect=[RuntimeError("unhealthy"), None],
                ),
            ):
                with self.assertRaisesRegex(RuntimeError, "unhealthy"):
                    deploy.deploy_service(
                        "menubuilder-backend", "candidate-image", "a" * 40
                    )
            import json

            candidate = json.loads(
                (state / "menubuilder-backend-candidate.json").read_text()
            )
            self.assertEqual(
                candidate["services"]["menubuilder-backend"]["image"], "previous-image"
            )
            self.assertFalse((state / "user1-images.json").exists())
            restarts = [call for call in calls if "up" in call]
            self.assertEqual(len(restarts), 2)
            for call in restarts:
                self.assertEqual(call[-1], "menubuilder-backend")
                self.assertIn("--no-deps", call)
                self.assertIn("--no-build", call)

    def test_migration_failure_leaves_running_processing_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls = []

            def execute(*args, capture=False):
                calls.append(args)
                if "upgrade" in args:
                    raise RuntimeError("migration failed")
                return "container-id" if capture else ""

            with (
                patch.object(deploy, "STATE", Path(tmp)),
                patch.object(deploy, "run", side_effect=execute),
                patch.object(deploy, "docker", return_value="previous-image"),
            ):
                with self.assertRaisesRegex(RuntimeError, "migration failed"):
                    deploy.deploy_service(
                        "processingbackend", "candidate-image", "a" * 40
                    )
            self.assertFalse(any("up" in call for call in calls))
            self.assertFalse((Path(tmp) / "user1-images.json").exists())

    def test_only_allowlisted_digest_images(self):
        ref = image_reference("menubuilder-backend", "a" * 40, "sha256:" + "b" * 64)
        self.assertEqual(
            ref, "dev-leo4-ru.cr.cloud.ru/etran/menubuilder-backend@sha256:" + "b" * 64
        )
        for args in [
            ("unknown", "a" * 40, "sha256:" + "b" * 64),
            ("processingbackend", "latest", "sha256:" + "b" * 64),
            ("processingbackend", "a" * 40, "latest"),
        ]:
            with self.assertRaises(ValueError):
                image_reference(*args)

    def test_override_preserves_other_images(self):
        original = {"services": {"processing-backend": {"image": "old"}}}
        result = update_override(original, "menubuilder-backend", "new")
        self.assertEqual(result["services"]["processing-backend"]["image"], "old")
        self.assertEqual(result["services"]["menubuilder-backend"]["image"], "new")
        self.assertNotIn("menubuilder-backend", original["services"])

    def test_compose_uses_explicit_project_and_override(self):
        command = compose_command("l4media-ingress", Path("images.json"))
        self.assertEqual(command[:4], ["sudo", "-n", "docker", "compose"])
        self.assertIn("l4media", command)
        self.assertIn("/home/user1/l4media/compose.yaml", command)

    def test_static_index_is_last_and_previous_assets_remain(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, destination = root / "release", root / "live"
            (source / "assets").mkdir(parents=True)
            (destination / "assets").mkdir(parents=True)
            (source / "index.html").write_text("new index")
            (source / "assets/new.js").write_text("new")
            (destination / "assets/old.js").write_text("old")
            (destination / "index.html").write_text("old index")
            publish_static(source, destination)
            self.assertEqual((destination / "index.html").read_text(), "new index")
            self.assertEqual((destination / "assets/old.js").read_text(), "old")
            self.assertEqual((destination / "assets/new.js").read_text(), "new")

    def test_invalid_static_release_does_not_touch_live(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, destination = Path(tmp) / "release", Path(tmp) / "live"
            source.mkdir()
            destination.mkdir()
            (destination / "index.html").write_text("old")
            with self.assertRaises(ValueError):
                publish_static(source, destination)
            self.assertEqual((destination / "index.html").read_text(), "old")


if __name__ == "__main__":
    unittest.main()
