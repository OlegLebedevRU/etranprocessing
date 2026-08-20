"""Add api_tokens table for long-lived API tokens.

Revision ID: 004
Revises: 003
Create Date: 2026-08-17
"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = '004'
down_revision: Union[str, None] = '003'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.execute("""
        CREATE TABLE IF NOT EXISTS api_tokens (
            id SERIAL PRIMARY KEY,
            jti VARCHAR(36) UNIQUE NOT NULL,
            user_id VARCHAR(100) NOT NULL,
            name VARCHAR(200),
            expires_at TIMESTAMPTZ NOT NULL,
            created_at TIMESTAMPTZ DEFAULT now(),
            last_used_at TIMESTAMPTZ,
            revoked_at TIMESTAMPTZ
        )
    """)
    op.execute("CREATE INDEX IF NOT EXISTS idx_api_tokens_user ON api_tokens(user_id)")
    op.execute("CREATE INDEX IF NOT EXISTS idx_api_tokens_jti ON api_tokens(jti)")


def downgrade() -> None:
    op.drop_table('api_tokens')
