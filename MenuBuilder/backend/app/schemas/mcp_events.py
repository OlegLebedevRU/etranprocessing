"""Bounded user-event history contract shared by the BFF and its IoT client."""

from datetime import UTC, datetime
from typing import Any, Self
from uuid import UUID

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


class UserEventFilters(BaseModel):
    model_config = ConfigDict(extra="forbid")

    correlation_id: UUID | None = None
    events_include: list[int] | None = Field(default=None, min_length=1, max_length=100)
    after_event_id: int | None = Field(default=None, gt=0)
    created_from: datetime | None = None
    created_to: datetime | None = None
    limit: int = Field(default=50, ge=1, le=100)

    @field_validator("correlation_id", mode="before")
    @classmethod
    def canonical_uuid(cls, value: Any) -> Any:
        if isinstance(value, str) and str(UUID(value)) != value.lower():
            raise ValueError("UUID must use 8-4-4-4-12 format")
        return value

    @field_validator("events_include")
    @classmethod
    def user_codes(cls, codes: list[int] | None) -> list[int] | None:
        if codes is not None and any(code < 900 or code > 999 for code in codes):
            raise ValueError("Only event codes 900–999 are allowed")
        return codes

    @field_validator("created_from", "created_to")
    @classmethod
    def utc_boundary(cls, value: datetime | None) -> datetime | None:
        if value is not None:
            if value.tzinfo is None or value.utcoffset() is None:
                raise ValueError("Time boundaries must include a timezone")
            return value.astimezone(UTC)
        return value

    @model_validator(mode="after")
    def ordered_interval(self) -> Self:
        if (
            self.created_from is not None
            and self.created_to is not None
            and self.created_from >= self.created_to
        ):
            raise ValueError("created_from must be earlier than created_to")
        return self


class UserEventRecord(BaseModel):
    id: int = Field(gt=0)
    device_id: int = Field(gt=0)
    event_type_code: int = Field(ge=900, le=999)
    dev_event_id: int
    created_at: datetime
    dev_timestamp: datetime
    payload: dict[str, Any] | None = None


class UserEventPage(BaseModel):
    items: list[UserEventRecord]
    next_after_event_id: int | None = Field(ge=1)
    has_more: bool
