# Документация экосистемы etranprocessing

Центральный навигатор по технической, архитектурной и эксплуатационной документации платформы процессинга платежей и управления терминалами `etranprocessing`.

---

## 1. Архитектура системы и модели данных

Документы, фиксирующие базовые принципы построения системы, доменные модели, разграничение владения таблицами БД и безопасность:

- **[`etranprocessing-architecture-analysis.md`](etranprocessing-architecture-analysis.md)** — Комплексный архитектурный анализ платформы: обзор доменов `ProcessingBackend` и `MenuBuilder`, потоки данных, контракты очередей и оценка рисков.
- **[`database-ownership.md`](database-ownership.md)** — Матрица владения базой данных PostgreSQL: разграничение прав на чтение/запись таблиц между `ProcessingBackend` и `MenuBuilder`, единый пакет `shared/etranprocessing_db`, правила создания Alembic-миграций.
- **[`multi-tenant-auth-architecture.md`](multi-tenant-auth-architecture.md)** — Мультитенантная архитектура аутентификации и авторизации: единый централизованный JWT-токен (RS256), хранение в cookie `accessToken`, сессии в БД/in-memory, контекст тенанта и переключение организаций суперюзером.
- **[`billing-architecture.md`](billing-architecture.md)** — Авторитетная архитектура и математика лицензионного биллинга: статусы (`DISABLED`, `OVERDUE`, `DUE_SOON`, `ACTIVE`), независимый биллинг сертификатов, расчет периодов с сохранением дня якоря, корзина и чекаут.
- **[`certificate-architecture.md`](certificate-architecture.md)** — Сквозная спецификация инфраструктуры сертификатов и mTLS: двухфакторный выпуск через PIN, терминальный установщик на C, проверка подлинности на Nginx и PowerShell-скрипты аудита.

---

## 2. Сервисы бэкенда

Документация по серверным компонентам обработки платежей, терминальным интерфейсам и авторизационным шлюзам:

- **[`processing-backend.md`](processing-backend.md)** — Архитектура и справочник сервиса `ProcessingBackend`: обработка платежей от терминалов по HTTPS (mTLS), логирование транзакций, таблицы балансов, legacy-совместимость.
- **[`certificates-flow.md`](certificates-flow.md)** — Поток выпуска сертификатов терминалов (эндпоинты `CHECK` и `SETUP`), взаимодействие с внешним CA (Yandex Cloud Functions), ветвление криптопровайдеров (`v=26` CNG vs legacy).
- **[`jwt-flow.md`](jwt-flow.md)** — Руководство по сборке и конфигурации Nginx с модулем `ngx-http-auth-jwt-module`: проверка RS256 JWT на обратном прокси, валидация cookie и форвардинг заголовков идентичности.

---

## 3. Фронтенд и пользовательский интерфейс

Руководства по клиентскому веб-приложению `MenuBuilder/frontend` (портал администратора и управление меню):

- **[`menubuilder-frontend-architecture.md`](menubuilder-frontend-architecture.md)** — Архитектура SPA на React 19, TypeScript и Vite: роутинг, управление сессиями, контекст тенанта (`SessionContext`), организация API-клиентов и сборка.
- **[`ui-patterns.md`](ui-patterns.md)** — Шаблоны интерфейса, конвенции Ant Design 6, табличные плотности, дизайн-токены и правила построения форм.
- **[`billing-cart-ux-requirements.md`](billing-cart-ux-requirements.md)** — Спецификация интерфейса корзины биллинга: правила автовыбора лицензий, расчет задолженности, блокировки и логика продления.

---

## 4. Терминалы, оборудование и интеграции

Взаимодействие с терминальными устройствами (киосками), протоколы связи, телеметрия и нативные утилиты:

- **[`terminal-tools-architecture-guide.md`](terminal-tools-architecture-guide.md)** — Архитектура терминальных утилит (l4superv, l4pin, l4mon, l4gate, l4route): стек C/WinAPI, IPC-пайпы, шифрование и интеграция с Windows.
- **[`terminal-tools-user-guide.md`](terminal-tools-user-guide.md)** — Руководство оператора и администратора по терминальным утилитам: Zero-to-Start, развертывание, конфигурирование и траблшутинг.
- **[`terminal-apps-developer-guide.md`](terminal-apps-developer-guide.md)** — Руководство разработчика терминальных клиентских приложений: спецификации протоколов взаимодействия с локальным супервайзером.
- **[`api-device-connection-and-audit.md`](api-device-connection-and-audit.md)** — Спецификация REST API состояния связи терминалов: структура объекта `connection`, фиксация клонов устройств (`DEVICE_CLONE`) и коллизий сертификатов (`SN_COLLISION`), журнал аудита жизненного цикла.
- **[`schannel-mqtt-cert-store-guide.md`](schannel-mqtt-cert-store-guide.md)** — Руководство по организации mTLS для MQTT с использованием неэкспортируемых сертификатов из Windows Certificate Store через Python SChannel TLS Proxy.

---

## 5. DevOps, эксплуатация и мониторинг

Регламенты развертывания, управления инфраструктурой и диагностических операций:

- **[`devops-runbook.md`](devops-runbook.md)** — Регламент эксплуатации: инструкции по сборке и обновлению контейнеров, деплой на серверы, управление сертификатами, переключение legacy IIS и процедуры отката.
- **[`nginx-config.md`](nginx-config.md)** — Справочник конфигурации Nginx: взаимная TLS-аутентификация (порт 4443), проксирование заголовков сертификатов, JWT-терминация (порт 443).
- **[`infrastructure-and-migration-connections.md`](infrastructure-and-migration-connections.md)** — Инфраструктурный справочник: сетевые адреса серверов (`176.108.247.249`, `87.242.100.34`), порты, параметры БД и окружений.
- **[`remote-console-diagnostics-flow.md`](remote-console-diagnostics-flow.md)** — Регламент удаленной диагностики терминалов и серверов через SSH, MQTT-каналы и операции с MCP Ops сервером (`server-ops`).

---

## 6. Руководства разработчика и ИИ-агентов

Практические руководства для быстрого погружения в проект и эффективной работы:

- **[`quickstart.md`](quickstart.md)** — Руководство по быстрому старту: развертывание локального окружения, запуск PostgreSQL, выполнение Alembic-миграций и тестирование API.
- **[`ai-agent-reference.md`](ai-agent-reference.md)** — Справочник для ИИ-агентов: структура проекта, ключевые точки входа, стандарты кодирования (Python 3.14, SQLAlchemy 2.0 asyncpg, shared models), шаблоны частых изменений.

---

## 7. История активной разработки и ретроспектива

> ⚠️ **Материалы в архиве истории разработки изолированы от основного контекста.**
> Они отражают этапы проектирования, промежуточные аудиты и журналы выполнения задач на разных фазах проекта.
> 
> Полный каталог архивных материалов доступен в **[`docs/history/`](history/)**:
> - **[`docs/history/README.md`](history/README.md)** — Навигатор по архиву и соглашение об изоляции контекста
> - **[`docs/history/reviews/`](history/reviews/)** — Отчеты аудитов контрольных точек (alfa-checkpoint, billing review, tenant readiness, models duplicate analysis, collision response analysis)
> - **[`docs/history/implementation-logs/`](history/implementation-logs/)** — Детальные пошаговые журналы реализации (tenant access log, models duplicate implementation)
> - **[`docs/history/planning-and-research/`](history/planning-and-research/)** — Исторические планы и исследования легаси-сервисов
> - **[`docs/history/prompts-and-drafts/`](history/prompts-and-drafts/)** — Архивные промпты и черновики активной разработки
