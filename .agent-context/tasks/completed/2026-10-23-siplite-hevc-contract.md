# siplite HEVC: новый контракт интеграции

## Контекст и intake

- Scope: docs-only, новый контракт для camera relay siplite; firmware, MQTT client,
  app code, frozen contract, production и camera settings не меняются.
- Дата: 2026-10-23; working tree без commit, локальный HEAD b0a4545816ccf8cc0267719e8d4bb7f005d1c007.
- Владельцы: camera/siplite — готовый поток и relay; BFF/IoT — auth/lease/control;
  l4media — service API/Redis/Janus; desktop encoder — l4capture.
- Producer → transport → consumer: camera → siplite → TLS/L4RTP/1 → ingress → Janus → browser.
- Инварианты: lease/identity/auth, no transcode, no automatic HEVC fallback/restart,
  desktop compatibility; проверка документа по frozen schemas и evidence.

## Выполнено

- Новый `docs/etran_arch-siplite-hevc-integration-contract.md`, ссылка в docs index.
- H.265 зафиксирован основным режимом; H.264 — явный совместимый режим/legacy.
- Настройки камер отделены от desktop encoder требований и текущих server bounds.
- Разработан порядок camera schema extension с неизменными desktop validation/
  profiles и согласованными readers/negotiation до camera writers.
- Приложены точные schema/OpenAPI snapshots и derived camera payload с provenance.
- Обновлена компонентная карточка l4media. Старый внешний план не перезаписывался.

## Контракты и evidence

| Граница | Producer → consumer | Compatibility |
|---|---|---|
| Media service | BFF → l4media | Baseline hevc-contract/2.0.0 без изменения |
| Camera extension | camera adapter → server validators | Проект; revision/negotiation фиксируются при реализации |
| Desktop | l4capture → server | Требование сохранить существующие v1/v2 формы и ограничения |

Источники: `D:/.codex/hevc-plan/siplite-task60/`, TASK-40/60 handoffs/acceptance.
Server source dcd3ea1c04a6be1e3fe3047f3d5a3bec8747081d. Frozen aggregate
f065b967cd10c2c709fb394ef9e6c2ecf74f77de060d88c9a821cc9069766ac4.
Историческое runtime evidence не объявляется новой проверкой production.

## Проверка и границы

- [x] JSON Schema Draft 2020-12 проверена; camera START валиден с FormatChecker.
- [x] Отрицательные варианты Main10/4K отклонены текущей frozen схемой.
- [x] Schema/OpenAPI побайтово совпадают с исходным пакетом; provenance SHA-256
  и относительные ссылки нового документа корректны.
- Проверено offline через jsonschema 4.26.0, exit 0; это проверка документационных
  артефактов, не product tests, runtime/decode или реализованного расширения.
- Firmware siplite, реальный camera sample и camera schema extension не реализованы.
- Нет нового MQTT/HTTP runtime probe, deploy, pytest/linters/frontend/native build.
- Нужны фактические camera settings/outputs, согласованная machine schema delta,
  camera producer adapter и E2E с регрессией H.264/l4capture.
- Cleanup: создавались только постоянные документационные файлы; credentials,
  sessions, фоновые процессы и server artifacts не создавались.
