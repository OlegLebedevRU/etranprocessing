import asyncio

import asyncpg
import pytest

from app import database


@pytest.mark.anyio
async def test_new_connection_retries_reset_then_succeeds(monkeypatch):
    calls = []
    delays = []
    connection = object()

    async def connect(*, dsn, timeout):
        calls.append((dsn, timeout))
        if len(calls) < 3:
            raise ConnectionResetError("reset before TLS")
        return connection

    async def sleep(delay):
        delays.append(delay)

    monkeypatch.setattr(database.asyncpg, "connect", connect)
    monkeypatch.setattr(database.asyncio, "sleep", sleep)
    monkeypatch.setattr(database.random, "uniform", lambda _lo, hi: hi)

    assert (
        await database._connect_with_retry("postgresql://localhost/test") is connection
    )
    assert (
        calls
        == [("postgresql://localhost/test", database._CONNECT_TIMEOUT_SECONDS)] * 3
    )
    assert delays == [0.1, 0.2]


@pytest.mark.anyio
async def test_new_connection_stops_after_bounded_attempts(monkeypatch):
    calls = 0

    async def connect(*, dsn, timeout):
        nonlocal calls
        calls += 1
        raise ConnectionResetError("reset before TLS")

    async def sleep(_delay):
        return None

    monkeypatch.setattr(database.asyncpg, "connect", connect)
    monkeypatch.setattr(database.asyncio, "sleep", sleep)
    with pytest.raises(ConnectionResetError):
        await database._connect_with_retry("postgresql://localhost/test")
    assert calls == database._CONNECT_ATTEMPTS


@pytest.mark.anyio
async def test_authentication_error_is_not_retried(monkeypatch):
    calls = 0

    async def connect(*, dsn, timeout):
        nonlocal calls
        calls += 1
        raise asyncpg.InvalidPasswordError("invalid password")

    monkeypatch.setattr(database.asyncpg, "connect", connect)
    with pytest.raises(asyncpg.InvalidPasswordError):
        await database._connect_with_retry("postgresql://localhost/test")
    assert calls == 1


@pytest.mark.anyio
async def test_cancelled_connection_is_not_retried(monkeypatch):
    calls = 0

    async def connect(*, dsn, timeout):
        nonlocal calls
        calls += 1
        raise asyncio.CancelledError

    monkeypatch.setattr(database.asyncpg, "connect", connect)
    with pytest.raises(asyncio.CancelledError):
        await database._connect_with_retry("postgresql://localhost/test")
    assert calls == 1
