import secrets
import string

from pin_server.db import Database


def generate_pin() -> str:
    """Generate a cryptographically random 6-digit numeric PIN."""
    return "".join(secrets.choice(string.digits) for _ in range(6))


async def generate_unique_pin(db: Database, max_retries: int = 5) -> str:
    """Generate a PIN that doesn't exist in the database."""
    for _ in range(max_retries):
        pin = generate_pin()
        exists = await db.fetchval(
            "SELECT EXISTS(SELECT 1 FROM certificate_pins WHERE pin = $1)", pin
        )
        if not exists:
            return pin
    raise RuntimeError("Failed to generate unique PIN after retries")
