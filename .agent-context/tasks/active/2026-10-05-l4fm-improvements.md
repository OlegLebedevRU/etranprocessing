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
