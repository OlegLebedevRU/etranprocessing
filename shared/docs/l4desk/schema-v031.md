# Shared schema 031

Adds terminals.l4desk_subscription_enabled BOOLEAN NOT NULL DEFAULT false.
This is explicit commercial enrollment of one terminal, independent of its
tenant's technical L4Desk profile, sessions, identity and Classic License rows.
Migration 031 changes no existing enrollment, license, payment or identity facts.
Product classification and enrollment backfill require the reviewed census and
compatible consumers; the column default does not itself authorize rollout.

Deploy an old-ORM MenuBuilder bridge accepting revisions 030 and 031 first.
Then PB applies migration 031; new ORM consumers require revision 031 and this
column. Old ORM consumers can still read the additive schema. Rollback keeps
the column and enrollment facts. Do not roll back to a startup guard rejecting 031.

Pure subscription decisions live in the separate etranprocessing-access package,
not this declarative ORM package. Caller-provided calendar deadlines preserve
the existing L4Desk grace policy; Classic licensebilling has no server grace.

Metadata and offline DDL checks do not prove live PostgreSQL migration,
classification correctness or end-to-end access. The source manifest is
package-source-v031.json; earlier snapshots remain historical.
