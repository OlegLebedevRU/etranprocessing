"""Operational hot telemetry datastore for l4media.

Stores high-volume technical stream samples and quality events in hot storage.
Supports bounded chunked purging of verified technical data while strictly
retaining route configurations, session summaries, and reconciliation metadata.
"""

from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any

from .canonical import (
    OWNER_PROJECT,
    RECORD_TYPE_QUALITY,
    RECORD_TYPE_SAMPLES,
    canonical_json_dumps,
)


class TelemetryStore:
    """SQLite-backed hot operational store for l4media stream samples and quality events."""

    def __init__(self, db_path: Path | str = ":memory:"):
        self.db_path = str(db_path)
        if self.db_path != ":memory:":
            Path(self.db_path).parent.mkdir(parents=True, exist_ok=True)
        self._conn = sqlite3.connect(self.db_path)
        self._conn.row_factory = sqlite3.Row
        if self.db_path != ":memory:":
            self._conn.execute("PRAGMA journal_mode=WAL;")
        self._init_db()

    def close(self) -> None:
        if self._conn:
            self._conn.close()
            self._conn = None

    def __enter__(self) -> TelemetryStore:
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.close()

    def _get_connection(self) -> sqlite3.Connection:
        if self._conn is None:
            raise RuntimeError("TelemetryStore is closed")
        return self._conn

    def _init_db(self) -> None:
        """Create tables and indexes for samples and quality events."""
        with self._get_connection() as conn:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS tb_media_stream_samples (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    record_id TEXT NOT NULL UNIQUE,
                    session_id TEXT NOT NULL,
                    sn TEXT,
                    tenant_id INTEGER,
                    terminal_id INTEGER,
                    occurred_at_utc TEXT NOT NULL,
                    source_month TEXT NOT NULL,
                    cursor INTEGER,
                    rtp_packets INTEGER DEFAULT 0,
                    rtcp_packets INTEGER DEFAULT 0,
                    bytes INTEGER DEFAULT 0,
                    bitrate_kbps REAL DEFAULT 0.0,
                    packet_loss_pct REAL DEFAULT 0.0,
                    jitter_ms REAL DEFAULT 0.0,
                    rtt_ms REAL DEFAULT 0.0,
                    fps REAL DEFAULT 0.0,
                    frame_width INTEGER DEFAULT 0,
                    frame_height INTEGER DEFAULT 0,
                    payload_json TEXT NOT NULL
                );
            """)
            conn.execute("""
                CREATE TABLE IF NOT EXISTS tb_media_quality_events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    record_id TEXT NOT NULL UNIQUE,
                    session_id TEXT NOT NULL,
                    sn TEXT,
                    tenant_id INTEGER,
                    terminal_id INTEGER,
                    occurred_at_utc TEXT NOT NULL,
                    source_month TEXT NOT NULL,
                    cursor INTEGER,
                    event_type TEXT NOT NULL,
                    metric_value REAL DEFAULT 0.0,
                    reason TEXT,
                    payload_json TEXT NOT NULL
                );
            """)
            # Non-purgeable session summary table (strictly retained online for audit and reconciliation)
            conn.execute("""
                CREATE TABLE IF NOT EXISTS tb_media_session_summaries (
                    session_id TEXT PRIMARY KEY,
                    operation_id TEXT,
                    sn TEXT NOT NULL,
                    device_id INTEGER,
                    mountpoint_id INTEGER,
                    state TEXT NOT NULL,
                    created_at_utc TEXT NOT NULL,
                    started_at_utc TEXT,
                    stopped_at_utc TEXT,
                    total_duration_sec INTEGER DEFAULT 0,
                    final_rtp_packets INTEGER DEFAULT 0,
                    final_bytes INTEGER DEFAULT 0,
                    stop_reason TEXT
                );
            """)

            conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_samples_month ON tb_media_stream_samples(source_month, occurred_at_utc);"
            )
            conn.execute("CREATE INDEX IF NOT EXISTS idx_samples_cursor ON tb_media_stream_samples(cursor);")
            conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_quality_month ON tb_media_quality_events(source_month, occurred_at_utc);"
            )
            conn.execute("CREATE INDEX IF NOT EXISTS idx_quality_cursor ON tb_media_quality_events(cursor);")
            conn.commit()

    def add_sample(
        self,
        record_id: str,
        session_id: str,
        occurred_at_utc: str,
        source_month: str,
        sn: str | None = None,
        tenant_id: int | None = None,
        terminal_id: int | None = None,
        cursor: int | None = None,
        rtp_packets: int = 0,
        rtcp_packets: int = 0,
        bytes_count: int = 0,
        bitrate_kbps: float = 0.0,
        packet_loss_pct: float = 0.0,
        jitter_ms: float = 0.0,
        rtt_ms: float = 0.0,
        fps: float = 0.0,
        frame_width: int = 0,
        frame_height: int = 0,
        extra_payload: dict[str, Any] | None = None,
    ) -> None:
        payload = {
            "rtp_packets": rtp_packets,
            "rtcp_packets": rtcp_packets,
            "bytes": bytes_count,
            "bitrate_kbps": bitrate_kbps,
            "packet_loss_pct": packet_loss_pct,
            "jitter_ms": jitter_ms,
            "rtt_ms": rtt_ms,
            "fps": fps,
            "frame_width": frame_width,
            "frame_height": frame_height,
        }
        if extra_payload:
            payload.update(extra_payload)

        with self._get_connection() as conn:
            conn.execute(
                """
                INSERT OR REPLACE INTO tb_media_stream_samples (
                    record_id, session_id, sn, tenant_id, terminal_id,
                    occurred_at_utc, source_month, cursor,
                    rtp_packets, rtcp_packets, bytes, bitrate_kbps,
                    packet_loss_pct, jitter_ms, rtt_ms, fps,
                    frame_width, frame_height, payload_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    record_id,
                    session_id,
                    sn,
                    tenant_id,
                    terminal_id,
                    occurred_at_utc,
                    source_month,
                    cursor,
                    rtp_packets,
                    rtcp_packets,
                    bytes_count,
                    bitrate_kbps,
                    packet_loss_pct,
                    jitter_ms,
                    rtt_ms,
                    fps,
                    frame_width,
                    frame_height,
                    canonical_json_dumps(payload),
                ),
            )
            conn.commit()

    def add_quality_event(
        self,
        record_id: str,
        session_id: str,
        occurred_at_utc: str,
        source_month: str,
        event_type: str,
        sn: str | None = None,
        tenant_id: int | None = None,
        terminal_id: int | None = None,
        cursor: int | None = None,
        metric_value: float = 0.0,
        reason: str | None = None,
        extra_payload: dict[str, Any] | None = None,
    ) -> None:
        payload = {
            "event_type": event_type,
            "metric_value": metric_value,
            "reason": reason,
        }
        if extra_payload:
            payload.update(extra_payload)

        with self._get_connection() as conn:
            conn.execute(
                """
                INSERT OR REPLACE INTO tb_media_quality_events (
                    record_id, session_id, sn, tenant_id, terminal_id,
                    occurred_at_utc, source_month, cursor,
                    event_type, metric_value, reason, payload_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    record_id,
                    session_id,
                    sn,
                    tenant_id,
                    terminal_id,
                    occurred_at_utc,
                    source_month,
                    cursor,
                    event_type,
                    metric_value,
                    reason,
                    canonical_json_dumps(payload),
                ),
            )
            conn.commit()

    def record_session_summary(
        self,
        session_id: str,
        sn: str,
        state: str,
        created_at_utc: str,
        operation_id: str | None = None,
        device_id: int | None = None,
        mountpoint_id: int | None = None,
        started_at_utc: str | None = None,
        stopped_at_utc: str | None = None,
        total_duration_sec: int = 0,
        final_rtp_packets: int = 0,
        final_bytes: int = 0,
        stop_reason: str | None = None,
    ) -> None:
        """Record non-purgeable operational session summary (stays hot)."""
        with self._get_connection() as conn:
            conn.execute(
                """
                INSERT OR REPLACE INTO tb_media_session_summaries (
                    session_id, operation_id, sn, device_id, mountpoint_id,
                    state, created_at_utc, started_at_utc, stopped_at_utc,
                    total_duration_sec, final_rtp_packets, final_bytes, stop_reason
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    session_id,
                    operation_id,
                    sn,
                    device_id,
                    mountpoint_id,
                    state,
                    created_at_utc,
                    started_at_utc,
                    stopped_at_utc,
                    total_duration_sec,
                    final_rtp_packets,
                    final_bytes,
                    stop_reason,
                ),
            )
            conn.commit()

    def get_counts_for_month(self, source_month: str) -> dict[str, int]:
        with self._get_connection() as conn:
            c1 = conn.execute(
                "SELECT COUNT(*) FROM tb_media_stream_samples WHERE source_month = ?",
                (source_month,),
            ).fetchone()[0]
            c2 = conn.execute(
                "SELECT COUNT(*) FROM tb_media_quality_events WHERE source_month = ?",
                (source_month,),
            ).fetchone()[0]
            return {
                RECORD_TYPE_SAMPLES: c1,
                RECORD_TYPE_QUALITY: c2,
                "total_records": c1 + c2,
            }

    def get_time_range_for_month(self, source_month: str) -> dict[str, str] | None:
        with self._get_connection() as conn:
            row = conn.execute(
                """
                SELECT MIN(occurred_at_utc) as min_ts, MAX(occurred_at_utc) as max_ts
                FROM (
                    SELECT occurred_at_utc FROM tb_media_stream_samples WHERE source_month = ?
                    UNION ALL
                    SELECT occurred_at_utc FROM tb_media_quality_events WHERE source_month = ?
                )
                """,
                (source_month, source_month),
            ).fetchone()
            if row and row["min_ts"] and row["max_ts"]:
                return {
                    "min_occurred_at": row["min_ts"],
                    "max_occurred_at": row["max_ts"],
                }
            return None

    def get_cursor_bounds_for_month(self, source_month: str) -> dict[str, Any] | None:
        with self._get_connection() as conn:
            row = conn.execute(
                """
                SELECT MIN(cursor) as min_c, MAX(cursor) as max_c
                FROM (
                    SELECT cursor FROM tb_media_stream_samples WHERE source_month = ? AND cursor IS NOT NULL
                    UNION ALL
                    SELECT cursor FROM tb_media_quality_events WHERE source_month = ? AND cursor IS NOT NULL
                )
                """,
                (source_month, source_month),
            ).fetchone()
            if row and row["min_c"] is not None and row["max_c"] is not None:
                max_c = row["max_c"]
                return {
                    "min_cursor": row["min_c"],
                    "max_cursor": max_c,
                    "through_cursor": max_c,
                    "consumers_passed_cursor": max_c,
                }
            return None

    def fetch_samples_for_month(self, source_month: str) -> list[dict[str, Any]]:
        records: list[dict[str, Any]] = []
        with self._get_connection() as conn:
            cur = conn.execute(
                """
                SELECT record_id, session_id, sn, tenant_id, terminal_id,
                       occurred_at_utc, cursor, payload_json
                FROM tb_media_stream_samples
                WHERE source_month = ?
                ORDER BY occurred_at_utc ASC, id ASC
                """,
                (source_month,),
            )
            for row in cur:
                records.append(
                    {
                        "record_type": RECORD_TYPE_SAMPLES,
                        "record_id": row["record_id"],
                        "occurred_at_utc": row["occurred_at_utc"],
                        "source_project": OWNER_PROJECT,
                        "cursor": row["cursor"],
                        "tenant_id": row["tenant_id"],
                        "terminal_id": row["terminal_id"],
                        "sn": row["sn"],
                        "session_id": row["session_id"],
                        "payload": json.loads(row["payload_json"]),
                    }
                )
        return records

    def fetch_quality_events_for_month(self, source_month: str) -> list[dict[str, Any]]:
        records: list[dict[str, Any]] = []
        with self._get_connection() as conn:
            cur = conn.execute(
                """
                SELECT record_id, session_id, sn, tenant_id, terminal_id,
                       occurred_at_utc, cursor, payload_json
                FROM tb_media_quality_events
                WHERE source_month = ?
                ORDER BY occurred_at_utc ASC, id ASC
                """,
                (source_month,),
            )
            for row in cur:
                records.append(
                    {
                        "record_type": RECORD_TYPE_QUALITY,
                        "record_id": row["record_id"],
                        "occurred_at_utc": row["occurred_at_utc"],
                        "source_project": OWNER_PROJECT,
                        "cursor": row["cursor"],
                        "tenant_id": row["tenant_id"],
                        "terminal_id": row["terminal_id"],
                        "sn": row["sn"],
                        "session_id": row["session_id"],
                        "payload": json.loads(row["payload_json"]),
                    }
                )
        return records

    def purge_records_chunked(self, source_month: str, chunk_size: int = 1000) -> int:
        """Purge verified high-volume technical stream samples and quality events in bounded chunks.

        Note:
            Session summaries (tb_media_session_summaries) are NEVER purged.

        Returns:
            Total number of purged records.
        """
        total_purged = 0
        with self._get_connection() as conn:
            # 1. Purge samples in chunks
            while True:
                ids = [
                    row[0]
                    for row in conn.execute(
                        "SELECT id FROM tb_media_stream_samples WHERE source_month = ? LIMIT ?",
                        (source_month, chunk_size),
                    ).fetchall()
                ]
                if not ids:
                    break
                conn.execute(
                    f"DELETE FROM tb_media_stream_samples WHERE id IN ({','.join(['?'] * len(ids))})",
                    ids,
                )
                conn.commit()
                total_purged += len(ids)

            # 2. Purge quality events in chunks
            while True:
                ids = [
                    row[0]
                    for row in conn.execute(
                        "SELECT id FROM tb_media_quality_events WHERE source_month = ? LIMIT ?",
                        (source_month, chunk_size),
                    ).fetchall()
                ]
                if not ids:
                    break
                conn.execute(
                    f"DELETE FROM tb_media_quality_events WHERE id IN ({','.join(['?'] * len(ids))})",
                    ids,
                )
                conn.commit()
                total_purged += len(ids)

        return total_purged

    def get_session_summary_count(self) -> int:
        with self._get_connection() as conn:
            return conn.execute("SELECT COUNT(*) FROM tb_media_session_summaries").fetchone()[0]
