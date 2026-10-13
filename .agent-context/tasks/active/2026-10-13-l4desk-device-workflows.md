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
and live health/served-file verification remain pending.
Initial browser failures were exact accessible-name mismatches, not API authorization.
MCP Ops unavailable; SSH preflight healthy (2041 MiB available, root64%, load .22).
No production PIN issuance, edit, toggle or physical terminal command was performed.

Credential scan: 1452 scoped tracked/addition files; reviewed synthetic key markers
only. Local pytest scratch removed; source worktree retained for review.
