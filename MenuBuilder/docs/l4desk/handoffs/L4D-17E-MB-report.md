# L4D-17E-MB — приёмка коммерческого контура: заблокировано

```yaml
prompt_id: L4D-17E-MB
scope_project: MenuBuilder
output_handoff_id: H-L4D-17E-MB-v1
status: BLOCKED_CONTRACT
deployment_status: NOT_CHANGED
deployed_environment: production
candidate_status: NOT_CREATED
next_prompt_id: L4D-17F-DOCS
```

## Task intake

- Цель: подтвердить project-local commercial control plane и consumer contracts перед 17F.
- Владелец API/UI/billing: MenuBuilder; lease и watch producer: app1; media: l4media; Alembic: ProcessingBackend. Producer → REST/WS/media → MenuBuilder → browser и financial ledger.
- Инварианты: не выполнять реальные платежи/массовые email, не подменять accepted handoff локальными тестами, не менять работающую версию на хосте при инвентаризации.
- Проверка: sequence gate, local suite/quality/build, сравнение развёрнутых файлов и non-secret flags, затем разрешённый production-safe smoke на тестовом tenant. Последний сейчас недоступен.

## Sequence gate

`H-L4D-17D-MEDIA-v1` принят (§47 единого журнала). Регистрация `R-L4D-17C-VIDEO-WATCH-MB-v1` есть (§48), но обязательный для 17E `H-L4D-17C-VIDEO-WATCH-MB-v1` **не принят**: [отчёт corrective](L4D-17C-VIDEO-WATCH-MB-report.md) имеет `READY_FOR_BROWSER_VALIDATION`, отдельного `DETACHED_V1` candidate нет. Это достаточный blocker для handoff 17E. Остальные required IDs перечислены в `L4D-17E-MB.md`; эта итерация не объявляет их повторно принятыми или повторно проверенными.

## Что выполнено 2026-09-26

| Проверка | Факт | Уровень |
|---|---|---|
| `uv run pytest -q` из `MenuBuilder/backend` | 469 passed, 41 warnings, exit 0. В предупреждениях есть `AsyncMock` coroutine not awaited; не считать предупреждения исправленными. | local test |
| `uv run ruff check app tests` | All checks passed, exit 0. | local lint |
| `uv run ruff format --check app tests` | 128 files already formatted, exit 0. | local format |
| `uv run pyright app` | 0 errors, 0 warnings, exit 0. | local type |
| `npm run build` из `MenuBuilder/frontend` | TypeScript/Vite build завершился, exit 0; предупреждение о размере vendor chunk. | local build |
| `npm test -- --run` из `MenuBuilder/frontend` | 11 files, 55 tests passed, exit 0. | local test |
| Host resource preflight | Load 0.11/0.15/0.23; available RAM 2208 MiB; root disk 73%. MCP Ops недоступен, использован SSH только для чтения. | runtime observation |
| Активные контейнеры | `app1`, `menubuilder-backend`, `nginx-default`, `processing-backend`, `l4media-ingress`, `l4media-janus` работают. Эта итерация их не пересоздавала. | runtime observation |
| Active MenuBuilder files | SHA-256 `config.py` `e4075f7316d676e3d97053fe4146cc0cbea4c61c7e62d6ecf4fbff2965027c86`; `video_control.py` `5f03d6b7deac5995dc67f0007c485da32c4eee4a71dbe01bceedb9e3be3e0b20`; `video.py` `ac7856605aa42cc24db1ec8657d5f5959e6496a35e5940ff68dbc614f78f03bc` — совпадают с локальными файлами. | runtime/source byte match |
| Active frontend | `index.html` `56cca6dd19c3c23777988fe917861350a58d5d4567577464bbe970fa17e44dc4`; video chunk `d8b661c4ceb6e4548964502024dacd88b306a3040215be25b833802d265b9de2` — совпадают с локальной сборкой. | runtime/source byte match |
| Non-secret effective flags | `l4desk_enabled`, registration, billing, UI, policy enforcement, entitlement worker и YooKassa — `false`; session orchestration — `true`. Значения прочитаны из активного backend process config без вывода секретов. | runtime observation |

Активный MenuBuilder image — производный hotfix `sha256:295996258a458b4e4338767ee2a7755cc620d64ea7677c1e30c7aedd61aa5cdd`. Совпадение трёх файлов и frontend assets не доказывает byte-for-byte равенство **всего** backend image репозиторию. Рабочая версия сохранена; штатный воспроизводимый release из принятого commit остаётся отдельной проверкой.

## Что не выполнено

- Нет утверждённого test tenant, краткоживущих тестовых учётных данных, безопасной email delivery и YooKassa sandbox/test payment path. Пользователь подтвердил отсутствие процедуры; секреты в отчёт не записывались.
- Production-safe commercial smoke и black-box последовательность registration → terminal → usage → payment → ledger/balance → grace/block не запускались. Реальные флаги коммерческого контура выключены.
- Полная матрица промпта (free 120 min, paid continuation, first payment anchor, terminal-month online once, DST/month boundaries, webhook/poll idempotency, manual payment/storno, double-entry/rebuild, rounding, Hub и archive) имеет локальные тесты в suite, но здесь не собрана самостоятельная per-scenario acceptance и не подтверждена на тестовом tenant.
- Не выполнена независимая сверка всех `artifact_paths`/digest и immutable consumer fixtures с окончательным candidate 17E: candidate намеренно не создан при незакрытом gate.
- `zero ledger imbalance` и `zero reconciliation mismatch` для живого тестового E2E не доказаны; запросы к production ledger не выполнялись.

## Следующий минимальный путь к ACCEPTED

1. Завершить и принять `H-L4D-17C-VIDEO-WATCH-MB-v1` по зарегистрированному corrective, включая отчёт `ACCEPTED`, опубликованный `DETACHED_V1` candidate и проверку deployment.
2. Утвердить выделенный test tenant, способ выдачи short-lived credentials, sandbox email/ЮKassa, тестовый терминал/Agent и cleanup; не включать коммерческие флаги для production users.
3. Выполнить полную матрицу `L4D-17E-MB.md` и production-safe smoke с financial invariants. Сверить полный backend release/image, versions, flags и rollback, затем выпустить новый отчёт и candidate. Только контроллер добавляет `H-L4D-17E-MB-v1` после независимой проверки.

Эта запись — evidence и blocker, не accepted handoff и не разрешение на 17F E2E.
