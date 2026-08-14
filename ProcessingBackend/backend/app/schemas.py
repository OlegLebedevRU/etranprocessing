from pydantic import BaseModel
from datetime import datetime


class TerminalRead(BaseModel):
    id: int
    device_id: int
    sn: str
    cert_serial: str | None
    org_id: int
    is_active: bool

    model_config = {"from_attributes": True}


class LicenseRead(BaseModel):
    id: int
    terminal_id: int
    org_id: int
    license_type: str
    expires_at: datetime
    balance: int
    is_active: bool

    model_config = {"from_attributes": True}


class OrgStatusRead(BaseModel):
    id: int
    org_id: int
    status: str

    model_config = {"from_attributes": True}
