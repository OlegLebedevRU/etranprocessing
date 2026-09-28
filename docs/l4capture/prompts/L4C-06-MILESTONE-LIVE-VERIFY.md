# L4C-06-MILESTONE-LIVE-VERIFY — M-1: живая сквозная верификация native desktop capture

    prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
    scope_project: tools/l4capture + tools/l4desk
    scope_root: D:\repo\platerra\Public\etranprocessing
    prompt_type: milestone-live-verification
    required_handoff_ids: H-L4C-05-v1
    sequence_gate_status: READY_FOR_L4C_06
    output_handoff_id: H-L4C-06-v1
    next_prompt_id: L4C-07-DXGI-CAPTURE
    branch: l4capture/l4c-06-milestone-live-verify
    report_path: docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md
    candidate_format: DETACHED_V1
    candidate_path: docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-candidate.md
    architecture_sections: 2, 3, 7, 8, 9, 10, 12, 13, 14
    consumers: L4C-07-DXGI-CAPTURE, ALL_FOLLOWING

---

## 1. Назначение

Провести воспроизводимую live-проверку минимального сквозного тракта:

    MenuBuilder → существующая orchestration → l4desk
    → IPC + Job Object → l4capture (GDI + OpenH264, base_480p)
    → RTP/RTCP loopback → leo4proxy → l4media → Janus → browser

M-1 проверяет не только наличие видеокадра, но и обязательные классы риска:

- R1 — безопасность lease и пользовательской сессии;
- R2 — внешняя доставка и свежесть кадра;
- R3 — внутренняя устойчивость, bounded queues и recovery;
- R4 — совместимость, DPI и геометрия.

Этот шаг не вносит скрытых runtime-исправлений. При обнаружении дефекта результатом является `BLOCKED_*`; исправление оформляется отдельным corrective-step, после чего M-1 повторяется полностью.

---

## 2. Scope и запреты

Разрешено:

- подготовить evidence в `tools/l4capture` и `tools/l4desk`;
- создать отчёт и candidate в `docs/l4capture/handoffs`;
- читать штатные статусы согласованных компонентов;
- проводить fault-тесты только на согласованном тестовом терминале и тестовом маршруте.

Запрещено:

- менять код, конфигурацию production-серверов, маршруты, БД, очереди или внешние контракты;
- изменять `MenuBuilder`, `l4media`, `leo4proxy`, `ProcessingBackend`, `shared`, `BACK`, `FRONT`;
- использовать `l4desk-service`;
- останавливать legacy tools без отдельного указания владельца тестового стенда;
- считать M-1 принятым без личного вердикта владельца `НОРМ`.

---

## 3. Contract Gate

До начала:

1. Проверить, что в `docs/l4capture/prompts/contract-handoff.md` присутствует ровно один блок `H-L4C-05-v1` со статусом `ACCEPTED`.
2. Сверить SHA-256 всех артефактов входного handoff:
    - `docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md`;
    - `tools/l4desk/include/media_backend.h`;
    - `tools/l4desk/include/l4capture_adapter.h`;
    - `tools/l4desk/include/input_gate.h`;
    - `tools/l4desk/include/kiosk_focus.h`;
    - `tools/l4desk/include/kiosk_lifecycle.h`;
    - `tools/l4desk/bin/x86/l4desk.exe`;
    - `tools/l4desk/bin/x64/l4desk.exe`.
3. Зафиксировать текущие SHA-256 `l4desk.exe` и `l4capture.exe`, commit, ветку и пилотную конфигурацию.
4. Проверить, что включены только пилотные feature flags:
    - `l4capture_backend_adapter`;
    - `l4capture_input_gate`;
    - `l4desk_kiosk_focus_guard`;
    - `l4desk_kiosk_lifecycle_backend`.
5. Получить подтверждение владельца о доступности тестового терминала и разрешении на согласованные fault-тесты.

Любое несоответствие означает `BLOCKED_CONTRACT` либо `BLOCKED_ENVIRONMENT`.

---

## 4. Паспорт стенда

До первого запуска в отчёте фиксируются:

| Поле | Значение |
|---|---|
| Terminal ID / SN | Безопасно сокращённый идентификатор |
| ОС | Edition, version, build, разрядность |
| CPU | Точная модель и число логических CPU |
| RAM | Установленный объём |
| Display | Исходный raster, DPI, количество дисплеев, наличие отрицательного origin |
| Driver | Версия видеодрайвера |
| Browser | Название и версия |
| Network | Тестовый маршрут и метод fault-инъекции |
| Media profile | Запрошенный `low`, фактический `854x480`, 10 FPS |
| Binaries | SHA-256 `l4desk.exe` и `l4capture.exe` |
| Build mode | x86/x64, `/MT`, отсутствие VC Redistributable |
| Legacy tools | Список процессов, решение: оставить / временно остановить |

Не включать в evidence секреты, ключи, PIN, сертификаты, содержимое рабочего экрана или запись экрана пользователя.

---

## 5. Как начать тесты

1. Не останавливать legacy tools заранее.
2. Выделить тестовый Windows 10 x64 терминал и отдельную тестовую учётную запись.
3. Убедиться, что на терминале нет активной video/console-сессии.
4. Зафиксировать baseline:
    - количество `l4capture.exe`;
    - Private Bytes и Working Set;
    - GDI handles;
    - RTP/RTCP loopback-порты;
    - состояние route/mountpoint;
    - browser `framesDecoded`.
5. Подготовить движущийся тестовый экран: секундомер, курсор, мелкий текст и прокрутку.
6. Выполнить 10 штатных циклов `start → first frame → stop`.
7. Только после зелёного baseline перейти к fault-тестам, начиная с R1.

Если для конкретного сценария понадобится отключить legacy process, исполнитель сначала указывает владельцу точный процесс, PID, причину, команду остановки и команду восстановления. Остановку и запуск выполняет только владелец стенда.

---

## 6. Обязательные проверки

Каждый fault-сценарий выполняется не менее трёх раз. Для каждой попытки фиксируются: время воздействия, ожидаемый результат, фактический результат, latency, состояние процессов, RTP-метрики, browser-метрики и итог `PASS` или `FAIL`.

### 6.1. Базовый просмотр

Выполнить:

1. 10 последовательных штатных запусков и остановок.
2. 30 минут работы с мелким текстом, прокруткой и движением курсора.

Критерии прохода:

- не более одного активного `l4capture.exe`;
- первый свежий декодированный кадр не позднее 1.5 с посл�� штатного start;
- `framesDecoded` увеличивается;
- отображаются правильные цвета и raster `854×480`;
- виден ровно один курсор;
- средний FPS не ниже 9;
- p95 encode не выше 100 мс;
- отсутствуют crash и hang;
- Private Bytes после прогрева не растёт монотонно;
- рост памяти более 5 МиБ означает `FAIL`.

### 6.2. R1 — Lease и session safety

Проверить отдельно:

1. прекращение renew;
2. штатный `stream_stop`;
3. завершение родительского `l4desk`;
4. закрытие IPC pipe;
5. lock workstation;
6. UAC / secure desktop;
7. session disconnect или session switch;
8. stop или expiry во время recovery backoff;
9. sleep до deadline с последующим resume;
10. старый renew, duplicate renew и renew с уменьшающимся deadline;
11. ввод при безопасных условиях:
    - `low`, фактический `854x480`, валидная lease, desktop-source, правильная epoch и geometry — разрешён;
    - `default`, даже при фактическом `854x480` — запрещён.

Критерии прохода:

- новые RTP прекращаются не позднее 500 мс после stop, expiry, lock, UAC или session loss;
- выполняется `input_release_all`;
- после unlock/resume stream не запускается автоматически;
- stale и duplicate renew не продлевают lease;
- orphan `l4capture.exe` отсутствует;
- recovery не запускается после terminal stop или expiry;
- `default → 480p` не даёт права удалённого ввода.

### 6.3. R2 — Внешняя доставка и свежесть кадра

На согласованном тестовом маршруте проверить:

1. late join браузера;
2. отключение медиатранспорта на 10 секунд при действующей lease с последующим восстановлением;
3. штатное удаление и восстановление тестового route;
4. loss RTP 1%;
5. loss RTP 5%.

Критерии прохода:

- после восстановления route и транспорта свежий кадр появляется не позднее 3 с;
- `framesDecoded` снова увеличивается;
- экранный секундомер подтверждает свежесть кадра;
- не воспроизводится старый backlog;
- восстановление не зависит от browser PLI;
- невосстановившийся tunnel фиксируется как `FAIL_R2`, а не маскируется бесконечным restart capture-процесса.

### 6.4. R3 — Внутренняя устойчивость

Проверить:

1. задержку encode 200 мс на кадр;
2. зависший media worker;
3. blocked status reader;
4. ошибку allocation;
5. ошибку `sendto`;
6. 100 циклов `start → stop`.

Критерии прохода:

- watchdog и safety stop не блокируются media worker или status reader;
- очереди остаются ограниченными;
- после stop не выполняется drain кадров;
- recovery не превышает 5 попыток за 600 секунд;
- backoff соответствует `1 / 2 / 4 / 8 / 16 секунд + jitter`;
- новый child не появляется до подтверждённого завершения старого;
- после прогрева GDI/handle count возвращается к baseline.

### 6.5. R4 — Платформа и геометрия

Проверить:

1. ранний smoke на Windows 7 SP1 x86;
2. ранний smoke на Windows 7 SP1 x64;
3. работу без optional API;
4. DPI 100%, 150% и 200%;
5. дисплей слева с отрицательным origin;
6. смену разрешения;
7. отключение выбранного дисплея;
8. неизвестный или превышающий лимит источник.

Критерии прохода:

- базовый путь не требует VC Redistributable;
- отсутствуют двойной курсор и неверные цветовые плоскости;
- input не использует старый RECT после изменения геометрии;
- input блокируется до нового подтверждения геометрии;
- отсутствие выбранного дисплея возвращает контролируемый `source_unavailable`;
- oversized source отклоняется до allocation.

---

## 7. Kiosk Focus и lifecycle

На отдельном тестовом киоск-процессе проверить:

1. `kiosk_mode`: процесс существует и окно находится в foreground.
2. Потерю фокуса с успешной командой `refocus_kiosk`.
3. Потерю фокуса, при которой refocus невозможен:
    - keyboard input не отправляется;
    - фиксируется `kiosk_focus_lost`.
4. `adaptive_generic_fallback`:
    - configured kiosk process отсутствует;
    - input не блокируется;
    - synthetic Alt не вызывается.
5. `generic`:
    - kiosk process не указан;
    - input направляется напрямую.
6. Команды `kiosk_start`, `kiosk_stop`, `kiosk_restart`, `kiosk_status`.
7. Graceful `WM_CLOSE` и fallback `TerminateProcess` для hung window.

---

## 8. Артефакты evidence

Создать:

- `docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md`;
- `docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-candidate.md`;
- компактные обезличенные логи;
- CSV или JSON с временными метками тестов;
- при необходимости PCAP только тестового RTP-маршрута без полезной нагрузки рабочего экрана;
- скриншоты метрик без персональных данных.

Отчёт обязан содержать:

1. входной handoff и сверенные SHA-256;
2. паспорт стенда;
3. способ воспроизведения каждого fault;
4. результаты всех строк R1–R4;
5. first frame, FPS, encode p95, CPU, RAM и GDI handles;
6. evidence отсутствия orphan-process и bounded recovery;
7. Input Gate, Kiosk Focus и lifecycle;
8. результаты Win7 x86/x64 smoke;
9. перечень legacy tools, которые действительно останавливались или запускались;
10. итог: `READY_FOR_OWNER_NORM` либо конкретный `BLOCKED_*`.

---

## 9. Условие принятия

`H-L4C-06-v1` может стать candidate только при одновременном выполнении условий:

1. все обязательные проверки R1–R4 имеют статус `PASS`;
2. соблюдены все измеримые пределы M-1;
3. входной `H-L4C-05-v1` валиден;
4. runtime-код и серверные компоненты не изменялись;
5. report и candidate содержат реальные SHA-256;
6. владелец тестового стенда отдельно подтверждает результат словом `НОРМ`.

Без явного сообщения владельца `НОРМ` финальный статус:

    BLOCKED_OWNER_APPROVAL

До принятия `H-L4C-06-v1` запуск `L4C-07-DXGI-CAPTURE` запрещён.