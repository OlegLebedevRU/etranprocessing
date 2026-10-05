# File manager: общий сеанс и S3-only передача

## Explorer follow-up — 2026-10-05

Frontend 09b22e8 deployed: Save As with safe browser fallback/cancel, compact tree
with readiness badge and address, bounded table, pointer folder rows. l4con 1.12.1
sorts directories before files/natural names before pagination; suite 1.13.1 signed,
published and installed on 1000007 via l4mcp. Installer ready/exit0, services running,
fresh PB registration confirms l4con 1.12.1/protocol2/filesystem_ready. Interactive
FM navigation after this upgrade was not repeated (desktop locked).

## Current deployment — FM v2, 2026-10-05

PB/MB/frontend deployed from b8c609effd9c39ef3fa124b6a515a7c3a349f1b9;
app1 from6209faf870d9d05028255c9e92f30017a33be781. Tools1.13.0 signed/published;
installation on 1000007 verified through l4mcp. Only v2 admitted. fmc/fmr binding
and two consumers verified, bytes remain S3-only through Leo4Proxy. Local drives
and privileged read enabled; upload ordinary desktop token only. Live L4Desk v2
start, roots, navigation/parent, upload/autorefresh, download/hash and close/reopen
passed on 1000007. Full fault/soak and privilege tests remain open.
[Evidence](../tasks/active/2026-10-05-l4fm-improvements.md). Older sections below
describe historical implementation/review states.

Серверная часть deployed, 2026-10-05; suite1.12.1 signed/published. Live review 1000007:
первая upload попытка failed, ручная повторная upload/download прошла с совпадением SHA-256;
полная fault matrix не выполнена. [Аудит и целевой план](../../docs/etran_arch-l4fm-review-and-improvement-plan.md).
[Production evidence](../tasks/active/2026-10-05-file-manager-production.md).
Пользователь подтвердил extra_service и tools/l4con.
IoT baseline origin/master 8c2be80, отдельный checkout D:/work/iot.leo4.ru/iot-rpc-rest-app-fm.

- IoT владеет единственной общей lease: files конфликтует с console/stream/input/view даже у одного owner.
- Start/renew/stop/list/transfer/cancel: MQTT RPC 7020–7023; только IDs/TTL, без URL/path/bytes.
- PB: agent mTLS HTTPS, instance/cert-bound tickets, manifests, bounded results, HEAD/checksum/version, commit/reconcile.
- MB: JWT/tenant/device policy, UUID browser view owner, metadata BFF; отдельный /files в Classic и L4Desk.
- Файловые байты исключительно agent–S3–browser. Нет server relay/fallback/hash Body.
- Suite1.12.1: все terminal HTTP через local Leo4Proxy; PB metadata через его mTLS,
  S3 HTTPS через policy-bound CONNECT без terminal cert. Общий MQTT/RTP/FM deny закрывает active tunnel.
  PB требует fs.proxy; setup автоматически создаёт C:\l4tools\fm, отдельный API env не нужен.
- Single PUT 0–64 МиБ; SHA-256, versioned S3, GET закреплён за проверенным VersionId.
- Fail-fast: ручное повторение с нуля, без resume/pause. Upload только CREATE_NEW/no-overwrite.
- Revoked files lease удерживает слот до первоначального deadline + 5 с. В review обнаружена
  статическая acquire/revoke race: WATCH только active key не защищает изменение lease hash;
  отсутствие воскрешения через acquire пока не доказано и требует исправления.
- FM start lock fix deployed: MB active-admission guard uses NO KEY UPDATE, permitting PB FK KEY SHARE
  insertion while serializing terminal changes. Failed start/stop/conflict expose safe drain countdown;
  UI refuses retry until it elapses. [Incident evidence](../tasks/active/2026-10-05-fm-start-lock.md).
- Неизвестный commit блокирует transfer до protected receipt reconciliation; receipt не возобновляет запись.
- S3 cleanup: обязательный provider lifecycle, delete worker не реализован. Crash staging orphan cleanup остаётся gate.

[Архитектура, API, F0–F8, release gates](../../docs/etran_arch-file-manager-remote-windows.md).
[Evidence и незавершённые проверки](../tasks/active/2026-10-05-file-manager-implementation.md).

Целевой v2 (не реализован): отдельные `srv/{SN}/fmc` / `dev/{SN}/fmr`, MQTT navigation
без per-list PB round trip, общий lease и PB transfer authority сохраняются. Новый l4setup
обязан автоматически заменить старый Mosquitto config актуальным с validation/rollback;
l4superv использует тот же versioned contract. Upload только обычным desktop user,
download допускает policy-bound read-only privileged broker. Explorer UI: терминалы по номеру,
autolist, refresh после commit, drives. [Review handoff](../tasks/active/2026-10-05-l4fm-full-stack-review.md).

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
