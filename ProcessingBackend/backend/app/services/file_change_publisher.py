"""Publish file-state invalidation only after the owning transaction commits."""

import asyncio
import json
import logging
import os

import aio_pika
from etranprocessing_db.file_manager import (
    FileManagerOperation,
    FileManagerReadinessProbe,
)
from sqlalchemy import event
from sqlalchemy.orm import Session

log = logging.getLogger(__name__)
_pending: set[tuple[int, int]] = set()
_wake = asyncio.Event()
_KEY = "portal_file_changes"


@event.listens_for(Session, "before_flush")
def collect(session, _context, _instances):
    for obj in (*session.new, *session.dirty):
        if isinstance(obj, (FileManagerOperation, FileManagerReadinessProbe)):
            session.info.setdefault(_KEY, set()).add((obj.tenant_id, obj.terminal_id))


@event.listens_for(Session, "after_rollback")
def discard(session):
    session.info.pop(_KEY, None)


@event.listens_for(Session, "after_commit")
def committed(session):
    if session.in_nested_transaction():
        return
    items = session.info.pop(_KEY, ())
    if items:
        for item in items:
            if len(_pending) < 512:
                _pending.add(item)
        _wake.set()


async def run(url: str):
    while True:
        try:
            async with await aio_pika.connect(url, timeout=5) as connection:
                channel = await connection.channel()
                exchange = await channel.get_exchange("portal.changes.v1", ensure=False)
                while True:
                    await _wake.wait()
                    await asyncio.sleep(0.15)
                    batch = _pending.copy()
                    _pending.clear()
                    _wake.clear()
                    for org, terminal in batch:
                        body = json.dumps(
                            {
                                "v": 1,
                                "org": org,
                                "terminal": terminal,
                                "kinds": ["files"],
                            }
                        ).encode()
                        await exchange.publish(
                            aio_pika.Message(body=body),
                            routing_key="",
                            mandatory=False,
                            timeout=2,
                        )
        except OSError, TimeoutError, aio_pika.exceptions.AMQPException, RuntimeError:
            log.warning("File notification channel unavailable; readers reconcile")
            await asyncio.sleep(5)


def start():
    url = os.environ.get("PORTAL_AMQP_URL")
    return asyncio.create_task(run(url)) if url else None
