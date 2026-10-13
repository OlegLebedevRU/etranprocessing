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
Pending: builder release and production verification.

MCP Ops unavailable; SSH preflight: 2038 MiB available RAM, root 63%, load .37.
Read-only credential scan: 1448 scoped tracked files; only previously reviewed
synthetic key markers/scanner constants matched. No credentials in this change.
