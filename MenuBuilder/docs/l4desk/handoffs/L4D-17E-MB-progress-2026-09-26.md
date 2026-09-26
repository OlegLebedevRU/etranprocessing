# L4D-17E-MB — изолированный commercial E2E: промежуточное evidence

```yaml
prompt_id: L4D-17E-MB
status: IN_PROGRESS
handoff_status: NOT_ACCEPTED
source_commit: 97e528d
scope: MenuBuilder payment, registration and terminal fixture
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
| Изолированный runtime | test-backend image `sha256:57d337e6d84c2a813fb8c2580e92c98243e618cc5cb23cdc75b66fe655c0d167`, доступ только на loopback; рабочий MenuBuilder image `sha256:e9559f30045479dbc1a4affc6b87ad9dcff7699c0249c28b18d6082ccdaf82ee` продолжает работать. |
| Первый terminal/onboarding | В `tenant_id=3` создан терминал `3717`, локальный `device_id=1000002`. IoT выделил другой ID `10000006`, а MenuBuilder ошибочно пометил provisioning как ready. PIN был потреблён и сертификат установился, но IoT identity не совпала. Терминал `3717` затем soft-delete через owner API; runtime `is_active=false`. IoT-запись `10000006` требует отдельного cleanup по provider-контракту. |
| Исправление identity | Принятый `L4D-13-MB-FIX-01` включён в текущую ветку коммитом `83a23cb` из `45bd645` (серверный файл совпадал с ним byte-for-byte). Коммит `f08b1e7` передаёт канонический ID в provisioning, требует полного совпадения ответа и выставляет runtime `iot_provisioned` только после успеха. Backend suite: 483 passed, 50 warnings; frontend: 58 passed, build прошёл. |
| Второй terminal/onboarding | Терминал `3718`, `device_id=1000003`, SN `a4b1000003c96241d260926`. DB, L4Desk и IoT by-operation совпадают по tenant, terminal, device и SN; `provisioning_state=ready`, `pin_state=consumed`, сертификат привязан. Пользователь подтвердил ввод PIN и сообщение Agent о подключении. IoT status на момент проверки всё ещё `is_online=false`, `connected_at=null`; отдельный MQTT/presence evidence отсутствует. Free-маркер перенесён на `3718` после удаления `3717`. |
| MQTT/IoT доступ нового терминала | RabbitMQ не содержал пользователя/ACL нового SN; в журнале 26 сентября повторялись `invalid credentials`. Контракт `devices/provision` создавал IoT-запись и событие, но не MQTT-доступ. Адресный вызов штатного `POST /api/internal/v1/provisioning/terminals` для `device_id=1000003`, tenant `3` вернул `success=true`, `rmq_user_status=ok` и точную identity. После него RabbitMQ показывает пользователя, vhost/topic ACL `^dev.{client_id}.*` и `^srv.{client_id}.*`, активное MQTT-соединение нового SN; IoT status вернул `is_online=true`, `connected_at=2026-09-26T12:35:13.022000Z`. В onboarding-код добавлен обязательный шаг MQTT access до `provisioning_state=ready`; локальная проверка и развёртывание этого исправления фиксируются отдельно. |

## Новый тестовый терминал и PIN

Пользователь уточнил маршрут: запись терминала 70 и его лицензия остаются без
изменений; его тестовый Agent используется для установки идентичности нового
терминала `3718`. Пользователь разрешил не сохранять конфигурацию Agent для
возврата. Onboarding включён только в изолированном backend. Оба PIN потреблены;
их значения не записаны в репозиторий или отчёт.

## Оставшиеся проверки и gate

- Подтвердить `l4desk` ctl presence отдельно от MQTT transport online, затем
  проверить console/video, usage и полную цепочку 17F. Запись терминала 70 и
  его лицензия не менялись.
- Разобрать и безопасно убрать осиротевшую IoT provisioning-запись `10000006`
  первого теста, не затрагивая рабочий терминал `3718`.
- Выполнить всю матрицу `L4D-17E-MB.md`: free 120 min, paid continuation,
  online once per month, DST/month boundaries, grace/block, manual payment и
  storno, projection rebuild/reconciliation, rounding, Hub и archive.
- Сверить candidate, полный deployed image/source, flags и rollback. Здесь
  доказан нулевой дисбаланс только тестового платежа, не всего ledger.
- Не выпускать `H-L4D-17E-MB-v1` и не открывать 17F по этому промежуточному
  evidence. Исторические verdict в исходных отчётах остаются отдельными.

После завершения испытаний отозвать тестовые сессии, деактивировать тестовый
терминал и test user/tenant по утверждённому admin-маршруту, остановить
изолированный Compose и удалить его private volume. Не удалять ledger вручную.
