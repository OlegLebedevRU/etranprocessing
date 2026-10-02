# MenuBuilder L4Desk UI — локальная реализация

## Task intake
- Цель: внедрить согласованный план таблицы терминалов, видеопанели и запрета monitoring для роли 5.
- Scope: MenuBuilder frontend/backend; карточка компонента и этот handoff. Исходный HEAD `c6096f4`, результат в working tree без commit.
- Владелец: MenuBuilder — `Terminal.is_active`, portal visibility, UI/BFF и permissions. app1 — lease; терминальный агент — ввод/stream. Shared ORM и миграции не менялись.
- Producer → consumer: settings API + IoT device snapshot → browser; browser activity PATCH → tenant-scoped MenuBuilder DB write; video/control BFF → существующий app1 API.
- Инварианты: tenant scope и серверные permissions; certificate identity не меняется; input только с действующей lease; stop/release разрешены и для отключённого терминала.
- Ограничения: без деплоя до команды пользователя, без подключения к MQTT и изменений native agents. Запрещённые каталоги не исследованы.
- Валидация: локальные backend/UI-тесты, build, Ruff/Pyright, Playwright с mocked API. Это не live media E2E.

## Выполнено
- Терминалы: Онлайн по умолчанию; Оффлайн исключает отключённые; Отключённые/Все. Сортировка по device_id asc, прежние поиски/сортировки сохранены.
- Для корректной фильтрации до пагинации UI получает весь разрешённый список через уже существующий `all=true`, применяет live presence и лишь затем slice. При ошибке presence — предупреждение, неизвестное состояние и возможность открыть Все.
- Активация: сертификат, граница 30 дней, истёкший/не установлен/срок неизвестен. PIN/provisioning/retry сохранены в popover; PIN читается по запросу.
- Редактирование у названия, увеличенные Console/Video, отдельная зона activity, SN справа. Удалена информационная плашка подключения.
- Новый `PATCH /api/settings/terminals/{id}/activity`: target state, роль 3/5, собственный tenant, undeleted record, запрет отключения при незавершённом remote session (409). Повтор не переключает обратно.
- Activity и admission новых сеансов блокируют строку Terminal, чтобы исключить параллельное отключение/старт. Отключённый терминал запрещён на video lease/session/start/scope/events и unified console/video start. Cleanup остаётся доступен.
- Видео: единый Drawer с начальным Онлайн, без авто выбора первого терминала; закрытие после выбора, включая повторный выбор текущего. Deep links сохранены; смена проходит через прежний stopSession.
- Video/Console выбирают только активные записи из разрешённого списка MenuBuilder, исключая provider-only/deleted devices. Название/адрес доступны в видео выборе.
- VideoToolbar вместо четырёх блоков: выбор, источник, start/stop, единственный stream status, input, настройки/масштаб/fullscreen. Качество, SN, адрес, refresh — в настройках; клавиши — в popover с прежними ограничениями.
- Постоянные оверлеи live/input/scale убраны из изображения. Fullscreen имеет панель масштаба/выхода с автоскрытием и keyboard focus. Native size запрещён при active input.
- В эфире требует свежих browser frames через requestVideoFrameCallback; после 5 секунд без кадров — Ожидание кадров. Недоступный агент не обозначается как доказанная блокировка экрана.
- Роль 5: monitoring menu/route/API закрыты даже при wildcard permissions; login/default route ведут на terminals.
- Vite config использует import.meta.url вместо __dirname, поддерживая configLoader runner в ограниченном Windows workspace.

## Проверено
- [x] Backend: 55 tests passed (`test_terminal_activity`, `test_terminal_visibility`, `test_settings`, `test_video_stream_permissions`, `test_remote_session_orchestration`, `test_role4_viewer`, `test_mcp_waitlist_and_role5`).
- [x] Frontend: 16 tests passed (terminalPresentation, deviceSelection, session-lifecycle, l4desk-accessibility-responsive).
- [x] TypeScript + Vite: `npm run build -- --configLoader runner`; артефакт `MenuBuilder/frontend/dist`.
- [x] Ruff check app + новый test; Ruff format изменённых Python файлов; Pyright app — 0 errors.
- [x] Local Chromium/Playwright + mocked API: прямой monitoring redirect роли 5, online filter, enable через confirmation и выход строки из disabled filter, начальный online Drawer, исключение provider-only/disabled, выбор и повторный выбор с закрытием Drawer.
- [x] Local mocked browser: 1440×900 — полоса 52 px, начало плеера y=124; 390×844 — около y=220, без горизонтального overflow и JS errors. До изменения сайт имел y≈271 / 594 соответственно.
- [ ] Live video/control, fullscreen с реальной active input и backend/DB integration на стенде — после отдельно разрешённого выпуска.
- [x] Registry/build/deploy/runtime image checks — выполнены пользовательским запуском; подтверждены предоставленным логом (см. ниже), без независимого SSH-аудита агента.

## Evidence и ограничения
- Уровень: локальный код + local tests + mocked browser. Никаких claims о внедрении на сайт.
- Первый стандартный запуск Vite был ограничен sandbox при чтении родительских каталогов; configLoader runner прошёл. Исходные Playwright селекторы поправлены под accessibility names/Ant Design v6, затем сценарии выполнены.
- В backend suite 5 предупреждений существующих тестовых mocks/Starlette. В build предупреждение о размере vendor-antd chunk; новые зависимости и lockfiles не изменялись.
- Full-list filtering требует объёма данных, пропорционального числу терминалов tenant; для очень больших организаций дальнейшая оптимизация — BFF aggregate snapshot, без перехода к фильтрации только текущей страницы.
- Секреты/PIN/JWT в handoff не сохранялись. Browser contexts закрыты; временные screenshots удалены, локальный preview остановлен. Автоматическая проверка отклонила recursive cleanup с причиной `blocked by policy`: в `%TEMP%` остались `mb-ui-npm-cache` и `mb-ui-uv-cache`. Код/артефакт сборки не затронуты.

## Выпуск после команды
- Проверить/принять изменения в main; собрать MenuBuilder backend на явно разрешённом builder, опубликовать immutable digest и подтянуть только menubuilder-backend на production.
- Затем доставить собранный frontend dist в существующий live mount. Миграции не нужны; порядок backend → frontend обеспечивает наличие activity API.
- Проверить image digest, health, role 5 direct route/API denial, activity/filters/certificate states, выбор/video/input/cleanup. Не пересоздавать соседние сервисы.
- Пользователь разрешил push/deploy по ранее показанному маршруту; разрешение получено 2026-10-02.

## Подготовка выпуска 2026-10-02
- Общий Git-каталог исходного worktree недоступен для записи sandbox. Создан независимый checkout `.ui-release-checkout` от текущего `origin/main` `9fcca6ca709aacdbd65c2f710628c1bf4fbc95b5`; изменения перенесены через three-way apply без конфликтов, новые файлы скопированы по явному списку.
- На новом base повторены Ruff check/format, Pyright (с явным Python из проверенного venv) — 0 errors, 0 warnings, и 55 backend tests — passed. Frontend в upstream не изменялся; предыдущие 16 tests и build относятся к тем же frontend-исходникам.
- Read-only scan всех staged файлов на private keys, credential URLs, типовые токены и литералы паролей — без совпадений.
- GitHub `gh auth status` сообщает о недействительной авторизации текущего sandbox-профиля. Обычный `git push --dry-run` завершился с exit 1 без вывода; пользователь подтвердил неисправность `git-remote-https.exe`. Публикация не выполнена, обход через Git Data API не применялся.
- SSH на production и builder завершился `Host key verification failed`; SSH этого профиля ищет known_hosts под C:\Windows. Проверка host key не отключалась. MCP Ops tools отсутствуют: readiness UNAVAILABLE; нагрузка серверов не проверена, mutating server commands не выполнялись.
- Создан `deploy/Clear-UiTaskTemp.ps1`: по умолчанию preview, `-Execute` удаляет только два task cache в текущем TEMP после проверки всех целей, предков и вложенных reparse points. Preview успешно перечислил обе цели. Удаление не запускалось; рабочие каталоги, dist, node_modules и venv скрипт не затрагивает.
- Независимый checkout сохраняется как единственная доступная для commit копия актуального main с результатом. Не удалять его до подтверждённой публикации коммита.

## Публикация и ручной запуск
- Пользователь подтвердил GitHub device login для профиля агента. GitHub API подтвердил аккаунт; credentials использовались из временного GH_CONFIG_DIR вне репозитория, без вывода токена.
- Credential-helper завершал git push с exit 1. Нормальный HTTPS git push восстановлен передачей OAuth Authorization header через process environment, без записи в Git config/исходники и без Git Data API. Dry run прошёл; опубликован `d538b74fd03c443b10ae463d8f17cc0ea7c82402`, удалённый main независимо проверен через ls-remote.
- Пользователь попросил скрипт и команду запуска вместо продолжения блокируемого SSH из sandbox. Создан `deploy/Deploy-MenuBuilderUi.ps1`: обязательные host/key/revision параметры, preview по умолчанию, `-Execute` вызывает штатный beta launcher только для backend, затем frontend. Frontend registry image используется как носитель dist; штатный deployer сохраняет live mount и проверяет HTTP без restart nginx.
- Script проверяет trusted known_hosts, main SHA до/после каждого компонента, ресурсы обоих серверов, nginx mount, production release records/digests, backend runtime revision/image ID, неизменность соседних контейнеров. Main должен оставаться на согласованном SHA в течение запуска; изменившийся main требует нового review.
- Script проверен Windows PowerShell parser/preview и mock success/resource-failure сценариями: ровно два component launches при успехе и отсутствие mutating commands при неуспешном resource preflight. Реальный деплой в этой сессии не выполнялся; следующий шаг — запуск пользователем из его PowerShell с доступом к SSH-файлам.
- Script опубликован вместе с handoff в main `5b4a880e2e88dea58a5b1c9399bd11e3b0f709f6`; SHA independently checked by ls-remote. Временная авторизация агента затем удалена через gh auth logout; пользовательский GH_CONFIG_DIR не изменялся.

## Исправление Windows SSH quoting
- Реальный пользовательский запуск остановился до mutating commands на resource probe: Windows PowerShell 5 удалил double quotes в POSIX escape `'"'"'`, испортив литералы Python. Предыдущие mocks не проверяли native argument marshalling.
- Локальные `deploy/Deploy-MenuBuilderUi.ps1` и копия в `.ui-release-checkout` исправлены: Shell-Quote использует POSIX escape с backslash без embedded double quotes. Это не требует изменения SHA приложения, который остаётся `5b4a880e2e88dea58a5b1c9399bd11e3b0f709f6`.
- Проверен реальный Windows PowerShell -> native Git bash -> Python roundtrip с multiline Python, single quotes, dictionary keys и JSON response. Parser/preview прошли. SSH-проверки с production из sandbox не заявлены.
- Исправление скрипта локальное, публикация этого исправления отдельно не выполнялась: временный OAuth-вход агента уже удалён. Для немедленного повторного запуска пользователем актуален исходный absolute script path и прежний Revision.

## Подтверждение пользовательского выпуска
- Прочитан предоставленный пользователем `D:\.codex\deploy1.txt`. Уровень evidence: полный лог реального пользовательского запуска, не независимый SSH inspection агента.
- Ревизия backend и frontend: `5b4a880e2e88dea58a5b1c9399bd11e3b0f709f6`; resource preflight обоих серверов прошёл.
- Backend: Ruff passed, Pyright 0 errors/0 warnings, 598 tests passed (52 warnings). Digest `sha256:e3c95619f15868ccc0b07ea10e372b8019ce5575718ad6c3949360e733e8fc2e`; deployer подтвердил revision/image, скрипт подтвердил runtime image ID и running state.
- Frontend: 69 tests passed, production build прошёл; digest `sha256:5253a37ed39f3438fc17922184198c39a5fa7b901e7806162429f480ef0525a5`. Штатный deployer подтвердил отдачу нового index через nginx HTTP и отсутствие nginx restart.
- Лог завершён `Deployment verified`; скрипт прошёл сравнение соседних контейнеров. ConnectionRefusedError относился к первой health-пробе сразу после старта backend; штатный wait_healthy повторил запрос и завершился успешно, отката реального выпуска не было.
- Предупреждения в логе: npm audit сообщает 3 vulnerabilities (2 moderate, 1 high), крупный vendor-antd chunk, backend test warnings и существующий orphan container. Они не останавливали выпуск; автоматическое удаление orphan/container или npm audit fix не выполнялось.
- Browser проверки role 5 monitoring API/direct route, настоящей трансляции/input и поведения UI после выпуска всё ещё не подтверждены этим логом.

## Corrective: profile and lease 500
- Task intake: MenuBuilder frontend navigation and backend terminal locking only; MB owns terminal activity/admission, frontend requests a lease via BFF. Tenant isolation, site restrictions, certificate identity, input lease gating and cleanup remain invariants. No shared models, schema, migrations, tenant data or user roles are modified. Existing schema compatibility guard remains enabled; no Alembic commands are required.
- Applied intake routing, frontend remote-control safety and migration ownership rules. Producer/consumer reviewed: browser video start -> control/lease -> terminal lookup/policy -> IoT lease; no MQTT client/protocol changes.
- Live Playwright with the supplied test account: fresh login goes to /terminals with L4Desk. With pre-existing `app_nav_profile:<tenant>=classic`, the same login redirects to /menu/terminals in Classic. Account /auth/me has role 5, site_mode both, default_site null. Credentials and session cookies were held only in the test process; no credentials, PINs or JWTs were logged or saved.
- Live direct /video?device_id=1000009: inventory, stream/state and control/status return 200. Clicking Start reproduces 500 at POST /api/v1/video/devices/1000009/control/lease; response body is generic Internal Server Error. No stream started. Initial unauthenticated /auth/me and /auth/refresh 401 before login are expected, not video errors.
- Code-level cause found: Terminal eagerly outer-joins nullable terminal_type; unconstrained FOR UPDATE locks the nullable join side, which PostgreSQL rejects. Changed all three new terminal locks (activity, video admission, unified remote session admission) to FOR UPDATE OF terminals, preserving serialization against activity changes. Server traceback was not independently read, so additional downstream failures after this correction remain possible until runtime retest.
- Role 5 navigation is forced to L4Desk before saved profile, URL and default_site; Classic routes/switching are denied. Site restrictions remain authoritative in isProfileAllowed; a tenant/role configuration conflict is not automatically migrated. Role 3 retains Classic by default for both-site tenants and can explicitly select L4Desk. User's stored choices for switchable roles remain supported.
- Local checks: 48 backend tests including schema compatibility, Ruff check/format and Pyright 0 errors/0 warnings; 70 frontend tests, TypeScript/Vite build passed. Regression SQL tests compile actual ORM queries with PostgreSQL dialect and verify outer join plus FOR UPDATE OF terminals for all affected paths.
- Local production build + Playwright mock API: role 5 with stale Classic and ?profile=classic stays in L4Desk, direct /menu/terminals redirects away, Classic switch absent; role 3 starts Classic and switching to L4Desk persists correctly. No JS page errors. An earlier mock returned the wrong array shape for menu-variants; fixture corrected before final pass.
- Deploy script now supports explicit -Publish from the user's authenticated PowerShell: clean/pinned independent checkout, exact parent/main baseline, ordinary HTTPS git push with ephemeral OAuth header, preservation/restoration of inherited process Git config, independent main SHA check, then existing backend/frontend release flow. Parser/preview and mock publish/release/environment cleanup/changed-main refusal passed. This does not run or introduce migrations.
- Corrective runtime release is pending the user's script execution; the agent's temporary OAuth login was already removed and sandbox SSH known_hosts remains inaccessible. Current-site repro and local fixed-build tests are separate evidence, not a claim that these fixes are already on the site.

## Final consolidation 2026-10-03
- Published main independently verified at `e9091fe86c2921fe3337f5eac55b71117a1c1582`; code/tests from the original workspace match the published independent checkout. The original component card lacked two newer upstream evidence blocks; these were preserved from main before updating its stale release claims.
- The remaining source change is the deployment script's command-scoped safe.directory for the resolved source checkout. It enables the operator to use the sandbox-owned checkout without modifying global Git config or trusting arbitrary directories. HEAD verification failures now have a distinct error from revision mismatch.
- Operator push/deploy failed at ownership verification before either operation began. Subsequent corrective deployment is not confirmed. The original worktree remains based on an older HEAD; it is not an additional unpublished implementation.
- Added root ignore for `.ui-release-checkout` to prevent accidental inclusion of the nested release repository. Personal Git/GH/SSH settings, tokens, known_hosts, private keys, env files, caches, node_modules, venv and dist stay local and are not staged.
- Only deploy script, project ignore rule and evidence documentation change in this consolidation. No additional MenuBuilder source checks or schema/migration runs are required; earlier code checks remain applicable. Current script parser and command-scoped Git operations passed; pre-commit secret scan is required before final publication.
