# 17E test05 registration evidence for 17F

This is a finite data-only extract of the accepted 17E history. It does
not initiate registration, send email, or verify current terminal state.

## Historical E2E fact

- The `L4D-17E-MB-FIX-01-v3-report.md` at commit
  `f031b963f43d9c52fbf28aee9f978157386721f1` records that test05
  created organization 10000 in MenuBuilder and IoT, a repeated email
  confirmation was idempotent, and occupied IoT organization ID 4 was
  rejected. Its Git/raw SHA-256 is
  `6dfa6654b7472351da1147f9a526634d6839507f917294fe75f9781f0f2e1be0`.
- The scoped org-ID handoff at commit
  `3c5113cefd8d96804b30d489d07cf40606b6b32b` records one owner
  membership and role ID 5 for tenant 10000; registration ID 4 was
  consumed. IoT held one organization and one reservation for 10000
  (`l4desk-registration:4`). The user confirmed the link; repeating it
  showed “already confirmed” and created no duplicate. The handoff's
  Git/raw SHA-256 is
  `bfe4a4f07cb5ab683ff50710a841da3d90b66c674af2cc22e8e03487483f1f2f`.

These are historical E2E observations, not a new account lifecycle in
17F. The current 17F browser ownership and terminal 1000007/PIN facts
must be evaluated from the separate 17F reports. No new tenant is required
to repeat this historical registration solely for the final report.
