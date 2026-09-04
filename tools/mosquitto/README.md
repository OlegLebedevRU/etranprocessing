# Mosquitto MQTT Broker (No-SSL Bridge)

Local lightweight MQTT broker for `PlaterraTerminal` kiosk software with upstream bridge functionality forwarding traffic through the native `leo4proxy` mTLS tunnel.

Build: Standalone 32-bit executable `mosquitto.exe` (Win32, `/MT`, no OpenSSL/TLS external dependencies), fully compatible with Windows 7, 10, 11, and Windows Embedded/POSReady (both x86 and x64 architectures).

---

## 1. Architecture & Integration with Native Proxy (`leo4proxy`)

The Mosquitto service acts as a local queue and topic multiplexer for internal terminal processes, aggregating their connections into a single upstream channel to the cloud broker.

```
+-------------------------------+
| PlaterraTerminal (UI / Master)| --(TCP :1883, ClientId="ui")----+
+-------------------------------+                                 |
+-------------------------------+                                 v
| PlaterraTerminalService (Svc) | --(TCP :1883, ClientId="svc")--> [Mosquitto Broker :1883]
+-------------------------------+                                 |   (Local, Plain TCP)
                                                                  |
                                   [Bridge Upstream: 127.0.0.1:18883]
                                   (remote_clientid = <SN>, MQTT 5.0)
                                                                  |
                                                                  v
                                                      +-------------------------------+
                                                      |    leo4proxy (:18883)         |
                                                      | (Native Win32 SChannel mTLS)  |
                                                      |  - Hardware-bound cert from   |
                                                      |    Windows Store (MY / System)|
                                                      +-------------------------------+
                                                                  |
                                                                  | (Outbound mTLS :8883)
                                                                  v
                                                      +-------------------------------+
                                                      |     Server MQTT Broker        |
                                                      |     (dev.leo4.ru:8883)        |
                                                      +-------------------------------+
```

### Key Advantages of Pairing with `leo4proxy`:
- **Zero OpenSSL Dependencies on Broker:** Mosquitto operates purely as a local TCP broker/client and does not require TLS certificates or OpenSSL DLLs on disk.
- **Hardware-Bound Authentication:** `leo4proxy` offloads mTLS handshakes to native Windows SChannel using the device private key stored in the secure Windows Certificate Store.
- **Single Connection to Cloud Broker:** The remote broker sees strictly 1 persistent connection with `client_id = <SN>`, avoiding session collision loops (*Client Takeover / Flapping*).

---

## 2. Working Directory & `%MOSQUITTO_DIR%` Environment Variable

### How Mosquitto Locates Configuration in Windows Service Mode
When the Windows Service Control Manager starts `mosquitto.exe run`:
1. The process queries the system environment variable `%MOSQUITTO_DIR%`.
2. The configuration file is resolved strictly at: `%MOSQUITTO_DIR%\mosquitto.conf`.
3. **CRITICAL:** If `%MOSQUITTO_DIR%` is undefined or points to a non-existent directory, the service will immediately terminate (`SERVICE_STOPPED`).

### How to Configure or Relocate the Working Directory
To set or change the configuration folder:
1. Set the system-level (`Machine` / `HKLM`) environment variable `MOSQUITTO_DIR`:
   - **Via PowerShell (Run as Administrator):**
     ```powershell
     [System.Environment]::SetEnvironmentVariable('MOSQUITTO_DIR', 'D:\Platerra26\tools\mosquitto', [System.EnvironmentVariableTarget]::Machine)
     ```
   - **Via Command Prompt (Run as Administrator):**
     ```cmd
     setx /M MOSQUITTO_DIR "D:\Platerra26\tools\mosquitto"
     ```
2. Or run the installer script with the target directory parameter:
   ```powershell
   .\install_mosquitto.ps1 -TargetDir "C:\Platerra\tools\mosquitto"
   ```
   *(The script automatically updates `MOSQUITTO_DIR` and registers the service with SCM).*

---

## 3. Installation Methods & Execution

All installation and service management scripts automatically detect permission level and request **Administrator (UAC)** elevation if launched from an standard user / unelevated session.

### Option A: One-Click Installation (Batch)
Execute:
```cmd
D:\Platerra26\tools\mosquitto\install.cmd
```
*(or `install_mosquitto.cmd`)*. The script requests UAC elevation if needed, auto-detects the device serial number, generates configuration files, sets system environment variables, registers the Windows service with automatic restart on failure, and starts it.

### Option B: PowerShell Execution with Parameters
```powershell
powershell.exe -ExecutionPolicy Bypass -File .\install_mosquitto.ps1 [-TargetDir <Path>] [-CustomSn <SerialNumber>]
```

**Parameters:**
- `-TargetDir` *(Default: script directory)* -- Target installation folder containing `mosquitto.exe`, `mosquitto.conf`, and logs.
- `-CustomSn` *(Optional)* -- Manually override the device serial number, bypassing auto-detection.

### Device SN Discovery Workflow:
1. Queries CLI `leo4proxy.exe --get-sn` in adjacent and standard application directories.
2. Fallback: Queries local HTTP REST endpoint `http://127.0.0.1:18443/_leo4/sn` of a running `leo4proxy` service.
3. **Neutral Mode (Fallback):** If `leo4proxy` is not found or no certificate is present, a neutral `mosquitto.conf` is generated without the upstream bridge section (broker serves local processes without attempting outbound connections).

---

## 4. Features of `mosquitto.conf`

When a device serial number is available, the generated configuration follows this structure:

```conf
# ==============================================================================
# Mosquitto MQTT Broker Configuration
# Generated automatically for Device SN: a4b0000773c82116d210826
# ==============================================================================

# Local listener for internal terminal processes
listener 1883 127.0.0.1
allow_anonymous true

# Bridge configuration to leo4proxy (Native mTLS tunnel)
connection platerra-upstream
bridge_protocol_version mqttv50
address 127.0.0.1:18883

# Remote client identifier for external broker
remote_clientid a4b0000773c82116d210826

# Disable Mosquitto $SYS status topics (required for external broker compatibility)
try_private false
notifications false

# Topic routing rules (topic <pattern> <direction> <QoS>)
# 1) Outbound responses and events from terminal to server:
topic dev/a4b0000773c82116d210826/out out 0
topic dev/a4b0000773c82116d210826/# out 1

# 2) Inbound commands from server to terminal:
topic srv/a4b0000773c82116d210826/rsp in 1
topic srv/a4b0000773c82116d210826/# in 1

# Connection reliability and keep-alive
cleansession true
restart_timeout 5 60
keepalive_interval 60

# Persistence and logging
persistence false
log_dest file D:/Platerra26/tools/mosquitto/log/mosquitto.log
log_type error
log_type warning
log_type notice
log_type information
```

### Essential Formatting Rules on Windows:
1. **UTF-8 No-BOM Encoding:** Mosquitto parser does not support Byte Order Marks (BOM `EF BB BF`). Files must be saved strictly in UTF-8 without BOM or pure ASCII.
2. **Forward Slashes in Paths:** Directives such as `log_dest file` and `persistence_location` must use forward slashes `/` (e.g. `D:/Platerra26/tools/mosquitto/log/mosquitto.log`).
3. **`try_private false` & `notifications false`:** Mandatory flags when bridging to third-party MQTT brokers (EMQX, VerneMQ, RabbitMQ). Prevents Mosquitto from sending internal `$SYS` topic loops.
4. **Topic Isolation via ACL:** An `acl.conf` file is created for role segregation between `main_app` and `extra_service`. Uncomment `acl_file` in `mosquitto.conf` if ACL enforcement is needed.

---

## 5. Troubleshooting & Diagnostics

```cmd
:: Test configuration syntax
mosquitto.exe -c mosquitto.conf --test-config

:: Query Windows service status
sc.exe query mosquitto

:: Verify local port 1883 listening status
netstat -ano | findstr :1883

:: Inspect broker runtime logs
type log\mosquitto.log
```
