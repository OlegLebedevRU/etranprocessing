# Certificate chooser reflects existing operations

## Task intake
Frontend task, MB owns authenticated PIN and renewal snapshots; PB remains sole
PIN/certificate writer. Existing GET /settings/terminals/{id}/pin and GET
/devices/{device_id}/certificate-renewal feed the chooser on open, in parallel.
No new API/schema/IoT/native changes. User authorizes base-main push and builder
release through prior session. Tenant/generation fences and UUID retry stay;
PIN plaintext stays in component memory, no issuance/RPC on open or check.

## Implementation
Two shared snapshot hooks lift reads above flow selection. Child renewal uses the
same snapshot in chooser/detail; Classic gets its own instance and retains modal.
Pending or issued/unconfirmed renewal blocks a new start and manual entry. Check
refreshes without a POST. Recovery remains a separate explicit action for existing
installation. Issued/unexpired manual PIN is displayed below its card, disabling
manual entry and competing remote issuance. Successful manual issuance returns to
chooser with the returned PIN, preserving no duplicate PIN request.
Expired/consumed PIN hides plaintext and allows selection. Failed reads disable
unsafe choices, preserve known active renewal and expose retry. Accepted issuance/
queue invalidate earlier reads, so late GET cannot undo a newly accepted result.

## Validation
Frontend100 unit tests passed. Final TypeScript/Vite build passed after queue-read
race hardening and explicit accessible labels on loading buttons. First browser
run21 passed/1 failed: exact accessible name included loading icon; drawer remained
open. Corrected labels. Final11 relevant browser scenarios passed, including a new delayed-GET/accepted-POST
race.23 unique browser scenarios now passed across the focused runs. Narrow390px
chooser and desktop issued-PIN layout visually reviewed. Additional verified-read
reconciliation clears the pending UUID only when the authenticated current PIN
response matches it; no blind clearing on null/failed/foreign operation. This
prevents a stale ambiguous flag blocking remote selection after manual completion.
Final build and both focused PIN lifecycle browser checks passed;24 unique
relevant browser scenarios passed across the runs.
Scoped credential scan162 tracked/new frontend files: no markers.
[MCP Ops Readiness: UNAVAILABLE]; standard SSH preflight2048MiB available,
root66%, load .23; release uses authorized builder/production runbook.
Backend code unchanged; backend tests/linters not run under project scope rules.
Real production PIN/RPC writes not performed. Push/release evidence pending.
Existing .pytest_tmp from preceding incident retained (previous approval rejection).
