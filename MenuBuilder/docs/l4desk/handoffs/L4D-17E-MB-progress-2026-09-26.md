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
| Agent ctl и деплой исправления | `GET /api/internal/v1/remote-input/devices/{sn}/status` вернул `agent.online=true`, `desktop_available=true`, `stale=false`, версию `1.7.6`; stream остановлен, аренда отсутствует. Коммит `64f747e` добавил штатный MQTT access в saga, retry при отказе и тест; `ruff`, `pyright` и полный backend suite `484 passed, 50 warnings` прошли. Изолированный test-backend пересобран в image `sha256:863591b928b3ddbdb9829cb144801e4df4d72c9a02a5e1c6dc315d7b8683ecd3`, `/docs` отвечает 200. Рабочий `menubuilder-backend` остался на image `sha256:e9559f30045479dbc1a4affc6b87ad9dcff7699c0249c28b18d6082ccdaf82ee`. |
| Browser video и console | В tenant `3` суперпользователь `o.lebedev` увидел устройство `1000003`; движущиеся кадры подтверждены при первом и повторном старте. Во время потока Agent сообщил `running`, ingress — `fresh_rtp=true`, `media_state=live`, 1592 RTP packets на момент замера. После окончательной остановки Agent сообщил `stopped`, lease `active=false`. Пользователь подтвердил подключение консоли и успешный Ping; app1 принял diagnostics WS, после отключения lease снова `active=false`. Путаница с промежуточной остановкой была уточнена пользователем: второй stop к тому моменту ещё не был нажат; дефекта UI stop этим наблюдением не установлено. |
| Взаимное исключение и роль владельца | Пока суперпользователь держал console lease, отдельный app1-запрос stream lease с иным session ID получил `409 lease_taken`, console не прервалась. Изолированный MenuBuilder test-backend под штатным тестовым owner (`tenant=3`, `role_id=5`, `role=l4desk_owner`) получил `403 scope_not_allowed` на stream lease и при занятой консоли, и после её отключения при свободном устройстве. Второй отказ исключает конфликт аренды как причину: граница ролей MenuBuilder → app1 несовместима для owner. Непредвиденной аренды не создано; в конце `stream=stopped`, `lease.active=false`. |
| Коммерческий blocker после реального потока | В общей БД test tenant `3` после нескольких video start/stop и консоли: `l4desk_remote_sessions=[]`, `fin_usage_daily=[]`, `fin_terminal_monthly_charges=[]`. На deployed MenuBuilder `video_control.py` создаёт/закрывает сессию через `flush`, но не `commit`; `get_db` не фиксирует транзакцию. Поиск активной сессии в video routes получает `device_id=1000003`, тогда как запись создаётся с `terminal.id=3718`. `FinMeteringService.record_session_usage` доступен через внутренний endpoint, но автоматический producer из video/IoT consumer в текущем MenuBuilder-коде не найден. В изолированном backend IoT consumer и entitlement worker намеренно выключены. Поэтому браузерное видео не доказывает commercial usage/monthly charge; нужен отдельный corrective и повтор E2E. |
| Финансовый срез после проверки | По tenant `3` имеется только posted payment transaction `3` на 1000/1000 копеек, две ledger entries с суммами debit=credit=1000; `first_payment_transaction_id=3`, balance=1000 копеек, entitlement=`active`, free terminal `3718`, `today_usage_seconds=0`. Нулевая разница доказана только для этого тестового tenant; отсутствие usage/charge является дефектом покрытия потока, а не успешной сверкой коммерческого E2E. |
| Состав запущенных образов | Сравнение `.py` по SHA-256 между текущей веткой и прямым `/workspace/MenuBuilder/backend/app` выявило 8 отличающихся файлов в production image и 8 в изолированном test-backend; наборы отличаются. В обоих образах также есть два дополнительных Python-файла относительно текущего дерева. Полная byte parity/release provenance не доказана; текущий production image сохранён без пересоздания. |

Реестр byte drift для пересборки кандидата (пути относительно `backend/app`):

- production: `repositories/l4desk_repository.py`, `routers/finance.py`,
  `services/financial_core/stop_outbox.py`, `services/iot_client.py`,
  `services/iot_event_feed_client.py`, `services/remote_session_use_case.py`,
  `services/terminal_creation_service.py`, `services/terminal_onboarding_service.py`;
- isolated test-backend: `config.py`, `repositories/l4desk_repository.py`,
  `routers/video_control.py`, `services/financial_core/stop_outbox.py`,
  `services/iot_client.py`, `services/iot_event_feed_client.py`,
  `services/remote_session_use_case.py`, `services/terminal_creation_service.py`;
- оба образа дополнительно содержат `permissions.py` и
  `services/remote_session_stop.py`. Сравнивались байты файлов, а не
  семантическая эквивалентность; исходные образы не заменялись.

## Новый тестовый терминал и PIN

Пользователь уточнил маршрут: запись терминала 70 и его лицензия остаются без
изменений; его тестовый Agent используется для установки идентичности нового
терминала `3718`. Пользователь разрешил не сохранять конфигурацию Agent для
возврата. Onboarding включён только в изолированном backend. Оба PIN потреблены;
их значения не записаны в репозиторий или отчёт.

## Оставшиеся проверки и gate

- Corrective для регистрации контроллером: согласовать допустимую роль
  `l4desk_owner` на границе MenuBuilder → app1 без подмены её суперпользователем;
  сделать запись и закрытие `L4DeskRemoteSession` долговечными с единым
  `terminal.id` для create/lookup; подключить идемпотентный producer реальных
  start/stop и first-online фактов к `FinMeteringService` и месячному начислению.
  Приёмочные доказательства: owner JWT получает разрешённый lease, другой
  session ID получает `409` при занятой консоли, после video stop есть одна
  закрытая сессия и измеренные секунды usage, first-online создаёт не более
  одного начисления за месяц, ledger debit=credit, повтор события не дублирует
  запись. До изменения provider-контракта отдельно сверить его владельца и
  accepted handoff; provider source в этой приёмке не открывался.
- Подготовить corrective для совместимости `l4desk_owner` с app1, durable remote
  session и metering/first-online интеграции. Затем повторить owner/browser/API
  E2E и только после этого проводить дальнейшую коммерческую матрицу. MQTT,
  ctl, video и console под суперпользователем подтверждены; owner stream,
  usage/monthly charge пока не проходят. Запись терминала 70 и его лицензия
  не менялись.
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
