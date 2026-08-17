# Certificates Service — Flow & Architecture

## Overview

Certificates service handles terminal certificate enrollment via two legacy-compatible endpoints:
- **CHECK** — terminal requests CA parameters with a PIN
- **SETUP** — terminal submits PKCS10 CSR, receives signed certificate (PKCS#7 chain)

PIN is pre-allocated in `certificate_pins` table (creation is out of scope).
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
    id          SERIAL PRIMARY KEY,
    pin         VARCHAR(10) UNIQUE NOT NULL,
    terminal_id INT NOT NULL REFERENCES terminals(id),
    status      VARCHAR(20) NOT NULL DEFAULT 'pending',  -- pending / used
    created_at  TIMESTAMPTZ DEFAULT NOW(),
    used_at     TIMESTAMPTZ
);
CREATE INDEX idx_cert_pins_terminal ON certificate_pins (terminal_id);
CREATE INDEX idx_cert_pins_status ON certificate_pins (status);
```

PIN creation is out of scope (separate process).

### terminals (relevant fields)

| Field | Type | Notes |
|---|---|---|
| `id` | SERIAL PK | Used as L in DN |
| `device_id` | INT UNIQUE | Used as OU in DN |
| `sn` | VARCHAR(100) UNIQUE | Used as CN in DN (after X-CN override) |
| `org_id` | INT | Used as O in DN |
| `cert_serial` | VARCHAR(100) | Updated after successful setup |

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
ProcessingBackend/
├── ca_sign_csr.py                          # CA serverless function (Yandex Cloud)
├── ca_test.py                              # Local CA test script
├── ca_requirements.txt                     # cryptography>=44.0.0
├── docs/
│   └── certificates-flow.md                # This document
├── nginx-mutual-ssl.conf                   # location /api/certificates/
└── backend/
    ├── app/
    │   ├── models.py                       # CertificatePin model
    │   ├── routers/
    │   │   └── certificates.py             # check/setup endpoints
    │   ├── services/
    │   │   └── ca.py                       # HTTP client to external CA
    │   └── logging_config.py               # cert_logger
    └── alembic/versions/
        ├── 001_initial_payment_flow_tables.py
        └── 002_add_certificate_pins.py
```

---

## Nginx location (nginx-mutual-ssl.conf)

```nginx
# Certificates: /api/certificates/ (PIN-based, no cert headers)
location ~ ^/api/certificates(/.*)?$ {
    proxy_pass http://processing-backend:8000/api/certificates$1$is_args$args;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_buffering off;
}
```

No `X-Client-Cert-*` headers — terminal has no certificate during enrollment.

---

## Environment Variables

### ProcessingBackend (.env)

| Variable | Value | Description |
|---|---|---|
| `CA_URL` | `https://functions.yandexcloud.net/d4efqr7g0mlpqr8acktl` | External CA endpoint |
| `CA_TIMEOUT` | `30` | HTTP timeout (seconds) |
| `CERT_VALIDITY_DAYS` | `365` | Certificate validity |
| `SIGN_KEY` | `EtranSignOK` | Key for MD5 sign computation |

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
