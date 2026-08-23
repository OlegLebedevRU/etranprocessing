"""Regenerate terminal SNs to platform standard format and reset cert_serial for clean auto-bind.

Revision ID: 011
Revises: 010
Create Date: 2026-08-21
"""

import secrets
from collections.abc import Sequence
from datetime import UTC, datetime

import sqlalchemy as sa

from alembic import op

revision: str = "011"
down_revision: str | None = "010"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None

PLATFORM = "4"


def _generate_device_sn(device_id: int) -> str:
    device_part = f"{device_id:07d}"
    rand_first = str(secrets.randbelow(9) + 1)  # 1-9
    rand_rest = "".join(str(secrets.randbelow(10)) for _ in range(4))  # 4 digits
    random_part = rand_first + rand_rest
    date_part = datetime.now(UTC).strftime("%d%m%y")
    return f"a{PLATFORM}b{device_part}c{random_part}d{date_part}"


def upgrade() -> None:
    conn = op.get_bind()

    # 1. Fetch all terminals
    terminals = conn.execute(
        sa.text("SELECT id, device_id FROM terminals ORDER BY id")
    ).fetchall()

    used_sns: set[str] = set()

    for row in terminals:
        term_id = row[0]
        dev_id = row[1]

        # Generate unique SN
        sn = _generate_device_sn(dev_id)
        while sn in used_sns:
            sn = _generate_device_sn(dev_id)
        used_sns.add(sn)

        conn.execute(
            sa.text(
                "UPDATE terminals SET sn = :sn, cert_serial = NULL WHERE id = :term_id"
            ),
            {"sn": sn, "term_id": term_id},
        )

    # 2. Fix terminal_cert_discovery linkage where OU matches device_id and O matches org_id
    conn.execute(
        sa.text("""
            UPDATE terminal_cert_discovery d
            SET terminal_id = t.id
            FROM terminals t
            WHERE d.ou = t.device_id::text
              AND (d.o IS NULL OR d.o = t.org_id::text)
        """)
    )


def downgrade() -> None:
    pass
