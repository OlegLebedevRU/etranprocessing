"""Passive MQTT log correlation. Only reduced snapshots cross the host boundary."""

import collections
import datetime as dt
import ipaddress
import json
import os
import re
import subprocess
import threading
import time
from pathlib import Path

OUT = Path(os.environ.get("MQTT_OBSERVATIONS", "/run/mb-mqtt-observer/observations.json"))
BROKER = os.environ.get("MQTT_CONTAINER", "rabbitmq")
ANSI = re.compile(r"\x1b\[[\d;]*m")
PID = re.compile(r"<\d+\.\d+\.\d+>")
PEER = re.compile(r"Rejected MQTT connection (\S+) -> \S+:8883\b")
CREDENTIAL = re.compile(r"failed to authenticate: user '([^']{1,128})'")
MISMATCH = re.compile(
    r'client_id in the certificate .*?does not match the client-provided ID \(<<"([^"]{1,128})">>\)'
)
TIMEOUT = re.compile(r"timeout error during handshake .*?peer (\S+)")
lock = threading.Lock()
events = collections.OrderedDict()
pending = collections.OrderedDict()
success = {}
active = {}
previous_ports = {}
sample_at = 0
history_since = int(time.time())
stream_live = False
loss_at = 0
cursor = ""
cursor_counts = collections.Counter()


def address(peer):
    try:
        host, port = peer.rsplit(":", 1)
        if not port.isdigit():
            return None
        return str(ipaddress.ip_address(host.strip("[]")))
    except ValueError:
        return None


def record(stamp, ip, identity, reason):
    global loss_at
    if not ip or stamp < time.time() - 86400:
        return
    key = (stamp // 60, ip, identity, reason)
    row = events.get(key)
    if row:
        row[0] += 1
        row[2] = stamp
    else:
        if len(events) >= 160000:
            events.popitem(last=False)
            loss_at = int(time.time())
        events[key] = [1, stamp, stamp]


def consume(line):
    timestamp, _, msg = line.partition(" ")
    try:
        stamp = int(dt.datetime.fromisoformat(timestamp[:19] + "+00:00").timestamp())
    except ValueError:
        return
    msg = ANSI.sub("", msg)
    pid = PID.search(msg)
    if not pid:
        return
    key = pid[0]
    credential = CREDENTIAL.search(msg)
    mismatch = MISMATCH.search(msg)
    if credential or mismatch:
        identity = (credential or mismatch)[1]
        pending[key] = (stamp, identity, "credentials" if credential else "client_id")
        if len(pending) > 8192:
            pending.popitem(last=False)
        return
    match = PEER.search(msg)
    if match:
        item = pending.pop(key, None)
        identity, reason = item[1:] if item and abs(stamp - item[0]) <= 10 else ("", "refused")
        record(stamp, address(match[1]), identity, reason)
    elif "mqtt/ssl" in msg and (match := TIMEOUT.search(msg)):
        record(stamp, address(match[1].rstrip(",.")), "", "tls_timeout")


def follow():
    global stream_live, cursor, cursor_counts
    while True:
        since = cursor or "24h"
        replay = cursor_counts.copy()
        with subprocess.Popen(
            ["docker", "logs", "--follow", "--timestamps", "--since", since, BROKER],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
        ) as proc:
            for line in proc.stdout:
                ts = line.partition(" ")[0]
                if len(ts) < 20 or not ts.startswith("20"):
                    continue
                with lock:
                    if ts == since and replay[line] > 0:
                        replay[line] -= 1
                        continue
                    if cursor != ts:
                        cursor, cursor_counts = ts, collections.Counter()
                    cursor_counts[line] += 1
                    consume(line)
                    stream_live = True
        with lock:
            stream_live = False
        time.sleep(5)


def sample():
    global active, sample_at, history_since, previous_ports, loss_at
    while True:
        try:
            raw = subprocess.check_output(
                [
                    "docker",
                    "exec",
                    BROKER,
                    "rabbitmqctl",
                    "-q",
                    "list_mqtt_connections",
                    "peer_host",
                    "user",
                    "peer_port",
                ],
                timeout=18,
                text=True,
            )
            peers = collections.defaultdict(set)
            ports = collections.defaultdict(set)
            for line in raw.splitlines():
                fields = line.split()
                if len(fields) != 3 or not fields[2].isdigit():
                    continue
                try:
                    ip = str(ipaddress.ip_address(fields[0]))
                except ValueError:
                    continue
                user = fields[1][:128]
                peers[ip].add(user)
                ports[ip, user].add(fields[2])
            now = int(time.time())
            with lock:
                if sample_at and now - sample_at > 120:
                    history_since = now
                elif sample_at:
                    for key, current in ports.items():
                        if key in success:
                            for _ in current - previous_ports.get(key, set()):
                                record(now, *key, "reconnect")
                previous_ports = ports
                active, sample_at = peers, now
                for ip, users in peers.items():
                    for user in users:
                        success.pop((ip, user), None)
                        if len(success) >= 160000:
                            del success[next(iter(success))]
                            loss_at = now
                        success[ip, user] = now
        except (OSError, subprocess.SubprocessError):
            pass  # sample_at exposes the age of the last valid snapshot.
        time.sleep(55)


def snapshot(now):
    for k in list(events):
        if k[0] < (now - 86400) // 60:
            del events[k]
    for k in list(success):
        if success[k] < now - 86400:
            del success[k]
    rows = {}
    for (minute, ip, user, reason), (count, first, last) in events.items():
        key = ip, user, reason
        if key not in rows:
            rows[key] = dict(
                ip=ip, login=user, reason=reason, counts=[0, 0, 0], first=first, last=last
            )
        row = rows[key]
        for i, window in enumerate((5, 60, 1440)):
            if 0 <= now // 60 - minute < window:
                row["counts"][i] += count
        row["last"] = max(last, row["last"])
    ranked = sorted(rows.values(), key=lambda r: (r["counts"][1], r["last"]), reverse=True)
    return dict(
        at=now,
        live=stream_live,
        sample_at=sample_at,
        history_since=history_since,
        incomplete=len(ranked) > 4000 or loss_at > now - 86400,
        rows=ranked[:4000],
        active={ip: sorted(v) for ip, v in active.items()},
        success=[[ip, user, stamp] for (ip, user), stamp in success.items()],
    )


if __name__ == "__main__":
    for target in (follow, sample):
        threading.Thread(target=target, daemon=True).start()
    while True:
        with lock:
            result = snapshot(int(time.time()))
        tmp = OUT.with_suffix(".tmp")
        tmp.write_text(json.dumps(result, separators=(",", ":")), encoding="utf8")
        tmp.replace(OUT)
        time.sleep(5)
