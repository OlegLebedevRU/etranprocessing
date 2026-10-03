# Shared schema 029

One additive nullable `paid_until` timestamp on `l4desk_terminals`. Historical financial
tables and facts are preserved. Migration ownership remains ProcessingBackend.
The package remains source-delivered; `package-source-v029.json` identifies this source
snapshot. Earlier schema and package manifests remain historical evidence, not current
metadata. The base-table fixture includes the already accepted org navigation policy 028.

`paid_until` is the durable subscription expiry; it is not derived from balance,
online events, certificate expiry or measured session duration. Business logic stays
outside the declarative package. Application rollback must preserve granted rights.
