# Shared schema 033

Additive terminal_tools_inventory and terminal_tools_inventory_inbox project only
contracted IoT event75. The projection records suite baseline, component inventory,
certificate observations and publication/receipt times. It never updates terminals.cert_serial.
L4DeskTerminal.user_disabled_at denies future admission independently of the
administrative flag, paid_until, enrollment and the free terminal ordinal.

First deploy the MenuBuilder 032/033 compatibility bridge, then the PB image
and migration 033, then new MenuBuilder writers. Keep expanded schema on image
rollback. The previous IoT image ignores its additive delivery table.

schema-v033.json describes ORM metadata. PostgreSQL migration and runtime evidence
are recorded separately in the task handoff.
