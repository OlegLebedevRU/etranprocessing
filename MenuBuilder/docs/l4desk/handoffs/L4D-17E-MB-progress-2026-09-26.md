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

## Терминал 70 и PIN

До любых изменений проверены только read-only данные: `terminals.id=820`,
`device_id=70`, `org_id=1`, 40-символьный serial сертификата нового CA,
действующая лицензия `id=43`, отдельная запись `l4desk_terminals` c tenant 1.
IoT status сообщает `org_id=1` и `is_online=true`; кеш MenuBuilder показывает
offline. Тестовый tenant имеет ID 3. Перенос не выполнен: существующий admin
endpoint меняет `terminals.org_id` и лицензию, но не синхронизирует
`l4desk_terminals.tenant_id` и IoT ownership. Нужен согласованный атомарный
маршрут с rollback и проверкой на живом Agent. PIN не выпускался и пользователю
пока ничего вводить не требуется.

## Оставшиеся проверки и gate

- Закрыть transfer/provisioning для устройства 70 и проверить online, PIN,
  console/video, usage и возврат устройства исходному владельцу. Текущую
  лицензию и certificate binding сохранить либо явно восстановить.
- Выполнить всю матрицу `L4D-17E-MB.md`: free 120 min, paid continuation,
  online once per month, DST/month boundaries, grace/block, manual payment и
  storno, projection rebuild/reconciliation, rounding, Hub и archive.
- Сверить candidate, полный deployed image/source, flags и rollback. Здесь
  доказан нулевой дисбаланс только тестового платежа, не всего ledger.
- Не выпускать `H-L4D-17E-MB-v1` и не открывать 17F по этому промежуточному
  evidence. Исторические verdict в исходных отчётах остаются отдельными.

После завершения испытаний отозвать тестовые сессии, вернуть устройство 70,
деактивировать test user/tenant по утверждённому admin-маршруту, остановить
изолированный Compose и удалить его private volume. Не удалять ledger вручную.
