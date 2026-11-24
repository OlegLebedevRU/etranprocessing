# Schema 037: MQTT quarantine decisions

ProcessingBackend migration 037 adds mqtt_quarantine_operations for superuser decisions. The operation UUID is idempotent, and the monotonically increasing row ID orders commands at the executor. The table contains the canonical IP, operator, reason, requested TTL and absolute UTC deadline, confirmation and outcome. Broker observations stay outside PostgreSQL. Previous application versions require schema compatibility with revision 037 before migration.
