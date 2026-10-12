# Plan12 implementation handoff

Scope authorized by user: implementation, push to base main/master, builder releases.
Owners and wire contract: docs/etran_arch-terminal-inventory.md.

Implemented adaptive aggregated terminal list, summary, FM navigation, independent
future-admission disable, certificate method choice, durable event75 projection and
isolated allowlisted IoT delivery. No physical terminal commands were issued.

Verified locally:
- ProcessingBackend: Ruff check/format and Pyright passed; 335 tests passed, 1 skipped.
- MenuBuilder: Ruff check/format and Pyright passed; full suite 785 passed, 22 skipped;
  latest inventory/presence additions: 8 passed.
- Shared: Ruff check/format and Pyright passed; 65 tests passed including provenance.
- IoT: full suite 658 passed, 7 skipped; latest UUID-collision tests 16 passed, 6 skipped
  (database integration requires configured test PostgreSQL). Changed delivery code lint passed.
- Frontend: production build and 100 unit tests passed; 13 terminal/certificate browser
  cases and 11 FM cases passed, including 390/820/1440 viewport widths.
- Schema 032/033 rollback bridge e043c06 was pushed to main and released by builder.

Release verified in production, 2026-10-12 22:43–22:49 UTC:
- etranprocessing release source main de63da27b1a71322a801194e98f3424e0a749a3a.
- IoT release source master 8cd723367c09f5aa45b71c98dc4d3a89ac69612d.
- Both pushes independently verified with git ls-remote. No force push.
- Builder ran locked dependencies/tests and published images; production pulled immutable
  digests and verified revision labels, image IDs and HTTP health.
- ProcessingBackend digest f3f32a7482468d40781ada2a41c0d9cb98186e3d7d2b782cf8e3e84799a80929.
- MenuBuilder backend digest 4dacbc0bf90eaee06439ac8e484e3da29cd72ac4d6cbe423f93546a82a3c367c.
- MenuBuilder frontend digest f7dc69bdda049ffb814ead1820d1c8f9e8d2b25a797a238dbba7256df28c008e.
- app1 digest 3f92a4f1df06f0231caf74e4f8a4c0be5085d5700aeb26b59273f06034255eef.
- Schema 033 applied before new MB; runtime compatibility checker passed. IoT schema
  0012_terminal_inventory_delivery confirmed. user_disabled_at is nullable.
- Private key configured using tracked configure_inventory_delivery.py, only in runtime
  env files. Existing public webhook keys/configuration untouched; no values printed.
- app1 → MB private validation: authenticated invalid envelope 422, anonymous body 403;
  private ingress absent from public OpenAPI. No synthetic accepted inventory was created.
- Startup replay: 6 deliveries, 0 pending; 1 accepted, 5 quarantined http_422. Rejected
  legacy component format contains path/size/sha256/present/modified_utc and unsupported
  architecture values. User-provided current contract expressly excludes these fields;
  validation was not broadened. Accepted real terminal 773 reports suite 1.13.43,
  publication 2026-10-12 21:58:29 UTC, receipt 22:46:06 UTC. Replay returned duplicate
  and left one applied inbox/projection. This is actual stored telemetry, not the example.
- Frontend deployed without restarting nginx-default; deployer compared served index.
- RabbitMQ, Redis, nginx-default and nginx-mutual retain pre-task container IDs.
  MB release also recreates jwt-issuer-signer by the existing standard deployment flow.
- MCP Ops unavailable; authorized standard SSH/runbook fallback used.
- Credential scan: 1448 main-repo files (scope exclusions honored) and 462 IoT files.
  Main matches reviewed: test fixtures and scanner markers only. IoT: zero matches.
- Builder MB full suite: 788 passed / 22 skipped (810 collected). PB 335 passed / 1 skipped.
  IoT locked full suite and frontend 100 unit tests/build passed on builder.

Verification limits: user-authenticated production UI and physical terminal commands
were not exercised; no ad hoc JWT was created from production secrets. Certificate
7011 installation and future transport rejection are covered by local tests, not a
new physical terminal execution. User will verify the UI result.

Cleanup: local pytest scratch directory removed; no temporary users, PINs, leases or
terminal commands created. Standard immutable release records retained for rollback.
Feature worktree retained for review; original dirty user checkout preserved.
Physical event75 publication and real terminal certificate/transport E2E are not yet
verified by this task. Do not infer installed versions from example payloads.
