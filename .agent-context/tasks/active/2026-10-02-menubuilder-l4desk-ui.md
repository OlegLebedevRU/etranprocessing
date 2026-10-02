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
- [ ] Registry/build/deploy/runtime image checks — не выполнялись; выпуск разрешён, но заблокирован Git/SSH-транспортом (см. ниже).

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
