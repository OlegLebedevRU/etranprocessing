# L4D: единый кандидат к main

Дата сверки: 2026-09-26. Статус: **draft merge candidate**, каскад ещё не закрыт.

## Task intake

- Цель: свести стабильный L4D/L4C код к одному пути в `main` без продолжения
  параллельного каскада веток. Эта задача не меняет production server.
- Владелец данных: `shared/etranprocessing_db`; миграций — ProcessingBackend;
  viewing session/API — MenuBuilder; lease/ctl — внешний app1; media route —
  l4media; терминальный агент — `tools/l4desk` и `l4capture` (`svc_desk`).
- Producer → consumer: UI → MenuBuilder → app1 → MQTT ctl → l4desk;
  l4capture → leo4proxy → l4media/Janus → browser. Контракты:
  [lease](../../contracts/lease-lifecycle.md),
  [video](../../contracts/video-streaming.md),
  [MQTT](../../contracts/mqtt-topic-matrix.md).
- Инварианты: lease expiry останавливает агент без recovery; новый stream
  требует новую эпоху; маршрут живёт весь срок трансляции; stop освобождает
  terminal/media/viewing state; `svc_desk` не занимает `dev/{SN}/svc`.

## Единая ветка и происхождение

- `origin/main` (`19c1dca`) является предком
  `release/l4tools-1.8.2-beta-1` (`41b73bc`): 111 коммитов только на стороне
  кандидата, конфликтов двух веток нет. Между ними 494 изменённых файла.
- Кандидат включает принятый каскад до 17C watch, серверный media renew/stop
  из `228dd57` и проверенный native capture из `4456586`. Сборка
  `l4-tools-1.8.2-beta-1` привязана к source commit `41b73bc`; её хеши и
  ограничения в [handoff](../l4tools-1.8.2-beta-1-handoff.md).
- Рабочий каталог на `l4desk/l4d-17e-e2e-fixture` содержит чужие изменения;
  merge проводить через PR из чистого release worktree. Следующие исправления
  кандидата вносить в эту PR-ветку и повторять затронутые проверки. Старые
  каскадные ветки не удалять до merge и проверки новой базы `main`.

## Проверки кандидата (локально, Windows, 2026-09-26)

| Слой | Команда / факт | Статус |
|---|---|---|
| ProcessingBackend | `uv run pytest -q`: 130 passed, 20 warnings | passed |
| MenuBuilder backend | `uv run pytest -q`: 492 passed, 50 warnings | passed |
| shared DB | `uv run pytest -q --basetemp=.pytest_tmp`: 65 passed; первый запуск с default temp получил WinError 5 | passed после корректировки окружения |
| Python quality | во всех трёх проектах `ruff check`, `ruff format --check`, `pyright`: clean, 0 type errors | passed |
| MenuBuilder frontend | `npm ci --ignore-scripts`; `npm run build`; `npm test -- --run`: 58 passed | passed |
| Native suite | x86/x64, l4capture 129/129, l4desk 21/21, l4setup rollback и manifest — [release handoff](../l4tools-1.8.2-beta-1-handoff.md) | passed ранее |
| l4media ingress local C unit | Linux Makefile; Windows MinGW compile failed before test binary was produced, without diagnostic from cc1 | not run |
| app1 | код и текущий deployed digest находятся в отдельном репозитории; не проверены этой задачей | not run |

Первый `shared` pytest запуск завершился `64 passed, 1 error`: доступ к
системному `%TEMP%/pytest-of-oleg_` запрещён. Повтор с локальным `--basetemp`
прошёл 65/65. Не считать это исправлением кода.

## Незакрытые merge gates

1. `17E`/`17F` в опубликованном каскаде ещё `BLOCKED_CONTRACT`: нет полного
   commercial E2E на утверждённом test tenant и доказательства точной версии
   всего работающего backend image. Принятый watch handoff §51 сам по себе
   не закрывает эти шаги.
2. Подтвердить текущие app1/Media/MenuBuilder deploy revisions и совместимость
   с source candidate, миграцию `027` и rollback план перед production release.
3. Для терминала `1000003` идёт 120-минутная трансляция с приблизительно
   **21:01 МСК**. Контрольная точка — **23:01 МСК**; зафиксировать фактические
   начало/конец, свежий кадр после 120 минут, задержку, ошибки и штатный stop.
   Пока статус **RUNNING**, не PASS.
4. Изолированный install/upgrade/rollback релизного `l4setup` и x86 runtime
   на Windows 7/POSReady 7 остаются за рамками подтверждённого теста.

## Возврат к 17E (2026-09-26)

- §51 журнала принял `H-L4D-17C-VIDEO-WATCH-MB-v1`; исторический
  `L4D-17E-MB-report.md` остаётся `BLOCKED_CONTRACT` до нового verdict.
- Изолированный контур уже описан в
  `MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-progress-2026-09-26.md` и
  `L4D-17E-corrective-2026-09-26.md`: test tenant 3, mock email/ЮKassa,
  успешный платёж без реального списания, owner lease, долговечная закрытая
  видеосессия с 5 секундами usage и одно бесплатное monthly начисление после
  подтверждённого `device_online`. Это частичное E2E, не полная 17E приёмка.
- Для следующего прогона сначала сверить точную версию test-backend с Git
  candidate и безопасные flags, затем повторить stop и убедиться, что одна
  закрытая сессия и usage не дублируются. Не прерывать текущую длительную
  трансляцию 1000003 до её контрольной отметки.
- Tenant 3 уже содержит posted payment. Сценарий **без платежа** с бесплатным
  терминалом и 120 минутами нельзя честно заявить на нём: нужен отдельный
  утверждённый no-payment tenant или чистый изолированный контур. Длительная
  видеотрансляция 1000003 сама по себе подтверждает медиатракт; коммерческий
  quota test требует измеренной usage, entitlement и ledger сверки.
- Затем проверить paid continuation, первый платёжный anchor, online once за
  месяц, DST/границы месяца, grace/late/block, webhook+poll replay, ручной
  платёж/storno, double-entry/rebuild, rounding, Hub и archive. Локальные
  unit/contract tests на эти случаи есть; per-scenario runtime verdict нет.
- После полного E2E сверить весь release image/source, актуальные app1/media
  provider версии, flags, migration `027`, rollback и нулевой ledger mismatch;
  только тогда выпускать новый report/candidate `H-L4D-17E-MB-v1` и передавать
  контроллеру для append-only приёмки. 17F начнётся после этой записи.

## Решение для PR

Один draft PR из `release/l4tools-1.8.2-beta-1` в `main`. После закрытия
обязательных gate обновить этот документ, повторить затронутые проверки,
перевести PR в ready и объединить. До этого не удалять старые ветки и не
считать кандидата финальным production release. В этой задаче серверные
файлы, службы и секреты не затрагивались.
