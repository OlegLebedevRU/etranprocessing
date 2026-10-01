# MenuBuilder: tenant-scoped reader событий900–999

## Контекст и intake

Владелец изменения — MenuBuilder API/internal client. Историю и tenant события
хранит IoT; схема etran/shared и ProcessingBackend не меняются. Будущий
consumer — l4mcp. База: origin/main `adbe67b`; отдельный чистый worktree.
Пользователь разрешил код, push в main и176→registry→production pull только
menubuilder-backend. Positive runtime search переносится в будущий E2E на773
tenant1; это не gate реализации MenuBuilder. Временный MCP token пока не нужен.

## Выполнено

- Новый GET /api/mcp/events/{device_id}, фильтры совместимы с IoT search.
- Авторизация отзываемого MCP token; current tenant обязателен даже для role1.
  Проверка доступа и SN/device повторяется на каждой странице; offline допустим.
- Только internal key/server X-Org-Id в IoT, без superuser override и user bearer.
- Ограниченная типизированная страница сохраняет payload446–448 буквально;
  некорректный ответ/недоступность IoT —503, не успешная пустая история.
- Отдельно исправлен существующий date-sensitive тест billing policy:
  его clock зафиксирован вне grace window; production billing code не менялся.

## Контракт

| Направление | Транспорт | Scope / совместимость | Ошибки |
|---|---|---|---|
| l4mcp → MenuBuilder | GET /api/mcp/events/{device_id} | additive; bearer/current tenant, limit1–100 |401/403/404/409/422/503 |
| MenuBuilder → IoT | internal GET device-events/search | IoT2cda32f; bounded read-only, без offset writes | upstream failures →503 |

Полный план, параметры и следующий круг тестов:
[план](../../../docs/menu_arch-l4mcp-user-events-plan.md).

## Проверки и ограничения

- Локальные targeted tests:25 passed; Ruff check/format app и Pyright app прошли.
- Первоначальный full suite:589 passed,1 failed — существующий billing test
  зависел от даты запуска. После фикса clock targeted test прошёл; полный
  повтор:590 passed,52 existing warnings. Биллинг в production не менялся.
- Secret review изменённых файлов: тестовые fixture-key/Bearer test и localhost
  не рабочие credentials; private env/key не сохранены. git diff --check прошёл.
- Production preflight: RAM available2154MiB, disk67%, load0.10; MCP Ops не
  доступен в session tools, транспорт SSH. Builder timer inactive.
- Новые MCP tools и native l4con не изменены. HTTP happy path с временным
  token и end-to-end event search выполняются в следующем MCP этапе.

## Выпуск

Развёрнуто2026-10-01 около19:38UTC. Source SHA
`4a730fb7ba3ddf5526839e1880d8d2168332ae39` принят в main; образ
`sha256:59e2bc7214d88cfd42418f61a62126a9c55dc3dcc633c72f08be18e5aa1bbafa`.
Builder176 повторил CI/backend checks и опубликовал образ. Deployer подтвердил
revision label, image ID и health после штатного повторного стартового poll.
На production новый маршрут в OpenAPI, без bearer401; restart-count0,
startup complete,0 ERROR/Traceback в окне проверки. Только menubuilder-backend
сменил ID; app1, l4mcp и остальные соседи сохранили прежние ID. При authenticated
E2E reader будет проверен на реальном новом событии; эта проверка не заявляется
выполненной сейчас.
Rollback — previous MenuBuilder image; миграции для этого изменения нет.
Рабочие773 и его службы не останавливались, событий не отправляли.
Worktree сохраняется для дальнейшего MCP этапа.
