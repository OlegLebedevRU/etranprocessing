import paho.mqtt.client as mqtt
import json
import time

SN = "a4b0000773c82116d210826"
TOPIC_CTL = f"srv/{SN}/ctl"
TOPIC_RESP = f"dev/{SN}/ctl"

received = []
connected = False
l4capture_running = False

def on_connect(client, userdata, flags, rc, properties=None):
    global connected
    connected = True
    client.subscribe(TOPIC_RESP, qos=1)

def on_message(client, userdata, msg):
    global l4capture_running
    try:
        payload = msg.payload.decode("utf-8")
        received.append({"topic": msg.topic, "payload": payload, "time": time.time()})
        d = json.loads(payload)
        t = d.get("type", "?")
        if t == "ack":
            print(f"  ACK: result={d.get('result')} state={d.get('state')}")
        elif t == "stream_event":
            print(f"  EVENT: state={d.get('state')} reason={d.get('reason','')}")
        elif t == "presence":
            # Check for l4capture-related info
            pass
    except Exception as e:
        print(f"  <- {msg.topic}: <err={e}>")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="m1_l4c_test")
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
    "command_id": "m1_l4c_001",
    "lease_id": "m1-l4c-lease-001",
    "sn": SN,
    "mode": "desktop",
    "source_id": "disp:3580dc5e",
    "profile": "low",
    "stream_instance_id": "m1-l4c-stream-001",
    "expires_at_ms": now_ms + 120000,
}

print(f"-> stream_start (l4capture backend, profile=low, disp:3580dc5e)")
client.publish(TOPIC_CTL, json.dumps(cmd), qos=1)

print("Waiting 12s...")
time.sleep(12)

print(f"\n--- Summary ({len(received)} messages) ---")
for m in received:
    try:
        d = json.loads(m["payload"])
        t = d.get("type", "?")
        if t == "ack":
            r = d.get("result", "?")
            s = d.get("state", "?")
            c = d.get("code", "")
            print(f"  ACK: result={r} state={s} code={c}")
        elif t == "stream_event":
            print(f"  STREAM_EVENT: state={d.get('state')} reason={d.get('reason','')}")
    except Exception:
        pass

# Check if l4capture is running
import subprocess
result = subprocess.run(["tasklist", "/FI", "IMAGENAME eq l4capture.exe"], capture_output=True, text=True)
if "l4capture" in result.stdout:
    print("\n*** l4capture.exe IS RUNNING ***")
    for line in result.stdout.strip().split("\n"):
        if "l4capture" in line.lower():
            print(f"  {line.strip()}")
else:
    print("\n*** l4capture.exe NOT RUNNING (ffmpeg fallback or failure) ***")
    result2 = subprocess.run(["tasklist", "/FI", "IMAGENAME eq ffmpeg.exe"], capture_output=True, text=True)
    if "ffmpeg" in result2.stdout:
        print("  ffmpeg.exe is running instead")

client.loop_stop()
client.disconnect()
