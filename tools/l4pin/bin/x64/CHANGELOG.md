# Changelog — l4pin

All notable changes to the `l4pin` (Leo4 Terminal Certificate Installer) component will be documented in this file.

## Unreleased (1.8.1 candidate)

- Default graphical enrollment flow with masked PIN, issuer-matched certificate list, expiry guard and explicit replacement confirmation; CLI remains available for unattended use.
- New CNG container per enrollment; local failures clean up only the newly created key. Added operation, store and status diagnostics without logging PIN or CA response bodies.
- Stage and verify the new certificate/key binding before retiring matching old certificates. Live certificate issuance and failure recovery remain outside the current test plan.

## [1.7.2] - 2026-09-14

### Changed
- Версионирование компонентов синхронизировано до 1.7.2.

## [1.7.0] - 2026-09-13

### Changed
- **Умный Cert Guard**: автоматическое разрешение обновления сертификата по новому PIN-коду при сроке действия <= 30 дней без флага принудительного перевыпуска (`--force` / `--force-reissue`).

## [1.2.0] - 2026-09-12

### Added
- **Certificate Discovery Module (`cert_discovery.c` / `cert_discovery.h`)**:
  - Implemented standalone discovery engine adhering to Stage 1 Contract 4.1.
  - Inspects `LocalMachine\MY` (or `CurrentUser\MY`) targeting issuer CN containing `iot.leo4.ru`.
  - Verifies CNG private key accessibility (`CryptAcquireCertificatePrivateKey` with `CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_CACHE_FLAG`) and non-expired status (`NotAfter > now`).
  - Evaluates candidate certificates, selects candidate with maximum `NotAfter`, and tallies duplicate candidate count (`cert_duplicates`).
  - Returns canonical states: `CERT_VALID` (`0`), `CERT_EXPIRING` (`1`), `CERT_BROKEN` (`2`), `CERT_ABSENT` (`3`), `CERT_STORE_ERROR` (`>= 20`).
- **Enrollment Pre-flight Guard ("Do Not Touch Valid Certificate")**:
  - Before initiating network calls or `GET /api/certificates/?function=check`, `l4pin.exe <PIN>` queries `cert_discover`.
  - If existing certificate is `CERT_VALID` and `--force` is absent: logs discovery state, outputs notification `Certificate <thumbprint> for <SN> is valid until <NotAfter>; reissue not required (use --force to override)`, and terminates with exit code `0` with zero network calls and zero store mutations.
  - If existing certificate is `CERT_EXPIRING`: issues warning and continues with certificate renewal.
  - Supports `--force` (`-force`, `-f`) to override existing valid certificate and force full reissue flow.
- **CLI Check Mode (`--check`)**:
  - Added non-destructive inspection command: `l4pin.exe --check [--sn <SN>] [--json] [--store <machine|user>]`.
  - Supports human-readable output as well as structured JSON: `{"state":"valid|expiring|broken|absent", "thumbprint", "sn", "not_after", "days_left"}`.
  - Returns matching exit codes: `0` (valid), `1` (expiring), `2` (broken), `3` (absent), or `>= 20` (store error).
- **Unit Test Suite (`tests/test_cert_discovery.c`)**:
  - Added standalone in-memory test runner creating temporary self-signed certificates with CNG keys in `CERT_STORE_PROV_MEMORY`.
  - Covers 7 unit scenarios: ABSENT, VALID, EXPIRING, BROKEN (missing key), BROKEN (wrong SN), DUPLICATES (highest NotAfter selection), and non-Leo4 issuer rejection.
- **Application Manifest**:
  - Added `res/l4pin.manifest` specifying `requestedExecutionLevel level="asInvoker"` and Windows compatibility IDs to eliminate legacy UAC Installer Detection heuristic for 32-bit binaries.

### Changed
- **Privacy & Security Masking**:
  - Masked PIN code display across console output and logs (`pin=***`).
  - Enhanced certificate cleanup logging to output deleted certificate thumbprint, serial, and subject without logging PIN or CA response payloads.
- **Resource Metadata**:
  - Updated `res/app.rc` with standard `VS_VERSION_INFO` version `1.2.0.0` and embedded application manifest.
- **Build System**:
  - Integrated `cert_discovery.c` and in-memory unit test execution into `build.cmd` (`build.cmd all`, `build.cmd test`).

---

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
