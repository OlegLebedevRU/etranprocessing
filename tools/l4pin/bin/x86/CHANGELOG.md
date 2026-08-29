# Changelog — l4pin

All notable changes to the `l4pin` (Leo4 Terminal Certificate Installer) component will be documented in this file.

## [1.1.0] - 2026-08-29

### Added
- **Smart Endpoint Discovery (`url-finder`)**:
  - Implemented 3-tier endpoint resolution strategy: explicit CLI argument -> `leo4proxy` auto-detection -> default fallback.
  - Auto-discovery queries local `leo4proxy` metadata (`https://127.0.0.1/_leo4/info?format=json` and `http://127.0.0.1:18443/_leo4/info`), extracts `listeners.http_local`, and constructs active target endpoint (`/api/certificates`).
  - Added fast probing (`http_get_simple`) with connection timeouts.
- **Universal Architecture Support (x86 / x64)**:
  - Standardized on a universal 32-bit (x86) `/MT` statically-linked binary (`bin/l4pin.exe`) that executes natively on 32-bit Windows systems (Windows 7 Embedded / POSReady 7) and seamlessly on 64-bit Windows via WOW64.
  - Verified cross-architecture compatibility: system-wide CNG KSP (`Microsoft Software Key Storage Provider`) and Windows Certificate Store (`LocalMachine\MY`) share unified state across 32-bit and 64-bit processes.
- **Enhanced CLI Options**:
  - Added support for single-dash aliases (`-pin`, `-url`, `-store`, `-status`, `-help`) alongside standard GNU options (`--pin`, `--url`, etc.).
  - Added positional PIN argument support (`l4pin.exe <PIN>`).

### Changed
- **Component Renaming**:
  - Renamed utility and repository directory from `terminal-cert-installer` to `l4pin` (`l4pin.exe`).
  - Updated CMake configuration, Windows resource metadata (`app.rc`), launch scripts (`install_cert.cmd`), and documentation (`README.md`, `USER_GUIDE.md`).
- **Endpoint Normalization**: Updated default base path and URL normalization to `/api/certificates`.
- **Build System**: Updated `build.cmd` to build both `bin/x86/` and `bin/x64/` and deploy universal default binary to `bin/l4pin.exe`.

---

## [1.0.0] - 2026-08-24

### Initial Release (as `terminal-cert-installer`)
- **PIN-based Certificate Enrollment Flow (`v=26`)**:
  - `GET /api/certificates/?function=check&pin=<PIN>&tosign=<sysinfo>&v=26` validation.
  - RSA 2048 key pair generation via CNG (`NCryptCreatePersistedKey`).
  - Enforced **non-exportable** private key security (`NCRYPT_ALLOW_EXPORT_NONE = 0`).
  - PKCS#10 Certificate Signing Request (CSR) generation with signature binding (`CN=<sign>`).
  - `POST /api/certificates/?function=setup&pin=<PIN>&cpserial=<sign>` CSR submission.
  - PKCS#7 certificate chain decoding and installation into Windows Certificate Store.
- **Automatic Certificate Lifecycle Management**:
  - Automatic detection and removal of obsolete/expired certificates matching terminal email in `LocalMachine\MY` / `CurrentUser\MY`.
  - Proper SChannel CNG key property binding (`CERT_KEY_PROV_INFO_PROP_ID`, `dwKeySpec = 0`) to prevent `SEC_E_UNKNOWN_CREDENTIALS` (`0x8009030D`).
- **Zero External Dependencies**: Native Win32 / WinHTTP / Crypt32 / NCrypt implementation with static CRT `/MT` linking.
- **Interactive Helper & UAC Automation**: `install_cert.cmd` script for automated Administrator elevation and PIN prompt.
- **Certificate Inspection CLI**: `--status` command to list and verify installed certificates and private key bindings.
