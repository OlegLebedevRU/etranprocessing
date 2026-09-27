# Candidate H-L4D-18B-PB-v1

This DETACHED_V1 candidate is proposed for independent controller review.
Its own path, digest and commit are intentionally absent from the block.

<!-- HANDOFF:H-L4D-18B-PB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-18B-PB-v1
status: ACCEPTED
contract_kinds: [DEPLOYMENT, REPORT]
producer_prompt_id: L4D-18B-PB-FIX-01
producer_scope_project: ProcessingBackend
producer_report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-report.md
producer_branch: release/l4tools-1.8.2-beta-1
producer_commit: 67827b607e64d771e368a0a43e1b6c0f09087dd8
report_commit: bcd9719106b94f2e10fd4346f73acf7132ce32af
contract_version: 1.0.0
schema_revision: "027"
artifact_version: 0.1.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-candidate.md
artifact_paths:
  - ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-report.md
  - ProcessingBackend/backend/Dockerfile
  - ProcessingBackend/backend/app/routers/certificates.py
  - ProcessingBackend/backend/app/dependencies.py
  - ProcessingBackend/backend/alembic/versions/027_add_l4desk_and_fin_ledger.py
  - ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf
  - ProcessingBackend/backend/.env.example
artifact_sha256:
  - fb15a37985e6c77cc6d20a8c620e196ce2b01a358e5c105772c49dc977906de1
  - 14154b9485efe6d240a682fb122daa7030b22e94107dd1b4c2e1fadbb7cc65ab
  - 0a0e925b1dfad9c6b96f5063cc33b5bea822cd558062681d9b011f0fe02ed93a
  - dff777bcc6b875875e0812d39eb237ea7ce7a10ff990da6ae48b49e3cc2efa78
  - bcf74175aa1bbaf870cfcaba9faa63d064ac33074ae1e788f720ef33ae019ed2
  - d896848147230f514bd76f2e26b0269bbe9ae3c868367fe083942f2ce7e43d13
  - b641f7cedcb0420a9771c07dff822b774be8a669aa3d409f2ca30150a35efcb7
compatibility:
  backward_compatible_with:
    - H-L4D-17B-PB-v1
    - H-L4D-18A-SHARED-v1
  breaking_changes: false
  notes: >
    Existing terminal certificate XML and service PIN contract 1.0.0 are
    unchanged. Public PIN routes are denied at ingress; the internal service
    API requires a private credential. Additive schema 027 was already head.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags: {}
contract_payload:
  registration_id: R-L4D-18B-PB-FIX-01-v1
  required_inputs:
    - H-L4D-18A-SHARED-v1
    - H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1
  pb_source_commit: 551f5c7998bc20f5361cbdf10709e6d0b1c7f995
  source_archive_sha256: 1ef3130a49573016cdbf4c63d81b5b56a0960d39d2e824203bd176aef61e12de
  production_image: sha256:8b61162f2b7544a9f733bf4bdddf1bf063c3e3e99034438c5e82f82e042ae43a
  image_tag: user1-processing-backend:18b-551f5c7-gitraw
  processing_backend_package: "0.1.0"
  shared_package: etranprocessing-db==0.1.1
  shared_source_commit: 537a1e493c83d1fa8e8cb765228be8d1b24a1d62
  alembic_head: "027"
  migration_action: already_at_head_no_ddl
  pin_contract_version: 1.0.0
  service_auth_configured: true
  service_auth_value_exported: false
  public_pin_ingress: nginx_404
  ingress_config_sha256: d896848147230f514bd76f2e26b0269bbe9ae3c868367fe083942f2ce7e43d13
  backup_sha256: 4ff0d4313a435bd99377417b4fcadbd9399a66affc414b9ed4dedf38f027b846
  rollback_image: sha256:e2194a9b3341ec24d5b0d176d825f6879eff6d7c074bb107276473c28936e511
  rollback_status: READY_NOT_EXECUTED
  checks:
    contract_gate: 12_of_12_git_raw_artifacts
    local_suite: 130_passed
    ruff: passed
    pyright: 0_errors
    package_build: wheel_and_sdist
    image_build: passed_git_raw_source
    nginx_config_test: passed
    health: HTTP_200
    schema: 027_head
    terminal_xml_check: HTTP_200_code_2_without_pin
    internal_pin_without_token: HTTP_401_SERVICE_AUTH_FAILED
    internal_pin_authorized_missing_operation: HTTP_404_OPERATION_NOT_FOUND
    internal_pin_authorized_missing_terminal: HTTP_404_TERMINAL_NOT_FOUND
    public_pin_route: nginx_HTTP_404
supersedes: []
known_risks:
  - Live PIN issuance and positive terminal enrollment were not performed; safe error paths and local contract tests passed.
  - The next IoT consumer must receive the private service credential through approved operations before using PIN API.
  - The local suite reports 20 existing asynchronous mock and TestClient deprecation warnings.
consumers: [L4D-18C-IOT, L4D-18E-MB]
next_prompt_id: L4D-18C-IOT
```
<!-- HANDOFF:H-L4D-18B-PB-v1:END -->
