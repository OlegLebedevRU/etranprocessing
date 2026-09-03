# История активной разработки и ретроспектива проектов etranprocessing

> ⚠️ **ВНИМАНИЕ ДЛЯ ИИ-АГЕНТОВ И РАЗРАБОТЧИКОВ (AI CONTEXT ISOLATION)**
> 
> Все материалы в этой директории (`docs/history/`) представляют собой **исторические артефакты активной разработки**:
> чекпоинт-аудиты, промежуточные отчеты ревью, пошаговые журналы реализации, первоначальные исследования легаси-систем и архивные промпты/черновики.
> 
> **ЭТИ ДОКУМЕНТЫ НЕ ЯВЛЯЮТСЯ ИСТОЧНИКОМ АКТУАЛЬНОЙ АРХИТЕКТУРНОЙ И ТЕХНИЧЕСКОЙ ИСТИНЫ.**
> Описанные в них временные решения, проблемы или исходные планы могли быть видоизменены, переработаны или закрыты в последующих коммитах.
> 
> **НЕ ИСПОЛЬЗУЙТЕ ДАННЫЕ ФАЙЛЫ ДЛЯ ПОСТРОЕНИЯ КОНТЕКСТА ТЕКУЩИХ ЗАДАЧ.**
> Вся актуальная спецификация, правила владения базами данных, потоки авторизации и регламенты находятся в каталоге **[`docs/`](../)**.
> Главный навигатор по актуальной документации: **[`docs/README.md`](../README.md)**.

---

## Структура архива истории разработки

### 1. `reviews/` — Отчеты аудитов и чекпоинт-ревью
Промежуточные ревизии и аналитические отчеты, подготовленные в процессе аудитов контрольных точек системы:
- **[`alfa-checkpoint-review.md`](reviews/alfa-checkpoint-review.md)** (21.08.2026) — Первичный архитектурный анализ перемешивания эндпоинтов и моделей между `ProcessingBackend` и `MenuBuilder`. Исторический аудит, предшествовавший выносу `shared/etranprocessing_db` и миграции биллинга.
- **[`billing-implementation-review.md`](reviews/billing-implementation-review.md)** (18.08.2026) — Комплексный аудит первой реализации биллинга (коммит 6758328), включающий гэп-анализ схемы БД, расчетных формул и транзакционного баланса.
- **[`tenant-readiness-review.md`](reviews/tenant-readiness-review.md)** (03.09.2026) — Deploy Readiness Review по переводу фронтенда и MenuBuilder на мультитенантную модель (RS256 JWT, cookie, сессии).
- **[`ui-services-tenancy-review.md`](reviews/ui-services-tenancy-review.md)** (26.08.2026) — Первичный отчет по отсутствию тенантности и авторизации в CRUD-роутерах меню (`menu_variants`, `groups`, `services`).
- **[`models-duplicate-analysis.md`](reviews/models-duplicate-analysis.md)** (22.08.2026) — Анализ дублирования и дрейфа моделей SQLAlchemy между `ProcessingBackend` и `MenuBuilder`, послуживший основой создания `shared/etranprocessing_db`.
- **[`device-collision-response-analysis.md`](reviews/device-collision-response-analysis.md)** (30.08.2026) — Анализ реального ответа API по тестовому терминалу (6209) при обнаружении коллизий сертификатов.

### 2. `implementation-logs/` — Журналы пошаговой реализации задач
Детальная хронология выполнения крупных архитектурных рефакторингов с фиксацией шагов, тестов и контрольных точек:
- **[`tenant-access-implementation-log.md`](implementation-logs/tenant-access-implementation-log.md)** (03.09.2026) — Полный журнал выполнения 9 шагов внедрения целевой мультитенантности: единый RS256 JWT, cookie, session store, суперюзерский переключатель тенантов.
- **[`models-duplicate-implementation.md`](implementation-logs/models-duplicate-implementation.md)** (22.08.2026) — Журнал создания пакета `shared/etranprocessing_db`, объединения 23 моделей и перевода бекендов на единый декларативный слой.

### 3. `planning-and-research/` — Исторические планы и исследования миграции
Первоначальные планы и исследования легаси-компонентов до их реализации в новом стеке:
- **[`billing-implementation-plan-2026-08-19.md`](planning-and-research/billing-implementation-plan-2026-08-19.md)** (19.08.2026) — Исходный пошаговый план разработки лицензионного биллинга (актуальная архитектура: [`docs/etran_bill-licensing-architecture.md`](../etran_bill-licensing-architecture.md)).
- **[`billing-cert-licensing-analysis-2026-08-18.md`](planning-and-research/billing-cert-licensing-analysis-2026-08-18.md)** (18.08.2026) — Сравнительный анализ привязки сертификатов и лицензий между легаси ASP.NET и новым FastAPI.
- **[`legacy-listmenuservice-research.md`](planning-and-research/legacy-listmenuservice-research.md)** (21.08.2026) — Исследование легаси WCF/OWIN сервиса `ListMenuFile` для его переноса в FastAPI.

### 4. `prompts-and-drafts/` — Архивные промпты и рабочие черновики
Черновики задач, выгрузки контекста и исходные требования активной разработки (ранее располагавшиеся в `prompt-arch/`):
- **[`prompt-tenant-architecture-access.md`](prompts-and-drafts/prompt-tenant-architecture-access.md)** — Комплексное исходное задание по целевой архитектуре доступа фронтенда к API тенанта.
- **[`prompt-users-and-jwt.txt`](prompts-and-drafts/prompt-users-and-jwt.txt)** — Исходные требования к подсистеме пользователей, сессий и централизованному JWT issuer.
- **[`prompt-billing-first-plan.txt`](prompts-and-drafts/prompt-billing-first-plan.txt)** — Черновой набросок первичного плана биллинга.
- **[`prompt-billing-update.txt`](prompts-and-drafts/prompt-billing-update.txt)** — Черновик расширения биллинговой логики.
- **[`prompt-target-architecture-notes.txt`](prompts-and-drafts/prompt-target-architecture-notes.txt)** — Черновые заметки по целевой архитектуре и взаимодействию сервисов.
- **[`prompt-legacy-proxy-cutover.txt`](prompts-and-drafts/prompt-legacy-proxy-cutover.txt)** — План переключения легаси-прокси на новый стек.
- **[`prompt-device-sn-formula.txt`](prompts-and-drafts/prompt-device-sn-formula.txt)** — Формула вычисления серийных номеров SN для терминалов.
- **[`prompt-terminal-configurations-list.txt`](prompts-and-drafts/prompt-terminal-configurations-list.txt)** — Справочные данные и режимы терминалов.
- **[`prompt-ui-services-tenancy.txt`](prompts-and-drafts/prompt-ui-services-tenancy.txt)** — Исходная выжимка дефектов тенантности в роутерах меню.
