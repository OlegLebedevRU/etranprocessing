# L4Update — архитектура и этапы реализации

## Текущий приоритет — решение владельца 2026-10-08

Совместимость допускает сборочный конвейер: обязательный gate новой связи со
старыми потребителями, затем один штатный переход и один принудительный откат
тем же исполнителем на стенде773. Отдельный шеститочечный измеритель удаляется;
побайтово одинаковый технический релиз не вводится как отдельная архитектура.
Первый локальный инженерный запуск использует подписанные X/Y без опубликованного
перехода; этот допуск недоступен через RPC. Реальный producer приёмки и локальный
entry ещё не готовы, поэтому RPC7031 закрыт. Ниже исторические checkpoints;
они не отменяют это решение и не доказывают E2E.


2026-10-07 admitted worker signed pre-stop: read-only SYSTEM ticket/receipt/parent/
Job/task/clear/WAIT recheck before/after NEW process-local signed operation64 gate.
Recovery bound to original supervisor epoch and first signed before/after/hash/size/
start; drift refuses. Capture retains signed switch, no redundant extra reload.
Native separate-child recheck + signed fixture composition; scheduler/SCM/channel
callbacks modeled. No marker/stop. Current helper is supervisor/window2 only;
communication rollback/watchdog absent, so live window1 entry remains disabled.

2026-10-07 journal handoff: SYSTEM parent flushes one bound ticket68 then closes
journal/deployment lock, keeps Job. Exact child polls existing journal (missing lock
never recreated), verifies self/parent epochs, Job/plan/clear generation/WAIT/task,
rechecks after audit and flushes receipt69. Verified history refuses replay/reissue.
Decision lock free during audit. Actual separate child/locks/Job/store fixtures;
scheduler modeled. Admission grants journal ownership only; signed pre-stop/typed
apply and actual worker payload/SYSTEM host/real tasks/live flow still pending.

2026-10-07 SYSTEM worker-start composition: common locked original journal/clear
state -> private Job/suspended child -> actual immutable plan -> task arm/audited
resume -> final Job/clear/WAIT check. Actual epoch replaces zero template; UUID/
operation64/codec/budgets/helper receipt/time checked before spawn. Existing plan
forbids retry with another PID. Failure kills only owned child, preserves persistent
plan/unknown task and original error vs cleanup error; no commit/clear/task delete.
Success returns retained Job+still-locked journal, not apply permission. Actual
worker admission/journal handoff/System host/RPC remain pending. Native fixtures
use real Job/worker/private journal/store with modeled scheduler; no live activation.

2026-10-07 producer worker Job: common new-only private original UUID Job,
SYSTEM/journal owner, kill-on-close/no breakaway/no inherited handles. Trusted
controller authenticates pinned worker/command/fresh env; locally creates suspended
child and assigns before execution. Held epoch binds recovery plan. Production
resume requires matching private plan + fresh task audit + Job/epoch recheck,
atomic one-caller election; no marker/SCM writes. Parent holds Job outside worker;
explicit cancel confirms child exit and zero processes. Existing helper worker
adapter actually kills native test worker+descendant; 80/0 x86/x64 each, task audit
modeled. SYSTEM controller/worker payload/real task bootstrap/live flow still pending.
Windows7 nesting failure refuses launch; helper binary/default rollout unchanged.

2026-10-07 recovery scheduler adapter: producer-only local SYSTEM COM arm/audit;
private original-UUID task, absolute rounded-up deadline + boot trigger, fixed helper
action and explicit recovery+overhead limit. No update/run/delete. Private SYS/BA
ACL, effective ServiceAccount principal, pinned helper hash, intent67, repeated
normalized XML/ACL readback; WAIT before/after, decision lock free during COM.
Unknown registration result fails; exact existing retry adds no intent. Overhead
min2s covers helper lock waits; measured startup reserve/OS bound still pending.
x86/x64 166 checks each, real read-only TaskDefinition normalization, modeled task
registration. No real task or service changes. Actual SYSTEM arm/boot/hard kill,
producer Job/bootstrap/controller remain pending; helper and release unchanged.

2026-10-07 minimal helper: tools/l4rollback standalone /MT x86/x64/default,
reader-only links no journal replay/write/installer/catalog/network/marker write.
SYSTEM canonical bootstrap path only; private runner serializes helpers across
release, then private UUID Job (kill-on-close/no breakaway) stop + observed exit,
deployment lock, pinned old EXE hash, fixed SCM/config restore, fresh supervisor
health/original+current epoch/exact marker recheck and terminal private result.
Untracked approved-path survivors waited for, never killed; reused PID untouched.
Original supervisor epoch added to initial unpublished plan. Real owned Job/files/
locks + modeled SCM tests; no deployed helper/tasks/bootstrap packaging/live gate.
Full outer barrier/marker clear remain controller-owned; default helper unchanged.

2026-10-07 supervisor recovery foundation: immutable private plan binds original
UUID/operation64 + live worker PID/creation, canonical supervisor paths/hashes,
config/ACL and explicit deadline/recovery budget. Independent reader uses only
private decision lock, no journal replay/deployment lock/catalog. COMMITTED vs
STARTED serialized; STARTED excludes commit; release lock before worker stop or
deployment lock, then reacquire for verified RESTORED/FAILED. Corrupt/foreign/
unsafe data refuses progress, FAILED blocks auto retries. Real files/locks/race
x86/x64 fixture gates; local helper executable now implemented; tasks/live restoration pending.
Live apply disabled, existing helper/release unchanged.

2026-10-07 pre-stop controller: capture_stop verifies operation64 + explicit clear
and saves original4 PID/creation epochs. Opaque process-local one-shot gate;
confirm CAS consumes even failure, binds same journal/owner/plan/next generation,
reloads signed plan/cache/config/SCM, invokes superv/con drain then fresh link and
rechecks locals. Never substitutes new PID or uses ordinary superv health in window1.
No stop/apply/marker clear or durable ready; outer watchdog/helper/candidate-port
and typed recovery remain mandatory, live/CLI disabled. Local fixture gates only.

2026-10-07 drain: existing SYS/BA-only health pipe v2 echoes exact original
operation/window/generation/plan/deadline; bounded state/PID/expiry checks.
Con admission fence covers dispatch/user events/FM parent lifecycle and waits
for async command cleanup, FM lease/work/results plus child quiet snapshot;
child hello/reconcile pauses under protected state, no new FM child during update.
Superv fence waits complete cycle; unconfirmed owned PIN termination blocks ACK.
Independent desk not acknowledged. Original SCM PID+creation must remain valid;
controller repeats state/source/config/actor and fresh link before any stop.
Local native gates only, no deployed703x/live apply/restart recovery evidence.

2026-10-07 persistent service admission: fixed protected operations/update.state,
112-byte LE/checksum, original UUID + operation64/generation/window/UTC deadline;
intent65 before atomic private publication under journal lock. CAS/foreign owner/
deadline checks; explicit terminal clear, no missing/expiry release or auto-repair.
Fresh bootstrap provisions before SCM creation. Installed con gates ordinary RPC/
FMC/user events but keeps results/link responses; supervisor gates startup ACL/
ordinary cycles and observes link read-only in window2. Portable isolated.
Drain acknowledgements/async tickets, helper/watchdog, complete controller/RPC,
fault/live773/Win7/compatibility and release still required; live apply remains off.

## Текущий native gate: каталог → релиз → дескриптор

`tools/l4setup/src/root_manifest.c` проверяет owner signature и закреплённый в
каталоге SHA256 до JSON. Точная версия, clean/signed provenance, издатель, обе
архитектуры, фиксированные имена/размеры/хеши обязательны. Root-bound descriptor
gate сверяет подпись, версию/архитектуру/издателя/ZIP и возвращает существующий
защищённый план подготовки; установки/SCM/HTTP внутри нет. Fixture signer не может
заменить compiled owner root. Native проверки x86/x64 — см. активный packet.
Registry acquisition API и durable route selection теперь подключены отдельно от
FM policy storage, обхода напрямую нет. `update_metadata.c` получает подпись/JSON,
допускает каталог по локальному UTC после загрузки, сохраняет exact catalog/входы
в record60 и получает pinned root/descriptor. Reload не меняет latest; production
acquisition отказывается от fixture trust и root другого каталога. Защищённый ZIP
cache и worker подготовки всех пакетов маршрута подключены: record61 сохраняет
exact signed root/descriptor + cache leaf каждого шага, record62 — завершение всех
пакетов. Offline reopen перепроверяет подписи/cache/full immutable inventory и
не выбирает latest заново. Готовность пакетов дополнена signed-source/composite
SCM/config plan и fresh-preflight wrapper ниже; live worker process/RPC/installer
и внешние проверки переключения остаются незакрытыми гейтами.

Pre-stop checking primitive: `l4_config_verify` сверяет exact старые байты/SD без
apply/rollback/intent; `setup_readiness_preflight` проверяет конфиги, actor ACL,
канонические команды/аккаунт/start type/running и PID+creation FILETIME четырёх
служб, application probes и свежий барьер, затем повторяет локальные проверки.
Истёкший бюджет/дрейф/отказ блокируют preflight; успех не журналируется как связь
после перезапуска. Это не разрешение stop: связь с signed installed-source receipt
и общим source/config/SCM планом описана ниже, live worker ещё не подключён.

Signed source/composite foundation now connected: record63 persists exact owner-
signed source root/descriptor pinned by original catalog, after signed inventory
verification; record64 binds package completion/config refs/all four switches per
hop. Offline reload recomputes and compares switches against verified source/targets
and original current SCM/config. `setup_update_operation_preflight` feeds this verified
source to fresh readiness; no cached barrier. LocalSystem AUTO/DEMAND profile only,
arguments preserved, no defaults or legacy migration. Temporary-port candidate
probe now loads this plan and pins its target EXE into a bounded owned child job;
actual PID-owned HTTP/MQTT listeners and ready certificate health are mandatory.
Dedicated proxy mode has exclusive loopback ports, no forwarding/MQTT CONNECT,
policy cache/poller, discovery/firewall/SCM/media or writable diagnostic paths.
Original RUNNING Leo4Proxy primary token now captured read-only and used for child
launch: LocalSystem/session0, exact source SCM/held creation epoch checks before
create and resume, exact child authentication identity, fresh non-inherited
environment, private thread privilege scopes restored before resume. Restore
failure terminates only own suspended child and worker, never returns privileged.
Certificate profile now derives effective last-wins email/thumbprint/store from
saved original SCM args, quotes a certificate-only probe command and checks explicit
thumb against owned health. Unknown/action/missing/non-ASCII/oversized options fail
closed; shared source/probe adapter tested roundtrip. No default fallback on missing
selector; original discovery precedence preserved. Network/media config coverage
still pending with quiescence/helper/watchdog/live worker/
apply. Temporary readiness is not full upstream/barrier evidence; no install enabled.

## Release foundation — этап 1

Добавлен [l4release](../../l4release/__main__.py): plan/prepare/release/verify,
keys init, один build config, checkpoints и подпись через существующий PS.
[Руководство](../../tools/release/README.md) и
[packet](../tasks/completed/2026-10-06-l4release-stage1.md) фиксируют проверки/ограничения.
Worker/layout/RPC/catalog ещё не реализованы. Full access устранил отказы OS gates;
native builds/tests и unsigned packaging PASS, diagnostic timestamped signing PASS.
OpenH264 vendor восстановлен и pinned. Signed candidate 1.13.2 опубликован:
19 EXE/оба payload проверены, anonymous full GET трёх файлов PASS.
Terminal gate/promote пока явно отклоняются.

## Windows layout — начало этапа 2

Логи l4capture и аварийный отчёт leo4proxy подключены к EXE runtime resolver:
installed release → ProgramData/logs/<component>, portable builds → EXE directory.
Proxy policy JSON, console workspace/FM journal и desk config/state/log paths
также адаптированы; новая установка и account-specific writable ACL ещё не готовы. Типы MQTT согласованы: l4con=extra_service, l4desk=svc_desk.

[Общий layout](../../tools/l4common/README.md) определяет системные корни,
versioned release/data paths, защищённое создание дерева и read-only SCM inventory.
`l4setup --layout-plan` показывает новую раскладку до elevation/install pipeline.
Native setup builds/default и gates x86/x64 PASS; layout 53/0, baseline 8/8.
JSON планы идентичны; independent SCM snapshots unchanged; стендовые аккаунты
четырёх служб LocalSystem. Это local tests, не installation E2E.
Новая live установка не включена: runtime consumers, account-specific writable ACL,
unpack/SCM/rollback и launchers остаются следующими частями этапа 2.
Supervisor/Mosquitto consumers теперь используют общий runtime resolver:
ProgramData config/state/logs для installed canonical release, EXE/path_match
остаются versioned. Installed binary-root redirect и legacy service autoregistration
отклоняются; installed log ACL не заменяется runtime repair. Config load refusal
останавливает orchestration. Con event75 читает package summary/state из ProgramData.
Desk и supervisor используют общий media state path; FM private journal стабилен
между версиями. Registry authority/record wire format proxy policy сохранены.
Новые path/refusal и Mosquitto template tests включены в build.cmd all x86/x64.
[Активный packet](../tasks/active/2026-10-06-l4layout-stage2.md).

## Роль и границы

Независимый SYSTEM worker обновления tools suite. Единственный MQTT-адаптер —
L4Con; backend UI вне scope. Публичный Registry, HTTP через Leo4Proxy.
Нет миграции C:\l4tools; bootstrap новой раскладки через l4setup.
MB/PB не меняются; IoT требует минимального допуска новых методов/events.

## Контракт и инварианты

- RPC7031(version=точная/latest, target=suite/updater) достаточен для всей операции.
- Operation ID = исходный 7031.task_id; 7032/7033 адресуют его параметром.
- 7031 завершается на durable accepted, не на факте установки.
- Обязательный барьер REQ/RSP + свежий EVT/EVA до изменения и после переключений.
- Сначала Leo4Proxy, затем Mosquitto, без одинаковых MQTT client ID одновременно.
- Два окна watchdog: связь и остальные; Mosquitto предел 5 минут.
- Новая связь + старые tools обязательно обратно совместимы, gate без waiver.
- Минимальный bootstrap rollback-helper только для supervisor, не обновляется
  штатно; изменение только по обоснованному решению владельца.
- Updater обновляется отдельно переключением указателя под страховкой L4Superv.
- Один тип EVT и один числовой тег со всем объектом; простой ограниченный outbox.
- Release dirs/явные SCM пути в Program Files, data/config/logs в ProgramData.
- Аккаунты служб сохраняются; ACL проверяются от их имени до остановки.
- Один встроенный RSA-3072 ключ, SHA256/PKCS1v1.5, detached signatures;
  ревизия/expiry каталога, без root.json и авто-ротации.
- Проверенные пары → прямой переход/кратчайшая цепочка. Cleanup ≤X защищает
  активные компоненты, резерв, незавершённые операции и pinned версии.

## Проверки и flow

Проверка комплекта без остановки → один реальный переход. Fault evidence
переиспользуется только при неизменных соответствующих входах/профиле.
Suite: подготовка → связь → остальные → supervisor → итог.
При timeout один откат; нет внешних ответов на старой связке → connectivity_unconfirmed.
Частично успешное применение отражает фактические версии, не ложную полную suite.

## Конвейер и стенд

Целевой uv Python release/sign/setup/publish/gate/promote с checkpoints.
sw_sign.env игнорируется Git, секреты не попадают в artefacts/logs.
Основной стенд 773/tenant1 — текущая машина, оператор может вмешаться.
Другой setup-стенд не назначен и не блокирует remote gate.

## Источники и актуальность

- [Авторитетная архитектура](../../docs/term_arch-l4update-flow.md).
- [Handoff](../tasks/completed/2026-10-06-l4update-architecture.md).
- Требование: диалог владельца, 2026-10-06; код l4update не написан.
- Статический IoT аудит: HEAD 2ffa1403, REQ completed→NOP/zero UUID,
  EVT/EVA collector; production не проверен.
- Открыто: event code/tag/API schemas, полный path/account audit, budgets,
  минимальный rollback формат/синхронизация и release module placement.
- Runtime/native build/API/MQTT/deploy evidence по новой архитектуре отсутствует.


Дополнение native foundation (2026-10-06): l4common/access захватывает реальные
RUNNING service tokens с account/PID/ImagePath recheck; offline ACL prepare отделён
от pre-stop verify без ACL repair. Config read и writable directory I/O probes,
закрытый private FM journal, собственный owner-capable service SID receipts.
Это ещё не подключённый deployment gate. Реальный desktop token/new installed file
ACL, non-SYSTEM profiles, immutable unpack/SCM rollback остаются открыты.
Layout-plan обеих архитектур подтвердил токены четырёх LocalSystem служб без изменений
SCM. Начальный Stop Pending L4Superv исчез к финальному read-only snapshot без вмешательства
агента. Полный live suite/IoT barrier в этом этапе не выполнялся.


2026-10-06 deployment primitives: common release module проверяет archive SHA
до ZIP parse, per-file size/SHA/immutable ACL и публикует уникальный staging dir
через rename; existing version только verify/reuse. Общий layout больше не создаёт
пустую версию. Service switch планирует явные versioned EXE и сохраняет digest
резерва, apply/rollback требуют STOPPED и expected account/path/start type;
ChangeServiceConfig меняет только ImagePath. Legacy source rejected, new CLI install
disabled. SCM mutations проверены только mock; live connection/power-loss recovery,
полный authenticated inventory adapter, bootstrap/journal/config/launchers открыты.


2026-10-06 journal/config primitives: private SYS/BA operation journal + exclusive
layout deployment lock; UUID-bound v1 framed SHA chain/sequence/end marker, flush,
incomplete-tail truncation, complete corruption refusal before replay callbacks.
SCM plan UTF-8/LE serialization roots-bound, no SCM action. Per-file config plan
stores old/new bytes + security descriptor; intent → flush/replace/readback → done,
idempotent recovery after missing done, operator conflict refusal, new-file rollback
delete. Caller must quiesce writers; multi-file/SCM phase machine absent. Linked
setup only; native NTFS tests do not establish power-loss/IoT E2E. Bootstrap,
launchers and full authenticated inventory adapter remain pending.


2026-10-06 bootstrap/launcher foundation: read-only clean-install preflight of four
explicit profiles/absent services; existing/error refuse, LocalSystem-only fresh
profile currently verified. Common immutable PF/bin launcher install journals intent/
done and reuses identical files, refuses overwrite. Per-tool bounded version/size/hash
pointer under config/launchers uses config transaction. Protected owner/ACL parents,
fixed tool/component mapping, pinned EXE/ancestors during child, explicit application
path; native real stdio/UTF-16/CWD/exit code and restricted token tests. Template added
to uv build/staging/signing inventory; only isolated unsigned packaging performed.
No live installation/SCM/PATH/MQTT changes. CreateService/rollback/start/orchestration,
signed full inventory adapter, desktop/Win7 and IoT gates still pending.


2026-10-06 registration/recovery foundation: bootstrap plan stores EXE hashes/sizes
and final requested start types in bounded UTF-8/LE; save rechecks absence under
journal lock. CreateService atomically includes UUID+plan owner marker, creates
stopped provisional LocalSystem/DEMAND_START services. Exact fingerprint retry;
reverse owned STOPPED-only deletion with actual-absence wait/aggregate poll budget.
Rollback-begin prevents re-registration, rollback-done terminal. Real journal/reopen/
file pin tests; all SCM mutations modeled. Linked setup only, install disabled.
Service start/stop/process-exit/barriers, AUTO_START commit, PATH and signed full
inventory admission not connected; concurrent privileged SCM actions not atomic.

2026-10-06 activation/readiness foundation: ordered fresh-install activation of
provisional registered services, flushed start intent, proxy -> broker(5min) ->
console -> barrier -> supervisor -> final barrier. Holds verified EXEs and running
process handles/image/SYSTEM-token/PIDs; changes/death refuse progression. Mandatory
callback probes/barriers repeat on resume; historical READY never replaces them.
SCM/process/callback evidence modeled, protected journal/hash/pinning real. Not a
remote-update switch or live install. Actual IoT/application adapters, managed stop/
process-exit recovery, quiescence/update-only, AUTO_START/PATH/signed inventory open.
Failure leaves manual services; delete-only rollback still refuses RUNNING services.

2026-10-06 managed abort foundation: observed PID/creation FILETIME journaled before
readiness; rollback direction -> reverse ordered stop -> process exit + STOPPED ->
delete. Single absolute stop/delete deadline; supervisor failure blocks lower stops.
Deletion refuses started services without STOP_DONE. Unknown stop/missing done
resumable; PID reuse does not control replacement, access denial is not absence.
Changed running identity or unobserved started STOPPED fails closed for owner recovery.
SCM/process lifecycle modeled, journal/hash/reopen real. No live mutation/helper or
backend changes. Application/IoT adapters, AUTO_START/PATH/signed admission pending.


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


2026-10-06 signed admission: setup native prepare/verify wrappers require trusted
archive/file inventory and exact publisher certificate SHA256. Common checked
prepare validates every hash, invokes mandatory admission with pinned file/parents
before publish, repeats on existing versions; timestamped Authenticode + publisher
+ revocation gates for EXE/DLL/SYS/MZ. Data remains hash-covered. New manifest emits
publisher_certificate_sha256 and signed verifier checks the same certificate.
Actual unsigned PE rejection and existing signed1.13.2 trust/timestamp verified;
provider failures and protected publication/reuse modeled/native file tests.
No install/publication/runtime service mutation. Trusted manifest acquisition,
full inventory/layout mapping/config separation and CLI commit are still required.


2026-10-06 complete inventory/layout: uv producer emits deterministic per-arch
layout ZIP + schema1 descriptor and four extra root-manifest assets/checkpoints.
Publisher validates fixed names/hashes/sizes/version/arch/publisher and sends root
metadata last. Native setup authenticates descriptor digest before bounded parsing,
requires ten EXEs/three templates and feeds signed admission. Configuration mapping
is read-only: immutable templates -> three ProgramData config destinations.
Python52 PASS; native manifest86/0 each arch; full setup x86/x64 build PASS.
Trusted metadata acquisition, journaled template application and worker/commit
remain pending. Installation disabled; SCM and published1.13.2 unchanged.


2026-10-06 detached metadata signatures: Python exact-byte RSA3072/exponent65537/
PKCS1v1.5/SHA256 producer + common native CNG verification (raw384-byte signature,
411-byte trusted public blob, bounded65535-byte document). Setup signed-descriptor
wrapper verifies before JSON/path parsing. Python58 PASS; actual Python/CNG
signature integration76/0 each arch, including tampering/wrong key/PSS/SHA512.
Owner key not used/created/rotated. Root embedding, root/catalog freshness,
pipeline signing/publication wiring and acquisition remain open; no installation.


2026-10-06 metadata integration: initial owner encrypted RSA key/public companion
provisioned externally after user requested help obtaining the key; env parameters
configured, private access owner/SYS/BA. Public trust root embedded in common native
code; production metadata/descriptor verification has no downloaded-key override.
Pipeline signs2 layout descriptors + final root, pins key ID, snapshots3 signatures;
metadata failure preserves signed stage for retry. Publisher validates local public
trust/crypto/inventory and uploads10 fixed assets with root last; unsigned layout
publication refused. Python71 PASS; native83 checks per arch targeted (see handoff).
No release candidate published or installation enabled. Native root/catalog parsing,
expiry/revision persistence, acquisition and journaled install remain open.


2026-10-07 package cache: bounded streamed ZIP uses unique CREATE_NEW/private
SYS/BA file, pinned canonical ancestors, signed size/SHA after flush and again
on read-only reopen; ADS/hardlink/reparse/insecure ACL refuse. Returned object
holds file against write/delete through signed full inventory admission. Partial
downloads delete only their own held object; crashes leave unreferenced quarantine,
never auto-adopted. setup prepare-package connects pinned descriptor/root/ZIP to
actual Authenticode/publisher/timestamp checks. Complete prepared operation plan,
signed-document persistence, account/config/SCM gates and worker still pending.

2026-10-06 catalog schema/floor: Python exact-byte catalog producer/validator +
native signature-first parser (embedded owner production root), explicit/latest
resolver, BFS24 releases/24 directed edges by arch/profile with root/evidence digests.
Expiry/future/stable/revocation/duplicate/shape/bounds fail closed. Global protected
state/catalog.floor appends chained revision/time/catalog digest under deployment
journal lock; survives different operation IDs/reopen, refuses rollback/equivocation/
clock rollback and corrupt/torn/unsafe files. Local recovery stays independent.
Fixture catalogs ephemeral-signed, no owner promotion/publication. Native root
manifest acquisition/parser, scoped773 admission/evidence, worker/CLI remain open.


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

### Native communication executor and independent monitor (2026-10-07)

`communication_runtime` now composes original SYSTEM supervisor epoch, pinned
immutable plan, durable STARTED/runner, original UUID Job exit and existing-only
journal acquisition. Decision mutex is released before Job/journal work. Journal
operation64 digest and exact record10/20 copies are rechecked before SCM actions;
old image hashes and ancestors remain pinned throughout. Fixed native SCM stop
observes STOPPED plus process exit, waits approved orphan survivors without killing
enumerated PIDs, refuses account/start/path/PID drift. Recovery restores proxy
before broker, including both broker config/ACL snapshots, and retains exact
running process handles/epochs around mandatory local probes/final barrier.
Signals are mandatory trusted local callbacks, not a second MQTT client.

Native total budget additionally includes two verify_ms reserves for initial
plan/claim and terminal publication. Setup producer now requires this stronger
budget; core policy-only budgets retain their earlier meaning. Runtime deducts
elapsed acquisition time and final reserve from core execution, detects late
returns and leaves marker active on every result. No retry/automatic clear.

`communication_monitor` is an independent Win32 thread bound to original
supervisor epoch and one immutable deadline, observing only explicit generation-1
clear then exact active window1. Never extends deadline or falls back to ordinary
supervisor tick. Cancellation before claim acts locally; after claim close waits
for executor and retains ownership on timeout. UTC rollback does not extend the
monotonic cap; it refuses with time-skew evidence. No public boot boolean.

Supervisor now registers communication_watch independently of ordinary cycles
and a read-only mode3 handler. It discovers one eligible private original-epoch
plan before active publication; ambiguity, foreign generation/epoch, corrupt state
or missing original Job refuse. Completed ownership stays for this supervisor
lifetime. Queries never arm/renew/create anything and ALWAYS refuse readiness
until protected boot admission exists.

communication_signals pins ports from original proxy command and saved broker
bytes, source-version LocalSystem Con PID/creation/handle, captured SN/thumbprint
and exact loopback listener owner PIDs. Supported profile: plain loopback HTTP,
one loopback broker listener/bridge; SSL/include/multiple-listener profiles refuse.
Raw TCP sends zero application bytes, never MQTT CONNECT. Proxy channels do not
depend on broker/IoT; final barrier delegates fresh REQ/RSP+EVT/EVA to Con mode1
with exact Con epoch before/after. Factory mode0 is local readiness, not that
fresh barrier. Factory does not independently verify signed Con inventory:
producer signed-source/all-hop/prestop original-epoch proof remains required.

Protected boot/new-supervisor admission, actual SYSTEM/SCM/773 fault acceptance
and full worker transitions remain required before live entry. Synchronous WinAPI
cannot be preempted; late return detection is not an OS execution-bound guarantee.
Helper unchanged. Native adapter tests use real loopback/PID ownership; SCM,
Con IPC and SYSTEM are modeled. Manager tests use real files/decisions/thread,
with modeled signal/executor dependencies.

### Protected restart admission and native continuation (2026-10-07)

communication_boot creates an opaque native permit: exact active window1 and
matching protected supervisor/communication UUID/operation64/worker+supervisor
epochs, original supervisor actually gone, new SCM LocalSystem own-process
supervisor serving the exact pinned old command/hash. RUNNING or START_PENDING
must report this process PID; process creation/image/token and marker are rechecked.
It holds the existing supervisor runner lock, keeping frozen helper execution out,
and releases the short supervisor decision mutex before runner/image/SCM work.
No helper decision mutation, worker termination or journal replay in admission.

execute_boot accepts only this native permit, not boot=true from RPC. Constructor
and claim share initial verify_ms; constructor entry anchors the unchanged total.
One invocation per restarted supervisor process, including expired claim reserve
or failed result publication; no production reset/reopen bypass. Existing STARTED
continues only with the old communication runner OS lock gone. Terminal decisions
never retry. Original fixed old-pair/config recovery, process pins, mandatory signals,
durable result and active marker retention remain unchanged.

This guarded entry is compiled but NOT registered in the startup manager. Current
signal factory requires healthy original pair/Con and cannot be reused after an
interruption blindly. Startup signal acquisition from authenticated original
metadata, exact Con source verification and actual SCM/773 boot acceptance remain
required; mode3/live entry STILL refuse. Fixture execution under real LocalSystem
exercises real tokens/processes/files/runner/decision; SCM/images/signals are modeled.

Producer-only recovery_store private_create now unwinds scoped owner tokens even
when previous=NULL. Frozen L4_RECOVERY_READER_ONLY compilation follows the existing
branch; helper code behavior/build/binary is not changed. Runtime fixture creates
barrier events before starting its monitor, with explicit larger fixture-only
timing margin for real NTFS/process scheduling; no production budget defaults.


### Interrupted-link startup registration (2026-10-07)

The same immutable communication plan now uses L4COM02 and includes the exact
first-hop source Con record10/hash/size, original PID/creation and the selected
old proxy certificate thumbprint. Producer reloads signed source/all-hop operation64,
captures certificate through original proxy epoch and owned loopback listeners,
and copies the Con record referenced by operation64. Missing/drifting identity
refuses publication. No additional persistent sidecar or external RPC contract.
Prior experimental L4COM01 plans refuse; this foundation has not been released
or installed, and no migration/compatibility path is introduced.

The shared original-command port parser is used by producer and native signal
adapter. Normal acquisition additionally pins signed-source Con EXE/hash and
requires its original epoch. Guarded boot acquisition pins that same old EXE,
SCM command/account/start mode, held running SYSTEM Con epoch and saved certificate;
it makes no initial proxy/broker/Con-IPC/network probe. If Con has restarted, it
accepts a new epoch only after proving the original is gone and checking the
same fixed old image/hash/SCM. It never starts/kills Con or creates MQTT CONNECT.
Con must already be RUNNING locally; missing/pending/foreign Con refuses acquisition.
After acquisition its epoch stays fixed through the fresh final REQ/RSP+EVT/EVA.

The independent supervisor owner thread now takes one startup snapshot of the
explicit active operation. Original owner continues the deadline monitor; restarted
owner uses opaque native permit -> interrupted-link signal acquisition -> guarded
synchronous old-pair recovery. Failure/completion suppresses re-admission for this
manager lifetime. STOP waits for actual executor exit before releasing profile,
permit, Con image/process and runner exclusions; timed-out close retains ownership.
Admission and signal acquisition share the original verify reserve; admission also
requires enough time for its entire recovery budget before immutable supervisor
deadline, and owner checks refuse after that deadline. No timer renewal, marker
clear or positive mode3/live-stop admission.

Native fixtures exercise actual protected files, original process exit/epochs,
loopback/PID/zero-byte transport and owner-thread retention; SCM, boot image
resolution and application barrier are modeled. Full Windows boot/SCM ordering,
actual failure recovery on773 and typed forward/apply/window transitions remain
required before live update readiness. Helper and published1.13.2 remain frozen.


### Real isolated SCM acceptance (2026-10-07)

Added an explicit LocalSystem-only SCM fixture using uniquely named temporary
services and private source/target mosquitto.exe test images. It includes the
unchanged production communication_runtime adapter: SCM calls, service state,
account/config, primary process token, PID/creation, orphan enumeration and
process exit are real, with no substituted API/status responses. Normal native
build only compiles the fixture and prints help; --system-scm is explicit and
refuses ordinary owner tokens before registration.

Checks cover healthy start/stop, approved surviving source process wait without
killing/adopting it, START_PENDING delay, STOPPED before actual host-process exit,
stop/start timeout, failed service start, account/start type/ImagePath drift
refused before start, and cached process epoch mismatch. Cleanup uses only the
CREATE_NEW service handle and exact private command, confirms process/service
exit and SCM absence, then deletes the owned tree. On failure it retains files
for reconciliation instead of claiming cleanup.

This tests actual native SCM adapter mechanics on the default local773 stand.
It does not install/repoint the four production services, modify helper or
published1.13.2, reboot Windows, or establish actual leo4proxy/Mosquitto/Con/IoT
barriers. Metadata signatures and full guarded recovery/controller flow are not
accepted by this fixture; mode3/live readiness remains closed. Pending/missing
Con startup ordering and full production-name failure acceptance remain required.


### Bounded automatic Con startup (2026-10-07)

Guarded boot signal acquisition now waits for the exact saved automatic old Con
in STOPPED (without failure code) or START_PENDING to reach RUNNING. Each poll
revalidates SCM own-process/account/start type/command and native boot admission;
missing, failed, stopping or drifting services refuse. It never starts/stops Con,
performs IPC/network preflight or creates MQTT clients while waiting. The existing
absolute initial verify reserve covers all waiting and subsequent epoch/image
checks; there is no timeout renewal or second attempt. Normal acquisition still
requires RUNNING immediately. After acquisition, the held same-source Con epoch
remains fixed through the fresh final REQ/RSP+EVT/EVA barrier.

Native fixtures model SCM ordering and exercise real bounded waiting; this does
not establish actual Windows reboot order or full guarded recovery/IoT acceptance.
Mode3/live update remains closed; helper and published1.13.2 remain unchanged.


### Installed stand IPC prerequisites (2026-10-07)

`tools/l4superv/build.cmd stand-probe` builds x86/x64/default
`probe_installed_communication.exe` under tests; normal all build only runs help.
Explicit `--live-preflight` observes installed L4Con/L4Superv via query-only SCM,
held primary SYSTEM process/image/creation and exact command/start type. It checks
existing local mode0 health, then Con mode1 fresh REQ/RSP+EVT/EVA only if both
local checks pass; process/config pins are rechecked around calls. Output JSON is
observational IPC prerequisites only, never signed source/update admission.
Missing health transport or changing epoch refuses; no new MQTT connection,
service start/stop, active marker, recovery task or automatic failure injection.
All observations share a fixed60s window; mode0 local health is capped at5s per
component so a missing endpoint does not consume the other observation. The
fresh barrier uses remaining time, not a durable readiness lease. Exit0
only means this observed prerequisite succeeded; live_update_enabled is always
false and mode3/controller/full guarded recovery acceptance is still separate.


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

`tools/l4setup/src/fresh_install.*` now composes production root/descriptor/full
archive admission before offline ACLs, signed config templates and explicit
component proposals, nine immutable launchers/pointers, then bootstrap register/
activate/commit with caller-owned fresh probes/barrier. Only primary SYSTEM,
original new locked journal and absent services/config/state/logs/update.state
are admitted; retained actors must be impersonation tokens. Apply is one attempt;
managed abort must remove owned services/processes before config rollback and
cannot undo successful commit. Immutable binaries remain. No legacy adoption.

Fixtures exercise real files/ACL/journal/config/launchers with modeled admission,
SYSTEM, SCM and signals. Public SYSTEM host/entry, durable
context restart/resume and actual signed install/IoT/reboot acceptance remain
open. Broker renderer and registry PATH preparation/apply are now connected;
cross-process context reload and final environment notification remain open.
Future release supervisor template omits legacy base_path, allowing the
installed executable to derive its pinned layout. Existing published1.13.2 and
frozen helper are unchanged; public installation and live update remain disabled.

Broker rendering uses authenticated discovered SN, fixed current contract3
loopback listener/bridge and fourteen routes with ProgramData logs; no new client,
custom template or activation of unused ACL directives. PATH writes only fixed
native HKLM Environment Path: original/candidate type/bytes in journal80, intent81,
flushed write/exact readback82, rollback83/84 only after managed services gone.
Preserves unrelated entries/placeholders; exact drift refuses restoration.
Native HKCU fixtures model HKLM binding/System but perform real registry/journal
I/O. No system PATH writes. Windows value CAS and external-writer serialization
are unavailable; public notification/durable context replay/live acceptance pending.


### Explicit fresh entry preparation (2026-10-07)

Owner boundary is readiness before actual installation; four live terminal773
services must remain unchanged. install_entry/install_bundle bind a current
owner-signed offline kit and exact signed installer to protected original inputs
and a fingerprinted temporary SYSTEM host. Verify prepares immutable files,
checks LocalMachine certificate/operator token, never suite SCM/PATH. Fresh install
requires all four services absent. Abort-only reload uses receipt85/original root;
PATH reconstructs exact original/candidate bytes and unresolved intent outcomes.
No forward replay/new-epoch adoption; completed commit forbids abort. A crash
before receipt or changed reboot epochs requires explicit owner repair.

Service-local broker Environment leaves global MOSQUITTO_DIR unchanged. Its
isolated SCM fixture proves SYSTEM inheritance, with journal admission modeled.
uv install-kit signs a seven-day exact-version authorization with no stable or
transition edges and refuses overwrite. A separate clean local source checkpoint
is used for candidate signing. Actual install/IoT/reboot acceptance remains open.


Ready first-install candidate1.13.5: clean source9c8adeb, complete locked x86/x64
build/sign/integrity and public SYSTEM verify exit0. [Readiness handoff](../tasks/active/2026-10-07-l4setup-installation-readiness.md)
records exact kit/hash/authorization and what remains unproven. The stand's four
old services/PIDs, PATH,237 tasks/helper/published1.13.2 remain unchanged. Before
real installation, retire the four old services/processes and eight reviewed old
PATH entries with private exact backup; preserve old directory for manual recovery.
Forward7031/mode3/stable/mixed/reboot acceptance must not be inferred from readiness.

Latest owner decision: no legacy files/configuration archive or migration. A
separate manual UAC wrapper now prepares the cold transition, holding the pinned
kit, verify-before-stop, exact original SCM/image/process epochs, bounded orderly
retirement, reviewed PATH cleanup and unchanged fresh-install; deletes C:\l4tools
only after native success, with link/process refusal. Guard/refusal tests and
actual read-only plan passed; real UAC/cutover/cleanup remain unexecuted.
[Operator transition handoff](../tasks/active/2026-10-07-l4setup-operator-transition.md).

Actual operator transition1.13.5 failed at first supervisor activation: empty
supervisor identity regenerated broker config/restarted broker+Con, invalidating
bootstrap epochs; original commit/abort incomplete. Three new communication
services remain manual/running, supervisor stopped. First identity activation
now preserves a valid already prepared store/proxy/config/SCM link; known identity
changes retain restart behavior. Signed local1.13.6 and actual SYSTEM verify pass.
Explicit UAC operator repair is prepared (original broker bytes/native SD,
original native abort, reviewed failed-runtime diagnostics, then new clean
install); not executed by the agent. See [incident handoff](../tasks/active/2026-10-07-fresh-install-epoch-fix.md).

Operator repair has now stopped/deleted all four services. Original bootstrap
abort finished, but configuration rollback failed error5 at the broker directory
ACL fence; bootstrap aborted=true does not prove full fresh cleanup. A separately
pinned ResumeAbort operator stage/read-only plan is prepared; actual ACL repair,
remaining native cleanup and installation1.13.6 still pending. See incident handoff.

Latest operator run completed original native recovery/error0 and removed all13
prepared configs, then whole-directory diagnostic move failed access denied.
Explicit ResumeAfterAbort read-only plan passed: all services/processes absent,
six residual files, private empty destination. File-only diagnostic retention
replaces tree rename; actual install1.13.6 still pending. See incident handoff.

Fourth operator run retained four diagnostic files, then denied SYSTEM-only
mosquitto.log. Agent matched its actual ACL, restored standard Administrators
access, retained both logs with unchanged hashes and protected all six files.
Runtime roots now empty; actual completed-retention inventory pin/admission and
wrong-pin refusal passed. Next operator launcher needs no further file move;
native1.13.6 install/communication/reboot acceptance still pending.

First clean1.13.6 attempt602aa123 failed183 before receipt (empty journal/no
services): native fresh admission rejects even empty runtime subdirectories.
Wrapper now removes only verified empty descendants after original recovery,
leaving roots/private diagnostics intact. WinPS refusal/empty-root fixture and
actual read-only admission pass; next real installation still pending.

Second1.13.6 pre-receipt183 was fixed update.state from original bootstrap,
not empty folders. Exact initial idle bytes/checksum/original journal65 proven;
agent retained marker privately in original operation with all services absent.
Fresh data preflight now checks both native predicates. Recorded config undo
completion replaces redundant completed abort replay. Actual admission and
marker/empty-directory refusal fixtures passed; actual install still pending.

Next1.13.6 attempt e75a45d0 reached receipt16 but rejected existing1.13.5 PF/bin
launcher hash (error23), then completed automatic abort/config undo. All nine
signed old launcher copies were independently admitted and privately retained by
agent, fresh bin/runtime roots cleared without altering releases/helper. Operator
preflight now rejects mismatched/unknown existing launchers; fixture and actual
read-only plan pass. Actual install1.13.6/communication/reboot remain pending.

Latest native installation54c027a1-b7b7-41f0-a9c3-abde8b32d2a4 is committed=true,
receipt16; SCM confirms four1.13.6 RUNNING/AUTO services. Operator confirms MB
console/control/FM. Reboot/fault/forward7031 acceptance remains unproven.
Authorized workspace cleanup removed2204 ignored artifacts/520.7MiB, preserving
759 tracked/non-ignored source files by pre/post hash verification, final release
assets/dependencies/frozen helper and installed runtime. Repeatable cleanup script
shares release workspace lock, defaults to preview and records its removal report.

2026-10-08 remote integration delta (source/local tests, not remote E2E): owner
accepted orphan `iot_probe=1` REQ/RSP + fresh EVT/EVA with distinct N/M nonces on
existing topics, no IoT task/event writes, billing or webhook. Native Con uses
existing client only; ordinary NOP/task responses no longer satisfy barrier.
IoT source adds polling7030–33/7021/23 and event76/tag449 without billing; not
deployed. Result codec is implemented, publisher/controller wiring remains.
Production readiness now verifies real proxy identity/listeners and known valid
policy grant; isolated candidate semantics remain distinct.
Fresh local deployment has separate90/91 terminal records and no terminal-cert
or IoT-channel requirement. Offline signing revocation policy is separately scoped
and verified (cache-only local, exact unknown-revocation fallback); remote admission remains strict. Mosquitto log reconciliation
and standard-only config file read policy implemented; custom config remains
private. Current live log was repaired without service changes and restricted
token read/no-write verified. Current standard broker config also verified by
exact renderer hash and granted Users file-only read; write/delete/DAC/owner
remain denied. Rotation/FM acceptance still pending candidate.
See [remote/ACL packet](../tasks/active/2026-10-08-l4update-remote-and-acl.md),
[policy](../tasks/active/2026-10-08-l4update-policy-gate.md),
[fresh local](../tasks/active/2026-10-08-fresh-offline-deployment.md),
[ACL](../tasks/active/2026-10-08-mosquitto-diagnostics-acl.md).

Independent SYSTEM host foundation is now built/tested: durable92, authenticated
source/self, ACK93 and independent SCM/process proof. Main engine remains disabled
until actual forward execution/result delivery are connected. Actual SYSTEM source
discovery and repeat verification passed x86/x64 against installed1.13.6 after a
single known runtime MFT cache was reversibly quarantined. Strict signed inventory
was not relaxed. New l4capture uses optional ProgramData/state/l4capture cache;
desktop-SID leaf is scoped separately, with no Users write. Current old capture
can recreate the PF extra until replaced; new leaf provisioning is a baseline
prerequisite. Real isolated SCM crash-profile acceptance passed97/0 each arch;
complete watchdog/remote fault acceptance remains open. Final setup/con/supervisor/
capture unified builds passed, setup full gates x86/x64 and ACL69/0 each passed.
IoT source51 targeted tests pass; changes remain undeployed. See
[host](../tasks/active/2026-10-08-remote-host-foundation.md),
[source](../tasks/active/2026-10-08-installed-source-discovery.md),
[watch](../tasks/active/2026-10-08-l4update-watch-proof.md).

Remote controller preparation now composes original92/93 with immutable route60
and all-hop package61/62 pins before service changes. Latest is resolved once;
retry uses the saved route. Production profile comes from native OS facts only:
windows-nt-10.0.19045-x64-client on this stand for both binary architectures,
with bundle architecture separate. Missing exact owner-signed profile evidence
refuses; fixture aliases do not authorize production. One preparation deadline
and cooperative cancellation include late checks after durable completion.
Setup unified build and focused preparation171/metadata1225/host104 checks
passed on both architectures. Main engine remains NULL; no installed binaries,
Registry metadata or IoT deployment changed. See
[preparation](../tasks/active/2026-10-08-remote-controller-preparation.md).

Next two implementation steps: owned remote worker-plan snapshot wraps signed
operation64 planning, without retaining borrowed source/journal pointers or
granting stop permission. Pre-worker failure/cancellation now has durable94,
original92/93 and optional trusted60 binding, exact immutable retry and a pure
Con76/449 adapter. Result218 checks/0 per arch; Con unified build passed. Delivery,
post-worker success authority and actual forward apply remain disconnected.
See [result](../tasks/active/2026-10-08-remote-preparation-result.md).
Final combined setup unified build passed after both steps; worker-plan392/0,
preparation174/0 and result218/0 per architecture. See
[worker plan](../tasks/active/2026-10-08-remote-worker-plan-composition.md).

Next two steps: supervisor recovery template now binds signed64/member config20
and exact live SCM/PID/birth/image/SYSTEM/session0 epoch, keeping worker epoch zero
until actual startup. It grants no stop/arm permission;348/0 per architecture.
Live status reader validates original92/93/trusted60/94 without deployment lock or
live codec view. Con can form76/449 only from a bound terminal result; no send yet.
Status209/0 and real signed catalog271/0 per arch; native isolated SYSTEM reader14/0
per arch, modeled outcome producer. Combined setup and Con unified builds passed.
At that checkpoint worker entry/bootstrap helper receipt were absent; main engine NULL.
See [template](../tasks/active/2026-10-08-supervisor-template-prerequisite.md) and
[status](../tasks/active/2026-10-08-remote-live-status.md).

Worker entry now exists as a fixed native source-setup CLI. Technical admission69,
signed64 source/route/supervisor binding, original parent93 PID/birth/installer hash,
held source image, strict publisher signature and repeat admission precede any
future forward executor. NULL/incomplete executor refuses before69; main remains
NULL. Worker-entry361/0 per architecture uses actual isolated files/ACL/CNG/journal
with admission/SCM/signature modeled; it is not actual worker/update acceptance.
See [worker entry](../tasks/active/2026-10-08-remote-worker-entry.md).

Immutable helper bootstrap now has separate owner-signed metadata bound to the
fresh kit root; existing root/catalog schemas remain unchanged. Explicit first
seal signs a copy of approved frozen native bits outside the repository, ordinary
kits reuse the exact signed bytes, and native SYSTEM install creates a protected
PF/recovery helper/receipt without replacement. Strict remote receipt loading
retains file/ancestor pins; the accepted local offline signature policy applies
only to fresh deployment. SYSTEM fresh-verify also checks any existing helper
before the operator stops old services: genuine absence is allowed, partial,
unsafe or mismatched state refuses without creating/repairing files.
Initial real signed seal and SYSTEM installation have
not occurred. Frozen helper source/bin identities remain unchanged. See
[helper bootstrap](../tasks/active/2026-10-08-immutable-helper-bootstrap.md).
Final combined native build clean x86/x64/default=x86; helper113/0 and
worker-entry361/0 each, real child handoff202/0 each. Python80 passed, Ruff/Pyright
clean. Initial full x64 metadata fixture hit a1s timeout under load; isolated
unchanged binary passed, named fixture-only semantic budget10s now passes1225/0
each and remaining x64 gates pass. Production budgets unchanged. Evidence:
tools/dist/.release/evidence/worker-bootstrap-20261008; modeled SYSTEM/signature
provider/SCM/network boundaries are explicit. Actual signed baseline/install,
worker/recovery/apply and bounded event publication remain pending.

Next two source steps completed: worker entry holds a strict immutable helper
receipt through execution and binds it to the task-audited68 identity twice;
entry526/0 each. Controller launch composes fixed source-derived argv/cwd and a
native-facts environment without inherited credentials, fresh source/self/ACK93,
owned Job/task startup, communication70 and transfer68. One shared compiled
executor getter controls child and controller; it returns NULL, so no production
Job/task starts. No borrowed source/journal is used after successful transfer.
Post-start proof binds exact66/67 and optional70 before transfer; template1085/0,
actual communication storage2514/0, actual child handoff202/0 and modeled launch
composition197/0 each. Unified setup build/default=x86 passed. Evidence:
tools/dist/.release/evidence/worker-launch-20261008. Frozen helper/source and live
services unchanged. Report94 cannot terminate a started operation containing66;
post-start failure/success authority and real forward executor remain pending.
See [helper binding](../tasks/active/2026-10-08-worker-helper-binding.md) and
[controller launch](../tasks/active/2026-10-08-controller-worker-launch.md).
