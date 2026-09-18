from __future__ import annotations

"""Database models for IoT event consumer storage re-exported from shared etranprocessing_db.l4desk."""

from etranprocessing_db.l4desk import (
    IotConsumerCheckpoint,
    IotEventInbox,
    IotEventQuarantine,
)

__all__ = [
    "IotConsumerCheckpoint",
    "IotEventInbox",
    "IotEventQuarantine",
]
