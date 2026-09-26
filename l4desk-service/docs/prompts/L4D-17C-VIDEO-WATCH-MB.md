# L4D-17C-VIDEO-WATCH-MB — Интеграция read-only video-watch consumer в MenuBuilder

```yaml
prompt_id: L4D-17C-VIDEO-WATCH-MB
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing\MenuBuilder
prompt_type: corrective-consumer
registration_id: R-L4D-17C-VIDEO-WATCH-MB-v1
required_handoff_ids:
  - H-L4D-17C-VIDEO-WATCH-IOT-01-v1
sequence_gate_handoff_id: H-L4D-17C-VIDEO-WATCH-IOT-01-v1
output_handoff_id: H-L4D-17C-VIDEO-WATCH-MB-v1
next_prompt_id: L4D-17E-MB
branch: l4desk/l4d-17c-video-watch-mb
report_path: MenuBuilder/docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-MB-report.md
candidate_format: DETACHED_V1
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-MB-candidate.md
architecture_sections: [3, 4, 5, 8, 11, 12, 16, 17]
```

## 1. Контекст корректирующего шага

Provider-контракт `H-L4D-17C-VIDEO-WATCH-IOT-01-v1` принят (§43 журнала). До corrective операторский экран MenuBuilder
опрашивал status тремя GET каждые 5 секунд, два из которых уходили в IoT status. Для закрытия
масштабирования browser status polling зарегистрирован отдельный consumer-corrective: MenuBuilder
подключается к read-only WebSocket invalidation feed и перечитывает REST snapshot по событиям и reconnect.

Уточнение по явному подтверждению пользователя от 2026-09-26: принимается фактически развёрнутый
контракт BFF → browser с **одним `invalidate` после проверки provider snapshot** и REST resnapshot
в browser. Server-side snapshot cache не требуется; BFF не передаёт браузеру сырой provider snapshot
или внутренний ключ. Это уточнение сохраняет авторитетность REST, tenant/auth и read-only ограничения.

Работа выполняется **строго после** принятия provider. Scope corrective ограничен `MenuBuilder`.
Внедрение polling вне зарегистрированного corrective scope запрещено.

## 2. Инварианты provider-контракта (consumer rules)

1. **Snapshot on connect**: при открытии внутреннего WS BFF получает один authoritative `snapshot`
   (`{"type":"snapshot","data":{...StatusResponse...}}`), проверяет SN и посылает браузеру
   только `invalidate`. Браузер читает авторитетный REST snapshot.
2. **Invalidate → REST resnapshot**: последующие `{"type":"invalidate","kind":"stream|presence",...}`
   — только сигнал перечитать REST status. Не трактовать transient event как долговечный факт.
3. **Не восстанавливать `running` из старого события.** Событие от старого `stream_instance_id`
   не меняет состояние новой stream epoch.
4. **Lease mutation запрещена**: feed только read-only; keepalive/release остаются отдельным REST/WS-контуром.
5. **Auth**: `X-Internal-Service-Key` + `X-Org-Id`; ownership `sn` проверяется provider. Ключ и browser JWT
   не попадают в event payload и логи.
6. **Reconnect**: после разрыва BFF заново проверяет provider snapshot; браузер переподключается
   с backoff и перечитывает REST status. При недоступном WS действует временный 5-секундный REST
   fallback. Slow subscriber закрывается provider (send timeout / backlog > 100).
7. **`WEB_CONCURRENCY=1`**: контракт действует только при одном worker `app1`. Cross-worker fanout
   отложен до после каскада L4D.

## 3. Задачи MenuBuilder

1. Backend: per-browser BFF WS соединение к `/api/internal/v1/remote-input/ws/watch/{sn}` с
   internal-service auth, проверкой provider snapshot, tenant/device/permission boundary и передачей
   браузеру только `invalidate`. Внутренний ключ и provider snapshot не раскрывать.
2. Backend: сохранить существующие авторитетные REST status endpoints и control API; browser WS
   должен быть read-only. Server-side snapshot cache в этом варианте контракта не используется.
3. Frontend: перевести операторский status polling на browser WS `invalidate` → REST refetch;
   reconnect/backoff и временный REST fallback при недоступном WS; убрать лишние status GET
   (цель: одна пара status endpoints на событие или reconnect).
4. Сохранить семантику «video running» только из текущего REST snapshot, не из invalidate-hint.
5. Tests: unit/consumer fixtures на snapshot validation, invalidate-only relay, stale
   `stream_instance_id`, reconnect, auth/tenant failure и lease non-mutation.
6. Линтеры/типы `MenuBuilder`: ruff, pyright, tsc, vitest.

## 4. Проверки

1. Gates: `H-L4D-17C-VIDEO-WATCH-IOT-01-v1` ACCEPTED; registration AUTHORIZED.
2. Digests MenuBuilder artifacts сверить с DETACHED_V1 candidate.
3. Полный local suite + lint/type.
4. Production-safe smoke: provider snapshot → BFF/browser invalidate → REST resnapshot без lease mutation.
5. Измерить снижение status GET rate до/после; зафиксировать latency.
6. При ошибке — corrective prompt, runtime в acceptance не менять.

## 5. Выход

`H-L4D-17C-VIDEO-WATCH-MB-v1`: `REPORT/DEPLOYMENT` с consumer integration, snapshot/invalidate
evidence, digests, metrics снижения polling и consumer `L4D-17E-MB`.
