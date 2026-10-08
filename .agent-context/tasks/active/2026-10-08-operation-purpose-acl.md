# Operation purpose ACL boundary — 2026-10-08

Owner /root/mosquitto_acl, authorized narrow runtime fix following readonly review.
No installed/live/public candidate9 changes, signing or publication. No frozen
helper, budget, RPC contract or flow changes.

Two real gates incorrectly called l4_store_pin(UUIDdir, operations, true):
acceptance_local open_inputs and remote_host no_local_purpose. Canonical operations
has BU GRGX from l4_layout_prepare. The control=true fence checks secure_root
inclusively and refuses any non-SY/BA ACE, so both gates failed5 on canonical ACLs.

Both now hold two separate fences: protected public-readable ancestry from data
to UUID (control=false), and private UUID self boundary (control=true). Operation
and staged control files remain private; no ACL is expanded or repaired. Both
fences close on normal and failure paths. Reader and installed-source saved-input
boundaries already use correct private roots; no further pin change was needed.

Existing tests were extended with actual temporary native ACL directories and
production functions. Canonical BU-RX data/update/operations plus private UUID
reproduces old ERROR_ACCESS_DENIED and passes new production optional-input /
ordinary-sidecar-absence gate. BU read on UUID and BU write on operations are both
refused. Exact owned temporary directories remove successfully after handles close.
RSA, SCM/host and operator authority are still modeled or fail-loud; these tests
do not claim real trial/controller admission or stand E2E.

Isolated /O2 /MT /W4 /WX x86/x64 builds and tests passed:
local acceptance131/0, remote host316/0 each. Both changed production translation
units compiled independently /W4 /WX botharch. Runner:
tools/l4setup/obj/operation-boundary-acl/run-x86.cmd and run-x64.cmd.
No global obj/bin/run_tests/build modifications. Parent's full release pipeline is
the next required verification; no broader native gate was run concurrently.

Frozen files: tools/l4setup/src/acceptance_local.c,
tools/l4common/remote_host.c, and their existing test_acceptance_local.c /
test_remote_host.c fixtures. Parent may proceed with reviewed checkpoint/release.
