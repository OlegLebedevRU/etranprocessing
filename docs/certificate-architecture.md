# Certificate Architecture, Terminal mTLS & Native C Tooling Reference

Authoritative reference for the **etranprocessing** certificate lifecycle, mutual TLS (mTLS) reverse proxy architecture, dual-issuer terminal authentication, and native C tooling for POS terminal integration.

---

## 1. High-Level Architecture & Request Routing

```
               ┌─────────────────────────────────────────────────────────────┐
               │                Payment Kiosk / POS Terminal                 │
               │   - Legacy .NET / C# App (CryptoAPI / Schannel)             │
               │   - Native C CLI Tooling (CNG KSP / WinHTTP)                │
               │   - Windows Certificate Store (LocalMachine\MY)             │
               └──────────────┬──────────────────────────────┬───────────────┘
                              │ mTLS (:443)                  │ mTLS (:4443)
                              ▼                              ▼
        ┌───────────────────────────────────┐    ┌───────────────────────────────────┐
        │       nginx-mutual-legacy         │    │       nginx-mutual-primary        │
        │         (87.242.100.34)           │    │         (176.108.247.249)         │
        │   - Terminates TLS 1.0 - 1.2      │    │   - Modern TLS 1.2 - 1.3          │
        │   - Validates client cert against │    │   - Validates client cert         │
        │     legacy & new CA bundles       │    │   - Forwards client cert headers  │
        │   - Dynamic Selective Routing     │    │                                   │
        └───────┬───────────────────┬───────┘    └─────────────────┬─────────────────┘
                │                   │                              │
                │ (proxy_pass)      │ (proxy_pass)                 │ (proxy_pass)
                ▼                   ▼                              ▼
     ┌──────────────────────┐  ┌─────────────────────────────────────────────────────┐
     │  Legacy IIS Backend  │  │                 ProcessingBackend                   │
     │   (46.38.51.114)     │  │          (FastAPI + asyncpg + PostgreSQL)           │
     │                      │  │   - Endpoint routing: /api/certificates,            │
     │  - Legacy SOAP/REST  │  │     /api/licensebilling, /api/payment, etc.         │
     │  - Legacy DB access  │  │   - Dual-issuer authentication (get_current_terminal)│
     │                      │  │   - Certificate enrollment & CA integration         │
     └──────────────────────┘  └──────────────────────────┬──────────────────────────┘
                                                          │ HTTPS
                                                          ▼
                                              ┌──────────────────────┐
                                              │ External Serverless  │
                                              │ CA (Yandex Cloud)    │
                                              │   (iot.leo4.ru)      │
                                              └──────────────────────┘
```

---

## 2. Certificate Issuance Lifecycle (`/api/certificates/`)

The certificate enrollment flow allows terminals without an existing client certificate to request and install a signed certificate using a one-time 6-character PIN code.

### 2.1. PIN Allocation (Billing Phase)
1. An administrator or tenant orders a certificate PIN via the MenuBuilder Web UI / Billing API (`POST /api/billing/terminals/{id}/certificate-pin`).
2. A row is created in `certificate_pins` with `status = 'pending'`, linked to `terminal_id`.

### 2.2. Enrollment Protocol (`CHECK` & `SETUP`)

```
Terminal (CLI / C tool)              Nginx Proxy                ProcessingBackend               External CA
        │                                 │                             │                            │
        │── 1. GET /api/certificates/ ───>│                             │                            │
        │      ?function=check            │                             │                            │
        │      &pin=XXXXXX                │── proxy (no cert) ─────────>│                            │
        │      &tosign=BASE64(sysinfo)    │                             │                            │
        │      &v=26                      │                             │ 1. Validate PIN (pending)  │
        │                                 │                             │ 2. sign = MD5(sysinfo+key) │
        │                                 │                             │ 3. v=26 -> CNG Provider    │
        │<── 2. XML (sign, dn, prov) ─────│<────────────────────────────│                            │
        │                                 │                             │                            │
        │ 3. Generate RSA 2048 in KSP     │                             │                            │
        │    (ExportPolicy = 0)           │                             │                            │
        │ 4. Create PKCS#10 CSR           │                             │                            │
        │    (Subject CN = sign)          │                             │                            │
        │                                 │                             │                            │
        │── 5. POST /api/certificates/ ──>│                             │                            │
        │      ?function=setup            │                             │                            │
        │      &pin=XXXXXX                │── proxy ───────────────────>│                            │
        │      &cpserial={sign}           │                             │                            │
        │      body: raw Base64 PKCS#10   │                             │ 1. Wrap PKCS#10 in PEM     │
        │                                 │                             │ 2. Call CA (sign_csr) ────>│
        │                                 │                             │                            │
        │                                 │                             │    CA operations:          │
        │                                 │                             │    - Override CN -> sn     │
        │                                 │                             │    - Add SAN urn:sign:     │
        │                                 │                             │    - Issue certificate     │
        │                                 │                             │<── 3. Return JSON ─────────│
        │                                 │                             │       (cert, ca_pem,       │
        │                                 │                             │        serial, expires)    │
        │                                 │                             │                            │
        │                                 │                             │ 4. Build PKCS#7 chain      │
        │                                 │                             │ 5. UPDATE cert_serial in DB│
        │                                 │                             │ 6. PIN status -> 'used'    │
        │                                 │                             │ 7. Audit log in DB         │
        │<── 6. XML (PKCS#7 Base64) ──────│<────────────────────────────│                            │
        │                                 │                             │                            │
        │ 7. Delete old certs by email    │                             │                            │
        │ 8. Install in LocalMachine\MY   │                             │                            │
        │ 9. Bind CNG key (dwKeySpec = 0) │                             │                            │
```

---

## 3. Database Rules & Dual-Issuer Authentication Strategy

### 3.1. Golden Rule for `terminals.cert_serial` Updates

1. **New CA (`iot.leo4.ru` / 40-character hex serial):**
   - `terminals.cert_serial` is updated **EXCLUSIVELY** during the `certificates / setup` endpoint when a new certificate is generated and signed.
   - **NO OTHER FLOW** (`licensebilling`, `payment`, `techgate`, `ListMenuFile`) can modify or overwrite the 40-character serial of a new CA certificate.
2. **Legacy CA (`SubCA` / serial $\le 20$ hex characters):**
   - For backwards compatibility with migrated MSSQL terminals where `cert_serial` was `NULL`, `dependencies.py` automatically binds the serial on first legacy request (`source = "legacy_auth"`).
   - If a terminal already has a 40-character new CA serial (`len(cert_serial) > 20`), the legacy auto-bind logic is **strictly inhibited** to prevent accidental regression.

### 3.2. Dual-Issuer Branching Logic (`get_current_terminal`)

All authenticated terminal endpoints share the unified dependency `get_current_terminal` (`ProcessingBackend/backend/app/dependencies.py`):

```python
issuer = extract_cert_issuer(request)
is_new_ca = bool(issuer and "iot.leo4.ru" in issuer.lower())

if is_new_ca:
    # ------------------------------------------------------------------
    # Branch 1: NEW CA (iot.leo4.ru) -> STRICT AUTHENTICATION
    # ------------------------------------------------------------------
    # 1. Strict SQL filter: Terminal.sn == CN AND Terminal.cert_serial == Serial
    # 2. If matched: returns Terminal object (auth success).
    # 3. If mismatched: raises HTTP 401 Unauthorized ("serial_mismatch").
    #    No fallback to OU/O/L, no auto-binding.
    result = await db.execute(
        select(Terminal).where(
            Terminal.sn == cn,
            Terminal.cert_serial == cert_serial,
        )
    )
    terminal = result.scalar_one_or_none()
else:
    # ------------------------------------------------------------------
    # Branch 2: LEGACY CA (SubCA) -> HEURISTIC LOOKUP & SAFE AUTO-BIND
    # ------------------------------------------------------------------
    # 1. Lookup terminal by OU (device_id) and O (org_id), or L (kiosk_id), or CN.
    # 2. If terminal found and cert_serial is unset or legacy (<= 20 chars):
    #    updates terminal.cert_serial and writes to TerminalCertHistory.
    # 3. If terminal already has a 40-char new CA cert, cert_serial is preserved.
```

---

## 4. Native C Tooling for POS Terminals & Backend Integration

### 4.1. Rationale for Using C on POS Terminals

Self-service payment kiosks and POS terminals in production environments present specific operational constraints:
- **Diverse Windows Versions**: POSReady 7 (Windows Embedded Standard 7), Windows 10 IoT Enterprise, Windows 11, Windows Server.
- **Zero Runtime Dependencies**: No need to install Python, modern .NET runtimes, or external frameworks on locked-down terminal OS images.
- **Minimal Footprint**: Standalone statically-linked executable (`/MT` via MSVC) is ~**150 KB**.
- **Direct Windows Security Integration**: Direct C-level access to Windows Cryptography Next Generation (CNG / `ncrypt.dll`), CryptoAPI (`crypt32.dll`), and Windows HTTP Services (`winhttp.dll`).

### 4.2. Core Security & Implementation Details

#### 1. Non-Exportable Private Key in CNG Key Storage Provider (KSP)
Private keys are generated directly inside the Windows CNG hardware/software security provider with export prohibition:
```c
// Open CNG Storage Provider
NCryptOpenStorageProvider(&hProvider, MS_KEY_STORAGE_PROVIDER, 0);

// Create persisted key container
NCryptCreatePersistedKey(hProvider, &hKey, BCRYPT_RSA_ALGORITHM, L"EtranTerminalKey", 0, 0);

// Set RSA 2048 bits
DWORD dwKeyLength = 2048;
NCryptSetProperty(hKey, NCRYPT_LENGTH_PROPERTY, (PBYTE)&dwKeyLength, sizeof(dwKeyLength), 0);

// PROHIBIT PRIVATE KEY EXPORT (Non-Exportable)
DWORD dwExportPolicy = 0; // NCRYPT_ALLOW_EXPORT_NONE (0)
NCryptSetProperty(hKey, NCRYPT_EXPORT_POLICY_PROPERTY, (PBYTE)&dwExportPolicy, sizeof(dwExportPolicy), 0);

// Finalize key generation
NCryptFinalizeKey(hKey, 0);
```

#### 2. Critical Windows Schannel / SSPI Integration (`dwKeySpec = 0`)
When .NET (`HttpWebRequest`) or native Windows applications initiate an mTLS handshake via Schannel, the Windows Security Support Provider Interface (SSPI) calls `AcquireCredentialsHandleW`.
- For CNG Key Storage Providers (`dwProvType = 0`), Schannel requires `dwKeySpec = 0`.
- Passing legacy `CERT_NCRYPT_KEY_SPEC` (`0xFFFFFFFF` / `-1`) causes Schannel to reject the credentials with error **`0x8009030D (SEC_E_UNKNOWN_CREDENTIALS)`**, resulting in `WebException: Не удалось создать защищенный канал SSL/TLS`.
- **Correct implementation:**
  ```c
  CRYPT_KEY_PROV_INFO provInfo = { 0 };
  provInfo.pwszContainerName = L"EtranTerminalKey";
  provInfo.pwszProvName = (LPWSTR)MS_KEY_STORAGE_PROVIDER;
  provInfo.dwProvType = 0;              // 0 indicates CNG (KSP)
  provInfo.dwFlags = CRYPT_MACHINE_KEYSET; // LocalMachine store
  provInfo.dwKeySpec = 0;               // CRITICAL: 0 for CNG / Schannel compatibility
  
  CertSetCertificateContextProperty(pCertContext, CERT_KEY_PROV_INFO_PROP_ID, 0, &provInfo);
  ```

#### 3. Automatic Cleanup of Obsolete Certificates
Before adding the newly issued certificate to `LocalMachine\MY`, the installer scans the certificate store and removes any existing certificates matching the email address of the new certificate (`новый_серт.email == старый_серт.email`):
```c
// Search for certificates matching the target email address in Subject DN
// and call CertDeleteCertificateFromStore before importing the new certificate context.
```

### 4.3. Strategic Roadmap for Native C Tooling

| Component | Target Location | Description |
|---|---|---|
| **CLI Certificate Installer** | `tools/terminal-cert-installer/` | Standalone CLI tool (`terminal-cert-installer.exe`) for manual terminal setup, silent unattended installation (`--silent`), and certificate status diagnosis (`--status`). |
| **Native Crypto Library (DLL)** | `tools/libterminal-crypto/` | Shared C dynamic library (`terminal_crypto.dll`) exposing a clean C API for direct P/Invoke integration into legacy C# (.NET 4.0) terminal binaries without changing legacy CryptoAPI code. |
| **Terminal Integration Sidecar** | `tools/terminal-sidecar/` | Lightweight background Windows Service in C for automatic certificate renewal (when expiring $\le 30$ days), watchdog telemetry, and high-speed menu cache synchronization. |

---

## 5. Practical PowerShell Verification & Testing Scripts

Below are the verified PowerShell scripts used for diagnosing certificates and executing mTLS requests.

### 5.1. Testing Terminal mTLS Request (`POST /licensebilling/`)
Tests the full mTLS roundtrip from Windows PowerShell using the client certificate installed in `LocalMachine\MY`:

```powershell
# Open LocalMachine\MY store and retrieve the terminal certificate
$store = New-Object System.Security.Cryptography.X509Certificates.X509Store("My", "LocalMachine")
$store.Open("ReadOnly")
$cert = $store.Certificates | Where-Object { $_.Subject -match "1.terminal@forpay.ru" } | Select-Object -First 1
$store.Close()

if (-not $cert) {
    Write-Error "Certificate not found in LocalMachine\MY"
    return
}

Write-Host "Using Certificate:"
Write-Host "  Subject:    $($cert.Subject)"
Write-Host "  Serial:     $($cert.SerialNumber)"
Write-Host "  Thumbprint: $($cert.Thumbprint)"
Write-Host "  Issuer:     $($cert.Issuer)"
Write-Host "  Expires:    $($cert.NotAfter)"

# Configure TLS 1.2 and bypass server cert validation (for self-signed migration certs)
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12
[System.Net.ServicePointManager]::ServerCertificateValidationCallback = {$true}

# Create HttpWebRequest with client certificate
$req = [System.Net.HttpWebRequest]::Create("https://iot-processing.ru/licensebilling/")
$req.Timeout = 10000
$req.Method = "POST"
$req.ContentType = "application/x-www-form-urlencoded"
$req.ClientCertificates.Add($cert)

$body = [System.Text.Encoding]::UTF8.GetBytes("function=check&Signature=TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE=")
$req.ContentLength = $body.Length
$stream = $req.GetRequestStream()
$stream.Write($body, 0, $body.Length)
$stream.Close()

try {
    $resp = [System.Net.HttpWebResponse]$req.GetResponse()
    $reader = New-Object System.IO.StreamReader($resp.GetResponseStream())
    $text = $reader.ReadToEnd()
    $reader.Close()
    Write-Host "HTTP Status: $([int]$resp.StatusCode) $($resp.StatusDescription)"
    Write-Host "Response Body:"
    Write-Host $text
    $resp.Close()
} catch [System.Net.WebException] {
    $resp = [System.Net.HttpWebResponse]$_.Exception.Response
    if ($resp) {
        $reader = New-Object System.IO.StreamReader($resp.GetResponseStream())
        $text = $reader.ReadToEnd()
        $reader.Close()
        Write-Host "Error HTTP Status: $([int]$resp.StatusCode) $($resp.StatusDescription)"
        Write-Host "Error Body: $text"
    } else {
        Write-Host "Exception: $_"
    }
}
```

### 5.2. Batch Testing Multiple Switched Endpoints
Verifies that all variations of URLs (root paths, `.ashx` extensions, and `/api/` prefixes) are properly handled:

```powershell
$store = New-Object System.Security.Cryptography.X509Certificates.X509Store("My", "LocalMachine")
$store.Open("ReadOnly")
$cert = $store.Certificates | Where-Object { $_.Subject -match "1.terminal@forpay.ru" } | Select-Object -First 1
$store.Close()

[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12
[System.Net.ServicePointManager]::ServerCertificateValidationCallback = {$true}

$urls = @(
    "https://iot-processing.ru/licensebilling",
    "https://iot-processing.ru/licensebilling/",
    "https://iot-processing.ru/licensebilling/gate.ashx",
    "https://iot-processing.ru/api/licensebilling",
    "https://iot-processing.ru/api/licensebilling/",
    "https://iot-processing.ru/api/licensebilling/check"
)

foreach ($url in $urls) {
    try {
        $req = [System.Net.HttpWebRequest]::Create($url)
        $req.Timeout = 5000
        $req.Method = "POST"
        $req.ContentType = "application/x-www-form-urlencoded"
        $req.ClientCertificates.Add($cert)

        $body = [System.Text.Encoding]::UTF8.GetBytes("function=check&Signature=TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE=")
        $req.ContentLength = $body.Length
        $stream = $req.GetRequestStream()
        $stream.Write($body, 0, $body.Length)
        $stream.Close()

        $resp = [System.Net.HttpWebResponse]$req.GetResponse()
        $reader = New-Object System.IO.StreamReader($resp.GetResponseStream())
        $text = $reader.ReadToEnd()
        $reader.Close()
        Write-Host "OK: $url -> Status: $([int]$resp.StatusCode) | Body: $($text.Trim())"
        $resp.Close()
    } catch {
        Write-Host "FAIL: $url -> Error: $_"
    }
}
```

### 5.3. Inspecting Certificate Store & Private Key Linkage
Checks whether the certificate has an attached private key and inspects certificate properties:

```powershell
$store = New-Object System.Security.Cryptography.X509Certificates.X509Store("My", "LocalMachine")
$store.Open("ReadOnly")
foreach ($c in $store.Certificates) {
    if ($c.Subject -match "forpay.ru") {
        Write-Host "Subject:        $($c.Subject)"
        Write-Host "Issuer:         $($c.Issuer)"
        Write-Host "Serial:         $($c.SerialNumber)"
        Write-Host "Thumbprint:     $($c.Thumbprint)"
        Write-Host "HasPrivateKey:  $($c.HasPrivateKey)"
        Write-Host "Valid From:     $($c.NotBefore)"
        Write-Host "Valid To:       $($c.NotAfter)"
        Write-Host "--------------------------------------------------------"
    }
}
$store.Close()
```

### 5.4. Database State Inspection via SSH / Docker
Inspects terminal records, certificate binding, and certificate history on the remote PostgreSQL database:

```powershell
# Check terminal record and current serial
"SELECT id, sn, device_id, org_id, cert_serial, cert_not_valid_after FROM terminals WHERE id = 1;" | `
    ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec -i iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing"

# Check certificate audit history
"SELECT id, terminal_id, cert_serial, source, issued_at FROM terminal_cert_history WHERE terminal_id = 1 ORDER BY id DESC LIMIT 5;" | `
    ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo docker exec -i iot-rpc-rest-app-pg-1 psql -U etran -d etranprocessing"
```
