import paho.mqtt.client as mqtt
import json
import time

SN = "a4b0000773c82116d210826"
TOPIC_CTL = f"srv/{SN}/ctl"
TOPIC_RESP = f"dev/{SN}/ctl"
TOPIC_OUT = f"dev/{SN}/out"
TOPIC_EVT = f"dev/{SN}/evt"

received = []
connected = False

def on_connect(client, userdata, flags, rc, properties=None):
    global connected
    connected = True
    print(f"Connected: rc={rc}")
    client.subscribe(TOPIC_RESP, qos=1)
    client.subscribe(TOPIC_OUT, qos=0)
    client.subscribe(TOPIC_EVT, qos=0)

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        received.append({"topic": msg.topic, "payload": payload})
        print(f"  <- {msg.topic}: {payload[:300]}")
    except Exception as e:
        print(f"  <- {msg.topic}: <binary, err={e}>")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="m1_test_pub")
client.on_connect = on_connect
client.on_message = on_message
client.connect("127.0.0.1", 1883, 60)
client.loop_start()

for _ in range(20):
    if connected:
        break
    time.sleep(0.2)

if not connected:
    print("FAILED: MQTT connect timeout")
    exit(1)

time.sleep(2)

now_ms = int(time.time() * 1000)
cmd = {
    "v": 1,
    "type": "stream_start",
    "command_id": "m1_test_001",
    "lease_id": "m1-lease-001",
    "sn": SN,
    "mode": "desktop",
    "source_id": "disp:0",
    "profile": "low",
    "stream_instance_id": "m1-stream-001",
    "expires_at_ms": now_ms + 120000,
}

print(f"-> {TOPIC_CTL}: {json.dumps(cmd)}")
client.publish(TOPIC_CTL, json.dumps(cmd), qos=1)

print("Waiting 8s for responses...")
time.sleep(8)

print(f"\nSummary: {len(received)} messages received")
for m in received:
    try:
        d = json.loads(m["payload"])
        print(f"  [{m['topic']}] type={d.get('type','?')} result={d.get('result','?')} state={d.get('state','?')} code={d.get('code','')}")
    except Exception:
        print(f"  [{m['topic']}] {m['payload'][:200]}")

client.loop_stop()
client.disconnect()
