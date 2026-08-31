---
name: test-terminal-requests
description: Executes test requests to etranprocessing terminal-api (https://iot-processing.ru:443) with mTLS authentication as test terminal 773 via local leo4proxy (http://localhost:18443 / https://localhost:18443) and handles MQTT messaging via local Mosquitto bridge (127.0.0.1:1883) with client_id=main_app. Use when testing terminal endpoints (/api/ListMenuFile, /api/health, /api/payment, /api/techgate, /api/licensebilling), verifying mTLS terminal identity forwarding, or testing MQTT presence and telemetry on behalf of terminal 773.
---

# Test Terminal Requests & MQTT Bridge Skill

Guide and operational workflow for sending authenticated test requests to the `etranprocessing` cloud platform (`https://iot-processing.ru:443`) and exchanging MQTT messages on behalf of **test terminal `773`** using the local `leo4proxy` mTLS tunnel and local Mosquitto bridge.

---

## 1. Architecture & Testing Concept

In production, payment terminals authenticate to `https://iot-processing.ru:443` using hardware/client certificates stored in the Windows Certificate Store (mTLS). To run local integration tests without managing raw TLS client certificates in test code:

```text
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           Local Development Machine                             │
│                                                                                 │
│   ┌───────────────────────────────────┐ ┌───────────────────────────────────┐   │
│   │    HTTP Test Client / Scripts     │ │    MQTT Test Client / Scripts     │   │
│   │    (curl, requests, pytest)       │ │    (paho-mqtt, client_id=main_app)│   │
│   └─────────────────┬─────────────────┘ └─────────────────┬─────────────────┘   │
│                     │ Plain HTTP (No-Cert)                │ Plain MQTT (1883)   │
│                     │ http://localhost:18443              │ No-SSL              │
│                     ▼                                     ▼                     │
│   ┌───────────────────────────────────┐ ┌───────────────────────────────────┐   │
│   │             Leo4Proxy             │ │             Mosquitto             │   │
│   │ - Listens on :18443 (HTTP/HTTPS)  │ │ - Local broker on 127.0.0.1:1883  │   │
│   │ - Holds real cert for Terminal 773│ │ - Bridges to cloud via Leo4Proxy  │   │
│   │ - Attaches mTLS & client cert     │ │   mTLS bridge on :18883           │   │
│   │ - Exposes metadata /_leo4/*       │ │                                   │   │
│   └─────────────────┬─────────────────┘ └─────────────────┬─────────────────┘   │
│                     │                                     │                     │
└─────────────────────┼─────────────────────────────────────┼─────────────────────┘
                      │ Outgoing mTLS (HTTPS :443)          │ Outgoing mTLS (MQTTS :8883)
                      ▼                                     ▼
        ┌───────────────────────────────────────────────────────────────────┐
        │                 Cloud Backend (iot-processing.ru)                 │
        │ - Nginx terminates mTLS and extracts X-Client-Cert-* headers      │
        │ - ProcessingBackend identifies Terminal 773 & executes requests   │
        │ - Leo4 MQTT Broker routes terminal topics (dev/773/*, srv/773/*)  │
        └───────────────────────────────────────────────────────────────────┘
```

### Key Principles:
1. **Plain HTTP to Proxy**: Local tests send standard HTTP requests to `http://127.0.0.1:18443/<endpoint>`. `leo4proxy` terminates local HTTP, attaches the real certificate for **terminal 773**, and forwards the request over mTLS to `https://iot-processing.ru:443`.
2. **Local MQTT Bridge**: Local MQTT clients connect to `127.0.0.1:1883` (No-SSL). The local Mosquitto bridge forwards messages upstream to `dev.leo4.ru:8883` using the certificate of terminal 773.
3. **Mandatory Client Role**: All tests interacting with the MQTT bridge must connect with `client_id=main_app` (or `main_app_{SN}`) and implement the `main_app` presence lifecycle (`dev/{SN}/app` -> `app_online` / `app_offline`).

---

## 2. Endpoints & Connection Reference

### Local Endpoints (`leo4proxy` & `mosquitto`):
| Service | Protocol | Host / URL | Role |
|---|---|---|---|
| **Terminal API Proxy** | HTTP (Plain) | `http://127.0.0.1:18443` | Proxies all requests to `https://iot-processing.ru:443` with Terminal 773 cert |
| **Terminal API Proxy (TLS)** | HTTPS | `https://127.0.0.1:18443` | Same as above over local TLS (use `-k` / insecure flag) |
| **Proxy Info & Metadata** | HTTP | `http://127.0.0.1:18443/_leo4/info` | Returns JSON status, serial number (`SN`), and certificate details |
| **Quick SN Endpoint** | HTTP | `http://127.0.0.1:18443/_leo4/sn` | Returns raw terminal `SN` string |
| **Proxy Health** | HTTP | `http://127.0.0.1:18443/_leo4/health` | Returns HTTP 200 `{"status": "ok"}` |
| **Local MQTT Broker** | MQTT TCP | `127.0.0.1:1883` | Local broker bridged to cloud Leo4 broker |

### Authenticated Terminal Context:
- **Terminal ID**: `773`
- **Device SN**: Obtained dynamically from `http://127.0.0.1:18443/_leo4/sn` (e.g. `a4b0000773...`)
- **Remote Target**: `https://iot-processing.ru:443` (ProcessingBackend)
- **MQTT Role**: `main_app`

---

## 3. Pre-Flight Verification

Before running tests, verify that `leo4proxy` and `mosquitto` are running locally and have loaded the certificate for terminal 773:

```bash
# 1. Check leo4proxy status and obtain terminal SN:
curl.exe -s http://127.0.0.1:18443/_leo4/info

# 2. Check quick SN:
curl.exe -s http://127.0.0.1:18443/_leo4/sn

# Expected response:
# {"status":"ready","certificate_found":true,"sn":"a4b0000773...","client_id":"a4b0000773..."}
```

If `status` is `"waiting_for_certificate"`, ensure the terminal certificate is installed in the Windows Certificate Store or start `leo4proxy`.

---

## 4. HTTP / REST API Testing Workflows (via `leo4proxy`)

### 4.1. Health Check & Header Verification
Verify that `leo4proxy` successfully injects mTLS certificate headers on the cloud backend:

```bash
# Health endpoint:
curl.exe -s http://127.0.0.1:18443/api/health

# Debug headers endpoint (shows forwarded cert identity):
curl.exe -s http://127.0.0.1:18443/api/debug/headers
```

### 4.2. Terminal Menu Retrieval (`GET /api/ListMenuFile`)
Fetch the configured terminal menu (XML or JSON) for Terminal 773:

```bash
# Fetch menu in JSON format:
curl.exe -s "http://127.0.0.1:18443/api/ListMenuFile?format=json"

# Fetch menu in XML format:
curl.exe -s "http://127.0.0.1:18443/api/ListMenuFile"

# Fetch menu for specific variant:
curl.exe -s "http://127.0.0.1:18443/api/ListMenuFile?variant_id=1&format=json"
```

### 4.3. License Billing Verification (`/api/licensebilling`)
Check active license status for Terminal 773:

```bash
# GET license check:
curl.exe -s "http://127.0.0.1:18443/api/licensebilling/check"

# POST license check:
curl.exe -s -X POST "http://127.0.0.1:18443/api/licensebilling/check" \
  -H "Content-Type: application/xml" \
  -d "<request><terminal_id>773</terminal_id></request>"
```

### 4.4. Tech Gate Operations (`/api/techgate`)
Test terminal diagnostic and operational endpoints:

```bash
# 1. Send device status:
curl.exe -s -X POST "http://127.0.0.1:18443/api/techgate/devicestatus" \
  -H "Content-Type: application/xml" \
  -d "<request><status>OK</status><bill_acceptor>OK</bill_acceptor></request>"

# 2. Get shift report:
curl.exe -s "http://127.0.0.1:18443/api/techgate/getshiftreport"

# 3. Get inkass report:
curl.exe -s "http://127.0.0.1:18443/api/techgate/getinkassreport"

# 4. Request TSP list:
curl.exe -s "http://127.0.0.1:18443/api/techgate/tsplist"
```

### 4.5. Payment Gate Operations (`/api/payment`)
Send a payment transaction request:

```bash
curl.exe -s -X POST "http://127.0.0.1:18443/api/payment" \
  -H "Content-Type: application/xml" \
  -d "<?xml version=\"1.0\" encoding=\"utf-8\"?><payment-request><terminal>773</terminal><account>79991234567</account><amount>100.00</amount><service_id>1</service_id></payment-request>"
```

---

## 5. MQTT Testing Workflows (via Local Bridge)

### 5.1. Protocol & Client Requirements for `main_app`
All test scripts connecting to the local MQTT bridge on `127.0.0.1:1883` must adhere to the **`main_app`** lifecycle specification:

1. **Client ID**: `main_app` or `main_app_{SN}`
2. **Before CONNECT**:
   - `will_topic   = dev/{SN}/app`
   - `will_payload = app_offline`
   - `will_qos     = 1`
   - `will_retain  = true`
3. **After CONNACK**:
   - `PUBLISH dev/{SN}/app = app_online` (`qos = 1`, `retain = true`)
   - Subscribe to server command topics: `srv/{SN}/#` (or `srv/{SN}/tsk`, `srv/{SN}/rsp`)
4. **Outgoing Messages**:
   - Telemetry/Events: `PUBLISH dev/{SN}/out` or `dev/{SN}/evt`
   - RPC Responses: `PUBLISH dev/{SN}/res`
5. **Clean Shutdown**:
   - `PUBLISH dev/{SN}/app = app_offline` (`qos = 1`, `retain = true`)
   - `DISCONNECT`

### 5.2. CLI Testing with `mosquitto_sub` & `mosquitto_pub`

```bash
# Step 1: In Terminal 1, subscribe to all server commands for terminal:
# (Replace {SN} with actual serial number from http://127.0.0.1:18443/_leo4/sn)
mosquitto_sub.exe -h 127.0.0.1 -p 1883 -t "srv/+/tsk" -v

# Step 2: In Terminal 2, publish presence online (main_app role):
mosquitto_pub.exe -h 127.0.0.1 -p 1883 -i "main_app" \
  -t "dev/a4b0000773c82116d210826/app" -m "app_online" -q 1 -r

# Step 3: Publish test telemetry event:
mosquitto_pub.exe -h 127.0.0.1 -p 1883 -i "main_app" \
  -t "dev/a4b0000773c82116d210826/evt" \
  -m "{\"event\":\"ping\",\"terminal_id\":773,\"timestamp\":1725148800}" -q 1

# Step 4: Graceful shutdown (offline presence):
mosquitto_pub.exe -h 127.0.0.1 -p 1883 -i "main_app" \
  -t "dev/a4b0000773c82116d210826/app" -m "app_offline" -q 1 -r
```

---

## 6. Automated Python Integration Test Template

The following script provides a ready-to-use end-to-end test verifying both HTTP REST endpoints via `leo4proxy` and MQTT messaging via Mosquitto bridge:

```python
"""Automated integration test for Terminal 773 via leo4proxy and local MQTT bridge."""

import json
import time
import paho.mqtt.client as mqtt
import requests

PROXY_BASE_URL = "http://127.0.0.1:18443"
MQTT_HOST = "127.0.0.1"
MQTT_PORT = 1883


def get_terminal_sn() -> str:
    """Discover terminal serial number from leo4proxy."""
    resp = requests.get(f"{PROXY_BASE_URL}/_leo4/info", timeout=5)
    resp.raise_for_status()
    data = resp.json()
    assert data.get("status") == "ready", f"Proxy not ready: {data}"
    sn = data.get("sn")
    assert sn, "Serial number (SN) is empty in proxy metadata"
    print(f"[OK] Terminal SN resolved: {sn}")
    return sn


def test_http_endpoints():
    """Verify HTTP API endpoints forwarded through leo4proxy with mTLS."""
    print("\n--- Testing HTTP Endpoints via leo4proxy ---")

    # 1. Health check
    r = requests.get(f"{PROXY_BASE_URL}/api/health", timeout=5)
    print(f"GET /api/health -> HTTP {r.status_code}")
    assert r.status_code == 200

    # 2. ListMenuFile (Menu check)
    r = requests.get(f"{PROXY_BASE_URL}/api/ListMenuFile?format=json", timeout=10)
    print(f"GET /api/ListMenuFile -> HTTP {r.status_code}")
    assert r.status_code in (200, 404)  # 200 if menu exists, 404 if no menu configured

    # 3. License check
    r = requests.get(f"{PROXY_BASE_URL}/api/licensebilling/check", timeout=5)
    print(f"GET /api/licensebilling/check -> HTTP {r.status_code}")
    assert r.status_code == 200


def test_mqtt_presence_and_messaging(sn: str):
    """Verify MQTT connect, presence lifecycle, and message exchange."""
    print("\n--- Testing MQTT via Mosquitto Bridge (main_app) ---")
    client_id = f"main_app_{sn}"
    received_messages = []

    client = mqtt.Client(
        client_id=client_id, protocol=mqtt.MQTTv311, clean_session=True
    )

    # 1. Set Last Will and Testament (LWT)
    client.will_set(
        topic=f"dev/{sn}/app", payload="app_offline", qos=1, retain=True
    )

    def on_connect(cli, userdata, flags, rc):
        print(f"[MQTT] Connected to {MQTT_HOST}:{MQTT_PORT} (rc={rc})")
        # 2. Publish online status
        cli.publish(f"dev/{sn}/app", payload="app_online", qos=1, retain=True)
        # 3. Subscribe to incoming tasks
        cli.subscribe(f"srv/{sn}/#", qos=1)

    def on_message(cli, userdata, msg):
        payload_str = msg.payload.decode("utf-8", errors="replace")
        print(f"[MQTT] Received {msg.topic}: {payload_str}")
        received_messages.append((msg.topic, payload_str))

    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    client.loop_start()

    try:
        time.sleep(1.0)
        # 4. Publish test event
        test_payload = json.dumps(
            {"event": "test_ping", "sn": sn, "terminal_id": 773}
        )
        client.publish(f"dev/{sn}/out", payload=test_payload, qos=1)
        print(f"[MQTT] Published test event to dev/{sn}/out")
        time.sleep(1.0)
    finally:
        # 5. Graceful shutdown
        print("[MQTT] Shutting down: publishing app_offline...")
        client.publish(f"dev/{sn}/app", payload="app_offline", qos=1, retain=True)
        client.disconnect()
        client.loop_stop()
        print("[OK] MQTT test completed successfully.")


if __name__ == "__main__":
    sn = get_terminal_sn()
    test_http_endpoints()
    test_mqtt_presence_and_messaging(sn)
```

---

## 7. Troubleshooting & Common Issues

| Issue / Symptom | Root Cause | Solution |
|---|---|---|
| `Connection refused` on `18443` | `leo4proxy` service is not running | Start `leo4proxy` binary (`C:\l4tools\leo4proxy\leo4proxy.exe`) or verify Windows service status via `sc query leo4proxy`. |
| `status: "waiting_for_certificate"` in `/_leo4/info` | Certificate for Terminal 773 is missing or PIN not entered | Install companion PFX certificate into Windows Certificate Store or enter PIN via `l4pin`. |
| HTTP `401 Unauthorized` / `403 Forbidden` from cloud | Terminal 773 is deactivated in DB or certificate is expired | Check terminal state in MenuBuilder admin (`is_active=True`) and verify license expiration (`license.expires_at`). |
| `Connection refused` on `1883` | Local `mosquitto` broker is stopped | Start Mosquitto service (`sc start mosquitto`) or run `mosquitto.exe -c mosquitto.conf`. |
| MQTT messages not received by cloud | mTLS bridge between Mosquitto and Leo4 is disconnected | Verify `leo4proxy` bridge port `18883` and inspect Mosquitto log (`mosquitto.log`). |
| MQTT presence status remains `app_offline` | Client did not publish `app_online` after `CONNACK` | Ensure `PUBLISH dev/{SN}/app = app_online` is sent with `retain=True` immediately in `on_connect`. |
