# L4Update IoT transport release — 2026-10-08

## Ownership and authorization

User requested remote work through RPC7031 test readiness and accepted marked
orphan transport probes plus persistent infrastructure event76/tag449. Root
reviewed PR105 and authorized normal master/builder/app1-only deployment. No MB/PB,
broker, nginx, secrets, runtime env or installed terminal actors were changed.

## Immutable source and image

- PR: <https://github.com/OlegLebedevRU/iot-rpc-rest-app/pull/105>.
- Reviewed12-file source commit: `2adf14f2686d68290ea1427e93c567239d8e11ca`.
- Accepted master merge: `5f80d6b0ead1c5c8cba54d67603656cdc71b899c`.
- OCI digest: `sha256:6beff4c0e52fa02caa0de9b74d212eec19bccf9ed7d5117e025ad0cc25b04694`.
- Image repository: `dev-leo4-ru.cr.cloud.ru/etran/app1`.
- Builder artifact: `/home/github-runner/etran-ci/artifacts/app1/5f80d6b0ead1c5c8cba54d67603656cdc71b899c.json`.

## Validation and delivery

Clean Windows worktree:575 passed/7 skipped, changed Python ruff/black pass,
12-file masked secret scan zero matches. No model/Alembic changes versus previous
runtime6209faf. Original external checkout's unrelated dirty schemas preserved.

Dedicated builder preflight: RAM2737MiB, root61%, load0.08. Normal
`deploy/registry_app1.py` ran from clean accepted-master checkout; locked gate
549 passed/33 Linux platform skips/4 existing warnings. Buildx published and
independently inspected immutable digest. Managed builder checkout remains clean.

Production preflight: RAM2132MiB, root51%, load0.13. Existing managed deployer and
components hashes matched current repository exactly. Its normal pull/label/
app1-only no-deps no-build/health/rollback flow completed. First startup health
attempt got connection-refused; bounded retry passed. No direct source patch,
production build or tooling edits.

Independent final verification: app1 container33f41d169e5c, image/revision exact,
RUNNING, restart count0, /docs200. Runtime source contains early strict marked REQ
and EVT dispatch, bounded2s identity lookup,7032 poll admission and76 billing
exemption. All11 neighboring container IDs unchanged from preflight, including
PB/MB, RabbitMQ, Redis, both nginx owners, media and MCP.

## Remaining acceptance

Source presence and HTTP health prove correct runtime delivery, not live MQTT
round-trip. Actual fresh REQ/RSP plus EVT/EVA must run through the existing Con
client on773 after candidate admission is ready. No duplicate MQTT connection or
fabricated success event was used. Event76 persistence/observer acceptance and
full watch/recovery fault acceptance remain part of terminal readiness.
