from __future__ import annotations


class FinError(Exception):
    """Base exception for all financial core subledger errors."""


class FinValidationError(FinError):
    """Raised when posting or account parameters violate schema, types, or rounding rules."""


class FinImbalanceError(FinValidationError):
    """Raised when double-entry rule sum(debit) == sum(credit) > 0 is violated."""


class FinDuplicatePostingError(FinError):
    """Raised when duplicate idempotency key is submitted with conflicting transaction parameters."""


class FinAccountNotFoundError(FinError):
    """Raised when a requested account does not exist."""


class FinTenantIsolationError(FinError):
    """Raised when transaction entries attempt to cross tenant account boundaries."""


class FinReversalError(FinError):
    """Raised when a transaction reversal cannot be executed (e.g. invalid target, already reversed)."""


class FinConcurrencyError(FinError):
    """Raised when optimistic version protection detects concurrent balance projection modification."""


class FinCorruptedProjectionError(FinError):
    """Raised when balance projection does not match sum of posted ledger entries."""


class FinImmutableError(FinError):
    """Raised when an attempt is made to mutate or delete posted ledger rows."""


class FinTariffNotFoundError(FinError):
    """Raised when an effective or requested tariff version cannot be found."""


class FinCycleNotFoundError(FinError):
    """Raised when a billing cycle cannot be found or resolved."""


class FinBillingProfileNotFoundError(FinError):
    """Raised when a billing profile cannot be found."""


class FinMeteringError(FinError):
    """Raised when metering aggregation or daily usage processing fails."""


class ArchiveError(FinError):
    """Base exception for archive coordination and retention errors."""


class ArchiveManifestValidationError(ArchiveError):
    """Raised when manifest violates schema, checksum, count, or hot retention constraints."""


class ArchiveConflictError(ArchiveError):
    """Raised when manifest with identical id has conflicting immutable fields."""


class ArchiveStorageUnavailableError(ArchiveError):
    """Raised when target volume or storage layout is inaccessible or unavailable."""


class NoFinancialPurgeViolationError(ArchiveError):
    """Raised when a purge operation targets protected financial subledger or session tables."""
