"""Durable projection of allowlisted IoT inventory events."""

import hashlib
import json
import time
from datetime import UTC, datetime, timedelta
from typing import Literal
from uuid import UUID

from etranprocessing_db.inventory import (
    TerminalToolsInventory,
    TerminalToolsInventoryInbox,
)
from fastapi import HTTPException
from pydantic import AwareDatetime, BaseModel, ConfigDict, Field
from sqlalchemy import delete, select, text
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import Terminal

_last_cleanup = 0.0


class Component(BaseModel):
    model_config = ConfigDict(extra="forbid")
    name: str = Field(pattern=r"^[a-zA-Z0-9_-]{1,64}$")
    file_version: str | None = Field(pattern=r"^\d{1,5}\.\d{1,5}\.\d{1,5}\.\d{1,5}$")
    arch: Literal["x86", "x64"]


class InventoryProperties(BaseModel):
    model_config = ConfigDict(extra="forbid", populate_by_name=True)
    sn: str = Field(alias="324", min_length=1, max_length=128)
    source: Literal["l4con"] = Field(alias="440")
    thumbprint: str = Field(alias="441", pattern=r"^[0-9a-fA-F]{40}$")
    serial: str = Field(alias="442", pattern=r"^[0-9a-fA-F]{1,40}$")
    expires_at: AwareDatetime = Field(alias="443")
    components: list[Component] = Field(alias="444", max_length=64)
    suite_version: str = Field(
        alias="445", min_length=1, max_length=64, pattern=r"^\S(?:.*\S)?$"
    )


class InventoryEvent(BaseModel):
    model_config = ConfigDict(extra="forbid", populate_by_name=True)
    source_event_id: int = Field(alias="101", gt=0, le=2147483647)
    published_at: AwareDatetime = Field(alias="102")
    event_code: Literal[75] = Field(alias="200")
    properties: list[InventoryProperties] = Field(
        alias="300", min_length=1, max_length=1
    )
    event_uuid: UUID = Field(alias="correlationData")


class InventoryDelivery(BaseModel):
    model_config = ConfigDict(extra="forbid")
    v: Literal[1] = 1
    tenant_id: int = Field(gt=0)
    device_id: int = Field(gt=0)
    sn: str = Field(min_length=1, max_length=128)
    source_revision: int = Field(gt=0)
    event: InventoryEvent


async def apply_inventory(db: AsyncSession, delivery: InventoryDelivery) -> str:
    global _last_cleanup
    event = delivery.event
    props = event.properties[0]
    if props.sn != delivery.sn:
        raise HTTPException(409, "inventory_identity_mismatch")
    if event.published_at > datetime.now(UTC) + timedelta(minutes=5):
        raise HTTPException(422, "inventory_clock_in_future")
    if len({item.name for item in props.components}) != len(props.components):
        raise HTTPException(422, "inventory_duplicate_component")
    # Terminal update and projection share the identity lock; no writes to cert_serial.
    terminal = await db.scalar(
        select(Terminal)
        .where(
            Terminal.org_id == delivery.tenant_id,
            Terminal.device_id == delivery.device_id,
            Terminal.sn == delivery.sn,
        )
        .with_for_update(of=Terminal)
    )
    if terminal is None:
        raise HTTPException(404, "inventory_terminal_not_found")
    await db.execute(
        text("SELECT pg_advisory_xact_lock(hashtextextended(:identity, 0))"),
        {"identity": f"inventory-event:{event.event_uuid}"},
    )
    digest = hashlib.sha256(
        json.dumps(
            delivery.model_dump(mode="json", exclude={"source_revision"}),
            sort_keys=True,
            separators=(",", ":"),
        ).encode()
    ).hexdigest()
    existing = await db.get(TerminalToolsInventoryInbox, event.event_uuid)
    if existing is not None:
        if existing.payload_sha256 != digest or existing.terminal_id != terminal.id:
            raise HTTPException(409, "inventory_event_conflict")
        return "duplicate"
    snapshot = await db.get(TerminalToolsInventory, terminal.id)
    if snapshot is not None and snapshot.event_uuid == event.event_uuid:
        if snapshot.payload_sha256 != digest:
            raise HTTPException(409, "inventory_event_conflict")
        return "duplicate"
    same_identity = (
        snapshot is not None
        and snapshot.tenant_id == terminal.org_id
        and snapshot.sn == terminal.sn
    )
    if (
        same_identity
        and snapshot is not None
        and delivery.source_revision == snapshot.source_revision
    ):
        raise HTTPException(409, "inventory_revision_conflict")
    older = bool(
        same_identity
        and snapshot is not None
        and (
            delivery.source_revision < snapshot.source_revision
            or event.published_at < snapshot.published_at
        )
    )
    now = datetime.now(UTC)
    outcome = "older" if older else "applied"
    db.add(
        TerminalToolsInventoryInbox(
            event_uuid=event.event_uuid,
            tenant_id=terminal.org_id,
            terminal_id=terminal.id,
            payload_sha256=digest,
            source_revision=delivery.source_revision,
            outcome=outcome,
            received_at=now,
        )
    )
    if not older:
        if snapshot is None:
            snapshot = TerminalToolsInventory(terminal_id=terminal.id)
            db.add(snapshot)
        snapshot.tenant_id = terminal.org_id
        snapshot.sn = terminal.sn
        snapshot.payload_sha256 = digest
        snapshot.cert_thumbprint = props.thumbprint
        snapshot.cert_serial = props.serial
        snapshot.cert_expires_at = props.expires_at
        snapshot.suite_version = props.suite_version
        snapshot.components = [item.model_dump() for item in props.components]
        snapshot.event_uuid = event.event_uuid
        snapshot.source_revision = delivery.source_revision
        snapshot.published_at = event.published_at
        snapshot.received_at = now
    cleanup = time.monotonic() - _last_cleanup > 1800
    if cleanup:
        old = (
            select(TerminalToolsInventoryInbox.event_uuid)
            .where(TerminalToolsInventoryInbox.received_at < now - timedelta(days=120))
            .order_by(TerminalToolsInventoryInbox.received_at)
            .limit(1000)
        )
        await db.execute(
            delete(TerminalToolsInventoryInbox).where(
                TerminalToolsInventoryInbox.event_uuid.in_(old)
            )
        )
    await db.commit()
    if cleanup:
        _last_cleanup = time.monotonic()
    return outcome
