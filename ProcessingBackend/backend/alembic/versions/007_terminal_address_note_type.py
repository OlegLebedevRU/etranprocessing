"""Add terminal_types dictionary and address, note, terminal_type_id to terminals.

Revision ID: 007
Revises: 006
Create Date: 2026-08-20
"""
from collections.abc import Sequence

from alembic import op
import sqlalchemy as sa


revision: str = '007'
down_revision: str | None = '006'
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    # 1. Create terminal_types table
    terminal_types_table = op.create_table(
        'terminal_types',
        sa.Column('id', sa.Integer(), primary_key=True, autoincrement=False),
        sa.Column('name', sa.String(100), nullable=False),
        sa.Column('description', sa.String(255), nullable=True),
        sa.Column(
            'created_at',
            sa.DateTime(timezone=True),
            server_default=sa.text('now()'),
            nullable=False,
        ),
    )

    # 2. Seed terminal_types data
    op.bulk_insert(
        terminal_types_table,
        [
            {'id': 0, 'name': 'Стандартный', 'description': 'По умолчанию'},
            {'id': 1, 'name': 'Платежный терминал', 'description': 'Терминал самообслуживания'},
            {'id': 2, 'name': 'Касса', 'description': 'Кассовый аппарат / онлайн-касса'},
            {'id': 3, 'name': 'Вендинг', 'description': 'Торговый / вендинговый автомат'},
            {'id': 4, 'name': 'Банкомат', 'description': 'Банкомат / АТМ'},
            {'id': 5, 'name': 'Мобильный терминал', 'description': 'mPOS / Мобильный терминал'},
        ],
    )

    # 3. Add columns to terminals
    op.add_column('terminals', sa.Column('address', sa.String(500), nullable=True))
    op.add_column('terminals', sa.Column('note', sa.String(500), nullable=True))
    op.add_column(
        'terminals',
        sa.Column(
            'terminal_type_id',
            sa.Integer(),
            server_default='0',
            nullable=False,
        ),
    )

    # 4. Foreign key and index
    op.create_foreign_key(
        'fk_terminals_terminal_type_id',
        'terminals',
        'terminal_types',
        ['terminal_type_id'],
        ['id'],
    )
    op.create_index(
        'idx_terminals_terminal_type_id',
        'terminals',
        ['terminal_type_id'],
    )


def downgrade() -> None:
    op.drop_index('idx_terminals_terminal_type_id', table_name='terminals')
    op.drop_constraint('fk_terminals_terminal_type_id', 'terminals', type_='foreignkey')
    op.drop_column('terminals', 'terminal_type_id')
    op.drop_column('terminals', 'note')
    op.drop_column('terminals', 'address')
    op.drop_table('terminal_types')
