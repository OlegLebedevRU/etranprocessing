"""Fixed-authority public reads and existing Generic Registry multipart PUT."""

from __future__ import annotations

import base64
import contextlib
import hashlib
import http.client
import re
import urllib.error
import urllib.request
import uuid
from pathlib import Path

from .common import ReleaseError
from .pipeline import read_config


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ReleaseError("Catalog Registry redirects are not permitted")


class CatalogRegistry:
    """No caller URL override, inherited credentials, cross-host redirects or CAS claim."""

    def __init__(self, root: Path, env: dict[str, str]):
        self.base = read_config(root)["registry"]
        self.env = env
        self.opener = urllib.request.build_opener(_NoRedirect())

    @staticmethod
    def _relative(relative: str) -> str:
        if not re.fullmatch(r"l4tools/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+", relative):
            raise ReleaseError("Invalid fixed Catalog Registry artifact path")
        if any(part in {".", ".."} for part in relative.split("/")):
            raise ReleaseError("Invalid Catalog Registry path component")
        return relative

    def download(self, relative: str, target: Path, maximum: int) -> bool:
        """Public bounded streaming; at most three GETs for a truncated body.

        Retry only an incomplete HTTP body, never a complete hash/signature
        mismatch. Keep one exclusively created output handle across attempts.
        False only for actual404. PUT and RPC behavior are unaffected.
        """
        relative = self._relative(relative)
        request = urllib.request.Request(self.base + "/" + relative, method="GET")
        try:
            with contextlib.ExitStack() as stack:
                stream = None
                for attempt in range(3):
                    try:
                        with self.opener.open(request, timeout=120) as response:
                            if response.status != 200:
                                raise ReleaseError("Catalog Registry GET did not return200")
                            length = response.headers.get("Content-Length")
                            if length is not None and (
                                not length.isascii() or not length.isdigit()
                            ):
                                raise ReleaseError("Catalog Registry Content-Length is invalid")
                            expected = int(length) if length is not None else None
                            if expected is not None and expected > maximum:
                                raise ReleaseError(
                                    "Catalog Registry artifact exceeds its signed bound"
                                )
                            if stream is None:
                                stream = stack.enter_context(target.open("xb"))
                            else:
                                stream.seek(0)
                                stream.truncate()
                            total = 0
                            while block := response.read(min(1024 * 1024, maximum + 1 - total)):
                                total += len(block)
                                if total > maximum:
                                    raise ReleaseError(
                                        "Catalog Registry artifact exceeds its signed bound"
                                    )
                                stream.write(block)
                            if expected is None or total == expected:
                                return True
                            if total > expected:
                                raise ReleaseError("Catalog Registry body exceeds Content-Length")
                    except http.client.IncompleteRead:
                        pass  # Retry only HTTP's explicit incomplete-body indication.
                    if attempt == 2:
                        raise ReleaseError(
                            f"Catalog Registry GET body truncated after3 attempts: {relative}"
                        )
                raise AssertionError("Bounded GET loop must return or refuse")
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return False
            raise ReleaseError(f"Catalog Registry GET failed: HTTP{error.code}") from error
        except urllib.error.URLError as error:
            raise ReleaseError("Catalog Registry GET transport failed") from error

    def upload(self, relative: str, data: bytes) -> None:
        """Same directoryURL/file multipart API as deploy/publish_l4tools.py."""
        relative = self._relative(relative)
        identifier = self.env.get("AR_GENERIC_KEY_ID", "")
        secret = self.env.get("AR_GENERIC_KEY_SECRET", "")
        if not identifier or not secret:
            raise ReleaseError("Set explicit Generic Registry write credentials in sw_sign.env")
        directory, name = relative.rsplit("/", 1)
        boundary = "L4Catalog" + uuid.uuid4().hex
        mime = "application/json" if name.endswith(".json") else "application/octet-stream"
        body = (
            (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="file"; filename="{name}"\r\n'
                f"Content-Type: {mime}\r\n\r\n"
            ).encode("ascii")
            + data
            + f"\r\n--{boundary}--\r\n".encode("ascii")
        )
        authorization = base64.b64encode(f"{identifier}:{secret}".encode()).decode("ascii")
        request = urllib.request.Request(
            self.base + "/upload/" + directory + "/",
            data=body,
            headers={
                "Authorization": "Basic " + authorization,
                "Content-Type": f"multipart/form-data; boundary={boundary}",
                "Content-Length": str(len(body)),
            },
            method="PUT",
        )
        try:
            with self.opener.open(request, timeout=120) as response:
                if response.status not in {200, 201, 204}:
                    raise ReleaseError("Catalog Registry PUT was not accepted")
        except urllib.error.HTTPError as error:
            raise ReleaseError(
                f"Catalog Registry PUT failed: HTTP{error.code}; no retry"
            ) from error
        except urllib.error.URLError as error:
            raise ReleaseError(
                "Catalog Registry PUT transport failed; verify exact pending plan"
            ) from error


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()
