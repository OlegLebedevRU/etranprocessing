# Remote update, IoT probe and runtime ACL

## Task intake
- Цель: подключение remote l4update; исправление чтения mosquitto.log; аудит ограниченных изменений IoT.
- Scope: tools/l4con, tools/l4setup, tools/l4common, tools/l4superv; внешний iot-rpc-rest-app. MB/PB и legacy вне scope.
- Владельцы: tools управляет локальной операцией; IoT управляет RPC transport и event persistence.
- Producer → transport → consumer: IoT RPC7031 → существующий MQTT → l4con → отдельный SYSTEM updater; результат → EVT/EVA → IoT.
- Инварианты: original7031.task_id = operation_id; один MQTT client ID; remote pre/final barriers обязательны; frozen helper не изменять; права Users только чтение логов, без записи конфигов/control state.
- Новое уточнение владельца: fresh local l4setup развёртывает suite без сертификата и/или связи. Installed и communication-ready должны различаться. Это не отменяет remote barriers.
- Риски: живой remote worker ещё не подключён. Orphan marker и event76/tag449 приняты владельцем; новые IoT исходники не развернуты. Polling capability расхождение исправлено в исходниках обеих сторон.
- Источники: component l4update; term_arch-l4update-flow; l4con rpc_contract/mqtt_client/link_probe; setup install_entry/fresh_install/readiness; supervisor service_mgr/mosquitto_log_acl; IoT device_tasks/fs_queues.
- Проверка: изолированные native tests и unified x86/x64 build; targeted IoT checks при изменениях. Без PB/MB checks, production deploy и остановки работающей1.13.6.

## Факты аудита IoT
- Адресный REQ допускает7030–7033 (limit7099); polling allowlist только7001,7002,7003,7011.
- Completed task REQ возвращает NOP с UUID0, исходный correlation не эхо.
- Обычный EVT сохраняется в БД, выполняет webhook/billing и получает EVA.75 исключён из billing.
- Orphan transport-probe на существующих req/rsp/evt/eva требует явной метки, свежей nonce-корреляции и отдельного dispatcher до persistence/billing. Это проверка транспорта, не event persistence.

## ACL
- Mosquitto2.1.2 log open restrict_read=true создаёт явную SYSTEM-only DACL; одного наследования папки недостаточно.
- Исправление должно охватывать создание и ротацию фиксированного log file; private config policy проверяется отдельно на секреты.

## Проверки / остаток
- [x] l4con unified x86/x64 build и RPC/runtime fixtures: nonce/marker, обычный NOP отказ, неправильный SN/ID, late/error replies, outbound existing-client REQ/EVT, RPC703x parameter validation.
- [x] Terminal-result76/449 codec: original operation vs delivery UUID, single object, bounded output, malformed/false-success refusal; x86/x64.
- [x] IoT45 targeted tests + ruff/black; transport-probe без writes/billing/webhook, persistent76 без billing. См. внешний docs/l4update-channel-probe-handoff-2026-10-08.md.
- [x] Supervisor unified build: x86/x64, policy/signals698 checks, logs/config ACL fixtures; exit0.
- [x] Live mosquitto.log DACL восстановлена root без SCM/restart, файл открывается на чтение; restricted-token AccessCheck read=true, write/delete/DAC/owner=false.
- [x] Live mosquitto.conf совпал с built-in renderer для фактического SN; Users read только на файл. SHA256 содержимого сохранён, restricted-token read/no-write проверен. Родитель и custom files приватны.
- [x] Actual isolated SCM под SYSTEM: x86/x64 по62 checks,0failures; slow start/stop, PID/process exit, rejected ImagePath/account/start-type drift. Owned services/task/private launch assets удалены; логи tools/dist/.release/evidence/system-scm-20261008. Четыре действующие службы Running.
- [x] Remote request record92: durable original journal identity, exact retry без нового timestamp/sequence, changed payload conflict, hostile version/duplicate refusal; изолированные x86/x64 tests.
- [x] Fresh local-only offline admission: cache-only signature/chain/time/timestamp/publisher; fallback только unknown revocation (два кода), known revocation запрещена. Remote strict APIs неизменны; x86/x64 admission732(+real signedPE735), fresh1137, manifest86 checks0failures.
- Текущая стадия кодинга: independent SCM host/ACK93, immutable installed-source reader и отдельный live status reader, monitor proof. Watch mode3 пока намеренно закрыт; forward executor/terminal installed-source receipt/publisher76 не подключены.
- Independent host foundation завершена: setup unified build x86/x64/default exit0, request/reader/source/host/entry fixtures PASS. Engine отсутствует → отказ до ACK93; не выдаёт ложный202. Подробности: 2026-10-08-remote-host-foundation.md.
- Первоначальный actual SYSTEM source probe правильно обнаружил исходную committed operation54c027a1-b7b7-41f0-a9c3-abde8b32d2a4/version1.13.6 и owner-signed metadata, но fullinventory отказал ERROR13: descriptor36 файлов, PF37, extra l4capture/bin/mft_capability.ini создан runtimeproducer рядом сEXE. Admission не ослаблен; последующее устранение причины и повторная проверка описаны ниже.
- После проверки idle marker/отсутствия capture/media и строгого known-cache format этот единственный extra перемещён под SYSTEM в private operator-transition/c21b0764-41ee-4d2c-b898-d051e0fc1887 с сохранением SHA/исходного ACL; не удалён. Повторные actual SYSTEM source open+verify PASS на x86/x64: original54c..., version1.13.6, signed bundle arch=x86, полные WinTrust/inventory/setup-host/SCM/четыре исходных процесса. Strict admission не менялся. Старый capture пока может снова создать extra; окончательно это устранит новый signed producer.
- l4capture producer переведён на optional ProgramData/state/l4capture cache; новый отдельный ACL leaf даёт запись конкретному desktopSID и чтение l4con, без Users write. Отсутствие provisioned leaf → no-cache, без записи в installed release. Capture unified build и130/0 tests на каждой архитектуре PASS; текущая1.13.6 не заменялась. Provision новой stateleaf для текущей установки остаётся baseline prerequisite.
- [x] Полный l4setup run_tests x86/x64 exit0. После добавления cacheleaf и meaningful ACL/missing-leaf fixtures access69/0 на каждой архитектуре. Final unified setup build после диагностических изменений x86/x64/default exit0; unsigned development outputs, installed1.13.6 не заменялась.
- Дополнительный найденный gap: fresh L4Superv не имел SCM crash restart profile. Добавлены фиксированный crash-only restart (1000ms, reset86400s, без command/reboot/non-crash restart), journal96/97 и read-only proof. Bootstrap43982, watch427, boot1364 проверок на каждой архитектуре прошли; новый supervisor unified build exit0.
- [x] Actual isolated SCM/SFA под SYSTEM: x86/x64 по97 checks,0failures; exact profile readback, drift/budget refusal, self-crash → автоматический restart с новым PID/creation, graceful STOP без restart. Уникальные тестовые службы, SYSTEM task и private launch assets удалены; evidence tools/dist/.release/evidence/system-sfa-20261008. Четыре действующие службы Running, их recovery profile не менялся. Это не проверка полного watchdog rollback flow.
- [x] Повторная unified l4con build после nil-UUID refusal: x86/x64/default exit0, RPC/event/FM/update-state fixtures passed.
- [x] IoT51 targeted tests + ruff/black: валидный orphan при неизвестной identity/ошибке или timeout БД возвращает общий status=error; lookup2s внутри общего10s, без записи в БД. Production не развернут.
- [ ] Remote apply и результат по одному7031.
- [x] Immutable helper bootstrap source: separate owner-signed kit document,
  first-seal copy only, protected fixed SYSTEM install/load without replacement.
  Frozen source/bin unchanged; no initial actual signing/install yet. See
  2026-10-08-immutable-helper-bootstrap.md.
- [x] Fixed source worker entry: original parent93 PID/birth/installer hash,
  technical68/69 and signed64 binding, strict held-image signature, repeat admission
  before execution.361/0 per arch; actual files/ACL/CNG/journal with
  admission/SCM/signature modeled. Main engine still NULL; real worker startup
  and apply remain pending. See 2026-10-08-remote-worker-entry.md.
- [x] Supervisor recovery template prerequisite348/0 per arch and original live status209/0 per arch. Actual isolated SYSTEM status read14/0 each, own task/assets/journals cleaned, installed SCM unchanged. Signed route pure decoder271/0 each and final setup/Con unified builds passed. No actual worker launch: entry/helper baseline receipt missing; RPC7032/publisher still unwired. See supervisor-template-prerequisite and remote-live-status packets.
- [x] Pre-worker terminal failure/cancellation94: original92/93/trusted60 binding, immutable exact retry, no success/stop authority; native isolated218/0 each architecture. Pure Con event76/449 adapter and unified Con gates passed; actual publisher not connected. See 2026-10-08-remote-preparation-result.md.
- [x] Owned remote worker-plan64 composition and copied source/request/GUID lifetime, no borrowed journal/source pointer.392/0 per architecture, preparation174/0, final combined setup unified build exit0 after both steps. No worker launched or installed files changed; see 2026-10-08-remote-worker-plan-composition.md.
- [x] Remote preparation composition92/93→60→61/62: immutable latest resolution, source/host/profile binding, retained all-hop pins, shared deadline/cancellation including after flushed62. Setup unified build passed; preparation171/metadata1225/host104 checks and platform fixture passed on both architectures. Exact runtime profile windows-nt-10.0.19045-x64-client, bundle arch separate. No actual live Registry preparation, signed publication or installed update. See 2026-10-08-remote-controller-preparation.md.
- [ ] Fresh offline/no-cert install, local integration.
- [ ] Mosquitto log read through desktop/FM после создания и ротации.
- [x] Orphan/event contract: решение владельца, native+IoT implementation, targeted tests; publisher76 и deployment ещё не выполнены.
- Evidence level на старте: source audit; действующая1.13.6 ранее принята оператором, это не remote E2E.
