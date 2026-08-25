# Etran Terminal Tools - End-User Deployment Guide

## 1. Package Overview

This directory contains native Windows utilities and services designed for self-service payment kiosks and POS terminals connecting to the Etran processing platform.

| Tool | Subdirectory | Primary Executable / Script | Description |
|---|---|---|---|
| **Terminal Certificate Installer** | `terminal-cert-installer/` | `install_cert.cmd` / `terminal-cert-installer.exe` | Enrolls and installs terminal client certificate via single-use PIN into `LocalMachine\MY`. |
| **Leo4Proxy Service** | `leo4proxy/` | `leo4proxy_install_start.cmd` / `leo4proxy.exe` | SChannel mTLS forward proxy and reverse HTTPS LAN gateway background service. |

### System Requirements & Compatibility
- **Supported Operating Systems**: Windows 7 / POSReady 7 / Embedded Standard 7, Windows 8 / 8.1, Windows 10, Windows 11, Windows Server 2008 R2+.
- **Architectures**: Native **32-bit (x86)** and **64-bit (x64)** support.
- **Dependencies**: **Zero** external runtime dependencies. Compiled with static MSVC CRT (`/MT`). No Visual C++ Redistributable or .NET runtime needed.
- **Privileges**: Administrator privileges required for certificate store installation and Windows Service registration (automated UAC elevation included).

---

## 2. Standard Terminal Deployment (2-Step Setup)

Deploying a new terminal or updating credentials takes two simple steps:

### Step 1: Enroll Client Certificate
1. Open the `terminal-cert-installer/` folder.
2. Double-click **`install_cert.cmd`** (or right-click -> *Run as administrator*).
3. Accept the Windows UAC elevation prompt if prompted.
4. Enter the **6-character PIN code** generated in the Etran Admin Portal.
5. The certificate will be automatically enrolled and stored in `LocalMachine\MY`.

### Step 2: Install and Start Proxy Service
1. Open the `leo4proxy/` folder.
2. Double-click **`leo4proxy_install_start.cmd`** (or right-click -> *Run as administrator*).
3. The script configures Windows Firewall rules, registers the `Leo4Proxy` service, and starts it.
4. Run **`leo4proxy_status.cmd`** to confirm the service is running (`RUNNING`) and bound to your terminal certificate.

---

## 3. Tool Reference Summaries

### A. Terminal Certificate Installer (`terminal-cert-installer/`)
- **Interactive Script**: `install_cert.cmd`
- **CLI Commands**:
  - `terminal-cert-installer.exe <PIN>` — Request and install certificate with PIN.
  - `terminal-cert-installer.exe --status` — View installed certificates in `LocalMachine\MY`.
- **User Guide**: See [`terminal-cert-installer/USER_GUIDE.md`](terminal-cert-installer/USER_GUIDE.md).

### B. Leo4Proxy (`leo4proxy/`)
- **Management Scripts**:
  - `leo4proxy_install_start.cmd` — Install Windows Service + Firewall rules + Start.
  - `leo4proxy_status.cmd` — Check service status and certificate binding.
  - `leo4proxy_restart.cmd` — Restart service.
  - `leo4proxy_stop_uninstall.cmd` — Stop and uninstall service.
- **Port Mapping**:
  - Local MQTT: `127.0.0.1:1883` $\rightarrow$ Remote `iot.leo4.ru:4443` (mTLS)
  - Local HTTP: `127.0.0.1:8080` $\rightarrow$ Remote `iot.leo4.ru:4443` (mTLS)
  - Reverse HTTPS: `0.0.0.0:443` $\rightarrow$ Local `127.0.0.1:8000` (Kiosk UI)
- **User Guide**: See [`leo4proxy/USER_GUIDE.md`](leo4proxy/USER_GUIDE.md).
