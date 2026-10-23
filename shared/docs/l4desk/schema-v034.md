# Shared schema034

Additive nullable terminals.credential_recipient_pem is PB-owned verified public
leaf material, never a private key. MB owns camera_credential_profiles and
camera_credential_receipts. Crypto/auth/state transitions stay outside shared ORM.
PB is the sole Alembic owner; migration034 follows033. No automatic trusted import.

Apply schema before new readers/writers. Keep schema and stable private KEK on
application rollback; dropping populated vault tables is destructive. Details:
[camera vault v1](../../../docs/etran_arch-camera-credential-storage.md).

Snapshot describes ORM only; PostgreSQL18 migration and crypto acceptance are
recorded separately in TASK-35 handoff. Frozen wire package remains unchanged.
