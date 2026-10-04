# l4mcp: аудит истории пользовательских событий IoT

## Контекст и границы
- Задача: проверить базовую ветку и runtime IoT перед доработкой l4mcp; реализация и деплой не выполнялись.
- IoT: `origin/master` / HEAD `769d83e2bd02eb1d61c2e778957b1830a0463143`, чистый checkout.
- etranprocessing: актуальный `origin/main` `adbe67bcaba3175c05aae7663a1eed5e628f0796`; пользовательский рабочий checkout сохранён.
- Runtime: 2026-10-01, 12:34–12:36 UTC. Revision label app1 равен IoT baseline; SHA-256 пяти файлов API/service/repository/schema совпали с Git blobs baseline.
- Авторизация внутреннего сервиса использована только внутри процесса контейнера, из штатных настроек; ключи не выводились и не сохранялись.
- Пользователь подтвердил: 900–999 — новые кастомные пользовательские/MCP-события; фиксированной таблицы смыслов нет.

## Проверенные контракты
| Контракт | Код baseline | Runtime | Вывод |
|---|---|---|---|
| `GET /api/internal/v1/device-events/` | device_id, events_include/exclude, Page; service не передаёт org_id в repository | 773: tenant 1 и посторонний 2147483647 получили HTTP 200, total=9, одинаковые строки | Tenant isolation отсутствует |
| `GET /api/internal/v1/device-events/fields/` | service/repository не ограничивают org_id | Посторонний tenant получил HTTP 200 и одну строку 997/tag448 | Аналогичная проблема |
| `GET /api/internal/v1/device-events/incremental` | Фильтр DeviceOrgBind.org_id; cursor last_event_id; чтение обновляет общий offset устройства | Посторонний tenant получил HTTP 200 / [] | Tenant проверяется, но API не является независимым read-only cursor |
| Внутренний ключ | Проверка настроенного секрета | Без ключа HTTP 403; configured=true | Авторизация сервиса работает, tenant-проверку не заменяет |
| Публичный perimeter | Документ обещает блокировку internal | Внешний запрос без авторизации на :3000 получил HTTP 401 | Этот probe не доказывает заявленный 403 и безопасность всех внешних маршрутов |
| Сохранение 9xx | int event_type_code + JSONB payload, без реестра новых типов | 9 событий 9xx терминала773; 446/447/448 доступны, 997 содержит 1024 UTF-8 байта, 996 содержит int32 max | MQTT ingestion / DB schema менять не требуется |

Источники IoT: `app-service/api/internal_v1/device_events.py`,
`internal_depends.py`, `core/services/device_events.py`,
`core/crud/dev_events_repo.py`, `core/schemas/device_events.py`,
`core/models/device_events.py`, `core/services/device_events_collect.py`.

## Существенные ограничения
- `events_include` позволяет уже сейчас выбрать 900–999; поиск по значению 448, временные границы и независимая keyset-пагинация в текущем list API отсутствуют.
- List сортирует только по created_at DESC без id как tie-breaker. Не обещать отсутствие пропусков при page-based обходе живой истории.
- Incremental обновляет tb_device_event_offsets даже при явно переданном last_event_id. Положительный runtime вызов не выполнялся, чтобы не менять offsets других потребителей.
- Runtime DevEventOut: id, device_id, event_type_code, dev_event_id, created_at, dev_timestamp, payload. Документ REST дополнительно заявляет поля sn/timestamp/received_at/correlation_id, которых DTO не возвращает. Internal документ ошибочно называет cursor last_id; код принимает last_event_id.
- Gauge-типы runtime не пересекаются с 900–999. Payload446 остаётся строкой, UUID448 берётся из payload["300"][0]["448"], не из отдельного верхнего поля.
- DevEvent не хранит tenant события, DeviceOrgBind не хранит время назначения. Проверка текущего owner не отделяет события прежнего tenant после переноса терминала.
- Архив runtime выключен. Есть ручной cleanup старше30 дней; его выполнение/расписание не проверялись. Архивный exporter для DevEvent формирует tenant_id=null. Долговременную tenant-safe историю/архив обещать нельзя.
- Новый MCP event reader должен проверять active tenant токена даже для роли1. Существующий console preflight допускает superuser-доступ к другим организациям через _verify_device_access; автоматически переносить это правило на новый reader нельзя.

## Уточнённый план
1. IoT safety: ограничить list и fields текущей DeviceOrgBind организацией в SQL. Shared service используется internal и public API; negative tests нужны для обеих границ.
2. IoT минимальное расширение чтения: поиск по внешнему UUID448 и явный cursor, не меняющий offsets; фильтрация и лимит в SQL, стабильный порядок. Предпочтительно расширить существующий list API опциональными параметрами, сохранив старый формат. Зафиксировать точный HTTP-контракт до реализации.
3. MenuBuilder: endpoint для MCP истории, проверка актуального API token/роли/active tenant/терминала; сверка device_id/SN и IoT binding; internal client с серверным X-Org-Id, без роли superuser и пользовательского org override; отказ при несовпадении двух контуров. Не запрашивать online/console lease для чтения истории.
4. l4mcp: correlation UUID в запуске/результате, read-only history tool с bounded cursor и полями 446–448; документировать multi-step workflow, user-defined event codes, CLI exit vs447 vs DB persistence. Новых MQTT-соединений и таблиц для первого этапа не требуется.
5. Проверить tenant mismatch, revoked token, повторное чтение без изменения offsets, concurrent history, payload oversize, позднюю доставку и историю offline терминала. Выпуск по стекам: IoT → MenuBuilder → l4mcp; native l4con не меняется.

Политика истории при переносе терминала требует решения владельца: текущему owner вся история либо tenant на момент события. Второе решение выходит за минимальную SQL/API-доработку, поскольку нужная историческая принадлежность не хранится.

## Проверки и cleanup
- Выполнены только Git/read-only code audit, SSH inspect и ограниченные HTTP GET; один DB SELECT binding773 в read-only транзакции с rollback.
- Новые события, команды терминалу, изменения offsets, конфигурации, сервисов и БД не создавались.
- Python tests/linters не запускались: код не изменялся. Полная E2E MCP-chain ещё не реализована и не проверена.
- Scratch files и временные credentials не создавались. Предыдущие пользовательские тестовые ключи не использовались.
