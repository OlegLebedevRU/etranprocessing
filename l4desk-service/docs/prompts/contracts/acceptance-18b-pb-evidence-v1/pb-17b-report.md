# Handoff Report: Приёмка certificate/PIN-контура (L4D-17B-PB)

## Candidate H-L4D-17B-PB-v1

```yaml
handoff_id: H-L4D-17B-PB-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - DEPLOYMENT
producer_prompt_id: L4D-17B-PB
producer_scope_project: ProcessingBackend
producer_report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-17B-PB-report.md
producer_branch: l4desk/l4d-17b-pb
producer_commit: 9ed5b22
accepted_at_utc: '2026-09-25T19:10:00Z'
contract_version: 1.0.0
schema_revision: "027"
artifact_version: 0.1.0
artifact_paths:
  - ProcessingBackend/backend/app/routers/certificates.py
  - ProcessingBackend/backend/app/schemas/certificates.py
  - ProcessingBackend/backend/app/services/cert_billing.py
  - ProcessingBackend/backend/app/dependencies.py
  - ProcessingBackend/backend/app/config.py
  - ProcessingBackend/backend/app/models.py
  - ProcessingBackend/backend/tests/test_certificate_pin_contract.py
  - ProcessingBackend/backend/alembic/versions/027_add_l4desk_and_fin_ledger.py
artifact_sha256:
  - 0a0e925b1dfad9c6b96f5063cc33b5bea822cd558062681d9b011f0fe02ed93a
  - 5e5327e51703cabb08c696c72477fed5abb75093782bd1514c43649b8681b6f0
  - 50ccee8cb7311e2a55d47ae127ffdcb41e48b2f121aeb1c6a2c8606026770ce5
  - 9b716bf58d274ef478f220ee398c13560d02919092a340fce008178ca955018c
  - 8873a6ecff0b01e045b6e3f66f39e866c0cbb8476aa2ecf6f97ca4b60b30c0ba
  - 5d6b0c36127443bc2d02f90341ad494a7fd86c021f60ef7ff5d81ff3574f403a
  - 4eaa876994b3be8037763685188ef0ade2b1a36a9a1a9c947de0300bf8dae37e
  - 5993027230ffc6121e4bd76988cbbf21d2a2602f64bdcfd39aad72f5efe6177d
compatibility:
  backward_compatible_with:
    - H-L4D-06A-PB-v1
    - H-L4D-04B-PB-v1
    - H-L4D-00C-PB-v1
  breaking_changes: false
  notes: >
    Terminal-facing certificate API (function=check/setup) сохранена и
    подтверждена на production smoke. PIN contract digests байтово совпадают
    с H-L4D-06A-PB-v1. Alembic head = 027 (schema_revision H-L4D-04B-PB-v1),
    незаявленных миграций нет. Локальная сборка acceptance-кодом не менялась.
checks:
  sequence_gate: PASS
  production_image_commit: PASS
  alembic_head: PASS
  api_version: PASS
  project_local_suite: PASS
  lint_ruff: PASS
  type_pyright: PASS
  provider_contract_fixtures: PASS
  production_smoke: PASS
  terminal_api_backward_compat: PASS
  no_undeclared_migrations: PASS
  contract_digests_vs_06a: PASS
  schema_artifacts_vs_04b: PASS
deployment_status: ACCEPTED
deployed_environment: production
feature_flags: {}
supersedes: []
known_risks:
  - "pytest basetemp .pytest_tmp can be file-locked on Windows host (env flake, not contract defect)"
  - "production smoke covers error paths only; no live PIN issuance performed (owner scope)"
consumers:
  - L4D-17C-IOT
next_prompt_id: L4D-17C-IOT
```

---

## 1. Sequence gate

| Handoff | Статус | Примечание |
|---|---|---|
| `H-L4D-04B-PB-v1` | ACCEPTED (journal) | schema_revision `027` |
| `H-L4D-06A-PB-v1` | ACCEPTED (journal) | Certificate PIN Contract v1.0.0 |
| `H-L4D-17A-TOOLS-v1` | опубликован `9ed5b22` | отчёт pushed; journal-регистрация контроллером; переход в 17B авторизован владельцем |

Ветка: `l4desk/l4d-17b-pb` @ `9ed5b22` (база `l4desk/l4d-17a-tools`).

---

## 2. Production image / commit / Alembic / API version

| Параметр | Handoff | Production | Verdict |
|---|---|---|---|
| Alembic head | `027` | `027 (head)` | MATCH |
| API version | `0.1.0` (app) | `0.1.0` | MATCH |
| Image | DEPLOYED production | `user1-processing-backend:latest` `e2194a9b3341`, created 2026-09-18T15:37Z | ok |
| Container | — | `processing-backend` Up 7 days | ok |

### Alembic inventory

Линейная цепочка `001` → `027` (27 миграций, `down_revision` непрерывен).
Незаявленных / лишних миграций **нет**.

---

## 3. Digests vs H-L4D-06A-PB-v1 / H-L4D-04B-PB-v1

### Certificate PIN contract (H-L4D-06A)

| File | SHA-256 | Match |
|---|---|---|
| `app/routers/certificates.py` | `0a0e925b…02ed93a` | YES |
| `app/schemas/certificates.py` | `5e5327e5…8681b6f0` | YES |
| `app/services/cert_billing.py` | `50ccee8c…26770ce5` | YES |
| `app/dependencies.py` | `9b716bf5…955018c` | YES |
| `app/config.py` | `8873a6ec…b30c0ba` | YES |
| `app/models.py` | `5d6b0c36…574f403a` | YES |
| `tests/test_certificate_pin_contract.py` | `4eaa8769…8dae37e` | YES |

### Schema 027 (H-L4D-04B)

| File | Match | Примечание |
|---|---|---|
| `alembic/versions/027_add_l4desk_and_fin_ledger.py` | YES | байтово |
| `docs/l4desk/schema-027.sql` | YES | после EOL-normalize (CRLF на диске) |
| `tests/test_schema_migration.py` | YES | после EOL-normalize; содержимое = git@d7b0b40 |

Функционального дрейфа контента нет.

---

## 4. Project-local suite / lint / type

```text
cd ProcessingBackend/backend
uv run pytest tests/ -v
uv run ruff check app tests
uv run ruff format --check app tests
uv run pyright app
```

| Check | Результат |
|---|---|
| pytest | **129 passed** + 1 env ERROR; isolated retry **PASS** → **130/130** |
| ruff check | All checks passed |
| ruff format | 49 files already formatted |
| pyright | **0 errors**, 0 warnings |

### Provider contract fixtures (`test_certificate_pin_contract.py`) — 17/17

| Сценарий | Test | Verdict |
|---|---|---|
| create | `test_issue_pin_success` | PASS |
| replay (idempotent) | `test_issue_pin_idempotent_replay` | PASS |
| conflict | `test_issue_pin_reused_operation_id_conflict` | PASS |
| ownership | `test_issue_pin_ownership_mismatch` | PASS |
| SN mismatch | `test_issue_pin_sn_mismatch` | PASS |
| not found | `test_issue_pin_terminal_not_found` | PASS |
| auth | `test_service_auth_security` | PASS |
| lookup by operation | `test_lookup_pin_by_operation_id` | PASS |
| terminal check | `test_terminal_facing_check_flow` | PASS |
| terminal setup | `test_terminal_facing_setup_success` | PASS |
| safe retry (network drop) | `test_terminal_facing_setup_network_drop_safe_retry` | PASS |
| CSR binding device_id | `test_terminal_facing_setup_csr_mismatch_device_id` | PASS |
| CSR binding CN | `test_terminal_facing_setup_csr_mismatch_cn` | PASS |
| CSR signature | `test_terminal_facing_setup_invalid_csr_signature` | PASS |
| expired | `test_terminal_facing_expired_pin` | PASS |
| used (no cache) | `test_terminal_facing_used_pin_no_cache` | PASS |
| row locking | `test_row_locking_concurrency_protection` | PASS |

### Schema migration fixtures — 7/7

`test_schema_contract_gate_tables_and_columns`, `..._indexes_and_constraints`, `test_alembic_linear_history`, `test_clean_upgrade_sql_generation`, `test_downgrade_sql_generation`, `test_upgrade_existing_db_and_repeated_deployment_idempotency`, `test_specific_business_constraints` — все PASS.

### Примечание к `test_sign_csr_with_san_fields`

В полном прогоне — ERROR at setup (`PermissionError` удаления `backend/.pytest_tmp`).
С `--basetemp=pytest_tmp_ca` — **PASS**. Это lock temp-каталога Windows, не дефект контракта.
В evidence: **PASS** (isolated clean basetemp).

---

## 5. Production smoke (redacted)

**[MCP Ops Readiness: READY]** (SSH fallback): load `0.29/0.23/0.24`, RAM available `2137 MiB`, disk `81%`, `processing-backend` Up.

Smoke выполнялся error-path probes на выделенных test identifiers (`smoke-17b-*`, terminal_id `99999`, tenant_id `99999`). **PIN не выпускался**, credentials в evidence не попали. Временные JSON на сервере удалены.

| Probe | Ожидание | Факт | Verdict |
|---|---|---|---|
| `GET /api/health` | 200 | `{"status":"ok"}` HTTP:200 | PASS |
| `GET /api/certificates/?function=check` (no PIN) | XML code=2 | `code=2`, «PIN не указан», windows-1251 | PASS |
| `POST /api/certificates/pins/issue` (unknown terminal) | 404 | `TERMINAL_NOT_FOUND` | PASS |
| `POST /api/certificates/pins/issue` (tenant mismatch) | 403 | `TENANT_OWNERSHIP_MISMATCH` | PASS |
| `GET /api/certificates/pins/by-operation/{missing}` | 404 | `OPERATION_NOT_FOUND` | PASS |

Созданных test records нет (только error paths, без записи в `certificate_pins`).
Cleanup: `/tmp/smoke404.json`, `/tmp/smoke403.json` удалены на сервере.

---

## 6. Backward compatibility terminal-facing certificate API

| Endpoint / function | Поведение | Подтверждение |
|---|---|---|
| `GET/POST /api/certificates/?function=check` | XML `code=2` без PIN | smoke + `test_terminal_facing_check_flow` |
| `function=setup` + valid PIN/CSR | success path | `test_terminal_facing_setup_success` |
| `function=setup` network drop | safe retry cache | `test_terminal_facing_setup_network_drop_safe_retry` |
| legacy aliases (`Dispatcher.ashx`) | сохранены | router decorators |
| Service PIN API (`/pins/issue`, `/pins/by-operation`) | additive | smoke 404/403 + fixtures |

Breaking changes: **нет**.

---

## 7. Воспроизводимые команды

```powershell
git switch l4desk/l4d-17b-pb   # HEAD = 9ed5b22

cd ProcessingBackend/backend
uv run pytest tests/ -v
uv run pytest tests/test_ca_signing.py --basetemp=pytest_tmp_ca -v
uv run ruff check app tests
uv run ruff format --check app tests
uv run pyright app

# production smoke (from host with SSH key)
ssh -i d:\.ssh\id_ed25519 user1@87.242.100.34 "curl -sS http://172.19.0.5:8000/api/health"
# issue probes — см. §5, payload через base64→file, error paths only
```

---

## 8. Вердикт

**`ACCEPTED`** — все checks зелёные.

- Exact versions: API `0.1.0`, Alembic head `027`, image `user1-processing-backend:latest` (`e2194a9b3341`)
- Tests: 130/130 + ruff + pyright clean
- Provider probes: create/replay/conflict/expired/used/CSR/auth — all PASS
- Production smoke: redacted error-path probes PASS
- Rollback readiness: Alembic `027` downgrade SQL generation tested; no destructive change in this acceptance
- Consumer: `L4D-17C-IOT`

Acceptance-код не менялся. Commit/push — только данный отчёт.
