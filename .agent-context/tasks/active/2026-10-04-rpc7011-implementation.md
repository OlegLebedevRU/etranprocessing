# RPC7xxx /7011 implementation handoff

## Intake
- Authorized implementation: contracts7xxx, native RPC/policy, PB renew PIN/route,
  IoT7011, l4pin and MenuBuilder. No legacy FRONT/BACK or SQL examples scope.
- Bases: etranprocessing origin/main91c0bc8; IoT origin/master987c5ba.
- IoT worktree: C:/Users/oleg_/.codex/worktrees/rpc7011-iot; original dirty
  checkout retained unchanged. Branches feat/rpc7011-renewal / feat/rpc7011-contracts.
- Owners: IoT task/registry; native l4con execution; PB issuance/shared migration;
  MenuBuilder authorization/dispatch/form. No new operation UUID, no outbox.
- MQTT type confirmed by user: extra_service; preserve existing l4con presence.
- Plan: [agreed details](../../../docs/term_arch-rpc-7011-certificate-renewal-plan.md).

## Stack gates
1. IoT: canonical method-aware7xxx validation, redaction, metadata audit and fixtures.
2. Native: consume gate1, parsing/TSK mode/status headers/dedup/timeout/policy expiry.
3. PB/shared: renew PIN, authenticated route, PIN locking/issuance/recovery; real API fixture.
4. IoT7011: consume gate3, registry/TTL/redacted history/result reconciliation contract.
5. Native renewal: consume gates3/4, l4pin authorized mode/protected execution and tests.
6. MenuBuilder: consume gates3/4/5, active/free/paid admission, synchronous PIN+queue,
   max3 manual retries same PIN, async result and inspection button.
7. Integrated matrix, signed suite handoff, release readiness. No unsigned publication.

## Current status — 2026-10-05

Gates1–6 implemented, committed and deployed through builder → registry → production.
Signed suite1.11.0 published and installed on773: ready/0. Real7011 happy path
passed: queue, execution, issuance, install, hotrotation, current-serial mTLS
discovery confirmation. Failure/reboot/Win7 gates below remain separate.
First rotation occurred during temporarynginxtrustwindow; finalroute-onlyPB
trust subsequently passed actualclientCHECK and currentserial authenticated discovery.

## Actual deployed contracts / revisions

Root implementation checkpoint0b0c48d38971721f9762819093709a9628305784;
frontend final9adae354a96d9e66b30a81f4aef0332a9752cabe;
IoT accepted master8c2be800567f074b079bc0c61e993e98184bd897.
Fixtures: docs/contracts/{rpc7xxx-gate1,pb-renewal-gate3,rpc7011-gate4}.json,
PB revision3.3 (route-only trust/serial representation clarification; API DTO unchanged). Native consumes producer fixtures; no contract invented at stack entry.

| Component | Production immutable digest |
|---|---|
| ProcessingBackend | sha256:6a17f70ff84c7ea590cea7aedfde52f4ed85da780db76c2905c6b5bacb8146db |
| MenuBuilder backend | sha256:6fab3cb0832f58ab370535c25d5d713df16add511c349f90cd376bdd8917fc3a |
| IoT app1 | sha256:0b51e5b174ce37087cd4f6b1a89291ae72361e2b65c0fe9c73052fe700085d74 |
| MenuBuilder frontend | sha256:4d8a3fe0e99d112bb956570c47eb8d271f5ec6eb4eab6bad55663fe0e1d1829a |

Base compose.yaml and production base Compose contain these PB/MB/IoT image
references in addition to the deployer's persistent override. Candidate config
validated before replacement; only3 image entries changed, no service recreate.
Frontend updated through live dist replacement; nginx-default was not restarted.
PB migrated real PostgreSQL029→030; Alembic head030 verified before compatible consumers.
Exact nginx renew location verifies mTLS/overwrites cert headers, timeout65s;
PIN-provider routes return404 externally. nginx -t passed, media unaffected.

## Completed verification

- IoT full Windows523 passed/7 skipped, builder Linux497 passed/33 skipped;
  Ruff/format/Pyright changed source passed. Original dirty IoT checkout untouched.
- PB176 passed, shared65 passed, MenuBuilder backend626 passed/20 skipped;
  required Ruff/format/Pyright passed. Existing warnings remain.
- Frontend all77 tests plus tsc/Vite passed locally and on builder. Isolated browser
  actual component: double click produces one POST; initial failure plus3 manual
  repeats reuse pin_id, close form and disable action for3min; PIN not shown.
  Preview mocked API, so this checks UI behavior, not certificate issuance.
- Unified x86/x64/default native builds/tests: parsing/fixture, cancel addressing,
  TSK/RSP dedup, status/result_uid, bounded network, protected busy, Job authority,
  IPC/lifecycle, DPAPI CSR/public-response recovery and corruption tests passed.
- Proxy2304 DNS/routing cases per architecture, deadline12/upstream20, loopback
  Schannel rotation and missing/expired-cert policy tests passed.
- l4setup readiness9/pipeline44/certificate2/upstream7 and SCM/desk checks passed.
- Signed19EXEs Valid with timestamps;130 embedded files match staged content.
  Twelve unchanged component EXEs preserve exact prior bytes/signatures.
  Three complete HTTPS downloads match size/hash. Published record:
  [1.11.0](../../../artifacts/l4tools/1.11.0.json).
- Setup SHA2563ee234ff1f5ab716a0de43a1b9d26303f9ad11432adc939bbb1e7d73cc1960ac
  unchanged after final docs/UI release. Do not rebuild/repack immutable tools.
- Operator installation773 confirmed; local install_summary ready/0, suite1.11.0,
  l4con1.10.0/l4pin1.8.0/proxy1.8.2, all4services Running, MQTT/HTTPS/RTP valid.
- Real safe RPC7003 via tenant-scoped internal API, emptydt, TTL1min:
  task_id bfd92378-606f-4f4e-9cb3-281d7f0b5f07, status3, RES200/pong.
  RealIoTtask ID generated byIoT; no preallocated UUID or certificate side effect.
- Real historical details now200: b5b6b450-d5c6-4c8c-a464-7ff32b721b4a(method7001)
  and df6be987-cc9d-405d-ba07-354209650cb7(method50), status3 anddt payload.
  Production JSONB uniqueness500 corrected in IoT8c2be80 by unique task ID;
  actualSQLAlchemy JSON result/join regression verifies this boundary.
- Service-key MB→PB invalid terminal0 gets422, not401; noPIN created.
  Anonymous externalrenew403 / publicPIN-provider404; no insecure TLS bypass.
- Final PB api/health, MB openapi.json, IoT docs200. Initial check used a wrong
  PB /health URL and got404; corrected to actual /api/health, which passed.
- nginx-default and mutual-nginx container IDs unchanged vs predeployment.

## Flow invariants / remaining risk

- IoT owns taskUUID; no operation_id/outbox/preassigned taskUUID. Max3 manual
  repeats reuse samePIN; PB also reuses pending PIN after reopening.
- RawPIN only device delivery; history/detail/export/log/webhook masked;
  raw7011 scrubbed on result/delete/expiry. No test PIN/credential saved here.
- Local idle poll60s, whitelist7001/7002/7003/7011. Protected renew has no FIFO:
  next exec/renew busy409; explicitcancel/shutdown may interrupt. Ordinary exec
  cancel-and-replace joins≤5s. Common installer mutex rejects parallel installers.
- Job120s/l4pin90s/auth grant≥100s remaining; execution only PIN remaining>120s.
  QueueTTL never exceeds PINexpiry, floor((remaining−5)/60), defaultPIN24h.
- Durable sameCSR recovery≤15min/currentissuance/PINexpiry; different CSR or
  superseded issuance refused. UsedPIN means issued; fresh current-serial PB
  mTLS discovery after used_at confirms actual use, independently of lostRES.
- Expired/missing/not-yet-valid cert locally denies MQTT/RTP even Policy down;
  valid-cert72h offline grace retained. CustomMQTThost DNS still lacks ceiling;
  standard local numeric endpoint bypasses DNS.
- CA adapter retains existingverify=False trust debt. CA issuance and DBcommit
  are not distributed atomic: loss ofCAresult beforecommit not promised recoverable.
- [x] Real7011 issuance/store/hotrotation/newserial/discovery confirmation on773.
- [ ] Multiworker PostgreSQL contention, actualCAresponse-loss/store/reboot recovery,
  Win7runtime; local fixtures do not prove these failure scenarios.
- Full timing/failure matrix: [implemented flow](../../../docs/term_arch-rpc7011-flow-matrix.md).

## Readiness / cleanup / rollback

[MCP Ops Readiness: UNAVAILABLE]; SSH fallback used with existing authorization.
Preflight production RAM2114MiB/root42%/load0.26; builder2733MiB/root57%/load0.01.
Final production available RAM2238240KiB/root44%/load0.23; services healthy.
No ad hoc JWT/test credentials, no secrets copied into containers. Read-only
changed/new text scan found no real credentials. Existing service auth read by
its configured client only; values were never printed.

Owned browser rpc7011-ui closed; temporaryVite session30236 stopped; exact preview
HTML/testscript removed. No broad process kill or wildcard retained-topic clearing.
Safe ping task stays in normal command history as release acceptance evidence.
Remote Compose and nginx prechange backups intentionally retained for rollback;
local ignored pre-release signed artifacts retained. Original dirtyIoTcheckout intact.

Rollback: disable newUI first; restore prior compatible images through standard
release path if necessary. Do not downgrade030 or oldserial blindly. If7011 has
issued a certificate, reconcile the actual installed serial before any rollback.
Keep signed published artifacts immutable and retain recovery state as applicable.

## PB runtime correction after operator7011 attempt

773 order returned503 before PIN creation. PostgreSQL rejected unqualified
FOR UPDATE because Terminal eagerly LEFT JOINs optional terminal_types.
Both providerPIN and renewSETUP now use FOR UPDATE OF terminals; PIN lock
remains scoped to CertificatePin. Generated PostgreSQL SQL assertions added
for both actual handler queries; PB176tests/quality passed. Scoped terminal
lock executed successfully on production PostgreSQL and transaction rolled
back without changing any certificate/PIN. PB-only image deployed from0b561ce7fa40fcfd2c10ea36a6576f4fa5821e0f, digest
sha256:731f5f1355bbd2691edf0e62db8ca09402b2bc7aa56409ec95e49095c72579ad.
Builder176tests/quality passed; productionhealth passed. MB/IoT/nginx IDs
unchanged. BaseCompose image updated after candidatevalidation, no recreate.
Actual MBservice→PBprovider request for773 returned201, pin_id302; repeat
with returnedpin_id reused the same hiddenPIN. NoCSR/issuance happened in
this check; this pendingPIN is reserved for the operator's retry, not orphan
scratchdata. User asked to repeatUI order. Unrelated403 browserentries are
unconfirmed: Response details no longer available, no authorization weakened.

## Endpoint trust / leading-zero correction

Real7011 task f79fe21f-200d-4ec2-a3da-d65926d1e71d returnedRES200/exit0 in2203ms,
new certificate installed and proxy hotrotated. Numeric serial E6A80EDDF507030DF7982A76C29484EB45BAF4E
is rendered by nginx/Windows as0E6A80EDDF507030DF7982A76C29484EB45BAF4E;
DB issuance uses unpadded hex. Strict string comparison rejected identity.
Fix: newCA identity compares uppercase/leading-zero-normalized numeric serial
AND SN, discovery records the unchanged issuance representation. No serial
write outsidesetup/renew; legacy auth branch unchanged. MB admission correction
for20<hexlength<=40 follows this PB contract result.

Global nginx trustedCA briefly enabled; user explicitly constrained changes
to newroute. Exact prechange globalTLS configuration restored/reloaded.
Final nginx delta onlyrenewlocation: requireclientcertificate (NONE denied),
overwrite certificateheaders, retainoptional_no_ca unchanged. PBrenew verifies
leaf signature directly with packaged publiciot.leo4.ru trustanchor, issuer/fullDN,
validity/SN/currentserial; SUCCESS alone cannot authenticate a forged certificate.
Trustedroot fingerprint matches existing ingressCA and terminalroot.
PB181tests/quality passed, including forgedleaf withSUCCESS/FAILED header denial.
Only publicCA resource packaged, no privatekeys or learned client trustanchor.
CorrectedPBimage rollout and runtimeverifiednewroute remain to complete.


PBb928f5f3549f713dc0e36e09e398dbb3ed25c1ab deployed with181tests/quality passed;
digest sha256:6a17f70ff84c7ea590cea7aedfde52f4ed85da780db76c2905c6b5bacb8146db.
Onlynewnginxrenewlocation applied/reloaded afterPBdeployment; byte-equality
assertion proves allotherconfigincludingglobalTLSunchanged. NoCAadvertisement/
trustlistchange remains. Runtimecurrentclient reachesrenewCHECK (dummyPINnotfound),
provingactualCA signature verification; anonymousrenew403. Policy returned200
and currentserial discovery is_validTrue afterused_at. Brief503 duringreload
resolved onnextquery. UsedPIN302 and freshdiscovery confirm actualinstallation.
MenuBuilder632tests/20skipped plusqualitypassed: accepts validnewCA21–40hex
representation, denieslegacy≤20/malformed/too-long. MB-onlyrolloutfollows.


## Final deployed state

MB89008f297dc285136ce72688b3f9920cbc1011ea, digest
sha256:6fab3cb0832f58ab370535c25d5d713df16add511c349f90cd376bdd8917fc3a.
632passed/20skipped on Windows andbuilderLinux, requiredqualitypassed.
PB181passed/quality onbothplatforms, sourceb928f5f. BothbaseCompose images
pinnedtofinaldigests, configvalid, noextra service recreation. Finalhealth200.
Nginxcomparisonasserts alltextoutside renewlocation byte-equivalent toprechange;
anonymousrenew403, legacyanonymousCHECK200, rootCA signature validated byPB.
Proxy ready/currentcertificate/mqttclient1/policyerror empty; no restartforrotation.
FreshsuccessfulPBdiscovery afterPIN302used_at confirmsnewserial in DB issuance
representation. UIuser should onlyclick «Проверить результат», no newissuance.
No realoldSubCAterminal tested duringthiswindow; configpreservation/legacyunit
coverage is notlegacyfleet E2E proof. TemporaryglobalCAtrust wasreverted promptly
on user'sconstraint; it isnotpart ofcommittedfinalnginxconfiguration.


Operator final acceptance: «Проверить результат» shows «Новый сертификат
используется терминалом» without the newCA warning (userconfirmed yes).
All implementation/deployment/happy-path acceptance gates complete. Remaining
fault/reboot/Win7/legacyfleet tests are explicitly scoped limitations above,
not claims of completed fault-injection coverage. Signed tools unchanged.
