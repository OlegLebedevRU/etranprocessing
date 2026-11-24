"""Administrative MQTT quarantine decisions; observations remain ephemeral."""
import sqlalchemy as sa
from alembic import op

revision = "037"
down_revision = "036"
branch_labels = depends_on = None


def upgrade():
    table = "mqtt_quarantine_operations"
    op.create_table(table,
        sa.Column("id", sa.BigInteger(), primary_key=True),
        sa.Column("operation_id", sa.Uuid(), unique=True, nullable=False),
        sa.Column("ip", sa.String(45), nullable=False),
        sa.Column("actor", sa.String(160), nullable=False),
        sa.Column("reason", sa.String(500), nullable=False),
        sa.Column("ttl", sa.Integer(), nullable=False),
        sa.Column("expiry", sa.BigInteger(), nullable=False),
        sa.Column("preview_token", sa.String(64), nullable=False),
        sa.Column("acknowledged", sa.Boolean(), nullable=False),
        sa.Column("status", sa.String(32), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.func.now(), nullable=False))
    op.create_index("ix_mqtt_quarantine_operations_ip",table,["ip"])
    op.create_index("ix_mqtt_quarantine_operations_status",table,["status"])


def downgrade():
    op.drop_table("mqtt_quarantine_operations")
