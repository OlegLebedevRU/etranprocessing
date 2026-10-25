import unittest
from pathlib import PurePosixPath

from desktop_stream_release import candidate, check_environment, compose


class DesktopReleaseTests(unittest.TestCase):
    def setUp(self):
        self.obj = {
            "Config": {
                "Image": "old-image",
                "Env": ["OTHER=unchanged", "VIDEO_PROXY_RTP_PORT=0"],
                "Labels": {
                    "com.docker.compose.project": "project",
                    "com.docker.compose.project.config_files": (
                        "/base.yaml,/camera.json,/operator.json"
                    ),
                },
            }
        }

    def test_keeps_all_existing_overlays_before_candidate(self):
        command = compose(self.obj, PurePosixPath("/release.json"))
        self.assertEqual(
            command[-8:],
            [
                "-f",
                "/base.yaml",
                "-f",
                "/camera.json",
                "-f",
                "/operator.json",
                "-f",
                "/release.json",
            ],
        )

    def test_rollback_restores_absence_and_previous_ports(self):
        env = candidate(self.obj, "menubuilder-backend", "old-image", restore=True)["services"][
            "menubuilder-backend"
        ]["environment"]
        self.assertEqual(env["VIDEO_PROXY_RTP_PORT"], "0")
        self.assertIsNone(env["VIDEO_PROXY_RTCP_PORT"])
        self.assertIsNone(env["VIDEO_CONTRACT_V2_ENABLED"])

    def test_rejects_unrelated_environment_change(self):
        with self.assertRaisesRegex(RuntimeError, "OTHER"):
            check_environment(
                {"OTHER": "preserve"}, {"OTHER": "changed", "FLAG": "true"}, {}, {"FLAG": "true"}
            )

    def test_preserves_image_defaults_and_literal_dollars(self):
        check_environment(
            {"PATH": "bin", "OTHER": "$literal"},
            {"OTHER": "$literal", "FLAG": "true"},
            {"PATH": "bin"},
            {"FLAG": "true"},
        )


if __name__ == "__main__":
    unittest.main()
