# 17E: org_id reservation handoff

## Контекст задачи

- Цель: исключить коллизию tenant `org_id` между MenuBuilder и IoT.
- MenuBuilder branch: `release/l4tools-1.8.2-beta-1`.
- IoT branch: `l4desk/fix-org-id-17e` в отдельном чистом worktree.
- Контур: локальные тесты и согласованный деплой на 87.242.100.34;
  существующие tenant/terminal записи не менялись, добавлен test05.
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
- IoT: `uv run pytest app-service/tests -q` из корня после rebase на серверную
  ревизию — 431 passed; адресные 5 тестов прошли повторно.
- IoT: `uv run alembic heads` — `0008_org_reservations`; `ruff` и `black`
  на изменённых Python-файлах проходят.
- Полные `ruff`/`black` IoT не проходят на существующих файлах вне этого
  изменения; их форматирование не входит в данную задачу.

## Проверено на сервере

- После явного подтверждения пользователя app1 переключён с `5e1093b`
  на `60f7762`, собран и пересоздан только сервис `app1`. Резервная копия
  приватного `.env` и прежняя ревизия сохранены.
- `alembic current` внутри app1: `0008_org_reservations (head)`; app1
  `/docs` отвечает 200, новый внутренний маршрут без ключа отвечает 403.
- Четыре Python-файла MenuBuilder из commit `224daaf` скопированы после
  резервного копирования, SHA-256 staging/live совпали, пересобран и
  пересоздан только `menubuilder-backend`; `/docs` отвечает 200.
- Вызов нового клиента MenuBuilder с действующей внутренней авторизацией
  для занятого IoT `org_id=4` вернул `409 org_id_already_in_use`.
- У backend после старта был единичный `ConnectionResetError` на asyncpg
  SSL connection; контейнер остался Up и `/docs` ответил 200. Стабильность
  managed PostgreSQL ведётся отдельно.
- После отдельного подтверждения обновлён только изолированный
  `l4desk-e2e-test-backend` до image `224daaf`. Регистрация=true, policy=true,
  бесплатная квота 600 с только для tenant 1000, IoT consumer=false;
  обычный production backend сохраняет registration=false. Mock gateway
  и остальные сервисы не пересоздавались.
- Штатный E2E runner с `E2E_REGISTRATION_ONLY=true` отправил одно письмо
  `test05@platerra.ru`; пользователь подтвердил ссылку без ошибки. Runner
  вернул `tenant_id=10000`, `user_id=655`, нулевой баланс и не запускал платёж.
  В MenuBuilder ровно одна org/user/owner membership для tenant 10000,
  user имеет role_id=5, registration id=4 consumed. В IoT ровно одна org
  и одна reservation для 10000 (`l4desk-registration:4`). Повтор ссылки
  показал «уже подтверждено» и не создал дубликатов.

## Не выполнено и риски

- Gate коллизии `org_id` закрыт положительным и отрицательным E2E. Глобальный
  production flag регистрации остаётся выключен до общего решения по 17E.
- Две БД не разделяют транзакцию: при сбое локального commit IoT резерв
  остаётся. Повтор регистрации с тем же `operation_id` использует его;
  неиспользованные резервы проверяются адресно, не освобождаются автоматически.
- При будущем rollout следить за неиспользованными резервами после сбоев
  локальной БД; tenant 4 и 1000 не менять.
- Временный приватный manifest test05 с автоматически созданным паролем
  удалён после проверки. Tenant 10000 оставлен как утверждённые тестовые
  данные; для дальнейшего входа потребуется штатный сброс пароля.
- Старый E2E image `f2f49ec` и резервные копии предыдущих серверных файлов
  оставлены для отката; staging-файлы текущего деплоя удалены.
