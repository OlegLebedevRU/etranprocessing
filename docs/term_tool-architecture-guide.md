# Архитектура L4 Tools Suite, Принципы Оркестрации и Руководство по Расширению

Опубликованный пакет — tools **1.10.1** (2026-10-04); кандидат **1.10.2**
устраняет проблемы resolving/diagnostics. Его границы ожиданий и проверенные
сочетания зафиксированы в [матрице надёжности](term_net-leo4proxy-resolving-reliability-matrix.md).
Установка — `l4setup.exe`,
индивидуальные версии, routing/TLS и runtime evidence — в
[руководстве инженера](term_tool-user-guide.md).


Настоящий документ является архитектурным стандартом и практическим руководством по оркестрации, интеграции с облачным REST-RPC API и добавлению новых нативных утилит в комплекс системных служб и инструментов **L4 Tools Suite** (`C:\l4tools`).

Исторические решения комплекта 1.9.6 — порядок SCM-переходов, сертификатное
ожидание, локальный ready, ACL журналов и выпуск с timestamp — закреплены
в [документе по стабилизации](term_arch-l4tools-stabilization-decisions.md).
Сборка x86/x64 и локальные smoke-пробы не заменяют проверки перечисленных там
сценариев на реальных терминалах.

---

## 1. Архитектурные Принципы Комплекса L4 Tools Suite

Комплекс утилит L4 спроектирован для работы на стороне платёжных терминалов и киосков самообслуживания под управлением ОС Windows (Windows 7–11, POSReady 7/Embedded, x86 и x64).

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                 Платёжный Терминал (Windows)                           │
│                                                                                        │
│   ┌───────────────────────────┐         ┌──────────────────────────────────────────┐   │
│   │   Основное ПО Терминала   │         │           L4 Tools Suite (C:\l4tools)    │   │
│   │   (PlaterraTerminal, UI)  │         │                                          │   │
│   └─────────────┬─────────────┘         │  ┌──────────────┐     ┌──────────────┐   │   │
│                 │                       │  │   l4con      │     │    l4sql     │   │   │
│                 │ (1883)                │  │ (веб-консоль)│     │(MS SQL клиент│   │   │
│                 ▼                       │  └──────┬───────┘     └──────────────┘   │   │
│       ┌───────────────────┐             │         │ (1883)                         │   │
│       │     Mosquitto     │◄────────────┴─────────┘                                │   │
│       │ (Локальный брокер)│                                                        │   │
│       └─────────┬─────────┘                                                        │   │
│                 │ Local Bridge (:18883)                                            │   │
│                 ▼                                                                  │   │
│       ┌───────────────────┐         ┌──────────────────┐     ┌──────────────────┐  │   │
│       │     Leo4Proxy     │◄────────┤     l4superv     │     │      l4pin       │  │   │
│       │  (mTLS Туннель)   │ (18443) │(Оркестратор/SCM) │     │ (Выпуск серт-та) │  │   │
│       └─────────┬─────────┘         └──────────────────┘     └──────────────────┘  │   │
│                 │                                                                  │   │
└─────────────────┼──────────────────────────────────────────────────────────────────┘   │
                  │ Защищённый внешний mTLS туннель (:8883)                              │
                  ▼                                                                      │
      Облачная Платформа etranprocessing (dev.leo4.ru)                                   │
```

### Ключевые компоненты:
1. **`leo4proxy`** (`C:\l4tools\leo4proxy\leo4proxy.exe`):
   * Источник истины по серийному номеру (`SN`) и сертификату устройства.
   * Аппаратная mTLS-аутентификация через Windows CNG KSP.
   * Проксирует MQTT на внешний порт `8883` и входящий HTTPS с порта `443` на локальный бэкенд `:8000`.
   * Предоставляет локальный HTTP API метаданных на порту `18443` (`GET /_leo4/info`, `GET /_leo4/sn`).
2. **`mosquitto`** (`C:\l4tools\mosquitto\mosquitto.exe`):
   * Локальный брокер на `127.0.0.1:1883`.
   * Мостирует сообщения во внешнее облако через туннель `leo4proxy` (`127.0.0.1:18883`).
3. **`l4superv`** (`C:\l4tools\l4superv\l4superv.exe`):
   * Автономный супервизор и сторожевой таймер (Watchdog) жизненного цикла служб.
   * Динамически переключает `mosquitto.conf` между локальным Standby-режимом (когда сертификата нет) и боевым mTLS Bridge (когда сертификат загружен).
   * Защищает от клонирования дисков (сверка Hardware Fingerprint) и контролирует пути запущенных процессов (`path_match`).
4. **`l4setup`** (отдельный скачанный `l4setup.exe`):
   * Установщик с UAC, встроенными payload x86/x64, certificate phase и verify worker.
   * Распаковывает payload выбранной архитектуры, регистрирует службы и настраивает `PATH`.
   * `l4install.exe` — сохранённый механизм старого ZIP-дистрибутива, не основной маршрут 1.10.1.
5. **`l4con`** (`C:\l4tools\l4con\l4con.exe`):
   * Агент удалённой диагностики и интерактивной веб-консоли (RPC методы `7001` Exec, `7002` Cancel, `7003` Ping).
   * Автоматически настраивает рабочий каталог (`C:\l4tools`) и `PATH` для порождаемых процессов, выводя приглашение командной строки.
6. **`l4sql`** (`C:\l4tools\l4sql\l4sql.exe`):
   * Нативный высокоскоростной клиент доступа к локальной базе данных MS SQL (`Terminal`) через Windows Authentication (Trusted Connection).
   * Автообнаружение активной папки ПО и `DBConfig.xml` по логам `PlaterraTerminal.log`.
   * Аппаратный Read-Only Guard (блокировка любых DML/DDL команд, разрешение выполнения процедур только с префиксом `l4_`).
7. **`l4pin`** (`C:\l4tools\l4pin\l4pin.exe`):
   * Автономный генератор ключей в Windows CNG KSP и установщик сертификата по одноразовому PIN-коду.

---

## 2. Изолированная Модель Размещения и Автоматический PATH

### 2.1. Структура каталогов (`C:\l4tools`)
Все утилиты размещаются строго в собственных поддиректориях первого уровня:
```text
C:\l4tools\
├── install_summary.json             # Итог l4setup; сам setup скачивается отдельно
├── l4superv.json                    # Конфигурация супервизора
├── state.json                       # Снимок состояния и аппаратный отпечаток
├── term_tool-user-guide.md          # Руководство пользователя для инженеров
├── l4sql\
│   └── l4sql.exe                    # Нативный клиент MS SQL
├── l4con\
│   └── l4con.exe                    # Агент удалённой веб-консоли
├── l4pin\
│   └── l4pin.exe                    # Утилита выпуска сертификата по PIN
├── l4superv\
│   └── l4superv.exe                 # Системный супервизор
├── leo4proxy\
│   └── leo4proxy.exe                # mTLS-туннель
├── l4desk\
│   └── l4desk.exe                   # Агент пользовательской сессии
├── l4capture\bin\l4capture.exe      # Native capture/encode/RTP
├── ffmpeg\ffmpeg.exe                # FFmpeg выбранной архитектуры
└── mosquitto\
    ├── mosquitto.exe                # Локальный брокер MQTT
    ├── mosquitto.conf               # Динамический конфиг
    └── log\mosquitto.log            # Лог брокера
```

### 2.2. Двухуровневый механизм прозрачного вызова утилит (Zero-Touch PATH)
Чтобы команды типа `l4sql tb_Variables` или `l4pin <PIN>` выполнялись без ввода полных путей:

1. **Уровень 1: Внутри `l4con` (`command_runner.c`)**:
   * При старте `l4con` вычисляет базовый путь `C:\l4tools`.
   * Устанавливает рабочий каталог процесса в `C:\l4tools` (`SetCurrentDirectoryW`).
   * В функции `command_runner_setup_environment()` обогащает переменную среды `PATH` путями:
     `C:\l4tools;C:\l4tools\l4sql;C:\l4tools\l4con;C:\l4tools\l4pin;C:\l4tools\l4superv;C:\l4tools\leo4proxy`.
   * Все дочерние процессы `cmd.exe` / `powershell.exe` мгновенно находят любую утилиту комплекса по короткому имени.
2. **Уровень 2: В инсталляторе `l4setup` (`src/services.c`)**:
   * При развёртывании прописывает пути к утилитам в системную ветку реестра `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment` (`Path`) и рассылает системное уведомление `WM_SETTINGCHANGE` для локальных сессий инженеров.

---

## 3. Интеграция с Облачным REST-RPC API (`POST /api/v1/device-tasks/`)

Любая задача удалённой диагностики (включая SQL-запросы через `l4sql`, сбор логов, диагностику оборудования) может инициироваться как из веб-интерфейса `MenuBuilder`, так и через публичный REST API платформы.

### 3.1. Создание задачи через REST-RPC
* **Эндпоинт**: `POST https://dev.leo4.ru/api/v1/device-tasks/?org_id={org_id}`
* **Заголовки**:
  ```http
  Content-Type: application/json
  X-Org-Id: 1
  X-Role: admin
  X-Role-Id: 1
  ```
* **Тело запроса (Method Code 7001 — `CMD_DIAG_EXEC`)**:
  ```json
  {
    "device_id": 773,
    "ext_task_id": "task-l4sql-check-vars-01",
    "method_code": 7001,
    "priority": 0,
    "ttl": 5,
    "payload": {
      "command_line": "l4sql --limit 5 tb_Variables"
    }
  }
  ```

### 3.2. Сквозной поток исполнения (E2E Message Flow)
```
1. Client ─── POST /api/v1/device-tasks/ ───► Cloud REST API (FastAPI)
                                                     │
2. Cloud Backend ─── PUBLISH srv/{SN}/tsk ───────────┤ (MQTT over mTLS)
                                                     ▼
3. Agent (l4con) ─── PUBLISH dev/{SN}/req ──────────► Cloud Backend
                                                     │
4. Cloud Backend ─── PUBLISH srv/{SN}/rsp ───────────┘
   (Payload: {method_code: 7001, command_line: "l4sql ..."})
        │
        ▼
5. Agent (l4con) выполняет: cmd.exe /c l4sql ...
        │
        ├─► Стриминг вывода: PUBLISH dev/{SN}/out (chunked JSON stdout)
        │
        └─► Финальный отчет: PUBLISH dev/{SN}/res (status: completed, exit_code: 0)
                                                     │
6. Cloud Backend сохраняет результат ◄───────────────┘
                                                     │
7. Client ◄─── GET /api/v1/device-tasks/{id} ────────┘
   (Response: status=3 (completed), results=[{exit_code: 0, duration_ms: 1546}])
```

---

## 4. Пошаговое Руководство: Добавление Новой Утилиты в L4 Suite

При добавлении нового нативного инструмента (например, `tools/l4net`, `tools/l4gpio`, `tools/l4log`):

### Шаг 1. Создание подпроекта в `tools/<subproject>`
1. Создать каталог `tools/<tool_name>/src` и `tools/<tool_name>/res`.
2. Написать исходный код на C11 / Win32 API.
3. Добавить файл ресурсов `res/<tool_name>.rc` с версионной информацией `VERSIONINFO`.
4. Сформировать `CMakeLists.txt` (стандарт C11, MSVC `/MT`, `/utf-8`, линковка системных библиотек).
5. Создать файл документации `README.md` с описанием параметров и примеров вызова.

### Шаг 2. Унифицированный скрипт сборки `build.cmd`
Создать `tools/<tool_name>/build.cmd` с поддержкой единой кросс-архитектурной сборки:
* `build.cmd x86` -> `bin\x86\<tool_name>.exe` и копия в `bin\<tool_name>.exe` (универсальный 32-битный бинарник).
* `build.cmd x64` -> `bin\x64\<tool_name>.exe`.
* `build.cmd` (или `all`) -> одновременная сборка обеих архитектур.

### Шаг 3. Включение в актуальные payloads и legacy упаковку

Для `l4setup` добавьте инструмент в `tools/build_dist.cmd` (build gate),
`tools/release/Build-StagingPayloads.ps1` (отдельный staging x86/x64),
`New-PayloadInventory.ps1` и `New-ReleaseManifest.ps1` (состав/версии/хэши).
В каждой staging-архитектуре должен находиться её EXE, а не случайный default.
Проверьте оба embedded payload через `Test-PayloadIntegrity.ps1`.
Подпись с timestamp и immutable publication выполняются по
[release runbook](../tools/release/README.md); после подписи компонентные сборки не повторяются.

Для совместимости прежнего ZIP-маршрута также обновите `pack_zip.cmd`:
В файле `tools/l4superv/pack_zip.cmd`:
1. Добавить создание подкаталога в staging-зоне:
   ```cmd
   md "%STAGING%\<tool_name>"
   ```
2. Добавить копирование бинарных файлов и документации:
   ```cmd
   if exist "%REPO_TOOLS%\<tool_name>\bin" (
       xcopy /e /y /q "%REPO_TOOLS%\<tool_name>\bin\*" "%STAGING%\<tool_name>\" >nul
   )
   ```

### Шаг 4. Установка и совместимость старого l4install

Для актуального setup проверьте `tools/l4setup/src/services.c` (PATH/SCM),
`unpack.c` и `tools/release/New-PayloadInventory.ps1` (состав/проверки).
Сохраняйте прежний packaging contract `tools/l4superv/src/installer_main.c`
для legacy ZIP-маршрута; следующие пункты относятся к нему:
1. В функции `install_files()` добавить создание директории:
   ```c
   swprintf_s(sub_dir, MAX_PATH, L"%ls\\<tool_name>", dest_dir);
   CreateDirectoryW(sub_dir, NULL);
   ```
2. В функции `add_to_system_path()` дополнить список регистрируемых каталогов:
   ```c
   swprintf_s(tools_path, sizeof(tools_path)/sizeof(wchar_t),
              L"%ls;%ls\\l4sql;%ls\\l4pin;%ls\\l4con;%ls\\l4superv;%ls\\<tool_name>",
              base_dir, base_dir, base_dir, base_dir, base_dir, base_dir);
   ```
3. В сводке верификации `[5/5]` добавить строку вывода статуса утилиты.

### Шаг 5. Регистрация в `l4con` (`command_runner.c`)
В функции `command_runner_setup_environment()`:
* Добавить подкаталог новой утилиты в формируемую строку `new_path`:
  ```c
  _snwprintf(new_path, sizeof(new_path)/sizeof(wchar_t),
             L"%ls;%ls\\l4con;%ls\\l4sql;%ls\\l4pin;%ls\\l4superv;%ls\\leo4proxy;%ls\\<tool_name>;%ls",
             g_active_working_dir, ...);
  ```

### Шаг 6. Обновление документации
1. Включить описание новой утилиты в `docs/term_tool-user-guide.md`.
2. Зафиксировать изменения в `CHANGELOG.md` супервизора и консоли.

---

## 5. Тестирование и Валидация

| Тест-кейс | Описание | Ожидаемый результат |
|---|---|---|
| **Direct CLI Execution** | Локальный вызов `l4sql tb_Variables` из `cmd.exe` | Вывод таблицы данных, код возврата `0` |
| **Local MQTT Echo** | Публикация команды в `srv/{SN}/rsp` локального Mosquitto | Чанки в `dev/{SN}/out`, финальный JSON в `dev/{SN}/res` |
| **REST-RPC E2E Test** | `POST /api/v1/device-tasks/` -> `GET /api/v1/device-tasks/{id}` | Статус `3 (completed)`, `exit_code: 0` |
| **Read-Only Enforcement** | Попытка выполнить `l4sql "UPDATE ..."` или `l4sql "DROP ..."` | Блокировка запроса валидатором, код возврата `2` |
| **SP Security Filter** | Вызов `l4sql "EXEC sp_ClearDB"` vs `l4sql "EXEC l4_..."` | Обычные процедуры блокируются (`exit_code 2`), процедуры с префиксом `l4_` выполняются |
| **System SCM Permissions** | Запуск под службой Windows (`NT AUTHORITY\SYSTEM`) | Успешное чтение данных из `Terminal` DB |
