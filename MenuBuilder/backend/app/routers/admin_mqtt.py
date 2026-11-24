"""Superuser access to MQTT observations and fixed-scope host operations."""

import asyncio
import ipaddress
import json
import time
from pathlib import Path
from typing import Literal
from uuid import UUID

import httpx
from etranprocessing_db.models.mqtt_quarantine import (
    MqttQuarantineOperation as Operation,
)
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy import select, update
from sqlalchemy.dialects.postgresql import insert

from app.database import async_session
from app.models import Org, Terminal
from app.routers.admin_identity_changes import require_identity_superuser

router = APIRouter(
    prefix="/api/admin/mqtt",
    tags=["admin-mqtt"],
    dependencies=[Depends(require_identity_superuser)],
)
OBS = Path("/run/mb-mqtt-observer/observations.json")
SOCKET = "/run/mb-mqtt-edge/control.sock"
inventory_cache: dict = {}
inventory_saved = 0.0
retry_task: asyncio.Task | None = None


class Decision(BaseModel):
    model_config = ConfigDict(extra="forbid")

    operation_id: UUID
    ip: str = Field(max_length=45)
    ttl: Literal[0, 900, 3600, 21600]
    reason: str = Field(min_length=3, max_length=500)
    token: str = Field(default="", max_length=64)
    acknowledged: bool = False


def canonical(value: str) -> str:
    try:
        addr = ipaddress.ip_address(value)
    except ValueError as exc:
        raise HTTPException(422, "invalid_address") from exc
    if "%" in value:
        raise HTTPException(422, "invalid_address")
    return str(addr)


async def edge(path: str, body: dict):
    try:
        async with httpx.AsyncClient(
            transport=httpx.AsyncHTTPTransport(uds=SOCKET), timeout=6
        ) as client:
            res = await client.post("http://localhost/" + path, json=body)
    except httpx.HTTPError as exc:
        raise HTTPException(503, "executor_unavailable") from exc
    if res.status_code != 200:
        raise HTTPException(res.status_code, res.json().get("error", "executor_error"))
    return res.json()


@router.get("/observations")
async def observations():
    global inventory_cache, inventory_saved
    try:
        data = json.loads(await asyncio.to_thread(OBS.read_text))
    except (OSError, ValueError) as exc:
        raise HTTPException(503, "observer_unavailable") from exc
    names = {r["login"] for r in data["rows"] if r["login"]}
    if time.monotonic() - inventory_saved > 60 or inventory_cache.keys() != names:
        async with async_session() as db:
            rows = await db.execute(
                select(Terminal.sn, Terminal.device_id, Terminal.org_id, Org.org_name)
                .join(Org, Org.org_id == Terminal.org_id)
                .where(Terminal.sn.in_(names))
            )
        inventory_cache = dict.fromkeys(names)
        for sn, device, org, name in rows:
            inventory_cache[sn] = {"device": device, "org": org, "owner": name}
        inventory_saved = time.monotonic()
    for r in data["rows"]:
        r["terminal"] = inventory_cache.get(r["login"])
    data.pop("success", None)
    data.pop("active", None)
    try:
        data.update(await edge("rules", {}))
    except HTTPException:
        data["active_rules"] = None
    return data


@router.get("/preview")
async def preview(ip: str):
    return await edge("preview", {"ip": canonical(ip)})


def serialize(op):
    return {
        "id": str(op.operation_id),
        "revision": op.id,
        "ip": op.ip,
        "actor": op.actor,
        "reason": op.reason,
        "ttl": op.ttl,
        "expiry": op.expiry,
        "status": op.status,
        "created_at": op.created_at.isoformat(),
    }


@router.get("/history")
async def history():
    async with async_session() as db:
        return [
            serialize(op)
            for op in await db.scalars(
                select(Operation).order_by(Operation.id.desc()).limit(100)
            )
        ]


async def deliver(revision: int):
    async with async_session() as db:
        op = await db.get(Operation, revision)
        if op is None or op.status != "pending":
            return
        data = {
            "revision": op.id,
            "ip": op.ip,
            "expiry": op.expiry,
            "token": op.preview_token,
            "ack": op.acknowledged,
        }
    try:
        await edge("command", data)
        result = "applied"
    except HTTPException as exc:
        if exc.status_code >= 500:
            return
        result = str(exc.detail)[:32]
    async with async_session.begin() as db:
        await db.execute(
            update(Operation)
            .where(Operation.id == revision, Operation.status == "pending")
            .values(status=result)
        )


async def recover():
    while True:
        async with async_session() as db:
            pending = (
                await db.scalars(
                    select(Operation.id)
                    .where(Operation.status == "pending")
                    .order_by(Operation.id)
                    .limit(100)
                )
            ).all()
        if not pending:
            return
        for revision in pending:
            await deliver(revision)
        await asyncio.sleep(10)


def schedule_recovery():
    global retry_task
    if retry_task is None or retry_task.done():
        retry_task = asyncio.create_task(recover())


@router.post("/operations")
async def decide(body: Decision, user: dict = Depends(require_identity_superuser)):
    ip = canonical(body.ip)
    actor = str(user.get("sub") or user.get("username") or user.get("user_id") or "")
    if not actor or len(actor) > 160:
        raise HTTPException(403, "missing_actor")
    reason = body.reason.strip()
    if len(reason) < 3:
        raise HTTPException(422, "reason_required")
    async with async_session.begin() as db:
        await db.execute(
            insert(Operation)
            .values(
                operation_id=body.operation_id,
                ip=ip,
                ttl=body.ttl,
                expiry=int(time.time()) + body.ttl if body.ttl else 0,
                actor=actor,
                reason=reason,
                preview_token=body.token,
                acknowledged=body.acknowledged,
                status="pending",
            )
            .on_conflict_do_nothing(index_elements=["operation_id"])
        )
        op = (
            await db.scalars(
                select(Operation).where(Operation.operation_id == body.operation_id)
            )
        ).one()
        if (op.ip, op.ttl, op.actor, op.reason, op.preview_token, op.acknowledged) != (
            ip,
            body.ttl,
            actor,
            reason,
            body.token,
            body.acknowledged,
        ):
            raise HTTPException(409, "operation_id_reused")
        revision = op.id
    await deliver(revision)
    async with async_session() as db:
        op = await db.get(Operation, revision)
        if op is None:
            raise HTTPException(503, "audit_unavailable")
        if op.status == "pending":
            schedule_recovery()
        return serialize(op)
