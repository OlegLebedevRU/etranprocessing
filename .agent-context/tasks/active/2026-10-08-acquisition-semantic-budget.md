# Acquisition fixture semantic budget — 2026-10-08

Owner /root/mosquitto_acl; authorized tools/release readiness scope. Only production
test source changed: tools/l4setup/tests/test_update_metadata.c. No native production
budget, service, signature, public artifact or existing dist report was changed.

Target unsigned1.13.8 failed setup-test-x86 at acquisition364: successful worker
capture unexpectedly timed out1460, then its expected non-null gate check failed.
That semantic path repeatedly verifies real signed multi-hop metadata, inventories,
CNG hashes and journal files within5000ms. The same fixture already uses10000ms
STOP_SEMANTIC_BUDGET_MS for equivalent stop ownership tests. Worker/communication
semantic cases and their mock remaining-budget bounds now use that same constant.
Removed worker_capture flag that only selected the artificial5000ms mock bound.
Controlled3000ms acquisition/cancel tests, injected clock-offset timeout, UTC guard
deadline faults and race synchronization wait remain unchanged.

Focused isolated /W4 /WX /MT builds and actual fixture runs both x86/x64:
1255 checks,0 failures each. Transport/trust/service authority modeled; CNG,
signed fixture metadata/journal/file I/O real; no live SCM. Used target8 root-fixture
read-only, separate original obj/acquisition-budget-08b3ed94-44cf-45fa-870f-560ad85654b9.
No global obj/bin/build script modifications or parallel full builds.

Failed target report and its relevant log retained byte-for-byte under that own obj
directory's failed-target8-checkpoint with retention.json hashes. Original target
report unchanged. Source revision654a42ac40d2b9f87836f415aa5dbcb27ddf397b, input digest
0aac53ad7ee5ad578cdc68d6bd99183a8a1e2af7a82644e159898d17e51bb6e0.

Next parent-owned step: freeze/review Sup service rights fix, this test correction
and mandatory privileged SCM gate; prepare a new clean source checkpoint. Candidate7
is published immutable. Unsigned/unpublished8 may be prepared again only after an
explicitly retained/archive old local checkpoint, never by editing its stored input
digest or rewriting an old prepared report. Full release/native gates remain required.
