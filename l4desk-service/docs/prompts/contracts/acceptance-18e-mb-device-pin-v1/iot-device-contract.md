# IoT device bridge contract for MenuBuilder 18E

Contract version: `1.0.0`. This is a data-only controller export of the
accepted IoT source at commit `35f1fce054e388a2206af45f1416b9572b0614a9`
(`H-L4D-18C-IOT-v1`). It does not publish neighboring source to the MB
runtime owner or repeat a production smoke. The selected field shapes are in
`iot-device-schemas.json`; examples are synthetic.

## Internal transport and tenant boundary

The provider exposes `GET /api/internal/v1/devices/` and
`POST /api/internal/v1/provisioning/terminals` behind internal service
authentication. The GET requires the caller's organization context and
filters devices by that organization, excluding deleted devices. Its `page`
starts at 1 and `size` is 1..100; `device_id` is an optional exact filter.
MenuBuilder must derive organization and user authorization from its own
trusted auth boundary. A browser-supplied organization header or device ID
does not grant access. Keep the internal credential server-side and private.

The response has `items`, `total`, `page`, `size`, `pages` and `stats`.
Each item has integer `device_id`, string `sn`, optional `connection`, and
`device_tags` entries with `tag` and nullable `value`. In the accepted source,
`connection.last_checked_result` and `connection.is_blocked` are booleans;
`checked_at` is optional. An absent connection or failed GET is unknown,
not proof of offline. The existing MenuBuilder management UI uses the
connection view for displayed status; L4Desk should use the same source and
refresh policy. This contract does not promise instantaneous presence.

The POST request has `device_id: int`, `sn: str`, `org_id: int`, optional
`name`, and optional `tags: dict[str, str]`. The provider persists nonempty
tag keys/values as system tags and upserts by device/tag/deletion state;
the GET exposes them as `device_tags`. The provider does not restrict `sys`
to an enum. MenuBuilder must enforce the user-requested values exactly
`windows`, `linux`, `esp32` and preserve the choice across retries. The
provider request still requires a real SN; a missing SN may not be
fabricated from `device_id`. The POST returns the same identifiers, a
`success` boolean and optional operational status/error fields.

The accepted provider source stores provisioning audit details containing
SN/name; it does not put `sys` in that audit payload. If MenuBuilder uses
its own existing audit details as retry state, that is MenuBuilder-owned
storage and must be tested for idempotency and tenant isolation. This
export does not authorize an IoT schema or implementation change.

## PIN boundary

`pin-contract.md`, `pin-schemas.json` and `pin-examples.json` are exact
copies of the already accepted `H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1` data
artifacts. The PIN POST needs `tenant_id`, business `terminal_id` and a
verified `sn`, in addition to `operation_id`. A new operation ID issues a
new PIN and expires the prior pending PIN; replay of the same operation ID
is idempotent. The GET by operation ID has no tenant filter, so MenuBuilder
must establish operation ownership before calling or revealing the PIN.
These service APIs are not browser endpoints. The exact response states,
errors and expiry semantics remain those of the copied contract/schema.
This packet does not authorize new provider behavior or make an SN-less
terminal eligible for PIN issuance.
