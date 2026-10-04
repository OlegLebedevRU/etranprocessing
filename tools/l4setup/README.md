# Leo4 Zero-Touch Setup (`l4setup`)

**Опубликованный выпуск 1.10.2**: исправлены read-only `--smoke-only`, DNS false degradation,
независимые TLS budgets, launch/missing-certificate failures и bounded transport
retries при cold policy startup. Диагностика использует SCM ImagePath. Новые
настройки сети при той же версии применяются через Repair; GUI загружает текущие
SCM поля и сохраняет параметры остальных каналов. Новые
бюджеты и проверенные сочетания: [матрица надёжности](../../docs/term_net-leo4proxy-resolving-reliability-matrix.md).
[Запись публикации 1.10.2](../../artifacts/l4tools/1.10.2.json): подписи/timestamps, payload и полные HTTPS downloads проверены.
Сведения ниже об эксплуатации 1.10.1 сохраняются как история; Upgrade773 до 1.10.2 ещё не проверен.

1.10.1 исправляет потерю последних строк TLS diagnostics при завершении дочернего
leo4proxy: pipe дочитывается после сигнала process exit. Настоящие ошибки TLS
сохраняют degraded/12; причина upstream_tls_failed учитывает все четыре канала.
Изменён код только setup. При фактической подписи оператор повторно подписал
компоненты: PE code/data/resources совпадают с 1.10.0, полные hashes изменились.
Релиз опубликован; Upgrade773 подтверждён ready/0 и valid MQTT/HTTPS/RTP TLS.
Оператор подтвердил работающий видеопоток.

The Details panel lists the installed package version from
`state.json` and the actual PE file versions of the installed tools. After an
installation, the panel rereads `state.json` and refreshes this list.

To sign a prepared release from a regular Windows PowerShell session, run
`tools/release/Complete-SignedRelease.ps1 -PfxPath <PFX path> -Version 1.10.2 -SignOnly leo4proxy`.
This incremental release signs changed leo4proxy EXEs, rebuilds both embedded payloads, signs
`l4setup.exe`, and regenerates the manifest and checksums. The private key
stays outside the repository. If the PFX is encrypted, set
`L4TOOLS_SIGN_PFX_PASSWORD` in the process environment before running.

Единый 32-битный bootstrapper-инсталлятор (`l4setup.exe`) терминального стека утилит **Leo4 Tools** для ОС Windows (начиная с Windows 7 SP1 x86/x64 до Windows 11 / Server 2022).

---

## 1. Назначение и возможности

`l4setup.exe` решает задачу развертывания и обновления терминального окружения с минимальным вмешательством инженера («Zero-Touch»):
- **Единый бинарный файл**: 32-битный PE (`/SUBSYSTEM:WINDOWS,6.01`, сборка MSVC `/MT`), запускаемый как на 32-битных, так и на 64-битных версиях Windows без внешних зависимостей.
- **Интеллектуальное управление UAC**: Запуск без манифеста `requireAdministrator` позволяет выполнять диагностические команды (`--version`, `--help`) без запроса прав администратора. При необходимости elevation выполняется программно через `ShellExecuteExW(runas)`. При отказе пользователя возвращается код `20`.
- **Preflight & Drainage**: Проверка версии ОС (отказ `21` при < 6.1), включение TLS 1.2 в Schannel для Windows 7 SP1, проверка наличия UCRT, настройка правил Windows Firewall (`L4Tools-*`), мягкая остановка служб с таймаутом и завершением зависших процессов, освобождение портов `1883`, `18443`, `18883` (с защитой чужих процессов), проверка активных стримов ffmpeg.
- **Атомарная замена и Rollback**: Распаковка во временный каталог `.staging-<pid>\` и атомарная замена подкаталогов. Предыдущая версия сохраняется в `rollback\<prev_version>\` (хранится ровно одна предыдущая копия). Пользовательские файлы (`l4superv.json`, `state.json`, `mosquitto.conf`, `pending_pin.json`, логи) сохраняются.
- **Повторное использование сертификата (Cert Discovery & Guard)**: Проверка валидности сертификата терминала в `LocalMachine\MY`. Если сертификат валиден — повторное использование без запроса PIN. Перевыпуск только по явному флагу `--force-reissue`. При отсутствии связи с CA сохранение зашифрованного PIN в `pending_pin.json` (DPAPI Local Machine, ACL SYSTEM + Administrators).
- **Smoke-тестирование**: Автоматическая проверка доступности `_leo4/info`, порта Mosquitto `1883`, процесса `l4desk.exe` в активной сессии пользователя, тестовый захват кадра `ffmpeg`, проверка блокировки рабочего стола. Формирование отчета `install_summary.json`.

---

## 2. Командная строка (CLI)

```text
l4setup.exe [options]
```

### Параметры:
| Флаг | Описание |
|---|---|
| `--pin <PIN>`, `-p <PIN>` | 6-значный PIN-код терминала для выпуска сертификата. При наличии валидного сертификата игнорируется (если не передан `--force-reissue`). |
| `--force-reissue` | Разрешить перевыпуск сертификата, даже если текущий сертификат валиден (требует `--pin`). |
| `--no-pin` | Не запрашивать PIN. Если сертификат отсутствует, перевести терминал в режим ожидания PIN (`activation_required`, код `10`). |
| `--silent`, `/S` | Тихий режим без диалоговых окон. Без переданного `--pin` автоматически эквивалентен `--no-pin`. |
| `--dest <DIR>`, `-d <DIR>` | Каталог установки (по умолчанию `C:\l4tools`, учитывается `state.json.installer_base_path`). |
| `--repair` | Принудительная переустановка файлов и служб даже при совпадении версии. |
| `--smoke-only` | В 1.10.2 — read-only discovery/SCM/local/TLS probes, без CA/enrollment/config/services/state изменений. В опубликованном 1.10.1 флаг ещё не ограничивает engine. |
| `--version` | Напечатать версию инсталлятора (`1.10.1`) и выйти с кодом `0` без запроса прав администратора. |
| `--help`, `-h`, `/?` | Показать справку по параметрам и кодам возврата. |
| `--resolve-auto` / `--no-srv` | Убрать manual remote и включить SRV / отключить только SRV. |
| `--policy-bootstrap-ip <IP>` | IPv4 recovery GET policy с прежним логическим TLS-именем. |
| `--mqtt-remote`, `--http-remote`, `--stream-remote`, `--rtp-remote` `<host:port>` | Ручной upstream, приоритетнее Auto. |
| `--preview-ui` | Предпросмотр GUI без изменения установки и сертификатов. |

| `--payload-dir <DIR>` | *(Служебный флаг разработчика)* Указать каталог с исходными архивами `tools.zip` и `ffmpeg.zip`. |

---

## 3. Коды возврата

| Код | Статус | Описание |
|---|---|---|
| `0` | `ready` | Установка и smoke-проверки успешно завершены; терминал активен. |
| `10` | `activation_required` | Терминал переведен в Standby, службы запущены, ожидается ввод PIN. |
| `11` | `ready_for_online` | Сервер CA недоступен; PIN зашифрован DPAPI в `pending_pin.json`, службы запущены в ожидании сети. |
| `12` | `degraded` | Не прошла DNS-проба, ожидание l4desk или TLS-проба включённого upstream. `error_reason` уточняет причину. |
| `20` | `failed` | Отказ в правах администратора / запрос UAC отклонен пользователем. |
| `21` | `failed` | Неподдерживаемая версия ОС (< Windows 7 SP1 / NT 6.1). |
| `22` | `failed` | Ошибка дренажа (активный стрим отклонен, посторонний процесс занимает порты `1883`/`18443`/`18883`). |
| `23` | `failed` | Ошибка распаковки архива или файловой атомарной замены. |
| `24` | `failed` | Ошибка регистрации или настройки служб Windows (SCM). |
| `25` | `failed` | Ошибка выпуска сертификата (PIN отклонен CA-сервером). |
| `26` | `failed` | Certificate discovery не подтвердил новый сертификат после enrollment. |
| `27` | `failed` | Провал критической smoke-проверки (`proxy_info` или `mosquitto_port`). |
| `28` | `failed` | Уже запущен другой l4setup. |
| `29` | `failed` | Downgrade заблокирован: установленная версия новее пакета. |
| `30` | `failed` | Недостаточно места: требуется минимум 100 MB headroom. |
| `31` | `cancelled` | Отмена пользователем; проверьте phase и состояния служб в summary. |

---

## 4. Машина состояний Фазы 3 (Сертификаты и PIN)

| Состояние сертификата | PIN передан | `--force-reissue` | Доступность CA | Действие инсталлятора | Код возврата |
|---|---|---|---|---|---|
| `CERT_VALID` | Нет | Нет | * | Повторное использование сертификата (`cert.reused = true`). К CA не обращаться. | `0` (или `12`) |
| `CERT_VALID` | Да | Нет | * | Повторное использование сертификата. Предупреждение `cert_reused_pin_ignored`. К CA не обращаться. | `0` (или `12`) |
| `CERT_VALID` | Да | Да | Доступен, принят | Выпуск через `l4pin.exe --force <PIN>`. Сигнал `SERVICE_CONTROL 128` службе `L4Superv`. | `0` |
| `CERT_VALID` | Да | Да | PIN отклонен CA | Прерывание. Ошибка неверного PIN. | `25` |
| `CERT_VALID` | Да | Да | Недоступен (3 retry) | Сохранение в `pending_pin.json` (DPAPI local machine, ACL SYSTEM+Admin). | `11` |
| `CERT_VALID` | Нет | Да | * | Запрос PIN (GUI) или переход в Standby при `--silent`. | `10` |
| `CERT_EXPIRING` | * | Нет | * | Повторное использование сертификата. Предупреждение `cert_expiring`. | `0` (или `12`) |
| `CERT_EXPIRING` | Да | Да | Доступен, принят | Выпуск через `l4pin.exe --force <PIN>`. | `0` |
| `CERT_EXPIRING` | Да | Да | PIN отклонен CA | Ошибка неверного PIN. | `25` |
| `CERT_EXPIRING` | Да | Да | Недоступен (3 retry) | Сохранение в `pending_pin.json`. | `11` |
| `CERT_ABSENT` / `BROKEN` | Нет | * | * | GUI: диалог ввода PIN / «Пропустить». В silent: переход в Standby. | `10` |
| `CERT_ABSENT` / `BROKEN` | Да | * | Доступен, принят | Выпуск через `l4pin.exe <PIN>`. Сигнал `SERVICE_CONTROL 128` службе `L4Superv`. | `0` |
| `CERT_ABSENT` / `BROKEN` | Да | * | PIN отклонен CA | Ошибка неверного PIN. | `25` |
| `CERT_ABSENT` / `BROKEN` | Да | * | Недоступен (3 retry) | Сохранение в `pending_pin.json`. | `11` |

---

## 5. Формат `install_summary.json`

Файл формируется в корне целевого каталога (`%dest%\install_summary.json`) в кодировке UTF-8 без BOM с атомарной заменой через `.tmp`:

```json
{
  "schema": 2,
  "timestamp": "2026-09-12T18:00:00Z",
  "installer_version": "1.10.1",
  "installed_version": "1.10.1",
  "target_version": "1.10.1",
  "os": "Windows 10 Pro (10.0.19045) x64",
  "target_arch": "x64",
  "dest": "C:\\l4tools",
  "status": "ready",
  "exit_code": 0,
  "cert": {
    "state": "valid",
    "reused": true,
    "reissued": false,
    "thumbprint": "CC88419A4C3763150A4C0905261EC073C58CFC09",
    "sn": "a4b0000773c82116d210826",
    "not_after": "2027-08-29T17:09:40Z"
  },
  "drainage": {
    "services_stopped": ["Leo4Proxy", "mosquitto", "L4Con", "L4Superv"],
    "processes_killed": [],
    "ports_freed": []
  },
  "probes": {
    "upstream_tls": {"mqtt":"valid", "https":"valid", "l4stream":"skipped", "l4rtp":"valid"},
    "proxy_info": "ok",
    "mosquitto_port": "ok",
    "user_session_id": 1,
    "l4desk_running": true,
    "ffmpeg_smoke_capture": "ok",
    "desktop_locked": false
  },
  "warnings": []
}
```

---

Schema 2 distinguishes the committed installed package version from the target version.
An early failure records the real exit code and current SCM service states (or `unknown`
when not readable). Unexecuted string probes are `not_run`; unobserved session/desktop
values are `null`. Consumers of schema 1 must allow these values and must not infer
success from file extraction or from a target version.

The payload installs `l4superv/package-components.json`: package version, selected
architecture, and each EXE's actual PE product version, size and SHA-256. Third-party
EXEs without a PE version have `null`; this does not mean the file is missing.
The release manifest includes both inventories. They are regenerated after signing.
A certificate is verified independently of services; a missing Mosquitto configuration
is prepared through `l4superv --prepare-mosquitto --dest <absolute path>` before service
startup. Only then does setup wait for proxy ready/standby and run smoke probes.

## 6. Безопасность и хранение секретов

1. **Маскирование PIN**: В лог-файл `%dest%\l4setup.log` и консоль выводятся сообщения с обязательным маскированием паттернов `pin=` / `--pin` на `******`. Аргументы командной строки с открытым PIN в лог не записываются.
2. **Очистка памяти**: Буферы, содержащие PIN, немедленно очищаются с помощью `SecureZeroMemory` после вызова дочернего процесса.
3. **`pending_pin.json`**: Шифруется с использованием Windows Data Protection API (`CryptProtectData`, флаг `CRYPTPROTECT_LOCAL_MACHINE`). Права на файл (DACL) строго ограничиваются субъектами `NT AUTHORITY\SYSTEM` и `BUILTIN\Administrators` (SDDL: `D:P(A;;GA;;;SY)(A;;GA;;;BA)`). Время жизни (TTL) составляет 72 часа.

---

## 7. Сборка и тестирование

### Сборка
Требуется установленный Microsoft Visual C++ (Visual Studio 2022 Community или Build Tools).
```cmd
cd tools\l4setup
build.cmd
```
Результат: `bin\x86\l4setup.exe`, `bin\x64\l4setup.exe` и universal
`bin\l4setup.exe` (копия x86, статический `/MT`). Release version headers и
embedded payloads готовятся штатным release flow; отдельный build.cmd
не заменяет staging, подпись и payload integrity gate.

### Запуск unit-тестов
```cmd
cd tools\l4setup
run_tests.cmd
run_tests.cmd x64
```
Тестовый набор проверяет:
- Все комбинации CLI-аргументов
- Маскирование PIN в логах
- Фильтрацию путей архивов по архитектуре x86/x64
- Все ветвления машины состояний Фазы 3
- Сериализацию `install_summary.json` и точечное обновление `state.json`

---

## 8. Известные ограничения Этапа 1

1. **Authenticode**: Подпись исполняемого файла сертификатом издателя не входит в Этап 1 и запланирована на последующие этапы.
2. **`--from-registry`**: Прямое скачивание дистрибутива инсталлятором из Generic-реестра запланировано на Этап 2.
3. **Обновление в рантайме (Update Agent)**: Самообновление через фоновый сервис `l4superv` относится к Этапу 2.

## Интерфейс1.9.4 и безопасный предпросмотр

Слева показаны операция, версия, папка установки, службы, ход и результат.
Справа всегда открыт журнал: ERROR выделен красным, WARN — охрой, дополнительно
сохранены текстовые маркеры. Основная кнопка справа называется по обнаруженной
операции: «Установить», «Обновить», «Восстановить» или «Проверить».
После ошибки эта же кнопка предлагает повтор с новой проверкой условий.
Копирование журнала находится в его панели, отдельно от кнопок установки.

Закрытие во время операции запрашивает отмену на границе этапа и ожидает worker;
повторное закрытие не завершает процесс принудительно. Код31 означает отмену,
проверяйте итоговое состояние служб по журналу/summary прежде чем продолжить.

`l4setup.exe --preview-ui` показывает демонстрационные сообщения и кнопки без
вызова installer engine, записи сертификатов и управления службами.
Это режим визуальной проверки, не доказательство успешного install/upgrade.

Штатные HTTP-запросы native tools используют NO_PROXY; intended local leo4proxy
routes сохраняются. Legacy certsrv не активирует внешние MQTT/RTP:
до действующего iot.leo4.ru стек остаётся в ожидании сертификата.
Для каждого выпуска используйте [постоянный сценарий подписи](../release/README.md).

## Сеть и TLS diagnostics (1.10.0–1.10.1)

«Сеть…» задаёт bootstrap IP и manual host:port для отдельных каналов. Пустое поле
канала означает Auto; проверка server certificate обязательна. При Upgrade/Repair
без изменения настроек сохраняются текущие SCM options. При изменении сети
сохраняются остальные CLI options. Для silent режима: `--policy-bootstrap-ip`,
`--mqtt-remote`, `--http-remote`, `--stream-remote`, `--rtp-remote`, `--no-srv`,
`--resolve-auto`. Bootstrap profile включается в подписываемый setup через
build environment `L4TOOLS_POLICY_BOOTSTRAP_IP`; private pins/ключи в него не входят.

После установки verify worker запускает ограниченную по времени TLS-диагностику
leo4proxy, журнал показывает verdict каждого канала; результаты находятся в
`install_summary.json` → `probes.upstream_tls`. Это TLS проверка с существующим
terminal cert, а не видео E2E. `service-args.txt` зеркалирует SCM options для
watchdog; существующий SCM ImagePath остаётся authoritative.

Общий budget TLS diagnostics — 8 секунд, worker ждёт процесс до 8.5 секунд.
Проба `network` проверяет DNS `iot.leo4.ru`, а не TCP/Internet: при отказе DNS
возможен degraded/network_unreachable даже с успешным IP recovery.
`--smoke-only` не ограничивает engine в 1.10.1. Verify выбирается автоматически
только при совпадении версии и целостности; Prepare/certificate/service checks
при этом сохраняются. Для диагностики без installer engine используйте
`leo4proxy --check-upstream` и local /_leo4/info.
Полный контракт verdicts и состав:
[руководство инженера](../../docs/term_tool-user-guide.md).
