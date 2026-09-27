# L4D-17F-MB-GRACE-TESTPATH-01 — handoff runtime-проверки

Статус предметного корректирующего шага: `VERIFIED_RUNTIME`. Это evidence
для повторной black-box приёмки 17F, а не `ACCEPTED` основного 17F и не
разрешение на запуск 18A.

## Task intake

- Владелец финансового ledger, entitlement, API и stop outbox —
  MenuBuilder. IoT event feed производит подтверждённый `device_online`;
  app1 принимает адресный stream/lease stop; браузер наблюдает результат.
- Scope: существующий tenant 10000, платный дополнительный терминал
  1000006 и бесплатный основной 1000007. Production commercial flags,
  исходный billing anchor и реальная дата PostgreSQL не менялись.
- Проверяемые инварианты: один monthly charge на terminal/cycle,
  идемпотентный повтор event; отказ нового lease после grace; stop только
  текущего stream epoch; подтверждённые интервалы учитываются, а
  неподтверждённый хвост освобождается в пользу клиента; mock-платёж
  создаёт одну проводку и восстанавливает право на сеанс.

## Исходники и доставка

| Что | Ревизия / fingerprint |
|---|---|
| Tenant-scoped test clock и адресный tick | `469fcd439e2e8bed9325d508c3eb90c2efdc12d5` |
| Stop текущего app1 stream epoch и metering closure | `9003400249a52518ef1a469d4e84ef5ae932aaac` |
| Контролируемый 409 для не внесённого в `l4desk_terminals` терминала | `e244bae6ebc2aac9ba678f10bb92da82dbfa7357` |
| Изолированный image после исправления | `sha256:247cc1403d08f50ec0f9cdf1d55d2f35f8fc27f34a9b40d398a09701f21603b1` |
| Production MenuBuilder image на момент проверки | `sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090` |

Исправленный test image собран из Git archive `e244bae`, SHA-256 архива
`ebc15c0c935cb3b24cc021147abafc06d4ebc853e3137b85dbdaab7a97d7d2c4`.
Production image не обновлялся. Итоговый test config: policy=true,
free quota tenant `[1000]`/600 секунд, entitlement test allowlist `[]`,
offset 0, IoT consumer=false. Архив исходников оставлен как release
provenance, временный tar и локальный SSH-туннель удалены.

## Факты прогона, 2026-09-27 UTC

| Проверка | Наблюдение | Вывод |
|---|---|---|
| Подтверждённый online и начисление | IoT event `evt_28f576a5b34c4ec8a1972452bdee37ca` в 15:54:39 UTC относится к 1000006/tenant 10000 и возник после anchor. После регистрации существующего терминала штатным коротким видеосеансом charge 3 списал 10000 коп. через ledger transaction 8. Повтор события вернул те же charge 3 и transaction 8; баланс −9000 коп. | Одно начисление и идемпотентный повтор. |
| Предусловие metering API | До первого штатного видеосеанса администраторский 1000006 отсутствовал в `l4desk_terminals`; прямой metering вызов получил 500/FK, транзакция откатилась. После `e244bae` smoke с отсутствующим terminal 999999 вернул 409 `terminal_not_enrolled`. | Ошибка преобразована в явный отказ до проводки; production image ещё без этого исправления. |
| Первый переход к blocked | Сеанс 493 начался в 16:10:28 UTC; сетевой сбой прекратил видео до тестовой границы. Tick вернул `blocked`, но provider lease уже отсутствовал; локальная запись `failed`, причина `entitlement_blocked`, недоказанный хвост прощён. | Отказ и waiver проверены; этот сеанс не доказывает stop живого потока. |
| Повторный stop живого видео | Сеанс 494 начался в 16:24:52 UTC. Tick до границы: `grace`, `sessions_stopped=0`; после границы: `blocked`, `sessions_stopped=1`. Текущий app1 stream остановлен; сеанс закрыт в 16:28:49 UTC с причиной `entitlement_blocked`. Пользователь подтвердил остановку изображения в браузере. | Runtime stop подтверждён на живом потоке. |
| Новый lease после блокировки | Test API под владельцем test05 вернул HTTP 403, `entitlement_blocked`; активных сессий tenant 10000 после stop нет. | Admission закрыт. |
| Восстановление | Изолированный mock ЮKassa: payment 5 на 10000 коп. стал `succeeded`. Два poll сослались на одну ledger transaction 9. Баланс +1000 коп., entitlement `active`; новый lease POST 201 и DELETE 204. | Paid continuation и идемпотентность poll подтверждены без реального платежа. |

Дневной агрегат бесплатного 1000007 после stop: 879 source/video seconds,
0 posted kopecks. Он включает более ранние тесты дня и не является
отдельной детализацией сеанса 494. Уведомление поставлено в очередь;
доставка email не проверялась.

## Проверки и граница передачи

- Локально для изменённого MenuBuilder: `uv run pytest -q --tb=line
  --disable-warnings` — 524 passed; `uv run ruff check --fix app tests`,
  `uv run ruff format app tests`, `uv run pyright app` — успешно.
- В работающем test image: отрицательный metering вызов 409;
  entitlement после восстановления `active`, баланс +1000 коп.,
  активных сессий tenant 10000 — 0. Production image остался прежним.
- Широкий `pyright app tests` ранее содержал ошибки в неизменённых тестах;
  этот runtime-отчёт не выдаёт его за успешный. Реальная ЮKassa,
  доставка email, archive restore и полная deployment matrix не
  проверялись.

Для контроллера 17F: использовать этот отчёт как адресное evidence
`grace → blocked → stop → recovery` и независимо сверить source/image,
ledger и ограничения. Текущий `L4D-17F-DOCS-FIX-01-report.md` остаётся
историческим `BLOCKED_TESTS`; после закрытия остальных corrective scopes
нужен новый black-box verdict. Сценарий и детальная хронология:
[L4D-17F-MB-GRACE-TESTPATH-01-plan.md](../../../../l4desk-service/docs/handoffs/L4D-17F-MB-GRACE-TESTPATH-01-plan.md).
