import threading
from typing import Any


class GaugeStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._by_device_id: dict[int, dict[str, Any]] = {}
        self._by_sn: dict[str, dict[str, Any]] = {}

    def get_by_device_id(self, device_id: int) -> dict[str, Any] | None:
        with self._lock:
            return self._by_device_id.get(device_id)

    def get_by_sn(self, sn: str) -> dict[str, Any] | None:
        with self._lock:
            return self._by_sn.get(sn)

    def set_snapshot(self, snapshot: dict[str, Any]) -> None:
        device_id = snapshot.get("device_id")
        sn = snapshot.get("sn")
        with self._lock:
            if device_id is not None:
                self._by_device_id[int(device_id)] = snapshot
            if sn:
                self._by_sn[str(sn)] = snapshot

    def get_all(self) -> dict[int, dict[str, Any]]:
        with self._lock:
            return dict(self._by_device_id)

    def update_enrichment(self, device_id: int, updates: dict[str, Any]) -> None:
        with self._lock:
            snapshot = self._by_device_id.get(device_id)
            if snapshot:
                snapshot.setdefault("internal_enrichment", {}).update(updates)

    def clear(self) -> None:
        with self._lock:
            self._by_device_id.clear()
            self._by_sn.clear()
