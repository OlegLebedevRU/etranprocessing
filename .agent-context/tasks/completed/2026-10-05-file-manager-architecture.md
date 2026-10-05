# FM architecture: редакция 2 и план реализации

## Контекст задачи
- Scope: только документационный анализ и проектирование, без runtime/code/deploy.
- Working tree от `d8719971fa143cd2e5b8a191adb43681a3f23aba`, без commit.
- Дата: 2026-10-05; runtime observation window отсутствует.
- Применены repo-intake-and-routing, architecture-decision-record,
  performance-and-resilience-risk-analysis и documentation-and-context-curation.
- Native/app1 исходники не исследованы, исключённые каталоги не просматривались.

## Выполнено
- [Основной документ](../../../docs/etran_arch-file-manager-remote-windows.md)
  переработан: общий lease, MQTT control, PB metadata HTTPS, S3-only bytes.
- Добавлены fail-fast/cleanup, state machine, Windows paths/commit, provider gates,
  UI wireframe/session UX, шаги F0–F8 и negative/E2E matrix, выпуск/rollback.
- Исторический S3 benchmark отделён от доказанного состояния; SDK snippets
  заменены контрактами без заявления выполненной реализации.
- Синхронизированы docs index, console/input проектные расширения и context
  карточки PB/lease/MQTT, добавлена [FM карточка](../../contracts/file-manager.md).

## Затронутые контракты
| Контракт | Producer → consumer | Совместимость |
|---|---|---|
| Общая session files | MB/PB → IoT → agent | Новое расширение; registry/fences и все старые acquire entrypoints требуют проверки |
| FM MQTT | IoT → agent | Предлагаемые 7020–7023; registry не проверен, новых runtime методов не заявлено |
| Agent HTTPS | Agent → PB | Новый metadata API, без файловых stream endpoints |
| Bulk | Agent ↔ S3 ↔ browser | Только прямой S3, server relay исключён |

## Изменённые проектные инварианты
- По уточнениям пользователя: FM занимает ту же монопольную сессию, что
  console/video; MQTT сигнализирует старт/стоп/renew; HTTPS агента идёт в PB;
  server byte proxy и fallback исключены.
- Изменения действующего runtime не выполнялись.
- Последнее уточнение пользователя: допустим полный отказ операции при проблемах
  доставки/целостности и ручной повтор. Resume/pause/partial recovery исключены
  из MVP; остаётся лишь уточнение неопределённого commit outcome без повторной записи.

## Проверено и оставшиеся проверки
- [x] Выборочно прочитан статический PB identity/policy/ingress и MB session/IoT client/console UI.
- [x] Первичные Microsoft/AWS/cloud.ru источники просмотрены, ссылки в основном документе.
- [x] Относительные ссылки проверены программно; отсутствующих targets нет.
- [x] `git diff --check` пройден; credential-pattern scan добавленного текста без совпадений.
- [ ] App1 source и native source audit: отдельные scope/project реализации.
- [ ] Provider checksum/size/zero-byte и browser integrity: F1 gates.
- [ ] Runtime lease exclusivity, MQTT, PostgreSQL migration и Windows/browser E2E: F2–F8.
- N/A: pytest/Ruff/Pyright/npm/native builds — только docs изменения.

## Evidence и следующие действия
- Уровень: документ и выборочный статический код; runtime не проверен.
- Secrets, tokens, file contents и private connection profiles не добавлены.
- F0: проверить registry и общий lease contract обоих проектов.
- F1: доказать S3-only integrity и limits; PB не скачивает Body даже для hash.
- До будущего изменения MQTT-клиента задать предусмотренный AGENTS.md вопрос о типе.
- Cleanup: временные файлы, credentials, внешние ресурсы и процессы не создавались.

## Context distillates
Обновлены PB, lease lifecycle, MQTT matrix, добавлен file-manager; все пометки
относятся к проекту, не выданы за результаты deployment/E2E.
