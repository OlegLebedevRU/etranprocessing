"""PIN utility functions for certificate management in ProcessingBackend."""


def mask_pin(pin: str) -> str:
    """Mask a PIN for safe logging/audit, e.g. '773773' -> '***773'."""
    if not pin:
        return pin
    tail = pin[-3:] if len(pin) > 3 else pin
    return f"***{tail}"
