# Changelog — Leo4Proxy

All notable changes to the `leo4proxy` component will be documented in this file.

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
