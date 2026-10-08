# Leo4 Zero-Touch Setup (`l4setup`)

**Текущий кандидат 1.13.5:** установка новой раскладки выполняется явным
`--fresh-install` из подписанного offline kit, старый install/migration закрыт.
Для ручного перехода со старых работающих служб подготовлен
[Invoke-L4FreshTransition.ps1](Invoke-L4FreshTransition.ps1): просмотр без изменений
по умолчанию, `-Apply` — локальный операторский запуск с UAC и предварительным
`--fresh-verify`. Конфигурации старого tools не переносятся и не архивируются;
`C:\l4tools` удаляется после успеха. Системный PATH и UUID сохраняются в закрытом
журнале процедуры. Проверки защитных условий и read-only plan выполнены;
реальный переход ещё не запускался. [Порядок и ограничения](../../docs/ops_run-l4tools-fresh-install.md).
Ниже ранние этапы и старые версии описаны как история.

**Windows layout, этап 2 — основа в разработке:** добавлен read-only
`l4setup.exe --layout-plan`. Команда возвращает JSON с планируемыми корнями
Program Files/ProgramData и существующими ImagePath/аккаунтами четырёх служб.
Не требует elevation, не создаёт каталоги/логи, не запускает install pipeline.
`installation_enabled=false` явно сообщает, что новая раскладка пока не включена
в установку. Общий модуль и ограничения: [l4common](../l4common/README.md).
Команду нельзя сочетать с установочными опциями.

**Опубликованный выпуск 1.10.2**: исправлены read-only `--smoke-only`, DNS false degradation,
независимые TLS budgets, launch/missing-certificate failures и bounded transport
retries при cold policy startup. Диагностика использует SCM ImagePath. Новые
настройки сети при той же версии применяются через Repair; GUI загружает текущие
SCM поля и сохраняет параметры остальных каналов. Новые
бюджеты и проверенные сочетания: [матрица надёжности](../../docs/term_net-leo4proxy-resolving-reliability-matrix.md).
[Запись публикации 1.10.2](../../artifacts/l4tools/1.10.2.json): подписи/timestamps, payload и полные HTTPS downloads проверены.
Сведения ниже об эксплуатации 1.10.1 сохраняются как история; Upgrade773 до 1.10.2 подтверждён операторским логом 16:36 UTC: ready/0, proxy 1.8.1, MQTT/HTTPS/RTP TLS valid.

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


План новой раскладки дополнен полями `access_token_ready` и `access_error` у каждой
службы: захват RUNNING process token с проверкой SCM account/PID/ImagePath. Это
read-only диагностика готовности токена, не проверка writable ProgramData и не
разрешение установки. `installation_enabled=false` сохраняется. Отсутствующая,
остановленная или недоступная служба имеет ready=false и Win32 error.
Общий account-specific ACL/access gate реализован в l4common; создание каталога,
получение фактического desktop token и вызов gate из новой установки ещё не включены.


В общий движок добавлены verified ZIP/release publish и stopped-service switch/
rollback. Они linked в native setup и проверяются штатным run_tests.cmd обеих
архитектур. Новый installer ещё не использует их: отсутствуют bootstrap registration,
launchers и inventory/catalog adapters; journal/config primitives описаны ниже.
`--layout-plan` сохраняет installation_enabled=false. Нельзя запускать текущий
обычный legacy install и считать его проверкой новой Program Files установки.


Native setup теперь также links закрытый operation journal, стабильную сериализацию
SCM плана и per-file config apply/rollback. run_tests.cmd проверяет их на temporary
NTFS fixtures обеих архитектур: missing done, partial tails, corruption, operator
edits, ACL roundtrip и отказ записи. Эти API пока не подключены к installer engine;
bootstrap registration, stable launchers, полный authenticated inventory adapter
и рабочий update state machine остаются впереди. --layout-plan по-прежнему
installation_enabled=false; действующие службы этот этап не переключает.


Bootstrap foundation now plans four absent services with explicit fresh-install
profiles and verifies the supplied complete file inventory. Existing services/query
errors refuse clean bootstrap. LocalSystem is the presently verified explicit profile;
other fresh accounts are unsupported pending real token/ACL evidence. Planning does
not create or start services. New installer orchestration remains disabled.

Stable launcher template is included in both staged payloads and component inventory;
its immutable PF/bin copies and per-tool pointers use the common journal/file engine.
Native bootstrap/launcher gates include real child process stdio, UTF-16 arguments,
CWD and exit code. Production templates are not installed by the existing legacy CLI.
Bootstrap registration/start, PATH provisioning and complete signed inventory adapter
must be connected before the new install can be enabled.


The common bootstrap registration/recovery adapter is now linked: journaled bundle
plan and create/delete intents, UUID+plan owner marker in SCM display name, exact
fingerprint checks, provisional stopped DEMAND_START services and reverse rollback.
A persisted rollback phase prevents registration retries from recreating the bundle.
Delayed deletion waits for actual SCM absence within the caller's aggregate poll
budget; it is not declared complete while a service handle blocks removal.

run_tests.cmd exercises modeled SCM mutations with actual protected native journal,
reopen/hash/file pins and a common wire fixture for x86/x64. Production CreateService/
DeleteService are compiled but not called by existing CLI. No service startup,
AUTO_START commit, PATH provisioning, signed full inventory admission or connectivity
barriers have been connected. --layout-plan remains installation_enabled=false.

Common fresh-bootstrap activation is now compiled: journaled start intent, ordered
proxy/broker/console launch, mandatory fresh barrier before supervisor and after it.
Verified running image/SYSTEM token/handle/PID and application probes are separate
gates. Checks repeat on resume; local READY cannot replace current transport evidence.
Mosquitto has its own full five-minute window. Callback success after deadline fails.

Probe/barrier callbacks are modeled in native tests; actual adapters and live CLI
wiring remain absent. Actual REQ/RSP + fresh EVT/EVA, managed stop/process-exit
recovery, AUTO_START commit, PATH and signed inventory admission are still required.
Failed activation leaves manual services; existing deletion accepts only STOPPED.
--layout-plan still reports installation_enabled=false. No live installation occurs.

Common managed abort now stops the owned provisional bundle in reverse order,
starting with supervisor. It confirms STOPPED and exit of the journaled PID +
creation-time process before progressing; deletion starts only after all stops.
Stop/deletion share one deadline. Unknown STOP/missing done resumes from the same
journal; changed running identity or missing evidence fails closed, with no kill.
The delete-only API cannot bypass stop evidence for services started by this plan.

Native tests model all SCM mutations and process lifecycle. Actual communication
adapters, AUTO_START commit, PATH and signed inventory admission remain pending.
The new layout installer is still disabled; live services/helper are unchanged.


## Readiness adapters (2026-10-06)

`setup_readiness_checks` supplies production callbacks to the existing bootstrap API:
proxy GET /_leo4/info must report ready/certificate_found; broker first proves a
literal loopback TCP listener (full MQTT proof follows after console); console
requires successful QoS1 SUBACK for tsk/rsp/eva; supervisor requires a new successful
orchestration cycle with active status. Mosquitto retains an explicit 300000ms budget.

The mandatory link barrier runs through the existing L4Con MQTT connection. It sends
ordinary zero-UUID REQ, validates RSP (including IoT method_code=0 NOP), then forces a
fresh existing certificate/inventory event75 and requires successful EVA with the
same UUID, event_type_code=75 and dev_event_id. Error, wrong identity, malformed or
late replies and disconnect never complete the barrier. Ordinary command activity
can refuse a probe with BUSY. No second MQTT CONNECT/client ID, new IoT contract,
new event code, presence change or durable event outbox is introduced. A zero-UUID
NOP has no unique request correlation; freshness proof relies on the subsequent
uniquely correlated event/EVA, not on the NOP alone.

Private local health IPC uses protected SYSTEM/Administrators-only message pipes,
rejects remote clients and verifies server PID against read-only SCM before and
after the request. The bootstrap caller still owns exact bundle/process identity
checks. Fixed request: four little-endian u32 values (v1, mode0 local/mode1 link,
budget1..300000ms, reserved0); response is Win32 result plus client receipt.
Timeouts/cancellation fail closed; synchronous external SDK calls remain subject
to the previously specified outer recovery/watchdog budget.

Native tests cover real restricted-token pipe access, wrong server PID, reconnect,
cancellation/deadline and real loopback TCP/IPC, with modeled SCM. MQTT packet tests
cover actual metadata parsing and incoming RSP/EVA dispatch, stale/wrong replies,
error/disconnect. Supervisor cycle tests use modeled service operations. These are
not real-terminal/IoT E2E evidence. Signed inventory admission, bootstrap CLI wiring,
AUTO_START/PATH commit and remote-update quiescence/update-only remain open; new
layout installation is still disabled. No installation or publication. See the active task packet for the pre-existing
component-test isolation incident and corrected verification.


## Signed release admission (native API)

`setup_release_prepare`/`setup_release_verify` require complete authenticated file
inventory, archive SHA256 and expected publisher certificate SHA256. Empty/all-zero
publisher identity is refused. They use the common protected preparation engine and
its mandatory admission hook; existing version reuse repeats signature checks.
EXE/DLL/SYS and all MZ files require Authenticode, exact publisher leaf certificate,
a timestamp validated by the provider and chain revocation checking. Every provider
state is closed; only exact zero is success ([Windows reference](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)).

Provider network/SDK calls are synchronous and have no hard per-call cancellation
guarantee. The outer worker/watchdog budget remains required; revocation outage is
a failed admission, not permission to weaken validation. No trust certificate is
installed, private key read, release signed or service changed by these APIs.

New release manifests include `publisher_certificate_sha256`; signed-release
verification checks it and staged signers against the configured signing certificate.
It is metadata for the authenticated manifest path, not a standalone proof of trust.
Existing published1.13.2 and resources are unchanged. Native tests cover actual
unsigned rejection, actual timestamped signed1.13.2 verification, modeled provider
failures, and protected ZIP prepare/reuse/refusal. Full manifest authentication,
complete layout mapping/config separation and CLI orchestration remain open;
--layout-plan installation_enabled=false remains unchanged.


## Complete descriptor and configuration plan (native API)

`setup_manifest_parse` requires an independently authenticated descriptor SHA256
and publisher certificate SHA256. It checks the digest before interpreting paths,
then validates the exact bounded schema/version/architecture, complete mandatory
inventory, Windows path rules and three approved templates. The owned inventory
feeds `setup_manifest_prepare`, preserving mandatory hash and signature admission.

`setup_manifest_config` only computes immutable release template sources and
ProgramData destinations for supervisor config, Mosquitto ACL and capture INI.
Applying this plan requires the existing journaled configuration transaction.
Downloading/authenticating metadata and wiring the installer worker/commit remain
pending. Neither parser nor plan modifies files/services. New installation stays
`installation_enabled=false`; no live installation or publication was performed.
Producer/native fixture integration passed86 checks on each x86/x64 architecture.


## Detached signature verification boundary

`setup_manifest_parse_signed` verifies a384-byte RSA3072/PKCS#1v1.5/SHA256 signature
through `l4_metadata_verify` before invoking descriptor parsing. It accepts the
411-byte owner-trusted public blob and independently expected publisher identity;
neither may be learned from this document/download. Failure clears the returned
manifest; generic verification clears its digest. No unsigned/network fallback.

Key format and verification use Windows CNG ([RSA blob reference](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_rsakey_blob),
[signature API](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptverifysignature)).
Raw cryptographic verification is not catalog freshness, release admission or
network acquisition. Owner-key embedding, signed root/catalog schema, durable
revision/expiry checks and signing/publication wiring are implemented below;
download and worker connection remain pending.
New installation remains disabled. Tests use public blobs from ephemeral fixture
keys, not the owner signing key; actual Win7/32-bit OS behavior remains unverified.


## Embedded owner metadata trust root

`l4_metadata_verify_trusted` always uses the compiled owner public key; there is no
network/parameter override. `setup_manifest_parse_trusted` applies that gate before
bounded descriptor parsing. The expected Authenticode publisher still comes from
authenticated root release metadata through `setup_root_parse_trusted`; acquisition remains pending.
Injected-key APIs remain low-level/test boundaries and do not establish trust.
Native tests verify the real owner-key non-JSON self-test and reject the ephemeral
fixture signer at the production entry. No release admission follows from a self-test.
The public key ID matches the configured key/pipeline report. Replacing the root
requires the previously agreed owner-controlled signed bootstrap/repair procedure.


## Catalog admission and durable revision floor

Common `l4_catalog_parse_trusted` authenticates with the embedded owner key before
strict bounded JSON/schema/freshness checks. `l4_catalog_resolve` resolves explicit
version or latest to a nonrevoked root manifest digest; `l4_catalog_route` computes
a shortest permitted architecture/profile chain. Catalog/route APIs do not fetch
packages, interpret compatibility evidence or start an installation.

`l4_catalog_accept` requires an open journal holding the common deployment lock.
It durably appends revision/time/exact-catalog digest to protected global
ProgramData state/catalog.floor, independent of operation ID. SHA256 chained
fixed records, pinned parents, private SYSTEM/Administrators ACL, no reparse/
ADS/hardlinks, no sharing and FlushFileBuffers protect the stored floor. Older
revisions, same revision/different bytes and UTC clock rollback refuse. Exact
same revision/digest may be reused before expiry; later accepted time is recorded.

A torn/corrupt floor refuses new admission rather than truncating acknowledged
history. Its8MiB bound fails closed and never silently resets the revision. Owner
repair is separate; already saved operation/local rollback must not depend on a
fresh network catalog. The worker must persist acceptance before new changes and
save the resolved route; these worker/CLI connections are still pending. New-layout
installation remains disabled. Test floor files are isolated under generated TEMP
roots, not installed ProgramData; native x86/x64 tests use ephemeral fixture keys.

## Signed root release admission

`setup_root_parse_trusted` verifies the compiled owner signature and the selected
catalog release's exact root digest before interpreting JSON. It requires schema1,
the catalog's canonical version, both x86/x64 inventories, clean signed provenance,
Valid signing status, Windows6.1 minimum and the embedded metadata key ID/algorithm.
The root contains exactly the current pipeline's18 fields and seven inventoried
assets. There is no historical/unsigned fallback or downloaded key/publisher override.
Descriptive component/provenance data does not authorize files; descriptor inventory
and actual ZIP/Authenticode admission remain mandatory.

Both architecture entries must have the fixed layout JSON/ZIP/signature names,
matching nonzero hashes and positive bounded sizes: descriptor<=65535 bytes,
signature exactly384, archive<=1GiB. Only the chosen architecture's assets are
retained; there are no URLs or redirect authorities in the returned plan.

`setup_root_descriptor_trusted` verifies descriptor and detached signature sizes/
hashes from that root, then the owner signature, descriptor/layout version, arch,
publisher and archive hash. It returns the existing protected ZIP admission plan;
root metadata alone cannot install anything. Injected-key fixture roots cannot be
used with the production descriptor entry, and cannot swap their signing key.

Tests use the real Python metadata finalizer with an ephemeral signer and complete
both architecture inventories, signature-valid malformed roots, duplicate keys,
hash/size/key/version/architecture failures and cross-document publisher/ZIP
mismatches. They make no network/service/file mutations in native root admission.
Registry transport and durable route selection are implemented below;
prepared packages and worker/installer commit remain pending. The FM CONNECT
allowlist remains restricted to policy storage and is not expanded for Registry.

## Registry acquisition and saved route

`l4_registry_fetch` uses a named127.0.0.1 proxy port, fixed public Registry:443,
TLS1.2 and GET. No direct fallback/proxy discovery/bypass, redirects, cookies,
automatic authentication, certificate-ignore flags or terminal client certificate.
Only catalog.json/catalog.json.sig under l4tools/metadata, and the fixed root/
layout JSON/signature/ZIP names under a canonical numeric version are accepted.
Positive bounded Content-Length and complete bounded reads are required; provisional
sink bytes must be discarded on any failure and authenticated before use. The
metadata wrapper allocates<=65535 bytes and requires exactly384 signature bytes.
Timeout checks reject late completion; synchronous WinHTTP may unwind its current
bounded phase after the deadline. Acquisition is outside stopped-service budgets.

`setup_update_acquire_route` downloads signatures/documents through that adapter,
uses local UTC after download, validates owner signature/schema/time/compatibility
route and flushes the global floor before journal record60. Only one selection can
be acknowledged per original operation; a new request cannot replace that plan.
The portable LE codec stores exact signed catalog/signature, original admission
time, current version/root digest and requested version/arch/profile. Root hashes
and evidence for every hop remain pinned in the signed catalog. Reload verifies
the signature at the saved admission time and recomputes the same route offline;
it neither fetches a new latest nor re-admits against the current global floor.
Wrong-key, damaged/ambiguous duplicate route records and malformed input refuse.

`setup_update_acquire_root/descriptor` consume that saved owner-trusted selection;
fixture/injected-key plans cannot enter production acquisition. Root identity must
match the selected hop's exact version/hash/architecture before descriptor I/O.
Fresh catalog admission, individual metadata acquisition and route persistence are
not a prepared installation. `setup_update_prepare_package` downloads the pinned
descriptor and ZIP using the signed root's exact archive size/hash, holds the
private verified cache object and calls full protected inventory/Authenticode/
publisher/timestamp admission before publishing the immutable PF release. Timeout
covers metadata/ZIP transport; filesystem/signature verification is outside the
stopped-service window. A signed metadata fixture with unsigned EXEs is refused
by the actual Authenticode gate and never publishes a PF release. Cache completion
alone grants no permission to stop. Account ACL/config/SCM checks, the complete
prepared operation plan and live worker/RPC connection remain pending. Signed
metadata persistence and all-hop preparation are described below. A failure after successful ZIP download
can leave an unreferenced verified cache object; no automatic adoption/deletion.
Tests model high-level network/trust inputs using ephemeral keys, with real CNG/
protected journal/reopen; the separate actual WinHTTP test refuses an untrusted
TLS peer through loopback CONNECT without changing any Windows certificate store.
Live public Registry through the installed proxy and Win7 remain unverified.

### Durable all-hop preparation worker

`setup_update_prepare_route` consumes an existing original-operation journal and
saved route sequence. It prepares all missing hops, then flushes one packages-only
completion. Record61 uses a bounded portable LE codec: route sequence/step, exact
signed root/descriptor bytes and their384-byte detached signatures, canonical
42-character cache GUID.zip leaf. No pointer/ABI dump, RPC path, secret or mutable
URL. Root/version/architecture/archive/publisher pins come from the saved catalog.
Record62 references every distinct hop record in route order. Duplicate/foreign/
malformed records, completion before all hops or packages after completion refuse.

Resume verifies saved catalog at original admission time, signatures and exact
root/descriptor agreement, private cached ZIP, and existing immutable full inventory
with Authenticode/publisher/timestamp. It acquires only missing hops; expired catalog
does not reselect `latest`. Already complete preparation repeats these local checks
and returns the same completion sequence without network traffic. Missing/damaged
saved cache/release refuses instead of silently replacing the acknowledged plan.
Failed completion append never returns a prepared sequence. Successful hop records
can survive interruption and be reused only after offline revalidation.

`setup_update_load_prepared` returns an opaque plan with held cache pins and
read-only per-hop manifests; caller frees it with `setup_prepared_free`. This worker
core performs preparation only; it does not create a new service, start a background
process, change accounts/configuration/SCM or connect MQTT/RPC. Account/config/source/
service plans, mandatory communication gates and their durable completion must
precede service stop. Cache pins are not permanent EXE pins; apply must use the
existing release fence primitives and recheck actual source/service state.

Tests use ephemeral signatures, actual CNG/cache/journal and explicitly modeled
successful EXE admission for restart/completion cases. Production Authenticode
refusal remains exercised separately and by the signed synthetic-package negative
case. Two-hop transport interruption, failed completion append, catalog expiry,
offline idempotence, corrupted signature, ambiguous journal and missing cache refuse
or resume as specified. No owner fixture signing or live update is performed.

### Current-state preflight before stop

`setup_readiness_preflight` checks saved OLD config snapshots (exact bytes,
existence and security descriptor), verifies the actual data ACL matrix with
captured actor tokens, and captures all four current service PID/creation FILETIME
epochs. Fixed canonical suite commands, LocalSystem account, own-process service,
configured start type and RUNNING status must match the supplied source plan.
It performs the four application probes and mandatory fresh existing-connection
REQ/RSP + EVT/EVA barrier, then repeats config/ACL/service-epoch checks. A restart,
changed config/account/path/start mode, failed ACL/barrier or late success refuses.
One total budget clips every callback; Mosquitto's configured budget remains300000ms.
Successful preflight is ephemeral and must never be restored from a journal as
current connectivity evidence. No service stop/config repair/SCM write occurs.

This is a checking primitive, not the complete update operation gate. The caller
must independently authenticate the installed source using signed root/descriptor,
verify the prepared package plan and durably bind source/config/SCM plans before
allowing a switch. The composite-plan wrapper below supplies that authenticated
source; live worker/RPC/apply connection is still pending. Tests model SCM/ACL verdicts
and orchestration callbacks, retaining actual pipe/TCP adapter tests and actual
process timestamps; real config drift/no-write checks run in journal tests.

### Signed installed source and composite operation plan

`setup_update_plan_operation` links the existing package completion to signed
installed-source metadata, component-prepared config snapshot references, and four
SCM switches for each route hop. It downloads only fixed source root/descriptor
names/version from the original catalog and verifies their owner signatures,
catalog root hash, version/architecture/publisher and actual full signed inventory.
Source raw metadata/signatures are flushed as record63. A catalog-revoked installed
source may be authenticated for a permitted exit route; revoked targets stay banned.
Actual source Authenticode verification is never waived.

The initial SCM commands must point to this exact canonical installed source, with
LocalSystem and AUTO/DEMAND start. Every hop derives its before-state from the prior
hop, preserving arguments/account/start mode. Existing switch codecs record each
plan; record64 references package completion, source receipt, original-state config
records and all switches in route order. No configuration defaults/copy/application,
SCM mutation, service restart or legacy migration. Config proposals still belong
to component adapters and must already be prepared; this core checks exact old state.

Offline reload rechecks signed package/source inventories, config snapshots and
current original SCM, recomputes every switch and compares all stored fields. Duplicate
source/composite records, foreign refs, malformed shape, changed config membership,
changed source paths/start/account or lost/corrupt artifacts refuse. A failed final
append acknowledges no plan; source receipt may be reused offline, orphan switch
records are not adopted and a retry writes a fresh complete reference set.

`setup_update_operation_preflight` loads this owner-authenticated composite plan,
constructs the exact source fingerprint and calls the fresh config/ACL/service-epoch/
REQ-RSP+EVT-EVA checker. No-op routes require no stop and refuse this gate. Neither
record64 nor a cached preflight success permits apply: console/supervisor
quiescence, immutable rollback-helper/budget preparation
and the live worker/apply path still remain. This loader checks the original state;
it must not be used as apply-progress/rollback recovery after services have switched.

Tests use actual ephemeral CNG signatures, protected source/target ZIP/files,
hash inventories, journals and switch serialization/recomputation. Positive EXE
admission, SCM snapshots and wrapper readiness are explicitly modeled only in tests;
actual unsigned-PE rejection and separate real readiness adapter tests remain.

`setup_update_candidate_probe` reloads the owner-authenticated operation, pins the
selected hop's verified Leo4Proxy EXE/ancestors, then starts only that executable
suspended in a private kill-on-close Windows job. Distinct loopback ports49152..65535
and a100..300000ms probe budget are mandatory. Both listening TCP rows must belong
to that live held child before and after strict ready/certificate health. Existing
responders, startup failure and late callbacks refuse. All outcomes terminate only
the owned job and wait up to5s for root process termination before releasing pins.
Job assignment failure (including Win7 nested-job restrictions) fails closed.

The candidate's exclusive `--update-probe <http> <mqtt> <ms> --version` mode branches before
normal startup. It requires a real currently-valid selected terminal certificate
and client/server Schannel credentials, starts actual HTTP/MQTT listeners, serves
only local GET /_leo4/info, closes every MQTT accept before any worker/CONNECT, and
has its own bounded lifetime. No policy identity/cache/poller, remote probe, tray,
discovery, firewall, SCM, reverse/media listeners or writable diagnostic paths.
Effective source certificate selection is connected below; other component argument/
config coverage is still required in the outer worker. Service-token launch is connected
below; no production caller-token fallback is allowed.
Temporary readiness never substitutes full upstream tests and fresh REQ/RSP +
EVT/EVA after the old proxy has stopped. No durable success/permission to stop.
The trailing --version is mandatory: historical proxies ignore unknown switches,
but recognize --version and exit before normal startup. Such candidates cannot
satisfy child listener/health checks and never fall back to ordinary execution.

`run_tests.cmd` covers actual owned-child cleanup/deadline/foreign listener failure
without certificates; composition tests reject invalid ports/hops and mismatching
original service token before any candidate launch. Optional local-stand
`tests/test_candidate_runtime.cmd` requires an accessible real terminal certificate
and freshly built proxy x86/x64; checks ready health, HTTP/CONNECT refusal, MQTT
close, exclusive occupied-port refusal and cleanup. It does not contact backends.

The candidate entry now captures the actual original RUNNING Leo4Proxy primary
token, independently bound to the signed source's original SCM command/start/account.
Only LocalSystem/session0/own-process AUTO-or-DEMAND is supported. An opaque lease
holds that process handle, creation FILETIME, duplicated primary token and its
authentication LUID; source SCM and epoch are rechecked after capture, around
creation and after job assignment before resume. Child SID/session/authentication
must match. No new logon/account/service, caller-token fallback or token adoption.

CreateProcessAsUserW uses the unchanged token session and a Unicode environment
from CreateEnvironmentBlock(FALSE), excluding the calling process environment.
No inherited handles, console or interactive desktop. Required privileges are
enabled only on a duplicate private thread token; caller thread token is restored
before the child may resume. Original service and caller process token privileges
are never modified by this code. If restoration itself fails, its own suspended
child is cancelled and the worker terminates immediately instead of returning
with elevated identity. The future recovery controller must observe worker death.
API references: [CreateProcessAsUserW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw),
[CreateEnvironmentBlock](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-createenvironmentblock).

`tests/test_candidate_runtime.cmd --system` is the optional actual stand gate.
It captures the existing live service read-only, verifies child LocalSystem/session0,
real certificate/listeners and refusals, confirms a synthetic parent environment
canary is absent in a separate owned child, checks caller process privileges and
thread identity are unchanged, then cleans jobs/events. This primitive test may
use the existing service's legacy path; production signed operation still rejects
legacy layouts. No signed full update/service migration is implied. Unit token
tests model SCM only, use real token/epoch/privilege/restore operations, and isolate
the fatal restoration fault in an owned test child.

`proxy_certificate` derives effective --cert-email/--cert-thumbprint/--user-store
from the saved authenticated original SCM command, preserving normal last-wins
behavior and provider thumbprint-over-email precedence. Known unrelated options
are consumed by their normal arity so their values never become certificate flags.
Unknown/operational actions, missing values, truncation, controls/non-ASCII selectors
or malformed nonempty thumbprints refuse before launch. This tightens update
eligibility only; the normal service CLI and discovery logic remain unchanged.
CurrentUser means the same captured service account/session, not the operator.

The common bounded command builder emits only certificate flags with Windows
backslash/quote escaping and mandatory final --version. The dedicated consumer
accepts this whitelist, refuses duplicates/control/network flags and reuses the
actual existing certificate discovery. Empty filters preserve defaults. An explicit
40-hex thumb must match the child's ready health exactly (case-insensitive); missing/
different/malformed fingerprint refuses. No mutable profile/default reselection
from initiator/environment. Default discovery remains fresh; it does not pin a
previously cached certificate when no explicit thumbprint was configured.

Native tests cover source->quoted command->probe roundtrip, empty/duplicate source
filters, spaces/quotes/trailing backslashes, maximum ASCII values and options as
literal selector data, unknown/action/missing/non-ASCII/oversize negatives. Actual
SYSTEM stand additionally covers explicit current thumb overriding nonmatching
email, wrong health thumb, missing thumb and missing email without default fallback.
CurrentUser transfer is covered by roundtrip; a positive live CurrentUser-store
certificate test is not claimed and no certificate/store is modified for testing.

### Fresh layout installation composition (internal native API)

`src/fresh_install.*` prepares an owner-trusted signed complete suite before ACL
provisioning, then journals three signed config templates, explicit component
broker configuration and nine launcher pointers. It requires primary SYSTEM,
impersonation actor tokens, a new locked journal and absent services/config/state/
logs/update.state. Apply revalidates the package/configs/ACLs, installs immutable
launchers, applies configs and invokes bootstrap register/activate/commit with
explicit probes and fresh communication barriers. One context admits one apply;
abort removes owned services and confirms process exit before config rollback.
Successful commit cannot be aborted. Immutable binaries are retained.

The explicit fresh installer entry uses an owned temporary SYSTEM service.
Durable original-context reload supports abort only. Actual signed installation,
communication and reboot acceptance remain pending; legacy installation is disabled. `run_tests.cmd` and `run_tests.cmd x64`
include the composition fixture: real files/ACL/journal/templates/launchers,
modeled admission/SYSTEM/SCM/barriers; no live installation or service mutation.

Normal native preparation uses `setup_fresh_prepare_broker` with an authenticated
discovered SN. Pure rendering preserves the current contract3 localhost listener/
bridge, fourteen exact QoS1 routes and ProgramData logs; no client is connected.
SN/config injection and layout drift refuse. Existing unused ACL templates are
not activated implicitly. Additional local component proposals remain restricted.

`install_path.*` captures the fixed native HKLM Environment Path and journals its
original/candidate bytes/type before any write. It adds one PF/bin entry while
preserving existing entries/placeholders and detecting quoted/case-insensitive
duplicates. Apply follows service activation and precedes final bootstrap commit;
abort restores PATH only after managed service abort, before config rollback.
Exact original/candidate rechecks reject operator drift; unknown mutation does
not trigger blind repair. Records80..84 cover plan/apply/rollback intent/readback.
Tests use a unique HKCU key, not system PATH. Context replay after restart and
final environment-change notification remain public-host responsibilities;
registry value CAS is unavailable, so external concurrent writers are not locked.


### Explicit fresh entry

The new --fresh-help entry supports --fresh-verify and --fresh-install with
--bundle <absolute signed kit>, --fresh-version <original version>, --arch x86|x64
and an optional --operation <UUID>. Only an elevated interactive local operator
can request a fingerprinted temporary SYSTEM host; no elevation is silently run.

Verify prepares authenticated immutable binaries and private operation inputs,
checks the LocalMachine terminal certificate and active local operator token. It
never mutates the four suite services or machine PATH; it is preparation, not a
side-effect-free dry run or a communication acceptance. Install additionally
requires all four old service names absent. Complete signed admission precedes
mutable configs, provisional registration, fresh probes/barriers and commit.

Save the original UUID. --fresh-status with original version/operation/arch reports
historical terminal state, not live health. --fresh-recover uses only original
accepted metadata and receipt85 to abort, even after catalog expiry. Successful
commit cannot be aborted. Foreign state, new process epochs and preparation crash
before receipt85 require explicit owner inspection; no forward retry/adoption.

The uv install-kit command takes --version, --env-file and --output <new absolute
directory>. Its owner-signed authorization expires after seven days, pins exactly
one version/root hash, has stable=null/transitions=[] and refuses overwrite. This
is not stable promotion or mixed-version update admission. Both signed layout
archives are included. Signing and native verification remain mandatory.

Fresh Mosquitto uses service-local REG_MULTI_SZ Environment/MOSQUITTO_DIR pointing
to ProgramData configuration, without changing the global machine variable.
Readiness rechecks it before broker/full barriers. The isolated fixture proves
real SCM/SYSTEM environment inheritance; journal admission is modeled.


Offline bundles import their held authenticated archive into the same unique
private cache used by remote acquisition. Native unpacking accepts only that
canonical cache, never the kit/input directory. Import validates size/hash,
flushes and reopens read-only while holding ancestors; bad/truncated content is
not returned. The signed archive consumer was checked with real owner metadata,
Authenticode/full inventory separately from modeled installer admission.
