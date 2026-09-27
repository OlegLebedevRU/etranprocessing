import asyncio
import logging
import random

import asyncpg
from etranprocessing_db.base import Base
from sqlalchemy.engine import make_url
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker, create_async_engine

from app.config import settings

logger = logging.getLogger(__name__)
_CONNECT_ATTEMPTS = 5
_CONNECT_TIMEOUT_SECONDS = 2


async def _connect_with_retry(dsn: str) -> asyncpg.Connection:
    """Retry only new connections; never replay SQL or a transaction."""
    for attempt in range(1, _CONNECT_ATTEMPTS + 1):
        try:
            return await asyncpg.connect(dsn=dsn, timeout=_CONNECT_TIMEOUT_SECONDS)
        except (OSError, TimeoutError, asyncpg.CannotConnectNowError) as exc:
            if attempt == _CONNECT_ATTEMPTS:
                logger.error(
                    "Database connection failed after %d attempts (%s)",
                    attempt,
                    type(exc).__name__,
                )
                raise
            logger.warning(
                "Database connection attempt %d/%d failed (%s)",
                attempt,
                _CONNECT_ATTEMPTS,
                type(exc).__name__,
            )
            await asyncio.sleep(random.uniform(0, min(0.1 * 2 ** (attempt - 1), 0.8)))
    raise AssertionError("unreachable")


_driver_dsn = (
    make_url(settings.database_url)
    .set(drivername="postgresql")
    .render_as_string(hide_password=False)
)


async def _create_connection() -> asyncpg.Connection:
    return await _connect_with_retry(_driver_dsn)


engine = create_async_engine(
    settings.database_url,
    echo=False,
    pool_pre_ping=True,
    pool_recycle=300,
    async_creator=_create_connection,
)
async_session = async_sessionmaker(engine, class_=AsyncSession, expire_on_commit=False)


async def get_db():
    async with async_session() as session:
        yield session


__all__ = ["Base", "async_session", "engine", "get_db"]
