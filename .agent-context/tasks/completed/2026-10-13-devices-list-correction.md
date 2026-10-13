# Devices list correction

User scope: restore organization devices omitted by plan12, rename the L4Desk
section to Устройства, filter Экран терминала by exact sys=windows. Authorized
existing delivery flow: push to main and builder deployments.

Ownership: MB settings list owns visible organization rows, IoT batch presence
owns connection observations. Enrollment/finance controls admission, not the
list scope. Deleted L4Desk rows, tenant boundaries and existing action guards remain.
No enrollment flags or other terminal data changed; no imports or new migrations.

Delta: removed the enrollment-only condition from both list/count and presence
selection; existing visibility query reused for IDs. Renamed navigation/header.
Video selection applies the exact sys tag after existing visibility/admission
selection. IP-camera tagging and USB sources on eligible Windows terminals remain.

Verification: MB Ruff check/format and Pyright passed. 27 relevant backend tests
passed, including SQLite execution of the actual endpoint queries for all/online/
disabled filters, count before pagination and organization isolation. Frontend
100 unit tests and build passed. 17 browser cases passed; Windows browser selector
was corrected to the accessible button label and its focused rerun passed.
Release source main c8b2d6670a9a582cb41862e64a696d651a43cab1, push SHA verified
independently. Backend released through the standard builder launcher; locked full
suite: 791 passed, 22 skipped (813 collected), quality checks passed.
Backend digest f5197a7a46708f363206e64242b4478bcdd107b23af2f2c1a99152d1d5a06122.
Production health verified by deployer. Read-only execution of the actual list
function using the organization derived from terminal 773 (no HTTP auth smoke,
no JWT generation): all=134, online=3, disabled=88; 4623 appears online and 4624
appears disabled/all. This did not change enrollment or activity data.
The final Windows browser case also selected 773 and verified no POST/stream start.
Frontend released through builder from the same SHA, digest
677f0e60ebb7057f8e4fd3357e7cfb530f888b62a6753b9ff9264b0c5a056687.
Builder frontend 100 unit tests/build passed; deployer verified served index and
kept nginx-default running. Both production release records independently checked.
Release window 2026-10-13 07:25–07:29 UTC.
No physical terminal command, session, PIN or test credential created.
Local test failure was a selector mismatch (№773 instead of bare 773), corrected
and rerun successfully; total relevant browser scenarios verified: 18.
Source worktree retained for review; original user modifications preserved.
Existing licensing/transport admission rules were not changed by this correction.

MCP Ops unavailable; SSH preflight: 2038 MiB available RAM, root 63%, load .37.
Read-only credential scan: 1448 scoped tracked files; only previously reviewed
synthetic key markers/scanner constants matched. No credentials in this change.
