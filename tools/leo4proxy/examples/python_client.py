# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "paho-mqtt>=2.0.0",
#     "tzdata; sys_platform == 'win32'",
# ]
# ///

"""
Leo4 IoT Python Event Publisher (extra_service role)
Connects to local Mosquitto Bridge (127.0.0.1:1883) via plain TCP (No-SSL).
Publishes telemetry events in a 10-minute cycle to topic 'dev/<SN>/evt' (QoS 1, Retain 0)
with MQTT 5.0 User Properties and hardware event payload.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.request
import uuid
from datetime import datetime
import zoneinfo
import paho.mqtt.client as mqtt

# --- Force UTF-8 for console I/O on Windows ---
if sys.platform == "win32":
    os.system("chcp 65001 >nul 2>&1")
    for _stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(_stream, "reconfigure"):
            _stream.reconfigure(encoding="utf-8", errors="replace")
# -----------------------------------------------

PROXY_HTTP_URL = os.getenv("LEO4_PROXY_HTTP", "http://127.0.0.1:18443")
MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "extra_service")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")
DEFAULT_SN = os.getenv("DEVICE_SN", "a3b1234567c10221d290825")
EVENT_INTERVAL_SEC = int(os.getenv("EVENT_INTERVAL_SEC", "600"))  # 10 minutes default


def get_current_iso_time_with_offset() -> str:
    """Returns ISO-8601 formatted string with timezone offset (e.g. 2026-08-03T12:41:33+03:00)."""
    try:
        msk_tz = zoneinfo.ZoneInfo("Europe/Moscow")
        now = datetime.now(msk_tz)
    except Exception:
        from datetime import timezone, timedelta
        msk_tz = timezone(timedelta(hours=3))
        now = datetime.now(msk_tz)
    basic = now.strftime("%Y-%m-%dT%H:%M:%S%z")
    if len(basic) >= 5 and (basic[-5] in ("+", "-")):
        return basic[:-2] + ":" + basic[-2:]
    return basic


def resolve_device_sn() -> str:
    """Resolves device serial number from local proxy REST endpoint with fallback."""
    if PROXY_HTTP_URL:
        try:
            req = urllib.request.Request(f"{PROXY_HTTP_URL}/_leo4/sn", headers={"User-Agent": "Leo4ExtraService/1.0"})
            with urllib.request.urlopen(req, timeout=2) as resp:
                sn = resp.read().decode("utf-8").strip()
                if sn:
                    print(f"[INFO] Discovered active Device SN from local proxy: {sn}")
                    return sn
        except Exception:
            pass
    print(f"[INFO] Using configured Device SN: {DEFAULT_SN}")
    return DEFAULT_SN


def create_event_data(dev_event_id: int) -> tuple[dict, dict[str, str]]:
    """
    Creates payload and user properties matching the specification:
    - Topic: dev/<SN>/evt
    - QoS: 1, Retain: 0
    - Payload: { "101": 36823, "102": "...", "200": 888, "300": [ { "301": "044AFE42C76781", "302": 6, "303": 0 } ] }
    - User Properties: event_type_code: 888, dev_event_id: 36823, dev_timestamp, correlation_id
    """
    now_ts = int(time.time())
    iso_time = get_current_iso_time_with_offset()
    corr_id = str(uuid.uuid4())

    payload = {
        "101": dev_event_id,
        "102": iso_time,
        "200": 888,
        "300": [
            {
                "301": "044AFE42C76781",
                "302": 6,
                "303": 0
            }
        ]
    }

    user_properties = {
        "event_type_code": "888",
        "dev_event_id": str(dev_event_id),
        "dev_timestamp": str(now_ts),
        "correlation_id": corr_id,
    }

    return payload, user_properties


def main():
    parser = argparse.ArgumentParser(description="Leo4 IoT Extra Service Event Sender (10-minute loop)")
    parser.add_argument("--interval", type=int, default=EVENT_INTERVAL_SEC, help="Interval in seconds between events (default: 600 = 10 min)")
    parser.add_argument("--once", action="store_true", help="Send a single event and exit immediately")
    parser.add_argument("--count", type=int, default=0, help="Maximum number of events to send (0 = infinite)")
    parser.add_argument("--port", type=int, default=MQTT_PORT, help="MQTT Broker port (default: 1883)")
    parser.add_argument("--host", type=str, default=MQTT_HOST, help="MQTT Broker host (default: 127.0.0.1)")
    args = parser.parse_args()

    print("================================================================")
    print("  Leo4 IoT Python Event Publisher (extra_service role)")
    print(f"  Target: Mosquitto Bridge ({args.host}:{args.port}, No-SSL Plain TCP)")
    print(f"  Interval: {args.interval}s (10 min), Topic: dev/<SN>/evt (QoS 1, Retain 0)")
    print("================================================================\n")

    sn = resolve_device_sn()
    topic = f"dev/{sn}/evt"

    client_id = f"{sn}_extra_py"
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=client_id,
        protocol=mqtt.MQTTv5,
    )

    if MQTT_USERNAME:
        client.username_pw_set(username=MQTT_USERNAME, password=MQTT_PASSWORD or None)

    is_connected = False

    def on_connect(c, userdata, flags, reason_code, properties=None):
        nonlocal is_connected
        if reason_code == 0:
            is_connected = True
            print(f"[MQTT] Successfully connected to {args.host}:{args.port} as '{MQTT_USERNAME}'")
        else:
            print(f"[MQTT] Connection failed with reason code: {reason_code}")

    def on_disconnect(c, userdata, disconnect_flags, reason_code, properties=None):
        nonlocal is_connected
        is_connected = False
        print(f"[MQTT] Disconnected from broker (rc={reason_code})")

    def on_publish(c, userdata, mid, reason_code=None, properties=None):
        print(f"    [ACK] Event delivered successfully! (mid={mid}, rc={reason_code})")

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_publish = on_publish
    client.reconnect_delay_set(min_delay=1, max_delay=3)

    print(f"[INFO] Connecting to broker at {args.host}:{args.port}...")
    try:
        client.connect(host=args.host, port=args.port, keepalive=60)
        client.loop_start()
    except Exception as e:
        print(f"[WARN] Connection error: {e}. Will retry in loop...")

    iteration = 0
    base_event_id = 36823

    try:
        while True:
            # Wait for active connection before publishing
            wait_attempts = 0
            while not is_connected and wait_attempts < 10:
                time.sleep(0.5)
                wait_attempts += 1

            iteration += 1
            dev_event_id = base_event_id + (iteration - 1)
            payload_obj, user_props_dict = create_event_data(dev_event_id)
            payload_json = json.dumps(payload_obj, ensure_ascii=False)

            # MQTT 5.0 properties
            props = mqtt.Properties(mqtt.PacketTypes.PUBLISH)
            props.UserProperty = [(k, v) for k, v in user_props_dict.items()]

            print(f"\n[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Publishing Event #{iteration}:")
            print(f"  Topic:           {topic}")
            print(f"  QoS:             1 (Retain: 0)")
            print(f"  Payload:         {payload_json}")
            print(f"  User Properties: {user_props_dict}")

            msg_info = client.publish(
                topic=topic,
                payload=payload_json.encode("utf-8"),
                qos=1,
                retain=False,
                properties=props,
            )
            msg_info.wait_for_publish(timeout=5)

            if args.once:
                print("\n[INFO] Single event sent (--once). Exiting.")
                break

            if args.count > 0 and iteration >= args.count:
                print(f"\n[INFO] Reached requested event count ({args.count}). Exiting.")
                break

            print(f"\n[SLEEP] Waiting {args.interval} seconds (10 minutes) until next event publication...")
            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n[SHUTDOWN] Interrupted by user. Stopping...")
    finally:
        client.loop_stop()
        client.disconnect()
        print("[SUCCESS] Python Extra Service terminated.")


if __name__ == "__main__":
    main()
