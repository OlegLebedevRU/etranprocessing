# FM implementation handoff — 2026-10-05

## Контекст задачи / intake
- Пользователь поручил внедрение FM после архитектурного анализа; extra_service и tools/l4con подтверждены.
- Root: etranprocessing, feat/rpc7011-renewal, baseline d8719971fa143cd2e5b8a191adb43681a3f23aba; working tree без commit.
- IoT: origin/master 8c2be80 сверён fetch + ahead/behind 0/0; отдельная feature/file-manager в D:/work/iot.leo4.ru/iot-rpc-rest-app-fm.
- Исходный D:/work/iot.leo4.ru/iot-rpc-rest-app с пользовательскими dirty docs сохранён без правок.
- Scope: shared/PB/MB/React/native и common IoT lease/MQTT producer; legacy FRONT/BACK/SQL исключён.
- Владельцы: IoT common lease/MQTT; PB agent HTTPS/state/S3 control; MB JWT/access/UI; native FS execution.
- File bytes только agent–S3–browser; relay/fallback запрещён. Fail-fast, manual retry с нуля, no resume.
- Runtime broker/production/provider, migration и deploy не выполнялись.

## Выполнено
- Standalone /files и «Файлы» в Classic/L4Desk; capability + heartbeat + certificate + MQTT preflight до старта.
- Common files lease конфликтует со всеми console/video/input/view даже для same owner; Redis WATCH renew/drain и запрет generic upgrade/keepalive.
- MQTT RPC 7020–7023 producer/consumer, strict metadata shape; preserved extra_service svc presence.
- PB verified leaf/CA/SN/serial и agent-instance binding; mTLS nginx ingress, strict internal auth, no-store responses.
- Metadata operations, SHA-256 manifests, versioned signed S3 PUT/HEAD/pinned GET, commit fence и receipt reconciliation.
- Native worker, direct S3 WinHTTP, bounded buffers, monotonic watchdog/disconnect cancellation,
  ancestor handles/path alias/junction/ADS protection, no-overwrite atomic rename, protected receipt.
- Shared declarative fm_agents/fm_operations, additive 032, snapshot/provenance; MB schema guard требует 032.
- План F0–F8 и фактические ограничения: [архитектура](../../../docs/etran_arch-file-manager-remote-windows.md).
- Updated l4con уже входит в existing x86/x64 pack_zip/installer distribution; подписанный пакет не выпускался.

## Затронутые контракты
| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| Files lease | IoT working tree | MB/PB/new agent | Требуется coordinated IoT rollout; старый agent не объявляет FM caps |
| RPC 7020–7023 | IoT file_manager dispatch | l4con native parser/worker | Strict dt[0], no path/URL/bytes; не меняет 7001/7011 |
| Agent metadata v1 | l4con instance UUID | PB mTLS routes | protocol=1 + 5 fs capabilities; old instances отклоняются |
| Shared schema 032 | PB Alembic | PB/MB | Additive tables; old MB guard на 031 требует bridge/coordinated release |
| S3 single PUT | browser/native | S3/pinned receiver | 0–64 МиБ; versioning/checksum required; provider gate ещё не выполнен |

## Проверено — локально
- [x] PB: ruff check --fix app / ruff format app / pyright app, exit 0.
- [x] PB финальный full pytest: 256 passed, 1 skipped; отдельные FM/migration 32 passed
  (restart generation rejection + additive 031→032 SQL).
- [x] MB: quality trio app, exit 0; full 704 passed / 22 skipped;
  после no-store изменения targeted test_file_manager.py: 4 passed.
- [x] Shared: quality trio etranprocessing_db, exit 0; full 65 passed.
- [x] Frontend: npm run build exit 0; npm test: 78 passed / 16 files;
  npx playwright test e2e/file-manager.spec.ts: 5 passed (Classic, L4Desk, incompatible agent,
  occupied console, corrupt direct S3 download abort/no portal auth headers).
- [x] IoT (из repo root): uv run --project app-service pytest app-service/tests -q:
  536 passed, 7 skipped; changed files ruff + black --check exit 0.
- [x] Native cmd /c build.cmd all: x86/x64 static executables + protocol/discovery/RPC/real Win32 FM tests, exit 0.
  Проверены malformed action, IDs, ADS/device/alias/junction, ancestor handles, existing target preservation/atomic rename.
- [x] Alembic single head 032 и offline additive SQL; shared schema contract и source provenance.
- [x] git diff --check root/IoT exit 0 (только line ending warnings).
- [x] Read-only secret pattern scan: 841 разрешённых tracked/new text files root;
  10 совпадений проверены: illustrative URLs/test fixtures/scanner regex, real credential не обнаружен.
  Запрещённые каталоги не читались; это pattern scan, не доказательство отсутствия любого секрета.

## Failed attempts и исправления
- Shared AST whitelist первоначально отверг UUID import; добавлен допустимый stdlib uuid, полный rerun 65 passed.
- Browser доступ к «Открыть» первоначально был неоднозначен; явный aria-label, rerun passed.
- Native relative FILE_RENAME_INFO RootDirectory дал Win32 87; absolute FileName с удерживаемыми ancestors,
  отрицательный no-overwrite и успешный rename проверены x86/x64. Build test exit propagation исправлен.
- IoT pytest из app-service дал 1 failure исторического docs evidence из-за относительного cwd;
  правильный run из root: 536 passed. Тестовые generated schema/fixture rewrites в isolated checkout восстановлены из HEAD;
  пользовательские files исходного checkout не затрагивались.

## Не выполнено / release gates
- [ ] Disposable PostgreSQL 18 live upgrade 031→032: Docker engine локально недоступен; offline SQL не заменяет DB check.
- [ ] Nginx runtime nginx -t и реальный client TLS ingress/CA/serial mismatch.
- [ ] Actual S3: signed checksum/length, VersionId GET, zero-byte, CORS, TLS, lifecycle/current+noncurrent retention.
- [ ] Live common lease + MQTT disconnect/lost renew/delayed stop, cross-worker fault E2E.
- [ ] Win7/POSReady + Windows 10/11 runtime и достаточность 5-second drain guard.
- [ ] Signed native suite packaging/release и payment latency/resource canary.
- [ ] S3 cleanup: используется provider lifecycle, собственного delete/retry worker нет.
- [ ] Crash до receipt может оставить partial; automatic orphan staging cleanup не реализован.
  Неизвестный committing без доказуемого receipt блокирует transfer до расследования.

## Cleanup / evidence level
- Metadata fixtures локальные, test credentials/live sessions/buckets/remote containers не создавались.
- Playwright preview завершён тестовым runner; local unsigned bin/dist и venv оставлены как build artifacts, не release.
- IoT выделенный checkout оставлен для review; внешние пользовательские изменения не переносились/не удалялись.
- Evidence: static source + local unit/mock browser/real local Windows APIs. Не integration/E2E/provider readiness.
- Secrets/PIN/JWT/file contents не сохранены в handoff.
