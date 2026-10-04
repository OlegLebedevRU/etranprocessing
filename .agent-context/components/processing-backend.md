# ProcessingBackend

## 2026-10-05: authenticated renewal / scoped locks

Schema030/PB0b561ce deployed. Dedicated live-newCA/SN/serial renew route and
purpose=renew PIN provider are separate from anonymous certificates/setup.
FOR UPDATE OF terminals is mandatory for both renewal terminal locks because
Terminal eagerly LEFT JOINs nullable terminal_types; unqualifiedlock failed
on realPostgreSQL. PINprovider773201/samePINretry passed; actual7011 issuance
remains open. [Evidence](../tasks/active/2026-10-04-rpc7011-implementation.md).

## 2026-10-03: production subscription transport policy

Ревизия 8bb0359 и schema029: MQTT/RTP admission учитывает admin is_active AND
вычисляемую terminal subscription; outgoing HTTPS сохранён. Classic без tenant
profile не включается в подписки. YooKassa OFF совпадает с MB; оплачиваемые
терминалы заблокированы. Migration029 принадлежит PB и выполнена новым образом.
[Release evidence](../tasks/completed/2026-10-03-l4desk-terminal-subscriptions-release.md).

## Назначение

Сверка policy 2026-10-04, HEAD `c58f607` (статический код): permissions учитывают
`is_active AND subscription_allowed`; клиент `tools/leo4proxy/src/policy.c` уже
реализован. Указания ниже «по is_active» и «клиент пока не реализован» относятся
к прежней сверке. [DNS/SRV packet](../../docs/term_net-leo4proxy-dns-srv-implementation-context.md)
описывает только предложенное расширение endpoints, без runtime evidence.
Терминальный payment/mTLS gateway и единая цепочка Alembic общей PostgreSQL-схемы.

## Границы ответственности
Payment ledger/balances, terminal requests, certificate audit/discovery и выдача меню.
Не размещать здесь пользовательский JWT portal/billing API MenuBuilder.
Автор доменной миграции — владелец таблицы; физическая цепочка — в ProcessingBackend.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| Terminal → backend | mTLS HTTPS через Nginx | payment, techgate, gategauge, licensebilling, certificates | request + X-Client-Cert-DN/Serial | terminal auth/license check |
| Terminal → backend | HTTPS | GET /api/ListMenuFile | terminal menu request | опубликованная версия/аудит выдачи |
| Terminal → backend | mTLS HTTPS | GET /api/leo4proxy/policy | v, sn, mqtt_rtp_allowed, outgoing_https_allowed, stop_facts | MQTT/RTP по Terminal.is_active; HTTPS пока всегда true; inactive terminal получает 200; no-store |
| Backend → shared DB | async SQLAlchemy | payment/ledger/audit | declarative models | owner-scoped запись |

## Инварианты
- Nginx валидирует certificate; доверие к forwarded identity требует защищённой границы proxy.
- ORM не дублировать и не добавлять в shared бизнес-логику.
- MenuBuilder читает payment reporting; это не разрешение на запись ledger.

## State machine
Зависит от payment/certificate/menu use case. Перед изменением статуса выписать
текущие допустимые переходы, повтор запроса и transaction boundary; не выдумывать общую enum.

## Ключевые исходники
- [app/main.py](../../ProcessingBackend/backend/app/main.py) — маршрутизация приложения.
- [app](../../ProcessingBackend/backend/app) — найти конкретный handler по endpoint задачи.
- [alembic/versions](../../ProcessingBackend/backend/alembic/versions) — единственные общие migrations.
- [shared models](../../shared/etranprocessing_db/models) — общая схема.

## Проверка

`test_leo4proxy_policy.py` проверяет контракт через настоящую terminal auth dependency с mock DB: активный/отключенный терминал, отсутствие сертификата, serial mismatch, неизвестный SN, запрет выбора чужого SN через query и OpenAPI. `test_nginx_routing_contract.py` проверяет явный маршрут policy и перезапись cert headers. Клиент leo4proxy пока не реализован; release evidence находится в [отчете](../../docs/term_arch-leo4proxy-server-permission.md).

Выпуск 2026-10-01 MSK: source main `1b8fdb5`, builder → registry → production pull, image digest `sha256:20307553eec9231d7724f6df13e6a7eef9bdd40cbf0e46fd3782024bcce1867e`. Windows/Linux: 138 backend tests, quality checks; production: health, revision/digest и публичный unauthenticated 401 проверены. По прямому разрешению пользователя authenticated 200 проверен через локальный leo4proxy терминала 773: оба флага true, stop_facts=[], no-store. Реальный disabled-сценарий не проверялся. Policy location использует тот же терминальный nginx-mutual-legacy и не содержит mirror.
Из ProcessingBackend\backend: `uv run pytest`, quality по [матрице](../operations/validation-matrix.md).
Auth tests используют simulated certificate headers; payment повторы/ошибки/tenant isolation
проверять в профильных тестах. Shared changes требуют также MenuBuilder и migration checks.

## Известные риски и незавершённые вопросы
Это ownership-карточка, не runtime-аудит gateway. Legacy-совместимость не разрешает
исследовать исключённые корневые каталоги без явного scope пользователя.

Аудит 2026-09-27: `get_current_terminal` намеренно распознаёт терминал при
`is_active=false`, чтобы `/api/licensebilling` вернул XML `<state>error</state>`.
Но `payment` использует эту dependency без отдельной проверки активности.
После мягкого удаления тестового 1000004 и удаления его MQTT-пользователя
старый mTLS сертификат остаётся потенциальным средством распознавания в
ProcessingBackend. Нужна отдельная authorization gate для операций с
терминалом или штатный отзыв сертификата; глобальная блокировка в
`get_current_terminal` нарушит подтверждённый XML-контракт.

## Источники и актуальность
- Authoritative docs: [backend guidelines](../../ProcessingBackend/GUIDELINES.md),
  [ownership](../../docs/etran_data-database-ownership.md), [AGENTS](../../AGENTS.md).
- Code references: entry points выше; обработчики здесь не проверялись.
- Проверено: 2026-09-27, чтение `dependencies.py`, `payment.py` и теста
  `test_licensebilling_contract.py`; runtime проверка старого сертификата
  не выполнялась.
- Обновить при: terminal endpoints/auth, ownership, migration policy.

## 2026-10-04: leo4proxy policy endpoints

Optional endpoints/TTL producer выпущен (main PR #5; deployment baseline PR #10).
Настройки LEO4PROXY_ENDPOINTS и TTL только из env; invalid routing не меняет
admission. response_model_exclude_none сохраняет старый JSON без новых настроек.
Ruff/format/pyright, 156 tests, deployed health и прямой authenticated mTLS GET
прошли; возвращены 4 канала с public fallback IP и TTL 86400. Миграций нет.
Native consumer/signing статус: [packet](../../docs/term_net-leo4proxy-dns-srv-implementation-context.md).
