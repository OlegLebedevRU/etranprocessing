# Fresh orphan transport evidence — 2026-10-08

## Task intake

- Native tools only; owner Con extra_service (previously confirmed), common local IPC, additive Setup readiness adapter. No IoT wire, MB/PB, helper, installed services or MQTT identity changes.
- Producer: existing Con client strict matching of orphan RSP/EVA. Transport: existing local protected health pipe, additive v3 mode4. Consumer: SYSTEM native measurement producer; signed catalog evidence validator is a separate owner.
- Evidence comes only from the current callback's fresh generation. No cached last success, caller PASS object, publication or signature authority.
- Validation: unified Con x86/x64, strict parser/codec and actual local pipe negative checks; readiness epoch/error orchestration fixture. No live broker, service mutation or release/signing.

## Source contract

`tools/l4common/link_probe_evidence.h` provides `L4LinkProbeEvidence` and fixed 244-byte version1 codec. It contains original request nonce, matched RSP correlation, event nonce, matched EVA correlation and original request echo, sent/matched-receive FILETIME values, sent/echoed event IDs. All UUIDs canonical lowercase, REQ and EVT distinct; timestamps strictly ordered and within 300 seconds. Parsing preserves actual echoed values rather than manufacturing echoes from inputs.

Con mode1 bool readiness is unchanged. New optional evidence completion copies under the same probe lock only after LINK_DONE and complete codec validation. No second MQTT connection or additional IoT schema. Message send failure, malformed/foreign/error/expired response, canceled/replaced generation or incomplete history cannot export success.

`l4_probe_evidence_call(expected_pid, timeout, out)` starts a fresh exchange. Local request nonce is generated inside the client; v3 mode4 echoes it and returns exactly the typed evidence. Server requires actual LocalSystem/session0 caller using named-pipe impersonation, then mandatory revert before Con callback. Existing v1/v2 health/drain/recovery keep SYS/BA behavior. Client repeats server PID, validates schema/nonce, rejects evidence older than this invocation or received after its finish, and zeroes output on error. Timeout capped at 300 seconds. `ole32.lib` is declared in probe_ipc.c for UUID generation, including standalone IPC tests.

`setup_readiness_barrier_evidence(plan, timeout, out)` is additive in existing readiness.c/h. It holds the original Con process handle and PID/creation across IPC, checks process is still alive, repeats exact SCM command/PID and broker environment, and validates complete typed proof before returning. Caller still owns signed plan, complete four-service measurement and report/sealing authority.

## Catalog mapping

- req_rsp: nonce=request_nonce; echo_nonce=rsp_correlation; sent_utc=req_sent_utc; received_utc=rsp_received_utc.
- evt_eva: nonce=event_nonce; echo_nonce=eva_correlation; request_nonce=request_nonce; echo_request_nonce=eva_request_nonce; event_id/echo_event_id direct; sent_utc=evt_sent_utc; received_utc=eva_received_utc.

Schema source: `l4release/catalog_evidence.py`, six measured forward/reverse checkpoints. This chunk does not construct or seal those checkpoints.

## Validation and limits

- Focused actual matched parser and codec x86/x64 PASS, including malformed nonce/ID/chronology/incomplete completion and codec alteration refusal.
- Actual private IPC x86/x64 PASS: existing health/drain/recovery, wrong server PID, ordinary caller evidence refusal, wrong local request nonce echo, stale complete payload, empty output and cancellation/deadline behavior.
- Readiness adapter x86/x64 /MT /W4 /WX PASS: actual local TCP/pipe; SCM/transport verdicts modeled. Added evidence error/incomplete/SCM drift/creation drift/late provider refusal. No real IoT measurement assertion.
- Final unified Con x86/x64/default build (including permanent evidence gate) PASS, no compiler warnings/errors: tools/l4con/obj/link-evidence-unified-build.log. Registered RPC runtime x86/x64 PASS: tools/l4con/obj/link-evidence-rpc-runtime.log. Existing same-client probe frames, 7032/event76 and update admission/drain semantics preserved.
- No actual SYSTEM positive mode4/IoT roundtrip on installed Con yet. Native six-point collection, signed owner attestation and platform-specific real fault acceptance remain separate gates. Unit fixture values are not release compatibility evidence.
