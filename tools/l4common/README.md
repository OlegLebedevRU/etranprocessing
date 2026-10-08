# Общая основа Windows-раскладки

`layout.h` / `layout.c` — контракт путей для l4setup и будущего l4update.
Модуль Win32/C, `/MT`, Windows 7 SP1+, x86/x64. Внешних runtime-зависимостей нет.

- `<Program Files>\Leo4\Tools\releases\<version>` — конкретный неизменяемый
  релиз; `bin` — будущие стабильные launchers.
- `<ProgramData>\Leo4\Tools` — config/state/logs/update.
- SYSTEM / Administrators имеют полный доступ; Users — чтение/исполнение.
  Владельцем создаваемых директорий является Administrators. DACL защищён
  от наследования permissive прав родителей, дочерние файлы наследуют его.
- `l4_layout_prepare` создаёт дерево, но не устанавливает payload или службы.
  Требует административного токена с SeRestorePrivilege. Привилегия включается
  только в копии токена текущего потока, прежний токен восстанавливается.
  Ошибка может оставить уже созданные защищённые директории; повторный вызов
  идемпотентен. Существующие файлы не удаляются.
- Предки открываются без share-delete и удерживаются во время записи; reparse
  points, относительные/UNC/device-пути, ADS, dot segments, DOS device names,
  пересекающиеся корни и переполнение MAX_PATH отклоняются.
- `l4_service_inventory` читает ImagePath/account/start type через SCM. Отсутствие
  службы отличается от отказа доступа; аккаунт существующей службы не выбирается
  заново и не меняется этим модулем.

Корни получаются через Windows Known Folders. Для x86 процесса на x64 Windows
ProgramFilesX64 [не поддержан Shell API](https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid):
используется `ProgramFilesDir` из системного 64-битного представления HKLM.
Переменные среды инициатора не определяют установочные корни. Параметры
`l4_layout_from_roots` предназначены для изолированных native-тестов, не являются
CLI override установки.

Проверки входят в существующие `tools/l4setup/run_tests.cmd x86|x64` и автоматически
в uv release prepare. Они создают уникальное временное дерево и удаляют его;
не регистрируют/останавливают службы и не подключаются к MQTT или backends.

Это основа этапа 2, не готовый новый installer. До его включения остаются:
разделение runtime путей всех компонентов; ACL writable leaves по реальным
аккаунтам служб и проверки от их токенов; versioned unpack/SCM/rollback;
инвентаризация активных версий и launchers. Нельзя считать обычный install
нынешнего l4setup установкой в новую раскладку.

Runtime resolver `l4_runtime_path` подключён к supervisor и генератору Mosquitto.
Для канонического `Program Files/Leo4/Tools/releases/<version>` он возвращает
ProgramData config/state/logs; explicit portable build/test tree остаётся
изолированным внутри своего дерева. Это не импорт/миграция C:\l4tools.
`l4_runtime_exe_path` выводит раскладку из EXE компонента, включая `bin/x86/x64`.
Для installed release логи l4capture и crash report leo4proxy используют
ProgramData/logs/<component>; portable diagnostic files остаются рядом с EXE.
Несовпадение имени компонента и неверная installed версия отклоняются.
Resolver не создаёт writable директории: их ACL остаются задачей installer.
Supervisor фиксирует корень запущенного релиза: environment/config не могут
перенаправить установленный EXE к другому набору бинарников. Неудачная загрузка
существующего конфигурационного файла останавливает запуск orchestration.

В новой раскладке старая автоустановка/foreign service cleanup отклоняется:
SCM принадлежит deployment engine. Runtime log ACL repair не удаляет выданные
установщиком права аккаунта Mosquitto. Это ещё не проверка доступа его токена.
Новый шаблон Mosquitto использует `%LOG_PATH%`, `%CONFIG_DIR%`, `%STATE_DIR%`;
`%BASE_PATH%` в установленном versioned release отклоняется. ACL/persistence
директории по реальным аккаунтам и допуск пользовательского шаблона остаются
за установщиком. Topic routes/client IDs и порядок MQTT restart не изменены.

Con использует ProgramData/state/l4con/work и private fm-state, desk — общий
state/l4desk/ffmpeg_state.json, config/l4desk policy и logs/l4desk + logs/ffmpeg.
Proxy policy JSON расположен в state/leo4proxy; записи registry остаются authority
для восстановления существующей policy. Старые JSON locations не импортируются.
Release resolver не принимает environment override; установленный desk дополнительно
отклоняет CLI redirect binary/log roots. ANSI adapter для desk отказывает при lossy
conversion, включая явную поддержку UTF-8 ACP. Stable launcher binaries и подключение account-specific ACL к deployment engine ещё впереди.


`access.h/.c` — account-specific provisioning и отдельный gate доступа. SCM account,
ImagePath и RUNNING PID сверяются до/после захвата реального process token; остановленная,
недоступная или изменившаяся служба отклоняется. SeDebug/SeRestore включаются только
в частном thread token с восстановлением исходного контекста.

`l4_access_prepare` предназначен для offline provisioning: создаёт каталоги и может
заменять ACL. Не вызывать на работающей раскладке. `l4_access_verify` используется
перед остановкой: не создаёт каталоги и не исправляет права. Он проверяет чтение
config и create/write/flush/read/append/rename/delete в уникальных temporary files.
Проверки держат ancestor handles и отклоняют reparse points; очищаются только
собственные probe files. Это проверка каталогов; доступ к конкретным уже существующим
конфигурационным файлам и их содержимое должен проверять deployment engine отдельно.

Все пять реальных actor tokens (proxy, broker, con, superv, desktop) нужны до
provisioning. Desktop token должен принадлежать фактической сессии запуска l4desk;
его получение installer/worker ещё не подключено. Нельзя подменять его SID владельца
или токеном инициатора. Для первоначальной установки отсутствующих служб нужен
отдельный bootstrap gate после их регистрации/запуска; API захвата RUNNING token
не выдаёт отсутствующую службу за проверенную.

Modify выдаётся конкретным SID, без WRITE_DAC/WRITE_OWNER. Корни и новые промежуточные
каталоги остаются закрыты от записи обычного desktop user. Supervisor пишет config
и общий state; broker читает конфигурацию и пишет свои state/logs. Общие con work/fm
и capture logs имеют двух явных writers: con и desktop. Private fm-state исключает
desktop user: gate проверяет отказ чтения/записи/удаления/смены ACL и замены родителя.
Не выдавать Modify группе Users/Authenticated Users/Everyone. Администратор стенда
остаётся доверенным субъектом восстановления.

Con receipt owner/ACE допускают SYSTEM, Administrators либо собственный enabled,
owner-capable service SID из реального token. Обычный account/user SID не становится
доверенным владельцем квитанции. Неподходящий service token вызывает отказ; скрытой
смены аккаунта на SYSTEM нет. Не-SYSTEM service SID/owner-capable profiles ещё не
проверены на реальном стенде. Имеющиеся файлы не переписываются рекурсивно: их ACL,
file-level secret ACL и immutable release audit завершаются в deployment engine.


`release.h/.c` и `service_switch.h/.c` — следующий слой общего движка. Общая
подготовка layout создаёт только контейнер `releases`, без пустого `<version>`.
`l4_release_unpack_publish` проверяет SHA-256 удерживаемого архива до ZIP parsing,
распаковывает только явно перечисленные файлы в отдельный protected staging и
публикует подготовленный каталог переименованием внутри `releases`. SHA-256 и
размер каждого файла проверяются до публикации; файлы сбрасываются на диск.
ZIP reader дублирует удерживаемый Win32 handle: нет повторного открытия имени,
Unicode внешний путь поддерживается. STORE/DEFLATE поддержаны; encrypted,
неизвестные/дублирующиеся/опасные entries и неполный inventory отклоняются.

Размер архива ограничен 1 GiB, файла — 512 MiB, inventory — 4096 файлов и ZIP —
8192 entries. Это Win7/x86 bounds, не измеренный runtime бюджет. Распаковка одного
entry использует heap; при недостатке памяти подготовка отказывает до переключения.
`l4_release_publish` также принимает уже expanded protected payload. Metadata
должны быть аутентифицированы вызывающим кодом: API не проверяет каталог RSA,
Authenticode или PE architecture и не принимает решение stable admission.
Нынешний package-components inventory только EXE недостаточен для полного набора
assets: адаптер полного authenticated file inventory ещё нужно подключить.

Существующая версия только проверяется и повторно используется; overwrite нет.
Audit отклоняет лишние файлы/каталоги, ADS у файлов, hardlink/reparse, неподходящий
owner либо writable для обычного пользователя ACL. New release owner — SYS/BA,
protected ACL SYS/BA full и Users read/execute. Failed `.pending-*` и expanded
`.payload-*` остаются отдельными от версии; удаление/GC и привязка к журналу
операции ещё не подключены. Не выдавать atomic directory rename за завершённый
power-loss recovery: durable operation journal и fault/reboot gate впереди.

Service switch plan содержит исходные account/ImagePath/start type, новый
явный command line и проверенные digest/size обоих EXE. Построение проверяет
полные inventories обоих релизов; старый C:\l4tools отвергается. Args либо явно
подготовлены adapter, либо сохраняется точный старый suffix. Apply и rollback
держат проверенный EXE/ancestors и требуют STOPPED и ожидаемые SCM поля. Меняется
только BinaryPathName: account/password/start type/dependencies/description не
переопределяются. Повторный apply/rollback идемпотентен; вмешательство в captured
account/path/start type отклоняется. Ошибка после SCM write не означает отсутствие
изменения: caller должен провести recovery. Эти функции не останавливают/запускают
службы, не проверяют связь и не регистрируют отсутствующие службы.

План нужно durably сохранить до apply. Локальные serialization/journal и
config transaction добавлены ниже; реальные SCM fault tests, fresh-install
bootstrap и launchers остаются следующими частями. Новые API linked в setup, но не включены в CLI install.
`test_deployment` использует реальные временные NTFS файлы и mock SCM mutation;
никакие действующие службы в этом gate не изменяются.


`journal.h/.c`, `journal_codec.c` и `config_transaction.c` добавляют закрытый
журнал операции и транзакцию отдельного файла конфигурации. UUID — исходный
7031.task_id; share-zero deployment.lock удерживается на весь срок открытого
журнала и исключает вторую операцию для той же раскладки. Lock не требует PID
lease или таймера: закрытие handle после завершения процесса освобождает его.
Per-operation directory и journal имеют protected SYS/BA-only ACL. Журнал
не является очередью сообщений или MQTT outbox.

Формат v1: UUID-bound header, последовательные записи с length/type/sequence,
SHA-256 цепочкой и end marker. Запись завершается FlushFileBuffers. Размер записи
ограничен 256 KiB, журнала — 8 MiB. При открытии отбрасывается только недописанный
хвост; повреждённая завершённая запись, последовательность или header вызывают
отказ. Replay проверяет весь committed prefix до передачи записей visitor.
Отказ записи блокирует append до закрытия/повторного открытия. SHA не является
аутентификацией: доверие обеспечивается защищённым ACL, а SYS/BA — доверенные
субъекты. План SCM сериализуется в явные UTF-8/LE поля, без ABI struct dump;
его roots обязаны совпадать с roots журнала. Сохранение плана не вызывает SCM.

Config prepare сохраняет исходные и новые bytes и self-relative ACL/owner/group
до изменения целевого файла. Применение записывает intent, создаёт соседний
временный файл, flush, сохраняет policy и заменяет файл через MoveFileEx с
WRITE_THROUGH; после readback пишет done. Rollback использует сохранённые bytes
и policy, либо удаляет созданный файл. Повторный вызов после missing done
проверяет фактический файл и идемпотентно завершает переход. Иное содержимое/ACL
оператора не перезаписывается. Для отсутствующего файла итоговый inherited file
ACL предварительно получает сама Windows через временный delete-on-close файл;
каталожные ACE не копируются вслепую. Повторная проверка policy обязательна.

Пути только config-relative, parents удерживаются и проверяются; reparse,
hardlinks и именованные streams у исходных файлов отклоняются. Config <=64 KiB,
SD <=4096 bytes. Wire SD bounds проверяются до WinAPI, descriptor копируется в
aligned memory. Привилегия SeRestore scoped на private thread token; ошибка
восстановления token не позволяет публиковать файл. Caller обязан остановить
всех writers до apply/rollback: API не даёт CAS с параллельной службой и не
является транзакцией сразу нескольких файлов/SCM.

Это локальные primitives, linked в setup без включения нового install. Worker,
SCM intent/done adapter, catalog verification binding, operation phase machine,
bootstrap/launchers и real power-loss/IoT gates ещё не подключены. Тесты используют
временные NTFS fixtures, повторное открытие, corruption и реальные file I/O faults;
они не имитируют отключение питания или установку на терминале.


`bootstrap.h/c` adds read-only planning for a clean installation: explicit profiles
for exactly Leo4Proxy/mosquitto/L4Con/L4Superv, complete authenticated file inventory
supplied by the caller, quoted versioned ImagePaths and explicit start type/arguments.
All four services must be absent; query failures and existing services are refusals.
The currently supported fresh account profile is explicitly supplied LocalSystem;
other profiles refuse, without replacing accounts or collecting passwords. It does
not register/start services, publish PATH, issue certificates or establish connectivity.
Absence preflight does not eliminate a race with subsequent service creation.

`launcher.h/c` and `tools/l4launch` provide per-tool version pointers, protected
immutable bootstrap copies into PF/bin and synchronous CLI execution. Pointers are
prepared using full inventory verification and changed through config plan/apply/
rollback. Immutable launcher installation appends intent before file publication,
readback verifies hash/policy, then appends done. Repeating after missing done can
verify/reuse the existing file; mismatching bytes are refused, never replaced.
Per-file installation is not an all-or-none transaction across the nine launchers.

Pointer grammar is bounded ASCII version/size/hash, with no arbitrary executable
path or arguments. Reader checks protected control parents, owner and permitted
SYS/BA/BU ACEs; only SYS/BA can write. File inheritance from guarded parents is allowed.
Known names map to fixed component paths (capture uses bin/l4capture.exe). Release
EXE/ancestors are pinned during the child lifetime, preventing release GC/replacement.
Explicit application path prevents PATH lookup. Argument suffix/UTF-16, stdio, safe
provisioned work CWD and child exit code are preserved; caller token/environment
are inherited. No service or MQTT client is started by bootstrap planning.

Native tests combine mocked SCM presence/errors, actual immutable publication,
ordinary restricted-token read/write denial, per-component apply/rollback,
malformed/ADS/hardlink/hash pointer refusal and a real redirected child process.
Full service creation/start/reboot/connection gates and actual desktop/Win7 remain
pending. New CLI installation stays disabled; signing/publishing were not run.


`bootstrap_services.c` adds registration and owned rollback of the four-service
bundle, linked in setup without a new CLI entry. Bootstrap plan now contains each
verified EXE size/SHA and requested final start type. Schema-v1 UTF-8/LE serialization
stores roots/version, four fixed names/commands, final start types and EXE metadata.
Save rechecks service absence under the held deployment lock; load enforces canonical
roots/name/path/type/size bounds before any SCM call. Signatures/full-inventory
admission are still caller prerequisites, not replaced by this structural decoder.

Register opens SCM with CONNECT|CREATE_SERVICE and pins each verified EXE while
creating it. Each service is provisional DEMAND_START, LocalSystem, own process,
SERVICE_ERROR_NORMAL, no dependencies/load group. No service starts here, even
when the saved requested final start type is AUTO_START. SCM display name atomically
contains original UUID + plan sequence; no follow-up registry marker write is needed.
Creation intent is flushed first, then exact configuration/STOPPED readback, then done.
Retry of an unknown result reuses only a matching display/account/ImagePath/type/
start/error/dependencies/load-group fingerprint. Existing mismatching services refuse.

Rollback persists a bundle rollback-begin before deleting in reverse order. It
rechecks ownership/configuration/STOPPED after a flushed per-service delete intent;
no stop/kill/start/account change APIs are used. Other owned stopped services may be
cleaned when one object is foreign/running/unavailable. Missing delete-done is recovered
by actual absence. A marked deletion is not success: caller's shared polling budget
(1..300000 ms) waits for SCM absence, closes owned handles, then appends done.
This bounds polling, not synchronous Windows API latency. A timeout leaves an
unfinished rollback that can be resumed after the blocking handle closes.

Once rollback-begin exists, registration of that plan refuses (ERROR_CANCELLED).
Rollback-done is terminal and repeated rollback has no further SCM side effects.
Typed phase inconsistencies/malformed records refuse before mutations. The boundary
with a concurrently acting privileged operator is not an atomic compare/delete;
the deployment file lock serializes suite workers, not arbitrary administrative SCM
clients. SYS/BA remain trusted recovery actors. Final AUTO_START activation, stop/
process-exit/start/readiness and mandatory communication barriers remain external.

Microsoft's documented DeleteService behavior is deferred removal until no open
service handles remain and the service is not running:
[SDK reference](https://github.com/MicrosoftDocs/sdk-api/blob/docs/sdk-api-src/content/winsvc/nf-winsvc-deleteservice.md).
Default service security is created by SCM; no ACL broadening is performed:
[service access rights](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights).

Native gate uses modeled SCM handles/races/failures and real protected journal/reopen/
file hash/pinning. It includes an independently encoded UTF-8/LE fixture checked by
both architectures. No real CreateService/DeleteService, power-loss, Win7 or IoT
checks have been run. New layout installation and worker remain disconnected.

## Bootstrap activation/readiness adapter (2026-10-06)

`l4_bootstrap_activate` activates an already registered fresh-install bundle:
proxy -> broker -> console -> fresh communication barrier -> supervisor -> another
fresh barrier. It is not the remote update switch operation. All four owner/config
fingerprints and EXE hashes are checked/pinned before the first start. EXE pins and
captured process handles remain held until the call finishes.

Start intent is flushed first. RUNNING/START_PENDING recovery requires this plan's
prior start intent; an externally started service is not adopted. Typed replay
enforces registration and ordered readiness prerequisites. SCM RUNNING is followed
by exact image, actual LocalSystem token, held process handle and PID checks, then
a mandatory fresh application probe. Process death or configuration/PID change
refuses progression. Local READY records are historical only; every resumed call
repeats probes and both barriers. There is no overall-success/autostart record.

All callbacks and budgets are explicit. Mosquitto's separate startup + probe window
is exactly 300000 ms. Each other service/barrier window is positive and at most
300000 ms; no production defaults or unmeasured final watchdog budget are introduced.
Callbacks receive remaining time; late success is rejected. Synchronous SDK calls
and callbacks cannot be interrupted here; an external watchdog is still required.
Pure preflight is outside these per-stage windows.

The barrier must obtain ordinary REQ/RSP plus fresh EVT/EVA through the existing
transport, with no duplicate production MQTT client ID. Missing callbacks refuse
before mutation. Actual IoT/application adapters are not connected; native tests
model them. Supervisor update quiescence and console update-only remain worker
integration requirements, outside this fresh-bootstrap adapter.

Failure leaves owned services provisional/manual. Delete rollback still refuses
RUNNING services. Managed stop with confirmed process exit, delete recovery,
AUTO_START commit, signed inventory admission and PATH remain prerequisites to
enabling new install. No stop/kill, automatic rollback or live CLI entry is added.

StartService returns before initialization and can synchronously block while SCM
handles another control request:
[Microsoft reference](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-startservicew).
Evidence is modeled SCM/process lifecycle/probes plus real protected journals,
write failure/reopen, EXE hashes/pins and existing local HTTP probe tests. Real
service activation, production IoT, power-loss and Win7 integration remain open.

## Bootstrap managed abort (2026-10-06)

`l4_bootstrap_abort` persists rollback direction, stops supervisor -> console ->
broker -> proxy, then runs registration deletion. Stop is fail-fast: a live or
unverifiable supervisor prevents lower services being stopped. No deletion begins
until all four stop checks succeed. Stop and deletion share one absolute polling
deadline (caller 1..300000ms); synchronous SDK execution cannot be interrupted.
The existing deletion API also refuses a started service without STOP_DONE.

Activation now durably records each observed process before its readiness probe:
24-byte LE payload (plan u64, index u32, PID u32, creation FILETIME u64). Stop intent
precedes ControlService. STOP_DONE requires both SCM STOPPED and exit of that exact
process. Running processes must still match owner/config/image/SYSTEM token and
recorded PID/creation time. Held handles protect the live identity during checks.
On reopen, a missing PID or different creation time proves the original process
gone; access denial never does. No replacement process is stopped or killed.

SCM's PID is undefined in pending/stopped states, so those states use only the
durable process identity ([Microsoft status reference](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatusex)).
Creation time comes from [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes).
ControlService is a request, followed by status and process-exit checks:
[Microsoft control reference](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-controlservice).

Unknown STOP result and missing journal done can resume from STOP_PENDING/STOPPED
without resending STOP. An external STOP_PENDING without our intent refuses.
START_PENDING waits within budget for a valid running identity. A started service
already STOPPED before any identity was captured is uncertain: abort returns
ERROR_NOT_READY and requires owner recovery rather than guessing that no process
remains. Foreign edits or changed running generation also refuse. No force-kill,
account changes or automatic recovery-policy changes are introduced. The file lock
does not serialize concurrent privileged SCM operators; they remain trusted actors.

Tests model SCM/process lifecycle but use real protected journals, flush failures,
reopen and release file hashes/pins. They cover early STOPPED, delayed process exit,
PID reuse, access denial, every stop failure position, aggregate timeout and missing
STOP_DONE. Production ControlService is compiled into setup but no live entry calls
it. New install/update worker, actual IoT/application checks, AUTO_START commit,
PATH and signed full inventory admission remain disconnected. Helper unchanged.


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


## Mandatory signed admission hook

`l4_release_unpack_publish_checked` requires a non-null admission callback after
file size/SHA256 validation, while the file and ancestors remain locked. It runs
on the protected pending bundle before publication and repeats on existing version
reuse. `l4_release_verify_checked` repeats it on an installed release. Failure
leaves a quarantined pending directory or refuses the existing version; it never
overwrites the release. Hash-only APIs remain lower-level primitives.

Setup supplies Windows Authenticode admission for EXE/DLL/SYS and MZ files, exact
publisher leaf-certificate SHA256, trusted timestamp and chain revocation checks.
Data remains covered by the authenticated complete inventory hashes. Trusted
metadata acquisition and legacy ZIP/root-config to new-layout separation remain
caller responsibilities; this is not an enabled installation worker.

## Registry and immutable routing plan

`registry_target.h` fixes the public Registry authority. The uv config loader
requires the same authority before preparing/signing/publishing. `registry_http`
uses only an explicit loopback Leo4Proxy with end-to-end native WinHTTP TLS1.2,
fixed GET paths, no redirects/auth/cert-ignore/direct fallback and bounded complete
responses. The stream sink receives provisional bytes; callers authenticate and
discard incomplete downloads rather than treating receipt as admission.

`route_plan` saves exact signed catalog bytes/signature and source/request/arch/
profile/original admission time in protected journal record60, under the common
deployment lock. Fresh schema/time/BFS and global floor flush precede journal
append/flush. One immutable record per operation, no ABI dump or mutable latest.
Owner-trusted load verifies the saved catalog using its original admission time,
reconstructs the exact route/root/evidence pins and makes no new floor admission.
Local recovery can thus survive newer catalog revision/expiry without new network
permission. It still requires verified prepared packages and matching actual source
state; saved routing alone authorizes no service stop or install. Injected-key
fixture loads are marked untrusted for the production acquisition consumer.

`package_cache` streams a ZIP into a unique CREATE_NEW GUID leaf in the protected
cache, with private protected SYS/BA ACL and pinned canonical ancestors. Exact
signed root size and SHA256 are checked after flush, then checked again after
dropping write access. The returned object holds read-only file/parent handles
without write/delete sharing through package admission. Reopen accepts only the
canonical generated leaf and repeats size/hash/owner/ACL/ADS/reparse/hardlink checks.
Failed downloads delete only their own newly created object by handle; a crash
can leave an unreferenced quarantine file, never automatically adopted or scanned
as a prepared package. Cache identity is not Authenticode or installation readiness.

The setup preparation worker now persists exact signed root/descriptor and cache
leaf per hop in record61 and references all hops in record62 after complete signed
admission. It reuses the common deployment lock and verified portable journal,
rechecks saved hops offline before resuming, and never renews the selected catalog.
All-hop package completion is separate from overall source/account/config/SCM/
communication readiness. See `tools/l4setup/src/update_metadata.h` and setup README.

`l4_config_verify` reuses the typed bounded config decoder and secure filesystem
snapshot to check saved original/candidate state without apply/rollback, intent or
DONE append. Exact bytes/existence/security policy must match; missing-original
also verifies inherited file policy through an owned delete-on-close probe. It
never repairs changed contents or adopts an operator edit. The setup pre-stop
preflight uses original-state checks before and after the fresh link barrier.

`child_probe` is a bounded pre-stop process primitive: no inherited handles,
suspended create -> private kill-on-close job -> resume -> caller check -> owned
job termination on every result (bounded5s cleanup wait). It never adopts/kills a
PID or controls SCM. Caller retains its authenticated EXE/ancestor fence. TCP
ownership checks require both exact127.0.0.1 listening rows on the held live PID.
Probe success is transient and never grants apply/stop or replaces signal barriers.

`service_token` captures fixed Leo4Proxy only against an independently authenticated
expected original SCM snapshot; only RUNNING own-process LocalSystem/session0.
Opaque lease holds actual process/creation FILETIME/duplicated primary identity.
Capture/create/verify refuse source changes and unsupported tokens. A clean Unicode
non-inherited environment and private thread privilege scope serve suspended
CreateProcessAsUserW launch. Caller thread restored and child SID/session/auth LUID
checked before `child_probe_service` assigns its private job and rechecks source
before resume. No fabricated logon/account/SCM, process-wide privilege edits or
operator-token fallback. Restoration failure cancels its own suspended child and
terminates the worker; it cannot return to update code with elevated identity.
The current-token `child_probe` remains a test/foundation primitive, not production
candidate admission. Native link adds only Windows7+ SDK userenv.lib/advapi32.lib.

`proxy_certificate` is a shared bounded source CLI/candidate probe adapter. It
extracts effective last-wins certificate email/thumb/store while consuming known
other arguments by arity; unknown/action/truncating/non-ASCII source options refuse.
Correct Windows quote/backslash escaping emits only certificate options plus the
mandatory --version guard. Dedicated consumer accepts no service/network actions
or duplicates. Native roundtrip tests exercise both ends, including selector text
that resembles commands; discovery precedence itself stays with Leo4Proxy.

`update_guard` is an in-process admission/drain primitive, not a persistent
maintenance marker or authorization endpoint. An owner is the nonzero original
RPC UUID; exact retry does not extend the absolute monotonic deadline. Window1
blocks ordinary communication and other work; window2 permits communication
oversight while blocking other work. Accepted tickets remain owned until actual
completion. Expiry stays closed and requires external verified rollback; only
the owner can release, after all tickets leave. No automatic timeout release.
An authenticated controller must reconcile durable state on startup and retain
tickets across async work. Service activation, IPC, watchdog and recovery are
not implemented by this primitive. The native gate runs under l4con build.cmd
for x86/x64 and exercises real threaded drain alongside negative owner/timeout
and window-scope cases; it does not claim live supervisor quiescence.

`update_state` persists the service admission restriction in one fixed protected
operations/update.state file. The 112-byte LE format binds version, original UUID,
generation, saved operation64 sequence, window and UTC FILETIME deadline, with
SHA256 corruption detection. Root/ancestor and SYS/BA-only protected file policy,
single link, no reparse/ADS and exact size are mandatory.
All three control parents (Data/update/operations) permit non-SYS/BA read only,
including specific service/account ACEs; DELETE_CHILD or other writes refuse.
Missing is a failure, not normal. Readers do not acquire the deployment lock. Deadline never releases
admission; an explicit terminal window0 replaces the file after verified recovery
or success. The checksum is not an owner signature; ACL and the trusted producer
are the authority for local state.

`update_state_store` is the internal producer under the journal lock: flushed
record65 intent precedes flushed unique private temp + atomic same-volume replace.
CAS generation, active owner/plan binding, no deadline extension within a window,
no expired advance and exact idempotent retry; no auto-adopt/repair/delete release.
State storage does not verify the complete operation64 admission or authorize
stop/apply. The controller must do those checks and verify completion/rollback
before terminal publication. Failed owner-scope restoration terminates its own
worker rather than returning with changed privileges.

Fresh bootstrap registration provisions the clear baseline before SCM creation,
only when all fixed services are absent. Existing clear state remains; active or
corrupt state refuses, including resume with a missing marker and existing service.
Installed con checks state before ordinary RPC/navigation/user events; existing
task results and fresh link RSP/EVA continue. Installed supervisor checks before
startup ACL writes and each cycle; window1 does no ordinary orchestration, window2
observes proxy identity and proxy/broker SCM health without mutation. Portable
development does not inspect production state. Actual drain acknowledgements and
async tickets, rollback/watchdog/controller and live activation are still pending.

## Operation-bound drain acknowledgement — 2026-10-07

Existing SYS/BA health pipe supports a read-only v2 request: fixed88 bytes
(version2/mode2/budget/reserved +72-byte canonical owner/window/generation/plan/
UTCdeadline); response76 bytes = Win32 result + exact72-byte identity echo and
existing client receipt. Legacy v1 health/link requests are unchanged. No marker
mutation, MQTT connection or new remote RPC is introduced. Invalid/oversized,
wrong PID, wrong echo, stale state, expiry and cancellation refuse success.

Installed services hold their consumer admission SRW lock shared THROUGH ordinary
dispatch/publication; drain takes it exclusive with a bounded cancellable wait,
reads exact protected state, checks pending async work, then rechecks state/time.
This fences decisions made before marker publication. Acknowledgement is a fresh
observation for the original SCM process epoch, not permission to stop or a durable
lease. Controller must retain the preflight PID+creation identity (never substitute
a restarted process), repeat source/config/actor/state checks and fresh link barrier
before stopping. Late IPC reply cannot pass the client operation deadline.

Con waits for command final cleanup, queued/running FM, live FM lease and pending
RPC/FMR results. Parent FM mutations/pump are serialized against drain. The owned
FM child reports its exact protected quiet snapshot via its existing private
inherited mapping: background hello/reconcile holds a local fence, pauses on
active/missing/unsafe state, and must complete before reporting quiet. New FM child
spawns/restarts are suppressed while update is active or state is unsafe. Accepted
jobs may finish; controller cannot stop until ACK and fresh checks pass.

Supervisor waits for the complete synchronous cycle, including PIN/config/SCM
transitions. Unconfirmed owned l4pin termination retains the exact handle and
blocks new ordinary cycles/drain until exit is observed. This does not acknowledge
independent l4desk/input work; updater still owns its later stop. Restart/recovery,
verified multi-hop progress, external watchdog/helper and RPC703x controller remain
pending; no live apply/installation is enabled.

## Pre-stop controller gate — 2026-10-07

`setup_update_capture_stop` reloads owner-authenticated composite operation64,
requires explicit valid clear state, runs the mandatory fresh preflight and saves
original four SCM PID+creation epochs into opaque single-owner SetupStopGate.
Original journal stays open/locked; gate is process-local, never persisted/adopted.
After the outer worker arms the independent watchdog/helper and publishes window1,
`setup_update_confirm_stop` atomically consumes the gate exactly once (including
failure), matches journal/operation/next generation/window and reloads signed plan,
cache inventory/config/SCM again within one aggregate budget.

`setup_readiness_quiescence` requires those ORIGINAL epochs before/after every
read-only drain (supervisor then console). It checks exact marker/config/actor ACL
and all four original service epochs, probes proxy/broker, obtains a fresh existing
REQ/RSP+EVT/EVA and repeats local checks. Ordinary supervisor health is deliberately
omitted while window1 is active. State and process changes, partial/late drain,
channel failure, expiry and invalid budgets refuse success. Mosquitto5min setting
is unchanged; individual checks clip to total monotonic and marker UTC deadlines.

These functions write no configuration, SCM, marker or durable READY. Actor ACL
verification retains existing uniquely owned temporary I/O probes. They do not
stop/apply/clear/recover or prove candidate-port/independent watchdog readiness;
those remain outer-controller prerequisites. A failure after marker publication
leaves update-only in force for the external recovery owner; no automatic clear.
An ACK is a momentary pre-stop check, never reusable permission for later stop.
Actual typed stop/progress/recovery/helper and live773 acceptance remain pending.

### Supervisor recovery contract/store (2026-10-07)

`recovery_plan` is the initial fixed supervisor-only binary codec: original
UUID/operation64, actual worker PID+creation, canonical before/after supervisor
images and SHA256/sizes, unchanged arguments, config snapshots/private ACL,
absolute deadline and explicit bounded recovery budget. Little-endian x86/x64
format contains no pointers; checksum binds exact bytes, not owner signature.
No generic service/path/command/catalog/network selection. Reader validates
bounded self-relative SD before Win32 access, including unaligned config body.

`recovery_store` publishes intent66 then immutable private supervisor.recovery
under the producer journal lock. Trusted controller must first verify signed
source/config/worker Job; producer checks actual live worker handle and existing
operation64 reference. Exact retry adds no intent; mismatched plan is refused.
Independent open reads plan and takes only supervisor.decision.lock, not journal
or deployment.lock. SYS/BA owner/protected ACL, no ADS/hardlink/reparse, pinned
parents and exact plan/result hash/UUID are required; missing lock is not created.

COMMITTED before deadline races STARTED after deadline/boot; one decision wins.
STARTED blocks subsequent commit and persists across release/reopen. Release the
decision lock BEFORE stopping worker or acquiring deployment.lock. Plan remains
pinned while released; state APIs refuse use without reacquisition. RESTORED or
FAILED follows STARTED only; exact terminal retries do not reopen election.
FAILED blocks automatic retries; corrupt/foreign result refuses progress.
No marker clear, service stop/start, task registration or process kill exists in
these APIs. Actual fixed helper executable/Job/SCM/config/health restoration and
protected deadline/boot tasks remain required before live update activation.

2026-10-07 helper integration: initial unpublished plan includes original supervisor
PID/FILETIME, not just worker. Producer must verify original epoch/quiescence and
protected UUID Job kill-on-close/no breakaway before arm. Private zero-byte
supervisor.runner.lock now provisioned with decision lock; helper never creates
missing locks. Runner is acquired without holding decision lock and completion
rechecked, avoiding duplicate executions/deadlock while decision is released.
`L4_RECOVERY_READER_ONLY` compiles journal file-security primitives only and removes
recovery producer APIs, so standalone helper cannot replay/append general journal.
See tools/l4rollback/README.md for fixed restore/Job/health behavior and boundaries;
Task Scheduler/bootstrap installer/controller/live acceptance remain pending.

### Independent recovery scheduler adapter (2026-10-07)

Producer-only recovery_task/core + local Windows COM arm/audit creates one private
`\L4ToolsRecovery\Supervisor.<original-UUID>` task. It binds the immutable plan
checksum and authenticated bootstrap helper size/SHA256, with fixed Program Files
recovery/l4rollback.exe action. SYSTEM/HighestAvailable and protected SYS/BA folder
and task ACLs are required; mismatched existing objects are refused, never updated.

Absolute deadline is rounded UP to the next whole UTC second. Time and immediate
boot triggers use existing protected --boot recovery mode. Demand start disabled,
IgnoreNew, no retries/repetition/network/idle/battery blockers. Execution limit is
ceil((recovery_ms + explicit overhead_ms)/1000) seconds. Overhead is 2000..60000ms:
minimum covers helper initial decision-open and final failure-save waits; caller
adds measured startup/scheduler reserve. No implicit budget or claim that the OS
setting proves bounded termination under blocked I/O.

Original journal/deployment lock and private plan WAIT required before/after;
decision lock released throughout scheduler callbacks so helper can elect rollback.
Intent67 is flushed before create. Unknown registration result fails arm; matching
existing task retry/audit adds no intent. Success rechecks pinned helper/parents/hash,
folder ACL, two normalized XML/task ACL readbacks, effective SYSTEM ServiceAccount
principal and enabled flag. Fresh observation only, no durable READY/stop permission.
No task update/delete/run API; authenticated bootstrap inventory supplied by owner.

XML omits LogonType; API registration explicitly selects ServiceAccount and effective
principal readback verifies it. See [Microsoft XML LogonType schema](https://learn.microsoft.com/en-us/windows/win32/taskschd/taskschedulerschema-logontype-principaltype-element).
x86/x64 fixtures166/0 each: real private files/journal/locks, modeled registration.
Real read-only TaskDefinition XML acceptance/normalization, no task/folder writes.
Actual SYSTEM registration/readback, deadline/boot firing, force termination,
bootstrap packaging/Job/controller integration and live acceptance remain pending.

### Producer worker Job ownership (2026-10-07)

`worker_job` creates only a NEW Global/L4UpdateWorker.{original-GUID} kernel Job
under original producer journal, SYSTEM gate and protected SYS/BA exact full-access
ACL. Name collisions (including another kernel object type) are refused without
adoption or changes. Kill-on-last-close only; no breakaway or inheritable handles.
The parent stays outside its Job and retains ownership until terminal cleanup.

Trusted controller must authenticate/pin executable/directory and authorize command
and explicit double-NUL Unicode environment. This API resolves no catalog/path and
exposes no CLI. It creates its own worker suspended with no inherited handles,
assigns it to the Job before any execution, binds its held PID+creation epoch, and
refuses failed assignment. It never attaches an arbitrary running PID. Assignment
failure cancels only the locally created never-resumed child and observes its exit.

Borrowed process handle permits immutable recovery_prepare while suspended.
Production resume requires original journal UUID, private immutable matching plan
and a fresh SYSTEM recovery-task audit; decision lock stays free during audit.
Recheck Job ACL/profile/worker membership/epoch/liveness after audit. Atomic resume
election permits only one caller; unexpected suspension count fails and cancels the
Job, never silently retries. Controller serializes spawn/close lifecycle; no close
while resume is in flight. Gate success alone does not publish marker or authorize
service switch. Retain Job outside worker; do not inherit/transfer its handle to it.

Explicit close terminates only this owned Job and observes held worker exit PLUS
zero active processes within supplied budget, then always releases handles.
Last-close protection alone cannot be assumed while external query handles exist.
Timeout/failed observation is failed cleanup, not success. Helper opens exactly the
same UUID Job; tests use its existing worker adapter without changing helper binary.

x86/x64 /WX native fixtures80/0 each: actual suspended worker/descendant, fresh env,
private ACL and breakaway drift, concurrent resume, exact epoch/name collision,
existing helper adapter termination and zero active processes. Task-audit callback
is modeled; no SYSTEM task registration or live update. Windows7 rejects nested
Jobs; failed OS confinement must refuse startup, never relax limits. See
[Microsoft Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects).
Actual authenticated worker, SYSTEM controller/task/bootstrap and live acceptance
remain pending; ordinary suite/helper rollout is unchanged.

### SYSTEM worker-start composition (2026-10-07)

`worker_start` connects original locked journal and explicit clear marker to new
Job, locally created suspended worker, immutable recovery_prepare, task arm,
fresh audited resume and final Job/epoch/clear/WAIT check. Caller authenticates
and pins worker/arguments/environment and supervisor snapshots before entry.
Template worker PID/creation must be zero: composition fills them from its actual
held child. Original UUID, operation64, codec, bootstrap inventory/budgets and time
are validated before spawn. Existing recovery file refuses a new PID/retry under
the same UUID; no adoption, replacement or automatic cleanup of persistent records.

Success returns retained Job owner while original journal remains locked. This is
process launch, not completed controller handoff or permission to stop services.
Actual worker must wait for the controller's admission/journal handoff; no marker
publication or apply may race initial clear-state checks. This protocol/payload and
host service/RPC wiring are still pending. Do not expose the callback test adapter
as a production token/CLI override.

Failure cancels only owned Job/child and preserves original stage/error separately
from cleanup failure. Immutable plan and possibly created task are retained; no
COMMITTED result for an aborted launch, task deletion or state clear. A positive
plan_published flag proves observed publication; false after I/O failure cannot
prove absence. task_attempted includes unknown registration outcome. Existing helper
must refuse a different operation/window; abandoned task is not permission to
restore while this operation has not entered supervisor window2.

Fixtures use actual Windows Job/worker/private journal/recovery store and decision
locks; only SYSTEM creation and scheduler adapters are substituted. Failure at each
stage, unknown task result, changed completion during arm/resume, validation before
spawn and refusal of a second PID are exercised. No real task/SCM/MQTT operation.

### Controller/worker journal handoff (2026-10-07)

`worker_handoff` SYSTEM producer flushes one ticket68 then consumes/closes original
journal, releasing deployment.lock; Job remains controller-owned. Ticket binds
original UUID/operation64, immutable recovery checksum, actual parent/worker PID+
creation, clear-state generation, authenticated helper size/hash and explicit task
overhead. No inherited journal/Job handle, new operation or migration. Existing
ticket/receipt anywhere in verified journal prevents reissue, including after reopen.
Failures retain parent journal for cancellation/recovery; no task/plan deletion.

Exact SYSTEM worker uses known-folder roots + original UUID and bounded polling
of existing journal/deployment.lock. Missing deployment.lock is now refused by
journal_open(create=false), never recreated. Consumer checks complete journal
history, one last ticket68/no receipt69, exact self epoch/Job confinement, original
parent epoch/liveness, immutable plan digest, explicit clear generation and WAIT.
Decision lock released for fresh task audit; parent, Job/security/profile, state
and WAIT rechecked afterward. One flushed receipt69 binds ticket SHA256 before
returning held journal. Repeat acceptance refuses; invalid/late receipt is not READY.

Job query handle is local to admission, never retained by worker after return.
Controller must retain its Job ownership until terminal result; kernel last-close
protection alone has query-handle/blocked-I/O caveats. Parent loss must not be taken
as permission to proceed. Every later typed action still needs live ownership and
recovery checks. Admission is journal ownership, not signed source/pre-stop/SCM
authorization, nor successful suite update or an external event/RPC contract.

Actual child/process/Job/journal/file-lock/ticket/receipt fixtures test lock wait,
timeout, wrong process, replay/reissue, failed audit, changed decision and Job drift.
Scheduler audit remains modeled. Actual worker payload/SYSTEM host/real Scheduler
boot/force termination, signed admission and typed apply/rollback still pending.

### Fresh admitted-worker proof before signed pre-stop capture

worker_recheck is SYSTEM-only/read-only while receipt69 is last. Verify one ticket/
receipt and hash, exact self/parent epochs, Job profile, immutable recovery, explicit
clear generation/WAIT and fresh scheduler audit again. Decision lock free during
audit. Copy fixed supervisor commands/hashes/sizes/start/original epoch and operation/
generation/deadline; no config pointers, receipt/journal append or marker. Failure
zeroes output; native separate child repeats proof without journal mutation.

Setup worker capture checks proof before/after NEW process-local signed operation64/
pre-stop capture, binding recovery to FIRST signed supervisor switch and original
epoch. Drift/generation changes refuse. Signed switch retained from capture's
authenticated reload, avoiding redundant third metadata reload. Source/config/
all-hop packages and fresh link checks remain required; confirm still one-use.

Available helper covers supervisor/window2 only. Independent communication rollback/
watchdog NOT implemented; live window1 publication/stop remains disabled. This stage
prepares fresh gate only; parent gate cannot transfer. Actual worker host/scheduler,
multi-hop/config recovery admission and typed service transitions remain pending.


### Window1 recovery policy and mandatory protection query (2026-10-07)

Communication recovery is owned by L4Superv; immutable supervisor-only l4rollback
remains unchanged. Common communication_recovery implements fixed one-shot policy:
exact ownership/source verification -> owned worker/Job exit -> deployment lock ->
repeat verification -> stop/restore/start/probe Leo4Proxy -> actual local signal endpoints ->
stop/restore Mosquitto -> start/probe with full300000ms -> fresh final barrier.
All explicit budgets must cover the sum, including two verifications/channel checks;
proxy and broker preparation/start phases have shared non-extendable subdeadlines.
Late success fails. Failure stops progression; final barrier failure retains locally
verified old pair as connectivity_unconfirmed. No automatic marker clear or retry.
Process-local atomic claim is NOT durable arbitration or boot recovery evidence.

Private SYS/BA IPC v2 mode3 queries exact independently armed window1 recovery;
health/drain never substitute. Setup pre-publication watch-ready checks unchanged
clear state, and one-shot stop confirmation queries before/after quiescence.
Missing/old/unarmed supervisor refuses. Current native supervisor has NO recovery
handler: production remains closed. This is policy+gate foundation, NOT completed
independent runtime watchdog: immutable pair/config ownership store, durable claim,
owned Job/SCM adapter and independent deadline/boot execution are still required.
No MQTT client/IoT/PB/MB change, service/task/publication action or helper rebuild.

Recovery local proxy-channel checks must not require a currently working broker
or available IoT host: a broken Mosquitto would otherwise prevent its own repair.
Fresh REQ/RSP+EVT/EVA is mandatory after restoring the entire old pair. Forward
update still verifies the new proxy with the old broker before switching Mosquitto.


### Immutable communication recovery binding (2026-10-07)

communication_plan/store now provides fixed window1 wire + protected immutable
communication.recovery, read independently of deployment.lock with held file/parent
pins. Exact original UUID, operation64 SHA256, next generation/deadline, actual
worker/supervisor PID+creation, explicit sum-covered budgets, first two record10
copies and mandatory existing broker config record20 copies (mosquitto/mosquitto.conf,
mosquitto/acl.conf). Decoder restricts canonical release images/services, LocalSystem,
AUTO/DEMAND start mode, preserved arguments, bounded configs and safe original SD.
SHA256 is corruption/binding protection; signed source producer admission is external.

Trusted producer verifies actual live worker handle and supervisor epoch, explicit
clear generation, operation64 first-hop switch references/config membership, exact
journal record bytes and current old config/SD before intent70 + immutable publish.
Exact retry does not append; conflicts/corruption refuse overwrite/repair. Reader
rejects writable ACLs, ADS, hardlinks, unsafe ancestors, malformed/rehashed payloads
and foreign operation; keeps file pinned against write/delete while borrowed bytes
are in use. Broker snapshots must exist, including unchanged files needing recovery.

L4Superv now links READER_ONLY store and validates the protected binding at startup
for active window1. No producer/journal intent70 publication code in supervisor.
Missing/mismatched plan keeps ordinary admission closed. Matching plan is NOT an
armed watchdog: mode3 readiness still refuses. Immutable l4rollback unchanged.

Next: authenticated setup plan producer/capture for both broker files; durable
communication decision/arbitration; owned worker Job exit + native fixed SCM/config
restore; independent supervisor deadline/boot executor and genuine readiness.
Existing worker_recheck requires receipt69 last: intent70 must be integrated at the
controller preparation boundary or use a subsequent typed ownership proof, never
relax receipt admission. Multi-hop/config semantics and runtime fault/E2E still open.


### Signed communication producer and durable arbitration (2026-10-07)

Setup controller prepare_communication runs BEFORE ticket68/receipt69, preserving
strict admitted-worker receipt rules. It reloads signed source/all-hop operation64,
captures fresh original epochs/channels, matches immutable supervisor recovery to
first signed switch/original supervisor epoch, repeats original owned Job proof and
WAIT, and selects only the two prepared broker snapshots. Saved record10/20 bytes
and operation digest reach the immutable publisher. Fixed armed/deadline supplied
by caller; communication deadline + full recovery total must fit BEFORE immutable
supervisor deadline. No config defaults, source reselection, marker or SCM change.
Missing broker proposal, drift, Job/epoch mismatch, late transfer and storage failure
refuse. Caller must cancel/retain recovery on failure, never assume absent publication.

Communication publisher also creates private empty decision/runner locks before
plan publication. Independent existing-only decision reader/writer uses exact active
window1 marker and96-byte hashed result bound to original UUID+plan checksum.
WAIT -> COMMITTED (verified NEW LINK only) before deadline, OR WAIT -> STARTED.
STARTED is irrevocable against worker commit. Separate exclusive runner lock stays
held when short decision lock is released before worker exit/deployment lock.
Only its owner can finish RESTORED/UNCONFIRMED/FAILED; exact terminal retry is read-only.
Verified boot can continue STARTED only after old runner OS lock is gone; normal
retry refuses. Clear/mismatched marker, missing/unsafe/corrupt locks/results refuse.
UNCONFIRMED/FAILED block automatic retries; no marker clear or SCM action here.

Supervisor L4_COMMUNICATION_READER_ONLY excludes immutable PLAN publisher and
journal appends, but now includes its future private decision/result writer. Current
startup only reads plan; no timer/recovery handler registered, no readiness success.
Native config/SCM/owned Job adapter + independent deadline/boot monitor, typed apply/
window transitions and773 runtime acceptance remain required before live entry.

Native communication runtime composes protected arbitration, original Job exit,
existing-only journal validation, fixed SCM pair restoration, saved broker config
transactions and mandatory caller-owned local signal probes/barrier. Native total
includes two extra verify_ms reserves for initial claim and final publication.
Independent monitor owns a Win32 thread, immutable deadline and retained context;
close never frees a running executor. Both modules require original SYSTEM owner,
refuse thread impersonation and cannot admit a new supervisor after restart.
They are linked into supervisor but not registered for live recovery/readiness;
production signal adapters, verified boot identity and real SYSTEM/SCM acceptance
are pending. No helper change, MQTT client, marker clearing or automatic retry.


### Fresh bootstrap start-type finalization (2026-10-07)

`l4_bootstrap_commit` finalizes the explicit AUTO/DEMAND profile only for the
original activated four-service bootstrap plan. It pins all four old images,
SCM owner/configuration and original recorded primary SYSTEM process epochs;
fresh application probes and existing Con REQ/RSP+EVT/EVA run before and after
start-type changes. Records52..55 bind commit begin, per-service intent/readback,
and final completion to the same bootstrap sequence. One aggregate explicit
budget includes SCM/callback time; a late synchronous return is rejected, not
preempted. Historical READY or COMMIT records never replace a fresh barrier.

Interrupted commit accepts only the plan's manual/selected start types backed
by its own flushed intent, exact recorded epochs and unchanged fingerprint.
Managed abort first durably excludes commit, restores only those owned start
types to manual, then uses the existing reverse stop/process-exit/delete path.
Unknown SCM/journal result remains incomplete; foreign state is retained rather
than repaired. Successful commit forbids bootstrap abort/registration/activation;
a repeated commit is read-only and repeats fresh checks, never starts services.
After a reboot changes the recorded epochs, automatic commit/abort refuses;
actual reboot/owner repair acceptance remains required.

This primitive does not authenticate a caller-supplied inventory independently,
prepare configs/ACLs/launchers/PATH, enable a public installer entry, migrate
C:\l4tools or admit live update. Signed complete suite and final fresh installer
orchestration remain mandatory. Frozen helper and published1.13.2 are unchanged.

### Fresh installer composition (2026-10-07)

`tools/l4setup/src/fresh_install.*` connects production owner-trusted descriptor
admission and signed complete archive preparation to offline ACL checks, three
signed config templates, explicit local broker proposals, nine immutable launcher
installations/pointers and bootstrap register/activate/commit. Preparation requires
an actual primary SYSTEM host without thread impersonation, retained actor
impersonation tokens, a new locked journal, four absent services, empty config/
state/logs and no update.state. Signature/inventory admission precedes ACL writes.
Additional bytes accept only fixed component-owned destinations and must include
mosquitto\\mosquitto.conf; no permissive broker defaults or RPC-selected paths.

Apply is one attempt bound to the original journal/header/layout and retained
context. It rechecks signed inventory, ACLs and original/candidate config states;
activation/commit require explicit application probes and fresh communication
barriers. Abort must finish managed service stop/process-exit/delete before
reversing config transactions; successful commit forbids abort. Immutable release
and launcher binaries remain. A failed preparation can retain protected journal/
directories and admitted immutable release; this is not automatic repair.

The composition fixture uses real files, ACLs, journal, templates and launchers,
with modeled trust/SYSTEM/SCM/barriers. Public SYSTEM installation entry,
durable installer-context restart/resume and
actual signed SYSTEM/SCM/IoT install acceptance remain pending. Mode3/live update
stays disabled; helper and published1.13.2 are unchanged.

Fresh broker producer now renders the current active contract3 profile using an
explicit authenticated discovered SN: localhost1883 listener, localhost18883
leo4proxy bridge, fourteen exact QoS1 routes and ProgramData log path. It opens
no MQTT connection, enables no previously unused broker ACL directive and accepts
no raw custom template/network/port override. Existing trusted component-proposal
API remains internal; normal preparation uses setup_fresh_prepare_broker.

Fresh install also prepares a fixed native HKLM Environment Path transaction:
retained original existence/type/bytes -> journal80 plan ->81 intent -> registry
write/flush/exact readback ->82 done. It preserves placeholders and all other
entries, adds PF/bin once, and retains REG_SZ/REG_EXPAND_SZ. Abort requires managed
services gone before83 rollback intent/value restore/flush/readback/84 done and
then config rollback. Changed external values refuse; unknown write result permits
only exact original/candidate resolution. No machine PATH was changed by tests:
actual HKCU fixtures exercise registry I/O with fixed HKLM binding/SYSTEM modeled.
No cross-process PATH-plan reload or environment-change broadcast is implemented;
public host must supply final notification. Registry value writes have no CAS;
external writers are checked but not serialized by the suite deployment lock.
