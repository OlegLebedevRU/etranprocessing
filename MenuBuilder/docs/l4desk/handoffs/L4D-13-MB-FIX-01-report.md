# L4D-13-MB-FIX-01 — Report

**Prompt ID:** `L4D-13-MB-FIX-01`  
**Scope project:** `MenuBuilder`  
**Branch:** `l4desk/l4d-13-mb-fix-01`  
**Producer (implementation) commit:** `45bd645a1f468a74e4cbfb705caccf309bfd1162`  
**Corrective registration:** `R-L4D-13-MB-FIX-01-v1` (publication package commit `fe516c82fa7199229cfd4d5c2c79a43e0305ca53`)  
**Candidate format:** `DETACHED_V1` (see `L4D-13-MB-FIX-01-candidate.md`)  
**Final status:** `ACCEPTED` (ready for controller acceptance)

---

## 1. Prompt ID, scope, branch, commit, status

| Field | Value |
|---|---|
| Prompt ID | `L4D-13-MB-FIX-01` |
| Scope | `MenuBuilder` only |
| Branch | `l4desk/l4d-13-mb-fix-01` |
| Implementation commit | `45bd645a1f468a74e4cbfb705caccf309bfd1162` |
| Status | `ACCEPTED` |

## 2. Проверка `H-L4D-13-MB-v1`

- `H-L4D-13-MB-v1` присутствует в `contract-handoff.md` ровно один раз (`HANDOFF:H-L4D-13-MB-v1:BEGIN/END`), `status: ACCEPTED`.
- `producer_prompt_id: L4D-13-MB`, `producer_commit: f5017615a8a84eee318a78b54a992ceb45f91edc`, `contract_version: 1.0.0`.
- Corrective prompt зарегистрирован контроллером (`R-L4D-13-MB-FIX-01-v1`, `status: AUTHORIZED`) и опубликован в commit `fe516c82fa7199229cfd4d5c2c79a43e0305ca53`.
- Addressing: регистрация выдаёт допуск для `prompt_id: L4D-13-MB-FIX-01` (в `consumers` исходного handoff corrective ID отсутствует).

## 3. Изменённые файлы (только `MenuBuilder`)

### Backend
- `MenuBuilder/backend/app/services/terminal_creation_service.py` (новый)
- `MenuBuilder/backend/app/services/terminal_onboarding_service.py`
- `MenuBuilder/backend/app/routers/admin_terminals.py`
- `MenuBuilder/backend/app/schemas/__init__.py`
- `MenuBuilder/backend/tests/test_terminal_creation_service.py` (новый)
- `MenuBuilder/backend/tests/test_terminal_onboarding.py`
- `MenuBuilder/backend/tests/test_admin.py`

### Frontend
- `MenuBuilder/frontend/src/routes/l4desk/L4DeskTerminalsPage.tsx` (новый)
- `MenuBuilder/frontend/src/App.tsx`
- `MenuBuilder/frontend/src/routes/layout.tsx`
- `MenuBuilder/frontend/src/routes/console/ConsolePage.tsx`
- `MenuBuilder/frontend/src/routes/settings-layout.tsx`
- `MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx`
- `MenuBuilder/frontend/src/components/OnboardingWizardModal.tsx`
- `MenuBuilder/frontend/src/api/settings.ts`
- `MenuBuilder/frontend/src/tests/l4desk-profile-navigation.test.ts`

### Прочее
- `MenuBuilder/CHANGELOG.md`

Изменения вне `MenuBuilder` не выполнялись (`shared`, `ProcessingBackend`, `l4media`, `tools`, `iot-rpc-rest-app`, `l4desk-service` runtime не менялись).

## 4. Существующий административный terminal creation use case

Канонический сценарий административного контура `menubuilder.admin/terminals`:

- **Backend entry point:** `POST /api/admin/terminals` (`MenuBuilder/backend/app/routers/admin_terminals.py::create_terminal`), superuser-only.
- **Общий сервис (выделен данным prompt):** `MenuBuilder/backend/app/services/terminal_creation_service.py`.
- **Формирование SN:** `generate_device_sn()` — формула `a4b<7-digit device_id>c<5-digit random>d<DDMMYY>` (единственная формула в проекте).
- **Правила выделения `device_id`:** `peek_next_device_id()` / `create_terminal_business_record()` — первый свободный ID в диапазоне `1000001…1999999`; явный `device_id` админ-API валидируется в этот же диапазон.
- **Уникальность:** unique constraints `terminals.sn`, `terminals.device_id` + application-level retry при `IntegrityError` (savepoint `begin_nested`).
- **Transaction boundary:** use case делает `flush` terminal business record; caller (admin route / L4Desk onboarding) владеет commit и дополнительными сущностями (License / L4DeskTerminal / audit).
- **Tenant/role/audit checks:** admin — `require_superuser`; L4Desk — tenant scope + actor + `L4DeskAuditEvent` (`terminal.created`).
- **Provisioning / PIN:** admin — optional `iot_client.provision_terminal`; L4Desk — saga IoT provisioning + `ProcessingBackendPinClient` (PIN issuance).

## 5. Отсутствие отдельной L4Desk-формулы SN и allocator-а

Подтверждено:

- Формула `SN-L4D-{tenant}-{ordinal}-{rand}` **удалена** из `terminal_onboarding_service.py`.
- Allocator `max(device_id)+1` **удалён**.
- L4Desk onboarding вызывает `create_terminal_business_record()` — тот же server-side use case, что и admin.
- Frontend не рассчитывает SN и `device_id`; `handleGenerateSn` и input `sn` удалены из `OnboardingWizardModal`.
- `TerminalOnboardRequest` не содержит полей `sn`/`device_id` (legacy `sn`/`device_id` в JSON игнорируются через `extra="ignore"` и не влияют на канонические идентификаторы).

## 6. Enforcement диапазона `1000001…1999999`

- Константы `DEVICE_ID_MIN = 1_000_001`, `DEVICE_ID_MAX = 1_999_999` в `terminal_creation_service.py`.
- `validate_new_device_id()` отклоняет ID вне диапазона (`422`).
- `peek_next_device_id()` выдаёт только ID внутри диапазона.
- `GET /api/admin/terminals/next-device-id` использует `peek_next_device_id()`.
- Новые терминалы с ID вида `10000005` невозможны.

## 7. Legacy IDs вне диапазона (агрегированно)

- В production и тестовых фикстурах встречаются legacy `device_id` вне нового диапазона (например, исторические `773`, `101`, мелкие последовательные ID).
- **Ни одна legacy-запись не изменена** данным prompt.
- Связи SN / сертификаты / provisioning / Агенты не переписывались.
- Массовая миграция legacy `device_id` **не выполнялась** и **не требуется** для работы корректировки: новый диапазон применяется только к новым терминалам.
- Отдельный migration prompt для нормализации legacy ID **не подтверждён как необходимый** и не запрашивается.

## 8. Отсутствие неявной миграции legacy identities

- Update-путей для `Terminal.sn` / `Terminal.device_id` в данном diff нет.
- Provisioning-ответ с иным `device_id`/`sn` **игнорируется** (лог warning), канонические значения сохраняются (исправлена прежняя возможность перезаписи `device_id` из IoT-ответа).
- Delete/retry onboarding не меняет SN/`device_id`.

## 9. Маршруты, navigation и UX-переходы

### L4Desk navigation (порядок)
1. **Терминалы** (`/terminals`)
2. **Видеонаблюдение** (`/video`)
3. **Консоль** (`/console`)
4. **Настройки** (`/settings`)
5. **MCP** (`/mcp`)
6. **Лицензии** (`/licenses`)

Classic profile не изменён (`NAV_ITEMS` / role4 path без изменения порядка).

### Маршруты
- `L4DeskRootTerminalsRoute` на `/terminals`: L4Desk → `L4DeskTerminalsPage`; classic → redirect `/menu/terminals` (legacy UX сохранён).
- `SettingsTerminalsRoute` на `/settings/terminals`: L4Desk → redirect `/terminals` (обратная совместимость L4Desk URL); classic → `TerminalsSettingsPage`.
- Default L4Desk landing: `/terminals` (было `/video`).
- Вложенный «Терминалы» убран из L4Desk settings-nav (`settings-layout.tsx`).

### UX
- «Мастер подключения» — из «Терминалов» (и из empty-state «Терминалов»).
- «Консоль» без терминалов: CTA «Перейти в «Терминалы»»; без самостоятельного создания.
- Console/video use cases переиспользуются: `DeviceConsoleTab`, `VideoSurveillancePage` / `RefusalReasonCard` без копирования.
- Форма создания: только бизнес-поля (`name`, `address`, `note`, `timezone`). Нет input SN / `device_id`.
- После создания: SN и `device_id` read-only + copy; PIN и readiness в существующей модели.
- Порядок мастера: создание → PIN/Агент → readiness/online → переход в console/video.

## 10. Результаты проверок

### Backend
| Check | Command | Result |
|---|---|---|
| Tests (terminal/onboarding/admin/settings) | `uv run pytest tests/test_terminal_creation_service.py tests/test_terminal_onboarding.py tests/test_admin.py tests/test_settings.py` | **PASSED** |
| Full backend suite | `uv run pytest -q` | **467 passed, 1 failed** (см. риски) |
| Ruff check/format | `uv run ruff check app tests --fix` / `uv run ruff format app tests` | **clean** |
| Pyright | `uv run pyright app` | **0 errors, 0 warnings** |

Покрытие обязательных backend-инвариантов:
1. Общий use case admin + L4Desk — да (`terminal_creation_service`).
2. SN formula только server-side — да.
3. L4Desk не принимает SN — да (`TerminalOnboardRequest` без `sn`, тест `test_user_supplied_sn_is_not_part_of_request_model`).
4. L4Desk не принимает `device_id` — да.
5. Новые `device_id` в `1000001…1999999` — да.
6. ID вне диапазона невозможен — да (`validate_new_device_id` tests).
7. SN/`device_id` уникальны — unique constraints + tests.
8. Конкурентность: unique constraint + retry loop в allocator-е (savepoint).
9. Идемпотентный повтор по `operation_id` не создаёт второй record — существующие tests сохранены и зелёные.
10. Ошибка provisioning не удаляет terminal record и не меняет SN/`device_id` — сохранено + запрет перезаписи из IoT-ответа.
11. Tenant isolation — существующие onboarding tests.
12. Административный сценарий не регрессирует — `test_admin_terminals_flow` обновлён под новый диапазон и зелёная.

### Frontend
| Check | Command | Result |
|---|---|---|
| Unit tests | `npx vitest run` | **58 passed / 58** (11 suites) |
| Typecheck + production build | `npm run build` (`tsc -b && vite build`) | **PASSED** |

Обязательные frontend-инварианты (1–12) покрыты `l4desk-profile-navigation.test.ts` и source assertions.

### Migration
- Migration **не создавалась** (enforcement диапазона реализован на уровне use case; legacy rows не трогаются; expand-migration не требуется).

### Smoke (production `87.242.100.34`)
| Probe | Result |
|---|---|
| `menubuilder-backend` | `Up`, startup complete, schema check **PASSED** (revision `027`, 23 tables) |
| `https://127.0.0.1:3000/` | `HTTP 200` |
| `L4DeskTerminalsPage-*.js` asset | `HTTP 200` |
| `terminal_creation_service` in container | `DEVICE_ID_MIN=1000001`, `DEVICE_ID_MAX=1999999`, `generate_device_sn(1000001)=a4b1000001c…d…` |

## 11. Deployment status, feature flags, rollback

- **Deployment status:** `DEPLOYED` (`menubuilder-backend` rebuilt + restarted; frontend `dist/` uploaded to live-mounted volume).
- **Environment:** production `87.242.100.34`.
- **Feature flags:** без новых флагов; существующие `l4desk_terminal_onboarding_enabled` / `l4desk_enabled` не менялись.
- **Feature-flag defaults:** не изменены.

### Rollback readiness
1. **Navigation/route change:** git revert commit `45bd645a…` на ветке `l4desk/l4d-13-mb-fix-01` (или redeploy предыдущего образа `menubuilder-backend`); фронтенд — восстановить предыдущий `dist/`.
2. **Migration:** отсутствует — rollback БД не требуется.
3. **Rollback не удаляет terminal records и не меняет SN/`device_id`.**

## 12. Открытые риски

1. **`tests/test_step6_quick_actions.py::test_stream_start_504_terminal_timeout_not_swallowed`** — падает с `502` вместо `504` (media orchestrator `503` на lifecycle start). Причина в video/media path (`video_control` + media lifecycle), **вне scope** L4D-13-MB-FIX-01 (не изменялся). Похоже на незакрытый регресс после `L4D-08B-FIX-01-MB` (тест не обновлён под обязательный media lifecycle start). Рекомендуется отдельный corrective для `video_control`/`test_step6`.
2. Legacy `device_id` вне `1000001…1999999` остаются в БД (сознательно); UI отображает их read-only.
3. Полный production UI smoke (клик-поток мастер → PIN → online) выполнялся ограниченно (HTTP/asset/API probes + unit/intory tests); deep E2E с live-агентом не входит в доступный контур данной сессии.

## 13. Artifact SHA-256

| Artifact | SHA-256 |
|---|---|
| `MenuBuilder/backend/app/services/terminal_creation_service.py` | `963436ea7f35ab4d8869d840ffc5dca424bd4cb8c4bd546a4e080f6870bc0ee6` |
| `MenuBuilder/backend/app/services/terminal_onboarding_service.py` | `f218c9337c8c56719eaa1ee6f33fa93337fc79070459d23a587708312b27d0f9` |
| `MenuBuilder/backend/app/routers/admin_terminals.py` | `57012055e139e21838a4d2fc9cda8d7acb4ed592a7ef8c790f230b8bd50a7bef` |
| `MenuBuilder/backend/app/schemas/__init__.py` | `19b7dae418f5d216fb11f7036e1d1b2185516786a860ed5c2e16cb8dd3705af3` |
| `MenuBuilder/frontend/src/routes/l4desk/L4DeskTerminalsPage.tsx` | `8c2200fbabc968e5a9ee26218ad2a6874abbacabb5408b9a90eabeca0f3d5c49` |
| `MenuBuilder/frontend/src/components/OnboardingWizardModal.tsx` | `68cdf4b8d4d3b59a394f9d3f8f4a3bfb3dfe18573bfb8606ba86f47428e3fa63` |
| `MenuBuilder/frontend/src/routes/layout.tsx` | `930f9590ee8d4f37b0c775bb4373b5c17421ee9c5ddea1da317b8f364fcc479e` |
| `MenuBuilder/frontend/src/App.tsx` | `9fe0438a784effe2833c858a4f33b167c6ace9ac4cd706b8625727a497717f0f` |
| `MenuBuilder/frontend/src/routes/console/ConsolePage.tsx` | `87993119e88718436a5402fa2f427c2e581423037fb4492950c573ee4be6b4a0` |
| `MenuBuilder/frontend/src/routes/settings-layout.tsx` | `9dccc20c871653c21cf129e471ba6ed42090a111f65127f96f4fb105ed5747e0` |
| `MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx` | `40e3609d482a386601775589c6816c2e9865db01deec97ad45d224a9c29f2f7d` |
| `MenuBuilder/frontend/src/api/settings.ts` | `ac6d7b939c986ebc794a91d5220d6f58a47a6fe9872a15be126beeb4d4dcfcb9` |
| `MenuBuilder/frontend/src/tests/l4desk-profile-navigation.test.ts` | `82d1feddea6a6b76cac6cff6eb63ba35b684f5e5276a7e782b0ad2a9820c1403` |
| `MenuBuilder/backend/tests/test_terminal_creation_service.py` | `14bb44f249ba5b03bd087c2d08ad8f49029088db03cfcde1dae8b8663a798e33` |
| `MenuBuilder/backend/tests/test_terminal_onboarding.py` | `dd1805f2acbc20cf138050e96cb553a0b167dbd3df48a382a79c344068600dbe` |
| `MenuBuilder/backend/tests/test_admin.py` | `61c12e74d373f7c12e6c4b942c48e3dde996cd691ac01f3a73b88abd404419bd` |
| `MenuBuilder/CHANGELOG.md` | `c9a32f621e67756a809cbfb26293d0c53a0040de8acaf552b5a702a6f5a28457` |

Отчёт и candidate публикуются раздельно (`DETACHED_V1`): digest отчёта указывается только в candidate, digest candidate — в финальном ответе исполнителя.
