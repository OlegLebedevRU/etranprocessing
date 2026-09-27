# L4D-18A-SHARED-FIX-01 — detached candidate

<!-- HANDOFF:H-L4D-18A-SHARED-v1:BEGIN -->
```yaml
handoff_id: H-L4D-18A-SHARED-v1
status: CANDIDATE
contract_kinds:
- SCHEMA
- DEPLOYMENT
producer_prompt_id: L4D-18A-SHARED-FIX-01
producer_scope_project: shared/etranprocessing_db
producer_report_path: shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-report.md
producer_branch: release/l4tools-1.8.2-beta-1
producer_commit: 537a1e493c83d1fa8e8cb765228be8d1b24a1d62
report_commit: 2b38855000d67f06269ddf40e6305382abe2d3a9
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: L4D-04A-v1
artifact_version: 0.1.1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-candidate.md
artifact_paths:
- shared/etranprocessing_db/__init__.py
- shared/etranprocessing_db/base.py
- shared/etranprocessing_db/l4desk.py
- shared/etranprocessing_db/models/__init__.py
- shared/etranprocessing_db/models/auth.py
- shared/etranprocessing_db/models/billing.py
- shared/etranprocessing_db/models/catalog.py
- shared/etranprocessing_db/models/email.py
- shared/etranprocessing_db/models/finance.py
- shared/etranprocessing_db/models/iot.py
- shared/etranprocessing_db/models/l4desk.py
- shared/etranprocessing_db/models/menu.py
- shared/etranprocessing_db/models/org.py
- shared/etranprocessing_db/models/payment.py
- shared/etranprocessing_db/models/telemetry.py
- shared/etranprocessing_db/models/terminal.py
- shared/pyproject.toml
- shared/uv.lock
- shared/docs/l4desk/schema-v1.json
- shared/docs/l4desk/schema-v1.md
- shared/docs/l4desk/package-source-v011.json
- shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-report.md
- shared/docs/l4desk/handoffs/evidence/18a-release-checks.json
artifact_sha256:
- 043f21a04d9137f2eb021b7fa88040666256cb35e563af1fb7b74955a4378261
- 987edfd9dfe38b2c49492c7d1a4e774015d16b72e2281b018e4160be6b47d560
- a75ecd79403083de55f7edb929693bf4e5e5aafc2d820ab7b2b23d856a8afd72
- 7bad42ec3cc900f2d668185222de2f74e67f94dd823daa033d49d2ecd6c4a88b
- 903916d7659e364c6f7c1fcee53c7491bcae5fdbae178005102acc77c45f3a1b
- f4260965741ed9e79fe40de06706f4f0b84f0232920ea444292df6f13b7837ba
- 1a1ce3ed54a80743349282f7a43955c6e6ad5905d9d254f18b832ba2ac5393a6
- 2424e6bffb76f2b81ca2482c836dc7cf6c7f343030dd8e01935014879b670392
- fc9d1458da9a8c5d6a609c72f7a9a7b547f07a531988a2442f907714c382b4f5
- 332242fc24e09db812ece65132487a5907b0d0c4766129a57d6ef5e31b3a5349
- a9ad8d88128cbfffcbd10785aeb5ffc0c8d4c3ef94930dfaa81358b2086044a3
- 2b60bd44d4c0f4029391be6f0686f3890208f783ae73e466a4a86b9dd04add9c
- 5bbb799776427b4cd5b2537620ab0494425e9b2340fba8e255afc8239dc9556a
- 8c4ff22f105a19d0bff9c77910e5e9f62a01c45a8798a105bf9c333afa5470fc
- dd20ccd8862cd0b36a11a03d2312595c29861856073620f6c02d6759b8cbb388
- 332d5266c2fc1711caf6862d3be4e9812de6fc91d74f9e6e558c75b70afe34ff
- 7e63aadd922d7df49bb029e364ab719233b3a9921a143504918dc8c863d45117
- fd69b07afee90d75e99a4adcdd99309e6a9726e66af5ff38b4380551f48899c0
- 52f481dd3d9985c54b5388a1d9e63062a8fdbe626870b58a83b3461c3e08e49f
- d80626d6f84bab0e44d2236a172b3ffa385c7779bccd26b42c2947b071f77935
- 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
- c7a6bebcba52dce053d01715699421d04edb301b0e3d858d1afba508d393b12a
- 454992eb726cdf4ac0b03df5eaa65bc390aa2408343048f22003f57cd57bc6ff
compatibility:
  backward_compatible_with:
  - H-L4D-04A-SHARED-v1
  - H-L4D-17F-DOCS-FIX-01-v1
  breaking_changes: false
  notes: Exact 0.1.1 Git source reused; 20 source/config/schema bytes and declarative metadata unchanged. No migration
    or new behavior.
deployment_status: PUBLISHED
deployed_environment: artifact-registry
feature_flags: {}
contract_payload:
  registration_id: R-L4D-18A-SHARED-FIX-01-v1
  registration_commit: 4ed4c78d19d4d443da21a91c5fc714a57d2038c0
  package_name: etranprocessing-db
  package_version: 0.1.1
  package_delivery: git-source
  package_url: https://github.com/OlegLebedevRU/etranprocessing/tree/537a1e493c83d1fa8e8cb765228be8d1b24a1d62/shared
  package_source_commit: 537a1e493c83d1fa8e8cb765228be8d1b24a1d62
  package_source_manifest_path: shared/docs/l4desk/package-source-v011.json
  package_source_sha256: 364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6
  source_files_verified: 20
  schema_tables: 31 legacy + 23 opt-in; no changed metadata
  consumer_locks:
    ProcessingBackend/backend: 0.1.1 editable ../../shared
    MenuBuilder/backend: 0.1.1 editable ../../shared
  checks:
    shared_pytest: 65 passed with local --basetemp
    ruff: check and format passed; 22 files unchanged
    pyright: 0 errors 0 warnings
    build: wheel and sdist built twice byte-identical; local verification artifacts only
  local_verification_artifact_sha256:
    wheel: f092073efd8506297e450a0e7e928584c9f3a4c103594b44747814bb067f2be2
    sdist: de76e01fea8b8b8818e2226fa823a4ba9194446fba44314c3c343f62ae55815d
  runtime_deployment: none in 18A
  rollback: Keep immutable 0.1.1 for current consumers/schema 027. 0.1.0 is previous source baseline only before
    04A integration; do not drop populated tables.
supersedes: []
known_risks:
- Wheel/sdist are local verification artifacts; no Python index publication is claimed or required by accepted 04A
  Git-source delivery.
- A standalone downgrade to 0.1.0 after schema 027 and 0.1.1 consumer imports is unsafe; roll back compatible consumer
  image/config while retaining the additive schema.
- First pytest run failed from Windows system temp PermissionError; rerun with project-local --basetemp passed all
  65 tests.
- 18A verifies package artifacts; PB and MB runtime rollout/health are separate 18B/18E gates.
consumers:
- L4D-18B-PB
- L4D-18E-MB
next_prompt_id: L4D-18B-PB
```
<!-- HANDOFF:H-L4D-18A-SHARED-v1:END -->

Independent controller acceptance is required before starting 18B.
