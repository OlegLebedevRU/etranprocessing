"""Canonical server-side terminal creation use case (L4D-13-MB-FIX-01).

Single source of SN formula and new device_id allocation for admin and L4Desk flows.
"""

from __future__ import annotations

import contextlib
import secrets
from datetime import UTC, datetime

from fastapi import HTTPException, status
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import Terminal

DEVICE_ID_MIN = 1_000_001
DEVICE_ID_MAX = 1_999_999
_ALLOCATE_RETRIES = 32


def generate_device_sn(device_id: int) -> str:
    """Canonical platform SN: a4b<7-digit device_id>c<5-digit random>d<DDMMYY>."""
    device_part = f"{device_id:07d}"
    rand_first = str(secrets.randbelow(9) + 1)
    rand_rest = "".join(str(secrets.randbelow(10)) for _ in range(4))
    random_part = rand_first + rand_rest
    date_part = datetime.now(UTC).strftime("%d%m%y")
    return f"a4b{device_part}c{random_part}d{date_part}"


def validate_new_device_id(device_id: int) -> None:
    if device_id < DEVICE_ID_MIN or device_id > DEVICE_ID_MAX:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail=(
                f"device_id must be in range {DEVICE_ID_MIN}…{DEVICE_ID_MAX} "
                "for newly created terminals"
            ),
        )


async def peek_next_device_id(db: AsyncSession) -> int:
    """First free device_id in the new-terminal range (no reservation)."""
    res = await db.execute(
        select(Terminal.device_id)
        .where(Terminal.device_id >= DEVICE_ID_MIN, Terminal.device_id <= DEVICE_ID_MAX)
        .order_by(Terminal.device_id)
    )
    taken = set(res.scalars().all())
    for candidate in range(DEVICE_ID_MIN, DEVICE_ID_MAX + 1):
        if candidate not in taken:
            return candidate
    raise HTTPException(
        status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
        detail="New terminal device_id range is exhausted",
    )


async def generate_unique_sn(db: AsyncSession, device_id: int) -> str:
    for _ in range(10):
        sn = generate_device_sn(device_id)
        existing = await db.execute(select(Terminal.id).where(Terminal.sn == sn))
        if not existing.scalar_one_or_none():
            return sn
    raise HTTPException(
        status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
        detail="Failed to generate unique SN",
    )


async def create_terminal_business_record(
    db: AsyncSession,
    *,
    org_id: int,
    address: str | None = None,
    note: str | None = None,
    timezone: str | None = None,
    terminal_type_id: int = 0,
    is_active: bool = True,
    show_in_monitoring: bool | None = None,
    device_id: int | None = None,
) -> Terminal:
    """Create Terminal business record with server-owned SN and device_id.

    Caller owns the surrounding transaction (license, L4Desk row, audit, commit).
    Flushes the Terminal row so identity uniqueness is enforced before extras.
    """
    explicit_id = device_id is not None
    if explicit_id:
        validate_new_device_id(device_id)
        existing = await db.execute(
            select(Terminal.id).where(Terminal.device_id == device_id)
        )
        if existing.scalar_one_or_none():
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail=f"Terminal with device_id {device_id} already exists",
            )

    last_error: Exception | None = None
    for _ in range(_ALLOCATE_RETRIES):
        candidate_id = device_id if explicit_id else await peek_next_device_id(db)
        sn = await generate_unique_sn(db, candidate_id)
        terminal = Terminal(
            device_id=candidate_id,
            sn=sn,
            org_id=org_id,
            address=address.strip() if address else None,
            note=note.strip() if note else None,
            timezone=timezone.strip() if timezone else None,
            terminal_type_id=terminal_type_id,
            is_active=is_active,
            show_in_monitoring=True
            if show_in_monitoring is None
            else show_in_monitoring,
        )
        db.add(terminal)
        try:
            async with db.begin_nested():
                await db.flush()
            return terminal
        except IntegrityError as exc:
            last_error = exc
            with contextlib.suppress(Exception):
                db.expunge(terminal)
            if explicit_id:
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=f"Terminal with device_id {device_id} already exists",
                ) from exc
            continue

    raise HTTPException(
        status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
        detail="Failed to allocate unique terminal identity",
    ) from last_error
