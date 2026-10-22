"""Isolated Docker smoke on the builder; never touches release/prod containers.

Run from repository root: python3 l4media/ingress/tests/builder_smoke.py
Requires Docker permission, no application credentials. Runtime secrets are random.
The synthetic RTP below proves forwarding/counters only, never decoded video.
"""
import json
import os
import re
import secrets
import socket
import struct
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
JANUS = "dev-leo4-ru.cr.cloud.ru/l4media-janus@sha256:93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae"


def docker(*args, capture=True):
    return subprocess.check_output(["docker", *args], text=True).strip() if capture else subprocess.check_call(["docker", *args])


def http(base, path, payload=None, token=None, expected=200):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["X-Media-Service-Token"] = token
    req = urllib.request.Request(base + path, data=json.dumps(payload).encode() if payload is not None else None, headers=headers)
    try:
        response = urllib.request.urlopen(req, timeout=15)
    except urllib.error.HTTPError as error:
        response = error
    body = response.read()
    assert response.code == expected, (path, response.code, body[:200])
    return json.loads(body)


def port(name, inside):
    return int(docker("port", name, f"{inside}/tcp").rsplit(":", 1)[-1])


def wait_ready(base, path, token=None):
    for _ in range(60):
        try:
            return http(base, path, token=token)
        except (OSError, AssertionError):
            time.sleep(1)
    raise RuntimeError("Isolated service did not become ready")


def main():
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True, cwd=ROOT).strip()
    prefix = "hevc-smoke-" + uuid.uuid4().hex[:10]
    image = os.environ.get("L4MEDIA_TEST_IMAGE")
    checkout_revision = revision
    if image:
        if not re.fullmatch(r"[A-Za-z0-9./:_-]+@sha256:[0-9a-f]{64}", image):
            raise ValueError("Acceptance image must use an immutable registry digest")
        docker("pull", image, capture=False)
        revision = json.loads(docker("image", "inspect", image))[0]["Config"]["Labels"]["org.opencontainers.image.revision"]
    else:
        image = "l4media-ingress-candidate:" + revision[:12]
        docker("build", "--build-arg", f"SOURCE_REVISION={revision}", "-t", image, str(ROOT / "l4media/ingress"), capture=False)
    token, secret = secrets.token_hex(32), secrets.token_hex(32)
    names = []
    docker("network", "create", prefix)
    def run(suffix, *args):
        name = prefix + "-" + suffix
        names.append(name)
        docker("run", "-d", "--name", name, "--network", prefix, "--network-alias", suffix, *args)
        return name
    try:
        redis = run("redis", "redis:7.4-alpine")
        mounts = []
        for filename in ("janus.jcfg", "janus.transport.http.jcfg", "janus.plugin.streaming.jcfg"):
            mounts += ["-v", f"{ROOT / 'l4media/janus' / filename}:/usr/local/etc/janus/{filename}:ro"]
        janus = run("janus", "-p", "127.0.0.1::8088", "-p", "127.0.0.1::7088", "-e", f"JANUS_ADMIN_SECRET={secret}", *mounts, JANUS)
        ingress = run("ingress", "-p", "127.0.0.1::9100", "-p", "127.0.0.1::9000", "-e", "JANUS_HOST=janus", "-e", f"JANUS_ADMIN_SECRET={secret}", "-e", f"L4MEDIA_SERVICE_TOKEN={token}", "-e", "L4MEDIA_HEVC_ENABLED=1", "-e", f"L4MEDIA_SOURCE_REVISION={revision}", "-e", "REDIS_URL=redis://redis:6379/2", image)
        base = f"http://127.0.0.1:{port(ingress, 9100)}"
        wait_ready(base, "/health")
        for _ in range(20):
            caps = http(base, "/api/v2/media/capabilities", token=token)
            if caps["hevc_runtime_verified"]:
                break
            time.sleep(1)
        if not caps["hevc_runtime_verified"]:
            admin = f"http://127.0.0.1:{port(janus, 7088)}"
            def diagnostic(request):
                response = http(admin, "/admin", {"janus":"message_plugin", "transaction":"diagnostic", "admin_secret":secret, "plugin":"janus.plugin.streaming", "request":request})
                text = json.dumps(response).replace(secret, "REDACTED").replace(token, "REDACTED")
                print("JANUS diagnostic:", text)
            diagnostic({"request":"create", "type":"rtp", "id":1987654321, "audio":False, "video":True, "videoport":6300, "videortcpport":6301, "videopt":96, "videocodec":"h265", "videofmtp":"profile-space=0;profile-id=1;tier-flag=0;level-id=93;sprop-max-don-diff=0"})
            diagnostic({"request":"info", "id":1987654321})
        assert caps["hevc_runtime_verified"] and caps["dependencies_ready"], caps
        http(base, "/api/v2/media/capabilities", expected=401)
        http(base, "/api/v2/media/sessions/start", dict(unexpected=True), token, 400)
        spec = http(base, "/api/v2/openapi.json")
        assert spec["openapi"] == "3.1.0"
        request = json.loads((ROOT / "contracts/hevc-v2/fixtures/media_start.json").read_text())["payload"]
        request["lease_expires_at"] = int(time.time()) + 120
        prepared = http(base, "/api/v2/media/sessions/start", request, token, 201)
        assert prepared["descriptor"] == request["descriptor"]
        assert http(base, "/api/v2/media/sessions/start", request, token)["session_id"] == request["session_id"]
        identity = dict(contract_version=2, sn=request["publisher"]["sn"], lease_id=request["lease_id"], session_id=request["session_id"], stream_instance_id=request["publisher"]["stream_instance_id"], descriptor_revision=request["descriptor"]["descriptor_revision"])
        health_path = "/api/v2/media/sessions/" + identity["session_id"] + "?" + urllib.parse.urlencode({k: v for k, v in identity.items() if k not in ("contract_version", "session_id")})
        bad = json.loads(json.dumps(request)); bad["descriptor"]["tracks"][0]["payload_type"] = 97
        http(base, "/api/v2/media/sessions/start", bad, token, 409)
        # Crash/reload uses real Redis and restores exact descriptor, never implicit H264.
        docker("kill", "--signal", "KILL", ingress)
        docker("start", ingress)
        base = f"http://127.0.0.1:{port(ingress, 9100)}"
        wait_ready(base, "/health")
        restored = http(base, health_path, token=token)
        assert restored["descriptor"] == prepared["descriptor"]
        assert restored["route_generation"] == prepared["route_generation"]
        # Three watch handles including late join receive the same codec-specific offer.
        janus_base = f"http://127.0.0.1:{port(janus, 8088)}"
        for _ in range(3):
            session = http(janus_base, "/janus", {"janus": "create", "transaction": uuid.uuid4().hex})["data"]["id"]
            handle = http(janus_base, f"/janus/{session}", {"janus": "attach", "plugin": "janus.plugin.streaming", "transaction": uuid.uuid4().hex})["data"]["id"]
            http(janus_base, f"/janus/{session}/{handle}", {"janus": "message", "transaction": uuid.uuid4().hex, "body": {"request": "watch", "id": prepared["mountpoint_id"], "pin": prepared["pin"]}})
            event = http(janus_base, f"/janus/{session}?maxev=1")
            assert "H265/90000" in event.get("jsep", {}).get("sdp", ""), event
        sn = identity["sn"].encode()
        with socket.create_connection(("127.0.0.1", port(ingress, 9000)), timeout=5) as publisher:
            publisher.sendall(b"L4RT" + bytes([1, 0]) + struct.pack("!H", len(sn)) + sn)
            packet = bytes.fromhex("806000010000000100000001260180")
            publisher.sendall(bytes([1, 0]) + struct.pack("!H", len(packet)) + packet)
            time.sleep(0.2)
            health = http(base, health_path, token=token)
            assert health["transport"]["rtp_fresh"] and health["state"] == "active", health
        stale = dict(identity, stream_instance_id=str(uuid.uuid4()), reason="stale")
        http(base, "/api/v2/media/sessions/stop", stale, token, 409)
        docker("stop", janus)
        http(base, "/api/v2/media/sessions/start", request, token, 502)
        docker("start", janus)
        janus_base = f"http://127.0.0.1:{port(janus, 8088)}"
        wait_ready(janus_base, "/janus/info")
        http(base, "/api/v2/media/sessions/start", request, token)
        http(base, "/api/v2/media/sessions/stop", dict(identity, reason="smoke_complete"), token)
        assert http(base, health_path, token=token)["state"] == "stopped"
        docker("kill", "--signal", "KILL", ingress)
        docker("start", ingress)
        base = f"http://127.0.0.1:{port(ingress, 9100)}"
        wait_ready(base, "/health")
        assert http(base, health_path, token=token)["state"] == "stopped"
        assert http(base, "/api/v2/media/sessions/start", request, token)["state"] == "stopped"
        print(json.dumps({"source_revision": revision, "checkout_revision": checkout_revision, "tested_image": image, "image_id": docker("image", "inspect", image, "--format", "{{.Id}}"), "janus_image": JANUS, "checks": ["auth", "strict_request", "openapi", "runtime_probe", "prepare_replay", "descriptor_conflict", "redis_crash_reload", "three_H265_SDP_offers", "L4RTP_forward_health", "stale_stop", "janus_failure_and_restart", "stopped_crash_reload_no_resurrection"], "decoded_video": False}))
    finally:
        for name in reversed(names):
            subprocess.run(["docker", "rm", "-f", name], check=False, stdout=subprocess.DEVNULL)
        subprocess.run(["docker", "network", "rm", prefix], check=False, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
