from pydantic import BaseModel


class GroupCreate(BaseModel):
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
    org_id: int
    number: int
    name: str
    parent_id: int | None

    model_config = {"from_attributes": True}


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
    group_id: int
    tsp_code: int
    name: str
    printname: str | None
    price: int
    protypenumber: int

    model_config = {"from_attributes": True}
