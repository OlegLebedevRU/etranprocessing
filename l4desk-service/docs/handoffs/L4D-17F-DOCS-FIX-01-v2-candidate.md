# L4D-17F FIX — detached candidate v2

<!-- HANDOFF:H-L4D-17F-DOCS-FIX-01-v1:BEGIN -->
```yaml
handoff_id: H-L4D-17F-DOCS-FIX-01-v1
status: CANDIDATE
contract_kinds:
- REPORT
- SEQUENCE_GATE
producer_prompt_id: L4D-17F-DOCS-FIX-01
producer_scope_project: l4desk-service
producer_report_path: l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-v2-report.md
producer_branch: release/l4tools-1.8.2-beta-1
producer_commit: a4b98e95e1493c76f0fdfc409a6441065f8e7020
report_commit: a4b98e95e1493c76f0fdfc409a6441065f8e7020
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: PB 027; IoT 0008_org_reservations
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-v2-candidate.md
artifact_paths:
- l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-v2-report.md
- l4desk-service/docs/handoffs/L4D-17F-MEDIA-PROVENANCE-02-report.md
- l4desk-service/docs/handoffs/L4D-17F-DEPLOY-EVIDENCE-01-report.md
- l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-report.md
- l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-supplement-2026-09-27.md
- l4desk-service/docs/handoffs/L4D-17F-MB-HUB-FIX-01-report.md
- l4desk-service/docs/handoffs/L4D-17F-MEDIA-ARCHIVE-EVIDENCE-01-report.md
- l4desk-service/docs/handoffs/evidence/17f-deploy-20260927/final-contract-gate.json
- l4desk-service/docs/handoffs/evidence/17f-deploy-20260927/media-build-provenance.json
- l4desk-service/docs/handoffs/evidence/17f-deploy-20260927/pb-source-fingerprint.json
- l4desk-service/docs/handoffs/evidence/17f-archive-20260927/manifest.json
- l4desk-service/docs/handoffs/evidence/17f-archive-20260927/checksum.sha256
- l4desk-service/docs/handoffs/evidence/17f-archive-20260927/samples.jsonl.gz
- l4desk-service/docs/handoffs/evidence/17f-archive-20260927/quality.jsonl.gz
- l4desk-service/docs/handoffs/evidence/17f-archive-20260927/summary.json
- l4desk-service/docs/handoffs/evidence/17f-archive-20260927/reproduce.py
artifact_sha256:
- 82b603df099f48e66f0ba86db362b1d041920ec47e14518ca962502daed76abb
- c774dbd5835460383b3d08cde90dbc0e4bd0ca05411268e25b9a10734e5fc854
- d26f966239e0e7fe32e852978fab67288720bdf2b7c9c108c758b76229409c55
- 8666252ec65e862f2d4e324ce239cc14383467cb22d4785ef579b1f4235716d5
- acf6a2c501efae934a33266c0233c0ff3b09160a70b2f149fd4cdf9296a848fb
- be5a9e6057755ec4671ddb8720591138f5a039b441a1243f76c0f791f00366ba
- 1e33a52f5be5409fb631edb1d41128d2ba673e4470e925424e3fd4b8330e8681
- ddb5d97380f623a415d5249488ebc7c9c5d5bd2cf9d2f05dd902ef03775c3703
- 61cce58d3de02fc4129e00d824ff2a9f0361ffa0f23770b7714dfbd240578faf
- ee021d9f888a2704f284a05e9ab963770cd6657a74813ea2031ce3755aee6cc7
- 0fdf5c04e8373b66ce9a448f038881588c7aa2a04e7aad259ca390a2171a642f
- 4e28d8c3692f405445ebc797cd8047c5d9c39a8ddc6b86bb5bf7dca9be3dd31c
- 74b36478417d11136c5a9f40439278c2dcda1534366db243834985cbd7fde010
- f9c061b6a0190d4fae7b76403a7adf867907387b54e0c0de2185051ecd2a00b7
- d7d041cdb87c8cd9174bb79e22497e0ee289cec2e6050ca480b46310e055ace3
- dcdb14c5db734d4dc54b23b565e1d2314ef0c6b5eac8a4484979547054071951
compatibility:
  backward_compatible_with: &id001
  - H-L4D-17ABC-CONTRACT-01-v1
  - H-L4D-17D-MEDIA-CONTRACT-01-v1
  - H-L4D-17E-CONTRACT-01-v1
  - H-L4D-17F-MB-EVIDENCE-CONTRACT-01-v1
  breaking_changes: false
  notes: Accepted Agent 1.8.2-beta-1 unchanged; technical pre-rollout acceptance, not global commercial activation.
deployment_status: DOCS_PUBLISHED
deployed_environment: documentation; production and isolated test runtime evidence in report
feature_flags:
  production_policy_enforcement: false
  production_registration: false
  production_billing: false
  production_yookassa: false
  production_entitlement_worker: false
  production_metering_close_worker: false
  production_iot_consumer: false
  isolated_test_policy_enforcement: true
  isolated_test_free_quota_tenant_ids:
  - 1000
  isolated_test_free_quota_seconds: 600
  isolated_test_entitlement_tenant_ids: []
  isolated_test_clock_offset_seconds: 0
  archive_purge: false
contract_payload:
  registration_id: R-L4D-17F-DOCS-FIX-01-v2
  registration_commit: 3de3524f35552b32c485a285a03be9f7768ea799
  required_handoff_ids: *id001
  sequence_gate_handoff_id: H-L4D-17E-MB-FIX-01-v1
  input_artifacts_verified: 13
  agent_release_baseline: 1.8.2-beta-1 accepted by owner as-is; no rebuild
  agent_runtime_version_evidence: operator attestation; package hashes historical, not a fresh terminal 1000007
    hash
  processing_backend:
    source_commit: 083138f223b723098e9a188803e3fc802e8a6011
    image: sha256:e2194a9b3341ec24d5b0d176d825f6879eff6d7c074bb107276473c28936e511
    schema: '027'
  iot:
    source_commit: 60f7762ec766e432bf372e255e94fdd33d3d91d2
    image: sha256:29aa88169ab8b051705044f0feb1ad8b4d5af5827bcef5294b437c7e29b9c431
    schema: 0008_org_reservations
  media:
    source_commit: c200d60485d32c805ae00530c57ad7a4b6b2b1ce
    image: sha256:f201ff382eaa9ca127681cee16314e86deffa9d2441f6c293f3e10ff57e61c9f
    binary_sha256: 1e7f48b11b518ebba4e63eaf84326632e1da1dac4cc024f8767490510be20f91
    proof: Independent Git-archive build produced byte-identical executable; not whole-image equality.
  menubuilder_production:
    source_commit: fb2273c0bb633426067b2a9487538d84155ef446
    image: sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090
  menubuilder_candidate_test:
    source_commit: 4184ee930e869ddfb044029512e51e7a69ed20f6
    image: sha256:fa37ec23d4c2a807b7209440af00a123bdecbb2a862a3e3e1cc1b3cd1e091345
    tests: 529 passed; Ruff app/tests pass; Pyright app and changed test 0 errors
  e2e_verdict: PASS within explicitly approved mock, short-period and synthetic-archive test scope
  critical_open_defects_in_candidate: 0
  media_soak: 'Historical session 462: 7492 seconds; current 10-minute traversal of 600-second TTL plus renew/stop
    regression and matched live binary.'
  financial_integrity: Duplicate online/webhook/poll preserved posting IDs; payment 5/transaction 9 debit=credit=10000
    kopecks; persisted Active repaired without balance/version change.
  archive: Synthetic manifest/restore 6/6; consumer contract replay/conflict/masking verified; no production purge
    or deployed Hub import claimed.
  next_gate: 18A historical H-L4D-17F-DOCS-v1 reference requires explicit controller correction before execution.
supersedes: []
known_risks:
- Production MenuBuilder has not received grace/payment recovery fixes; deploy tested candidate before enabling
  commercial flags in 18E.
- Production archive mount, permissions and backup readiness remain 18D rollout gates; archive worker and purge
  stay disabled.
- Archive consumer uses DB double; producer restore uses synthetic SQLite, not production PostgreSQL restore.
- Real YooKassa, grace email delivery, Windows 7/POSReady x86 runtime and mass 100 start/stop cycles were not exercised.
- Historical reports remain immutable BLOCKED_TESTS records; this final report closes their measured gaps within
  the operator-approved scope.
consumers:
- L4D-18A-SHARED
next_prompt_id: L4D-18A-SHARED
```
<!-- HANDOFF:H-L4D-17F-DOCS-FIX-01-v1:END -->

Independent controller acceptance is required; this candidate does not change the journal.
