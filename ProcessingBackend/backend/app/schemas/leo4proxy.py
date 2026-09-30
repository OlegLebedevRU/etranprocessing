"""Terminal transport policy consumed by leo4proxy."""

from typing import Literal

from pydantic import BaseModel


class Leo4ProxyPolicy(BaseModel):
    v: Literal[1] = 1
    sn: str
    mqtt_rtp_allowed: bool
    outgoing_https_allowed: Literal[True] = True
    stop_facts: list[Literal["terminal_inactive"]]
