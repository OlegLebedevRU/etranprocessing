"""Local socket executor. Owns only the inet mb_mqtt table on this host."""

import hashlib
import http.server
import ipaddress
import json
import math
import os
import re
import socketserver
import sqlite3
import subprocess
import time
from pathlib import Path

TABLE = "mb_mqtt"
SOCKET = Path("/run/mb-mqtt-edge/control.sock")
OBS = Path("/run/mb-mqtt-observer/observations.json")
STATE = Path("/var/lib/mb-mqtt-edge/rules.sqlite")
DENY = [ipaddress.ip_network(s) for s in os.environ.get("MQTT_PROTECTED_NETWORKS", "").split()]


def nft(*args, batch=None):
    return subprocess.run(
        ["nft", *args], input=batch, text=True, capture_output=True, check=True, timeout=4
    ).stdout


def initialize(interface):
    if (
        not re.fullmatch(r"[a-zA-Z0-9_.-]{1,15}", interface)
        or not Path("/sys/class/net", interface).exists()
    ):
        raise ValueError("Invalid external interface")
    exists = any(
        t.get("table", {}).get("name") == TABLE and t["table"].get("family") == "inet"
        for t in json.loads(nft("-j", "list", "tables"))["nftables"]
    )
    commands = [f"delete table inet {TABLE}"] if exists else []
    commands.extend(
        [
            f"add table inet {TABLE}",
            f"add chain inet {TABLE} ingress {{ type filter hook prerouting priority -110; policy accept; }}",
        ]
    )
    for family, addr in ((4, "ip"), (6, "ip6")):
        commands += [
            f"add set inet {TABLE} deny{family} {{ type ipv{family}_addr; flags timeout; }}",
            f'add rule inet {TABLE} ingress iifname "{interface}" fib daddr type local tcp dport 8883 ct state new tcp flags & (syn | ack) == syn {addr} saddr @deny{family} counter drop',
        ]
    nft("-f", "-", batch="\n".join(commands) + "\n")


def members():
    data = json.loads(nft("-j", "list", "table", "inet", TABLE))
    result = set()
    counters = {}
    for obj in data["nftables"]:
        entry = obj.get("set", {})
        for elem in entry.get("elem", []):
            value = elem if isinstance(elem, str) else elem.get("elem", {}).get("val")
            if isinstance(value, str):
                result.add(value)
        if "rule" in obj:
            rule = obj["rule"]
            for e in rule["expr"]:
                if "counter" in e:
                    counters[str(rule["handle"])] = e["counter"]["packets"]
    return result, counters


def preview(ip, obs, now):
    current = obs["active"].get(ip, [])
    recent = sorted({u for addr, u, _ in obs["success"] if addr == ip})
    logins = sorted({r["login"] for r in obs["rows"] if r["ip"] == ip and r["login"]})
    seen = any(r["ip"] == ip and r.get("reason") != "reconnect" and r["last"] >= now - 3600 for r in obs["rows"])
    fresh = obs["live"] and now - obs["at"] <= 20 and now - obs["sample_at"] <= 100
    partial = obs["incomplete"] or now - obs["history_since"] < 86400
    facts = [ip, current, recent, logins, bool(fresh), bool(partial), bool(seen)]
    last_success = max((stamp for addr, _, stamp in obs["success"] if addr == ip), default=0)
    return dict(
        sample_at=obs["sample_at"],
        last_success=last_success,
        active=current,
        recent=recent,
        logins=logins,
        fresh=fresh,
        partial=partial,
        seen=seen,
        token=hashlib.sha256(json.dumps(facts).encode()).hexdigest(),
    )


def apply(ip, expiry):
    family = ipaddress.ip_address(ip).version
    for attempt in range(2):
        present, _ = members()
        commands = []
        if ip in present:
            commands.append(f"delete element inet {TABLE} deny{family} {{ {ip} }}")
        ttl = math.ceil(expiry - time.time())
        if ttl > 0:
            commands.append(f"add element inet {TABLE} deny{family} {{ {ip} timeout {ttl}s }}")
        try:
            if commands:
                nft("-f", "-", batch="\n".join(commands) + "\n")
            if (ip in members()[0]) != (expiry > time.time()):
                raise RuntimeError("Verification failed")
            return
        except subprocess.CalledProcessError:
            if attempt:
                raise


class Conflict(Exception):
    pass


class Executor:
    def __init__(self, db):
        self.db = db
        db.execute(
            "create table if not exists rules(ip text primary key, rev integer not null, expiry integer not null)"
        )
        for ip, expiry in db.execute(
            "select ip,expiry from rules where expiry>?", (int(time.time()),)
        ):
            apply(ip, expiry)

    def command(self, body):
        addr = ipaddress.ip_address(body["ip"])
        ip = str(addr)
        if body["ip"] != ip or "%" in ip:
            raise ValueError("Invalid IP")
        rev = body["revision"]
        expiry = body["expiry"]
        if (
            type(rev) is not int
            or rev < 1
            or type(expiry) is not int
            or not 0 <= expiry <= time.time() + 21605
        ):
            raise ValueError("Invalid revision or deadline")
        old = self.db.execute("select rev,expiry from rules where ip=?", (ip,)).fetchone()
        if old and old[0] >= rev:
            if old != (rev, expiry):
                raise Conflict("superseded")
        elif expiry > time.time():
            if not addr.is_global or addr.is_multicast or any(addr in n for n in DENY):
                raise Conflict("protected_address")
            info = preview(ip, json.loads(OBS.read_text()), int(time.time()))
            if not info["fresh"] or not info["seen"] or info["token"] != body.get("token"):
                raise Conflict("refresh_preview")
            if (info["recent"] or info["active"] or info["partial"]) and body.get(
                "ack"
            ) is not True:
                raise Conflict("confirm_impact")
        with self.db:
            self.db.execute("insert or replace into rules values (?,?,?)", (ip, rev, expiry))
        apply(ip, expiry)
        return {"revision": rev, "applied": True}

    def read(self):
        ips, counters = members()
        return {"active_rules": sorted(ips), "dropped_packets": sum(counters.values())}


class Handler(http.server.BaseHTTPRequestHandler):
    timeout = 5

    def log_message(self, *args):
        pass

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length", 0))
            if not 0 < n <= 2048:
                raise ValueError("Invalid length")
            body = json.loads(self.rfile.read(n))
            if self.path == "/command":
                result = self.server.executor.command(body)
            elif self.path == "/rules":
                result = self.server.executor.read()
            elif self.path == "/preview":
                result = preview(
                    str(ipaddress.ip_address(body["ip"])),
                    json.loads(OBS.read_text()),
                    int(time.time()),
                )
            else:
                self.send_error(404)
                return
            status = 200
        except Conflict as exc:
            status, result = 409, {"error": str(exc)}
        except (KeyError, ValueError, TypeError):
            status, result = 400, {"error": "invalid_request"}
        except (OSError, sqlite3.Error, subprocess.SubprocessError, RuntimeError):
            status, result = 503, {"error": "executor_unavailable"}
        raw = json.dumps(result).encode()
        self.send_response(status)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    initialize(os.environ["MQTT_EXTERNAL_INTERFACE"])
    executor = Executor(sqlite3.connect(STATE))
    SOCKET.unlink(missing_ok=True)
    with socketserver.UnixStreamServer(str(SOCKET), Handler) as server:
        os.chmod(SOCKET, 0o600)
        server.executor = executor
        server.serve_forever()
