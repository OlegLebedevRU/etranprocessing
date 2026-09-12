# Agent handoff: селективная адаптация software-architecture-skills

## Контекст задачи
- Задача: Реализация плана селективного внедрения архитектурных навыков из https://github.com/45ck/software-architecture-skills.
- Scope: Документальные навыки в `.claude/skills/` и индекс `.agent-context/README.md`. Пакетный импорт «as-is» отклонён.
- Репозиторий: etranprocessing, working tree без commit.
- Дата: 2026-09-12.
- Владелец изменения: System Architecture / Docs Curator.

## Выполнено
1. **Адаптация adr-writer**:
   - Размещён в `.claude/skills/architecture-decision-record/SKILL.md`.
   - Дополнен ссылками на проектные правила и контракты (`.agent-context/contracts/`):
     - `lease-lifecycle.md` (fail-closed, bounded recovery);
     - `mqtt-topic-matrix.md` (запрет ad-hoc топиков и retained команд);
     - `remote-control.md`, `remote-console.md`, `video-streaming.md`;
     - `api-and-data-ownership` (тонкий ORM `shared/etranprocessing_db`, единый источник Alembic миграций `ProcessingBackend`, изоляция тенантов `org_id`).
   - Включает компактный скелет ADR с матрицей соответствия инвариантам проекта.

2. **Скомбинированный навык рисков производительности и отказоустойчивости**:
   - Размещён в `.claude/skills/performance-and-resilience-risk-analysis/SKILL.md`.
   - Объединяет `architecture-risk-assessor` и `scalability-hotspot-detector` в компактный чеклист по 3 критическим доменам платформы:
     - Очереди сообщений (RabbitMQ / MQTT / AMQP): backlog, unack, DLX, лимиты High/Low Watermark, flow control, retry storm;
     - Таймауты watchdog и lease: рассинхронизация keepalive/lease/watchdog (+5 с grace в l4desk), лимиты bounded recovery (5/600 с), fail-closed при expired lease, reconcile при перезапуске;
     - Нагрузка на PostgreSQL / asyncpg / Shared ORM: истощение пула соединений, N+1/selectinload, длительные транзакции, строчные блокировки балансов, фильтрация по `org_id`.
   - Содержит таблицу матрицы рисков с классификацией Severity и смягчающими мерами.

3. **Обоснование отказа от остальных навыков пакета 45ck**:
   - `integration-boundary-mapper`, `component-boundary-reviewer`, `runtime-view-writer`: задачи границ интеграции, контрактов продюсер/транспорт/консьюмер, QoS и payload уже полностью закрыты проектным специализированным навыком `cross-stack-contract-audit` и карточками `.agent-context/contracts/`.
   - `layered-architecture-designer`, `monolith-vs-modular-monolith-reviewer`, `service-decomposition-advisor`: неактуальны для текущего этапа устоявшейся микросервисной и mTLS/Nginx архитектуры платформы.
   - `tradeoff-analysis-writer`: встроен в секцию альтернатив и компромиссов `architecture-decision-record`.
   - `availability-strategy-reviewer`: скомбинирован в `performance-and-resilience-risk-analysis`.

4. **Регистрация в индексе**:
   - Обновлён `.agent-context/README.md`: добавлена роль `System Architect` в таблицу маршрутизации и оба навыка в `Каталог skills`.

## Затронутые контракты
| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| Архитектурные навыки | Agent / Developer | Agent / Developer | Совместимы с форматом `.claude/skills/` и `.agent-context/` |

## Изменённые инварианты
- Инварианты системы не ослаблялись. Новые навыки закрепляют обязательную проверку инвариантов контрактов при проектировании изменений.

## Проверено
- [x] Статическая проверка ссылок во всех созданных и изменённых Markdown-файлах: битых локальных ссылок нет (exit code 0).
- [x] Проверка форматирования: `git diff --check`, exit code 0.
- [x] Размеры навыков выдержаны в проектном компактном стиле (≤80 строк, без избыточного многословия).
- [ ] Runtime-тесты и линтеры кода не запускались (изменения касаются только документации и навыков).

## Риски и следующие действия
- При появлении новых контрактов в `.agent-context/contracts/` обновлять секцию проверки в `architecture-decision-record`.
- Использовать `performance-and-resilience-risk-analysis` при планировании изменений брокера RabbitMQ и оптимизации запросов БД.
