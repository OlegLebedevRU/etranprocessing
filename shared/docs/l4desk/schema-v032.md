# Shared schema 032

Additive FM tables: fm_agents (authenticated capability heartbeat) and fm_operations
(PB-owned metadata, browser identity, immutable manifests, pinned S3 versions and commit outcome).
No file bodies or presigned URLs are persisted. IoT remains the only lease owner.
Migration 032 does not alter certificate, subscription, payment or existing terminal facts.

Apply PB migration before new writers. The new MenuBuilder startup guard requires 032
and both FM tables. Before rollout an old-ORM bridge must accept 031/032; do not
roll back to a consumer rejecting 032. Keep tables and commit receipts on rollback.

schema-v032.json is ORM/offline evidence; live PostgreSQL upgrade and provider/agent
end-to-end evidence must be recorded separately. Earlier snapshots remain historical.
