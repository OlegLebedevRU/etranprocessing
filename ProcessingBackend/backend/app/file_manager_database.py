"""Bounded FM database budget, independent of the terminal/payment pool."""

import logging
from collections.abc import AsyncIterator
from time import monotonic

from fastapi import HTTPException, Request
from sqlalchemy.exc import DBAPIError
from sqlalchemy.exc import TimeoutError as PoolTimeoutError
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine

from app.config import settings

logger = logging.getLogger("uvicorn.error.fm_database")

engine = create_async_engine(
    settings.database_url,
    pool_pre_ping=True,
    pool_recycle=300,
    pool_size=settings.file_manager_db_pool_size,
    max_overflow=0,
    pool_timeout=settings.file_manager_db_pool_timeout_seconds,
    connect_args={
        "timeout": settings.file_manager_db_connect_timeout_seconds,
        "command_timeout": settings.file_manager_db_command_timeout_seconds,
        "server_settings": {"application_name": "processingbackend-fm"},
    },
)
async_session = async_sessionmaker(engine, expire_on_commit=False)


async def get_fm_db(request: Request) -> AsyncIterator[AsyncSession]:
    started = monotonic()
    acquired: float | None = None
    outcome = "ok"
    try:
        async with async_session() as session:
            await session.connection()
            acquired = monotonic()
            yield session
    except (PoolTimeoutError, DBAPIError, TimeoutError, ConnectionError) as exc:
        # No retries: callers may have already committed an operation.
        outcome = "unavailable"
        logger.warning("fm_database_unavailable error=%s", type(exc).__name__)
        raise HTTPException(
            503,
            detail={"code": "fm_database_unavailable"},
            headers={"Retry-After": "5", "Cache-Control": "no-store"},
        ) from exc
    except BaseException:
        outcome = "aborted"
        raise
    finally:
        finished = monotonic()
        route = request.scope.get("route")
        logger.info(
            "fm_database path=%s outcome=%s acquire_ms=%.1f total_ms=%.1f",
            route.path if route is not None else "fm",
            outcome,
            ((acquired or finished) - started) * 1000,
            (finished - started) * 1000,
        )
