# Active updater input ancestry ACL correction

Fresh1.13.9 operation `4101bbec-8e20-471a-8e30-b484a9e2f3aa` completed local
deployment91, then active-updater initialization failed with access denied.
Read-only inspection found the existing admission pinned `inputs` with private
security starting at the ProgramData suite root. That root and update ancestry
correctly permit Users read/execute; the operation UUID and inputs are private.

`active_updater.c` now retains two fences: the protected suite-root-to-operation
ancestry permits read, and operation-to-inputs enforces private SYS/Administrators
security. Both remain held until admission closes. Root/signature files retain
the same private security and metadata/PE hash admission. No trust, owner,
terminal outcome or signature gate changes.

The existing `test_active_updater.c` now creates actual temporary security
descriptors/directories. It reproduces the former ERROR_ACCESS_DENIED, accepts
public BU RX ancestry with private operation/inputs, and refuses BU read on
either private boundary. Exact empty fixture directories are removed after
handles close; cleanup passed. No installed path is modified.

Validation: x86/x64 production `/O2 /MT /W4 /WX` compile passed. Both existing
metadata fixtures and **16 actual native ACL assertions per architecture** passed.
RSA metadata verification remains modeled in this fixture; full signed fresh
initialization was not executed here. Runner:
`tools/l4setup/obj/updater-acl/build.cmd x86|x64`.

Changed files: `tools/l4common/active_updater.c` and its existing fixture. Published
1.13.9 and frozen helper were not changed; no service, pointer initialization,
signing or deployment action was performed by this agent.
