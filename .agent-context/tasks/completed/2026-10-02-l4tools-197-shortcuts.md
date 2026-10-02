# l4tools1.9.7: supervised quick actions

## Intake / scope
- Owner: l4superv supervised launch; consumer: existing l4desk1.9.3.
- Request: enable approved F12/Win+D/Alt+F4 for all terminals by default.
- Change/build only supervisor and setup; reuse all other signed1.9.6 components.
- No MQTT client/protocol change, live tests, service/certificate/runtime replacement.
- Lease/session/display/secure-desktop/Alt+F4 target guards remain unchanged.

## Changes
- l4superv1.9.6 appends the three allow flags after configured l4desk arguments.
  This covers old JSON retained during Upgrade/Repair, without rewriting user config.
- l4setup/suite1.9.7 version/resource/assembly identity synchronized.
- `build.cmd supervisor`: isolated /MT x86/x64/default build, no l4install rebuild.
- `Complete-SignedRelease.ps1 -SignOnly l4superv`: sign changed supervisor only
  plus final universal setup, mandatory SHA256 RFC3161 timestamps. Preserve every
  other staged EXE. Package hashes/inventory still checked, capture source gate skipped.

## Evidence / readiness
- [x] Local log: two shortcut_action commands reached773; historical NACK not observed.
- [x] Runtime argv had no allow flags; config/handler all-false policy explains refusal.
- [x] Supervisor-only x86/x64 build exit0; default is x86. Initial batch target routing
  failed before compilation, then corrected; no failed source compilation.
- [x] Setup x86/x64/default build exit0; PE1.9.7 and assembly1.9.7.0.
- [x] Both embedded payloads match staging (61 files each); package inventory1.9.7.
- [x] All eight other EXEs per architecture match the previous signed release
  byte-for-byte. Initial generic staging pulled unsigned third-party source binaries;
  package hash comparison caught this before signing, and all unchanged EXEs were
  restored from the backed-up signed1.9.6 staging and repacked.
- [x] Signing script syntax, default-x86 equality, launch buffer bound and changed-text
  credential scan passed. Other tools were not rebuilt or functionally retested.
- [x] Operator signed; updated x86/x64 supervisor and universal setup Authenticode
  Valid, mandatory timestamp present, signtool /pa /all /tw passed. Same signer.
- [x] Signed embedded payload61 files each and inventory9 EXEs each match staging;
  all other eight EXEs each retain the exact signed1.9.6 bytes.
- [x] Signed supervisor synchronized to x86/x64/default outputs; no rebuild.
- Setup:29655096 bytes, SHA256
  `1ddc01ef388244f0608d9849e9d38cb8a77ef15cdbef6dc948c498f7db11faeb`.
- [x] Clean signed checkpoint `04b20972f74046d3fc0018ba76f524b2db83a42e`
  pushed to main; manifest signed=true/dirty=false, setup hash unchanged.
- [x] Three PUT200/Digest checks passed. Python full GET was truncated
  (28677163/29655096 bytes), publisher refused recording. Full direct curl NO_PROXY
  downloads of all three files passed exact size/SHA; downloaded setup Valid/timestamp
  and signtool passed. Standard publisher record ran only after these checks.
- Release: [signed installer](https://l4tools-generic.ar.cloud.ru/l4tools/1.9.7/l4setup.exe),
  [record](../../../artifacts/l4tools/1.9.7.json). No overwrite/allow-dirty/source rebuild.
- Public manifest4721 bytes SHA25695f62e9f084f0c1559220adb9c2d7008ffa88fd2118d13c9e796812498a13c39;
  sums165 bytes SHA256cef36da48928959c95c30843ea9b5e9561639177747037f1dfe0f7e33439a7b6.
- Publication evidence in ignored backup/public-verified; existing signed1.9.6 remains
  installed on773. No services/certificates changed. Operator may install/test1.9.7 later.
- [ ] Live input: intentionally deferred by user. F12 requires a responsive focused
  application; Alt+F4 must target an allowed disposable application, never an editor
  with unsaved work. Win+D does not guarantee a visible change in every kiosk shell.

## Signing command
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'D:\work\etranprocessing-mcp-user-events\tools\release\Complete-SignedRelease.ps1' -PfxPath '<path to signing PFX>' -Version '1.9.7' -SignOnly l4superv
```
Use the existing operator PFX path; password only via L4TOOLS_SIGN_PFX_PASSWORD.
No source/component rebuild after signing. Backups/evidence in ignored
`tools/dist/.runtime-backup/20261002-shortcuts-197/`.
