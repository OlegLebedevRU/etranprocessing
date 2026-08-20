from unittest.mock import AsyncMock

import pytest

from pin_server.pin_generator import generate_pin, generate_unique_pin


def test_generate_pin_format():
    pin = generate_pin()
    assert len(pin) == 6
    assert pin.isdigit()


def test_generate_pin_randomness():
    pins = {generate_pin() for _ in range(100)}
    # With 6 digits, collisions in 100 attempts are extremely unlikely
    assert len(pins) > 90


@pytest.mark.asyncio
async def test_generate_unique_pin():
    mock_db = AsyncMock()
    mock_db.fetchval = AsyncMock(
        side_effect=[True, False]
    )  # first exists, second doesn't

    pin = await generate_unique_pin(mock_db)
    assert len(pin) == 6
    assert pin.isdigit()
    assert mock_db.fetchval.call_count == 2


@pytest.mark.asyncio
async def test_generate_unique_pin_no_collision():
    mock_db = AsyncMock()
    mock_db.fetchval = AsyncMock(return_value=False)

    pin = await generate_unique_pin(mock_db)
    assert len(pin) == 6
    assert mock_db.fetchval.call_count == 1


@pytest.mark.asyncio
async def test_generate_unique_pin_exhausted():
    mock_db = AsyncMock()
    mock_db.fetchval = AsyncMock(return_value=True)  # always exists

    with pytest.raises(RuntimeError, match="Failed to generate unique PIN"):
        await generate_unique_pin(mock_db, max_retries=3)
