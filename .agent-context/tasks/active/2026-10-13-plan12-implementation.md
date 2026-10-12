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

Pending: final release SHAs/digests, production schema/health/config/outcomes verification.
Physical event75 publication and real terminal certificate/transport E2E are not yet
verified by this task. Do not infer installed versions from example payloads.
