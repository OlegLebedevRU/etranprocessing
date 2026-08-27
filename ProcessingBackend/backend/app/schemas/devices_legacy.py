"""Schemas for legacy device companion certificate endpoints."""

from pydantic import BaseModel, ConfigDict, Field


class LegacyPfxResponse(BaseModel):
    """PFX companion certificate response schema for /map_legacy_crt/."""

    model_config = ConfigDict(extra="ignore")

    pfx: str = Field(..., description="Base64-encoded PKCS#12 (PFX) bundle")
    pfx_password: str = Field(..., description="Password protecting the PFX bundle")
    not_valid_before: str = Field(
        ..., description="Certificate validity start time (YYYY-MM-DD HH:MM:SS)"
    )
    not_valid_after: str = Field(
        ..., description="Certificate expiration time (YYYY-MM-DD HH:MM:SS)"
    )
    valid_days: int = Field(..., description="Validity duration in days")
    sn: str = Field(..., description="Terminal serial number (Subject CN)")
    device_id: int = Field(..., description="Device ID / OU numerical code")
    days_left: int = Field(..., description="Days remaining until certificate expires")
