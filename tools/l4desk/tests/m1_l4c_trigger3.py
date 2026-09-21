import paho.mqtt.client as mqtt
import json
import time

SN = "a4b0000773c82116d210826"
TOPIC_CTL = f"srv/{SN}/ctl"
TOPIC_RESP = f"dev/{SN}/ctl"

received = []
connected = False

def on_connect(client, userdata, flags, rc, properties=None):
    global connected
    connected = True
    client.subscribe(TOPIC_RESP, qos=1)

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        received.append({"topic": msg.topic, "payload": payload, "time": time.time()})
        d = json.loads(payload)
        t = d.get("type", "?")
        if t == "ack":
            print(f"  ACK: result={d.get('result')} state={d.get('state')} code={d.get('code','')}")
        elif t == "stream_event":
            print(f"  EVENT: state={d.get('state')} reason={d.get('reason','')}")
    except Exception:
        pass

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="m1_l4c_test2")
client.on_connect = on_connect
client.on_message = on_message
client.connect("127.0.0.1", 1883, 60)
client.loop_start()

for _ in range(20):
    if connected:
        break
    time.sleep(0.2)
time.sleep(2)

now_ms = int(time.time() * 1000)
cmd = {
    "v": 1,
    "type": "stream_start",
    "command_id": f"m1_l4c_{int(time.time())}",
    "lease_id": "m1-l4c-lease-002",
    "sn": SN,
    "mode": "desktop",
    "source_id": "disp:3580dc5e",
    "profile": "low",
    "stream_instance_id": f"m1-l4c-{int(time.time())}",
    "expires_at_ms": now_ms + 120000,
}

print(f"-> stream_start (cmd={cmd['command_id']})")
client.publish(TOPIC_CTL, json.dumps(cmd), qos=1)
print("Waiting 15s...")
time.sleep(15)

print(f"\n--- Summary ({len(received)} msgs) ---")
for m in received:
    try:
        d = json.loads(m["payload"])
        t = d.get("type", "?")
        if t in ("ack", "stream_event"):
            print(f"  {t}: {json.dumps(d)[:200]}")
    except Exception:
        pass

import subprocess
r = subprocess.run(["tasklist"], capture_output=True, text=True)
for name in ["l4capture", "ffmpeg"]:
    if name in r.stdout.lower():
        for line in r.stdout.split("\n"):
            if name in line.lower():
                print(f"  PROCESS: {line.strip()}")

client.loop_stop()
client.disconnect()
