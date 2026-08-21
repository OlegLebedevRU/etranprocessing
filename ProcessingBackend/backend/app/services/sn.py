"""Terminal Serial Number (SN) generation utilities."""

import secrets
from datetime import UTC, datetime

PLATFORM = "4"


def generate_device_sn(device_id: int) -> str:
    """Generate automatic serial number (sn) according to platform standard:
    a4b<7-digit device_id>c<5-digit random>d<DDMMYY>
    """
    device_part = f"{device_id:07d}"
    rand_first = str(secrets.randbelow(9) + 1)  # 1-9
    rand_rest = "".join(str(secrets.randbelow(10)) for _ in range(4))  # 4 digits
    random_part = rand_first + rand_rest
    date_part = datetime.now(UTC).strftime("%d%m%y")
    return f"a{PLATFORM}b{device_part}c{random_part}d{date_part}"
