#!/usr/bin/env python3
"""Local production-process check of reverse RTCP event dispatch (no real Janus)."""
import os
import socket
import struct
import subprocess
import time
import urllib.request
from pathlib import Path


def request(path, method="GET"):
    with urllib.request.urlopen(urllib.request.Request("http://127.0.0.1:9100" + path, method=method), timeout=2) as response:
        return response.read()


def exact(sock, length):
    data = bytearray()
    while len(data) < length:
        part = sock.recv(length - len(data))
        assert part, "feedback transport closed unexpectedly"
        data.extend(part)
    return bytes(data)


def run():
    root = Path(__file__).resolve().parents[1]
    env = dict(os.environ, JANUS_HOST="127.0.0.1", JANUS_ADMIN_SECRET="local-test-placeholder", L4MEDIA_SERVICE_TOKEN="local-test-placeholder", REDIS_URL="")
    clients = []
    udp = []
    with open(os.devnull, "w") as log:
        process = subprocess.Popen([str(root / "l4media-ingress"), "/dev/null"], env=env, stdout=log, stderr=log)
        try:
            for _ in range(50):
                assert process.poll() is None, "local ingress exited (check ports 9000/9100 are free)"
                try:
                    request("/health")
                    break
                except OSError:
                    time.sleep(0.1)
            sr = struct.pack("!BBH6I", 0x80, 200, 6, 1, 0, 0, 0, 0, 0)
            endpoints = []
            for sn in ("feedback_local_A", "feedback_local_B"):
                janus = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                janus.bind(("127.0.0.1", 0))
                janus.settimeout(2)
                udp.append(janus)
                port = janus.getsockname()[1]
                request(f"/routes/{sn}?rtp={port}&rtcp={port}", "PUT")
                client = socket.create_connection(("127.0.0.1", 9000), timeout=2)
                clients.append(client)
                preamble = b"L4RT" + struct.pack("!BBH", 1, 0, len(sn)) + sn.encode()
                frame = struct.pack("!BBH", 2, 0, len(sr)) + sr
                client.sendall(preamble[:5])
                client.sendall(preamble[5:] + frame[:3])
                client.sendall(frame[3:])
                received, endpoint = janus.recvfrom(65535)
                assert received == sr
                endpoints.append(endpoint)
            assert endpoints[0] != endpoints[1]
            for i in range(2):
                pli = struct.pack("!BBHII", 0x81, 206, 2, 9, i + 1)
                udp[i].sendto(pli, endpoints[i])
                header = exact(clients[i], 4)
                assert header == struct.pack("!BBH", 2, 0, len(pli))
                assert exact(clients[i], len(pli)) == pli
            request("/routes/feedback_local_A", "DELETE")
            udp[0].sendto(pli, endpoints[0])
            clients[0].settimeout(0.2)
            try:
                unexpected = clients[0].recv(4)
            except socket.timeout:
                pass
            else:
                raise AssertionError(f"feedback after route deletion or transport closed: {unexpected!r}")
            print("Live reverse RTCP event loop passed: two streams, SR endpoints, fragmented input, route deletion.")
        finally:
            for sock in clients + udp:
                sock.close()
            process.terminate()
            process.wait(timeout=10)


if __name__ == "__main__":
    run()
