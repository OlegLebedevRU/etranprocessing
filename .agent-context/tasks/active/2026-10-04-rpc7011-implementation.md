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

Gates1–6 implemented locally in the two owned feature branches; Gate7 unsigned
release preparation completed; waiting for mandatory operator signing. No production changes, commits or publication.
Actual contracts: root docs/contracts/{rpc7xxx-gate1,pb-renewal-gate3,rpc7011-gate4}.json;
PB contract revision3.2 reuses pending/same-current-issuance recovery PINs.
Native consumer uses producer fixtures. Full timing/risk matrix:
[implemented flow](../../../docs/term_arch-rpc7011-flow-matrix.md).

### Completed checks
- IoT full pytest from repository root:522 passed,7 skipped. Ruff and Pyright
  changed source passed. Original dirty IoT checkout untouched; generated
  newline-only contract snapshots restored in task worktree.
- PB full176 passed (20 existing warnings); Ruff/format/Pyright app passed.
- Shared full65 passed; quality passed; schema030/source manifest and offline
  029→030 SQL verified. No destructive migration statements.
- MenuBuilder backend full626 passed,20 skipped (52 existing warnings); app quality
  passed. Final frontend tsc/Vite passed;7 RPC DTO/masking tests passed including
  manual l4pin command-argument redaction; cooldown timer added.
- l4con1.10.0, l4pin1.8.0, leo4proxy1.8.2 x86/x64/default unified static builds.
- Native both architectures: producer fixture, method validation, cancel addressing,
  TSK/RSP dedup, status/result_uid, TTL, bounded TCP/partial packet receive,
  protected busy/atomic replacement decision, actual child-process Job authority
  (outsideJob/wrongexe/shortdeadline rejected), user-event IPC/lifecycle passed.
- l4pin DPAPI CSR/public-response recovery fixture/corruption/strict endpoint,
  certificate discovery/profile cleanup/http discovery/GUI tests passed.
- Proxy existing2304-case resolving matrix per architecture, deadlines12,
  upstream20, loopback Schannel rotation and missing/expired policy tests passed.
- l4setup local readiness9, pipeline44, certificate2, upstream7 and SCM/desk tests
  passed before new payload assembly. No real terminal certificate modified.

### Flow invariants / limitations
- IoT owns task UUID; no new operation_id/outbox/preassigned task UUID. Queue
  response loss allows max3 manual repeats with samePIN; reused also after reopen.
- IoT payload delivery remains raw; authorized history/results/logs/webhook masked,
  SQL bind parameters hidden; raw7011 scrubbed on result/delete/expiry.
- Local queue poll reconnect/idle60s with whitelist7001/7002/7003/7011. No7011FIFO.
- Protected Job120s; l4pin90s; IPC requires≥100s remaining, directCLI denied.
  Competing install uses common non-waiting mutex. Explicitcancel/shutdown allowed.
- SameCSR durable recovery≤15min and PIN expiry; superseded issuance rejected.
  CSR/key stored beforeSETUP, publicPKCS7 beforecertstore update.
- UsedPIN is issuance only. Confirmation requires fresh current-serial PB mTLS
  discovery afterused_at; manualcheck may inspectknownrealIoTtask once.
- CustomMQTThost DNS lacks ceiling; normal127.0.0.1/localhost path bypassesDNS.
- CA adapter retains existingverify=False trust debt; CA issuance/DBcommit not
  a distributed transaction. Do not claim response-loss recovery beforeDBcommit.

### Outstanding gates
- Unsigned1.11.0 ready: both payloads65 files match staging;12 unchangedEXEs
  retain exact hashes/Valid timestamped signatures;6 changedEXEs unsigned.
  Networkprofile preserved. Setup x86/x64/default and manifest verified; signed=false.
  Backup tools/dist/.runtime-backup/rpc7011-1.11.0-preparation retained for rollback.
  Read-only changed/new text secret scan98 files found no credential matches.
- Mandatory operator signature, then verify both payloads/all18EXEs/setup timestamp
  and exact hashes; no rebuildafter signing/no unsigned publication.
- RealPostgreSQLmigration030, multiworker lock contention, realCA/store/reboot/Win7
  recovery and E2E7011 on773 remain unexecuted. Dockerdaemon unavailable locally.
- Production activation waits migration030 +compatiblePB/IoT/MenuBuilder +signed
  capabletools. Oldready/0 Upgrade773 to1.10.2 is not acceptance of thisflow.

### Operator signing handoff

Run from this worktree in regular Windows PowerShell:

```powershell
& .\tools\release\Complete-SignedRelease.ps1 -PfxPath '<path to signing PFX>' -Version 1.11.0 -SignOnly @('leo4proxy', 'l4con', 'l4pin')
```

Unsigned setup SHA256 04dc2c644a95267ea23a51eb44d2050d5ab46510e2db07f1f21ffde6ef7253b7
is preparation evidence only; signing/repacking will change it. Do not publish this hash
as a signed release or regenerate native tools after successful signing.
