# L4Update — принятая архитектура, не реализовано

## Release foundation — этап 1

Добавлен [l4release](../../l4release/__main__.py): plan/prepare/release/verify,
keys init, один build config, checkpoints и подпись через существующий PS.
[Руководство](../../tools/release/README.md) и
[packet](../tasks/completed/2026-10-06-l4release-stage1.md) фиксируют проверки/ограничения.
Worker/layout/RPC/catalog ещё не реализованы. Full access устранил отказы OS gates;
native builds/tests и unsigned packaging PASS, diagnostic timestamped signing PASS.
OpenH264 vendor восстановлен и pinned. Signed candidate 1.13.2 опубликован:
19 EXE/оба payload проверены, anonymous full GET трёх файлов PASS.
Terminal gate/promote пока явно отклоняются.

## Роль и границы

Независимый SYSTEM worker обновления tools suite. Единственный MQTT-адаптер —
L4Con; backend UI вне scope. Публичный Registry, HTTP через Leo4Proxy.
Нет миграции C:\l4tools; bootstrap новой раскладки через l4setup.
MB/PB не меняются; IoT требует минимального допуска новых методов/events.

## Контракт и инварианты

- RPC7031(version=точная/latest, target=suite/updater) достаточен для всей операции.
- Operation ID = исходный 7031.task_id; 7032/7033 адресуют его параметром.
- 7031 завершается на durable accepted, не на факте установки.
- Обязательный барьер REQ/RSP + свежий EVT/EVA до изменения и после переключений.
- Сначала Leo4Proxy, затем Mosquitto, без одинаковых MQTT client ID одновременно.
- Два окна watchdog: связь и остальные; Mosquitto предел 5 минут.
- Новая связь + старые tools обязательно обратно совместимы, gate без waiver.
- Минимальный bootstrap rollback-helper только для supervisor, не обновляется
  штатно; изменение только по обоснованному решению владельца.
- Updater обновляется отдельно переключением указателя под страховкой L4Superv.
- Один тип EVT и один числовой тег со всем объектом; простой ограниченный outbox.
- Release dirs/явные SCM пути в Program Files, data/config/logs в ProgramData.
- Аккаунты служб сохраняются; ACL проверяются от их имени до остановки.
- Один встроенный RSA-3072 ключ, SHA256/PKCS1v1.5, detached signatures;
  ревизия/expiry каталога, без root.json и авто-ротации.
- Проверенные пары → прямой переход/кратчайшая цепочка. Cleanup ≤X защищает
  активные компоненты, резерв, незавершённые операции и pinned версии.

## Проверки и flow

Проверка комплекта без остановки → один реальный переход. Fault evidence
переиспользуется только при неизменных соответствующих входах/профиле.
Suite: подготовка → связь → остальные → supervisor → итог.
При timeout один откат; нет внешних ответов на старой связке → connectivity_unconfirmed.
Частично успешное применение отражает фактические версии, не ложную полную suite.

## Конвейер и стенд

Целевой uv Python release/sign/setup/publish/gate/promote с checkpoints.
sw_sign.env игнорируется Git, секреты не попадают в artefacts/logs.
Основной стенд 773/tenant1 — текущая машина, оператор может вмешаться.
Другой setup-стенд не назначен и не блокирует remote gate.

## Источники и актуальность

- [Авторитетная архитектура](../../docs/term_arch-l4update-flow.md).
- [Handoff](../tasks/completed/2026-10-06-l4update-architecture.md).
- Требование: диалог владельца, 2026-10-06; код l4update не написан.
- Статический IoT аудит: HEAD 2ffa1403, REQ completed→NOP/zero UUID,
  EVT/EVA collector; production не проверен.
- Открыто: event code/tag/API schemas, полный path/account audit, budgets,
  минимальный rollback формат/синхронизация и release module placement.
- Runtime/native build/API/MQTT/deploy evidence по новой архитектуре отсутствует.
