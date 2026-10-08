# Installed source: active updater selects applicability

Scope: `tools/l4setup/src/installed_source.c` and its existing fixture. No live
services, installation, signing, publication or history removal.

Two discovery defects were addressed: a retained aborted fresh operation was
treated as a potential source and blocked discovery; repeated clean installation
of the same signed version/root made older committed operations appear equally
applicable.

Discovery now holds and verifies the existing active-updater anchor. Its exact
origin, version, architecture and owner-signed root identity select outer source
applicability. Remote102 descendants retain their authenticated ancestor's
updater identity independently of their suite version. Historical recursion still
authenticates its complete signed ancestry without filtering against today's
anchor.

Other fresh operations are read through the protected immutable journal reader.
Their bootstrap40, terminal phase grammar and receipt85 references are checked;
they cannot authorize a source. Old aborted input packages are not re-admitted.
Incomplete pre-receipt bootstrap histories are excluded; malformed receipt-only,
contradictory terminal or corrupt histories fail closed. The anchor's fresh base
must still pass the full existing signed metadata, request and receipt admission
and have a committed, non-aborted terminal outcome.

Validation: isolated `/O2 /MT /W4 /WX` production compile and existing focused
fixture both x86/x64: **84 checks, 0 failures each**. Protected journal/receipt
references and production rollback45/46 terminal grammar are actual. Expensive
bootstrap-plan admission and updater identity fixtures are modeled; SCM and
external authentication paths not exercised fail loudly. This does not prove
live source selection or RPC readiness. No broader gate was repeated.

Runner: `tools/l4setup/obj/source-selection/build.cmd x86|x64`. Initial x64 fixture
run failed one assertion before the intended rollback records had been added;
both corrected final runs passed. Production timeout/security policy unchanged.
