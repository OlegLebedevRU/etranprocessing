from datetime import datetime

from pydantic import BaseModel

# --- MenuVariant ---


class MenuVariantCreate(BaseModel):
    name: str
    org_id: int | None = None


class MenuVariantRead(BaseModel):
    id: int
    org_id: int
    name: str
    created_at: datetime

    model_config = {"from_attributes": True}


class MenuVariantDuplicate(BaseModel):
    source_variant_id: int
    new_name: str | None = None  # auto-generated if omitted


# --- Group ---


class GroupCreate(BaseModel):
    menu_variant_id: int
    name: str
    org_id: int | None = None
    parent_id: int | None = None
    number: int = 0


class GroupUpdate(BaseModel):
    name: str | None = None
    parent_id: int | None = None
    number: int | None = None


class GroupRead(BaseModel):
    id: int
    menu_variant_id: int
    org_id: int
    number: int
    name: str
    parent_id: int | None

    model_config = {"from_attributes": True}


# --- Service ---


class ServiceCreate(BaseModel):
    group_id: int
    name: str
    tsp_code: int = 0
    printname: str | None = None
    price: int = 0
    protypenumber: int = 0


class ServiceUpdate(BaseModel):
    name: str | None = None
    printname: str | None = None
    price: int | None = None
    protypenumber: int | None = None


class ServiceRead(BaseModel):
    id: int
    menu_variant_id: int
    group_id: int
    tsp_code: int
    name: str
    printname: str | None
    price: int
    protypenumber: int

    model_config = {"from_attributes": True}


# --- Terminal binding ---


class TerminalBindingRead(BaseModel):
    id: int
    device_id: int
    menu_variant_id: int
    menu_variant_name: str | None = None
    created_at: datetime

    model_config = {"from_attributes": True}


class TerminalBindingCreate(BaseModel):
    device_id: int
    menu_variant_id: int


class TerminalInfo(BaseModel):
    terminal_id: int
    device_id: int
    sn: str
    org_id: int
    is_active: bool
    binding_id: int | None = None
    menu_variant_id: int | None = None
    menu_variant_name: str | None = None
    address: str | None = None
    note: str | None = None
    terminal_type_id: int = 0
    terminal_type_name: str | None = None
    created_at: datetime | None = None


# --- Admin: Organizations & Licensing ---


class AdminOrgRead(BaseModel):
    org_id: int
    org_name: str
    name: str
    status: int
    is_active: bool
    created_at: datetime | None = None
    updated_at: datetime | None = None
    monthly_price_minor: int = 100_000
    currency: str = "RUB"
    cert_billing_mode: str = "none"
    cert_price_minor: int | None = None
    tenant_pin_creation_enabled: bool = False
    cert_charge_primary_issue: bool = True
    cert_charge_reissue: bool = True

    model_config = {"from_attributes": True}


class AdminOrgCreate(BaseModel):
    org_id: int | None = None  # None -> auto-generated
    org_name: str
    name: str
    status: int = 1
    is_active: bool = True
    monthly_price_minor: int = 100_000
    currency: str = "RUB"
    cert_billing_mode: str = "none"
    cert_price_minor: int | None = None
    tenant_pin_creation_enabled: bool = False
    cert_charge_primary_issue: bool = True
    cert_charge_reissue: bool = True


class AdminOrgUpdate(BaseModel):
    org_name: str | None = None
    name: str | None = None
    status: int | None = None
    is_active: bool | None = None
    monthly_price_minor: int | None = None
    currency: str | None = None
    cert_billing_mode: str | None = None
    cert_price_minor: int | None = None
    tenant_pin_creation_enabled: bool | None = None
    cert_charge_primary_issue: bool | None = None
    cert_charge_reissue: bool | None = None


# --- Admin: Terminal Types & Terminals ---


class TerminalTypeRead(BaseModel):
    id: int
    name: str
    description: str | None = None

    model_config = {"from_attributes": True}


class AdminTerminalRead(BaseModel):
    id: int
    device_id: int
    sn: str
    cert_serial: str | None = None
    cert_not_valid_after: datetime | None = None
    org_id: int
    org_name: str | None = None
    is_active: bool
    address: str | None = None
    note: str | None = None
    terminal_type_id: int = 0
    terminal_type_name: str | None = None
    created_at: datetime | None = None
    updated_at: datetime | None = None
    license_id: int | None = None
    license_expires_at: datetime | None = None
    license_is_active: bool | None = None
    license_balance: int | None = None
    license_type: str | None = None
    billing_period_months: int | None = None
    monthly_price_override_minor: int | None = None
    renewal_enabled: bool | None = None
    deactivation_requested_at: datetime | None = None
    pending_pin: str | None = None
    pin_expires_at: datetime | None = None
    pin_status: str | None = None

    model_config = {"from_attributes": True}


class AdminTerminalListResponse(BaseModel):
    total: int
    page: int
    page_size: int
    items: list[AdminTerminalRead]


class AdminTerminalCreate(BaseModel):
    device_id: int
    org_id: int
    terminal_type_id: int = 0
    address: str | None = None
    note: str | None = None
    is_active: bool = True
    license_expires_at: datetime | None = None
    billing_period_months: int = 1
    renewal_enabled: bool = True


class AdminTerminalUpdate(BaseModel):
    org_id: int | None = None
    terminal_type_id: int | None = None
    address: str | None = None
    note: str | None = None
    is_active: bool | None = None
    license_expires_at: datetime | None = None
    license_is_active: bool | None = None
    renewal_enabled: bool | None = None
    billing_period_months: int | None = None
    monthly_price_override_minor: int | None = None


class GeneratePinResponse(BaseModel):
    pin: str
    terminal_id: int
    device_id: int
    expires_at: datetime


class SetLicenseRequest(BaseModel):
    expires_at: datetime
    is_active: bool = True
    renewal_enabled: bool = True


class SetLicenseResponse(BaseModel):
    terminal_id: int
    license_id: int
    expires_at: datetime
    is_active: bool
    renewal_enabled: bool


class SetStatusRequest(BaseModel):
    is_active: bool


class NextDeviceIdResponse(BaseModel):
    next_device_id: int
