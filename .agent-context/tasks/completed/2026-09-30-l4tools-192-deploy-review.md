# L4 Tools 1.9.2 and remote control release review

## Scope and ownership

- Source: `etranprocessing` main and `iot-rpc-rest-app` master.
- Flow: browser input → MenuBuilder → app1 lease/command relay → MQTT `srv/{SN}/ctl` → l4desk; l4con → MQTT `dev/{SN}/evt` → app1 event collector.
- No database schema change. The l4con MQTT client type is `extra_service`; l4desk is `svc_desk`.
- Deployment was authorized for the 176 builder → registry → production pull route.

## Source and release provenance

- MenuBuilder drag/wheel relay: `7d1e0cf` on `etranprocessing` main.
- app1 drag/wheel relay: `769d83e` on `iot-rpc-rest-app` master.
- L4 Tools 1.9.1–1.9.2 source and release records: `cda76f1`, `d5f8ae7`, `c75e2ed`, `519ea5f`, `0206ef3`, merged into `etranprocessing` main without changing the published source commits.
- Signed 1.9.2 installer: SHA-256 `1802e852b696498e6bfbc87ab836782614c526bd43d3c18e93c34fbbebf3fb46`. All 18 staged executables and the installer had valid Authenticode signatures at publication. The immutable registry record is `artifacts/l4tools/1.9.2.json`.

## Deployment evidence, 2026-09-29 UTC

- Builder tests: MenuBuilder backend 566 passed; frontend 65 passed and built; app1 429 passed, 27 skipped. The backend/frontend build and app1 build were pushed to the registry.
- Production pulled MenuBuilder backend digest `sha256:3919e346762b86285dc5969255b8789d4b307624b93160fb8111f6a4739cc358`, frontend digest `sha256:ffa4953899ab886d5f2a7e2c84ada6b9f2656faf3c108c7340988c8bcda4ceb4`, and app1 digest `sha256:062a711b539a4a3607b4755ccbce536c4695faf9a2842843903f744d33ee15a7`. The app1 release record identifies revision `769d83e2bd02eb1d61c2e778957b1830a0463143` and deployment time `2026-09-29T22:56:02.559704+00:00`.
- At deployment verification app1 applied migrations, started, served `/docs` with HTTP 200, and accepted the remote-input WebSocket. The neighboring 11 containers remained unchanged.
- Terminal 1000009 installation evidence: `install_summary.json` and `state.json` both reported installed version 1.9.2; setup status `ready`, exit code 0. L4Con, L4Superv, Leo4Proxy and Mosquitto services were running. Event 75 ID 1356692 at `2026-09-29T22:29:04.076Z` reported package version 1.9.2 and an inventory tag.
- In one browser session, the video stream started and remote control was enabled. Wheel and left drag actions were sent; app1 logs showed the remote-input WebSocket and lease keepalive with `wait_ack=true` returning 200. Control and stream were then stopped. The terminal preflight remained online with its service online.

## Review checks and limits

- The merge had no conflicts. The MenuBuilder part of the 1.9.2 feature commit was already identical to main, so the merge adds tools, release records, and E2E guidance. `git diff --cached --check` passed. Both release JSON records parsed successfully. The tools source and release files in the merge matched the published feature branch before the documentation correction below.
- Corrected the event 75 documentation: package tag 445 reads successful `install_summary.json` first and falls back to `state.json`, matching the implementation.
- No fresh production SSH verification was possible during this review: the local SSH client rejected the host key and the server-ops MCP was unavailable. The deployment statements above are evidence recorded during the preceding release, not a fresh observation.
- The browser trace and app1 lease ACK do not alone prove the operating system visibly completed drag or scrolling. The previous release session did not capture a terminal-side injection log or a before/after screenshot proving those two effects. Treat visual completion as open until a controlled single-session retest.
- The historical 1.9.1 handoff records that l4pin CNG key creation and l4setup tests were incomplete in the restricted environment; do not reinterpret those as passes. Unchanged native tools were not retested during this review.
