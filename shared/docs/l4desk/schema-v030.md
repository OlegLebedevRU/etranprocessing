# Shared schema 030

Additive renewal PIN purpose and response recovery fields on certificate_pins.
Existing rows default to setup. No financial tables, rights or identities are
rewritten. PB owns renewal PIN issuance/consumption and certificate serials.
PKCS#7 recovery stores public certificates, the old authenticated serial and CSR
SHA256. Access requires the same live device identity and same CSR, and expires
at the earlier of PIN expiry and 15 minutes after consumption.

Apply PB migration029→030 before starting new ORM consumers. All consumers
must accept revision030. Rollback preserves columns and consumed PIN facts.
The source-delivered package manifest is package-source-v030.json; old manifests
and schema snapshots remain historical. Real PostgreSQL upgrade and lock races
remain integration checks, not claims made by metadata snapshots.
