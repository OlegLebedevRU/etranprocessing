# File manager: общий сеанс и S3-only передача

Локальный кандидат, 2026-10-05; не deployed/E2E. Пользователь подтвердил extra_service и tools/l4con.
IoT baseline origin/master 8c2be80, отдельный checkout D:/work/iot.leo4.ru/iot-rpc-rest-app-fm.

- IoT владеет единственной общей lease: files конфликтует с console/stream/input/view даже у одного owner.
- Start/renew/stop/list/transfer/cancel: MQTT RPC 7020–7023; только IDs/TTL, без URL/path/bytes.
- PB: agent mTLS HTTPS, instance/cert-bound tickets, manifests, bounded results, HEAD/checksum/version, commit/reconcile.
- MB: JWT/tenant/device policy, UUID browser view owner, metadata BFF; отдельный /files в Classic и L4Desk.
- Файловые байты исключительно agent–S3–browser. Нет server relay/fallback/hash Body.
- Single PUT 0–64 МиБ; SHA-256, versioned S3, GET закреплён за проверенным VersionId.
- Fail-fast: ручное повторение с нуля, без resume/pause. Upload только CREATE_NEW/no-overwrite.
- Revoked files lease удерживает слот до первоначального deadline + 5 с; Redis CAS не воскрешает её.
- Неизвестный commit блокирует transfer до protected receipt reconciliation; receipt не возобновляет запись.
- S3 cleanup: обязательный provider lifecycle, delete worker не реализован. Crash staging orphan cleanup остаётся gate.

[Архитектура, API, F0–F8, release gates](../../docs/etran_arch-file-manager-remote-windows.md).
[Evidence и незавершённые проверки](../tasks/active/2026-10-05-file-manager-implementation.md).
