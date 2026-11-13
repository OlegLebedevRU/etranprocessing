# Current certificate installation confirmation

POST `/api/certificates/renew` also accepts `{"function":"confirm"}`.
This is an additive operation on the existing pinned-CA/mTLS ingress; it needs
no PIN, CSR, new MQTT code, route or Nginx configuration. Extra fields are rejected.

The certificate must be currently valid under the configured terminal CA, with
TLS proof-of-possession ingress headers and matching actual leaf/header serial.
The unique CN identifies the terminal. O/OU must exactly match current org/device;
the leaf serial must match the current issuer record, with no old-leaf recovery
exception. Active/subscription policy is retained. The terminal row is locked.

Success records authenticated TerminalCertDiscovery for the exact current leaf.
No issuance, PIN consumption, key change, billing operation, task completion or
inventory projection occurs. A failed discovery write returns503, not success.
Repeated confirmation only refreshes the observation and is safe after reboot
or a lost response. Existing `_unconfirmed_renewal` observes this evidence through
its normal current-serial and post-issuance discovery predicates.

The200 JSON response has exactly `confirmed` (true), `sn`, `device_id` (string)
and lowercase `fingerprint_sha256` of the DER TLS leaf; Cache-Control:no-store.
The client checks these against its locally verified active credential. Identity
failure401, policy403, malformed payload422, unavailable persistence503. Do not
log certificate PEM, request headers or response bodies.

An install result (RPC7011 RES200/applied), MQTT CMT, inventory75/EVA and this
authenticated PB observation remain separate evidence. Failed confirmation must
not replay enrollment or change an already successful installation result.

Tests use a synthetic CA and cover real certificate verification, wrong current
serial/SN/tenant/device, expiry, disabled terminal, extra enrollment fields,
discovery failure and duplicate confirmation without calling the signer.
