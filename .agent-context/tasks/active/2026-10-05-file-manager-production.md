# FM production release — 2026-10-05

## Intake
- User explicitly authorized production deploy and preparation of l4tools signing script.
- Standard builder→registry→production flow; separate IoT master; only PB/MB/frontend/app1 and FM ingress.
- MCP Ops unavailable; SSH fallback. Initial production available RAM2076MiB/root46%/load0.21.
- S3 profile supplied by user; values never printed/committed. User approved C:\l4tools\fm.
- l4tools1.12.0 unsigned incremental packet, l4con1.11.0; native publication waits for operator signing.

## Preflight
- Origin main d871997 / IoT master8c2be80; GitHub credentials and SSH connectivity verified.
- Existing FM temp bucket discovered through allowed bucket inventory; profile example URL is an alias, not bucket name.
- Enabled versioning on FM-only bucket; preserved existing public access block and CORS/lifecycle,
  added Classic production origins and noncurrent FM-prefix cleanup1day.
- Synthetic local S3 probe passed checksum rejection, zero-byte, HEAD SHA256/VersionId, pinned GET after latePUT;
  all3 created versions removed. This is provider integration, not terminal E2E.
- l4con x86/x64 tests/build and setup x86/x64 build passed;130 embedded files matched staging.
- Signing packet sealed in ignored tools/dist/fm-signing-input.json. Script prepares and signs incrementally
  through existing Complete-SignedRelease.ps1; no private keys/PFX copied into repository.

## Release evidence
- Root main `62ae8e51a19abc7f50b7c79074f77503fbc5c104`, accepted PR29.
- IoT master `f4cd4128e4f0caa33788918692f97603c6061687`, accepted PR102.
- Standard installed launcher built/tested/published/pulled root components; external registry_app1.py
  built IoT from clean accepted master, deployed via the repository image deployer. No source hotfixes.
- Immutable production images:
  - PB: `sha256:bdf0dba2fb8b07d544b222e7b63796423721a032aebd5ed31a26f56eb30b20ed`
  - MB: `sha256:3e9252adb042dfd4ac1abc2f51868373a7a85d7d5a1de5c3076819617a4114cb`
  - frontend artifact: `sha256:e96b5abc940eb465567de0d611abeb66f3f406dabee49da3562472edfdfd5479`
  - app1: `sha256:1547da7ced049cfcf23f1b002d81a3a4aafb35961ec5663cc26f85c8d4120059`
- Production Compose pins running PB/MB/app1 digests; frontend dist replaced through artifact flow.
  Other services were not recreated; nginx-mutual was tested/reloaded, nginx-default not restarted.
- FM secrets persisted in owner-only raw env files; S3 credentials only PB, not MB/IoT.
  Terminal root policy is C:\l4tools\fm. Terminal service configuration/install remains separate.

## Verification
- Builder: PB256 passed/1 skipped; MB704/22; IoT510/33; frontend78 unit tests/build.
  Python quality checks passed. Local browser5 scenarios passed; shared65 tests passed.
- Disposable PostgreSQL18 full prior ORM schema stamped031 then upgraded032: all prior tables,
  FM tables/indexes/64MiB constraint verified. Isolated container/network/volume removed.
- Standard production migration applied031→032; actual DB revision032, fm_agents=0/fm_operations=0.
- PB/MB/app1 health200; MB→PB authenticated readiness200/not_registered/available=false;
  MB→IoT read-only nonexistent FM lease404. No terminal task/lease/test user or ad hoc JWT created.
- Public /files?profile=classic and /files?profile=l4desk returned200 on the configured portal.
  Anonymous MB FM401; unauthenticated PB internal401; agent ingress without client cert403;
  private internal ingress404. nginx -t passed before reload.
- Ingress checked using explicit configured CA and localhost address resolution with TLS verification.
  Windows public curl hit CRYPT_E_NO_REVOCATION_CHECK; public self-IP hairpin timed out.
  These attempts do not establish external agent mTLS acceptance. l4desk.ru resolved builder and404;
  deployed portal route is the existing dev.leo4.ru:3000 /files.
- Provider versioning/CORS/lifecycle configured on dedicated FM temp bucket; synthetic3 versions
  cleaned after probe. Actual PB runtime versioning/presign worked without creating a file operation.
- Final host safety: available RAM2155MiB, root48%, load0.14 (15:53 UTC).

## Native handoff / open gates
- Suite1.12.0/l4con1.11.0 unsigned incremental packet sealed:130 payload files, unchanged other signed tools.
  Setup unsigned SHA256 `b443c84ae42ff927d2464d65545aed2588faacf3ded45d09bcabb3fe7b50441c`.
- Existing signed1.11.0 preserved in ignored runtime backup. Do not publish this unsigned setup.
  Operator command from this worktree:
  `& .\tools\release\Prepare-FmSignedRelease.ps1 -Mode Sign -PfxPath '<PFX outside repository>'`.
  Password only via L4TOOLS_SIGN_PFX_PASSWORD. Real signing has not run; script AST and missing-PFX
  fail-before-mutation check passed. Do not rebuild sealed components after signing.
- After verified signature/publication/install: provision existing C:\l4tools\fm plus L4FM_ROOT
  and L4FM_API_URL on L4Con service. Until then preflight refuses unregistered/incompatible agent.
- Open: live agent mTLS/MQTT/common-lease/cancel/disconnect E2E, Win7/POSReady, drain timing,
  payment load canary, actual lifecycle expiry, receipt-crash/orphan staging cleanup policy.
- Migration is additive, but old MB schema031 admission is incompatible with032. Roll forward a
  compatible consumer or close FM; do not blindly roll back MB to its prior schema031 image.

## Failed attempts / cleanup
- Initial IoT builder invocation lacked uv Python install-directory permissions; rerun with installed
  launcher's standard uv/PATH environment succeeded, without changing builder tools.
- Initial immediate health connection refusal recovered through deployer retry; final health200.
- Provider profile example alias was not a bucket; dedicated existing FM bucket selected by inventory.
- First native preparation compared old manifest with new setup; restored task backup, generated
  unsigned manifest before integrity check, reran complete preparation successfully.
- Secret transfer JSON removed locally/remotely. Permanent protected env, rollback backups, release
  receipts and sealed unsigned signing packet are retained. No keys/PIN/JWT/file contents in handoff.
- Deployment helper ruff check/format and pyright passed; final changed/new8-file secret pattern
  scan found0 matches; git diff --check passed. Task-owned remote helper scripts removed.
