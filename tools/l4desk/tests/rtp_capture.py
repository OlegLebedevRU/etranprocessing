"""Capture RTP/RTCP packets on UDP ports 5004/5005 and analyze them."""
import socket
import struct
import threading
import time
import sys

stats = {"rtp": [], "rtcp": []}

def parse_rtp_header(data):
    if len(data) < 12:
        return None
    b0, b1 = data[0], data[1]
    version = (b0 >> 6) & 3
    padding = (b0 >> 5) & 1
    extension = (b0 >> 4) & 1
    cc = b0 & 0x0F
    marker = (b1 >> 7) & 1
    pt = b1 & 0x7F
    seq = struct.unpack("!H", data[2:4])[0]
    ts = struct.unpack("!I", data[4:8])[0]
    ssrc = struct.unpack("!I", data[8:12])[0]
    payload_offset = 12 + cc * 4
    if extension and len(data) > payload_offset + 4:
        ext_len = struct.unpack("!H", data[payload_offset+2:payload_offset+4])[0]
        payload_offset += 4 + ext_len * 4
    nal_type = None
    if pt == 96 and len(data) > payload_offset:
        first_byte = data[payload_offset]
        nal_type = first_byte & 0x1F
        if nal_type == 28 and len(data) > payload_offset + 1:
            fu_byte = data[payload_offset + 1]
            nal_type = f"FU-A({fu_byte & 0x1F})"
            if fu_byte & 0x80:
                nal_type += " START"
            if fu_byte & 0x40:
                nal_type += " END"
    return {
        "version": version, "marker": marker, "pt": pt, "seq": seq,
        "ts": ts, "ssrc": ssrc, "payload_len": len(data) - payload_offset,
        "nal_type": nal_type, "total_len": len(data),
    }

def parse_rtcp_header(data):
    if len(data) < 8:
        return None
    pt = data[1]
    types = {200: "SR", 201: "SDES", 202: "RR", 203: "BYE", 206: "PSFB"}
    return {"pt": pt, "type": types.get(pt, str(pt)), "len": len(data)}

def listener(port, name, proto_type, duration):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(1.0)
    start = time.time()
    first_ts = None
    first_wall = None
    seq_prev = None
    gaps = 0
    ts_jitter_max = 0
    count = 0
    while time.time() - start < duration:
        try:
            data, addr = sock.recvfrom(65535)
        except socket.timeout:
            continue
        wall = time.time()
        count += 1
        if len(data) >= 2 and (data[1] & 0x7F) in (200, 201, 202, 203, 206):
            info = parse_rtcp_header(data)
            if info:
                stats["rtcp"].append({"port": port, "wall": wall, **info})
            continue
        info = parse_rtp_header(data)
        if not info:
            continue
        if first_ts is None:
            first_ts = info["ts"]
            first_wall = wall
        elapsed_ms = (wall - first_wall) * 1000
        ts_ms = (info["ts"] - first_ts) / 90.0
        drift = abs(elapsed_ms - ts_ms)
        if drift > ts_jitter_max:
            ts_jitter_max = drift
        if seq_prev is not None:
            expected = (seq_prev + 1) & 0xFFFF
            if info["seq"] != expected:
                gaps += 1
        seq_prev = info["seq"]
        stats["rtp"].append({
            "port": port, "wall": wall, "elapsed_ms": elapsed_ms,
            "ts_ms": ts_ms, "drift": drift, **info,
        })
    sock.close()
    stats["_summary_" + str(port)] = {
        "count": count, "gaps": gaps, "ts_jitter_max_ms": ts_jitter_max,
    }

def main():
    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    print(f"Capturing on ports 5004 (RTP) and 5005 (RTCP) for {duration}s...")
    t1 = threading.Thread(target=listener, args=(5004, "RTP", "rtp", duration))
    t2 = threading.Thread(target=listener, args=(5005, "RTCP", "rtcp", duration))
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    rtp = stats["rtp"]
    rtcp = stats["rtcp"]
    s5004 = stats.get("_summary_5004", {})
    s5005 = stats.get("_summary_5005", {})

    print(f"\n=== CAPTURE RESULTS ({duration}s) ===")
    print(f"RTP packets:  {s5004.get('count', 0)} (port 5004)")
    print(f"RTCP packets: {s5005.get('count', 0)} (port 5005)")
    print(f"Seq gaps:     {s5004.get('gaps', 0)}")
    print(f"Max TS drift: {s5004.get('ts_jitter_max_ms', 0):.1f} ms")

    if rtp:
        nals = {}
        pts = set()
        for p in rtp:
            nt = p.get("nal_type")
            if isinstance(nt, int):
                nals[nt] = nals.get(nt, 0) + 1
            elif isinstance(nt, str) and "FU-A" in nt:
                nals["FU-A"] = nals.get("FU-A", 0) + 1
            pts.add(p["pt"])
        print(f"PT values:    {sorted(pts)}")
        print(f"SSRC:         0x{rtp[0]['ssrc']:08X}")
        print(f"NAL types:    {nals}")

        print(f"\n--- First 15 RTP packets ---")
        for p in rtp[:15]:
            print(f"  seq={p['seq']:5d}  ts={p['ts']:10d}  marker={p['marker']}  "
                  f"payload={p['payload_len']:5d}B  nal={p.get('nal_type','?')}")

        print(f"\n--- IDR analysis (marker=1 packets) ---")
        idr_markers = [p for p in rtp if p["marker"] == 1][:5]
        for p in idr_markers:
            print(f"  seq={p['seq']:5d}  ts={p['ts']:10d}  payload={p['payload_len']:5d}B")

        if len(rtp) > 100:
            print(f"\n--- RTP around frame 100 (seq ~{rtp[100]['seq']}) ---")
            for p in rtp[95:110]:
                print(f"  seq={p['seq']:5d}  ts={p['ts']:10d}  marker={p['marker']}  "
                      f"payload={p['payload_len']:5d}B  nal={p.get('nal_type','?')}  "
                      f"drift={p['drift']:.1f}ms")

    if rtcp:
        print(f"\n--- RTCP packets ---")
        for p in rtcp[:10]:
            print(f"  type={p['type']}  len={p['len']}")

if __name__ == "__main__":
    main()
