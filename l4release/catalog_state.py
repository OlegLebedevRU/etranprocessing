"""External publisher anti-rollback state; signed bytes also require private storage."""

from __future__ import annotations

import ctypes
import os
import re
import stat
from pathlib import Path

from .common import ReleaseError

_RIGHTS = {
    "FA": 0x1F01FF,
    "FR": 0x120089,
    "FW": 0x120116,
    "FX": 0x1200A0,
    "GA": 0x10000000,
    "GR": 0x80000000,
    "GW": 0x40000000,
    "GX": 0x20000000,
    "RC": 0x20000,
    "SD": 0x10000,
    "WD": 0x40000,
    "WO": 0x80000,
    "CC": 1,
    "DC": 2,
    "LC": 4,
    "SW": 8,
    "RP": 16,
    "WP": 32,
    "DT": 64,
    "LO": 128,
    "CR": 256,
}
_TRUSTED_INSTALLER = "S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464"


def _mask(text: str) -> int:
    if re.fullmatch(r"0x[0-9a-fA-F]+", text):
        return int(text, 16)
    if len(text) % 2:
        raise ReleaseError("Publisher state ACL has unsupported rights")
    mask = 0
    for index in range(0, len(text), 2):
        if text[index : index + 2] not in _RIGHTS:
            raise ReleaseError("Publisher state ACL has unsupported rights")
        mask |= _RIGHTS[text[index : index + 2]]
    return mask


def _windows_security(path: Path) -> tuple[str, str]:
    from ctypes import wintypes

    advapi = ctypes.WinDLL("advapi32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    advapi.GetNamedSecurityInfoW.argtypes = [
        wintypes.LPWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    advapi.GetNamedSecurityInfoW.restype = wintypes.DWORD
    advapi.ConvertSecurityDescriptorToStringSecurityDescriptorW.argtypes = [
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.LPWSTR),
        ctypes.c_void_p,
    ]
    advapi.OpenProcessToken.argtypes = [
        wintypes.HANDLE,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.HANDLE),
    ]
    advapi.GetTokenInformation.argtypes = [
        wintypes.HANDLE,
        ctypes.c_int,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
    ]
    advapi.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.LPWSTR)]
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    security = ctypes.c_void_p()
    descriptor, sid_text = wintypes.LPWSTR(), wintypes.LPWSTR()
    token = wintypes.HANDLE()
    try:
        if advapi.GetNamedSecurityInfoW(
            str(path), 1, 5, None, None, None, None, ctypes.byref(security)
        ):
            raise ReleaseError("Cannot verify publisher state file security")
        if not advapi.ConvertSecurityDescriptorToStringSecurityDescriptorW(
            security, 1, 5, ctypes.byref(descriptor), None
        ):
            raise ReleaseError("Cannot decode publisher state security")
        if not advapi.OpenProcessToken(kernel.GetCurrentProcess(), 8, ctypes.byref(token)):
            raise ReleaseError("Cannot identify publisher state owner")
        user = ctypes.create_string_buffer(4096)
        count = wintypes.DWORD()
        if not advapi.GetTokenInformation(
            token, 1, user, len(user), ctypes.byref(count)
        ) or not advapi.ConvertSidToStringSidW(
            ctypes.c_void_p.from_buffer(user).value, ctypes.byref(sid_text)
        ):
            raise ReleaseError("Cannot identify publisher state principal")
        if not descriptor.value or not sid_text.value:
            raise ReleaseError("Publisher state security is empty")
        return descriptor.value, sid_text.value
    finally:
        for value in (descriptor, sid_text, security):
            if value:
                kernel.LocalFree(value)
        if token:
            kernel.CloseHandle(token)


def protect_state(path: Path, *, private: bool = True) -> None:
    """Read-only permission check; never grants rights or trusts a signed old floor replay."""
    if not path.exists():
        raise ReleaseError("Create the external protected publisher state directory first")
    parts = [path, *path.parents]
    for index, item in enumerate(parts):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ReleaseError("Publisher state path contains a reparse point")
        if os.name != "nt":
            if (
                index == 0
                and private
                and (info.st_uid not in {0, os.getuid()} or info.st_mode & 0o022)
            ):
                raise ReleaseError("Publisher state is writable by an untrusted principal")
            if index and info.st_mode & 0o022 and not info.st_mode & stat.S_ISVTX:
                raise ReleaseError("Publisher state parent allows untrusted replacement")
            continue
        descriptor, actor = _windows_security(item)
        trusted = {"SY", "BA", actor}
        owner = re.search(r"O:(.*?)(?=G:|D:|S:|$)", descriptor)
        owners = trusted if index == 0 and private else trusted | {_TRUSTED_INSTALLER}
        if not owner or owner.group(1) not in owners:
            raise ReleaseError("Publisher state owner is untrusted")
        if "D:" not in descriptor or "NO_ACCESS_CONTROL" in descriptor:
            raise ReleaseError("Publisher state requires an explicit DACL")
        for raw in re.findall(r"\(([^()]*)\)", descriptor):
            values = raw.split(";")
            if len(values) != 6 or values[0] not in {"A", "D"}:
                raise ReleaseError("Publisher state has an unsupported ACL entry")
            if values[0] == "D" or ("IO" in values[1] and (index or values[5] == "CO")):
                continue
            dangerous = 0x500D0156 if index == 0 and private else 0x500D0040
            if values[5] not in trusted and _mask(values[2]) & dangerous:
                raise ReleaseError("Publisher state or parent permits untrusted replacement/write")


def state_directory(root: Path, env: dict[str, str], env_path: Path) -> Path:
    value = env.get("L4TOOLS_METADATA_KEY_PATH", "")
    if not value:
        raise ReleaseError("Set the existing external metadata key path for publisher state")
    key = Path(value)
    if not key.is_absolute():
        key = env_path.parent / key
    if key.absolute().is_relative_to(root.resolve()):
        raise ReleaseError("Publisher anti-rollback state must be outside the repository")
    protect_state(key.parent)
    directory = key.parent / "catalog-publisher"
    if directory.exists():
        protect_state(directory)
    return directory
