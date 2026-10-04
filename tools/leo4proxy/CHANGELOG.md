# Changelog — Leo4Proxy

All notable changes to the `leo4proxy` component will be documented in this file.

## [1.8.1] - 2026-10-04 (signed; published)

- Share an absolute deadline across SRV/A/TCP/TLS; reserve recovery time for IP fallback and remove duplicate endpoints/IPv4 addresses.
- Try up to eight IPv4 addresses per endpoint; evict completed DNS slots while keeping the 24-worker limit and numeric bypass.
- Probe enabled channels concurrently with independent 8-second budgets; include elapsed time, attempts and last selected endpoint in JSON.
- Bound partial writes and fragmented policy TLS reads; finish framed HTTP responses without waiting for EOF and reject malformed lengths/chunks.
- Keep logical TLS identity, embedded CA and admission gates mandatory. Correct obsolete insecure-default help text.

## [1.8.0] - 2026-10-04

- Add bounded SRV resolution and per-channel policy host/IP fallback, with explicit CLI precedence and SN-bound expiring cache.
- Connect to numeric IPs while retaining logical TLS/SNI and HTTP Host; validate server identity against the embedded iot.leo4.ru CA on all outgoing channels.
- Recover policy through provisioned bootstrap IP without DNS; preserve admission deny/grace and isolate routing storage.
- Add read-only --check-upstream and --check-policy-bootstrap diagnostics; expose connected source/target in local info.

## [1.7.3] - 2026-10-02

- Activate only currently valid iot.leo4.ru certificates with an accessible key;
  legacy certsrv stays in certificate-wait mode, including explicit thumbprint selection.
- Choose the latest NotAfter within certificate priority; retain diagnostic listeners in standby.
- Hold TLS credentials/certificate references until sessions close; use unique ownership tokens
  so recycled SSPI handles cannot revive old references. Snapshot request identity on rotation.
- Retain live listener state on stop timeout and retry after exit; fence old MQTT/stream workers.
- Write crash diagnostics beside the EXE and never pause for console input in a service crash.
- Add x86/x64 certificate, ownership, loopback TLS retirement and existing policy checks.
## [1.7.2] - 2026-10-01

### Added
- Terminal policy polling over certificate-authenticated HTTPS every 600 seconds. A server deny closes external MQTT/media connections and prevents reconnect while retaining local listeners, identity readiness, incoming HTTPS and policy polling.
- Persisted MQTT/RTP decision and 72-hour API-outage grace in registry (shared x86/x64 view) and atomic JSON copy, with customer-favoring recovery and accessible storage fallbacks. No certificate removal or tamper sanctions.
- Independent outgoing HTTPS permission: only explicit false in a valid policy blocks ordinary routes; policy, certificates and licensebilling remain available. Missing/invalid policy defaults HTTPS to allowed.
- Local info policy diagnostics and x86/x64 tests for schema, grace, storage recovery, HTTPS exceptions, socket cancellation and UDP draining.

### Changed
- Executable and Windows resource versions synchronized to 1.7.2. No MQTT presence/topic changes; leo4proxy remains a transparent proxy. The l4tools package is not rebuilt in this task.

## [1.7.1] - 2026-09-13

### Changed
- Включение rtp_tunnel_enabled: true по умолчанию (Lazy Connect для видеопотока L4RTP/1) и запуск службы Leo4Proxy с аргументом --rtp-tunnel.

## [1.2.0] - 2026-09-07

### Added
- **Primary Media Video Mode: RTP/RTCP UDP -> framed mTLS/TCP tunnel (`--rtp-tunnel`, L4RTP/1)**:
  - Added native `rtp_tunnel` module (`src/rtp_tunnel.c`, `src/rtp_tunnel.h`) for capturing local RTP (`:5004`) and RTCP (`:5005`) UDP datagrams on loopback and encapsulating them into framed mTLS TCP tunnel (protocol `L4RTP/1`) targeting a single external endpoint (`dev.leo4.ru:8443`).
  - Wire protocol L4RTP/1:
    - Preamble: Magic "L4RT", version 0x01, uint16 big-endian SN length, followed by SN extracted strictly from client certificate (`certDetails->sn`).
    - Frames: Type (0x01 RTP, 0x02 RTCP, 0x03 keepalive), flags 0x00, uint16 big-endian payload length, raw UDP datagram. Header and payload sent in single `schannel_send()` call.
  - Strict Lazy Connect lifecycle: outbound mTLS connection is opened only when first local UDP packet arrives from ffmpeg, and closed gracefully after idle timeout (`--rtp-idle-timeout`, default 30s).
  - Exponential Backoff on network failure: delay starts at 3s and doubles up to 30s, interruptible sleep in 50ms slices, dropping local datagrams without memory allocation during outage (`rtp_tunnel_dropped_no_upstream`). Reconnect is attempted only if video traffic continues.
  - Sockets configured with `SO_RCVBUF = 512 KB` to absorb H.264 IDR packet bursts without packet drop; `SO_REUSEADDR` intentionally omitted.
  - Added CLI options: `--rtp-tunnel`, `--no-rtp-tunnel`, `--rtp-local`, `--rtp-port`, `--rtcp-port`, `--rtp-remote`, `--rtp-idle-timeout`, `--rtp-reconnect`.
  - Service lifecycle support: start/stop/restart on certificate rotation, standby on certificate deletion/expiration.
  - Diagnostic API (`/_leo4/info`) extended with `"rtp_tunnel_enabled"`, local/remote listener endpoints, and transmission metrics.
  - Companion example script: `examples/ffmpeg_rtp_tunnel_example.cmd`.
- Preserved existing `--stream` as optional legacy TCP stream forwarder.

## [1.1.0] - 2026-09-07

### Added
- **Stream Forwarder (`--stream`)**:
  - Added transparent plain-TCP to SChannel mTLS forwarder (`src/stream_proxy.c`, `src/stream_proxy.h`) for streaming video from terminal cameras (`ffmpeg.exe`, RTSP interleaved, MPEG-TS, RTP-over-TCP) into cloud media ingress (`dev.leo4.ru:8443`).
  - Added CLI flags:
    - `--stream`: enable stream forwarder (opt-in, default: disabled).
    - `--no-stream`: disable stream forwarder.
    - `--stream-local <ip:port>`: local TCP listener (default: `127.0.0.1:8554`).
    - `--stream-remote <host:port>`: remote cloud ingress (default: `dev.leo4.ru:8443`).
    - `--stream-max-clients <n>`: maximum concurrent client connections (default: `2`, min: 1).
    - `--stream-idle-timeout <sec>`: idle timeout in seconds before closing tunnel (default: `30`, 0 = disabled).
  - Diagnostic API (`/_leo4/info`) enhancements:
    - Added top-level `"stream_enabled": true|false`.
    - Added `"stream_local"` in `listeners` (`"127.0.0.1:8554"` or `"127.0.0.1:8554 (disabled)"`).
    - Added `"stream_remote"` in `upstreams` (`"dev.leo4.ru:8443"`).
    - Added `"stream_active_clients"`, `"stream_total_connections"`, `"stream_bytes_up"`, `"stream_bytes_down"` in `clients`.
  - Windows Service integration: automatic start with service, hot-swap reconnection on certificate rotation, standby on certificate deletion/expiration, and clean shutdown.
  - Companion example script: `examples/ffmpeg_stream_example.cmd`.

---

## [1.1.1] - 2026-08-29

### Fixed
- **Certificate Removal / Deletion Handling**:
  - Fixed issue where deleting a certificate from Windows Certificate Store (`LocalMachine\MY`) left proxy running with stale in-memory credentials.
  - Proxy worker thread now detects certificate disappearance (`!foundBest`), immediately stops mTLS listeners/tunnels (`:18883`, `:443`), frees SChannel credentials, clears certificate details, and resets state to `waiting_for_certificate` (Standby mode).

---

## [1.1.0] - 2026-08-29

### Added
- **Runtime Certificate Polling & Hot-Reloading in Service Mode**:
  - Implemented non-aggressive background polling of the Windows Certificate Store (`LocalMachine\MY` / `CurrentUser\MY`) with configurable polling interval (`--cert-poll-interval <sec>`, default: 30s).
  - Added automatic hot-swapping of SChannel security credentials (`CredHandle`) when an updated/renewed terminal certificate is detected (by SHA-1 thumbprint).
  - Added seamless reconnection triggers for outbound proxies: HTTP proxy credentials updated dynamically, MQTT proxy restarts listener to prompt immediate reconnect for local clients and `mosquitto bridge`.
  - Added dynamic reload of Reverse HTTPS listeners (`:443`) and Discovery services (`mDNS` / `LLMNR`) if the Device SN changes.
- **Configurable Expiration Handling (`--drop-on-expire`)**:
  - Default behavior: keeps running with expired certificate allowing remote processing centers / brokers to make verification decisions.
  - Optional strict standby mode: `--drop-on-expire` transitions proxy into standby state (`waiting_for_certificate`) if active certificate expires and no valid non-expired certificate is present in store.
- **Certificate Expiration Helper**: Added `cert_store_is_cert_expired()` to inspect validity windows against system time.

### Changed
- Improved `MqttProxyServer` worker lifecycle: workers now track `server->isRunning` and gracefully terminate within 50ms upon server stop/restart.
- Updated documentation (`README.md`, `USER_GUIDE.md`) with the new CLI options and runtime rotation behavior.

---

## [1.0.0] - 2026-08-24

### Initial Release
- Forward mTLS Proxies for MQTT (`:18883` -> `dev.leo4.ru:8883`) and HTTP (`:18443` -> `iot-processing.ru:443`).
- Reverse HTTPS Gateway (`0.0.0.0:443` -> `127.0.0.1:8000`).
- LAN Discovery via mDNS and LLMNR (`leo4-<sn>.local`).
- Windows Service integration with SCM and automatic recovery.
- Windows Defender Firewall automation and UAC self-elevation.
- System Tray notification icon for interactive console mode.
