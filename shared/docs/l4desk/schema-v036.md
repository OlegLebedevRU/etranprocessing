# Schema036 identity changes and device events

Migration036 is owned by ProcessingBackend. It adds two tables and includes
org_id in the existing daily-balance unique key; existing rows are unchanged. Root package import retains its legacy metadata;
import `etranprocessing_db.models.device_events` explicitly for the new models.

`terminal_identity_changes` preserves operation history with at most one active
operation per terminal (partial unique index), source and
target tenants, UUID, previous/current serial, issued-PIN reference and durable
same-CSR certificate response. The successful setup transaction switches org and
serial together; the IoT stage is persisted as sync_pending and retried explicitly.
Only pending operations may be cancelled. Renew is not an identity-change PIN.
The optional archived_technical_terminal_id links the immutable source snapshot;
source historical FK references move to it without changing tenant or money.

`device_admin_events` stores superuser-only incidents. event_key uniquely binds
operation+type; status/created_at is indexed for the inbox and new-event badge.
Failed-auth diagnostics alone inspect identity changes. Successful terminal auth
has no additional query, polling, or callback. No plaintext PIN is in incidents.

MB owns intent/cancel and event acknowledgement; PB owns issue/commit, IoT stage
and incident creation. Source payment ledgers stay in their tenant; paid license
time is preserved, balance/individual price and menu assignment are cleared.

Roll out IoT transfer API first, apply036, then compatible PB/MB and frontend.
Configure PB IDENTITY_CHANGE_IOT_URL/KEY through the environment. Source artifact
compatibility is not proof of production installation or broker acceptance.
Rollback application consumers first; do not downgrade while operations are
pending or unresolved. Downgrade refuses non-cancelled identity operations and cross-tenant aggregate
groups. After real use, keep the tenant-scoped reader and roll forward.
