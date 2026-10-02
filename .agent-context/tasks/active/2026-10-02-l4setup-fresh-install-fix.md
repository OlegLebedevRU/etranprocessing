# L4 Tools 1.9.5 — fresh-install correction

## Task intake
- Scope/owner: native l4setup, l4superv bootstrap, packaging/inventories; current capture source retained.
- Producer → consumer: setup → bounded hidden supervisor child → existing Mosquitto config generator; setup → SCM → services → local health probes.
- Invariants: existing configurations/certificates retained; bootstrap no services/state/CA/network; no MQTT changes or new connections.
- Base: bee59fa7, implementation checkout D:/work/etranprocessing-mcp-user-events. Original dirty checkout untouched.
- Evidence: operator terminal35 Win10Pro19045 x86 fresh install fails26 after successful enrollment, then24 on missing broker config; operator supervisor startup restores services/config.
- Plan/checks: actual engine + certificate-phase regression fixtures, x86/x64 builds, bootstrap in temporary paths, current capture test suite, exact PE/staging/embedded inventories, mandatory signed completion.

## Implemented
- Certificate verification before service startup; proxy ready/standby wait after.
- Supervisor --prepare-mosquitto --dest creates only missing local config via existing generator; bounded hidden setup child.
- STOPPED with zero SCM exit code is immediate startup failure.
- Summary schema2 real failure codes/SCM states, not_run/null; installed vs target versions. UI distinguishes missing EXE and missing PE metadata.
- Payload installs component inventory; signed completion regenerates hashes. Release manifest validates two architectures and exact setup version; embedded integrity gate covers every file and current capture PE sections.
- Versions: setup/package1.9.5, superv1.9.4; capture1.0.0.0 current source, unchanged runtime. SBOM packaging metadata updated.

## Verification and remaining work
- [x] Native seven-component x86/x64 build and setup x86/x64 build.
- [x] Both arch: engine11 cases, certificate phase2 cases, stopped-zero-exit regression, existing8 unit tests.
- [x] Real superv bootstrap x86/x64: fresh/existing config, spaces, invalid path, no state file; fixtures cleaned.
- [x] Capture129/129; current source preserves MF stabilization plus later profile tuning.
- [x] Both embedded payloads61 files match staging; capture matches fresh build.
- [x] Operator signing; all19 EXEs Valid Authenticode/same signer/RFC3161, signtool /pa /all /tw and SHA256SUMS passed. Both61-file embedded payloads match signed staging and current capture PE sections.
- [ ] Real corrected Repair/Upgrade, fresh x86 install E2E in isolated terminal/VM.
- [ ] Signed clean source/artifact checkpoint and publication; immutable1.9.4 unchanged.
- No local or remote terminal services/certificates changed by this task.
- First packaging attempt failed due to unavailable Get-FileHash; fixed using .NET SHA256 and resumed staging/setup/manifest checks.
- Long report: [cascade step14](../../../docs/term_dev-l4tools-cascade-report.md).

- Signed setup SHA3737d86ee322b458744a9a7b2bac8c9c1f9839eaefd67382f7c0baedfa5618f4,29646904bytes. Operator Upgrade on773 requested; no runtime result yet.
