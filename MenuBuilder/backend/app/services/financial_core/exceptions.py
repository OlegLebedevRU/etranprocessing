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
