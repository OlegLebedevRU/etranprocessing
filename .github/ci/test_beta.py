import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import beta

A = "a" * 40
B = "b" * 40
C = "c" * 40
DIGEST = "sha256:" + "d" * 64


class SelectionTests(unittest.TestCase):
    def test_checkout_includes_processing_proxy_contract_fixture(self):
        source = (beta.ROOT / "deploy/beta/launcher.py").read_text()
        self.assertIn('"ProcessingBackend/nginx-mutual-legacy/nginx-configs"', source)

    def state(self):
        return {
            "schema": 1,
            "bootstrap_revision": A,
            "releases": {"menubuilder-backend": {"revision": B}},
        }

    def test_each_component_uses_its_own_last_success(self):
        changes = Mock(return_value=["shared/models.py"])
        self.assertTrue(
            beta.needs_release("menubuilder-backend", C, self.state(), changes)
        )
        changes.assert_called_with(B, C)
        self.assertTrue(
            beta.needs_release("processingbackend", C, self.state(), changes)
        )
        changes.assert_called_with(A, C)

    def test_initial_baseline_does_not_claim_a_deployment(self):
        changes = Mock()
        state = {"bootstrap_revision": A, "releases": {}}
        self.assertFalse(beta.needs_release("processingbackend", A, state, changes))
        changes.assert_not_called()
        self.assertEqual(state["releases"], {})

    def test_already_released_and_unrelated_changes(self):
        changes = Mock(return_value=["docs/ci.md", "MenuBuilder/frontend/src/app.tsx"])
        self.assertFalse(
            beta.needs_release("menubuilder-backend", B, self.state(), changes)
        )
        changes.assert_not_called()
        self.assertFalse(
            beta.needs_release("menubuilder-backend", C, self.state(), changes)
        )

    def test_flow_changes_select_all_and_docs_select_none(self):
        for component in beta.COMPONENTS:
            self.assertTrue(beta.affected(component, ["deploy/beta/versions.json"]))
            self.assertTrue(beta.affected(component, [".github/ci/beta.py"]))
            self.assertFalse(beta.affected(component, ["deploy/beta/README.md"]))

    def test_invalid_revision_and_unknown_schema_are_not_accepted(self):
        for revision in ["main", "../bad", "", "a" * 39, "A" * 40]:
            with self.assertRaises(ValueError):
                beta.require_revision(revision)


class ReleaseTests(unittest.TestCase):
    def test_failed_deploy_never_advances_checkpoint(self):
        state = {"bootstrap_revision": A, "releases": {}}
        with tempfile.TemporaryDirectory() as tmp:
            state_file = Path(tmp) / "state.json"
            state_file.write_text(json.dumps(state))
            with (
                patch.object(beta, "build", return_value={"revision": B}),
                patch.object(
                    beta, "deploy_artifact", side_effect=RuntimeError("failed")
                ),
            ):
                with self.assertRaisesRegex(RuntimeError, "failed"):
                    beta.release("menubuilder-backend", B, state, state_file)
            self.assertEqual(json.loads(state_file.read_text()), state)

    def test_success_updates_only_target_and_writes_journal(self):
        state = {
            "bootstrap_revision": A,
            "releases": {"processingbackend": {"revision": A}},
        }
        artifact = {"component": "menubuilder-backend", "revision": B, "digest": DIGEST}
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            with (
                patch.object(beta, "HOME", home),
                patch.object(beta, "build", return_value=artifact),
                patch.object(beta, "deploy_artifact"),
            ):
                result = beta.release(
                    "menubuilder-backend", B, state, home / "state.json"
                )
            self.assertEqual(result["releases"]["menubuilder-backend"], artifact)
            self.assertEqual(
                result["releases"]["processingbackend"],
                state["releases"]["processingbackend"],
            )
            self.assertNotIn("menubuilder-backend", state["releases"])
            self.assertEqual(
                json.loads((home / "releases.jsonl").read_text())["digest"], DIGEST
            )

    def test_build_only_does_not_deploy_or_mark_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "state.json"
            with (
                patch.object(beta, "build", return_value={}),
                patch.object(beta, "deploy_artifact") as deploy,
            ):
                self.assertEqual(
                    beta.release("menubuilder-backend", B, {}, path, deploy=False), {}
                )
            deploy.assert_not_called()
            self.assertFalse(path.exists())

    def test_retry_reuses_existing_artifact_without_rebuilding(self):
        artifact = {"component": "menubuilder-backend", "revision": B, "digest": DIGEST}
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / "artifacts/menubuilder-backend"
            directory.mkdir(parents=True)
            (directory / (B + ".json")).write_text(json.dumps(artifact))
            with (
                patch.object(beta, "HOME", Path(tmp)),
                patch.object(beta, "execute") as execute,
                patch.object(beta, "test_component") as test,
            ):
                self.assertEqual(beta.build("menubuilder-backend", B), artifact)
            test.assert_not_called()
            self.assertEqual(
                execute.call_args.args[:4],
                ("docker", "buildx", "imagetools", "inspect"),
            )
            self.assertIn("@" + DIGEST, execute.call_args.args[-1])

    def test_corrupt_artifact_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "artifact.json"
            path.write_text(
                json.dumps(
                    {
                        "component": "menubuilder-backend",
                        "revision": B,
                        "digest": "latest",
                    }
                )
            )
            with self.assertRaises(ValueError):
                beta.load_artifact(path, "menubuilder-backend", B)
            with self.assertRaises(ValueError):
                beta.load_artifact(path, "processingbackend", B)


if __name__ == "__main__":
    unittest.main()
