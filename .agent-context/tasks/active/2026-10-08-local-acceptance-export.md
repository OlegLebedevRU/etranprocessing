# Local acceptance outcome export, 2026-10-08

## Scope and authority

New Setup acceptance_export.c/h and focused test_acceptance_export.c only.
Producer: primary SYSTEM/session0 exporter after the owned local acceptance
controller and worker retire. Consumer: stable Python pipeline reads held fixed
private artifact and seals the combined two-operation acceptance attestation.
No live installed writes, service changes, new MQTT client, signing/publication,
caller PASS, mutable codec view, journal repair or public RPC admission bypass.

## Implemented

The exporter holds existing deployment.lock exclusively (OPEN_EXISTING), native
canonical fixed-reference immutable full-chain reader and original private
ancestry fences. It reuses strict remote_status reducer plus live marker reader,
authenticated signed operation64, opaque local-purpose snapshot adapter and
existing success102 admission. RESTORED uses the strict applied-status history
and bound typed108, including source root/plan hash/all12 config references.
Missing/active/recovery-required/foreign/later-generation markers refuse. Exact
clear is checked twice, including immediately before publication.

The fixed output is original operation directory acceptance.result.json, maximum
8192 bytes, CREATE_NEW, SYSTEM owner, SYSTEM full and Administrators read only.
Existing exports refuse overwrite. Interrupted/failed writes can leave a partial
protected artifact; consumers must reject incomplete or malformed JSON.

Output schema1 kind l4tools-local-acceptance-result includes exact source/target
root and descriptor digests, actual platform/profile, local773/tenant1 scope,
owner-signed authorization hash and native operator-intent hash, private catalog
revision/hash, actual updater executable version/hash/size from ACKv2, original
operation/plan/proof/outcome hashes, typed SUCCESS or RESTORED, original error,
installed endpoint, actual proof epochs/config references and UTC FILETIME bounds.
Terminal/tenant are configured local stand attestation, not a new native IoT
identity measurement. Existing actual completion/restore gates retain certificate
and fresh IoT communication requirements.

Communication backwards evidence is inferred from genuine forward102 produced by
the exact shared executor that must pass the mixed communication barrier before
Con/Sup switching. This exporter adds no nonce latch, extra journal record,
six-point collector or caller supplied backwards verdict. Python independently
binds reviewed pipeline pair compatibility checks and both native exports.

## Verification

- Production export source isolated /MT /W4 /WX compilation PASS x86 and x64.
- Composition fixture PASS214 checks, zero failures on each architecture.
- Tests cover forward/restored output, absent local purpose, malformed/absent
  completion, immutable/lock refusal, wrong hashes/configs/epochs/profile/scope,
  duplicate outcome, active/foreign/later-generation clear, final clear drift,
  revalidation refusal, existing artifact and failed publication.
- Fixture explicitly models trust, SCM, token/owner security and file handles;
  no genuine SYSTEM positive export or installed proof is claimed by these tests.
- No concurrent permanent Setup/Con objects were changed by focused builds;
  isolated outputs are under l4con/obj/active-updater-probe.

## Integration and remaining gates

Root owns Setup permanent build/run_tests links and local baseline replacement.
Fresh agent owns local launcher, owner authorization, protected purpose snapshot
API and fixed executor forcepoint. Python agent owns held artifact reader and
combined sealed acceptance producer. Link production acceptance_export.c;
focused test includes the production source and needs advapi32.lib only.

Before real release: finish all three integrations, final native unified gates,
clean reviewed source checkpoint, signed baseline/target including actual worker
and controller plus Con admission (public edge absent until acceptance), frozen
helper owner seal, clean fresh baseline with actual anchor/SFA/ancestry. Run
forced X-to-Y-to-X then forward X-to-Y, consume genuine exports. Reinstall the
same signed X only for actual RPC X-to-Y test; preserve monotonic catalog floor,
frozen helper and historical operation journals. Public catalog revision must
exceed the real private trial revision. No component roots may change after
acceptance through post-test enable/re-sign.

Existing Invoke-L4FreshTransition targets legacy C:\l4tools images, while Repair
only handles uncommitted fresh installation. Neither is a replacement mechanism
for already committed Program Files baseline1.13.6. Root is adding the narrow
reviewed owner-controlled replacement rather than adopting a missing anchor.
