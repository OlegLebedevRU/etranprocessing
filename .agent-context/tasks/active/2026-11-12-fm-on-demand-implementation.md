# FM on-demand implementation

## Intake and invariants

- User authorized full implementation, release, deployment and pilot773 only.
  MQTT type extra_service and repeated UAC explicitly confirmed.
- Owners: PB terminal identity, readiness proofs, fm_agents and Alembic;
  MB tenant/user/view admission and UI; IoT existing MQTT dispatcher/shared lease;
  l4tools native isolation, policy, filesystem and worker lifetime.
- Flow: explicit UI -> MB -> PB pending -> IoT fmc probe -> L4Con -> mTLS PB
  result -> owner-bound single-use proof -> existing reservation/start/ACK.
- Preserve current SN/serial/activity checks, no auth cache; no new MQTT topic,
  client or retained command. Existing svc presence and lease/drain semantics.
  File bytes remain S3-only; metadata helper lifetime is bounded30s.
- Original dirty workspaces preserved. Server based on main9eaf7e0,
  IoT on master3b735ef, native l4tools on main8391be2.

## Completed local checks

- PB:399 passed,2 skipped; subsequent identity-lock regression91 passed.
  Isolated PostgreSQL18 verifies034->035, rollback/reapply and concurrent
  proof consumption. Production database not used for these tests.
- MB:1232 passed,25 skipped; subsequent contract/bridge focused tests passed.
- Shared:65 passed, schema035 and package source provenance captured.
- UI:161 unit tests,13 browser tests and final production build passed.
  Ten-minute browser clock advance asserts zero idle FM requests. This is
  simulated browser evidence, not a hardware idle measurement.
- IoT:1218 passed,13 skipped. Probe dispatch uses existing fmc, no lease.
- Ruff/format/Pyright passed for modified Python owners; rollout helper5 passed.
- Native implementation moved to actual owner OlegLebedevRU/l4tools in
  `.worktrees/fm-on-demand-tools`; old server native files restored.
  L4Con1.12.5 x86/x64 compiled; administrative tests being completed.
  Controller262 Python tests passed. Preserve signed55 baseline; unchanged
  executables reused byte-for-byte. Candidate65 registry slot was absent.

## Pending release and hardware acceptance

- Publish reviewed sources in all three owning repositories; build server
  images through standard builder/registry, production pulls exact digests.
- Deploy MB bridge accepting034/035 first, PB with migration035, IoT dispatcher,
  UI and signed native pilot773. Preserve actual Compose overlays, environment,
  mounts, network and neighboring containers. No fleet native rollout.
- Installed773 observed suite55/L4Con1.12.4. Preserve cert/private key and config.
- Real pilot idle/probe/start/ACK/renew/close, no idle respawn and bounded fault
  behavior remain pending. Do not conflate local tests with deployed E2E.
- Cleanup only temporary resources created by this task, preserving evidence.
