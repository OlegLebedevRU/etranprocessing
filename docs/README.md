# Документация экосистемы etranprocessing

Центральный навигатор по технической, архитектурной и эксплуатационной документации платформы процессинга платежей и управления терминалами `etranprocessing`.

> 📌 **Стандарт именования файлов:** Все активные документы в каталоге `docs/` именуются по обязательному правилу двух мнемокодов с разделителем `{СФЕРА}_{ФЛОУ}-{дескриптивное-имя}.md`.  
> Спецификация и словарь мнемокодов: **[`etran_dev-documentation-naming-convention.md`](etran_dev-documentation-naming-convention.md)**.

---

## 1. Архитектура системы и модели данных

Документы, фиксирующие базовые принципы построения системы, доменные модели, разграничение владения таблицами БД и безопасность:

- **[`etran_arch-architecture-analysis.md`](etran_arch-architecture-analysis.md)** — Комплексный архитектурный анализ платформы: обзор доменов `ProcessingBackend` и `MenuBuilder`, потоки данных, контракты очередей и оценка рисков.
- **[`etran_data-database-ownership.md`](etran_data-database-ownership.md)** — Матрица владения базой данных PostgreSQL: разграничение прав на чтение/запись таблиц между `ProcessingBackend` и `MenuBuilder`, единый пакет `shared/etranprocessing_db`, правила создания Alembic-миграций.
- **[`menu_auth-multi-tenant-architecture.md`](menu_auth-multi-tenant-architecture.md)** — Мультитенантная архитектура аутентификации и авторизации: единый централизованный JWT-токен (RS256), хранение в cookie `accessToken`, сессии в БД/in-memory, контекст тенанта и переключение организаций суперюзером.
- **[`menu_auth-role-model-and-viewer.md`](menu_auth-role-model-and-viewer.md)** — Ролевая модель платформы, пользователь-наблюдатель (Viewer / роль 4), реестр permissions и управление пользователями тенанта для роли 3.
- **[`menu_auth-smartcaptcha.md`](menu_auth-smartcaptcha.md)** — CAPTCHA (Yandex SmartCaptcha) при входе и регистрации: настройки окружения, контракт `/api/auth/captcha`, поведение UI и ограничения.
- **[`etran_bill-licensing-architecture.md`](etran_bill-licensing-architecture.md)** — Авторитетная архитектура и математика лицензионного биллинга: статусы (`DISABLED`, `OVERDUE`, `DUE_SOON`, `ACTIVE`), независимый биллинг сертификатов, расчет периодов с сохранением дня якоря, корзина и чекаут.
- **[`menu_bill-terminal-subscription.md`](menu_bill-terminal-subscription.md)** — Новая модель подписок L4Desk, коммерческий переключатель, отдельная статистика длительности и порядок перехода со старой финансовой модели.
- **[`etran_arch-licensing-variants-audit-2026-10-05.md`](etran_arch-licensing-variants-audit-2026-10-05.md)** — Анализ Classic/L4Desk, сущностей, блокировок и конфликтов; read-only снимки tenants 1/339/10000, риски неявного enrollment и варианты дальнейшей реализации.
- **[`etran_cert-infrastructure-architecture.md`](etran_cert-infrastructure-architecture.md)** — Сквозная спецификация инфраструктуры сертификатов и mTLS: двухфакторный выпуск через PIN, терминальный установщик на C, проверка подлинности на Nginx и PowerShell-скрипты аудита.
- **[`etran_arch-serverless-email-integration.md`](etran_arch-serverless-email-integration.md)** — Архитектура и спецификация интеграции serverless-сервиса отправки email: email организации, подтверждение адресов, отправка финансовых и сменных отчетов, mTLS-проброс от терминалов.
- **[`etran_arch-l4media-streaming-architecture.md`](etran_arch-l4media-streaming-architecture.md)** — Архитектура подсистемы видеотрансляций `l4media`: прием mTLS видеопотоков L4RTP/1 от киосков (`leo4proxy`), декапсуляция, Janus WebRTC Gateway и интеграция с `MenuBuilder`.
- **[`etran_arch-l4media-resource-profiling-and-unit-budgets.md`](etran_arch-l4media-resource-profiling-and-unit-budgets.md)** — Профилирование аппаратных и сетевых ресурсов, юнит-бюджеты и расчет емкости стека видеотрансляций `l4media`: сравнительный анализ 480p против 720p и масштабирование на 2 одновременных стрима на сервере 87.242.100.34.
- **[`etran_arch-video-remote-desktop-e2e.md`](etran_arch-video-remote-desktop-e2e.md)** — Единая E2E-архитектура видеонаблюдения и удаленного управления: краткая и детальная части, схемы media/control/signaling, оркестрация и контракты MenuBuilder/app1/l4desk, задачи надежности и развертывания, направления Linux и ESP32-P4.
- **[`etran_arch-remote-input-control.md`](etran_arch-remote-input-control.md)** — Спецификация протокола удалённого ввода (remote input): аренда lease, input-аренда и capabilities, команды pointer/keyboard/shortcut, контракт l4desk-агента.
- **[`etran_arch-file-manager-remote-windows.md`](etran_arch-file-manager-remote-windows.md)** — Файловый менеджер удалённых Windows-устройств в MenuBuilder: гибрид MQTT RPC + S3 (cloud.ru `l4desk-fm-temp`), методы 7020–7022, FS-контур l4con, бенчмарк, CORS/PAB/lifecycle и образцы кода.
- **[`menu_arch-l4mcp-v1.md`](menu_arch-l4mcp-v1.md)** — MCP-доступ ролей 1/3/5, безопасная проверка агента перед консолью, PIN и порядок подключения клиентов.
- **[`menu_arch-l4mcp-user-events-plan.md`](menu_arch-l4mcp-user-events-plan.md)** — История событий900–999, tenant-контекст internal API, этапы MenuBuilder/l4mcp и E2E-проверки.
- **[`menu_arch-l4mcp-scenario-practice.md`](menu_arch-l4mcp-scenario-practice.md)** — События как память удалённых сценариев, формат результатов и пример предупредительного обслуживания Windows-клиентов; предложения следующих этапов.
- **[`ops_run-l4mcp-release.md`](ops_run-l4mcp-release.md)** — Адресный выпуск L4mcp с сохранением серверного Redis, приватного env и процедурой отката.

---

## 2. Сервисы бэкенда

Документация по серверным компонентам обработки платежей, терминальным интерфейсам и авторизационным шлюзам:

- **[`proc_pay-backend-architecture.md`](proc_pay-backend-architecture.md)** — Архитектура и справочник сервиса `ProcessingBackend`: обработка платежей от терминалов по HTTPS (mTLS), логирование транзакций, таблицы балансов, legacy-совместимость.
- **[`proc_cert-issuance-flow.md`](proc_cert-issuance-flow.md)** — Поток выпуска сертификатов терминалов (эндпоинты `CHECK` и `SETUP`), взаимодействие с внешним CA (Yandex Cloud Functions), ветвление криптопровайдеров (`v=26` CNG vs legacy).
- **[`ops_net-nginx-jwt-module.md`](ops_net-nginx-jwt-module.md)** — Руководство по сборке и конфигурации Nginx с модулем `ngx-http-auth-jwt-module`: проверка RS256 JWT на обратном прокси, валидация cookie и форвардинг заголовков идентичности.

---

## 3. Фронтенд и пользовательский интерфейс

Руководства по клиентскому веб-приложению `MenuBuilder/frontend` (портал администратора и управление меню):

- **[`menu_ui-frontend-architecture.md`](menu_ui-frontend-architecture.md)** — Архитектура SPA на React 19, TypeScript и Vite: роутинг, управление сессиями, контекст тенанта (`SessionContext`), организация API-клиентов и сборка.
- **[`menu_ui-patterns-and-tokens.md`](menu_ui-patterns-and-tokens.md)** — Шаблоны интерфейса, конвенции Ant Design 6, табличные плотности, дизайн-токены и правила построения форм.
- **[`menu_bill-cart-ux-requirements.md`](menu_bill-cart-ux-requirements.md)** — Спецификация интерфейса корзины биллинга: правила автовыбора лицензий, расчет задолженности, блокировки и логика продления.

---

## 4. Терминалы, оборудование и интеграции

- **[leo4proxy DNS/SRV: контекст реализации](term_net-leo4proxy-dns-srv-implementation-context.md)** — реализованные SRV/policy/IP routing и strict TLS, выпущенный tools 1.10.1, Upgrade773 ready/0 и оставшиеся проверки.

- **[Пользовательские события l4con](term_tool-l4con-user-events.md)** — тихий
  `--send-event` из удаленной консоли/l4mcp, теги 446–448 и ограничение потока.

Взаимодействие с терминальными устройствами (киосками), протоколы связи, телеметрия и нативные утилиты:

- **[`term_tool-architecture-guide.md`](term_tool-architecture-guide.md)** — Архитектура терминальных утилит (l4superv, l4pin, l4mon, l4gate, l4route): стек C/WinAPI, IPC-пайпы, шифрование и интеграция с Windows.
- **[L4 Tools: решения по стабилизации и риски](term_arch-l4tools-stabilization-decisions.md)** — принятые решения по сети, сертификатам, SCM-зависимостям, журналам, готовности и подписанному выпуску 1.9.6; границы проверок.
- **[`term_arch-leo4proxy-server-permission.md`](term_arch-leo4proxy-server-permission.md)** — MQTT/RTP и исходящий HTTPS leo4proxy: терминальный API ProcessingBackend, запас 72 часа, локальное хранение и сохранение внутреннего контракта.
- **[`term_tool-zero-touch-installer-and-remote-runtime-plan.md`](term_tool-zero-touch-installer-and-remote-runtime-plan.md)** — Архитектура и план перехода к Zero-Touch комплексу tools (Ревизия 4): единый установщик «нажал и забыл», преодоление любого начального состояния Windows-терминала (включая повторное использование уже установленного сертификата и отложенный ввод PIN с активным ожиданием), сквозной флоу «инсталляция-сертификация-старт», прямая публикация релизов в публичный Generic Artifact Registry, канал самообновления терминалов и завершение изолированных заданий 1–6 Этапа 1 (`prompts/prompt_step5_*`).
- **[`term_tool-user-guide.md`](term_tool-user-guide.md)** — Авторитетное руководство сервисного инженера по развертыванию и эксплуатации комплекса Leo4 Tools: актуальный релиз **1.10.2**, настройки сети и TLS, Zero-Touch (`l4setup`), матрица состояний Windows (S1–S10), коды возврата, структура отчета `install_summary.json`, откат и решение инцидентов.
- **[`term_run-l4mcp-binary-update.md`](term_run-l4mcp-binary-update.md)** — Пример промпта и проверенный сценарий адресного обновления l4capture и l4desk через l4mcp с хешами, резервными копиями и откатом.
- **[`term_tool-developer-guide.md`](term_tool-developer-guide.md)** — Руководство разработчика терминальных клиентских приложений: спецификации протоколов взаимодействия с локальным супервайзером.
- **[`term_conn-device-connection-and-audit.md`](term_conn-device-connection-and-audit.md)** — Спецификация REST API состояния связи терминалов: структура объекта `connection`, фиксация клонов устройств (`DEVICE_CLONE`) и коллизий сертификатов (`SN_COLLISION`), журнал аудита жизненного цикла.
- **[`term_conn-schannel-mqtt-cert-store.md`](term_conn-schannel-mqtt-cert-store.md)** — Руководство по организации mTLS для MQTT с использованием неэкспортируемых сертификатов из Windows Certificate Store через Python SChannel TLS Proxy.
- **[`term_dev-main-app-mqtt-client.md`](term_dev-main-app-mqtt-client.md)** — Краткая инструкция разработчика внутреннего MQTT-клиента основного приложения терминала (`main_app`, C#): параметры подключения, получение SN, сценарий LWT и соглашение по топикам.
- **[`term_tool-event75-inventory.md`](term_tool-event75-inventory.md)** — Инвентаризация событий метода 75 (event75): состав, теги и потребители.
- **[`ingress_iot/`](ingress_iot/)** — Протокол remote-input для ingress IoT (`remote-input-protocol.md`). Дублирует домен [`etran_arch-remote-input-control.md`](etran_arch-remote-input-control.md): **авторитетна архитектурная спецификация** `etran_arch-remote-input-control.md`; `ingress_iot/remote-input-protocol.md` — вариант протокола для ingress-контура (ctl/l4desk), при расхождении сверяться с эталоном.
- **[`l4capture/`](l4capture/)** — Архитектура и handoff-журналы разработки l4capture (захват экрана, OpenH264, RTP).
- **[`history/planning-and-research/`](history/planning-and-research/)** — В т.ч. перенесённые исторические материалы: `legacy-terminal-integration-guide.md`, `terminal-tools-user-guide.md` (устаревший ZIP-маршрут tools), `menu_arch-l4mcp-tenant-skill-library.md` (концепция без реализации).

---

## RPC7xxx и authenticated renewal7011

- [Фактический flow и матрица таймингов](term_arch-rpc7011-flow-matrix.md) — контракты, очереди, защита установки, recovery и acceptance.
- [План RPC7011](term_arch-rpc-7011-certificate-renewal-plan.md) — согласованные решения и этапы с переходом к реализованной матрице.
- [Аудит RPC7xxx](term_arch-rpc-7xxx-contract-audit.md) — исходные расхождения форматов, tasks/history и маршрута cancel.

## 5. DevOps, эксплуатация и мониторинг

Регламенты развертывания, управления инфраструктурой и диагностических операций:

- **[`ops_run-devops-runbook.md`](ops_run-devops-runbook.md)** — Регламент эксплуатации: инструкции по сборке и обновлению контейнеров, деплой на серверы, управление сертификатами, переключение legacy IIS и процедуры отката.
- **[`ops_run-beta-ci-cd.md`](ops_run-beta-ci-cd.md)** — Автономный CI/CD на выделенном builder: версионированные immutable-образы, ручной адресный запуск релиза, установка и восстановление (единственный действующий порядок деплоя).
- **[`ops_run-git-and-release-flow.md`](ops_run-git-and-release-flow.md)** — Выбор базовой ветки, вход в GitHub из нужного профиля Windows, builder/registry/production и безопасная очистка локальных копий.
- **[`ops_net-nginx-config-guide.md`](ops_net-nginx-config-guide.md)** — Справочник конфигурации Nginx: взаимная TLS-аутентификация (порт 4443), проксирование заголовков сертификатов, JWT-терминация (порт 443).
- **[`ops_net-infrastructure-connections.md`](ops_net-infrastructure-connections.md)** — Инфраструктурный справочник: сетевые адреса целевого сервера (`87.242.100.34`), Managed PostgreSQL (`10.0.0.7`), порты, параметры БД и окружений.
- **[`ops_run-remote-console-diagnostics.md`](ops_run-remote-console-diagnostics.md)** — Регламент удаленной диагностики терминалов и серверов через SSH, MQTT-каналы и операции с MCP Ops сервером (`server-ops`).

---

## 6. Руководства разработчика и ИИ-агентов

Практические руководства для быстрого погружения в проект и эффективной работы:

- **[`etran_dev-agent-context-workflow.md`](etran_dev-agent-context-workflow.md)** — Рабочая память агентов: skills, выборочные context distillates, маршрутизация ролей, task/release handoff и регламент актуальности; [быстрый индекс](../.agent-context/README.md).
- **[`etran_dev-documentation-naming-convention.md`](etran_dev-documentation-naming-convention.md)** — Стандарт именования файлов технической документации: формула префикса `{СФЕРА}_{ФЛОУ}-{имя}.md`, словарь мнемокодов и реестр.
- **[`etran_dev-quickstart.md`](etran_dev-quickstart.md)** — Руководство по быстрому старту: развертывание локального окружения, запуск PostgreSQL, выполнение Alembic-миграций и тестирование API.
- **[`etran_dev-ai-agent-reference.md`](etran_dev-ai-agent-reference.md)** — Справочник для ИИ-агентов: структура проекта, ключевые точки входа, стандарты кодирования (Python 3.14, SQLAlchemy 2.0 asyncpg, shared models), шаблоны частых изменений.

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

- [L4 Tools: финальные изменения, стабилизация, UI и подписанный выпуск](term_dev-l4tools-stabilization-ui-release-plan.md) — план от 2026-10-02.

- [Единый отчёт каскада L4 Tools](term_dev-l4tools-cascade-report.md) — результат и проверки каждого этапа.
