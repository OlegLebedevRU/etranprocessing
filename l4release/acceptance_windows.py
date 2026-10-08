"""Win32 held, fixed native acceptance assets; no caller summaries become authority."""

from __future__ import annotations

import ctypes
import os
import re
import uuid
from contextlib import contextmanager
from pathlib import Path

from .catalog_state import _TRUSTED_INSTALLER, _mask, _windows_security
from .common import ReleaseError


def known_folder(identifier: str) -> Path:
    if os.name != "nt":
        raise ReleaseError("Local acceptance requires this Windows stand")
    from ctypes import wintypes

    shell = ctypes.WinDLL("shell32", use_last_error=True)
    shell.SHGetKnownFolderPath.argtypes = [
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.HANDLE,
        ctypes.POINTER(wintypes.LPWSTR),
    ]
    free = ctypes.WinDLL("ole32").CoTaskMemFree
    free.argtypes = [ctypes.c_void_p]
    guid = ctypes.create_string_buffer(uuid.UUID(identifier).bytes_le)
    text = wintypes.LPWSTR()
    try:
        if shell.SHGetKnownFolderPath(guid, 0, None, ctypes.byref(text)) < 0 or not text.value:
            raise ReleaseError("Cannot resolve native acceptance Known Folder")
        return Path(text.value)
    finally:
        if text:
            free(text)


def windows_platform() -> dict:
    if os.name != "nt":
        raise ReleaseError("Local acceptance requires Windows")
    from ctypes import wintypes

    class Version(ctypes.Structure):
        _fields_ = [
            ("size", wintypes.DWORD),
            ("major", wintypes.DWORD),
            ("minor", wintypes.DWORD),
            ("build", wintypes.DWORD),
            ("platform", wintypes.DWORD),
            ("csd", wintypes.WCHAR * 128),
            ("sp_major", wintypes.WORD),
            ("sp_minor", wintypes.WORD),
            ("suite", wintypes.WORD),
            ("product", ctypes.c_ubyte),
            ("reserved", ctypes.c_ubyte),
        ]

    version = Version()
    version.size = ctypes.sizeof(version)
    ntdll = ctypes.WinDLL("ntdll")
    ntdll.RtlGetVersion.argtypes = [ctypes.POINTER(Version)]
    if ntdll.RtlGetVersion(ctypes.byref(version)) < 0:
        raise ReleaseError("Native OS profile query refused")
    info = ctypes.create_string_buffer(64)
    kernel = ctypes.WinDLL("kernel32")
    kernel.GetNativeSystemInfo.argtypes = [ctypes.c_void_p]
    kernel.GetNativeSystemInfo(info)
    architecture = int.from_bytes(info.raw[:2], "little")
    if architecture not in (0, 9):
        raise ReleaseError("Unsupported native acceptance architecture")
    return {
        "major": version.major,
        "minor": version.minor,
        "build": version.build,
        "native_arch": "x64" if architecture == 9 else "x86",
        "product_type": version.product,
    }


def roots() -> tuple[Path, Path]:
    facts = windows_platform()
    programs = known_folder(
        "6d809377-6af0-444b-8957-a3773f02200e"
        if facts["native_arch"] == "x64"
        else "905e63b6-c1bf-494e-b29c-65b732d3d21a"
    )
    data = known_folder("62ab5d82-fdc1-4dc3-a9dd-070d1d495d97")
    return programs / "Leo4" / "Tools", data / "Leo4" / "Tools"


def _security(path: Path, *, system: bool, export: bool, ancestor: bool = False) -> None:
    descriptor, _ = _windows_security(path)
    owner = re.search(r"O:(.*?)(?=G:|D:|S:|$)", descriptor)
    trusted = {"SY", "BA"} | ({_TRUSTED_INSTALLER} if ancestor else set())
    if not owner or owner.group(1) not in ({"SY"} if system else trusted):
        raise ReleaseError("Native acceptance asset is not SYSTEM-owned")
    if "D:" not in descriptor or "NO_ACCESS_CONTROL" in descriptor:
        raise ReleaseError("Native acceptance asset has no protected DACL")
    for raw in re.findall(r"\(([^()]*)\)", descriptor):
        values = raw.split(";")
        if len(values) != 6 or values[0] not in {"A", "D"}:
            raise ReleaseError("Unsupported native acceptance DACL")
        if values[0] == "D" or "IO" in values[1]:
            continue
        rights = _mask(values[2])
        if (values[5] not in trusted or (export and values[5] == "BA")) and rights & (
            0x500D0040 if ancestor else 0x500D0156
        ):
            raise ReleaseError("Native acceptance asset permits untrusted write/replacement")


@contextmanager
def held(path: Path, root: Path, *, system: bool = False, export: bool = False):
    """Hold every fixed ancestor and leaf against WRITE/DELETE; refuse aliases."""
    if os.name != "nt" or not path.is_absolute() or not path.is_relative_to(root):
        raise ReleaseError("Native acceptance asset escaped its fixed root")
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    ]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.GetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
    kernel.ReadFile.argtypes = [
        wintypes.HANDLE,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        ctypes.c_void_p,
    ]
    handles = []
    try:
        parts = [*reversed(root.parents), root]
        relative = path.relative_to(root)
        for part in relative.parts:
            parts.append(parts[-1] / part)
        for index, item in enumerate(parts):
            leaf = index == len(parts) - 1
            handle = kernel.CreateFileW(str(item), 0x80020000, 1, None, 3, 0x02200000, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ReleaseError("Cannot hold native acceptance asset safely")
            handles.append(handle)
            info = ctypes.create_string_buffer(52)
            if not kernel.GetFileInformationByHandle(handle, info):
                raise ReleaseError("Cannot inspect held native acceptance asset")
            attributes = int.from_bytes(info.raw[:4], "little")
            links = int.from_bytes(info.raw[40:44], "little")
            if (
                attributes & 0x400
                or (leaf and (attributes & 0x10 or links != 1))
                or (not leaf and not attributes & 0x10)
            ):
                raise ReleaseError(
                    "Native acceptance asset is a reparse/alias or wrong object type"
                )
            _security(item, system=system and leaf, export=export and leaf, ancestor=not leaf)

        def read(maximum: int) -> bytes:
            data = ctypes.create_string_buffer(maximum + 1)
            got = wintypes.DWORD()
            if (
                not kernel.ReadFile(handles[-1], data, maximum + 1, ctypes.byref(got), None)
                or not 0 < got.value <= maximum
            ):
                raise ReleaseError("Native acceptance asset read is empty/oversized")
            _security(path, system=system, export=export)
            return data.raw[: got.value]

        yield read
    finally:
        for handle in reversed(handles):
            kernel.CloseHandle(handle)


def elevate(root: Path, arguments: list[str]) -> int:
    """One engineer UAC elevation for the same reviewed CLI; never pass secrets."""
    import base64
    import subprocess
    import sys

    def literal(value):
        return "'" + value.replace("'", "''") + "'"

    command = subprocess.list2cmdline(["-m", "l4release", *arguments])
    script = (
        "$ErrorActionPreference='Stop'; $p=Start-Process -FilePath "
        + literal(sys.executable)
        + " -ArgumentList "
        + literal(command)
        + " -WorkingDirectory "
        + literal(str(root))
        + " -Verb RunAs -WindowStyle Hidden -Wait -PassThru; exit $p.ExitCode"
    )
    env = {
        k: v
        for k, v in os.environ.items()
        if k.upper() != "PSMODULEPATH"
        and not k.startswith(
            (
                "SW_SIGN_",
                "W_SIGN_",
                "L4TOOLS_SIGN_",
                "AR_GENERIC_",
                "IOT_API_KEY",
                "L4TOOLS_METADATA_",
            )
        )
    }
    result = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-NonInteractive",
            "-EncodedCommand",
            base64.b64encode(script.encode("utf-16-le")).decode("ascii"),
        ],
        cwd=root,
        env=env,
        check=False,
    )
    return result.returncode
