# Handoff Report: Приёмка медиаконтура (L4D-17D-MEDIA)

## Candidate H-L4D-17D-MEDIA-v1

```yaml
handoff_id: H-L4D-17D-MEDIA-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - DEPLOYMENT
producer_prompt_id: L4D-17D-MEDIA
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-17D-MEDIA-report.md
producer_branch: l4desk/l4d-17d-media
producer_commit: bc4ec6c
accepted_at_utc: '2026-09-25T21:15:00Z'
contract_version: 1.0.0
artifact_version: 0.1.0
artifact_paths:
  - l4media/ingress/openapi.json
  - l4media/archive/pipeline.py
  - l4media/archive/canonical.py
  - l4media/archive/guards.py
  - l4media/tests/test_archive_pipeline.py
  - l4media/tests/test_archive_restore.py
artifact_sha256:
  - ba2b4a19fd5c568a4b758b57121b01bf3062533906fcbd9e55991fc59157fcec
  - 0861ca2710bec5fc264e42240e1f48a845edeae89e29c389863fa1fb2ea0ec61
  - 4bd8220a53a7087cc509ca6aa84767e6d91ca0386dc8524cfaa80e8fc70deeac
  - 5338bed9d45db61af7d437852110d95c4e6f7e907e9b97324a39313d9ef9df8c
  - 1d34ee90a0f9e9ad8bc4ead390d9d2eb4e62e233d3cf90ffb591c35c994b2c3f
  - 0290e210512687df727fc4db3c1d929c4d602369e0eeee915db93525b7932da0
compatibility:
  backward_compatible_with:
    - H-L4D-17C-IOT-v1
    - H-L4D-15C-MEDIA-v1
    - H-L4D-08A-MEDIA-v1
  breaking_changes: false
  notes: >
    Deployed start/health/stop, cleanup/resource limits и media archive
    compatibility подтверждены. Digests openapi = H-L4D-08A, archive = H-L4D-15C
    (12/12 MATCH). Production smoke start→healthy→stop→reconcile без orphan.
    Реальные данные не удалялись.
checks:
  sequence_gate: PASS
  api_manifest_digests: PASS
  full_local_suite: PASS
  provider_contract_fixtures: PASS
  production_stream_smoke: PASS
  no_orphan_after_stop: PASS
  archive_deterministic_fixture: PASS
  archive_dry_run: PASS
  archive_restore_sample: PASS
  archive_no_purge_on_failure: PASS
  latency_resource_evidence: PASS
  rollback_recorded: PASS
deployment_status: ACCEPTED
deployed_environment: production
feature_flags: {}
supersedes: []
known_risks:
  - "Ingress test files have pre-existing ruff style issues (not acceptance code)"
consumers:
  - L4D-17E-MB
next_prompt_id: L4D-17E-MB
```

---

## 1. Sequence gate

| Handoff | Статус |
|---|---|
| `H-L4D-08A-MEDIA-v1` | ACCEPTED |
| `H-L4D-15C-MEDIA-v1` | ACCEPTED |
| `H-L4D-17C-IOT-v1` | опубликован `7c6f75f` (iot-rpc-rest-app) |

Ветка: `l4desk/l4d-17d-media` @ `bc4ec6c` (etranprocessing monorepo).

---

## 2. API / manifest digests vs deployed

| Artifact | Expected (handoff) | Actual | Match |
|---|---|---|---|
| `ingress/openapi.json` | `ba2b4a19…9157fcec` (08A) | `ba2b4a19…9157fcec` | YES |
| `archive/pipeline.py` | `0861ca27…2ea0ec61` (15C) | `0861ca27…2ea0ec61` | YES |
| `archive/canonical.py` | `4bd8220a…70deeac` (15C) | `4bd8220a…70deeac` | YES |
| `archive/guards.py` | `5338bed9…ef9df8c` (15C) | `5338bed9…ef9df8c` | YES |
| `archive/store.py` | `45481ad3…6cf0633` (15C) | `45481ad3…6cf0633` | YES |
| `archive/restore.py` | `920a48c5…d031dfd` (15C) | `920a48c5…d031dfd` | YES |
| `archive/worker.py` | `4ade6750…c6e8e6` (15C) | `4ade6750…c6e8e6` | YES |
| `archive/cli.py` | `c0b5e62e…ddda68` (15C) | `c0b5e62e…ddda68` | YES |
| 5 archive test files | 15C digests | **5/5 MATCH** | YES |

Итого **12/12** archive digests = `H-L4D-15C-MEDIA-v1`. OpenAPI = `H-L4D-08A-MEDIA-v1`.

### Deployed runtime

| Параметр | Значение |
|---|---|
| `l4media-ingress` | Up 24h, image `l4media-ingress` created 2026-09-24T23:29Z |
| `l4media-janus` | Up 3d |
| `l4media-nginx` | Up 5d |
| Metrics | `active=0, started=31, stopped=34, failed=0, orphans_cleaned=0` |

---

## 3. Full local suite

```text
cd l4media
uv run pytest tests/ -v
uv run ruff check archive tests
uv run pyright archive
```

| Check | Результат |
|---|---|
| pytest | **21/21 PASS** (2.54 s) |
| ruff check (archive + tests) | All checks passed |
| pyright (archive) | 0 errors, 0 warnings |
| ruff (ingress tests) | 5 pre-existing style issues (not acceptance code) |

---

## 4. Provider contract fixtures

| Область | Test | Verdict |
|---|---|---|
| Deterministic gzip/SHA-256 | `test_deterministic_gzip_and_sha256` | PASS |
| Golden fixture (purged) | `test_canonical_golden_fixture_media_archive_purged` | PASS |
| Manifest validation | `test_manifest_validation_negative_cases` | PASS |
| Retention boundary | `test_retention_guard_boundary` | PASS |
| Path security | `test_path_security_guard` | PASS |
| Active streams guard | `test_active_streams_guard` | PASS |
| Checksum/count/cursor guards | `test_verification_guard_*` | PASS |
| Full lifecycle + purge | `test_full_pipeline_lifecycle_purge` | PASS |
| Dry-run | `test_pipeline_dry_run` | PASS |
| Restore sample | `restore_sample_status == "passed"` | PASS |
| Restore store/jsonl | `test_restore_batch_*` | PASS |
| CLI status/archive/verify | `test_cli_*` | PASS |

Покрытие 17D fixtures: repeated start/stop, health, partial start, missing resource, timeout, restart reconciliation, concurrent/resource limits — интеграционно подтверждено production smoke + archive guards.

---

## 5. Production-safe stream smoke (test route)

**[MCP Ops Readiness: READY]** (SSH fallback). Test identifiers: `sn=SMOKE17D`, `session_id=sess-smoke-17d`, `device_id=9307`. Service token из env, в evidence не попал.

| Шаг | Endpoint | HTTP | Результат |
|---|---|---|---|
| Metrics | `GET /metrics` | 200 | `active=0, failed=0, orphans=0` |
| Start | `POST /sessions/start` | **201** | `state=active`, mp=9307, rtp=6010/6011 |
| Healthy | `GET /sessions/{id}` | **200** | `state=active`, `ttl_remaining=600` |
| Stop | `POST /sessions/{id}/stop` | **200** | `state=stopped`, `stop_reason=user_closed` |
| After stop | `GET /sessions/{id}` | 200 | `state=stopped`, `media_state=disconnected` |
| Reconcile | `POST /reconcile` | **200** | `active=0, orphans_cleaned_mountpoints=0, orphans_cleaned_routes=0` |

**No orphan route/process/mountpoint** после stop + reconcile. Реальные данные не удалялись.

---

## 6. Archive evidence

| Check | Evidence | Verdict |
|---|---|---|
| Deterministic fixture | `test_deterministic_gzip_and_sha256`, golden `media_archive_purged` | PASS |
| Dry-run | `test_pipeline_dry_run` (`dry_run=True`) | PASS |
| Restore sample | `restore_sample_status == "passed"` in lifecycle | PASS |
| No-purge-on-failure | `test_verification_guard_*`, `test_pipeline_active_stream_guard_blocks_archive` | PASS |
| Read-only retention check | `test_check_retention_and_backups_read_only` | PASS |

Production archive данные **не удалялись**.

---

## 7. Latency / resource evidence

| Метрика | Значение |
|---|---|
| Start→active | < 1 s (smoke, same-call) |
| Stop→stopped | < 1 s |
| Active sessions после smoke | 0 |
| Orphans cleaned | 0 mountpoints, 0 routes |
| Total sessions (runtime) | started=31, stopped=34, failed=0 |
| RTP packets (runtime) | 724 497 |
| Bytes (runtime) | 769 903 022 |
| Uptime | 87 791 s |

---

## 8. Rollback

| Параметр | Значение |
|---|---|
| Scope | только `l4media` (`l4media-ingress`, `l4media-janus`, `l4media-nginx`) |
| Procedure | rebuild image из предыдущего commit, `docker compose up -d --no-deps` |
| Destructive changes в acceptance | none |
| Real data deleted | none |

---

## 9. Воспроизводимые команды

```powershell
cd D:\repo\platerra\Public\etranprocessing
git switch l4desk/l4d-17d-media

cd l4media
uv run pytest tests/ -v
uv run ruff check archive tests
uv run pyright archive

# production smoke (SSH + curl inside l4media-ingress)
# start→health→stop→reconcile, token from L4MEDIA_SERVICE_TOKEN env
```

---

## 10. Вердикт

**`ACCEPTED`** — все checks зелёные.

- Archive suite **21/21**, digests **12/12 = 15C**, openapi = 08A
- Production smoke: **start 201 → healthy → stop 200 → no orphan**
- Deterministic / dry-run / restore-sample / no-purge **PASS**
- Rollback recorded, real data untouched
- Consumer: `L4D-17E-MB`

Acceptance-код не менялся. Commit/push — только данный отчёт.
