# L4Desk SCHEMA v1 — etranprocessing-db 0.1.1

## Назначение и доставка

- Additive opt-in schema-model package, consumer `L4D-04B-PB`, затем `L4D-04C-MB`.
- Публикация согласована пользователем: исходники `shared` через Git, точный producer commit в handoff. Python registry, GitHub Release и загрузка wheel не нужны. Не использовать HEAD/main вместо указанного commit.
- Runtime dependencies не изменены. Python `==3.14.*`, SQLAlchemy 2.0. Wheel/sdist собираются только для проверки упаковки.
- `package-source-v011.json` фиксирует SHA-256 каждого Python-исходника пакета, pyproject, lockfile и schema artifacts. Для исходников проверяются Git blob bytes (UTF-8, LF; CRLF рабочего checkout нормализуется). Digest самого manifest в handoff — digest исходного пакета; отдельно фиксируются SHA-256 schema artifacts.
- Полный исполняемый PostgreSQL metadata contract: `schema-v1.json`. Для каждой таблицы перечислены **все** колонки (тип, nullability, PK, client/server defaults, onupdate), CREATE TABLE с PK/FK/UQ/CHECK и все CREATE INDEX. JSON содержит 31 неизменную legacy-таблицу и 23 opt-in таблицы; это **не готовая миграция** и его нельзя вслепую выполнять поверх production.
- Модели не содержат расчётов, IO, API, миграций, listeners/triggers, crypto/auth helpers или seed данных.

## Подключение и совместимость

```python
from etranprocessing_db import Base  # прежние 31 таблица, без L4Desk/IoT
from etranprocessing_db import l4desk  # явная регистрация всех 23 таблиц
```

Обычные импорты `etranprocessing_db` и `etranprocessing_db.models` не меняются. Не использовать `extend_existing=True`: оно скрыло бы расхождение схем.

**MenuBuilder не должен импортировать opt-in модуль одновременно с локальными объявлениями IoT.** В 04C локальные классы заменяются импортами `IotConsumerCheckpoint`, `IotEventInbox`, `IotEventQuarantine` из shared. Нужен полный restart процесса, не hot-reload второго набора объявлений в той же metadata. В 04B opt-in модуль импортируется в окружении Alembic до чтения `Base.metadata`.

04B/04C обновляют свои dependency/lockfiles: Docker использует `uv sync --locked`; bump shared без обновления lock потребителя может остановить его сборку. В этой задаче файлы потребителей не менялись. До завершения последовательности не сливать ветку в main/не запускать автоматический rollout.

## Модели и таблицы

| Модель | Таблица | Ключ/назначение |
|---|---|---|
| L4DeskRegistration | l4desk_registrations | Pending registration; unique normalized email и token hash, срок/одноразовое consumption |
| L4DeskTenantProfile | l4desk_tenant_profiles | Tenant timezone + отложенное изменение timezone |
| L4DeskMembership | l4desk_memberships | PK tenant/user, role_id=5, не более одного owner |
| L4DeskTerminal | l4desk_terminals | Устойчивая business identity, tenant ordinal, provisioning/PIN state и связь с runtime terminal |
| L4DeskAuditEvent | l4desk_audit_events | Actor/subject/outcome/correlation audit; без токенов и PIN |
| L4DeskRemoteSession | l4desk_remote_sessions | Reservation и итоговая session summary, технические факты приходят из IoT |
| IotConsumerCheckpoint | iot_consumer_checkpoints | Существующий PK consumer_id, durable int64 cursor |
| IotEventInbox | iot_event_inbox | Существующий PK event_id, JSON payload, process status |
| IotEventQuarantine | iot_event_quarantine | Существующий PK id, error/raw event/retry state |
| FinTariffVersion | fin_tariff_versions | Unique version/effective_from; ставки и free quota snapshot |
| FinAccount | fin_accounts | Один tenant_settlement на tenant, глобальные payment_clearing/usage_revenue |
| FinLedgerTransaction | fin_ledger_transactions | Operation/source idempotency, declared balanced totals, original/reversal/adjustment |
| FinLedgerEntry | fin_ledger_entries | Unique transaction/line, односторонние positive debit либо credit |
| FinBillingProfile | fin_billing_profiles | First-payment anchor, исходный день месяца, timezone, entitlement projection |
| FinBillingCycle | fin_billing_cycles | Unique tenant/sequence и tenant/start, half-open период, grace deadline |
| FinUsageDaily | fin_usage_daily | Unique terminal/local_date, pooled seconds и округление, источник и ledger link |
| FinTerminalMonthlyCharge | fin_terminal_monthly_charges | Unique terminal/cycle, достоверный первый online, включая нулевую льготную строку |
| FinPayment | fin_payments | Provider/operation idempotency, verified payment, fiscal receipt snapshot |
| FinManualPayment | fin_manual_payments | Bank document, creator user, evidence и обязательная ledger transaction |
| FinBalanceProjection | fin_balance_projections | Tenant settlement balance, version и last transaction; отрицательные значения допустимы |
| FinNotificationDelivery | fin_notification_deliveries | Unique tenant/cycle/type, retry/delivery state |
| FinReconciliationRun | fin_reconciliation_runs | Totals/discarded kopecks, mismatches и source hash; ошибки не скрываются |
| FinArchiveBatch | fin_archive_batches | PK batch/project, JSON manifest/checksum, time/cursor bounds и retention |

## Identifiers и tenant scope

- `tenant_id` соответствует `orgs.org_id` (Integer); `user_id` — `users.id` (Integer). Membership — дополнительная связь, не изменение существующего User/auth boundary. Consumer атомарно создаёт user role_id=5, org, owner membership и profile.
- `l4desk_terminals.terminal_id` сохраняет исходный Integer `terminals.id`, **не генерируется самостоятельно и не переиспользуется**. `runtime_terminal_id` nullable unique FK ON DELETE SET NULL; после удаления runtime-записи историческая business row сохраняется, consumer ставит `deleted_at`. Финансовые документы и sessions ссылаются на устойчивую business row, а не удаляемый runtime terminal.
- `ordinal` positive и unique внутри tenant, никогда не перенумеровывается. Бесплатный терминал вычисляет consumer по минимальному ordinal среди неудалённых; package это не вычисляет. Перенос уже учтённого терминала между tenant не предусмотрен этим контрактом; нельзя менять tenant у historical row.
- Composite FK `(terminal_id, tenant_id)`, `(billing_cycle_id, tenant_id)`, `(ledger_transaction_id, tenant_id)` запрещают смешение tenants для документов/сессий.
- Импортированные event/session/operation/correlation/terminal IDs — opaque `String(128)`, не native UUID. Producer UUID для новых operations генерирует/проверяет API boundary; fixture `op-onl-001` не должен ломать event ingestion. IoT `device_id` остаётся Integer.
- IoT inbox дедуплицирует по event_id, **не по operation_id или cursor**: одна operation имеет несколько lifecycle events. Cursor BigInteger; никаких новых UNIQUE/CHECK/server defaults на существующие IoT-таблицы не добавлено. Их колонки, Python defaults, JSON и индексы сохранены. Quarantine PK — `id`, несмотря на упрощённое описание `quarantine_id` во входном handoff.

## Reservation и session summary

- Unique `(tenant_id, operation_id)` обеспечивает REST idempotency; nullable unique provider_session_id разрешает reservation до ответа IoT.
- Partial unique index `l4desk_session_reservation_uq` на terminal_id действует для **всех** типов console/video при состояниях `reserved/start_requested/active/stop_requested`.
- `active` требует active_at; `closed/failed` требуют closed_at. Закрытая/failed строка сохраняет summary и освобождает reservation.
- Это коммерческая reservation/проекция, не замена техническому IoT lock. Consumer использует транзакции при захвате, монотонный cursor при обновлении, watchdog и подтверждённые active/closed facts. Переходы состояний, конечный timeout и запрет начисления до active не реализованы в package.

## Финансовые инварианты

- Все новые финансовые таблицы, включая PK/FK/UQ/CHECK/index names, имеют prefix `fin_`. Все суммы и totals — BigInteger в копейках, без Float/Numeric; posting/entries/payments кратны 100. Никакой скрытой конвертации единиц.
- Usage хранит `source_seconds = video_seconds + console_seconds = free_seconds + billable_seconds`, timezone/local_date и тариф. Monthly хранит first_online/source_event_id и snapshot льготы.
- Daily/monthly CHECK: `calculated = posted + discarded`, `0 <= discarded < 100`, `posted >= 0`, `posted % 100 = 0`. Нулевая расчётная строка допустима; нулевая transaction имеет нулевые totals и не требует нулевых ledger entries (они запрещены).
- Ledger transaction имеет unique tenant/operation и unique tenant/source_project/source_type/source_id. Для corrections нужен **новый source_id** и operation, ссылки на исходную transaction не могут ссылаться на себя или другой tenant.
- Transaction row проверяет равенство **объявленных** debit/credit totals. Ledger entry — ровно одна положительная сторона, unique transaction/line, FK к transaction/account. Это поддержка double-entry, **не доказательство суммы всех entries**.
- `FinUsageDaily`/monthly — один оригинальный расчёт на естественный ключ. До posting consumer может пересчитывать строку под блокировкой. После posting оригинал не меняется. Delta calculation сохраняется в `FinLedgerTransaction.calculation_snapshot` новой `adjustment`/`reversal` с corrects_transaction_id; consumer фиксирует original document ID, signed delta metrics, тариф, timezone и source hashes. Signed delta не вставляется вторым оригинальным daily/monthly документом. Положительные totals новой transaction и направление её entries выражают сторно/возврат.
- Payment unique `(provider, provider_payment_id)` и `(tenant_id, operation_id)`; nullable provider id допускает создание до HTTP ответа. `succeeded` требует provider id, verified_at и succeeded_at; ledger link возможна только при succeeded. Receipt snapshot хранится отдельно от статуса оплаты. Return URL не доказывает оплату.
- Manual payment unique operation, positive whole-ruble amount, обязательный creator и ledger link. Проверка superuser — обязанность consumer, не declarative package.
- Anchor nullable до первой проведённой оплаты, устанавливается единым набором полей. Сохраняется исходный anchor_day 1–31. Entitlement free по умолчанию; active/grace без anchor запрещены. Период cycle — `[starts_at, ends_at)`, grace_deadline внутри периода; точное правило +3 календарных дня применяет consumer с учётом timezone/DST.
- Notification types: `cycle_minus_7`, `cycle_minus_3`, `cycle_minus_1`, `grace`, `blocked`; unique tenant/cycle/type. Повтор delivery обновляет ту же строку/attempts, а не создаёт новую.

## Обязательные обязанности 04B и финансового consumer

Следующее **не гарантируется обычными CHECK/FK** и не должно считаться готовой финансовой реализацией:

1. Deferred/transactional проверка `sum(entries.debit) = sum(entries.credit) = transaction totals` при posting, минимум необходимых проводок, совпадение суммы/type/source документа с проводкой. Нулевые расчёты обрабатываются отдельно.
2. Account ownership: settlement account принадлежит tenant transaction/projection; global clearing/revenue используется по допустимым правилам. FK проверяет существование account, но не эту межстрочную семантику.
3. Append-only для posted transaction/entries и проведённых calculation/payment/manual документов, запрет UPDATE/DELETE и сторно уже сторнированного документа по бизнес-правилам. Package не устанавливает триггеры. 04B должен определить DB guard strategy до финансовой активации; финансовый consumer выполняет проверки атомарно.
4. Atomic successful verified payment → posting → first anchor → balance projection; provider verification, ledger idempotency, row locking/version checking и reconciliation. Last transaction id — ссылка, не гарантия commit-order при concurrent writers.
5. Cycle non-overlap, неизменность anchor, +3 календарных дня, tariff effective selection, pooled quota/rounding, split sessions по tenant midnight, timezone change не ранее следующих локальных суток. Состояние grace нельзя предоставлять без оплаты.
6. Нормализация email, hash/token expiry/replay и atomic onboarding; scheduled timezone promotion; role/auth checks. Unique token hash сам по себе не предотвращает повтор consume без условного UPDATE/lock.

04B сверяет live IoT DDL с данным baseline до migration. Существующие таблицы нельзя безусловно CREATE или принимать за правильные по одному `checkfirst`: сравнить колонки/defaults/indexes; при drift — остановиться и оформить corrective contract. Старые 31 таблица не являются целью expand migration. Никаких destructive ALTER/DROP или backfill старых пользовательских данных в 04A.

## Архив и retention

- Источники сохраняются как source_project, opaque source_event_id, source_events_hash и optional archive_batch_id, **без FK на горячие IoT events или обязательного manifest FK**. Создание manifest позже/удаление hot events не разрушает финансовые связи.
- `fin_archive_batches` допускает один batch ID в нескольких проектах. JSON manifest содержит version/source types/counts/time bounds/checksum; storage_reference — путь/идентификатор, не credential.
- Purged row требует verified_at/status и прохождения cursor всеми consumers (если through_cursor задан). CHECK не проверяет fsync, доступность файла, backup или истинность cursor evidence.
- Экспорт только закрытых месяцев старше трёх полных месяцев, checksum reread/atomic rename, retention >=3 лет и restore drills — обязанности archive consumer. Поля здесь expand seam; финальный wire Manifest Contract публикует 15A. Архивный transport/schema не объявляется принятым в 04A.
- Actors и correlation обязаны не содержать секреты; audit payload, payment receipt и архивы могут содержать персональные данные и требуют контроля доступа. PIN/token plaintext не хранится в новых моделях.

## Проверки и rollback

- Shared tests: неизменность полного старого PostgreSQL DDL/defaults, root import isolation, mapper/FK resolution, names/types, PostgreSQL compilation, SQL CHECK/UNIQUE/FK/partial unique индексы, negative/edge cases.
- SQLite используется для исполнения package-local constraints; только legacy JSONB/default cast адаптированы к SQLite. PostgreSQL migration/live concurrency/permissions не проверяются SQLite и обязательны для 04B.
- Откат исходного пакета на предыдущий commit/0.1.0 до активации consumers безопасен, поскольку старые импорты и таблицы не менялись. После 04B не удалять populated expand tables для rollback приложения; согласовать lockfiles и возвращать совместимые импорты. Стирание финансовых данных недопустимо.
- Следующий prompt стартует только после независимой проверки и фактической регистрации accepted handoff контроллером в общем журнале; этот package provider журнал не редактирует.
