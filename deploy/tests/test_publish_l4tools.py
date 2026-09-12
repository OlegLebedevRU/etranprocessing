#!/usr/bin/env python3
"""Unit tests for deploy/publish_l4tools.py."""

import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch
import urllib.error

import deploy.publish_l4tools as publish_l4tools


class TestPublishL4Tools(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.artifacts_dir = Path(self.tmp_dir.name)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def _create_sample_artifacts(
        self,
        version: str = "1.6.0",
        exe_content: bytes = b"dummy exe content",
        dirty: bool = False,
        manifest_exe_sha: str | None = None,
        corrupt_sums: bool = False,
    ):
        exe_path = self.artifacts_dir / "l4setup.exe"
        exe_path.write_bytes(exe_content)
        exe_sha = hashlib.sha256(exe_content).hexdigest().lower()

        manifest_data = {
            "schema": 1,
            "version": version,
            "git_sha": "abc1234567890abcdef1234567890abcdef12345",
            "dirty": dirty,
            "built_at": "2026-09-12T18:00:00Z",
            "builder": "windows-dev",
            "files": {
                "l4setup.exe": {
                    "sha256": manifest_exe_sha
                    if manifest_exe_sha is not None
                    else exe_sha,
                    "size": len(exe_content),
                }
            },
            "components": {"l4superv": "1.6.0", "leo4proxy": "1.2.0"},
            "min_os": "6.1",
            "arch": ["x86", "x64"],
        }
        manifest_path = self.artifacts_dir / "l4tools-release.json"
        manifest_path.write_text(json.dumps(manifest_data, indent=2), encoding="utf-8")
        manifest_sha = hashlib.sha256(manifest_path.read_bytes()).hexdigest().lower()

        if corrupt_sums:
            sums_content = f"0000000000000000000000000000000000000000000000000000000000000000  l4setup.exe\n{manifest_sha}  l4tools-release.json\n"
        else:
            sums_content = (
                f"{exe_sha}  l4setup.exe\n{manifest_sha}  l4tools-release.json\n"
            )

        sums_path = self.artifacts_dir / "SHA256SUMS"
        sums_path.write_text(sums_content, encoding="utf-8")

    def test_compute_sha256(self):
        f = self.artifacts_dir / "test.bin"
        f.write_bytes(b"hello world")
        expected = hashlib.sha256(b"hello world").hexdigest().lower()
        self.assertEqual(publish_l4tools.compute_sha256(f), expected)

    def test_parse_sha256sums(self):
        f = self.artifacts_dir / "SHA256SUMS"
        f.write_text(
            f"# Comment\n{'a' * 64}  file1.exe\n{'b' * 64} *file2.json\n",
            encoding="utf-8",
        )
        parsed = publish_l4tools.parse_sha256sums(f)
        self.assertEqual(parsed["file1.exe"], "a" * 64)
        self.assertEqual(parsed["file2.json"], "b" * 64)

    def test_parse_rfc3230_digest(self):
        headers = {
            "Digest": "sha-256=abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"
        }
        digest = publish_l4tools.parse_rfc3230_digest(headers)
        self.assertEqual(
            digest, "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"
        )

        headers_lower = {"digest": "SHA-256=ABCDEF1234567890"}
        digest_lower = publish_l4tools.parse_rfc3230_digest(headers_lower)
        self.assertEqual(digest_lower, "abcdef1234567890")

        self.assertIsNone(publish_l4tools.parse_rfc3230_digest({"Other": "val"}))

    def test_mask_secret(self):
        secret = "secret_password_123"
        log_line = f"Error connecting to https://key:{secret}@registry.ar.cloud.ru"
        masked = publish_l4tools.mask_secret(log_line, secret)
        self.assertNotIn(secret, masked)
        self.assertIn("***", masked)

    def test_verify_artifacts_valid(self):
        self._create_sample_artifacts()
        manifest, computed_sums, sizes = publish_l4tools.verify_artifacts(
            self.artifacts_dir
        )
        self.assertEqual(manifest["version"], "1.6.0")
        self.assertIn("l4setup.exe", computed_sums)
        self.assertIn("l4tools-release.json", computed_sums)
        self.assertIn("SHA256SUMS", computed_sums)

    def test_verify_artifacts_missing_file(self):
        self._create_sample_artifacts()
        (self.artifacts_dir / "l4setup.exe").unlink()
        with self.assertRaises(FileNotFoundError):
            publish_l4tools.verify_artifacts(self.artifacts_dir)

    def test_verify_artifacts_corrupt_checksum(self):
        self._create_sample_artifacts(corrupt_sums=True)
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            publish_l4tools.verify_artifacts(self.artifacts_dir)

    def test_verify_artifacts_manifest_sha_mismatch(self):
        self._create_sample_artifacts(manifest_exe_sha="bad" * 20 + "badbad")
        with self.assertRaisesRegex(ValueError, "Manifest sha256 mismatch"):
            publish_l4tools.verify_artifacts(self.artifacts_dir)

    def test_verify_artifacts_dirty(self):
        self._create_sample_artifacts(dirty=True)
        with self.assertRaisesRegex(ValueError, "dirty"):
            publish_l4tools.verify_artifacts(self.artifacts_dir, allow_dirty=False)

        manifest, _, _ = publish_l4tools.verify_artifacts(
            self.artifacts_dir, allow_dirty=True
        )
        self.assertTrue(manifest["dirty"])

    @patch("urllib.request.urlopen")
    def test_check_remote_version_already_published(self, mock_urlopen):
        mock_resp = MagicMock()
        mock_resp.status = 200
        mock_resp.__enter__.return_value = mock_resp
        mock_urlopen.return_value = mock_resp

        code = publish_l4tools.check_remote_version("1.6.0")
        self.assertEqual(code, 2)

    @patch("urllib.request.urlopen")
    def test_check_remote_version_not_published(self, mock_urlopen):
        mock_urlopen.side_effect = urllib.error.HTTPError(
            url="http://dummy", code=404, msg="Not Found", hdrs={}, fp=io.BytesIO()
        )
        code = publish_l4tools.check_remote_version("1.6.0")
        self.assertEqual(code, 0)

    @patch("urllib.request.urlopen")
    def test_check_remote_version_network_error(self, mock_urlopen):
        mock_urlopen.side_effect = urllib.error.URLError(reason="Connection refused")
        code = publish_l4tools.check_remote_version("1.6.0")
        self.assertEqual(code, 1)

    def test_publish_dry_run(self):
        self._create_sample_artifacts()
        with patch(
            "deploy.publish_l4tools.check_remote_version", return_value=0
        ) as mock_check:
            code = publish_l4tools.publish_release(
                artifacts_dir=self.artifacts_dir,
                dry_run=True,
                registry_base="https://mock-registry.example.com",
            )
            self.assertEqual(code, 0)
            mock_check.assert_called_once_with(
                "1.6.0", registry_base="https://mock-registry.example.com"
            )

    def test_publish_already_published_halts(self):
        self._create_sample_artifacts()
        with patch("deploy.publish_l4tools.check_remote_version", return_value=2):
            code = publish_l4tools.publish_release(
                artifacts_dir=self.artifacts_dir,
                dry_run=False,
            )
            self.assertEqual(code, 2)

    @patch("urllib.request.urlopen")
    def test_publish_live_flow_and_digest_verification(self, mock_urlopen):
        self._create_sample_artifacts()
        exe_sha = publish_l4tools.compute_sha256(self.artifacts_dir / "l4setup.exe")
        sums_sha = publish_l4tools.compute_sha256(self.artifacts_dir / "SHA256SUMS")
        manifest_sha = publish_l4tools.compute_sha256(
            self.artifacts_dir / "l4tools-release.json"
        )

        sha_sequence = [exe_sha, sums_sha, manifest_sha]

        # Simulating sequence of responses:
        # For each file:
        # 1. PUT response (status 201)
        # 2. HEAD response (status 200, Digest header with corresponding sha)
        mock_responses = []
        for sha in sha_sequence:
            # PUT response
            put_resp = MagicMock()
            put_resp.status = 201
            put_resp.__enter__.return_value = put_resp
            mock_responses.append(put_resp)

            # HEAD response
            head_resp = MagicMock()
            head_resp.status = 200
            head_resp.headers = {"digest": f"sha-256={sha}"}
            head_resp.__enter__.return_value = head_resp
            mock_responses.append(head_resp)

        mock_urlopen.side_effect = mock_responses

        record_dir = self.artifacts_dir / "artifacts"
        releases_file = self.artifacts_dir / "releases.jsonl"

        with (
            patch("deploy.publish_l4tools.check_remote_version", return_value=0),
            patch.dict(
                "os.environ",
                {
                    "AR_GENERIC_KEY_ID": "mock_id",
                    "AR_GENERIC_KEY_SECRET": "mock_secret",
                },
            ),
        ):
            code = publish_l4tools.publish_release(
                artifacts_dir=self.artifacts_dir,
                dry_run=False,
                record_dir=record_dir,
                releases_file=releases_file,
            )
            self.assertEqual(code, 0)

        # Verify record files were created
        record_json = record_dir / "1.6.0.json"
        self.assertTrue(record_json.is_file())
        data = json.loads(record_json.read_text(encoding="utf-8"))
        self.assertEqual(data["version"], "1.6.0")
        self.assertTrue(data["registry_digest_verified"])

        self.assertTrue(releases_file.is_file())
        lines = releases_file.read_text(encoding="utf-8").strip().splitlines()
        self.assertEqual(len(lines), 1)
        journal = json.loads(lines[0])
        self.assertEqual(journal["component"], "l4tools")
        self.assertEqual(journal["version"], "1.6.0")

    @patch("urllib.request.urlopen")
    def test_publish_digest_mismatch_returns_exit_code_3(self, mock_urlopen):
        self._create_sample_artifacts()

        # PUT succeeds
        put_resp = MagicMock()
        put_resp.status = 201
        put_resp.__enter__.return_value = put_resp

        # HEAD returns wrong digest
        head_resp = MagicMock()
        head_resp.status = 200
        head_resp.headers = {"digest": "sha-256=" + "f" * 64}
        head_resp.__enter__.return_value = head_resp

        mock_urlopen.side_effect = [put_resp, head_resp]

        with (
            patch("deploy.publish_l4tools.check_remote_version", return_value=0),
            patch.dict(
                "os.environ",
                {
                    "AR_GENERIC_KEY_ID": "mock_id",
                    "AR_GENERIC_KEY_SECRET": "mock_secret",
                },
            ),
        ):
            code = publish_l4tools.publish_release(
                artifacts_dir=self.artifacts_dir,
                dry_run=False,
            )
            self.assertEqual(code, 3)


if __name__ == "__main__":
    unittest.main()
