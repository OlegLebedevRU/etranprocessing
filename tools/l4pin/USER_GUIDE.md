# Leo4 Terminal Certificate Installer (l4pin) - User Guide

## 1. Overview

`l4pin` (`l4pin.exe`) is a standalone, lightweight CLI utility for Windows that requests, generates, and installs a mutual TLS (mTLS) client certificate on payment kiosks and POS terminals using a single-use 6-character PIN code.

### Key Features
- **Zero Dependencies**: Native C / Win32 / CNG binary. No .NET runtime, Python, or Visual C++ Redistributables required (`/MT` static link).
- **Universal Architecture**: Fully compatible with both **32-bit (x86)** and **64-bit (x64)** Windows (including Windows 7 Embedded / POSReady 7 and Windows 10/11 x64 via WOW64).
- **Smart URL Finder (`url-finder`)**: Automatically discovers backend endpoints via local `leo4proxy` daemon or seamlessly falls back to production endpoints (`https://iot-processing.ru/api/certificates`).
- **Secure Key Storage (CNG)**: Private keys are generated in `Microsoft Software Key Storage Provider` and marked as **non-exportable** (`NCRYPT_ALLOW_EXPORT_NONE`).
- **Auto-Cleanup**: Automatically detects and cleans up expired/obsolete terminal certificates matching the terminal's email address in `LocalMachine\MY`.
- **SChannel Ready**: Correctly configures CNG key specifications (`dwKeySpec = 0`) for Windows SChannel, Leo4Proxy, and mTLS connections without `SEC_E_UNKNOWN_CREDENTIALS` (`0x8009030D`) errors.

---

## 2. Quick Start (Recommended)

1. Right-click **`install_cert.cmd`** and select **Run as administrator** (or double-click it and accept the Windows UAC elevation prompt).
2. Enter the **6-character PIN code** provided by your system administrator.
3. The script will automatically:
   - Discover the target endpoint (via local `leo4proxy` or `https://iot-processing.ru/api/certificates`).
   - Generate an RSA 2048 key pair inside the Windows CNG secure store.
   - Send the PKCS#10 Certificate Signing Request (CSR).
   - Install the signed certificate chain into `LocalMachine\MY`.
   - Remove obsolete certificates.

---

## 3. Command-Line Reference

```cmd
l4pin.exe <PIN> [options]
l4pin.exe --pin <PIN> [options]
l4pin.exe -pin <PIN> [options]
l4pin.exe --status [--store <machine|user>]
```

### Options & Flags

| Flag | Description | Default |
|---|---|---|
| `<PIN>` / `--pin <PIN>`, `-pin <PIN>`, `-p` | 6-character terminal certificate PIN code | Prompted if omitted |
| `--status`, `-status`, `-l`, `--list` | List installed certificates in the target store | - |
| `--store <machine\|user>`, `-store`, `-s` | Target certificate store (`machine` = `LocalMachine\MY`, `user` = `CurrentUser\MY`) | `machine` |
| `--url <URL>`, `-url <URL>`, `-u` | Base URL override of the certificate enrollment endpoint | Auto-detected via `leo4proxy` -> fallback |
| `--key-name <NAME>`, `-key-name`, `-k` | CNG key container name | `EtranTerminalKey` |
| `--email <email>`, `-email`, `-e` | Filter certificate list by email address | - |
| `--help`, `-help`, `-h`, `/?` | Display usage information | - |

### Examples

```cmd
:: Enroll certificate with PIN 021358 into LocalMachine\MY (auto-detect URL)
l4pin.exe 021358

:: Enroll certificate with explicit flags
l4pin.exe --pin 021358 --store machine

:: Check all installed certificates in LocalMachine\MY
l4pin.exe --status

:: Install into CurrentUser store (for testing)
l4pin.exe --pin 021358 --store user
```

---

## 4. Verification

To verify that the certificate is properly installed:

1. **Via CLI**:
   ```cmd
   install_cert.cmd --status
   ```
   or:
   ```cmd
   l4pin.exe --status
   ```
2. **Via Windows Certificate Manager**:
   - Press `Win + R`, type `certlm.msc`, and press Enter.
   - Navigate to **Certificates (Local Computer) -> Personal -> Certificates**.
   - Verify that your terminal certificate is present with the private key icon.

---

## 5. Troubleshooting & Exit Codes

| Exit Code | Meaning | Resolution |
|:---:|---|---|
| **0** | Success | Certificate installed and bound to CNG private key. |
| **1** | General error / Invalid arguments | Check arguments or execute with `--help`. |
| **2** | PIN not found / invalid | Ensure PIN code was copied accurately from the admin portal. |
| **3** | PIN already used | PIN codes are single-use. Request a new PIN from the administrator. |
| **4** | PIN expired | PIN codes expire after validity window. Request a new PIN. |
| **5** | Network / HTTP error | Check internet access and firewall connectivity. |
| **6** | Crypto / Store error | Run from an elevated Administrator command prompt. |
