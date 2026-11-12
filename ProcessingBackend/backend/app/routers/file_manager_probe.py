"""User-triggered readiness. Each request uses a short FM DB transaction."""

import hashlib
import json
from datetime import UTC, datetime, timedelta
from typing import Literal
from uuid import UUID

from etranprocessing_db.file_manager import FileManagerAgent, FileManagerReadinessProbe
from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel, ConfigDict, Field, field_validator
from sqlalchemy import select
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.ext.asyncio import AsyncSession

from app.dependencies import get_current_terminal
from app.file_manager_database import get_fm_db
from app.models import Terminal
from app.routers.file_manager import fm_service_identity, no_store, trusted_actor
from app.schemas.file_manager import AgentHello
from app.services.file_manager_readiness import deployment_readiness
from app.services.terminal_certificate import verified_terminal_identity

router = APIRouter(dependencies=[Depends(no_store)], tags=["file-manager-probe"])
INTERNAL = "/api/internal/file-manager/v1"
AGENT = "/api/file-manager/v1/agent"
CAPABILITY = "fs.readiness_on_demand"


class ProbeCreate(BaseModel):
    model_config = ConfigDict(extra="forbid")
    request_id: UUID


class ProbeSuccess(AgentHello):
    request_id: UUID
    readiness_mode: Literal["on_demand_v1"]
    agent_version: str = Field(
        min_length=1, max_length=64, pattern=r"^[0-9A-Za-z.+_-]+$", strict=True
    )
    protocol_version: int = Field(ge=1, le=65535, strict=True)
    filesystem_ready: bool = Field(strict=True)

    @field_validator("capabilities", mode="before")
    @classmethod
    def unique_capabilities(cls, value):
        if (
            not isinstance(value, list)
            or any(not isinstance(item, str) for item in value)
            or len(set(value)) != len(value)
        ):
            raise ValueError("Unique capability strings required")
        return value


class ProbeFailure(BaseModel):
    model_config = ConfigDict(extra="forbid")
    request_id: UUID
    agent_instance_id: UUID
    error_code: str = Field(min_length=1, max_length=64, pattern=r"^[a-z0-9_]+$")


def probe_state(probe: FileManagerReadinessProbe, now: datetime) -> dict:
    result = dict(probe.result or {})
    if probe.result is None:
        result = {
            "state": "pending" if now < probe.expires_at else "expired",
            "available": False,
        }
    elif result.get("available") and (
        probe.consumed_by or probe.proof_until is None or now >= probe.proof_until
    ):
        result = {"state": "expired", "available": False}
    return {
        **result,
        "request_id": str(probe.request_id),
        "expires_at": probe.expires_at.isoformat(),
    }


@router.post(
    INTERNAL + "/devices/{device_id}/readiness-probes",
    dependencies=[Depends(fm_service_identity)],
)
async def create_probe(
    device_id: int,
    body: ProbeCreate,
    request: Request,
    db: AsyncSession = Depends(get_fm_db),
):
    actor = trusted_actor(request)
    terminal = await db.scalar(
        select(Terminal)
        .where(
            Terminal.device_id == device_id, Terminal.org_id == int(actor["X-Org-Id"])
        )
        .with_for_update(of=Terminal)
    )
    if terminal is None or not terminal.is_active or not terminal.cert_serial:
        raise HTTPException(403, "Active terminal required")
    now = datetime.now(UTC)
    probe = await db.get(FileManagerReadinessProbe, terminal.id)
    if probe and probe.request_id == body.request_id:
        if probe.actor != actor:
            raise HTTPException(404, "Probe not found")
        return probe_state(probe, now)
    if (
        probe
        and not probe.consumed_by
        and (probe.result is None or probe.result.get("available"))
        and now < (probe.proof_until or probe.expires_at)
    ):
        raise HTTPException(409, detail={"code": "fm_probe_busy"})
    values = {
        "request_id": body.request_id,
        "tenant_id": terminal.org_id,
        "actor": actor,
        "cert_serial": terminal.cert_serial,
        "expires_at": now + timedelta(seconds=10),
        "proof_until": None,
        "agent_instance_id": None,
        "response_hash": None,
        "result": None,
        "consumed_by": None,
    }
    if probe is None:
        probe = FileManagerReadinessProbe(terminal_id=terminal.id, **values)
        db.add(probe)
    else:
        for key, value in values.items():
            setattr(probe, key, value)
    await db.commit()
    return probe_state(probe, now)


@router.get(
    INTERNAL + "/readiness-probes/{identifier}",
    dependencies=[Depends(fm_service_identity)],
)
async def get_probe(
    identifier: UUID, request: Request, db: AsyncSession = Depends(get_fm_db)
):
    probe = await db.scalar(
        select(FileManagerReadinessProbe).where(
            FileManagerReadinessProbe.request_id == identifier
        )
    )
    if probe is None or probe.actor != trusted_actor(request):
        raise HTTPException(404, "Probe not found")
    return probe_state(probe, datetime.now(UTC))


@router.post(AGENT + "/readiness-probes/{identifier}/result")
async def probe_result(
    identifier: UUID,
    body: ProbeSuccess | ProbeFailure,
    request: Request,
    db: AsyncSession = Depends(get_fm_db),
):
    identity = verified_terminal_identity(request)
    terminal = await get_current_terminal(request, db)
    if not terminal.is_active or identity[0] != terminal.sn:
        raise HTTPException(403, "Active terminal identity required")
    if (
        body.request_id != identifier
        or str(body.agent_instance_id)
        != request.headers.get("X-FM-Agent-Instance-Id", "").lower()
    ):
        raise HTTPException(403, "Probe instance mismatch")
    # Terminal lock order matches create/consume; authentication committed its audit.
    terminal = await db.scalar(
        select(Terminal)
        .where(Terminal.id == terminal.id)
        .execution_options(populate_existing=True)
        .with_for_update(of=Terminal)
    )
    if (
        terminal is None
        or not terminal.is_active
        or terminal.sn != identity[0]
        or (terminal.cert_serial or "").upper().lstrip("0")
        != identity[1].upper().lstrip("0")
    ):
        raise HTTPException(401, "Certificate changed during probe")
    probe = await db.get(FileManagerReadinessProbe, terminal.id, with_for_update=True)
    now = datetime.now(UTC)
    if (
        probe is None
        or probe.request_id != identifier
        or probe.tenant_id != terminal.org_id
        or probe.cert_serial != terminal.cert_serial
    ):
        raise HTTPException(409, "Probe identity changed")
    digest = hashlib.sha256(
        json.dumps(body.model_dump(mode="json"), sort_keys=True).encode()
    ).hexdigest()
    if probe.response_hash:
        if (
            probe.response_hash != digest
            or probe.agent_instance_id != body.agent_instance_id
        ):
            raise HTTPException(409, "Probe response changed")
        return {"accepted": True}
    if now >= probe.expires_at or probe.consumed_by:
        raise HTTPException(409, "Probe expired")
    if isinstance(body, ProbeFailure):
        result = {
            "state": "unavailable",
            "available": False,
            "error_code": body.error_code,
        }
    else:
        if CAPABILITY not in body.capabilities:
            raise HTTPException(422, "On-demand capability required")
        values = {
            **body.model_dump(exclude={"request_id", "readiness_mode"}),
            "terminal_id": terminal.id,
            "tenant_id": terminal.org_id,
            "cert_serial": terminal.cert_serial,
            "last_seen_at": now,
        }
        statement = insert(FileManagerAgent).values(**values)
        await db.execute(
            statement.on_conflict_do_update(
                index_elements=[FileManagerAgent.terminal_id], set_=values
            )
        )
        result = deployment_readiness(
            terminal, FileManagerAgent(**values), now=now
        ).model_dump(mode="json")
    probe.agent_instance_id = body.agent_instance_id
    probe.response_hash = digest
    probe.result = result
    probe.proof_until = now + timedelta(seconds=15)
    await db.commit()
    return {"accepted": True}


async def consume_probe(
    db: AsyncSession,
    terminal: Terminal,
    actor: dict,
    identifier: UUID | None,
    session: UUID,
    agent: FileManagerAgent,
) -> None:
    if identifier is None:
        raise HTTPException(409, detail={"code": "fm_probe_required"})
    # Caller holds the terminal row lock; one proof can create only one session.
    probe = await db.get(FileManagerReadinessProbe, terminal.id, with_for_update=True)
    if (
        probe is None
        or probe.request_id != identifier
        or probe.actor != actor
        or probe.tenant_id != terminal.org_id
        or probe.cert_serial != terminal.cert_serial
        or probe.agent_instance_id != agent.agent_instance_id
        or not (probe.result or {}).get("available")
        or probe.proof_until is None
        or datetime.now(UTC) >= probe.proof_until
        or probe.consumed_by not in (None, session)
    ):
        raise HTTPException(409, detail={"code": "fm_probe_invalid"})
    probe.consumed_by = session
