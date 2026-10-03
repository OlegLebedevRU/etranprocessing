# L4Desk terminal subscriptions — production release

## Контекст

- Пользователь явно разрешил production deploy через 176 builder → registry → production pull. Источник: чистый `main`, `8bb035944e709ace5b53df8d76586a4d596cfe89`, опубликован обычным Git push и независимо сверён.
- Дата: 2026-10-03, окно наблюдения 20:08–20:22 UTC. Владельцы: shared contract / ProcessingBackend migration / MenuBuilder subscriptions и UI.
- Release flow: установленный beta launcher `52e93a952defdca8eaa05b26dfb734db7c0697d5`, worker из source SHA. Сначала адресные `--build-only` трёх компонентов, затем адресный deploy PB → MB → frontend. Timer остался inactive.
- MCP Ops unavailable; SSH preflight READY: builder RAM available 2786 MiB, диск 57%, load 0.03; production 2119 MiB, 40%, load 0.11. Credentials/keys не выводились и не копировались в Git; env flags проверены по allowlist без раскрытия secret values.

## Выпущенные артефакты

Registry: `dev-leo4-ru.cr.cloud.ru/etran`. Все артефакты имеют revision/flow label `8bb035944e709ace5b53df8d76586a4d596cfe89` и уникальный тег с полным SHA.

| Компонент | Immutable digest | Build ID UTC |
|---|---|---|
| processingbackend | `sha256:ed5ab7e04efcd938399b2c74235fdbc2bffc1c122a0813209cb0552bb6edfd6c` | `20261003T201038058529Z` |
| menubuilder-backend | `sha256:89a74c5ca500040786ce889c83951ccb89912e6ae6f54b7b64c1e8d73f64575a` | `20261003T201214134335Z` |
| menubuilder-frontend | `sha256:bc639af33b0d6669a682d9380483b94f290402dabf622b584bf44d51a8c4f14a` | `20261003T201304240336Z` |

Deployer подтянул digest, проверил labels, сменил только два backend через `--no-deps --no-build`, сравнил running image ID с опубликованным. Frontend извлечён из registry image и опубликован как dist; Nginx не перезапускался. Persistent override и базовый Compose закреплены на новых digest. Перед изменением двух image строк сохранена закрытая backup копия Compose; структурно проверено, что иных изменений нет; `compose config --quiet` exit 0.

## Проверки и evidence

- [x] Release automation unit tests: 31 passed, exit 0. На builder PB: 145 passed; MB: 604 passed / 20 отдельные DB tests skipped без disposable DB; frontend: 70 passed + production build. Ruff, format check, Pyright — exit 0.
- [x] До публикации реализации отдельный PostgreSQL 18 прогон дал 20 passed; браузер с mock API — 4 passed. Подробности в [implementation handoff](2026-10-03-l4desk-terminal-subscriptions-handoff.md).
- [x] Alembic новым PB образом: `Running upgrade 028 -> 029`; production DB revision `029`. MB startup: 23 required tables и paid_until column доступны, schema check passed.
- [x] PB `/api/health` 200; MB `/openapi.json` 200. Оба running, restart count 0. Новые subscription routes присутствуют; прежние finance mutation routes не зарегистрированы.
- [x] Внешний `/api/auth/register/status`: 200, enabled=true; `/api/subscriptions` без credentials: 401. Frontend deployer сравнил отдаваемый Nginx index с новым dist.
- [x] YooKassa OFF в обоих backend. MB registration=true, required revision=029 по defaults: explicit env overrides для них отсутствуют. `.env` не менялся. Classic автоматически не включён в подписочную модель.
- [x] Реальные данные: 17 неудалённых L4Desk technical records, paid_until NULL у всех. Для profile-linked subset MB states: free=3, payments_disabled=4; оставшиеся technical records без соответствующего L4Desk tenant profile не включаются в финансовую модель. PB allowance проверена для всех 17 runtime links: 12 allowed/admin-active, 4 denied/admin-active, 1 allowance/admin-inactive (итоговая transport policy сохраняет AND с admin flag). NULL runtime links и mismatched IDs отсутствуют.
- [x] Admin runtime states до/после: false=871, true=1715; без изменений. Активных remote sessions до релиза не было. Новых FinLedgerTransaction с 20:14 UTC — 0.
- [x] ID всех 10 соседних контейнеров совпали с preflight, включая app1/l4mcp/media/brokers/обе proxy. Только PB/MB заменены.
- [ ] Реальная ЮKassa purchase и hardware MQTT/RTP/session-stop E2E не выполнялись: платежи выключены, dedicated production test credentials не выпускались. Проверки DB policy не являются hardware E2E.

Durable evidence хранит штатная автоматика: builder artifact JSON по source SHA, state checkpoint и releases journal; production component release JSON и previous-image records. Временные task stdout logs удалены после сохранения обезличенного результата. Ожидаемые connection-refused при первых health retries закончились успешным 200; deploy rollback не понадобился.

## Риски / восстановление

- Миграция только ADD nullable paid_until. Старые деньги не конвертированы, admin disable не изменён. Downgrade 029 намеренно запрещён; восстановление должно сохранить схему/сроки и использовать совместимый подписочный образ, без запуска прежних денежных workers.
- Для коммерческого включения нужны настроенные provider credentials/HTTPS return URL/receipt и согласованный `YOOKASSA_ENABLED` в обоих backend с restart. Включение флага не выдаёт подписки автоматически.
- Сохранены штатные release metadata, checkpoints, registry digests, previous images и frontend/Compose backup для recovery. Чужие данные/контейнеры не очищались.
- Реализация и production rollout завершены; дополнительные платежные/hardware проверки имеют явно ограниченный evidence level и не представлены выполненными.
