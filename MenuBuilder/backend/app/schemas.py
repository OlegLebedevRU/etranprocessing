from datetime import datetime

from pydantic import BaseModel


# --- MenuVariant ---

class MenuVariantCreate(BaseModel):
    name: str


class MenuVariantRead(BaseModel):
    id: int
    name: str
    created_at: datetime

    model_config = {"from_attributes": True}


class MenuVariantDuplicate(BaseModel):
    source_variant_id: int
    new_name: str | None = None  # auto-generated if omitted


# --- Group ---

class GroupCreate(BaseModel):
    menu_variant_id: int
    org_id: int
    name: str
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
    tsp_code: int
    name: str
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
