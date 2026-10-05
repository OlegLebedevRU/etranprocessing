# FM automatic provisioning / Leo4Proxy transport — 2026-10-05

## Intake
- User confirmed signing1.12.0; all19 timestamped EXEs valid, embedded130 files match staging.
- User requires automatic FM setup, every terminal HTTP flow through Leo4Proxy and common MQTT/RTP/FM deny.
- Owners: PB transport policy produces FM admission/storage endpoint; local Leo4Proxy forwards PB metadata
  with existing mTLS and S3 HTTPS via restricted CONNECT; l4con owns files/hash/receipts; setup owns dedicated root.
- No server file relay/fallback, no S3 client certificate, no insecure TLS, no arbitrary proxy destinations.
- Existing extra_service choice persists. MQTT topics/presence and common IoT lease stay unchanged.
- Signed1.12.0 must remain immutable; new1.12.1 packet and new operator signature required.
- Check x86/x64 native builds, local isolated provisioning/discovery/tunnel/policy tests;
  PB quality/full tests and standard accepted-source deployment for changed policy producer.
- Live terminal transfer/policy fault E2E remains separate from local fixtures.

## Work / evidence
- Signed1.12.0 setup SHA256677b62f048814318ba9cb44673720f89c346b6e4f9d9359ef482f7c8e16ebf7a verified;
  not published. Preparing separate1.12.1 instead of altering that signed artifact.
- l4con1.11.1 discovers its loopback API from ready matching-SN Leo4Proxy; L4FM_API_URL no longer read.
  Root defaults to C:\l4tools\fm, setup creates/protects it. Optional L4FM_ROOT is admin configuration.
- PB metadata uses local HTTP→Leo4Proxy→existing PB mTLS. S3 HTTPS uses WinHTTP named loopback proxy
  and restricted CONNECT, with end-to-end S3 TLS, no terminal certificate, cookie, authentication or redirect.
- PB policy produces fm_allowed and exact path-style storage host/port; native proxy fails closed on
  missing/stale/changed identity routing and common media deny. No arbitrary destination or server relay.
- Active storage socket participates in common policy cancellation; its bridge also checks FM policy
  every250ms, has45s deadline/70MiB TLS-wire bounds and one concurrent storage tunnel per process.
- PB requires fs.proxy capability: the former direct-HTTP agent is reported incompatible before acquire.
- Setup-root fixture initially failed WRITE_DAC-only ACL update (Win325); READ_CONTROL added.
  Non-elevated fixture uses explicit OWNER RIGHTS only in test to inspect/remove its protected temp directory.
  Production ACL remains SYSTEM/admins. Runner now propagates abnormal negative Windows exit codes.
- PB local ruff check/format/pyright passed; full265 passed/1 skipped. Existing async-mock/deprecation
  warnings remain; MB/frontend/shared unchanged, their checks not rerun.
- Final unified leo4proxy/l4con x86/x64 builds/tests passed. Named-proxy routing fixture proves no direct
  metadata/S3 fallback; CONNECT fixture checks authority/private-IP refusal, duplex bytes and active deny.
  Native policy tests include encoded/dot-segment FM route bypass refusal. Local proxy capability gate passed.
- Setup x86/x64 tests/build passed: dedicated root create/repair preserves data, protected fixture ACL,
  junction refusal, pipeline44 cases and existing readiness/service/certificate regressions.
- Suite1.12.1 unsigned setup SHA25621376a7071d45f9e08c0bf6df253eca0e1d436d0427b98ace4cfd4a1dfdecc86;
  all130 embedded files match staging. Signing input sealed; only two components replaced.
  Signed1.12.0 retained in runtime backup. Tracked proxy binaries restored to prior signed checkpoint;
  unsigned candidate lives only in ignored staging/artifacts until operator signing.
- Failed tests' eight exact empty fixture directories removed nonrecursively without ACL mutation.
- [MCP Ops Readiness: UNAVAILABLE]; SSH fallback preflight available RAM2140MiB/root48%/load0.32.
  Live terminal TLS/transfer/policy E2E not performed.

## Production server release
- PR31 accepted main d7b590bd070b6cbc2467ccd2c02e4c3519435469. Standard installed builder launcher
  processingbackend only: Linux quality checks/full265 passed1 skipped, immutable image built/published/pulled.
- Running PB digest sha256:3a747c4b0fe004cde73b1648cdc63412a8b317585c4e7de40a5e4225a9fa8b03;
  container068bfe2bc20fe68b6d6a70117b1d38f01ddfa28c7aa0b61f562bfad6bf99cc2e, health200.
- Persistent production Compose pinned to verified running digest; compatible schema032 unchanged.
  Read-only DB check fm_agents0/fm_operations0. No terminal operation, lease, test account or token created.
- Runtime configured-policy/in-memory fixtures: active FM true, inactive FM false/endpoint absent,
  former agent without fs.proxy incompatible, proxy-capable fixture ready. These are not authenticated
  terminal transport or file-transfer E2E. Initial health refusal recovered through standard retry.
- MB/app1/nginx/rabbitmq container IDs unchanged. No companion rebuild/recreation or nginx reload needed.
- Task temporary pin helper removed remotely; protected env/backups and immutable builder receipts retained.
- Final37-file source secret pattern scan0 matches; diff check and signing-wrapper AST parse passed.
  Post-commit sealed130 files/setup-resource hashes unchanged. Real1.12.1 signing/publication still pending.

## Operator handoff
Operator confirmed1.12.1 signed. Verified all19 timestamped EXEs, all130 embedded files and unchanged
other component bytes. Final signed setup SHA256838615b489a3cf7fa344e4244c63d145ee34772d3cc747dbf2e70d926a71adaa.
leo4proxy1.8.3.0/l4con1.11.1 match both architectures. No rebuild after signing; publication completed below.

Historical signing command, do not rerun against the published packet:
`& .\tools\release\Prepare-FmSignedRelease.ps1 -Mode Sign -PfxPath '<external PFX path>'`.
Password only through L4TOOLS_SIGN_PFX_PASSWORD. New signature required by native-windows-tool-change
after native source changes; do not rebuild the sealed components. After confirmation verify signatures,
timestamp/payload hashes, synchronize signed outputs/checkpoint, then publish via strict release workflow.

## Publication complete, 2026-10-05
- Signed checkpoint e98d14fc312ec11df24a4ff4e63d4cb08f2e9449 accepted through PR33;
  main merge3386e099642225e445c41fab6717e1996ffbf23c. Only manifest/checksums refreshed after signing.
- Strict publisher verified clean source, all19 valid timestamped EXEs, all130 embedded files and
  unchanged unrelated components. No unsigned/dirty bypass, rebuild or repack after signing.
- Suite1.12.1 published at2026-10-05T16:55:31Z. Registry digests and all three complete public HTTPS
  downloads verified by size/SHA256. Setup29775928 bytes,
  SHA256838615b489a3cf7fa344e4244c63d145ee34772d3cc747dbf2e70d926a71adaa.
- [Immutable publication record](../../../artifacts/l4tools/1.12.1.json) contains URLs and checksums.
- Production PB correction remains deployed. Terminal installation, live S3-through-proxy transfer,
  active common policy revocation and Windows7 acceptance are not confirmed by these release checks.
- Earlier pending/unsigned entries above describe historical preparation, not current release status.
