"""Fixed-authority public reads and existing Generic Registry multipart PUT."""

from __future__ import annotations

import base64
import hashlib
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
        """Public exact bytes, bounded streaming. False only for actual404."""
        relative = self._relative(relative)
        request = urllib.request.Request(self.base + "/" + relative, method="GET")
        try:
            with self.opener.open(request, timeout=120) as response:
                if response.status != 200:
                    raise ReleaseError("Catalog Registry GET did not return200")
                total = 0
                with target.open("xb") as stream:
                    while block := response.read(min(1024 * 1024, maximum + 1 - total)):
                        total += len(block)
                        if total > maximum:
                            raise ReleaseError("Catalog Registry artifact exceeds its signed bound")
                        stream.write(block)
                return True
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
