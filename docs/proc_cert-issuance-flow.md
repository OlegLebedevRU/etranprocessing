# Certificates Service — Flow & Architecture

> **Authoritative Architecture Reference**: See **[`docs/etran_cert-infrastructure-architecture.md`](etran_cert-infrastructure-architecture.md)** for complete end-to-end mTLS reverse proxy routing, dual-issuer authentication rules, native C tooling (`tools/terminal-cert-installer`), and PowerShell testing scripts.

## Authenticated renewal7011 (schema030 / suite1.11.0)

The flows below describe ordinary purpose=setup enrollment, which remains
anonymous/PIN-based. Renewal is separate: service-auth POST
/api/certificates/renewal-pins creates/reuses purpose=renew PIN; terminal POST
/api/certificates/renew requires a verified live new-CA certificate and same
SN/current serial. Normal CHECK/SETUP rejects renew PINs.
Only the exact nginx renew location changes: global optional_no_ca and CA
advertisement remain unchanged for legacy clients. PB verifies the presented
TLS certificate signature directly against its packaged public iot.leo4.ru CA,
then checks validity and ownership. Client-provided verification headers cannot
replace cryptographic trust. The CA public resource must be updated deliberately
if the authority rotates; never learn a trust anchor from the request.
New-CA hex serials may omit leading zeros (21–40chars); authentication compares
numeric hex equality, discovery keeps the issuance representation, no auth-route
serial overwrite. MenuBuilder admission accepts the same variable width.

MenuBuilder queues7011 synchronously without exposing PIN; IoT owns task_id.
l4con invokes l4pin directly with PIN stdin in a protected120s Job; l4pin has90s.
PB commits serial/history and persisted PKCS7 together; same-CSR recovery is
limited to15min and PIN expiry, and cannot restore a superseded issuance.
UsedPIN is issuance; fresh successful current-serial mTLS discovery confirms use.
[Implemented contract/timing/recovery matrix](term_arch-rpc7011-flow-matrix.md).

## Overview

Certificates service handles terminal certificate enrollment via two legacy-compatible endpoints:
- **CHECK** — terminal requests CA parameters with a PIN
- **SETUP** — terminal submits PKCS10 CSR, receives signed certificate (PKCS#7 chain)

PIN is pre-allocated in `certificate_pins` table via the organizational billing flow —
see [PIN-driven organizational billing](#pin-driven-organizational-billing) below and
[`docs/history/planning-and-research/billing-cert-licensing-analysis-2026-08-18.md`](history/planning-and-research/billing-cert-licensing-analysis-2026-08-18.md).
External CA is a Yandex Cloud Functions serverless function called over HTTPS.
Terminal does NOT have a client certificate during enrollment — auth is purely PIN-based.

---

## CHECK Flow

```
Terminal                           Nginx (4443)             ProcessingBackend
   │                                 │                            │
   │─ GET /api/certificates/ ────────>│                            │
   │   ?function=check               │                            │
   │   &pin=773773                   │── proxy (no cert headers)─>│
   │   &tosign=BASE64(sysinfo)       │                            │
   │   [&v=26]                       │                            │
   │                                 │                            │ 1. PIN lookup → terminal
   │                                 │                            │ 2. sign = MD5(decode(tosign) + SignKey)
   │                                 │                            │ 3. DN = CN={sn},O={org_id},OU={device_id},
   │                                 │                            │        S=msk,C=ru,L={terminal_id},E=1.terminal@forpay.ru
   │                                 │                            │ 4. v=26 → CNG prov, else → legacy prov
   │<── XML ─────────────────────────│<───────────────────────────│
```

### Provider branching (v parameter)

| `v` param | Provider | Key size |
|---|---|---|
| absent / empty | `Microsoft Enhanced Cryptographic Provider v1.0` | RSA 1024 (default) |
| `v=26` | `Microsoft Software Key Storage Provider` | RSA 2048 (terminal sets `Length=2048`) |

### Response XML (legacy format, windows-1251)

```xml
<?xml version="1.0" encoding="windows-1251"?>
<Response>
  <Result>OK</Result>
  <code>0</code>
  <CERTDATA>
    <catype>SubCA</catype>
    <prov>Microsoft Enhanced Cryptographic Provider v1.0</prov>
    <dn>CN=A99D2F18001ECC93DF5CBE27F442C8FA,O=1,OU=773,S=msk,C=ru,L=1,E=1.terminal@forpay.ru</dn>
    <pin>773773</pin>
    <sign>B2B829B03880E5FAAFC143A2F2D357F0</sign>
  </CERTDATA>
</Response>
```

### Terminal behavior (from clsCertificateInstaller.cs)

1. Parses DN from response, extracts `sign` field
2. **Replaces CN with sign** → `CN=B2B829B0...,O=1,OU=773,...`
3. Generates PKCS10 CSR with modified DN
4. Sends `function=setup&pin=773773&cpserial={sign}` + PKCS10 body

---

## SETUP Flow

```
Terminal                           Nginx (4443)             ProcessingBackend          External CA
   │                                 │                            │                        │
   │─ POST /api/certificates/ ───────>│                            │                        │
   │   ?function=setup               │                            │                        │
   │   &pin=773773                   │── proxy ──────────────────>│                        │
   │   &cpserial=B2B829B0...         │                            │                        │
   │   body: raw Base64 PKCS10       │                            │                        │
   │                                 │                            │ 1. PIN lookup → terminal│
   │                                 │                            │ 2. Wrap raw Base64     │
   │                                 │                            │    in PEM headers      │
   │                                 │                            │ 3. POST CA ───────────>│
   │                                 │                            │                        │
   │                                 │                            │   Headers:             │
   │                                 │                            │     X-Ssl-Client-Csr   │
   │                                 │                            │       URL-encoded PEM  │
   │                                 │                            │     X-Ssl-Client-Exp-  │
   │                                 │                            │       Days: 365        │
   │                                 │                            │     X-Sign: B2B829...  │
   │                                 │                            │     X-CN: A99D2F...    │
   │                                 │                            │                        │
   │                                 │                            │   CA:                  │
   │                                 │                            │   - Override CN → sn   │
   │                                 │                            │   - Add SAN urn:sign:  │
   │                                 │                            │   - Sign with CA key   │
   │                                 │                            │                        │
   │                                 │                            │<── JSON ──────────────│
   │                                 │                            │   cert (URL-encoded)   │
   │                                 │                            │   ca_pem (URL-encoded) │
   │                                 │                            │   serial_number        │
   │                                 │                            │   not_valid_after      │
   │                                 │                            │                        │
   │                                 │                            │ 4. Build PKCS#7       │
   │                                 │                            │    degenerate chain    │
   │                                 │                            │    (leaf + CA certs)   │
   │                                 │                            │ 5. UPDATE cert_serial  │
   │                                 │                            │ 6. PIN → used          │
   │                                 │                            │ 7. COMMIT              │
   │<── XML (PKCS#7 Base64) ─────────│<───────────────────────────│                        │
```

### Response XML

CERTDATA contains **PKCS#7 degenerate (certificates-only)** — raw Base64, no PEM headers.
This is the same format as Windows CA `CR_OUT_CHAIN` output.
CertEnroll.InstallResponse expects this format with `XCN_CRYPT_STRING_BASE64` encoding.

```xml
<?xml version="1.0" encoding="windows-1251"?>
<Response>
  <Result>OK</Result>
  <code>0</code>
  <CERTDATA>MIIHMQYJKoZIhvcNAQcCoIIHIjCCBx4CAQExADALBgkqhkiG9w0BBwGgggc...</CERTDATA>
</Response>
```

---

## Issued Certificate

```
Subject:
  CN  = A99D2F18001ECC93DF5CBE27F442C8FA  ← terminal.sn (from X-CN header)
  O   = 1                                  ← org_id
  OU  = 773                                ← device_id
  S   = msk
  C   = ru
  L   = 1                                  ← terminal.id
  E   = 1.terminal@forpay.ru               ← preserved from CSR

SAN:
  URI = urn:sign:B2B829B03880E5FAAFC143A2F2D357F0  ← cpserial (MD5 hash)
```

---

## Encoding chain (implementation detail)

Terminal sends raw Base64 PKCS10 in POST body. Backend wraps in PEM headers before sending to CA.
CA parses PEM, signs, returns URL-encoded PEM. Backend parses PEM, builds PKCS#7 DER, returns Base64.

```
Terminal: raw Base64
  → backend: + PEM headers → URL-encode → CA header
  → CA: URL-decode → parse PEM → sign → URL-encode PEM → JSON
  → backend: URL-decode PEM → parse cert → build PKCS#7 DER → Base64
  → terminal: InstallResponse(Base64)
```

---

## Contracts

### Terminal ↔ ProcessingBackend (legacy XML, unchanged)

| Field | CHECK response | Notes |
|---|---|---|
| `catype` | `SubCA` | |
| `prov` | Legacy or CNG (by `v` param) | See provider branching |
| `dn` | `CN={sn},O={org_id},OU={device_id},S=msk,C=ru,L={terminal_id},E=1.terminal@forpay.ru` | Terminal replaces CN with sign |
| `pin` | `{pin}` | Same as input |
| `sign` | `MD5(decode(tosign)+SignKey)` | Terminal uses as CN + cpserial |

| Field | SETUP response | Notes |
|---|---|---|
| `CERTDATA` | PKCS#7 degenerate (certificates-only), raw Base64 | leaf cert + CA cert chain |

### ProcessingBackend → External CA (HTTPS, JSON)

**Request headers:**

| Header | Value | Purpose |
|---|---|---|
| `X-Ssl-Client-Csr` | URL-encoded PKCS10 PEM | CSR to sign |
| `X-Ssl-Client-Exp-Days` | `365` | Validity period |
| `X-Sign` | `B2B829B0...` | → SAN URI `urn:sign:...` |
| `X-CN` | `A99D2F18001ECC93DF5CBE27F442C8FA` | → replace CN in Subject |

Note: Yandex Cloud Functions converts headers to Title-Case (`X-CN` → `X-Cn`). CA code uses case-insensitive lookup.

**Response JSON:**

```json
{
  "cert":            "URL-encoded PEM",
  "ca_pem":          "URL-encoded CA cert PEM",
  "serial_number":   "HEX",
  "not_valid_after": "YYYY-MM-DD HH:MM:SS"
}
```

---

## Database

### certificate_pins

```sql
CREATE TABLE certificate_pins (
    id               SERIAL PRIMARY KEY,
    pin              VARCHAR(10) UNIQUE NOT NULL,
    terminal_id      INT NOT NULL REFERENCES terminals(id),
    org_id           INT NOT NULL,
    order_item_id    INT REFERENCES billing_order_items(id),
    created_by       VARCHAR(100),
    creation_source  VARCHAR(20) NOT NULL DEFAULT 'system',  -- tenant / global_admin / system
    payment_required BOOLEAN NOT NULL DEFAULT FALSE,
    status           VARCHAR(20) NOT NULL DEFAULT 'pending', -- pending / used / expired / cancelled
    expires_at       TIMESTAMPTZ NOT NULL,                    -- TTL, default 24h (settings.cert_pin_ttl_hours)
    created_at       TIMESTAMPTZ DEFAULT NOW(),
    used_at          TIMESTAMPTZ
);
CREATE INDEX idx_cert_pins_terminal ON certificate_pins (terminal_id);
CREATE INDEX idx_cert_pins_status ON certificate_pins (status);
CREATE INDEX idx_cert_pins_org ON certificate_pins (org_id);
CREATE INDEX idx_cert_pins_order_item ON certificate_pins (order_item_id);
CREATE UNIQUE INDEX uq_cert_pins_one_pending_per_terminal
    ON certificate_pins (terminal_id) WHERE status = 'pending';
```

PIN creation now happens via `POST /api/billing/terminals/{terminal_id}/certificate-pin`
(MenuBuilder, tenant-scoped,
org billing policy driven) or, for paid operations, after the corresponding billing order is
confirmed — see [PIN-driven organizational billing](#pin-driven-organizational-billing).
A `check`/`setup` request against an expired `pending` PIN is lazily marked `expired` and
treated as not found; it does not block a new PIN from being requested.

### terminals (relevant fields)

| Field | Type | Notes |
|---|---|---|
| `id` | SERIAL PK | Used as L in DN |
| `device_id` | INT UNIQUE | Used as OU in DN |
| `sn` | VARCHAR(100) UNIQUE | Used as CN in DN (after X-CN override) |
| `org_id` | INT | Used as O in DN |
| `cert_serial` | VARCHAR(100) | Updated after successful setup |
| `cert_not_valid_after` | TIMESTAMPTZ | Updated after successful setup (from CA `not_valid_after`) |

### terminal_cert_history

Append-only operational history of certificates issued for a terminal, written by the
`setup` handler on every successful CA issuance.

```sql
CREATE TABLE terminal_cert_history (
    id              SERIAL PRIMARY KEY,
    terminal_id     INT NOT NULL REFERENCES terminals(id),
    cert_serial     VARCHAR(100) NOT NULL,
    not_valid_after TIMESTAMPTZ,
    pin_id          INT REFERENCES certificate_pins(id),
    source          VARCHAR(20) NOT NULL DEFAULT 'setup',
    issued_at       TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_terminal_cert_history_terminal ON terminal_cert_history (terminal_id);
```

---

## PIN-driven organizational billing

Full analysis: [`docs/history/planning-and-research/billing-cert-licensing-analysis-2026-08-18.md`](history/planning-and-research/billing-cert-licensing-analysis-2026-08-18.md).

Key rule: **payment/checkout never calls the CA.** `sign_csr()` remains the only CA call site,
invoked exclusively from this router's `setup` handler. The billing subject is the
*permission to create a PIN* for a terminal, not certificate issuance itself.

```
POST /api/billing/terminals/{id}/certificate-pin (MenuBuilder, tenant, JWT)
  → org_billing_settings.cert_billing_mode/cert_price_minor decide:
      mode=none            → PIN created immediately (pin_ready)
      per_operation, price=0 → PIN created immediately (pin_ready, audited)
      per_operation, price>0 → billing_orders/billing_order_items(operation=cert_pin) created (payment_required)
  → POST /api/billing/orders/{id}/confirm (mock provider) or real provider webhook
      → for operation=cert_pin: creates the CertificatePin (still no CA call)
  → terminal check/setup (this document) → sign_csr() → cert_serial + cert_not_valid_after + terminal_cert_history
```

Org-level settings added to `org_billing_settings`: `cert_billing_mode` (`none`|`per_operation`),
`cert_price_minor`, `tenant_pin_creation_enabled`, `cert_charge_primary_issue`, `cert_charge_reissue`.
See `app/services/cert_billing.py` for policy resolution, PIN generation/TTL, and PIN masking helpers.

---

## Error codes

| code | Description |
|---|---|
| 0 | Success |
| 1 | CA signing error |
| 2 | PIN not found or not pending |
| 4 | PKCS10 not found in request body |

---

## File Structure

```
etranprocessing/
├── docs/
│   └── proc_cert-issuance-flow.md                  # This document
├── shared/etranprocessing_db/models/               # Declarative ORM models (CertificatePin, Terminal, TerminalCertHistory)
├── ProcessingBackend/
│   ├── ca_sign_csr.py                              # CA serverless function (Yandex Cloud)
│   ├── ca_test.py                                  # Local CA test script
│   ├── ca_requirements.txt                         # cryptography>=44.0.0
│   ├── nginx-mutual-legacy/nginx-configs/
│   │   └── legacy_ssl.conf                         # Terminal mTLS gateway: /api/certificates routes
│   └── backend/
│       ├── app/
│       │   ├── routers/
│       │   │   └── certificates.py                 # check/setup + PIN endpoints
│       │   ├── services/
│       │   │   ├── ca.py                           # HTTP client to external CA
│       │   │   └── cert_billing.py                 # PIN billing policy/generation/TTL
│       │   └── logging_config.py                   # cert_logger
│       └── alembic/versions/                       # Full chain 001–029 (002, 006 for PINs/billing)
└── MenuBuilder/backend/app/
    └── routers/billing.py                          # POST /api/billing/terminals/{id}/certificate-pin, orders confirm/get
```

---

## Nginx locations (`ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf`)

Certificate enrollment routes (PIN-based, no cert headers) on the terminal mTLS
gateway `nginx-mutual-legacy` (:443):

```nginx
# Public terminal-facing enrollment entry points → processing-backend:8000:
location = /certificates/            { proxy_pass http://new_processing_backend/api/certificates/$is_args$args; }
location = /certificates             { proxy_pass http://new_processing_backend/api/certificates$is_args$args; }
location = /certificates/Dispatcher.ashx   # same rewrite to /api/certificates/
location ~ ^/api/certificates(/.*)?$ { proxy_pass http://new_processing_backend/api/certificates$1$is_args$args; }

# PIN issue API is service-internal (require_service_auth) — not on the public mTLS gateway:
location = /api/certificates/pins    { return 404; }
location ^~ /api/certificates/pins/  { return 404; }
```

Также на gateway: `location = /api/leo4proxy/policy` → `processing-backend:8000/api/leo4proxy/policy`.
Матрица выше — сокращение; авторитетный список location — сам `legacy_ssl.conf`.

Common proxy headers: `Host`, `X-Real-IP`, `X-Forwarded-For`, `X-Forwarded-Proto`,
`proxy_buffering off`. Full authoritative list — see `legacy_ssl.conf` itself.

No `X-Client-Cert-*` headers — terminal has no certificate during enrollment.

---

## Environment Variables

### ProcessingBackend (.env)

| Variable | Value | Description |
|---|---|---|
| `CA_URL` | `<endpoint URL from the private operator storage>` | External CA endpoint (Yandex Cloud Functions) |
| `CA_TIMEOUT` | `30` | HTTP timeout (seconds) |
| `CERT_VALIDITY_DAYS` | `365` | Certificate validity |
| `SIGN_KEY` | `<secret from the private operator storage>` | Key for MD5 sign computation |

### CA serverless function (env)

| Variable | Default | Description |
|---|---|---|
| `CA_CERT_PATH` | `/function/storage/keys/ca.crt` | CA certificate file |
| `CA_KEY_PATH` | `/function/storage/keys/ca.key` | CA private key file |
| `CA_CERTS_DIR` | `/function/storage/certs/` | Directory to save issued certs |

---

## Testing

```bash
# Generate dev CA keys
python generate_ca_keys.py --out-dir keys

# Test CA locally (both flows)
python ca_test.py --ca-cert keys/ca.crt --ca-key keys/ca.key
```
