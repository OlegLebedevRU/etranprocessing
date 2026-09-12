---
name: performance-and-resilience-risk-analysis
description: Анализ архитектурных рисков производительности и отказоустойчивости с фокусом на очереди сообщений, таймауты watchdog/lease и нагрузку на PostgreSQL/ORM.
---

# Performance and resilience risk analysis

Скомбинированный анализ рисков узких мест производительности (scalability hotspots), отказоустойчивости и деградации системы при пиковых нагрузках и аварийных сценариях.

1. **Очереди и брокеры сообщений (RabbitMQ / MQTT / AMQP)**:
   - **Backlog и пропускная способность**: выяви риск накопления сообщений при падении или отставании потребителей (`unack`, нулевые консьюмеры, зависшие транзакции).
   - **Лимиты ресурсов и блокировки**: пороги High Watermark памяти и Low Watermark диска RabbitMQ; блокировка publishers (`flow control`).
   - **Маршрутизация и доставка**: накладные расходы wildcard-биндингов, poison messages, отсутствие DLX (Dead Letter Exchange), циклы вечных повторов (retry storm).
   - **Контрактные инварианты**: запрет retain для команд/ACK/NACK; всплеск публикаций при массовом reconnect терминалов.
   - Ссылка: [rabbitmq-diagnostics](../rabbitmq-diagnostics/SKILL.md), [mqtt-topic-matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md).

2. **Таймауты watchdog, lease и восстановление (Watchdog / Keepalive / Recovery)**:
   - **Рассогласование таймеров**: UI/BFF keepalive interval vs server lease TTL vs терминальный supervisor watchdog (tick + 5 с grace в l4desk).
   - **Пределы восстановления**: лимиты bounded recovery (максимум 5 перезапусков за 600 с), экспоненциальный backoff, защита от flapping/crash-loop.
   - **Инвариант Fail-Closed**: `lease_expired` — безусловный терминальный останов, запрет любых попыток recovery.
   - **Гонки и отмена**: гонки renew vs stop vs expiry; немедленная отмена retry/recovery при получении `stop` / `release`.
   - **Перезапуск агентов**: reconcile процессов при рестарте сервиса, недопущение orphan-процессов ввода и зависших сессий FFmpeg.
   - Ссылка: [lease-and-watchdog-safety](../lease-and-watchdog-safety/SKILL.md), [lease-lifecycle](../../../.agent-context/contracts/lease-lifecycle.md).

3. **Нагрузка на базу данных (PostgreSQL / asyncpg / Shared ORM)**:
   - **Пул соединений**: истощение пула asyncpg при конкурентных запросах, утечки сессий при необработанных исключениях.
   - **Паттерны запросов**: проблема N+1 при выборке связанных данных (терминалы, меню, параметры платежей); обязательное использование `selectinload` / `joinedload`.
   - **Транзакции и блокировки**: длительные открытые транзакции, строчные блокировки (`SELECT FOR UPDATE`) балансов при параллельных платежах, блокировки таблиц DDL-миграциями.
   - **Индексация и объёмы**: фильтрация по `org_id` (мультитенантность) и составным индексам (`sn`, `created_at`); отсутствие пагинации при выгрузках.
   - Ссылка: [api-and-data-ownership](../api-and-data-ownership/SKILL.md), [shared-db](../../../.agent-context/components/shared-db.md).

4. **Оценка тяжести и приоритизация**:
   - Оцени вероятность (Low/Med/High) и влияние (Minor/Major/Critical) на доступность терминалов, потерю платежей или безопасность ввода.
   - Отделяй теоретические предположения от подтверждённых фактов (код vs конфигурация vs runtime-телеметрия).

## Обязательный результат
| Домен / Компонент | Риск / Hotspot | Условие проявления | Влияние / Severity | Архитектурное смягчение (Mitigation) | Метрика / Evidence |
|---|---|---|---|---|---|
| Очереди (RabbitMQ/MQTT) | Накопление очереди команд | Отставание консьюмера / burst | High / Critical | DLX, TTL, prefetch limit, rate limiting | messages backlog, unack count, memory % |
| Watchdog & Lease | Crash loop при рестарте | Flapping процесса / jitter | Critical | Bounded recovery (max 5/600s), fail-closed | restart count, uptime, applied_deadline |
| База данных (PG/ORM) | Exhaustion пула asyncpg | Всплеск одновременных платежей | High | Pool max_size tuning, selectinload, timeout | active/idle pool conns, query latency p99 |

Дополнительно: список выявленных точек отказа (SPOF) и рекомендации по метрикам мониторинга.

## Связанные контракты и источники
- [Lease lifecycle](../../../.agent-context/contracts/lease-lifecycle.md), [RabbitMQ diagnostics](../rabbitmq-diagnostics/SKILL.md).
- [MQTT topic matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md), [Lease safety](../lease-and-watchdog-safety/SKILL.md).
- [Database ownership](../../../docs/etran_data-database-ownership.md), [Shared DB](../../../.agent-context/components/shared-db.md).
