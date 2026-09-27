# 17E: org_id reservation handoff

## Контекст задачи

- Цель: исключить коллизию tenant `org_id` между MenuBuilder и IoT.
- MenuBuilder branch: `release/l4tools-1.8.2-beta-1`.
- IoT branch: `l4desk/fix-org-id-17e` в отдельном чистом worktree.
- Контур: локальные исходники и тесты; серверные файлы и данные не менялись.
- Intake и ограничения: `.agent-context/tasks/active/l4d-main-convergence.md`,
  AGENTS.md обоих репозиториев. Существующий tenant 4 сохранён.

## Выполнено

- MenuBuilder registration и admin create запрашивают ID у защищённого
  internal API IoT до записи своей БД. При отсутствии IoT регистрация
  останавливается; подтверждение письма повторяет тот же operation ID.
- IoT создаёт строку в `tb_orgs` и `tb_org_reservations` в одной транзакции.
  Advisory lock сериализует запросы allocator, а уникальный `org_id`
  защищает от сторонних IoT writers.
- Миграция IoT: `0008_org_reservations`; терминальные/медийные протоколы
  не меняются.

## Затронутые контракты

| Contract | Producer | Consumer | Совместимость |
|---|---|---|---|
| POST `/api/internal/v1/provisioning/organizations/reserve` | IoT app1 | MenuBuilder backend | app1 разворачивается первым; старый MenuBuilder может работать с новым app1 |
| `tb_orgs.org_id` + `tb_org_reservations.operation_id` | IoT DB | IoT app1 | новая таблица, существующие строки не меняются |

## Проверено локально

- MenuBuilder: `uv run pytest -q` — 513 passed; затем адресный контрактный
  `test_iot_org_reservation.py` — 3 passed.
- MenuBuilder: `uv run ruff check --fix app tests`, `uv run ruff format app tests`,
  `uv run pyright app` — проходят.
- IoT: `uv run pytest app-service/tests -q` — 427 passed; после небольшой
  правки release lock адресные 5 тестов прошли повторно.
- IoT: `uv run alembic heads` — `0008_org_reservations`; `ruff` и `black`
  на изменённых Python-файлах проходят.
- Полные `ruff`/`black` IoT не проходят на существующих файлах вне этого
  изменения; их форматирование не входит в данную задачу.

## Не выполнено и риски

- Не запускались миграция и E2E на сервере: требуется согласовать развёртывание
  app1, затем MenuBuilder backend. До этого gate публичной регистрации
  нельзя считать закрытым.
- Две БД не разделяют транзакцию: при сбое локального commit IoT резерв
  остаётся. Повтор регистрации с тем же `operation_id` использует его;
  неиспользованные резервы проверяются адресно, не освобождаются автоматически.
- После развёртывания проверить создание нового тестового tenant при занятом
  ID в IoT, совпадение ID в обеих БД, повтор подтверждения и отсутствие
  новых коллизий. Tenant 4 и 1000 не менять.
