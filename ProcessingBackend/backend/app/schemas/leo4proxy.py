"""Terminal transport policy consumed by leo4proxy."""

import re
from ipaddress import IPv4Address
from typing import Literal

from pydantic import BaseModel, Field, field_validator


class Leo4ProxyEndpoint(BaseModel):
    host: str
    port: int = Field(ge=1, le=65535)
    fallback_ips: list[str] = Field(default_factory=list, max_length=4)

    @field_validator("host")
    @classmethod
    def validate_host(cls, value: str) -> str:
        host = value.rstrip(".").lower()
        if len(host) > 253 or not all(
            re.fullmatch(r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?", label)
            for label in host.split(".")
        ):
            raise ValueError("Invalid endpoint DNS name")
        return host

    @field_validator("fallback_ips")
    @classmethod
    def validate_ips(cls, values: list[str]) -> list[str]:
        addresses = [IPv4Address(value) for value in values]
        if any(not address.is_global or address.is_multicast for address in addresses):
            raise ValueError("Fallback must be a public unicast IPv4 address")
        return list(dict.fromkeys(str(address) for address in addresses))


class Leo4ProxyPolicy(BaseModel):
    v: Literal[1] = 1
    sn: str
    mqtt_rtp_allowed: bool
    outgoing_https_allowed: Literal[True] = True
    stop_facts: list[Literal["terminal_inactive"]]
    endpoints: (
        dict[Literal["mqtt", "https", "l4rtp", "l4stream"], Leo4ProxyEndpoint] | None
    ) = None
    endpoints_ttl_seconds: int | None = Field(default=None, ge=300, le=604800)
