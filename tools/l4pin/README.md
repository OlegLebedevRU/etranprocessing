# Leo4 Terminal Certificate Installer — l4pin (C / CNG / Win32)

Автономная нативная утилита `l4pin` для Windows с графическим окном при запуске без аргументов и CLI для установки сертификата по PIN-коду (флоу `v=26` / CNG KSP / RSA 2048). Закрытый ключ неэкспортируемый; перед удалением прежнего сертификата новый ключ проверяется.

## 1.8.0 / suite 1.11.0 (подписано и опубликовано)

Новый authenticated renewal: --renew-authenticated --pin-stdin. Режим разрешён
только дочернему процессу действующего Job l4con с ≥100 s остатка; прямой запуск
не даёт права продления. RPC7011 передаёт PIN через stdin, без shell. Ручная
удалённая консоль допустима с достаточным TTL; её command_line маскируется в
истории. Legacy GUI / обычный PIN flow сохраняются отдельно.

Renew PIN принимается только POST JSON /api/certificates/renew с живым
сертификатом, тем же SN/current serial и правами терминала. По умолчанию запрос
идёт через localhost Leo4Proxy; explicit HTTPS требует строгого mTLS. Нет
anonymous retry/redirect/insecure TLS. Общий non-waiting mutex исключает две
одновременные установки. Бюджет90 s покрывается Job120 s.

До SETUP CSR/key name сохраняются в DPAPI + защищённом HKLM, публичный PKCS7 —
до установки. Повтор использует тот же CSR. Серверное recovery ограничено15 min
и PIN expiry, другой CSR/выпуск отклоняется. После успешной установки pending
состояние удаляется. Старый сертификат удаляется после проверки нового ключа.
[Матрица, recovery limits и acceptance](../../docs/term_arch-rpc7011-flow-matrix.md).

---

## Возможности

- **Графическое окно:** номер терминала из стандартного CN/SN выделен крупно с сохранением семи цифр и ведущих нулей. При ином формате CN показывается OU; O выводится рядом. PIN имеет крупный открытый шрифт, Force — сенсорную область. Окно ограничено рабочей областью экрана, в том числе 800x600.

- **Cert Discovery и Guard «не трогать валидный сертификат» (Этап 1 / Контракт 4.1):**
  - Перед сетевым запросом к CA утилита проверяет наличие и валидность действующего сертификата в хранилище `LocalMachine\MY`.
  - При наличии годного сертификата (`days_left > 30`, приватный ключ CNG доступен, эмитент `iot.leo4.ru`, совпадает серийный номер):
    - Печатает сообщение: `Certificate <thumbprint> for <SN> is valid until <NotAfter>; reissue not required (use --force to override)`.
    - Возвращает код `0`.
    - **Не выполняет ни одного сетевого запроса и ни одного изменения хранилища**, сберегая одноразовый PIN-код.
  - При истекающем сертификате (`days_left <= 30`) без `--force`: выдаёт предупреждение и продолжает продление.
  - При неисправном (`CERT_BROKEN`) или отсутствующем (`CERT_ABSENT`): продолжает штатную процедуру выпуска.
  - Флаг `--force`: принудительный перевыпуск поверх существующего сертификата.
- **Режим инспекции хранилища (`--check`):**
  - Недеструктивная диагностика сертификата терминала без сетевых запросов.
  - Форматы вывода: текстовый и структурированный JSON (`--json`).
  - Коды возврата: `0` (valid), `1` (expiring), `2` (broken), `3` (absent), `>= 20` (ошибка доступа к хранилищу).
- **Флоу `v=26` (CNG / KSP):**
  - Шаг 1: `GET /api/certificates/?function=check&pin=<PIN>&tosign=<sysinfo>&v=26`
  - Шаг 2: Генерация неэкспортируемого ключа RSA 2048 в `Microsoft Software Key Storage Provider` (`NCRYPT_ALLOW_EXPORT_NONE = 0`).
  - Шаг 3: Формирование PKCS#10 CSR с подстановкой `CN=<sign>`.
  - Шаг 4: `POST /api/certificates/?function=setup&pin=<PIN>&cpserial=<sign>` с телом CSR.
  - Шаг 5: Декодирование PKCS#7 цепочки и установка сертификата в системное хранилище Windows.
- **Стратегия автоопределения URL (`url-finder`):**
  1. **Явный аргумент командной строки**: `--url <URL>`, `-url <URL>`, `-u <URL>`.
  2. **Автообнаружение через leo4proxy**: опрос локальных эндпоинтов `https://127.0.0.1/_leo4/info?format=json` и `http://127.0.0.1:18443/_leo4/info`, извлечение `listeners.http_local` и формирование рабочего URL `https://<http_local>/api/certificates` (с откатом на `http://`).
  3. **Fallback по умолчанию**: `https://iot-processing.ru/api/certificates`.
- **Единый универсальный бинарник (x86 для x86 и x64 Windows):**
  - 32-битный x86 бинарник со статической линковкой CRT (`/MT`) работает нативно на 32-битных ОС (POSReady 7, Win 7/10/11 x86) и полностью совместимо на 64-битных ОС через WOW64 без ограничений (общесистемный CNG KSP и хранилище сертификатов `LocalMachine\MY`).
- **Безопасность закрытого ключа:**
  - Закрытый ключ генерируется внутри защищенного хранилища Windows (CNG KSP) и помечается как неэкспортируемый.
  - Невозможно извлечь ключ через экспорт сертификата в файл (`.pfx`/`.key`).
- **Маскирование PIN и безопасность логов:**
  - PIN-код маскируется (`***`) во всех сообщениях и диагностических логах.
- **Очистка устаревших сертификатов:**
  - Для каждого выпуска создаёт отдельный неэкспортируемый CNG-ключ, сохраняя прежний ключ при ошибке CA или установки.
  - Сначала устанавливает новый сертификат и проверяет новую CA, срок, email и доступность ключа; затем удаляет прежние `iot.leo4.ru` и распознанные терминальные legacy `certsrv` из Machine MY и MY всех Windows-профилей. Недоступность профиля останавливает очистку до удаления. При ошибке удаления восстанавливаются прежние записи и возвращается отказ; новый проверенный сертификат сохраняется, поскольку backend serial и одноразовый PIN уже изменены. Ошибка очистки не означает, что выпуск не состоялся.
  - После успешной смены SN требуется перезапуск `L4Superv`, `L4Con`, `Leo4Proxy` и `mosquitto`.
- **Zero Dependencies:**
  - Нативный бинарник без зависимостей от сторонних DLL, .NET или Python runtime.
  - Статическая линковка CRT (`/MT`).

---

## Архитектура и интеграция

Подробная спецификация подсистемы сертификатов, правил валидации в БД и дорожной карты развития C-инструментов для терминалов описана в:
👉 **[`docs/certificate-architecture.md`](../../docs/certificate-architecture.md)**
Контракт Cert Discovery и Zero-Touch Installer описан в:
👉 **[`docs/term_tool-zero-touch-installer-and-remote-runtime-plan.md`](../../docs/term_tool-zero-touch-installer-and-remote-runtime-plan.md)**

---

## Сборка и тестирование

### Вариант 1: Через командную строку (MSVC Build Tools 2022)
Скрипт `build.cmd` автоматически компилирует статически слинкованные бинарники и запускает юнит-тесты:
```cmd
cd tools\l4pin
build.cmd all
```
* **Параметры сборки:**
  - `build.cmd` (или `build.cmd all`) — собирает обе архитектуры (x86 и x64) и запускает тесты модуля.
  - `build.cmd test` — сборка и запуск только юнит-тестов Cert Discovery на in-memory хранилищах.
  - `build.cmd x86` — собирает 32-битную универсальную версию.
  - `build.cmd x64` — собирает 64-битную версию.
* **Результаты сборки в каталоге `bin/`:**
  - `bin\l4pin.exe` — универсальный 32-битный бинарник по умолчанию (работает на x86 и x64).
  - `bin\x86\l4pin.exe` — 32-битный нативный бинарник.
  - `bin\x64\l4pin.exe` — 64-битный нативный бинарник.

### Вариант 2: Через CMake (MinGW или MSVC)
```powershell
# Сборка Release
cmake -B build -A Win32
cmake --build build --config Release
```

---

## Использование

Запуск `l4pin.exe` без аргументов открывает окно установки: PIN виден при вводе, а флажок `Force reissue` разрешает замену действующего сертификата. Старые сертификаты удаляются по обязательному правилу после проверки нового ключа; отключить очистку через окно нельзя.

### 1. Проверка состояния сертификата (Cert Discovery, без изменения хранилища и сети):
```cmd
:: Человекочитаемый вывод
l4pin.exe --check

:: JSON-вывод с проверкой привязки к конкретному SN
l4pin.exe --check --sn a4b0000773c82116d210826 --json
```

### 2. Установка / продление сертификата по PIN-коду (с защитой валидного):
```cmd
l4pin.exe 021358
```
или:
```cmd
l4pin.exe --pin 021358
```
*Если сертификат уже установлен и годен, утилита выведет уведомление и завершится с кодом `0` без расхода PIN.*

### 3. Принудительный перевыпуск сертификата (--force):
```cmd
l4pin.exe --force 021358
```

### 4. Установка с явным URL и хранилищем:
```cmd
l4pin.exe --pin 021358 --url https://iot-processing.ru/api/certificates --store machine
```

### 5. Просмотр установленных сертификатов в хранилище:
```cmd
l4pin.exe --status
```

## 1.7.3: direct HTTP and cross-profile replacement
Native HTTP uses direct connections (NO_PROXY). CHECK/SETUP have finite phase timeouts and bounded responses; truncated bodies and HTTP errors fail. Discovery uses only the known local HTTP listener and validates the JSON listeners object and loopback address.
After verifying the new iot.leo4.ru issuer, validity, email and accessible CNG key, replacement cleans old IoT and terminal certsrv records from Machine MY and MY of all registered Windows profiles. Inaccessible profiles fail before deletion; deletion failures restore captured certificate contexts. Unrelated certsrv credentials and old private key containers are preserved. GUI confirmation explicitly describes this scope. See the cascade report for tests and outstanding runtime checks.
