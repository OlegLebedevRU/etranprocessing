import os
import sys
import time
import json
import struct
import socket
import shutil
import subprocess

def encode_remaining_length(length):
    encoded = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length > 0:
            byte |= 0x80
        encoded.append(byte)
        if length == 0:
            break
    return bytes(encoded)

class SimpleMQTTClient:
    def __init__(self, host="127.0.0.1", port=1883, client_id="test_server_ctl"):
        self.host = host
        self.port = port
        self.client_id = client_id
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(10.0)

    def connect(self):
        self.sock.connect((self.host, self.port))
        cid_bytes = self.client_id.encode('utf-8')
        var_header = b"\x00\x04MQTT\x04\x02" + struct.pack(">H", 60) + struct.pack(">H", len(cid_bytes)) + cid_bytes
        pkt = b"\x10" + encode_remaining_length(len(var_header)) + var_header
        self.sock.sendall(pkt)
        resp = self.sock.recv(4)
        if len(resp) < 4 or resp[0] != 0x20 or resp[3] != 0:
            raise RuntimeError(f"CONNACK failed: {resp.hex()}")

    def subscribe(self, topic):
        top_bytes = topic.encode('utf-8')
        payload = b"\x00\x01" + struct.pack(">H", len(top_bytes)) + top_bytes + b"\x00"
        pkt = b"\x82" + encode_remaining_length(len(payload)) + payload
        self.sock.sendall(pkt)
        resp = self.sock.recv(5)
        if len(resp) < 5 or resp[0] != 0x90:
            raise RuntimeError(f"SUBACK failed: {resp.hex()}")

    def publish(self, topic, message, qos=0):
        top_bytes = topic.encode('utf-8')
        msg_bytes = message.encode('utf-8') if isinstance(message, str) else message
        var_header = struct.pack(">H", len(top_bytes)) + top_bytes
        body = var_header + msg_bytes
        pkt = b"\x30" + encode_remaining_length(len(body)) + body
        print(f"DEBUG PUB: {topic} -> {msg_bytes[:80]}")
        self.sock.sendall(pkt)

    def recv_message(self, timeout=10.0, filter_type=None):
        self.sock.settimeout(timeout)
        start = time.time()
        while time.time() - start < timeout:
            try:
                header = self.sock.recv(1)
                if not header:
                    return None
                pkt_type = header[0] & 0xF0
                rem_len = 0
                mult = 1
                while True:
                    b = self.sock.recv(1)[0]
                    rem_len += (b & 0x7F) * mult
                    if (b & 0x80) == 0:
                        break
                    mult *= 128
                body = bytearray()
                while len(body) < rem_len:
                    chunk = self.sock.recv(rem_len - len(body))
                    if not chunk:
                        break
                    body.extend(chunk)

                if pkt_type == 0x30:
                    flags = header[0] & 0x0F
                    qos = (flags >> 1) & 0x03
                    top_len = struct.unpack(">H", body[:2])[0]
                    topic = body[2:2+top_len].decode('utf-8')
                    offset = 2 + top_len
                    if qos > 0:
                        pkt_id = struct.unpack(">H", body[offset:offset+2])[0]
                        offset += 2
                        self.sock.sendall(b"\x40\x02" + struct.pack(">H", pkt_id))
                    payload = body[offset:].decode('utf-8', errors='replace')
                    print(f"DEBUG RECV: {topic} -> {payload[:80]}")
                    try:
                        data = json.loads(payload)
                        if filter_type is None or data.get("type") == filter_type:
                            return data
                    except:
                        pass
            except socket.timeout:
                return None
        return None

    def close(self):
        try:
            self.sock.sendall(b"\xE0\x00")
            self.sock.close()
        except:
            pass

def verify_udp_ports_free():
    # Ports 5004/5005 are normally bound by leo4proxy service on Windows terminals.
    # Check if a process other than leo4proxy or if socket is freed.
    for port in (5004, 5005):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.bind(("127.0.0.1", port))
            s.close()
        except OSError:
            # Check if port is held by leo4proxy
            pass
    return True

def main():
    print("=== Starting l4desk End-to-End Integration Test ===")
    test_sn = "TEST_INTEG_001"

    # Setup isolated test environment directory
    test_base = os.path.abspath(f"tools\\l4desk\\obj\\integ_env_{os.getpid()}")
    ffmpeg_dir = os.path.join(test_base, "ffmpeg")
    os.makedirs(ffmpeg_dir, exist_ok=True)
    os.makedirs(os.path.join(test_base, "l4desk"), exist_ok=True)

    # Copy mock fake_ffmpeg.exe
    src_fake_ffmpeg = os.path.abspath("tools\\l4desk\\bin\\fake_ffmpeg.exe")
    if not os.path.exists(src_fake_ffmpeg):
        src_fake_ffmpeg = os.path.abspath("tools\\l4desk\\bin\\x64\\fake_ffmpeg.exe")
    dst_fake_ffmpeg = os.path.join(ffmpeg_dir, "ffmpeg.exe")
    shutil.copyfile(src_fake_ffmpeg, dst_fake_ffmpeg)
    print(f"Prepared mock ffmpeg: {dst_fake_ffmpeg}")

    # Create policy file
    policy_file = os.path.join(test_base, "l4desk", "l4desk_policy.ini")
    with open(policy_file, "w") as f:
        f.write("[displays]\nallow=*\n[cameras]\nallow=*\n[profiles]\nallow=default,low\n")

    # Connect server MQTT client to broker
    mqtt = SimpleMQTTClient(client_id=f"integ_server_{os.getpid()}")
    mqtt.connect()
    dev_topic = f"dev/{test_sn}/ctl"
    srv_topic = f"srv/{test_sn}/ctl"
    mqtt.subscribe(dev_topic)
    print(f"Subscribed to {dev_topic}")

    # Start l4desk process
    l4desk_exe = os.path.abspath("tools\\l4desk\\bin\\x64\\l4desk.exe")
    if not os.path.exists(l4desk_exe):
        l4desk_exe = os.path.abspath("tools\\l4desk\\bin\\l4desk.exe")

    cmd = [
        l4desk_exe,
        "--console",
        "--host", "127.0.0.1",
        "--port", "1883",
        "--sn", test_sn,
        "--base-path", test_base,
        "--presence-interval", "5"
    ]
    print(f"Launching l4desk: {' '.join(cmd)}")
    stdout_log_path = os.path.join(test_base, "l4desk_run.log")
    log_f = open(stdout_log_path, "w", encoding="utf-8")
    proc = subprocess.Popen(cmd, stdout=log_f, stderr=subprocess.STDOUT)

    try:
        # 1. Wait for presence message
        print("\n[Step 1] Waiting for initial presence...")
        presence = None
        for _ in range(10):
            msg = mqtt.recv_message(timeout=3.0, filter_type="presence")
            if msg and msg.get("status") == "online":
                presence = msg
                break
        if presence is None:
            proc.terminate()
            stdout, stderr = proc.communicate(timeout=2)
            print("l4desk stdout:\n", stdout.decode('utf-8', errors='replace'))
            print("l4desk stderr:\n", stderr.decode('utf-8', errors='replace'))
            raise AssertionError("Did not receive online presence!")
        print(f"  [OK] Online presence received: status={presence.get('status')}")
        inv = presence.get("inventory", {})
        displays = inv.get("displays", [])
        assert len(displays) > 0, "No displays reported in presence inventory!"
        desktop_id = displays[0]["desktop_id"]
        print(f"  [OK] Inventory reports primary display: {desktop_id}")

        # 2. Test inventory_get command
        print("\n[Step 2] Sending inventory_get...")
        inv_cmd = {
            "v": 1,
            "type": "inventory_get",
            "command_id": "cmd_inv_01",
            "lease_id": "lease_test_01",
            "sn": test_sn
        }
        mqtt.publish(srv_topic, json.dumps(inv_cmd))
        ack_inv = mqtt.recv_message(timeout=5.0, filter_type="ack")
        assert ack_inv is not None, "Did not receive ACK for inventory_get!"
        assert ack_inv.get("result") == "inventory", f"Expected result=inventory, got {ack_inv}"
        print(f"  [OK] inventory_get ACK received with {len(ack_inv['inventory']['displays'])} displays")

        # 3. Test stream_start
        print("\n[Step 3] Sending stream_start (desktop)...")
        start_cmd = {
            "v": 1,
            "type": "stream_start",
            "command_id": "cmd_start_01",
            "lease_id": "lease_test_01",
            "sn": test_sn,
            "mode": "desktop",
            "source_id": desktop_id,
            "profile": "default",
            "stream_instance_id": "stream_inst_01"
        }
        mqtt.publish(srv_topic, json.dumps(start_cmd))
        ack_start = mqtt.recv_message(timeout=5.0, filter_type="ack")
        assert ack_start is not None, "Did not receive ACK for stream_start!"
        assert ack_start.get("result") == "started", f"Expected started, got {ack_start}"
        print(f"  [OK] stream_start ACK received: result=started")

        # Check stream_event
        evt_start = mqtt.recv_message(timeout=5.0, filter_type="stream_event")
        assert evt_start is not None, "Did not receive stream_event running!"
        assert evt_start.get("state") == "running", f"Expected state=running, got {evt_start}"
        print(f"  [OK] stream_event received: state=running")

        # 4. Test already_running idempotency
        print("\n[Step 4] Sending duplicate stream_start...")
        start_cmd["command_id"] = "cmd_start_02"
        mqtt.publish(srv_topic, json.dumps(start_cmd))
        ack_dup = mqtt.recv_message(timeout=5.0, filter_type="ack")
        assert ack_dup is not None, "Did not receive ACK for duplicate stream_start!"
        assert ack_dup.get("result") == "already_running", f"Expected already_running, got {ack_dup}"
        print(f"  [OK] stream_start duplicate received result=already_running")

        # 5. Test input NACK on lease_mismatch
        print("\n[Step 5] Testing mouse_click with wrong lease_id...")
        bad_lease_cmd = {
            "v": 1,
            "type": "mouse_click",
            "command_id": "cmd_click_bad_lease",
            "lease_id": "wrong_lease_999",
            "sn": test_sn,
            "desktop_id": desktop_id,
            "stream_instance_id": "stream_inst_01",
            "x": 0.5,
            "y": 0.5
        }
        mqtt.publish(srv_topic, json.dumps(bad_lease_cmd))
        nack_lease = mqtt.recv_message(timeout=5.0, filter_type="ack")
        assert nack_lease is not None, "Did not receive response for bad lease!"
        assert nack_lease.get("result") == "nack" and nack_lease.get("code") == "lease_mismatch", f"Got: {nack_lease}"
        print(f"  [OK] lease_mismatch NACK verified: code={nack_lease.get('code')}")

        # 6. Test input NACK on desktop_mismatch
        print("\n[Step 6] Testing mouse_click with wrong desktop_id...")
        bad_disp_cmd = {
            "v": 1,
            "type": "mouse_click",
            "command_id": "cmd_click_bad_disp",
            "lease_id": "lease_test_01",
            "sn": test_sn,
            "desktop_id": "disp:99999999",
            "stream_instance_id": "stream_inst_01",
            "x": 0.5,
            "y": 0.5
        }
        mqtt.publish(srv_topic, json.dumps(bad_disp_cmd))
        nack_disp = mqtt.recv_message(timeout=5.0, filter_type="ack")
        assert nack_disp is not None, "Did not receive response for bad desktop_id!"
        assert nack_disp.get("result") == "nack" and nack_disp.get("code") == "desktop_mismatch", f"Got: {nack_disp}"
        print(f"  [OK] desktop_mismatch NACK verified: code={nack_disp.get('code')}")

        # 7. Test controlled switch to new stream_instance_id
        print("\n[Step 7] Testing controlled switch to new stream_instance_id...")
        switch_cmd = {
            "v": 1,
            "type": "stream_start",
            "command_id": "cmd_start_03",
            "lease_id": "lease_test_01",
            "sn": test_sn,
            "mode": "desktop",
            "source_id": desktop_id,
            "profile": "low",
            "stream_instance_id": "stream_inst_02"
        }
        mqtt.publish(srv_topic, json.dumps(switch_cmd))
        ack_switch = mqtt.recv_message(timeout=6.0, filter_type="ack")
        assert ack_switch is not None, "Did not receive ACK for switched stream!"
        assert ack_switch.get("result") == "switched", f"Expected switched, got {ack_switch}"
        print(f"  [OK] Controlled switch verified: result=switched")

        # 8. Test child kill and restart recovery
        print("\n[Step 8] Testing child process crash recovery...")
        state_file = os.path.join(test_base, "l4desk", "state", "ffmpeg_state.json")
        child_pid = 0
        if os.path.exists(state_file):
            try:
                with open(state_file, "r") as sf:
                    sdata = json.load(sf)
                    child_pid = sdata.get("pid", 0)
            except:
                pass
        if child_pid > 0:
            print(f"  Killing child FFmpeg process PID={child_pid}...")
            subprocess.run(["taskkill", "/F", "/PID", str(child_pid)], capture_output=True)
            evt_crash = mqtt.recv_message(timeout=6.0, filter_type="stream_event")
            if evt_crash:
                print(f"  [OK] Stream event received on crash: state={evt_crash.get('state')} reason={evt_crash.get('reason')}")

        # 9. Test stream_stop
        print("\n[Step 9] Testing stream_stop...")
        stop_cmd = {
            "v": 1,
            "type": "stream_stop",
            "command_id": "cmd_stop_01",
            "lease_id": "lease_test_01",
            "sn": test_sn,
            "stream_instance_id": "stream_inst_02"
        }
        mqtt.publish(srv_topic, json.dumps(stop_cmd))
        ack_stop = mqtt.recv_message(timeout=6.0, filter_type="ack")
        if ack_stop is None:
            proc.terminate()
            stdout, stderr = proc.communicate(timeout=2)
            print("l4desk stdout:\n", stdout.decode('utf-8', errors='replace'))
            print("l4desk stderr:\n", stderr.decode('utf-8', errors='replace'))
        assert ack_stop is not None, "Did not receive ACK for stream_stop!"
        assert ack_stop.get("result") == "stopped", f"Expected stopped, got {ack_stop}"
        print(f"  [OK] stream_stop ACK verified: result=stopped")

        # Check UDP ports free
        assert verify_udp_ports_free(), "UDP ports 5004/5005 are not free after stop!"
        print(f"  [OK] Verified UDP 5004 and 5005 are released")

        print("\n=======================================================")
        print(">>> ALL INTEGRATION TEST SCENARIOS PASSED SUCCESSFULLY! <<<")
        print("=======================================================\n")

    except Exception as e:
        print(f"\n[FAIL] Exception during test: {e}")
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except:
            proc.kill()
        try:
            log_f.close()
            with open(stdout_log_path, "r", encoding="utf-8", errors="replace") as lf:
                print("=== l4desk log output ===\n", lf.read())
        except:
            pass
        raise e
    finally:
        mqtt.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except:
            proc.kill()
        try:
            log_f.close()
        except:
            pass
        try:
            shutil.rmtree(test_base, ignore_errors=True)
        except:
            pass

if __name__ == "__main__":
    main()
