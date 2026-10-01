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

IoT and MenuBuilder results will be appended after their separate steps.
