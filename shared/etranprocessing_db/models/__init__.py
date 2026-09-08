from etranprocessing_db.models.auth import ApiToken, User, UserSession
from etranprocessing_db.models.billing import (
    BillingOrder,
    BillingOrderItem,
    CertificatePin,
)
from etranprocessing_db.models.catalog import CatalogCategory, CatalogItem
from etranprocessing_db.models.email import EmailLog, EmailVerification
from etranprocessing_db.models.menu import (
    Group,
    MenuVariant,
    MenuVariantSnapshot,
    Service,
    TerminalMenuBinding,
)
from etranprocessing_db.models.org import (
    Org,
    OrgBillingSettings,
    OrgStatus,
)
from etranprocessing_db.models.payment import (
    BalanceTerminalTsp,
    Payment,
    PaymentParam,
    Tsp,
    TspParameterCode,
)
from etranprocessing_db.models.telemetry import (
    GateGaugeRecord,
    TechGateRecord,
    TerminalGaugeState,
)
from etranprocessing_db.models.terminal import (
    License,
    Terminal,
    TerminalCertDiscovery,
    TerminalCertHistory,
    TerminalType,
)

# Alias for backward compatibility during migration
ServiceMenu = Service

__all__ = [
    "ApiToken",
    "BalanceTerminalTsp",
    "BillingOrder",
    "BillingOrderItem",
    "CatalogCategory",
    "CatalogItem",
    "CertificatePin",
    "EmailLog",
    "EmailVerification",
    "GateGaugeRecord",
    "Group",
    "License",
    "MenuVariant",
    "MenuVariantSnapshot",
    "Org",
    "OrgBillingSettings",
    "OrgStatus",
    "Payment",
    "PaymentParam",
    "Service",
    "ServiceMenu",
    "TechGateRecord",
    "Terminal",
    "TerminalCertDiscovery",
    "TerminalCertHistory",
    "TerminalGaugeState",
    "TerminalMenuBinding",
    "TerminalType",
    "Tsp",
    "TspParameterCode",
    "User",
    "UserSession",
]
