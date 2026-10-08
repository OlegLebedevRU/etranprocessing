"""ACL parser fixtures and actual read-only SDK roundtrip on owned temp storage."""

import os

import pytest

from l4release import catalog_state as module
from l4release.common import ReleaseError


@pytest.mark.skipif(os.name != "nt", reason="Win32 owner/DACL SDK")
def test_actual_windows_security_roundtrip(tmp_path):
    descriptor, actor = module._windows_security(tmp_path)
    assert "D:" in descriptor and actor.startswith("S-1-5-")


@pytest.mark.skipif(os.name != "nt", reason="Win32 ACL parser policy")
@pytest.mark.parametrize("rights", ["FA", "GA", "GW", "WD", "WO", "SD", "0x40"])
def test_foreign_ancestor_replacement_refused(tmp_path, monkeypatch, rights):
    leaf = tmp_path / "leaf"
    leaf.mkdir()
    actor = "S-1-5-21-1-2-3-1001"

    def security(path):
        acl = f"O:{actor}G:SYD:(A;;FA;;;SY)(A;;FA;;;{actor})"
        if path == tmp_path:
            acl += f"(A;;{rights};;;BU)"
        return acl, actor

    monkeypatch.setattr(module, "_windows_security", security)
    with pytest.raises(ReleaseError, match="replacement/write"):
        module.protect_state(leaf)


@pytest.mark.skipif(os.name != "nt", reason="Win32 ACL parser policy")
def test_public_read_does_not_become_write(tmp_path, monkeypatch):
    actor = "S-1-5-21-1-2-3-1001"
    monkeypatch.setattr(
        module,
        "_windows_security",
        lambda path: (f"O:{actor}G:SYD:(A;;FA;;;SY)(A;;FA;;;{actor})(A;;FR;;;BU)", actor),
    )
    module.protect_state(tmp_path)


@pytest.mark.skipif(os.name != "nt", reason="Win32 ACL parser policy")
def test_foreign_ancestor_owner_refused_even_readonly_dacl(tmp_path, monkeypatch):
    leaf = tmp_path / "leaf"
    leaf.mkdir()
    actor = "S-1-5-21-1-2-3-1001"
    monkeypatch.setattr(
        module,
        "_windows_security",
        lambda path: (
            f"O:{'BU' if path == tmp_path else actor}G:SYD:(A;;FA;;;SY)(A;;FR;;;BU)",
            actor,
        ),
    )
    with pytest.raises(ReleaseError, match="owner"):
        module.protect_state(leaf)
