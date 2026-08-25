# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "paho-mqtt>=2.0.0",
#     "pytest>=7.0.0",
#     "tzdata; sys_platform == 'win32'",
# ]
# ///

"""
Automated Presence & LWT Test Suite for all Leo4 MQTT Client Examples.
Uses a specialized test client (WITHOUT LWT) subscribed to dev/+/app and dev/+/svc on localhost:1883.
"""

from __future__ import annotations

import os
import queue
import subprocess
import sys
import threading
import time
import traceback
import uuid
import paho.mqtt.client as mqtt

# --- Force UTF-8 for console I/O on Windows ---
if sys.platform == "win32":
    os.system("chcp 65001 >nul 2>&1")
    for _stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(_stream, "reconfigure"):
            _stream.reconfigure(encoding="utf-8", errors="replace")
# -----------------------------------------------

BROKER_HOST = "127.0.0.1"
BROKER_PORT = 1883


class PresenceWatcher:
    """Specialized test client (WITHOUT LWT) subscribed to presence topics."""

    def __init__(self, host: str = BROKER_HOST, port: int = BROKER_PORT):
        self.host = host
        self.port = port
        self.client_id = f"test_suite_watcher_{uuid.uuid4().hex[:8]}"
        self.events: queue.Queue[tuple[str, str, bool, int]] = queue.Queue()
        self.client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            protocol=mqtt.MQTTv5,
            client_id=self.client_id,
        )
        # Note: No will_set called - test client does NOT have LWT!
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.connected_event = threading.Event()

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if reason_code == 0:
            client.subscribe("dev/+/app", qos=1)
            client.subscribe("dev/+/svc", qos=1)
            self.connected_event.set()

    def _on_message(self, client, userdata, msg):
        payload_str = msg.payload.decode("utf-8", errors="replace")
        self.events.put((msg.topic, payload_str, bool(msg.retain), int(msg.qos)))

    def start(self):
        self.client.connect(host=self.host, port=self.port, keepalive=60)
        self.client.loop_start()
        if not self.connected_event.wait(timeout=5):
            raise RuntimeError("Watcher client could not connect to broker within 5 seconds")
        # Drain any initial retained messages
        time.sleep(0.5)
        while not self.events.empty():
            self.events.get_nowait()

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()

    def clear(self):
        while not self.events.empty():
            self.events.get_nowait()

    def wait_for_presence(self, expected_suffix: str, expected_payload: str, timeout: float = 10.0) -> tuple[str, str, bool, int]:
        """
        Waits for a presence event whose topic ends with expected_suffix (e.g. '/svc' or '/app')
        and whose payload matches expected_payload (e.g. 'svc_online', 'app_offline').
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            remaining = max(0.1, deadline - time.time())
            try:
                topic, payload, retain, qos = self.events.get(timeout=remaining)
                if topic.endswith(expected_suffix) and payload == expected_payload:
                    return topic, payload, retain, qos
            except queue.Empty:
                break
        raise AssertionError(f"Did not receive presence event ({expected_suffix} -> '{expected_payload}') within {timeout}s")


def test_python_client_extra_service(watcher: PresenceWatcher):
    print("\n--- Testing python_client.py (role=extra_service) ---")
    watcher.clear()
    cmd = ["uv", "run", "tools/leo4proxy/examples/python_client.py", "--once", "--role", "extra_service"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")

    topic_on, payload_on, retain_on, qos_on = watcher.wait_for_presence("/svc", "svc_online", timeout=8)
    print(f"  [PASSED] Online Presence: {topic_on} = {payload_on} (qos={qos_on})")
    assert payload_on == "svc_online"

    stdout, stderr = proc.communicate(timeout=15)
    if proc.returncode != 0:
        print("STDOUT:\n", stdout)
        print("STDERR:\n", stderr)
    assert proc.returncode == 0

    topic_off, payload_off, retain_off, qos_off = watcher.wait_for_presence("/svc", "svc_offline", timeout=8)
    print(f"  [PASSED] Offline Presence: {topic_off} = {payload_off} (qos={qos_off})")
    assert payload_off == "svc_offline"


def test_python_client_main_app(watcher: PresenceWatcher):
    print("\n--- Testing python_client.py (role=main_app) ---")
    watcher.clear()
    cmd = ["uv", "run", "tools/leo4proxy/examples/python_client.py", "--once", "--role", "main_app"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")

    topic_on, payload_on, retain_on, qos_on = watcher.wait_for_presence("/app", "app_online", timeout=8)
    print(f"  [PASSED] Online Presence: {topic_on} = {payload_on} (qos={qos_on})")
    assert payload_on == "app_online"

    stdout, stderr = proc.communicate(timeout=15)
    if proc.returncode != 0:
        print("STDOUT:\n", stdout)
        print("STDERR:\n", stderr)
    assert proc.returncode == 0

    topic_off, payload_off, retain_off, qos_off = watcher.wait_for_presence("/app", "app_offline", timeout=8)
    print(f"  [PASSED] Offline Presence: {topic_off} = {payload_off} (qos={qos_off})")
    assert payload_off == "app_offline"


def test_mqtt_test_client_main_app(watcher: PresenceWatcher):
    print("\n--- Testing mqtt_test_client.py (role=main_app) ---")
    watcher.clear()
    cmd = ["uv", "run", "tools/leo4proxy/examples/mqtt_test_client.py", "--max-iterations", "1", "--role", "main_app"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")

    topic_on, payload_on, retain_on, qos_on = watcher.wait_for_presence("/app", "app_online", timeout=8)
    print(f"  [PASSED] Online Presence: {topic_on} = {payload_on} (qos={qos_on})")
    assert payload_on == "app_online"

    stdout, stderr = proc.communicate(timeout=15)
    if proc.returncode != 0:
        print("STDOUT:\n", stdout)
        print("STDERR:\n", stderr)
    assert proc.returncode == 0

    topic_off, payload_off, retain_off, qos_off = watcher.wait_for_presence("/app", "app_offline", timeout=8)
    print(f"  [PASSED] Offline Presence: {topic_off} = {payload_off} (qos={qos_off})")
    assert payload_off == "app_offline"


def test_csharp_client_main_app(watcher: PresenceWatcher):
    print("\n--- Testing csharp_example.csproj (role=main_app) ---")
    watcher.clear()
    env = dict(os.environ)
    env["MQTT_MAX_ITERATIONS"] = "1"
    env["MQTT_ROLE"] = "main_app"

    cmd = ["dotnet", "run", "--project", "tools/leo4proxy/examples/csharp_example.csproj", "--no-build"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace", env=env)

    topic_on, payload_on, retain_on, qos_on = watcher.wait_for_presence("/app", "app_online", timeout=12)
    print(f"  [PASSED] Online Presence: {topic_on} = {payload_on} (qos={qos_on})")
    assert payload_on == "app_online"

    stdout, stderr = proc.communicate(timeout=20)
    if proc.returncode != 0:
        print("STDOUT:\n", stdout)
        print("STDERR:\n", stderr)
    assert proc.returncode == 0

    topic_off, payload_off, retain_off, qos_off = watcher.wait_for_presence("/app", "app_offline", timeout=8)
    print(f"  [PASSED] Offline Presence: {topic_off} = {payload_off} (qos={qos_off})")
    assert payload_off == "app_offline"


def test_powershell_example_extra_service(watcher: PresenceWatcher):
    print("\n--- Testing powershell_example.ps1 (role=extra_service) ---")
    watcher.clear()
    cmd = ["powershell", "-ExecutionPolicy", "Bypass", "-File", "tools/leo4proxy/examples/powershell_example.ps1", "-Once"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")

    topic_on, payload_on, retain_on, qos_on = watcher.wait_for_presence("/svc", "svc_online", timeout=8)
    print(f"  [PASSED] Online Presence: {topic_on} = {payload_on} (qos={qos_on})")
    assert payload_on == "svc_online"

    stdout, stderr = proc.communicate(timeout=15)
    if proc.returncode != 0:
        print("STDOUT:\n", stdout)
        print("STDERR:\n", stderr)
    assert proc.returncode == 0

    topic_off, payload_off, retain_off, qos_off = watcher.wait_for_presence("/svc", "svc_offline", timeout=8)
    print(f"  [PASSED] Offline Presence: {topic_off} = {payload_off} (qos={qos_off})")
    assert payload_off == "svc_offline"


def test_curl_mosquitto_pub_extra_service(watcher: PresenceWatcher):
    print("\n--- Testing curl_mosquitto_pub.cmd (role=extra_service) ---")
    watcher.clear()
    cmd = ["cmd", "/c", "tools\\leo4proxy\\examples\\curl_mosquitto_pub.cmd", "--once"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")

    topic_on, payload_on, retain_on, qos_on = watcher.wait_for_presence("/svc", "svc_online", timeout=8)
    print(f"  [PASSED] Online Presence: {topic_on} = {payload_on} (qos={qos_on})")
    assert payload_on == "svc_online"

    stdout, stderr = proc.communicate(timeout=15)
    if proc.returncode != 0:
        print("STDOUT:\n", stdout)
        print("STDERR:\n", stderr)
    assert proc.returncode == 0

    topic_off, payload_off, retain_off, qos_off = watcher.wait_for_presence("/svc", "svc_offline", timeout=8)
    print(f"  [PASSED] Offline Presence: {topic_off} = {payload_off} (qos={qos_off})")
    assert payload_off == "svc_offline"


def test_lwt_abnormal_kill(watcher: PresenceWatcher):
    print("\n--- Testing LWT trigger on abnormal process termination ---")
    watcher.clear()
    cmd = ["uv", "run", "tools/leo4proxy/examples/python_client.py", "--interval", "100", "--role", "extra_service"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")

    topic_on, payload_on, retain_on, qos_on = watcher.wait_for_presence("/svc", "svc_online", timeout=8)
    print(f"  [PASSED] Connected & published: {topic_on} = {payload_on}")
    assert payload_on == "svc_online"

    # Simulate sudden crash/kill (ungraceful termination)
    print("  [KILL] Abruptly terminating process to trigger Mosquitto broker LWT...")
    proc.kill()
    proc.wait()

    # Mosquitto broker detects broken TCP connection and publishes LWT
    topic_off, payload_off, retain_off, qos_off = watcher.wait_for_presence("/svc", "svc_offline", timeout=8)
    print(f"  [PASSED] LWT Triggered by Broker: {topic_off} = {payload_off}")
    assert payload_off == "svc_offline"


def main():
    print("================================================================")
    print("  Leo4 MQTT Client Examples Presence & LWT Test Suite")
    print("  Watcher Client: NO LWT, Subscribed to dev/+/app & dev/+/svc")
    print("  Broker: 127.0.0.1:1883 (Mosquitto Bridge)")
    print("================================================================\n")

    watcher = PresenceWatcher()
    watcher.start()
    print("[TEST-WATCHER] Active and monitoring presence topics on localhost:1883.\n")

    tests = [
        ("Python Extra Service", test_python_client_extra_service),
        ("Python Main App", test_python_client_main_app),
        ("Python Reference Client (mqtt_test_client)", test_mqtt_test_client_main_app),
        ("C# Main App (csharp_example)", test_csharp_client_main_app),
        ("PowerShell Extra Service (powershell_example)", test_powershell_example_extra_service),
        ("cURL + Mosquitto_pub Extra Service", test_curl_mosquitto_pub_extra_service),
        ("Abrupt Kill LWT Broker Trigger", test_lwt_abnormal_kill),
    ]

    passed = 0
    failed = 0

    for name, test_fn in tests:
        try:
            test_fn(watcher)
            passed += 1
        except Exception as e:
            print(f"  [FAILED] {name}: {e}")
            traceback.print_exc()
            failed += 1

    watcher.stop()

    print("\n================================================================")
    print(f"  TEST SUMMARY: {passed} PASSED, {failed} FAILED")
    print("================================================================")

    if failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
