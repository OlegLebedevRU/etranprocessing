# Fresh local deployment without enrollment or channel

## Task intake
- Goal: owner-authorized local clean l4setup installation must work without a terminal certificate or IoT connectivity.
- Scope: l4setup fresh entry/composition, local broker renderer and typed bootstrap journal. No MQTT client changes, IoT/backend changes, live installation, frozen helper changes.
- Owner/producer/consumer: local elevated operator -> authenticated signed kit -> existing SYSTEM setup host -> owned SCM/config/ACL/PATH; later supervisor enrollment/activation remains separate.
- Invariants: signed inventory and actual actor ACL verification stay mandatory; no fake transport barrier or READY; existing strict bootstrap activation/commit unchanged; only original owned services are touched; terminal deployment forbids abort.
- Validation: mandatory x86/x64/default build and native isolated fixtures, no PB/MB checks.

## Implementation
- Records 90/91 are LOCAL_DEPLOY_BEGIN/DONE, bound to original bootstrap plan. No READY records or probe/barrier callbacks are generated.
- Local commit verifies pinned release files, all four owned STOPPED SCM fingerprints, config/ACL and applied PATH before selecting recorded start types. Per-service type intent/readback retains managed partial-commit abort.
- Postcommit service start is best effort under a separate bounded budget; missing certificate/channel cannot undo installation. Each start pins the expected executable and checks owned SCM/start type. No communication-ready claim is made.
- Certificate discovery is optional for fresh verify/install. Missing usable certificate leaves proxy thumbprint unset for autodiscovery and selects a localhost-only broker with no bridge/client ID. No duplicate MQTT connection is created.
- Historical fresh status exposes deployment_committed, local_deployment and communication_ready=null. committed/deployment_committed represent either terminal deployment or strict terminal activation; local_deployment distinguishes record90/91. None proves live health.
- Rendered active/standby broker configurations call the ACL agent's typed secret-free public-read API; arbitrary extra configs retain private ACLs.

## Validation / limitations
- Native focused fixtures x86 and x64 each: bootstrap lifecycle 41408 checks, install profiles 634 checks, fresh composition 1084 checks; 0 failures. Fresh composition includes real files/ACL/journal/launcher writes with modeled admission/SYSTEM/SCM; local mode invokes zero activation/barrier callbacks. SCM tests include postcommit activation failure, partial start-type failure/abort, foreign/running refusal and strict-path rejection of local terminal records.
- Final unified `tools/l4setup/build.cmd all` completed exit0, fresh x86/x64/default outputs inspected; unsigned development binaries only, no signing/publishing.
- All SCM lifecycle checks are modeled; no live installation or service changes performed.
- Native APIs remain synchronous; deadline checks cannot interrupt a blocked WinAPI call.
- Owner approved local-only unavailable revocation handling. Explicit per-context SetupAdmissionPolicy local APIs use WinVerifyTrust cache-only with whole-chain revocation first. Only CRYPT_E_REVOCATION_OFFLINE / CRYPT_E_NO_REVOCATION_CHECK may retry cache-only without revocation retrieval, after scanning signer/timestamp certificate chains for known revocation. Cryptographic signature, Windows chain/time, pinned publisher, validated timestamp and signed metadata/hash checks remain mandatory. CERT_E_REVOCATION_FAILURE has no fallback. Strict original remote APIs retain online whole-chain revocation checks.
- Fresh bundle context, manifest prepare/verify, SYSTEM host self/staging and local fresh recovery explicitly select local policy. setup_fresh_apply rejects a locally admitted context; local policy cannot feed strict activation. No global flag and no insecure root-store bootstrap.
- Cold offline Windows with missing root/intermediate CA trust may still fail correctly; no arbitrary chain/time trust exceptions. Actual installed signed 1.13.6 leo4proxy PE passed both original strict and local cache-only APIs; authenticated publisher leaf SHA256 and validated timestamp/Windows trust exercised. Native /WX fixtures compile against Windows7 target declarations. Mocked unknown/bad-signature/chain/time/publisher/known-revocation cases pass; cache-only flags and whitelist assertions are explicit.
- An interruption while selecting AUTO start types followed by reboot can start a partial owned installation. Recovery deliberately refuses unrecorded live process adoption; operator diagnostics may be needed. No crash/reboot installation claim.

## Owner-approved offline admission follow-up
- Final x86/x64 each: admission 732 checks, or 735 including actual pre-existing signed PE; fresh composition 1137 checks; manifest 86 checks; all 0 failures. Both real unsigned test PEs fail local and strict APIs.
- Whitelist tests cover both allowed missing-evidence statuses, strict rejection, unknown policy enums, bad signature/digest/chain/time/publisher/timestamp, known signer/counter/certificate/chain revocation, final verification/close failure, public release-verification policy and exact cache-only flags.
- Final unified l4setup build after production policy changes completed exit0 with x86/x64/default outputs. Development unsigned outputs only; release signing/publishing and live installation not performed. Root remote_request SOURCE integration is a subsequent distinct build.
