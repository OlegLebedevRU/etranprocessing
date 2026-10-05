from datetime import datetime
from typing import Literal
from uuid import UUID

from pydantic import BaseModel, ConfigDict, Field, field_validator


class AgentHello(BaseModel):
    model_config = ConfigDict(extra="forbid")

    agent_instance_id: UUID
    agent_version: str = Field(
        min_length=1, max_length=64, pattern=r"^[0-9A-Za-z.+_-]+$"
    )
    protocol_version: int = Field(ge=1, le=65535)
    capabilities: list[str] = Field(max_length=32)
    filesystem_ready: bool

    @field_validator("capabilities")
    @classmethod
    def validate_capabilities(cls, value: list[str]) -> list[str]:
        if any(not item.startswith("fs.") or len(item) > 64 for item in value):
            raise ValueError("Invalid file-manager capability")
        return sorted(set(value))


class AgentReadiness(BaseModel):
    state: Literal[
        "ready",
        "not_registered",
        "offline",
        "incompatible",
        "filesystem_unavailable",
        "disabled",
        "certificate_changed",
        "storage_unavailable",
        "policy_unconfigured",
    ]
    compatible: bool = False
    available: bool = False
    write_available: bool = False
    agent_version: str | None = None
    protocol_version: int | None = None
    capabilities: list[str] = Field(default_factory=list)
    missing_capabilities: list[str] = Field(default_factory=list)
    last_seen_at: datetime | None = None
    valid_until: datetime | None = None
    server_time: datetime


class SessionRegistration(BaseModel):
    model_config = ConfigDict(extra="forbid")
    lease_id: UUID


class OperationCreate(BaseModel):
    model_config = ConfigDict(extra="forbid")
    id: UUID
    lease_id: UUID
    kind: Literal["upload", "download"]
    path: str = Field(max_length=1024)


class AgentResult(BaseModel):
    model_config = ConfigDict(extra="forbid")
    state: Literal["active", "completed", "failed", "cancelled"]
    grant_id: UUID | None = None
    roots: list[str] | None = Field(default=None, max_length=32)
    error_code: str | None = Field(default=None, max_length=64, pattern=r"^[a-z0-9_]+$")


class SourceManifest(BaseModel):
    model_config = ConfigDict(extra="forbid")
    size_bytes: int = Field(ge=0, le=64 * 1024 * 1024)
    sha256: str = Field(pattern=r"^[0-9a-f]{64}$")


class CommitReceipt(SourceManifest):
    state: Literal["completed", "failed"] = "completed"
