"""Run via sudo unshare --net python3 netns_smoke.py; never touches the host table."""
import os
import sqlite3
import socket
import subprocess
import time

import edge


def connect(family, host, port):
    sock=socket.socket(family)
    sock.settimeout(0.45)
    try:
        sock.connect((host,port))
    except BaseException:
        sock.close()
        raise
    return sock


def check(family,host):
    with socket.socket(family) as listener,socket.socket(family) as sibling:
        for sock,port in [(listener,8883),(sibling,18883)]:
            sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            sock.bind((host,port));sock.listen()
        with connect(family,host,8883) as live,listener.accept()[0] as peer:
            edge.apply(host,int(time.time())+4)
            assert host in edge.members()[0]
            try:
                with connect(family,host,8883):
                    raise AssertionError("blocked handshake succeeded")
            except TimeoutError:
                pass
            live.sendall(b"ok");assert peer.recv(2)==b"ok"
            with connect(family,host,18883):
                pass
            edge.apply(host,0)
            with connect(family,host,8883):
                pass
            # Simulate a stopped executor with an accepted permanent decision.
            with sqlite3.connect(":memory:") as db:
                edge.Executor(db)
                db.execute("insert into rules values (?,?,?)", (host, 10, edge.PERMANENT))
                edge.initialize("lo")
                executor = edge.Executor(db)
                assert host in executor.read()["permanent_rules"]
                try:
                    with connect(family,host,8883):
                        raise AssertionError("persistent block missing after restart")
                except TimeoutError:
                    pass
                executor.command(dict(ip=host, revision=11, expiry=0))
                assert host not in executor.read()["active_rules"]
                with connect(family,host,8883):
                    pass
            edge.apply(host,int(time.time())+2)
            time.sleep(2.2)
            with connect(family,host,8883):
                pass


if __name__=="__main__":
    if os.stat("/proc/self/ns/net").st_ino==os.stat("/proc/1/ns/net").st_ino:
        raise SystemExit("Isolated network namespace required")
    subprocess.run(["ip","link","set","lo","up"],check=True)
    edge.initialize("lo")
    check(socket.AF_INET,"127.0.0.1")
    check(socket.AF_INET6,"::1")
    print("IPv4/IPv6: drop new, preserve established and other ports, remove, expire: OK")
