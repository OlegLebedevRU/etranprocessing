"""Opt-in L4Desk schema; import only after removing consumer-local IoT declarations."""

from etranprocessing_db.models.finance import (
    FinAccount,
    FinArchiveBatch,
    FinBalanceProjection,
    FinBillingCycle,
    FinBillingProfile,
    FinLedgerEntry,
    FinLedgerTransaction,
    FinManualPayment,
    FinNotificationDelivery,
    FinPayment,
    FinReconciliationRun,
    FinTariffVersion,
    FinTerminalMonthlyCharge,
    FinUsageDaily,
)
from etranprocessing_db.models.iot import (
    IotConsumerCheckpoint,
    IotEventInbox,
    IotEventQuarantine,
)
from etranprocessing_db.models.l4desk import (
    L4DeskAuditEvent,
    L4DeskMembership,
    L4DeskRegistration,
    L4DeskRemoteSession,
    L4DeskTenantProfile,
    L4DeskTerminal,
)

__all__ = [
    "FinAccount",
    "FinArchiveBatch",
    "FinBalanceProjection",
    "FinBillingCycle",
    "FinBillingProfile",
    "FinLedgerEntry",
    "FinLedgerTransaction",
    "FinManualPayment",
    "FinNotificationDelivery",
    "FinPayment",
    "FinReconciliationRun",
    "FinTariffVersion",
    "FinTerminalMonthlyCharge",
    "FinUsageDaily",
    "IotConsumerCheckpoint",
    "IotEventInbox",
    "IotEventQuarantine",
    "L4DeskAuditEvent",
    "L4DeskMembership",
    "L4DeskRegistration",
    "L4DeskRemoteSession",
    "L4DeskTenantProfile",
    "L4DeskTerminal",
]
