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
- [x] Контейнерная интеграция и browser smoke: backend image 4e79e80 прошёл 557 тестов на сервере, L4mcp `/health`=200, публичный MCP `initialize` и `tools/list`=200, страница роли 5 работает.
- [x] Production E2E: роль 5 tenant 10000 получила тестовый API-токен; preflight 1000009 подтвердил `sys=windows`, `online=true`, `svc_online=true`; команда `echo L4MCP_SMOKE_OK` вернула exit 0, аренда после неё отсутствует. Тестовый токен отозван.
- [ ] Роли 1/3 и отказы 2/4, PIN-инструменты с реальным выпуском до пяти кодов не проверялись на production.

## Риски и следующий шаг
- Production runtime image MenuBuilder совпадает по SHA-256 с базовой ревизией ветки; серверные source-файлы старее образа. Не использовать серверные source-файлы как источник сборки.
- Серверный `/home/user1/compose.yaml` содержит Redis, теперь перенесённый в локальный compose, а также приватные значения Nginx inline. Нельзя перезаписывать серверный файл целиком: для текущего выпуска подготовлен адресный план в [`docs/ops_run-l4mcp-release.md`](../../docs/ops_run-l4mcp-release.md), а перенос существующих секретов в приватный env остаётся отдельной операцией.
- Выпуск 4e79e80 установлен на production по явному подтверждению пользователя. Старый `mcp-pin-server` остановлен, оставлен как rollback; `app1` не менялся.
- После выпуска пользователь сообщил о дублировании символов в браузерном удалённом управлении. Исправлена отправка `text` при `keyup` в frontend; сборка прошла, но отдельный hotfix ещё не установлен и не подтверждён на терминале.
- Отдельно наблюдались периодические HTTP 400 на input lease 1000009 при успешном повторе; тело отказа утрачено, точная причина не установлена. В production логах есть пары 201/400.
- При подключении Codex выявлена наследуемая cookie-проверка nginx на `/api/mcp/`: браузерный тест с cookie проходил, bearer-only клиент получал HTML 401. В следующем изменении отключена проверка cookie только внутри этого location; проверка bearer-токена остаётся в MenuBuilder. Глобальный `auth_jwt_location COOKIE=accessToken` сохранён. Локальная настройка Codex исправлена на имя переменной `L4MCP_TOKEN`, старый ошибочно помещённый в config токен отозван.
- При сбое release аренда остаётся до штатного TTL; проверить по runtime логам.
- Отдельного хранилища курсора вывода в V1 нет.
