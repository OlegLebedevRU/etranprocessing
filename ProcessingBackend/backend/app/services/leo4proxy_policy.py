"""Compute transport permissions without storing a separate terminal flag."""

from app.models import Terminal
from app.schemas.leo4proxy import Leo4ProxyPolicy


def get_leo4proxy_policy(terminal: Terminal) -> Leo4ProxyPolicy:
    return Leo4ProxyPolicy(
        sn=terminal.sn,
        mqtt_rtp_allowed=terminal.is_active,
        outgoing_https_allowed=True,
        stop_facts=[] if terminal.is_active else ["terminal_inactive"],
    )
