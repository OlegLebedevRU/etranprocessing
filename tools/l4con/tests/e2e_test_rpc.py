import json
import time
import uuid
import sys
import paho.mqtt.client as mqtt

SN = "a3b1234567c10221d290825"
MQTT_HOST = "127.0.0.1"
MQTT_PORT = 1883

received_chunks = []
test_results = {}

def on_connect(client, userdata, flags, rc, properties=None):
    print(f"[TEST] Connected to MQTT broker (rc={rc})")
    client.subscribe(f"dev/{SN}/out", qos=1)
    client.subscribe(f"dev/{SN}/res", qos=1)
    client.subscribe(f"dev/{SN}/svc", qos=1)

def on_message(client, userdata, msg):
    payload_str = msg.payload.decode("utf-8", errors="replace")
    # print(f"[TEST RECV] {msg.topic}: {payload_str}")
    try:
        data = json.loads(payload_str)
        if msg.topic.endswith("/out"):
            received_chunks.append(data)
        elif msg.topic.endswith("/res"):
            print(f"[TEST RES] Task result: {data}")
    except Exception as e:
        print(f"[TEST ERR] {e}")

def run_command_test(client, cmd_name, cmd_line, shell="cmd", is_blacklisted=False, ttl_sec=15):
    global received_chunks
    received_chunks = []
    session_id = str(uuid.uuid4())
    task_id = str(uuid.uuid4())

    payload = {
        "id": task_id,
        "method_code": 7001,
        "session_id": session_id,
        "command_line": cmd_line,
        "shell": shell,
        "ttl_sec": ttl_sec,
        "topic": f"dev/{SN}/out"
    }

    print("\n" + "="*70)
    print(f"TEST: {cmd_name}")
    print(f"Command: '{cmd_line}' (Shell: {shell})")
    print("="*70)

    # Publish to srv/<SN>/rsp (standard RPC task payload)
    client.publish(f"srv/{SN}/rsp", json.dumps(payload), qos=1)

    # Wait for completion (eof=True)
    start_time = time.time()
    completed = False
    full_output = ""
    exit_code = None

    while time.time() - start_time < (ttl_sec + 5):
        for chunk in received_chunks:
            if chunk.get("session_id") == session_id:
                data = chunk.get("data", "")
                if data and data not in full_output:
                    full_output += data
                if chunk.get("eof"):
                    completed = True
                    exit_code = chunk.get("exit_code")
                    break
        if completed:
            break
        time.sleep(0.05)

    print(f"Execution Output:\n{full_output.strip()}")
    print(f"Status: {'COMPLETED' if completed else 'TIMED OUT'} | Exit Code: {exit_code}")
    print("="*70)

    test_results[cmd_name] = {
        "command": cmd_line,
        "shell": shell,
        "completed": completed,
        "exit_code": exit_code,
        "output": full_output.strip()
    }

def main():
    client = mqtt.Client(client_id="e2e_tester_agent", protocol=mqtt.MQTTv311)
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"Connecting to MQTT broker at {MQTT_HOST}:{MQTT_PORT}...")
    client.connect(MQTT_HOST, MQTT_PORT, 60)
    client.loop_start()
    time.sleep(1)

    try:
        # Test 1: Simple echo
        run_command_test(client, "1. Echo Test", "echo Leo4 Remote Diagnostics E2E Verified", shell="cmd")

        # Test 2: Windows System Info
        run_command_test(client, "2. Windows Host & Version", "hostname & ver", shell="cmd")

        # Test 3: Windows IP Config
        run_command_test(client, "3. Windows Network Info (ipconfig)", "ipconfig", shell="cmd")

        # Test 4: PowerShell Environment & Date
        run_command_test(client, "4. PowerShell Execution", "Get-Date -Format 'yyyy-MM-dd HH:mm:ss'; [System.Environment]::OSVersion.VersionString", shell="powershell")

        # Test 5: Services Check
        run_command_test(client, "5. Services Check", "sc query type= service state= all | findstr /i \"SERVICE_NAME:.*Leo4\"", shell="cmd")

    finally:
        client.loop_stop()
        client.disconnect()

    print("\n\n" + "#"*70)
    print("                      E2E TEST SUMMARY REPORT")
    print("#"*70)
    all_passed = True
    for name, res in test_results.items():
        status_str = "PASSED" if res["completed"] else "FAILED"
        if not res["completed"]:
            all_passed = False
        print(f"[{status_str}] {name} (Exit Code: {res['exit_code']})")
    print("#"*70)
    print(f"OVERALL RESULT: {'ALL TESTS PASSED' if all_passed else 'SOME TESTS FAILED'}")

if __name__ == "__main__":
    main()
