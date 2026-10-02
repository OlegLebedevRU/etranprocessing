# L4 Tools1.9.6 — completed release

## Scope and ownership
- Native l4superv dependency-aware MQTT stack restart, read-only l4desk status, Mosquitto log ACL; l4setup bounded readiness and packaging1.9.6.
- Producer/consumer: supervisor → Windows SCM → Mosquitto/L4Con; status CLI → process path/session → l4desk. No MQTT client/topic, server stack, database or capture runtime change.
- Invariants preserved: SCM dependencies, pending identity on failed transition, pinned own-process shutdown only, Users read-only log, private keys/certificates unchanged outside operator enrollment.
- Worktree D:\work\etranprocessing-mcp-user-events; signed source/artifact checkpoint a76e586032a518e33540ce45406ee6c2a462af9a pushed and verified on origin/main. Original dirty checkout preserved.

## Result
- Durable decisions and verification limits: [stabilization reference](../../../docs/term_arch-l4tools-stabilization-decisions.md); operator summary/status guidance updated for schema2.
- Suite/setup1.9.6, superv1.9.5; other PE versions unchanged. Current capture1.0.0.0 preserves MF stability and later profiles.
- L4Con stops before broker restart and returns afterwards if previously running; initially absent/stopped client left to watchdog. Failure does not commit transition. Error code/stage logged.
- Separate --status discovers live l4desk by exact executable path/session, without changing processes.
- Shared narrow ACL applies to Mosquitto log directory and existing file: SYSTEM/Admins full, Builtin Users read/traverse. Existing protected ACL repaired; no owner/content/reparse target change. Installer applies after payload preparation; SYSTEM supervisor retries once at startup.
- Installer waits up to15s for asynchronous l4desk startup, preserves genuine timeout and reports a degraded reason. No/invalid interactive session skips waiting.

## Verified
- x86/x64/default suite builds; dependency/failure-order and process-discovery regressions; actual Windows AccessCheck with restricted non-admin token verifies read allowed/write denied for new and protected existing logs, content retained.
- x86/x64 readiness delayed/immediate/15s timeout/invalid/changed session fixtures; pipeline11cases, certificate phase2cases, STOPPED/zero-exit regression. Bootstrap temporary-directory checks passed; fixture files cleaned.
- Operator repeat signing:19EXE Valid/same signer/timestamp, signtool /pa /all /tw; SHA256SUMS, both61-file embedded payloads and current capture PE sections passed. Signed outputs synchronized without component rebuild.
- Second clean Install773 Win10Home19045x64,2026-10-02T13:10:21–13:10:43Z: no directory/services/certificate → new PIN enrollment → ready/exit0. Only pending_reboot_detected warning. Four services Running, supervisor active, l4desk19956/session1, proxy ready and MQTT active1. Nine installed EXEs match signed staging/inventory/signatures. Actual log ACL Users Read/Synchronize, SYSTEM/Admins FullControl.
- First clean attempt was degraded12 from a one-shot desk probe before asynchronous launch; fixed and re-signed. Initial ACL path-not-found warning before unpack was also fixed. Earlier failed fixture compilations and absent-client handling are documented in the cascade report, not counted as passed runs.

## Publication
- Signed setup29653560bytes, SHA25683c261fd018b51c011b6b1b9ae6e520e9c4d15477d9aefbf6911d8e244ec71ef.
- Strict manifest signed=true/dirty=false/source checkpoint a76e586; immutable1.9.6 uploaded, all3PUT200 and matching Digest. Initial PUT connection reset was retried by publisher.
- Publisher Python GET truncated29508510/29653560bytes, rejected without record. Full direct curl NO_PROXY downloads of all3files then matched exact size/SHA; downloaded setup Valid/timestamp. Only then standard record saved; no manual overwrite or allow-dirty.
- [Release record](../../../artifacts/l4tools/1.9.6.json), releases.jsonl and [cascade step15](../../../docs/term_dev-l4tools-cascade-report.md).
- [Signed installer](https://l4tools-generic.ar.cloud.ru/l4tools/1.9.6/l4setup.exe).

## Limits / cleanup
- User explicitly declined reboot and authorized continuing publication. Reboot startup is not tested. Remote55 upgrade, live fresh x86 and remote input/video E2E remain untested, do not infer them from local smoke.
- Operator cleared/re-enrolled local773 for clean tests; agent made no manual service/certificate changes. Signed1.9.6 remains installed.
- Ignored evidence/previous signed installers/public downloads retained in tools/dist/.runtime-backup/20261002-supervisor-dependencies. PIN/credentials never recorded in tracked files. No new test token or remote session created.
