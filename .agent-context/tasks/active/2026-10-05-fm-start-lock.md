# FM 773: start503 and drain409

## Task intake
- Goal: fix operator-reported FM start failure and misleading repeat refusal.
- Scope: MenuBuilder admission guard/BFF and standalone Files UI; PB/IoT/native contract consumers checked.
- Owners: MB admission/UI, PB fm_operations, IoT exclusive lease. No schema migration or native change.
- Flow: UI -> MB -> IoT acquire -> PB registration -> MQTT start -> agent ACK.
- Invariants: same tenant/owner permission guard; terminal-disable serialization; common exclusive lease;
  MQTT lifecycle; Leo4Proxy HTTP; S3-only file bytes; no direct fallback or premature drain release.
- Verification: regression lock SQL, existing disabled-terminal/cleanup checks, backend full suite/quality,
  frontend build/unit/browser tests and runtime PostgreSQL lock compatibility. Live operator acceptance separate.

## Evidence / root cause
- 2026-10-05T17:02:23Z initial773 acquire200, PB lease GET200, BFF returns503 after8s,
  cleanup stop200; repeat409. Same pattern on subsequent operator attempts.
- During requested operator retry, pg_stat_activity: PB INSERT fm_operations waiting on transactionid;
  pg_blocking_pids pointed to MB's idle-in-transaction admission/subscription transaction.
- _verify_device_access(require_active=True) held FOR UPDATE OF terminals across awaited PB request.
  PB's FK terminal_id insert requires KEY SHARE on that same row, so MB waits for PB while PB waits for MB.
  The BFF timeout/transaction teardown releases the row; the persisted created session is not proof
  that registration responded before the timeout. No file operation was submitted.
- IoT stop revokes but keeps the files slot through the last authorised deadline+5s by design.
  UI previously discarded failed-start context and told the operator to close another session.

## Fix / validation
- Admission now uses FOR NO KEY UPDATE OF terminals: keeps competing terminal changes serialized,
  permits FK KEY SHARE insertion. Disabled/access/subscription checks preserved; no early commit.
- BFF reports fm_start_failed with server-computed retry_after_sec; stop/cancel and files conflicts
  expose the safe drain wait. Diagnostic logging includes only target/path/exception class.
- UI distinguishes failed start from file integrity failure; shows countdown and disables manual retry
  until drain deadline. No automatic retry or removal of the common exclusion/drain guard.
- MB Ruff check/format/Pyright passed. Full706passed/22skipped; known async-mock/deprecation warnings.
  Initial full run caught the old FOR UPDATE expectation; changed to the required protective lock.
- Frontend build passed,78unit passed,6fake-REST browser scenarios passed including failure/countdown/retry.
  Initial build caught optional-state narrowing; corrected and rebuilt before browser acceptance.
- PostgreSQL two-session probe on773,250ms lock_timeout, all transactions rolled back, no data changes:
  FOR UPDATE/KEY SHARE blocked; NO KEY UPDATE/KEY SHARE compatible;
  NO KEY UPDATE/NO KEY UPDATE blocked. This validates lock behaviour, not terminal file E2E.
- [MCP Ops Readiness: UNAVAILABLE]; SSH fallback RAM2143MiB/root48%/load0.32, healthy.
- PB/native/shared/IoT unchanged; no signing, terminal install, lease takeover or credential generation.

## Release / open verification
- PR35 accepted main920e544cae772cd11bd1a6927f59bfca388c2145; installed builder launcher ran
  only menubuilder-backend and menubuilder-frontend. Linux706passed/22skipped, quality passed;
  frontend78unit/build passed. Both launcher invocations exit0.
- MB digest sha256:99b88b0f639c52fa5ec41eea6206d08c2839b8cc6fa51291d5b43073b8db7871;
  running container411fdf19499c7bd337925c248c4635ea476190f412d3890c9adba2c96a3208a7,
  openapi200. Runtime compiled guard is FOR NO KEY UPDATE; drain calculator returns65 for a60s lease.
- Frontend artifact sha256:512b01ee6193e896a468543a04c1b132a2b6c51ab9476080ef51242934f97d8b;
  static deployment verified served index, nginx-default unchanged. Local CLI browser mock also
  verified failed-start wording/countdown/disabled retry/re-enabled manual retry.
- Production base Compose persisted from verified running MB digest via repository pin helper;
  remote task helper removed. Initial startup health refusal recovered through standard retry.
- PB068bfe2bc20f, app16d6668571e7c, nginx-defaultf57699a9f1ca, rabbitmq41777886db72 unchanged.
  No migrations, MQTT client edits, signatures or terminal file mutations performed by this task.
- Operator start/list requested after Ctrl+F5; live file-transfer acceptance remains open.
