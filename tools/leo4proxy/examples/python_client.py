# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "paho-mqtt>=2.0.0",
# ]
# ///

"""
Leo4Proxy Python Client Example
Connects to Leo4 IoT Platform via local SChannel TLS Proxy (127.0.0.1:18883 / 18443).
No OpenSSL certificate or private key files required on disk!
"""

import urllib.request
import json
import time
import paho.mqtt.client as mqtt

def main():
    print("==================================================================")
    print("  Leo4 IoT Python Client via Leo4Proxy (SChannel mTLS)")
    print("==================================================================")

    # 1. Obtain active Device SN and metadata from local proxy info endpoint
    info_url = "http://127.0.0.1:18443/_leo4/info"
    print(f"\n[1] Querying device info from local proxy: {info_url}...")
    
    try:
        with urllib.request.urlopen(info_url, timeout=3) as resp:
            info = json.loads(resp.read().decode("utf-8"))
            sn = info.get("sn")
            print(f"    Status:     {info.get('status')}")
            print(f"    Device SN:  {sn}")
            print(f"    Email:      {info.get('email')}")
            print(f"    Thumbprint: {info.get('thumbprint')}")
            print(f"    MQTT Local: {info.get('endpoints', {}).get('mqtt_local')}")
            print(f"    HTTP Local: {info.get('endpoints', {}).get('http_local')}")
    except Exception as e:
        print(f"[ERROR] Failed to query local proxy: {e}")
        print("Please ensure leo4proxy.exe is running!")
        return

    # 2. Example HTTPS backend API call via local HTTP proxy
    print(f"\n[2] Performing mTLS LicenseBilling check via local proxy (127.0.0.1:18443)...")
    billing_url = "http://127.0.0.1:18443/licensebilling/"
    req_body = "function=check&Signature=TUI9MSBCQkhFNTMyMUI0MDAxNDkwfENQVT0xIEJGRUJGQkZGMDAwODA2QzE=".encode("utf-8")
    req = urllib.request.Request(
        billing_url,
        data=req_body,
        headers={"Content-Type": "application/x-www-form-urlencoded"}
    )
    
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            xml_resp = resp.read().decode("utf-8")
            proxy_sn_header = resp.headers.get("X-Leo4-Proxy-Sn")
            print(f"    HTTP Status:       {resp.status}")
            print(f"    X-Leo4-Proxy-Sn:   {proxy_sn_header}")
            print(f"    Response XML Body: {xml_resp.strip()}")
    except Exception as e:
        print(f"    [WARN] Backend check: {e}")

    # 3. Connect to MQTT Broker via local proxy (plain TCP 18883)
    print(f"\n[3] Connecting to MQTT Broker via local SChannel Proxy (127.0.0.1:18883)...")

    def on_connect(client, userdata, flags, rc, properties=None):
        print(f"    [MQTT] Connected successfully! ReasonCode: {rc}")
        sub_topic = f"srv/{sn}/#"
        print(f"    [MQTT] Subscribing to: {sub_topic}")
        client.subscribe(sub_topic, qos=1)

    def on_message(client, userdata, msg):
        print(f"    [MQTT] Message arrived on {msg.topic}: {msg.payload.decode('utf-8', 'ignore')}")

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=sn,
        protocol=mqtt.MQTTv5
    )
    client.reconnect_delay_set(min_delay=1, max_delay=3)
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect("127.0.0.1", 18883, 60)
    client.loop_start()

    time.sleep(2)

    # Publish an event
    pub_topic = f"dev/{sn}/evt"
    event_payload = json.dumps({"101": 1047, "200": 44, "300": [{"310": "1.04.025", "311": 13}]})
    print(f"    [MQTT] Publishing event to: {pub_topic}")
    client.publish(pub_topic, event_payload, qos=1)

    time.sleep(2)
    client.loop_stop()
    client.disconnect()
    print("\n[SUCCESS] Completed Leo4Proxy demonstration.")

if __name__ == "__main__":
    main()
