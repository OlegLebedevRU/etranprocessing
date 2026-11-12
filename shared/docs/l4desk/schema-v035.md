# Schema035 FM readiness probes

Additive migration035 belongs to ProcessingBackend. Existing034 tables and
constraints are unchanged. The shared package0.1.2 adds only a declarative model.

`fm_readiness_probes` contains at most one current record per terminal, with
request UUID, tenant/actor/view, current serial, request/proof deadlines, response
hash, agent instance, bounded result and consumed session UUID. PB is its sole
writer. Idle terminals do not create/update rows. No periodic sweeper is required.

Server locks the terminal before create/result/consume; concurrent proof consumers
cannot open two sessions. A completed identical response does not extend deadlines.
Historical schema snapshots/provenance are retained. Rollback drops only this
new table; roll back on-demand consumers first. MenuBuilder's bridge release must
accept034 and035 before production migration.
