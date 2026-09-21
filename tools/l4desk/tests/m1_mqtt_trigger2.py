import paho.mqtt.client as mqtt
import json
import time

SN = "a4b0000773c82116d210826"
TOPIC_CTL = f"srv/{SN}/ctl"
TOPIC_RESP = f"dev/{SN}/ctl"

received = []
connected = False
display_ids = []

def on_connect(client, userdata, flags, rc, properties=None):
    global connected
    connected = True
    client.subscribe(TOPIC_RESP, qos=1)

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        received.append({"topic": msg.topic, "payload": payload})
        d = json.loads(payload)
        if d.get("type") == "presence" and "inventory" in d:
            for disp in d["inventory"].get("displays", []):
                display_ids.append(disp)
                print(f"  Display: {disp['desktop_id']} name={disp['name']} primary={disp['primary']} {disp['width']}x{disp['height']} policy={disp.get('policy','?')}")
    except Exception as e:
        pass

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="m1_inv_check")
client.on_connect = on_connect
client.on_message = on_message
client.connect("127.0.0.1", 1883, 60)
client.loop_start()

for _ in range(20):
    if connected:
        break
    time.sleep(0.2)

time.sleep(3)

print(f"Found {len(display_ids)} displays")
if not display_ids:
    print("No displays found in presence inventory!")
else:
    # Use primary display
    primary = next((d for d in display_ids if d.get("primary")), display_ids[0])
    print(f"\nUsing primary display: {primary['desktop_id']}")

    now_ms = int(time.time() * 1000)
    cmd = {
        "v": 1,
        "type": "stream_start",
        "command_id": "m1_test_002",
        "lease_id": "m1-lease-002",
        "sn": SN,
        "mode": "desktop",
        "source_id": primary["desktop_id"],
        "profile": "low",
        "stream_instance_id": "m1-stream-002",
        "expires_at_ms": now_ms + 120000,
    }

    received.clear()
    print(f"-> stream_start with source_id={primary['desktop_id']}")
    client.publish(TOPIC_CTL, json.dumps(cmd), qos=1)

    print("Waiting 10s for responses...")
    time.sleep(10)

    print(f"\n{len(received)} messages:")
    for m in received:
        try:
            d = json.loads(m["payload"])
            t = d.get("type", "?")
            if t == "ack":
                print(f"  ACK: result={d.get('result')} state={d.get('state')} stream={d.get('stream_instance_id')}")
            elif t == "stream_event":
                print(f"  STREAM_EVENT: state={d.get('state')} reason={d.get('reason')}")
            else:
                print(f"  {t}: {json.dumps(d)[:200]}")
        except Exception:
            pass

client.loop_stop()
client.disconnect()
