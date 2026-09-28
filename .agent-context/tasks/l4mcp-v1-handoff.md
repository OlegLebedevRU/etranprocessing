# Handoff: L4mcp v1

## Контекст задачи
- Владелец: MenuBuilder (MCP proxy, токены, консоль, PIN, UI); IoT app1 — неизменённый producer состояния и diagnostics; ProcessingBackend — только прежний владелец миграций.
- Изолированная ветка `feat/l4mcp-v1`, основание `a361086`.
- Ограничения: app1 не изменять; не вводить новую финансовую схему; не создавать миграцию; запросы только по точному терминалу или ограниченному диапазону.

## Выполнено
- Перенос FastMCP в MenuBuilder/l4mcp; PIN через штатный billing endpoint; API-токены в существующей таблице.
- Консоль с обязательной проверкой sys/online/svc_online и штатной арендой.
- Страница MCP с выдачей/отзывом токена и настройками клиентов.

## Затронутые контракты
| Contract | Producer | Consumer | Совместимость |
|---|---|---|---|
| IoT device status + sys/svc_connect | app1 без правок | MenuBuilder/L4mcp | Существующий контракт, точечный запрос |
| Console lease + diagnostics WS | MenuBuilder/app1 без изменения протокола | L4mcp | RPC 7001/7002, штатный usage |
| PIN billing | MenuBuilder | L4mcp | Существующий endpoint |

## Проверено
- [x] MenuBuilder/backend: Ruff check/format и Pyright — без ошибок. Полный pytest: 558 passed, один устаревший тест роли 3 заменён по новому контракту; адресный повтор 6 passed.
- [x] MenuBuilder/l4mcp: Ruff check/format, Pyright и pytest — 5 passed; список 10 MCP tools загружается.
- [x] MenuBuilder/frontend: `npm run build` — exit 0.
- [x] Compose-файлы: YAML разобран без синтаксической ошибки.
- [ ] Контейнерная интеграция и browser smoke после штатного выпуска.
- [ ] Production E2E: роли, status gate, консольный usage и PIN billing.

## Риски и следующий шаг
- Production runtime image MenuBuilder совпадает по SHA-256 с базовой ревизией ветки; серверные source-файлы старее образа. Не использовать серверные source-файлы как источник сборки.
- Серверный `/home/user1/compose.yaml` содержит сервис Redis, которого нет в локальном compose, а также приватные значения Nginx inline. Нельзя перезаписывать его целиком локальным файлом: перед выпуском нужен адресный план сохранения Redis и переноса секретов в приватный env.
- Переход nginx и контейнеров должен быть атомарным; приватные адреса сервисов задаются при деплое. Сервер в этой задаче пока не изменялся.
- При сбое release аренда остаётся до штатного TTL; проверить по runtime логам.
- Отдельного хранилища курсора вывода в V1 нет.
