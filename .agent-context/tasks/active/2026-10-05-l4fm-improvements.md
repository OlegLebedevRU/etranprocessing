# L4FM improvements — implementation in progress

## Task intake
- User authorised implementation of the full-stack review and selected Ant Design Explorer UI.
- Scope: MB frontend/BFF, PB policy/metadata, app1 lease/MQTT, native l4con, l4setup/l4superv.
- Owners: IoT common exclusive lease and MQTT; PB policy and transfers; MB browser authentication/UI;
  native agent Windows access; setup/supervisor managed Mosquitto configuration.
- Producer → consumer: browser → MB → IoT → srv/SN/fmc → l4con → dev/SN/fmr → IoT → MB.
- Invariants: same exclusive lease as console/video; no automatic transfer replay; file bytes only
  agent–S3–browser; agent HTTP through Leo4Proxy; uploads only ordinary desktop user; bounded fail-closed.
- UI: left Tree terminals → disks only; right pane folders/files, history/breadcrumbs; selection starts
  session automatically; changing terminal closes previous first; modal transfers block both panes.
- Validation: lease races and typed cleanup tests, protocol boundary tests, UI build/unit/browser tests,
  native x86/x64 builds; signed release and real-terminal evidence separately from local tests.
- Baseline: etran 93a07fba; previous review documentation in working tree retained.

## Implementation / verification
In progress. No claim of deployment or completed E2E. Update with actual evidence before handoff.

## FM v2 only — решение пользователя 2026-10-05

Поддержка FM v1 исключена из новой реализации. Readiness требует protocol_version=2,
fs.mqtt_navigation, fs.write_user, fs.drives и прежние обязательные capabilities;
старый агент отображается несовместимым и не допускается к acquire.
Листинг выполняется только через srv/{SN}/fmc → dev/{SN}/fmr (envelope v=2).
PB/MB не принимают kind=list и PB не принимает listing results. Stop/cancel всегда
ожидают коррелированного подтверждения завершения дочернего процесса; optional
confirmed_close удалён. Потеря ACK сохраняет deadline guard, не включает старый RPC.
RPC 7020, 7022 и action=stop в 7023 отклоняются; 7021 transfer и 7023 start/renew сохраняются.
HTTP prefix /v1 обозначает существующее пространство API, не поддержку протокола FM v1;
смена URL не требуется для обязательной проверки версии и capabilities.
Изменения локальные, без деплоя; общий implementation/release gate остаётся открытым.

## Explicit Mosquitto routes — 2026-10-05 implementation

Shared generator/migration now emits topic contract 3, without wildcard or overlapping bridge routes:
- outbound `dev/{SN}/`: `app`, `svc`, `evt`, `req`, `res`, `out`, `ctl`, `fmr` (QoS 1);
- inbound `srv/{SN}/`: `tsk`, `rsp`, `eva`, `cmt`, `ctl`, `fmc` (QoS 1).

Sources checked: l4con MQTT publishers/subscriptions, l4desk ctl, app1
core/config.py and core/services/device_task_processing.py (eva/cmt).
Presence app/svc retained payloads are unchanged. Bridge routes do not change retain semantics.
Known old own-SN wildcards are migration input only; emitted config has exactly 14 distinct routes.
Foreign-SN, unknown routes and remapping are rejected without replacing the existing config.
The active-config validator requires the exact route set, direction, QoS and SN; comments cannot
satisfy routes, duplicates/wildcards invalidate the configuration. Setup uses supervisor's
prepare-mosquitto path and therefore the same migration. Local builds/tests are not live broker evidence.

## Local verification — v2-only follow-up

- PB: ruff/format/pyright app passed; FM tests 26 passed (v1 readiness rejected,
  mandatory capabilities, HTTP listing/result schema rejection included).
- MB: ruff/format/pyright app passed; FM tests 7 passed, including rejection of
  legacy listing and optional unconfirmed close.
- app1: black/ruff selected changed files passed; lease/navigation tests 22 passed.
- Frontend TypeScript/Vite build passed; browser fixture run recorded separately below.
- l4con unified x86/x64 build and native tests passed, including blocked child stop
  through v2 fmc, ACK after process exit and retired RPC rejection.
- Mosquitto migration test passed for x86/x64: exactly 14 routes, no wildcard,
  idempotence, foreign/unknown route rejection preserving original, comment spoof rejection.

No signing, publishing or production deployment in this follow-up. These tests do not prove
real-terminal/Broker E2E. Outstanding from the broader review: complete migration health
rollback and custom ACL preservation, remaining native path-policy/access audit, unknown
commit/orphan recovery, user/read-mode diagnostics, full fault/soak matrix and signed release.

Final follow-up checks: all 7 Playwright browser fixture tests passed (Classic/L4Desk, home,
no lease on incompatible agent, exclusive conflict, close-before-switch, error preservation,
modal transfer and corrupt data). l4superv/l4install unified x86/x64 build and native tests passed.

## Release preparation 1.13.0

User authorised all-component deployment and post-sign registry publication followed by
remote l4setup upgrade on1000007 through l4mcp. Token source supplied by operator is
outside Git; never log it. Operator signing is the only pending manual action.
[MCP Ops Readiness: UNAVAILABLE] tools absent; SSH preflight healthy: production RAM2094MiB,
root49%, load0.31; builder RAM2741MiB/root60%/load0.07.
PB full269passed/1skipped, IoT545passed/7skipped, frontend80unit +7browser fixtures passed.
MB initial full run706passed/22skipped/1 existing WS cleanup timing failure; isolated
rerun passed, full rerun and builder gate pending. Native unified builds/setup tests passed.
Fixed policy-root bypass; terminal config/ACL now copied into staging with checked result
before swap. Failed migration/start/critical health during upgrade restores previous suite
only after stopping new services. Rollback fixtures pass. Signed1.12.1 backed up; sealed
unsigned1.13.0 prepared. Not signed/published yet. All secret-scan findings were scanner
marker strings and synthetic fixtures, no detected credential added.

MB full rerun: 707 passed/22 skipped. Operator signed1.13.0; 19 EXEs valid with timestamps,128 embedded files match staging. Setup SHA256 d9cb76c947d5358093e8b93bd4bd505259fb2987babcce705428c98184d71530.

## Production / publication evidence

- Root PR37 merged: b8c609effd9c39ef3fa124b6a515a7c3a349f1b9 (source873c7ed).
- IoT PR103 merged:6209faf870d9d05028255c9e92f30017a33be781.
- Standard builder tests/build/registry/pull completed; production revision/image verified:
  PB sha256:0124b851fb378b53710268a3614d7ba37dfda8da576e737e84737c498a4ac266;
  MB sha256:8e1a589c8d95bc625b6d7a662416428f1fc7549ebc7c6f543cce9f3c491cd3b7;
  frontend sha256:127cdefee6a13df6c724dfdeda858cc2213120a48776cd80be46d560b07d8a58;
  app1 sha256:36fb27a6826927d1e27527d61a6c26a3a165c05a26882cd2e8d4bff99167387e.
- Health checks passed after startup retries; frontend served index matched artifact.
  Nginx/RabbitMQ/Redis/media/l4mcp containers not recreated.
- FM v2 policy configured through deploy/configure_fm_v2.py; previous env backed up privately.
  Local drives=true, privileged_read=true verified in running PB; no credentials logged.
- RabbitMQ binding amq.topic→fm_result_v1 via dev.*.fmr, two consumers, zero backlog verified.
  Queue name version is an internal label; accepted envelope v=2 only.
- Strict publisher uploaded signed1.13.0 and checked complete HTTPS GET size/SHA256 for
  installer, manifest, checksums. Release record artifacts/l4tools/1.13.0.json.
- l4mcp token loaded privately from operator-provided path. Initial1000007 preflight ready;
  readonly version/config console request ran; its local stdout serialization failed encoding.
  Subsequent preflights reported terminal online but svc_online=false (l4con_offline).
  No download/task/install was dispatched. User asked asynchronously to inspect local services.
  Never bypass preflight or send commands through another terminal. Continue update on recovery.


## Live acceptance on 1000007 after operator installation

2026-10-05 20:00–20:08 UTC: operator installed suite 1.13.0 after removing
antivirus that had intercepted L4Con. l4mcp preflight ready; signed L4Con 1.12.0,
L4Superv 1.11.0 verified. L4Con/L4Superv/Leo4Proxy/Mosquitto running. Installer
summary target/installed 1.13.0, ready/exit 0, no rollback; certificate reused,
desktop agent and input available. Mosquitto contract 3 has 14 explicit own-SN
routes including fmc/fmr, no wildcard.

Live browser under authorised test account, L4Desk /files:
- Initial Обзор парка has no session; terminal 1000007 shown ready.
- Start returned active with C:, D:, G: roots.
- C:\, C:\l4tools\fm, parent C:\l4tools, parent C:\ navigation succeeded;
  renew/status remained healthy, no connection-lost error.
- Uploaded 45-byte fm-check-20261005.txt; modal disabled navigation, committed
  file appeared automatically without manual refresh.
- Download completed; local original and downloaded SHA256 both
  cd6b99af6c1b56604e779816ed9f58bb9a11c32dba4df2a99efd12df339f3462.
- Home closed first lease; immediate second start succeeded; home closed again.
  Subsequent console session admitted, demonstrating release of exclusive slot.
- Cleanup of the exact hash-guarded test file through l4mcp was BLOCKED by command
  security policy (exit 126). No bypass attempted. File remains at
  C:\l4tools\fm\fm-check-20261005.txt; operator cleanup needed.
- Test account logged out and browser closed. No new tool build/publication.

This proves the basic live v2 navigation/transfer/close path on this terminal;
full fault/soak matrix, privileged-read and user-token security acceptance, and
Classic live UI remain unverified in this run.


## Follow-up: Save As and Explorer presentation

Task intake: frontend owns destination selection/rendering; l4con owns the complete
sorted directory listing before MQTT pagination. PB/IoT/MB API contracts unchanged.
No bytes through servers; checksum validation precedes local write, uploads remain
ordinary-user only, no automatic transfer retry. Applied intake, frontend safety
and native-windows-tool-change skills; extra_service choice remains authorised.

Implemented native Save As on the initiating click, with browser-download fallback
only for missing API or explicit blocked/unsupported picker. AbortError cancels
without creating a transfer (also avoids bypassing sensitive-path denial). Local
write/close failures abort the writable and report failure, never silently switch
destination. Transfer timeout starts after the picker returns; lease renew continues.
Navigation remains modal until save completion/result acknowledgement.

Tree uses fixed 232px desktop column (including gutter), compact readiness badge,
address from terminal settings with ellipsis/tooltip and an indented second line;
search includes address. Nonready reasons remain visible. Folder rows use pointer.
File table max 560px with bounded name/size/action columns and full-name tooltip.

L4Con 1.12.1 sorts folders then files, StrCmpLogicalW name comparison with stable
case-sensitive tie-break, before the 64-entry slice. Enumeration memory capped at
65,536 visible entries (about 39 MiB); allocation/scan/limit failure returns no
partial listing. Directory changes between requests are not a snapshot guarantee.
Worker cancellation and owned Job termination remain in force. x86/x64 builds and
native tests passed, including 130 shuffled entries spanning a page boundary.

Suite 1.13.1 prepared from verified signed 1.13.0 baseline, which was backed up.
Only L4Con and setup require new signatures; other payload binaries preserved.
Setup regressions/build x86/x64 and 128 embedded file comparisons passed.
Signing pending; do not publish unsigned packet or republish 1.13.0.
Frontend 88 unit tests passed; initial browser run found a test-fixture collision
with read-only window.closed (fixed to saveClosed). Next run 10 passed and Edge
exited at newContext for unsupported test; isolated unsupported run passed.
Final visual/build verification and frontend deployment recorded below when complete.
[MCP Ops Readiness: UNAVAILABLE] connector absent; SSH resources healthy:
production available RAM2124MiB/root50%/load0.05; builder2741MiB/root61%/load0.00.

Final local evidence: TypeScript/Vite build passed. Playwright CLI visual checks at
1440px and 768px confirmed bounded layout; corrected intrinsic tree width/address
overflow. Download fixtures now await download completion before closing context;
all three save/picker-blocked/unsupported scenarios passed together. Other eight
browser scenarios passed in the preceding run; 88 unit tests passed. Tracked-text
secret scan matches were scanner patterns and synthetic fixtures, no credentials.

Frontend deployed through builder/registry/pull from merged PR39, main
09b22e8bf867db1bf4bcbd70559b585d93dccfcb, artifact
sha256:81c485847233e61a33980a3726da4c1d741b7ccb1f7894a45d4175109a866e3e.
Builder 88 unit tests/build passed. Production deployer verified revision and
served index; nginx-default not restarted. Independent public HTTPS check found
FilesPage-CkAKrx5T.js containing showSaveFilePicker and maxWidth:560,
SHA256 b45f4e147f7a0496b4c7c39ff7a5d75384dea898171a95ad44606565d3080592.
Local preview/browser closed. Tools 1.13.1 remain unsigned/unpublished; terminal
1000007 remains at verified 1.13.0 until operator signing and upgrade.


## Signed publication and remote installation 1.13.1

Operator confirmed signing. Verified all 19 timestamped EXE signatures and 128
embedded files against staging. Strict publisher uploaded 1.13.1 and verified full
HTTPS GET bytes for setup/manifest/checksums. Setup SHA256
 a17968be482e21e5c63dd9816d868a9a47438fe16424e817667be5edc2705682.
Publication record: artifacts/l4tools/1.13.1.json; source 7e8baa3, clean.

User explicitly requested update on 1000007 via l4mcp. Preflight ready. Staged
private version-pinned S3 installer object, downloaded on terminal via explicit
local Leo4Proxy (curl CONNECT), verified SHA256 and Authenticode there. No direct
terminal HTTP fallback and no file relay through PB/IoT/MB. Local staging directory
C:\ProgramData\L4Tools-Update-1.13.1-a17968be protected to SYSTEM/Administrators.
One-shot SYSTEM scheduled task ran signed setup --silent outside L4Con's owned Job,
allowing normal service drainage. No certificate reissue requested or performed.

2026-10-05T20:38:34Z install_summary: installed/target/installer 1.13.1, x64,
ready/exit0/finish, rollback none, cert reused, all four services running. Scheduled
task LastTaskResult=0. L4Con 1.12.1 signature Valid; Mosquitto contract3 retains
14 exact routes including fmc/fmr. Subsequent console request succeeded. PB
fm_agents confirmed version1.12.1/protocol2/filesystem_ready=true, heartbeat age4.8s.
Desktop was locked during install verification; live interactive FM navigation was
not repeated in this run. Existing pending_reboot warning preserved.

Removed completed scheduled task and exact temporary S3 object version (grant
invalidated). Signed installer retained in protected update cache. No lingering
update task or alternate network route. Installer/agent upgrade fulfilled.
