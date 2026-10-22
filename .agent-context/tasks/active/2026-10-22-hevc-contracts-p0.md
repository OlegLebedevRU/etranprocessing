# HEVC TASK-00: contracts and golden fixtures

## Контекст задачи

- Scope: только TASK-00 из `D:/.codex/hevc-plan/tasks/TASK-00-CONTRACTS.md`.
  P1/P2/P3/P4, product changes, broker/DB access, deploy, hardware testing не выполняются.
- Дата:2026-10-22; уровень: static + offline conformance.
- Канонический [контракт](../../../docs/etran_arch-hevc-contracts.md),
  [план](../../../docs/etran_arch-hevc-rollout-plan.md),
  [media/siplite](../../../docs/etran_arch-hevc-media-siplite-contract.md).
- Revision:`hevc-contract/2.0.0`; полный пакетный hash и source references —
  [manifest](../../../contracts/hevc-v2/manifest.json).
- Финальный aggregate SHA-256:
  `f065b967cd10c2c709fb394ef9e6c2ecf74f77de060d88c9a821cc9069766ac4`.
- Результат подготовлен для фиксации отдельными TASK-00 коммитами в working trees:
  `D:/.codex/worktrees/hevc-etran` (`hevc/contracts-v2`),
  `D:/.codex/worktrees/hevc-iot` (`hevc/control-v2`),
  `D:/.codex/worktrees/hevc-l4tools` (`hevc/contracts-p0`).
  Имена веток не означают реализацию P1.
- Исходные пользовательские рабочие деревья и их изменения сохранены.

## Исходные SHA / immutable code references

| Владелец | Исходная/текущая базовая ревизия | Первичный источник |
|---|---|---|
| etranprocessing | `4d4be68d45a197b4390af6e0c29e7bb6225c0789` | [SourceSelector](https://github.com/OlegLebedevRU/etranprocessing/blob/4d4be68d45a197b4390af6e0c29e7bb6225c0789/MenuBuilder/frontend/src/components/video/SourceSelector.tsx), [video_control](https://github.com/OlegLebedevRU/etranprocessing/blob/4d4be68d45a197b4390af6e0c29e7bb6225c0789/MenuBuilder/backend/app/routers/video_control.py) |
| IoT | `36814a1705216520bb841b6e52e57bdde07f9de7` | [schemas](https://github.com/OlegLebedevRU/iot-rpc-rest-app/blob/36814a1705216520bb841b6e52e57bdde07f9de7/app-service/core/remote_input/schemas.py), [leases](https://github.com/OlegLebedevRU/iot-rpc-rest-app/blob/36814a1705216520bb841b6e52e57bdde07f9de7/app-service/core/remote_input/leases.py) |
| native | `8fb1e730779d0544b97ba8fb3c1ebebe2bd0ca06` | [profile IDs](https://github.com/OlegLebedevRU/l4tools/blob/8fb1e730779d0544b97ba8fb3c1ebebe2bd0ca06/tools/l4capture/include/l4capture/video_profile.h), [adapter](https://github.com/OlegLebedevRU/l4tools/blob/8fb1e730779d0544b97ba8fb3c1ebebe2bd0ca06/tools/l4desk/src/l4capture_adapter.c), [raster](https://github.com/OlegLebedevRU/l4tools/blob/8fb1e730779d0544b97ba8fb3c1ebebe2bd0ca06/tools/l4capture/src/main.c) |

Новые исходные SHA относительно пакета плана отсутствуют: чистые bases совпали.
Итоговые полные commit SHA передаются исполнителям в координаторском файле
`D:/.codex/hevc-plan/TASK-00-HANDOFF.md` после фиксации всех трёх репозиториев.
Этот handoff входит в соответствующий TASK-00 commit; его hash не вписывается
в собственное содержимое. Source SHAs таблицы выше не заменяют итоговые SHA.

## Выполнено и файлы

- Все repos: `contracts/hevc-v2/` (schema, positive/negative fixtures, actual v1
  model snapshots, semantic/lifecycle vectors, verifier, offline native reader,
  README, per-file manifest), `contracts/hevc-v2.lock.json`,
  `.github/workflows/hevc-contract.yml`, LF правило в `.gitattributes`.
- etranprocessing: три canonical docs выше, docs/README.md, этот handoff,
  ссылки в context index/video/l4media и active task index.
- IoT: `docs/hevc-contracts-p0.md`, docs/README.md,
  `docs/l4desk/handoffs/2026-10-22-hevc-contracts-p0.md`.
- l4tools: `docs/term_arch-hevc-contracts-p0.md`, README.md,
  `.agent-context/tasks/2026-10-22-hevc-contracts-p0.md`, context index.
- Формы CTL v2 START/ACK/READY/error/STOP/renew/inventory, service descriptor,
  bootstrap, strict limits и projection закреплены отдельно от v1.

## Проверено

- [x] Actual неизменённые IoT v1 Pydantic models принимают все6 legacy fixtures;
  их schemas равны frozen snapshots. Импортирован только DTO модуль, без app startup.
- [x] 30 schema fixtures,24 PREPARE и9 lifecycle vectors проходят offline oracle.
- [x] 61 unknown-property mutations отвергаются на вложенных уровнях, duplicate keys
  отвергаются; полный v1 error projection сравнивается с golden object.
- [x] Все3 пакета побайтово совпадают (`verify.py --peer`), manifest проверяет
  missing/extra/changed package files и общий digest, local lock и CI pin.
- [x] Старый native `json_min.c` прочитал golden v1 START/STOP: MSVC /MT /W4 /WX,
  x86 и x64; по2 PASS на архитектуру. Не запускался agent/CTL command handler,
  не создавались capture/MQTT/lease и не производился пользовательский ввод.
- [x] Негативные проверки drift: неверный CI pin, изменённые bytes fixture,
  добавленный/удалённый файл и несовпадающий local lock завершают verifier ошибкой.
- [x] Все25 относительных ссылок новых документов разрешаются; diff --check проходит.
- [x] Read-only scan167 task artifact files: private-key markers, authenticated DB
  URLs, GitHub token patterns и AWS access key patterns не обнаружены.
- [x] Profile low raster: расхождение README1280×960 и main.c1920×1080 вынесено
  пользователю. При отсутствии уточнения сохранён фактический предел1920×1080/15fps
  с обязательным probe точного режима, согласно плану сохранения размеров.
  Default —1920×1080/25fps. Это контрактный выбор, не новая аппаратная capability.
- [ ] Product handler conformance, native full handler, live broker, Janus replay,
  browser decoded frames, real GPU: не выполнялись, относятся к последующим этапам.
- N/A: pytest/ruff/pyright/build в MenuBuilder/ProcessingBackend — их код не менялся.
- N/A: native product build/signature/packaging — product code/binaries не менялись;
  собирался только безопасный offline decoder harness.
- CI workflow подготовлены; GitHub hosted run без push/PR не выполнялся.

## Publisher / watcher / совместимость

Actual in-memory/Redis lease acquisition допускает одну active lease/SN,
reacquire для того же user/org/device+owner_session. Иная сессия получает lease_taken.
Новых независимых viewer leases нет. Janus fan-out проверяется отдельно от admission.
Watcher связан с реальным stream UUID; leave не должен останавливать publisher.
Owner stop и input exclusivity сохраняются. Follow-up: independent viewer admission
как отдельный дизайн, не скрытое расширение HEVC scope.

Legacy requests/writers и IPC START70bytes остаются прежними; v2 opt-in явный.
HEVC runtime error terminal; suggested retry — информация, auto_restart_allowed=false.
Исходные проектные примеры `schema_version`/top-level tracks заменены единым
нормативным `v=2`/`contract_version=2` и вложенным descriptor во всех копиях.

## Запуск, rollback и cleanup

Команды воспроизведения — [package README](../../../contracts/hevc-v2/README.md).
Верификатор — reference oracle для спецификации; реальные handlers должны пройти
те же vectors на TASK-10/20/30. Offline green не означает deployed server readiness.
Rollback P0 — удалить/revert только перечисленные файлы и index additions;
runtime состояние не создавалось. Для будущего rollback readers требуется drain
HEVC/v2 state и сброс negotiation, см. canonical contract.

Task-owned compiler artifacts/запуски собраны в `D:/.codex/hevc-plan/evidence/`;
сборочные процессы завершены. Credentials/test leases не создавались.
Worktrees сохранены как результат P0. Грязная исходная копия не очищалась.
Cleanup failed: автоматическая проверка отклонила удаление task-owned неудачной
partial clone `D:/.codex/worktrees/hevc-native` и offline compiler `.exe/.obj`
в `D:/.codex/hevc-plan/evidence/{x86,x64}` с причиной `blocked by policy`.
Команда не запускалась; эти временные артефакты оставлены, обход запрета не выполнялся.

## Итог G0

G0 спецификации/offline conformance пройден: единый revision/hash, схемы и vectors
распределены по трём владельцам, legacy readers проверены в указанной границе,
неизменяемость descriptor/compatibility/admission/rollback описаны. Product reader/
writer integration и запуск hosted CI не входят в это evidence. P1 и последующие
этапы не запускались. Профильный выбор выше явно отражает неотвеченное уточнение,
поэтому не должен интерпретироваться как аппаратная приёмка или изменение v1.
