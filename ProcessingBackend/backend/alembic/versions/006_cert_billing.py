"""Add org-level cert billing policy, extend certificate_pins/billing_order_items,
add terminal cert history.

Revision ID: 006
Revises: 005
Create Date: 2026-08-18
"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = '006'
down_revision: Union[str, None] = '005'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    # --- org_billing_settings: org-level cert tariff policy ---
    op.add_column(
        'org_billing_settings',
        sa.Column(
            'cert_billing_mode', sa.String(20), server_default='none', nullable=False
        ),
    )
    op.add_column(
        'org_billing_settings', sa.Column('cert_price_minor', sa.BigInteger(), nullable=True)
    )
    op.add_column(
        'org_billing_settings',
        sa.Column(
            'tenant_pin_creation_enabled',
            sa.Boolean(),
            server_default='false',
            nullable=False,
        ),
    )
    op.add_column(
        'org_billing_settings',
        sa.Column(
            'cert_charge_primary_issue',
            sa.Boolean(),
            server_default='true',
            nullable=False,
        ),
    )
    op.add_column(
        'org_billing_settings',
        sa.Column(
            'cert_charge_reissue', sa.Boolean(), server_default='true', nullable=False
        ),
    )
    op.create_check_constraint(
        'ck_org_cert_price_non_negative',
        'org_billing_settings',
        'cert_price_minor IS NULL OR cert_price_minor >= 0',
    )
    op.create_check_constraint(
        'ck_org_cert_mode',
        'org_billing_settings',
        "cert_billing_mode IN ('none', 'per_operation')",
    )

    # --- certificate_pins: org/order linkage, TTL, audit ---
    op.add_column('certificate_pins', sa.Column('org_id', sa.Integer(), nullable=True))
    op.add_column(
        'certificate_pins',
        sa.Column('order_item_id', sa.Integer(), nullable=True),
    )
    op.add_column(
        'certificate_pins', sa.Column('created_by', sa.String(100), nullable=True)
    )
    op.add_column(
        'certificate_pins',
        sa.Column(
            'creation_source', sa.String(20), server_default='system', nullable=False
        ),
    )
    op.add_column(
        'certificate_pins',
        sa.Column(
            'payment_required', sa.Boolean(), server_default='false', nullable=False
        ),
    )
    op.add_column(
        'certificate_pins',
        sa.Column('expires_at', sa.DateTime(timezone=True), nullable=True),
    )
    op.create_foreign_key(
        'fk_cert_pins_order_item',
        'certificate_pins',
        'billing_order_items',
        ['order_item_id'],
        ['id'],
    )

    op.execute(
        "UPDATE certificate_pins cp SET org_id = t.org_id "
        "FROM terminals t WHERE t.id = cp.terminal_id AND cp.org_id IS NULL"
    )
    op.execute(
        "UPDATE certificate_pins SET expires_at = created_at + INTERVAL '24 hours' "
        "WHERE expires_at IS NULL"
    )

    op.alter_column('certificate_pins', 'org_id', nullable=False)
    op.alter_column('certificate_pins', 'expires_at', nullable=False)
    op.alter_column(
        'certificate_pins',
        'expires_at',
        server_default=sa.text("(now() + INTERVAL '24 hours')"),
    )

    op.create_check_constraint(
        'ck_certificate_pins_status',
        'certificate_pins',
        "status IN ('pending', 'used', 'expired', 'cancelled')",
    )
    op.create_check_constraint(
        'ck_certificate_pins_creation_source',
        'certificate_pins',
        "creation_source IN ('tenant', 'global_admin', 'system')",
    )

    op.create_index('idx_cert_pins_org', 'certificate_pins', ['org_id'])
    op.create_index(
        'idx_cert_pins_order_item', 'certificate_pins', ['order_item_id']
    )
    op.execute(
        "CREATE UNIQUE INDEX uq_cert_pins_one_pending_per_terminal "
        "ON certificate_pins(terminal_id) WHERE status = 'pending'"
    )

    # --- billing_order_items: cert operation + policy snapshot ---
    op.add_column(
        'billing_order_items',
        sa.Column('cert_policy_snapshot', sa.JSON(), nullable=True),
    )
    op.create_check_constraint(
        'ck_billing_order_items_operation',
        'billing_order_items',
        "operation IN ('renewal', 'reactivation', 'cert_pin')",
    )

    # --- terminals: operational cert expiry tracking ---
    op.add_column(
        'terminals',
        sa.Column('cert_not_valid_after', sa.DateTime(timezone=True), nullable=True),
    )

    # --- terminal_cert_history: append-only history of issued certificates ---
    op.create_table(
        'terminal_cert_history',
        sa.Column('id', sa.Integer(), autoincrement=True, nullable=False),
        sa.Column('terminal_id', sa.Integer(), nullable=False),
        sa.Column('cert_serial', sa.String(100), nullable=False),
        sa.Column('not_valid_after', sa.DateTime(timezone=True), nullable=True),
        sa.Column('pin_id', sa.Integer(), nullable=True),
        sa.Column('source', sa.String(20), server_default='setup', nullable=False),
        sa.Column(
            'issued_at',
            sa.DateTime(timezone=True),
            server_default=sa.text('now()'),
            nullable=False,
        ),
        sa.PrimaryKeyConstraint('id'),
        sa.ForeignKeyConstraint(['terminal_id'], ['terminals.id']),
        sa.ForeignKeyConstraint(['pin_id'], ['certificate_pins.id']),
    )
    op.create_index(
        'idx_terminal_cert_history_terminal', 'terminal_cert_history', ['terminal_id']
    )


def downgrade() -> None:
    op.drop_index('idx_terminal_cert_history_terminal', table_name='terminal_cert_history')
    op.drop_table('terminal_cert_history')

    op.drop_column('terminals', 'cert_not_valid_after')

    op.drop_constraint(
        'ck_billing_order_items_operation', 'billing_order_items', type_='check'
    )
    op.drop_column('billing_order_items', 'cert_policy_snapshot')

    op.execute("DROP INDEX IF EXISTS uq_cert_pins_one_pending_per_terminal")
    op.drop_index('idx_cert_pins_order_item', table_name='certificate_pins')
    op.drop_index('idx_cert_pins_org', table_name='certificate_pins')
    op.drop_constraint(
        'ck_certificate_pins_creation_source', 'certificate_pins', type_='check'
    )
    op.drop_constraint('ck_certificate_pins_status', 'certificate_pins', type_='check')
    op.drop_constraint('fk_cert_pins_order_item', 'certificate_pins', type_='foreignkey')
    op.drop_column('certificate_pins', 'expires_at')
    op.drop_column('certificate_pins', 'payment_required')
    op.drop_column('certificate_pins', 'creation_source')
    op.drop_column('certificate_pins', 'created_by')
    op.drop_column('certificate_pins', 'order_item_id')
    op.drop_column('certificate_pins', 'org_id')

    op.drop_constraint('ck_org_cert_mode', 'org_billing_settings', type_='check')
    op.drop_constraint(
        'ck_org_cert_price_non_negative', 'org_billing_settings', type_='check'
    )
    op.drop_column('org_billing_settings', 'cert_charge_reissue')
    op.drop_column('org_billing_settings', 'cert_charge_primary_issue')
    op.drop_column('org_billing_settings', 'tenant_pin_creation_enabled')
    op.drop_column('org_billing_settings', 'cert_price_minor')
    op.drop_column('org_billing_settings', 'cert_billing_mode')
