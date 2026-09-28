# L4D-17F-DOCS-FIX-01 — дополнительный black-box прогон

Статус: `BLOCKED_TESTS`. Это дополнение к неизменяемому
`L4D-17F-DOCS-FIX-01-report.md`, а не замена его verdict. Время ниже — UTC
2026-09-27. Исходники работающих приложений, БД и приватные env не
открывались. Для вызова закрытого mock control API прочитаны README и runner
тестового контура в локальном дереве. Проверки шли через обычный сайт,
изолированный test-backend и read-only интерфейсы контейнеров.

## Test fixture и границы

- Владелец `test05@platerra.ru`: `org_id=10000`, `role_id=5`; существующий
  неоплаченный tenant. Штатный owner onboarding создал терминал `1000007`
  (`terminal_id=3722`, `is_free=true`), оператор установил его на согласованный
  Agent и подтвердил online. `control/status` вернул agent online,
  desktop available, 1920×1080. Ранее созданный админским способом `1000006`
  остался offline и `is_free=false`; его видео не запускалось.
- Изолированный test-backend временно получил policy=true, allowlist
  `[10000]`, бесплатный лимит 120 секунд. Production backend и его
  коммерческие флаги не менялись. После прогона test-backend восстановлен с
  policy=true, allowlist `[1000]`, лимитом 600 секунд; значения проверены в
  работающем контейнере. Серверные файлы не менялись.
- Платёж проведён только через изолированный mock YooKassa. Полученная ссылка
  `example.invalid` не открывалась; реального списания не было.

## Наблюдения

| Проверка | Evidence | Verdict |
|---|---|---|
| Owner и free-terminal | До старта entitlement: `free`, баланс 0, `free_terminal_id=3722`, `today_usage_seconds=0`, `free_quota_seconds=120`, `can_start_sessions=true`. Test API stream lease 201 и release 204. | PASS для tenant 10000 и штатного onboarding. |
| Видео и порционный учёт | Обычный сайт запустил 1000007 около 14:01:29. Плеер: readyState 4, 1920×1080, текущее время выросло с 9 до 149 секунд. Во время открытой сессии usage стал 62, затем 124 секунды. Одна строка usage `id=5`, `source_project=MenuBuilder`, `video_seconds=free_seconds`, `billable_seconds=0`, без ledger transaction. | PASS для наблюдаемого учёта в открытой сессии. Кадровая динамика на экране Agent отдельно не измерялась. |
| Бесплатный отказ | При 124 секундах entitlement сообщил `can_start_sessions=false`, `reason_code=free_quota_exceeded`. Обычная видеосессия остановлена штатно около 14:04:29. Финальный usage 179 секунд, баланс 0; test API после stop вернул stream lease 403 с тем же кодом. `control/status`: stream stopped, lease inactive. | PASS для отказа **новой** сессии. Обычный сайт имеет выключенную policy, поэтому этот прогон не доказывает принудительное завершение уже открытого потока. |
| Mock payment и идемпотентность | В изолированном API payment `id=4` создан 201/pending на 1000 копеек. Mock control перевёл provider payment в succeeded; два одинаковых webhook вернули 200, poll вернул 200/succeeded. Платёж ссылается на одну posted ledger transaction `id=6`; две entries дают debit=credit=1000 копеек. Balance 1000 копеек, version 1. | PASS для повторной доставки webhook и poll в этом fixture; проверка через реальный YooKassa не выполнялась. |
| Paid continuation | После оплаты entitlement: active, first paid, `cycle_id=3`, `can_start_sessions=true`, баланс 1000 копеек при прежнем usage 179 секунд. Тот же stream lease для 1000007 дал 201, release 204. | PASS для admission после оплаты. |
| Hub read path | Под test superuser `sutest` frontend Hub обращался к `/api/v1/admin/hub/...` и получал 404; внутренние `/api/internal/v1/hub/...` отвечали 200. Внутренний finance overview отвечал 500; traceback указывает на `hub_service.py:504`, деление `Decimal` на `float`. Scoped reconciliation tenant 1000, окно 20 минут, без auto rebuild, вернул `matched`, mismatch 0; повтор того же operation id создал отдельный run. | FAIL для Hub UI и finance overview. Повторный run требует отдельной оценки контракта идемпотентности. |
| Correlation и архив | Tenant 1000: drilldown по terminal/session возвращал `matched`, mismatch пуст; по payment/registration — `TERMINAL_NOT_FOUND` и `PIN_PROVISIONING_MISSING`. Список archive manifests пуст, `total=0`; restore dry-run на реальном manifest недоступен. | PARTIAL: нет полного end-to-end доказательства correlation/archive. |

## Решение для каскада

`H-L4D-17F-DOCS-FIX-01-v1` по-прежнему не принимается; запись `ACCEPTED`
не создаётся. Сценарии free refusal → mock payment → paid lease теперь
подтверждены на новом бесплатном terminal. Открытые обязательные входы:

1. `L4D-17F-MB-HUB-FIX-01` (`MenuBuilder`): исправить публичный путь Hub
   либо frontend proxy, `Decimal`/`float` в finance overview и отдельно
   оценить повтор operation id и correlation filters; затем browser smoke.
2. `L4D-17F-MB-GRACE-TESTPATH-01` (`MenuBuilder`): безопасные короткие
   tenant-scoped периоды и проверка active → grace → blocked → stop без
   изменения других tenant; обязательна независимая сверка ledger.
3. `L4D-17F-MEDIA-ARCHIVE-EVIDENCE-01` (`l4media`): immutable manifest,
   restore dry-run и 120-минутная route/TTL трасса для уже проверенного
   видеотракта.
4. `L4D-17F-DEPLOY-EVIDENCE-01` (`l4desk-service`): источник/image/schema/
   flags/rollback matrix для Agent, ProcessingBackend, IoT, media и
   MenuBuilder с проверенными digest.

До закрытия этих входов 17F остаётся `BLOCKED_TESTS`, а 18A не запускается.
