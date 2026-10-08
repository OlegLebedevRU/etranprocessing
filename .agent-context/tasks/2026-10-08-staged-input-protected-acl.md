# Protected ACL on staged bundle inputs

Fresh1.13.11 operation `6e6c36c8-c635-46e2-83e3-a0ecb617f92e` reached durable
local deployment91, then updater initialization returned error13. The original
journal was opened read-only and its full SHA chain validated (85 records,
SHA256 `c4f3dfb38a8a8e1a21a563f5b0a2abe6887e1e7395c4af038f9e7ab1af7f63f0`).
Receipt16 references bootstrap14, PATH15,13 config proposals; descriptor5854 and
signature384 match the owner-signed root. Actual native replay, bootstrap load,
committed/non-aborted terminal and both ancestry fences passed.

Exact failure: `inputs/l4tools-release.json` and its signature inherited a
private SYS/Administrators DACL but did not have `SE_DACL_PROTECTED`. The strict
active-updater reader refuses this; its read wrapper surfaced error13. Readers
are unchanged. The producer `install_bundle.c:copy_held` now creates every input
with explicit `D:P` SYS/Administrators full-access DACL. Existing private input
retry checks that same strict security before hash reuse. Installed public setup
host still uses ordinary inherited security. No existing files are repaired.

Validation:

- x86/x64 production source `/O2 /MT /W4 /WX` compile passed.
- New actual copy -> strict `active_updater:read_bytes` fixture:29 checks,0 failures
  each architecture. Protected private copy/exact retry pass; inherited copy,
  wrong hash and protected BU-read contamination refuse. Public host copy keeps
  its previous read behavior. Exact temporary files/directories removed.
- Integrated `build.cmd test-bundle-copy` passed both architectures. The same
  fixture is included in normal `run_tests.cmd`. No full suite repeated.
- Separate read-only diagnostic used actual1.13.11 signed root/signature copied
  only into temporary producer-created protected files. Strict reader, REAL
  compiled-owner metadata PKI (root and descriptor) and held installed PE exact
  size/hash all passed x86/x64. No signature model or pointer authority was used.

The diagnostic proves those remaining gates and the corrected producer/reader
contract. It does **not** claim current1.13.11 inputs were fixed or a live pointer
initialized; original production `authenticate_history` still refuses their
unprotected file DACL. Published11, services, configs and helper were unchanged.
Fresh baseline/target releases remain the root agent's deployment responsibility.

Fixture links `journal.c`, `layout.c`, `metadata.c`, `bootstrap_history.c`,
`journal_codec.c`, `journal_reader.c`, `policy_json.c`; libraries advapi32,bcrypt,
shell32,ole32. Other bundle admission calls are fail-loud unused fixture stubs,
not successful modeled trust. Isolated artifacts: `tools/l4setup/obj/bundle-copy-acl`.
