# Agent handoff: 17F grace runtime, tenant 10000

## Контекст задачи

- Задача / scope / владелец: короткая проверка commercial grace, blocked,
  stop и восстановления; MenuBuilder владеет ledger, entitlement и API.
- Репозиторий: `release/l4tools-1.8.2-beta-1`; исходные кодовые коммиты
  `469fcd4`, `9003400`, `e244bae`; runtime-отчёт в
  [MenuBuilder](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-GRACE-TESTPATH-01-report.md).
- Контур / дата: production video test05 и изолированный 17F backend,
  2026-09-27 UTC. Новые tenant/terminal не создавались.
- Intake: документационная фиксация уже выполненного сценария; без
  нового подключения к брокеру, теста кода или изменения сервера.

## Выполнено

- Отдельный отчёт MenuBuilder связывает source commits, test image,
  платежи, ledger, сессии, HTTP-статусы и наблюдение браузера.
- Карточка MenuBuilder обновлена по результату runtime; тестовое время
  возвращено к нулевому offset и пустому entitlement allowlist.

## Затронутые контракты

| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| IoT `device_online` | app1 event feed, event ID в отчёте | MenuBuilder monthly metering | Один charge на terminal/cycle; повтор идемпотентен. |
| Entitlement stop | MenuBuilder адресный tick/stop outbox | app1 lease/stream API и браузер | Сверка текущей stream epoch до stop; неподтверждённый хвост прощён. |
| Mock payment | Изолированный YooKassa mock | MenuBuilder ledger/entitlement | Повтор poll сохраняет одну posted transaction. |

## Изменённые инварианты

- Новых в ходе этой документальной фиксации нет. Production commercial
  flags и image не менялись в runtime-сценарии.

## Проверено

- [x] История Git содержит опубликованные исходники и детальную
  хронологию; runtime-отчёт явно различает два сеанса и сетевой сбой.
- [x] В предыдущем прогоне: 524 backend теста; test API 403 на blocked,
  201/204 после recovery; пользователь подтвердил stop живого видео.
- [ ] Независимая приёмка контроллером, доставка email, реальная ЮKassa,
  archive restore и полная deployment matrix — вне этого handoff.

## Наблюдаемое evidence

- Charge 3 / ledger transaction 8; video sessions 493 и 494;
  mock payment 5 / ledger transaction 9. Время и пределы каждого факта
  записаны в [отчёте](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-GRACE-TESTPATH-01-report.md).
- Уровень: локальные тесты + изолированный deployed API + production
  video stop с наблюдением оператора. Секреты, PIN и JWT в handoff отсутствуют.

## Риски и следующие действия

- Контроллеру 17F независимо сверить отчёт, image/ledger и оставшиеся
  corrective scopes перед новым black-box verdict. Старый отчёт 17F
  `BLOCKED_TESTS` не переименован в acceptance; 18A не открыт.
- Cleanup runtime: активных сеансов tenant 10000 нет; mock-платёж оставлен
  как проверяемая проводка, тестовые флаги возвращены к исходным.

## Обновить context distillates

- [MenuBuilder](../../components/menubuilder.md): обновлено по runtime
  evidence и ограничению приёмки.
