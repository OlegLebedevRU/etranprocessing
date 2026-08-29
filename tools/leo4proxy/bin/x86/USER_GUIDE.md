# Leo4Proxy - User & Operator Guide

## 1. Overview

`leo4proxy` is a zero-dependency, high-performance Windows background service written in native C (Win32 / SChannel). It provides:
1. **Forward mTLS Proxy**: Enables legacy terminal applications (HTTP/MQTT) to communicate with the processing center over mutual TLS (port `4443`) using client certificates stored in Windows `LocalMachine\MY`.
2. **Reverse HTTPS Gateway**: Securely exposes local terminal interfaces over HTTPS (port `443`) with automatic mDNS / LLMNR local network discovery (`terminal.local`).

### Key Features
- **Zero External Dependencies**: Pure Win32/SChannel. No OpenSSL, .NET runtime, or Visual C++ Redistributable required (`/MT` static binary).
- **Universal Architecture**: Native **32-bit (x86)** and **64-bit (x64)** support across Windows 7 POSReady / 8 / 10 / 11.
- **Resource Footprint**: Minimal memory (< 5 MB RAM) and near-zero CPU usage.
- **Automated Service & Firewall**: Complete set of `.cmd` automation scripts with built-in UAC elevation and Windows Defender Firewall configuration.

---

## 2. Quick Start

### Step 1: Install Client Certificate
Before starting `leo4proxy`, ensure the terminal certificate is installed into `LocalMachine\MY` using `terminal-cert-installer` (see `install_cert.cmd`).

### Step 2: Install and Start Leo4Proxy Service
1. Right-click **`leo4proxy_install_start.cmd`** and select **Run as administrator** (or double-click and accept UAC elevation).
2. The installer will automatically:
   - Configure Windows Defender Firewall rules for inbound ports (`443/TCP`, `80/TCP`, `5353/UDP`, `5355/UDP`).
   - Register the Windows Service `Leo4Proxy` set to Automatic startup (`start=auto`).
   - Start the service.

### Step 3: Verify Status
Run **`leo4proxy_status.cmd`** to verify that the service is running and the certificate in `LocalMachine\MY` is recognized.

---

## 3. Management Scripts

| Script Name | Purpose | Description |
|---|---|---|
| **`leo4proxy_install_start.cmd`** | Full Setup | Installs firewall rules, registers Windows service, and starts it. |
| **`leo4proxy_status.cmd`** | Diagnostic | Checks Windows Service status (SCM) and inspects certificate in store. |
| **`leo4proxy_restart.cmd`** | Restart | Restarts the running `Leo4Proxy` service. |
| **`leo4proxy_start.cmd`** | Start | Starts the installed service. |
| **`leo4proxy_stop.cmd`** | Stop | Stops the service and any running standalone instances. |
| **`leo4proxy_stop_uninstall.cmd`** | Removal | Stops service, removes it from SCM, and removes firewall rules. |

---

## 4. Default Network Topology

```
+-------------------------------------------------------------------------+
| POS Terminal (Local Machine)                                            |
|                                                                         |
|  [Terminal App] ---> 127.0.0.1:1883 (MQTT) ----+                        |
|  [Legacy App]   ---> 127.0.0.1:8080 (HTTP) ----+                        |
|                                                |                        |
|                                                v                        |
|                                       +------------------+              |
|                                       |    leo4proxy     |              |
|                                       | (SChannel mTLS)  | ===(mTLS)===> Processing Center
|                                       +------------------+               (iot.leo4.ru:4443)
|                                                ^                        |
|  [Local Browser / Admin] ---> :443 (HTTPS) ----+                        |
|                                                |                        |
|                                                v                        |
|                                 127.0.0.1:8000 (Local Web UI)           |
+-------------------------------------------------------------------------+
```

- **Local MQTT Proxy**: `127.0.0.1:1883` $\rightarrow$ Remote `iot.leo4.ru:4443` (with client certificate).
- **Local HTTP Proxy**: `127.0.0.1:8080` $\rightarrow$ Remote `iot.leo4.ru:4443` (with client certificate).
- **Reverse HTTPS Gateway**: `0.0.0.0:443` $\rightarrow$ Local `127.0.0.1:8000` (Terminal UI).
- **LAN Discovery**: mDNS (`5353/UDP`) and LLMNR (`5355/UDP`) resolve `https://terminal.local`.

---

## 5. Command-Line Reference

```cmd
leo4proxy.exe [options]
```

### Key Options

| Option | Description |
|---|---|
| `--status` | Check Windows service state and certificate availability |
| `--test-cert` | Verify TLS handshake capability with SChannel and certificate |
| `--get-sn` | Print certificate Subject Name, Serial, and Thumbprint |
| `--console` | Run in foreground interactive console mode (with tray icon) |
| `--verbose`, `-v` | Enable detailed diagnostic log output |
| `--no-reverse` | Disable inbound reverse HTTPS gateway (forward proxy only) |
| `--reverse-target <host:port>` | Target endpoint for reverse proxy (default: `127.0.0.1:8000`) |
| `--cert-email <email>` | Select certificate matching specific email in `LocalMachine\MY` |
| `--cert-thumbprint <hex>` | Select certificate matching specific SHA-1 thumbprint |
| `--cert-poll-interval <sec>` | Polling interval in seconds for certificate changes in service mode (default: `30`) |
| `--drop-on-expire` | Transition to standby mode if certificate expires and no valid replacement exists |
| `--install` / `--uninstall` | Register or unregister Windows Service |
| `--start` / `--stop` | Start or stop Windows Service |
| `--help` | Display usage and all CLI flags |

---

## 6. Troubleshooting

- **Service fails to start (`Error: No valid client certificate found`)**:
  - Run `install_cert.cmd` from `terminal-cert-installer` to enroll a valid certificate.
  - Run `leo4proxy_status.cmd` to verify certificate presence in `LocalMachine\MY`.
- **Port 443 already in use**:
  - Check if IIS or another web server is occupying port 443 (`netstat -ano | findstr :443`).
  - Run `leo4proxy.exe --reverse-port 8443` or use `--no-reverse` if reverse proxy is not required.
- **Firewall blocking incoming traffic**:
  - Re-run `leo4proxy_install_start.cmd` with Administrator privileges to reset firewall rules.
