# Etran Terminal Certificate Installer - User Guide

## 1. Overview

`terminal-cert-installer` is a standalone, lightweight CLI utility for Windows that requests, generates, and installs a mutual TLS (mTLS) client certificate on payment kiosks and POS terminals using a single-use 6-character PIN code.

### Key Features
- **Zero Dependencies**: Native C / Win32 / CNG binary. No .NET runtime, Python, or Visual C++ Redistributables required (`/MT` static link).
- **Universal Architecture**: Fully compatible with both **32-bit (x86)** and **64-bit (x64)** Windows (including Windows 7 Embedded / POSReady 7).
- **Secure Key Storage (CNG)**: Private keys are generated in `Microsoft Software Key Storage Provider` and marked as **non-exportable** (`NCRYPT_ALLOW_EXPORT_NONE`).
- **Auto-Cleanup**: Automatically detects and cleans up expired/obsolete terminal certificates matching the terminal's email address in `LocalMachine\MY`.
- **SChannel Ready**: Correctly configures CNG key specifications (`dwKeySpec = 0`) for Windows SChannel, Leo4Proxy, and mTLS connections without `SEC_E_UNKNOWN_CREDENTIALS` (`0x8009030D`) errors.

---

## 2. Quick Start (Recommended)

1. Right-click **`install_cert.cmd`** and select **Run as administrator** (or double-click it and accept the Windows UAC elevation prompt).
2. Enter the **6-character PIN code** provided by your system administrator.
3. The script will automatically:
   - Contact the enrollment server (`https://iot-processing.ru/certificates/Dispatcher.ashx`).
   - Generate an RSA 2048 key pair inside the Windows CNG secure store.
   - Send the PKCS#10 Certificate Signing Request (CSR).
   - Install the signed certificate chain into `LocalMachine\MY`.
   - Remove obsolete certificates.

---

## 3. Command-Line Reference

```cmd
terminal-cert-installer.exe <PIN> [options]
terminal-cert-installer.exe --pin <PIN> [options]
terminal-cert-installer.exe --status [--store <machine|user>]
```

### Options & Flags

| Flag | Description | Default |
|---|---|---|
| `<PIN>` / `--pin <PIN>`, `-p` | 6-character terminal certificate PIN code | Prompted if omitted |
| `--status`, `-l` | List installed certificates in the target store | - |
| `--store <machine\|user>`, `-s` | Target certificate store (`machine` = `LocalMachine\MY`, `user` = `CurrentUser\MY`) | `machine` |
| `--url <URL>`, `-u` | Base URL of the certificate enrollment endpoint | `https://iot-processing.ru/certificates/Dispatcher.ashx` |
| `--key-name <NAME>`, `-k` | CNG key container name | `EtranTerminalKey` |
| `--email <email>`, `-e` | Filter certificate list by email address | - |
| `--help`, `-h`, `/?` | Display usage information | - |

### Examples

```cmd
:: Enroll certificate with PIN B75GL9 into LocalMachine\MY
terminal-cert-installer.exe B75GL9

:: Check all installed certificates in LocalMachine\MY
terminal-cert-installer.exe --status

:: Install into CurrentUser store (for testing)
terminal-cert-installer.exe --pin B75GL9 --store user
```

---

## 4. Verification

To verify that the certificate is properly installed:

1. **Via CLI**:
   ```cmd
   install_cert.cmd --status
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
| **5** | Network / HTTP error | Check internet access and firewall connectivity to `iot-processing.ru`. |
| **6** | Crypto / Store error | Run from an elevated Administrator command prompt. |
