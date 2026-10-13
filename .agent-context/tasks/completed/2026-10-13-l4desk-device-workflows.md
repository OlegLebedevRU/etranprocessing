# L4Desk device workflows

## Task intake
User authorized SU editing/PIN/certificate actions in any selected tenant, silent
polling like Classic monitoring, admin disable precedence, explicit status reasons
and deliberate, visually distinct certificate flows. Existing session authorization
includes base-main push and builder releases. MB owns settings auth/data and UI;
PB remains certificate/PIN issuance authority. No schema/native/IoT changes needed.
Base source main dc258cda0db109a61664b6214c96a2324e288add, isolated clean worktree.

## Implementation
- Scoped SU exception only for terminal descriptive edits and GET/POST PIN;
  selected-tenant filters and viewer guards remain. Profile settings keep SU read-only.
  Existing renewal endpoint/provider/service and billing authorization reused.
- User activity requests cannot change user disable while admin is_active=false;
  attempted administrative re-enable is also denied. Admission guard uses locked row.
  Free ordinal, payment, enrollment, identity and current sessions are unchanged.
- Timer/visibility refresh is silent, preserving rows and forms. Manual/query loads
  show loading. Mutation callbacks refresh silently; tenant/generation fencing remains.
- Status exposes admin/user/IoT restrictions separately, including simultaneous flags.
- TerminalCertificateDrawer owns flow selection and manual PIN lifecycle; two distinct
  cards, explicit selection, selected-flow banner, confirmation. Existing CertificateRenewal
  supports inline confirmation inside drawer, retaining default modal in other consumers.
- PIN GET only after choosing manual. Operation UUID survives ambiguous replies via
  sessionStorage/ref, with explicit retry state. PIN plaintext is never stored there.
  Busy confirmation prevents switching/closing; unmount destroys PIN confirmation and
  fences stale callbacks. Shared edit modal fences stale save responses on tenant switch.

## Verified / pending
MB Ruff check/format/Pyright passed; full local suite 803 passed/22 skipped before
final strict selected-tenant edit test. That extra settings test passed (10 settings
cases). Frontend 100 unit tests and build passed. Existing 15 terminal/renewal browser
cases passed. New SU (tenants1/1000), admin precedence, silent DOM/edit retention and
mobile chooser cases passed after adding explicit accessible labels to icon buttons.
Ambiguous PIN retry preserved the same operation UUID in browser; mobile chooser
was visually inspected at 390px after animation settled. Eight new browser cases
passed; default Classic modal confirmation is checked separately. Builder release
and live health/served-file verification completed below.
Initial browser failures were exact accessible-name mismatches, not API authorization.
MCP Ops unavailable; SSH preflight healthy (2041 MiB available, root64%, load .22).
No production PIN issuance, edit, toggle or physical terminal command was performed.

Credential scan: 1452 scoped tracked/addition files; reviewed synthetic key markers
only. Local pytest scratch removed; source worktree retained for review.


## Release evidence
Accepted main source 41157fff75206033aed6928ae5c24782c04af7d7; push independently
verified with git ls-remote. Both components built/deployed via standard builder launcher.
Backend immutable digest e15881c6350b5e7a60610a5e178fd0b61ce537355eb83f51d486a520ef9c76f4.
Frontend immutable digest 2503665c91b9e8f985ded83ba144ae5a7edfd841a9bd17919bb4a0bae079fe63.
Builder backend quality/full suite: 804 passed, 22 skipped (826 collected).
Builder frontend: 100 unit tests and build passed. 24 unique relevant browser
scenarios verified across targeted runs, including Classic modal confirmation;
mobile chooser visually reviewed after animation settled, with no horizontal overflow.
Production release records independently match source SHA/digests. Deployer verified
backend HTTP health, image revision and served frontend index without Nginx restart.
Published bundle independently checked for chooser/access status text.
Read-only live handler verification with an inactive nonenrolled device under a
locked row: both L4Desk user enable/disable denied with403 before any state write;
transaction rolled back. This is service/DB evidence, not an authenticated UI smoke.
No actual production PIN issuance, certificate RPC, edit or toggle performed; these
writes are validated in local API/browser tests with synthetic/mocked providers.
Original user changes preserved. Normal immutable rollback/release artifacts retained.
