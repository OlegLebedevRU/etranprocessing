# L4 Tools 1.9.1 release handoff

## Scope and ownership

- Worktree: `.scratch-medium-hd`, branch `feat/l4capture-medium-hd-input`; the immutable published manifest records base commit `1a06f6c` and `dirty=true`. Source changes are committed afterward in this branch.
- Producer: `tools/l4con` (`extra_service`); transport: MQTT `dev/{SN}/evt`; consumer: app1 generic event collector.
- Installer and display: `tools/l4setup`; distribution: `tools/release` and Generic Registry.
- Original remote drag, selection and wheel regression on terminal 1000009 remains open. Terminal 773 was inaccessible through l4mcp; read-only remote console on 1000009 was rejected by the environment approval policy. No claim of remote input repair is made by this release.

## Delivered

- Recovered event 75 from the separate `fix/l4pin-cert-cleanup` worktree, retained certificate identity checks, and added inventory tags 444 and 445. Scan interval 60 seconds; retry interval 15 seconds; force one publish after each reconnect.
- Fixed `l4setup` display of installed package version after installation and added per-tool PE versions to Details. `state.json` update now checks write, rename and readback.
- Added signing scripts and the operator pause rule in `tools/release/README.md`. The operator signed both payloads and installer in a regular Windows session.
- Published 1.9.1 using `deploy/publish_l4tools.py --allow-dirty`. Publication record: `artifacts/l4tools/1.9.1.json`.

## Verification

- `l4con/build.cmd all`: x86/x64 builds and MQTT 5 protocol tests passed.
- Other native tools built x86/x64. `l4setup/build.cmd` passed.
- All 18 staged EXE files and `tools/dist/l4setup.exe` had Authenticode status `Valid` after operator signing.
- Both embedded payload ZIPs matched the signed staged EXE bytes.
- Publisher verified local sums, remote HEAD digest and downloaded all three artifacts by HTTPS GET with matching SHA-256. Setup SHA-256: `7e7b28bc937373aaf0ec61c40d078b12bed6a415c56d9326653d91d28ee1684c`.
- App1 source inspection: generic collector stores the payload as JSON without tag-specific parsing.

## Limits and follow-up

- `l4pin` test could not create a test CNG key in the restricted environment (`0x80070002`), so the standard `build_dist.cmd` gate stopped. `l4setup` tests also hung in this environment and were interrupted. These are failed/incomplete checks, not passes.
- The original workspace sandbox could not write `.git/index.lock`; the sandbox changed after publication and a follow-up source commit became possible. The immutable published manifest still records `dirty=true` and base `git_sha=1a06f6c`.
- Event 75 has not been observed from a terminal after installing 1.9.1. Verify tag 444/445 with terminal 1000009 and compare against terminal 773 if access becomes available.
- To resolve the original remote input regression, reproduce drag and wheel on both terminals with the same browser and inspect the actual control messages. The 1.9.1 release provides file hashes and versions for that comparison.
