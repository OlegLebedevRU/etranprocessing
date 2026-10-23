"""Test-only local RTP/RTCP relay. Camera bitstream and parameter sets stay native."""

from __future__ import annotations

import contextlib
import socket
import subprocess
import threading
import time

from native_camera_rtp import NativeRtpMonitor


class NativeRelay:
    def __init__(self, url: str, ffmpeg: str, parameters: dict[int, bytes], *, rtp_port=5004, rtcp_port=5005):
        self.url, self.ffmpeg = url, ffmpeg
        self.targets = (rtp_port, rtcp_port)
        self.monitor = NativeRtpMonitor(parameters)
        self.halt = threading.Event()
        self.sockets: list[socket.socket] = []
        self.threads: list[threading.Thread] = []
        self.process: subprocess.Popen | None = None
        self.last_packet_at: float | None = None
        self.failure: str | None = None
        self.forwarded = 0
        self.started_at = 0.0

    def start(self) -> None:
        if self.started_at or self.halt.is_set():
            raise ValueError("relay_generation_already_used")
        self.started_at = time.monotonic()
        try:
            for _ in range(2):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.sockets.append(sock)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
                sock.bind(("127.0.0.1", 0))
                sock.settimeout(0.1)
            rtp, rtcp = (sock.getsockname()[1] for sock in self.sockets)
            self.process = subprocess.Popen(
                [
                    self.ffmpeg,
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-rtsp_transport",
                    "tcp",
                    "-i",
                    self.url,
                    "-map",
                    "0:v:0",
                    "-an",
                    "-c:v",
                    "copy",
                    "-payload_type",
                    "96",
                    "-f",
                    "rtp",
                    f"rtp://127.0.0.1:{rtp}?rtcpport={rtcp}&pkt_size=1200",
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            for control, sock in enumerate(self.sockets):
                thread = threading.Thread(target=self.relay, args=(sock, bool(control)), daemon=True)
                self.threads.append(thread)
                thread.start()
        except Exception:
            self.stop()
            raise ValueError("camera_relay_start_failed") from None

    def relay(self, sock: socket.socket, control: bool) -> None:
        source = None
        destination = ("127.0.0.1", self.targets[int(control)])
        while not self.halt.is_set():
            try:
                packet, address = sock.recvfrom(65535)
                if self.halt.is_set():
                    return
                if control and address == destination:
                    if source:
                        sock.sendto(packet, source)
                    continue
                if source is None:
                    source = address
                if address != source:
                    continue
                if not control:
                    if not self.monitor.accept(packet):
                        if self.monitor.failure:
                            self.failure = self.monitor.failure
                            return
                        continue
                    self.last_packet_at = time.monotonic()
                sock.sendto(packet, destination)
                if not control:
                    self.forwarded += 1
            except TimeoutError:
                continue
            except OSError:
                if not self.halt.is_set():
                    self.failure = "camera_relay_socket_failed"
                return

    def health(self) -> str | None:
        if self.failure:
            return self.failure
        if self.process is None or self.process.poll() is not None:
            return "camera_relay_process_exited"
        if time.monotonic() - (self.last_packet_at or self.started_at) > 10:
            return "camera_rtp_timeout"
        if self.monitor.last_parameters_at is not None and time.monotonic() - self.monitor.last_parameters_at > 55:
            return "camera_parameter_sets_stale"
        return None

    def stop(self) -> None:
        self.halt.set()
        if self.process is not None:
            with contextlib.suppress(OSError):
                self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
        for thread in self.threads:
            thread.join(timeout=1)
        for sock in self.sockets:
            sock.close()
        if any(thread.is_alive() for thread in self.threads):
            raise RuntimeError("camera_relay_cleanup_incomplete")
