# Consolidation into base branches: l4media → IoT75 → MenuBuilder

## Task intake

- User authorized staged integration/push of l4media, IoT event75 after contract
  review, then MenuBuilder. No deployment or native tools changes.
- etranprocessing starts from origin/main2a2e59e; IoT from origin/master3e6f997.
- Media owns source-build/registry scripts; IoT owns event ingestion/storage;
  MenuBuilder owns consumer authorization and its acceptance records.
- Invariants: keep current ingress/tenant/history behavior, immutable historical
  handoff SHAs, no private credentials in Git, no production changes.
- Validation: component-local tests/quality, secret scan and remote base SHA.
  Builds/E2E are separate from claims in historical accepted release evidence.

## l4media

Selectively restored source build, immutable Janus baseline, image-layer scan,
LF attributes and accepted18D historical reports from l4desk/l4d-18d-media.
Did not restore older ingress C/lifecycle/tests or roll back current image pins.
Compose allows env image overrides but retains current pinned defaults.
Private Janus config rendering is restored; public example credentials removed.
Baseline Janus lives at registry root independently of ingress namespace.
The scan fails if no supported image layers were inspected; synthetic tests
cover .env, root .ssh, private PEM and harmless source-format constants.

Checks: WSL Linux C unit suite/build passed after installing libhiredis-dev;
bash/sh syntax passed; scanner pytest5 passed, Ruff/format/Pyright passed.
Local Docker engine unavailable, so image smoke/layer scan of actual images
not run. Janus was not rebuilt; production and builder were not accessed.
Historical18D handoff is preserved as prior release evidence, not current E2E.

Published l4media step: e5dcc6db0d4d20a6852541b19dcabe40e926b681 in main.

## IoT event75 (2026-10-02)

Published 987c5badbd810b55642be6c21870f5c9f0027640 in origin/master.
Updated the current MQTT5 contract with inventory444/package445 and existing
certificate tags. Old320ceac MQTT3 fallback decoder not copied; normal collector
and tenant isolation unchanged. Only75 is exempt from evt/activity billing.
Current result received_at tracking and durable RPC result path retained.
Changed subscriber quality required explicit missing-correlation REQ/RES refusal
and specific parser catches/debug logging; added tests and preserved intentional
nonfatal billing/optional relay behavior. No migration/native change/deployment.
Full pytest463 passed/7 skipped; Ruff/format/Pyright passed on changed sources.
Six history DB tests need explicit local DSN and were skipped; no live MQTT E2E.
Detailed integration record: IoT docs/handoffs/2026-10-02-event75-consolidation.md.

## MenuBuilder

Producer04560ca is already reachable from main. Restored only final historical
18D-AUTH report/candidate from97e39dd, with original immutable producer/report
SHAs and acceptance wording. No source/env changes or new credential rotation.
Report SHA25661d74d4ff286f8d31c3d842b04bcedd0b96224425e7c1fe2f711137c00c0d010
matches candidate. Restored media report/evidence hashes also match its candidate.
The report distinguishes producer acceptance from pending controller acceptance;
consolidation does not rewrite those historical verdicts or prove current runtime.
MB/PB tests/builds not run: this step changes documentation only, per AGENTS.

## Completion boundaries

All three selected stacks are integrated in their base branches after separate
checks. No force push, production deployment, native tools update, or Janus rebuild.
Unrelated landing work and controller prompt edits remain outside this scope.
