# L4D-17E-MB — изолированный commercial E2E: промежуточное evidence

```yaml
prompt_id: L4D-17E-MB
status: IN_PROGRESS
handoff_status: NOT_ACCEPTED
source_commit: 20598d3
scope: MenuBuilder payment and registration fixture
```

## Владелец и контракт

MenuBuilder владеет регистрацией, tenant/user, платёжным API и ledger. Изолированный
`MenuBuilder/e2e/compose.yaml` запускает отдельный backend с общей БД и отдельный
мок ЮKassa во внутренней Docker-сети. Мок отвечает на `POST/GET /v3/payments` и
отдаёт авторитетный статус для webhook/poll. Рабочий `menubuilder-backend` не
пересоздавался, его коммерческие флаги остаются выключенными. Тестовый backend
включает registration/billing/YooKassa, но держит выключенными JWT mock, IoT
consumer и entitlement worker; наружу доступен только через loopback порт.

## Выполнено

| Проверка | Факт |
|---|---|
| Email и регистрация | Одно письмо на заранее согласованный адрес; владелец подтвердил переход по ссылке. Через HTTP созданы тестовые `tenant_id=3`, `user_id=653`. Пароль остался в приватном Docker volume, в Git и отчёте его нет. |
| Платёж | Мок: `payment_id=2`, статус `succeeded`; webhook, повтор webhook и poll прошли. Ledger: `transaction_id=3`, две строки, дебет и кредит по 1000 копеек; одна проводка для платежа. Баланс увеличился на 1000 копеек. Реального списания не было. |
| Дефект и исправление | Первый прогон вернул 201 на создание платежа без сохранения строки, затем webhook получил 400. Причина: `get_db` не делает commit, а write-маршруты finance не фиксировали транзакции. Исправлены payment/webhook/poll и остальные write-маршруты finance; коммиты `fce24cb`, `20598d3`. Повторный прогон прошёл на том же tenant. Несохранённый первый provider ID оставлен только в приватном моке для аудита. |
| Локальные проверки | `ruff check --fix`, `ruff format`, `pyright` прошли; `uv run pytest -q --disable-warnings`: 477 passed, 47 warnings; тесты мока: 2 passed. |
| Изолированный runtime | test-backend image `sha256:3e63cb1d9610599b0f7d0c839953bde8ff79670295f9878a6246453eefcfbb77`; рабочий MenuBuilder image `sha256:e9559f30045479dbc1a4affc6b87ad9dcff7699c0249c28b18d6082ccdaf82ee` продолжает работать. |

## Новый тестовый терминал и PIN

Пользователь уточнил маршрут: терминал 70 остаётся у своего владельца; в
`tenant_id=3` нужно создать **новый** терминал. Канонический endpoint
`POST /api/settings/terminals` атомарно создаёт runtime и L4Desk записи, затем
выполняет IoT provisioning и выдаёт краткоживущий PIN. На изолированном backend
onboarding пока выключен; до шага создания его нужно включить только там.
После создания проверить выданные `terminal_id`, `device_id`, SN, readiness и
связь с выбранным тестовым Agent. Если PIN вводится на Agent, ранее обслуживавшем
терминал 70, сохранить его исходную конфигурацию и путь возврата до ввода.
Новый терминал ещё не создан, PIN не выпускался и пользователю пока ничего
вводить не требуется.

## Оставшиеся проверки и gate

- Создать новый терминал в тестовом tenant через onboarding API, затем проверить
  provisioning/PIN, Agent online, console/video и usage. Терминал 70 и его
  лицензию не менять. Если переиспользуется его Agent, вернуть исходную
  конфигурацию Agent после теста.
- Выполнить всю матрицу `L4D-17E-MB.md`: free 120 min, paid continuation,
  online once per month, DST/month boundaries, grace/block, manual payment и
  storno, projection rebuild/reconciliation, rounding, Hub и archive.
- Сверить candidate, полный deployed image/source, flags и rollback. Здесь
  доказан нулевой дисбаланс только тестового платежа, не всего ledger.
- Не выпускать `H-L4D-17E-MB-v1` и не открывать 17F по этому промежуточному
  evidence. Исторические verdict в исходных отчётах остаются отдельными.

После завершения испытаний отозвать тестовые сессии, вернуть тестовый Agent
в исходное состояние, деактивировать test user/tenant по утверждённому admin-маршруту, остановить
изолированный Compose и удалить его private volume. Не удалять ledger вручную.
