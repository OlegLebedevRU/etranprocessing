import json
import secrets
from datetime import UTC, datetime
from uuid import UUID, uuid4

from etranprocessing_db.file_manager import FileManagerAgent, FileManagerOperation
from fastapi import APIRouter, Depends, Header, HTTPException, Request, Response
from sqlalchemy import select, update
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.dependencies import extract_cert_issuer, get_current_terminal
from app.models import Terminal
from app.schemas.file_manager import (
    AgentHello,
    AgentReadiness,
    AgentResult,
    CommitReceipt,
    OperationCreate,
    SessionRegistration,
    SourceManifest,
)
from app.services import file_manager_storage as storage
from app.services.file_manager_control import lease_request, operation_actor
from app.services.file_manager_readiness import deployment_readiness, evaluate_readiness
from app.services.terminal_certificate import verified_terminal_identity


def no_store(response: Response) -> None:
    response.headers["Cache-Control"] = "no-store"


agent_router = APIRouter(
    prefix="/api/file-manager/v1/agent",
    tags=["file-manager-agent"],
    dependencies=[Depends(no_store)],
)
internal_router = APIRouter(
    prefix="/api/internal/file-manager/v1",
    tags=["file-manager-internal"],
    dependencies=[Depends(no_store)],
)


async def fm_service_identity(
    x_file_manager_service_key: str = Header(default=""),
) -> None:
    if not settings.file_manager_service_key or not secrets.compare_digest(
        x_file_manager_service_key, settings.file_manager_service_key
    ):
        raise HTTPException(401, "File-manager service authentication required")


async def fm_terminal(request: Request, db: AsyncSession = Depends(get_db)) -> Terminal:
    identity = verified_terminal_identity(request)
    terminal = await get_current_terminal(request, db)
    if not terminal.is_active or identity[0] != terminal.sn:
        raise HTTPException(403, "Active terminal identity required")
    try:
        instance = UUID(request.headers.get("X-FM-Agent-Instance-Id", ""))
    except ValueError as exc:
        raise HTTPException(403, "Agent instance required") from exc
    if not request.url.path.endswith("/hello"):
        agent = await db.get(FileManagerAgent, terminal.id)
        if agent is None or agent.agent_instance_id != instance:
            raise HTTPException(409, detail={"code": "fm_agent_generation_changed"})
    return terminal


@agent_router.post("/hello", response_model=AgentReadiness)
async def hello(
    body: AgentHello,
    request: Request,
    response: Response,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
) -> AgentReadiness:
    if (
        str(body.agent_instance_id)
        != request.headers.get("X-FM-Agent-Instance-Id", "").lower()
    ):
        raise HTTPException(403, "Agent instance mismatch")
    if "iot.leo4.ru" not in extract_cert_issuer(request).lower():
        raise HTTPException(403, detail={"code": "fm_certificate_upgrade_required"})
    if not terminal.is_active:
        raise HTTPException(403, detail={"code": "terminal_inactive"})
    now = datetime.now(UTC)
    values = {
        **body.model_dump(),
        "terminal_id": terminal.id,
        "tenant_id": terminal.org_id,
        "cert_serial": terminal.cert_serial,
        "last_seen_at": now,
    }
    statement = insert(FileManagerAgent).values(**values)
    statement = statement.on_conflict_do_update(
        index_elements=[FileManagerAgent.terminal_id], set_=values
    )
    await db.execute(statement)
    await db.commit()
    response.headers["Cache-Control"] = "no-store"
    return evaluate_readiness(terminal, FileManagerAgent(**values), now=now)


@internal_router.get(
    "/devices/{device_id}/readiness",
    response_model=AgentReadiness,
    dependencies=[Depends(fm_service_identity)],
)
async def readiness(
    device_id: int,
    response: Response,
    x_org_id: int = Header(gt=0),
    db: AsyncSession = Depends(get_db),
) -> AgentReadiness:
    terminal = await db.scalar(
        select(Terminal).where(
            Terminal.device_id == device_id, Terminal.org_id == x_org_id
        )
    )
    if terminal is None:
        raise HTTPException(404, "Device not found")
    agent = await db.get(FileManagerAgent, terminal.id)
    response.headers["Cache-Control"] = "no-store"
    return deployment_readiness(terminal, agent, now=datetime.now(UTC))


def trusted_actor(request: Request) -> dict[str, str]:
    names = ("X-Org-Id", "X-User-Id", "X-Session-Id", "X-Role")
    actor = {name: request.headers.get(name, "") for name in names}
    if any(not value or len(value) > 128 for value in actor.values()):
        raise HTTPException(403, "Actor required")
    if not actor["X-Org-Id"].isdigit() or int(actor["X-Org-Id"]) <= 0:
        raise HTTPException(403, "Tenant required")
    if actor["X-Role"] not in ("superuser", "admin", "user", "l4desk_owner"):
        raise HTTPException(403, "FM permission required")
    return actor


async def owned_operation(
    identifier: UUID, request: Request, db: AsyncSession
) -> FileManagerOperation:
    operation = await db.get(FileManagerOperation, identifier, with_for_update=True)
    if operation is None or operation_actor(operation) != trusted_actor(request):
        raise HTTPException(404, "Operation not found")
    return operation


def public_operation(operation: FileManagerOperation) -> dict:
    return {
        "id": str(operation.id),
        "lease_id": str(operation.lease_id),
        "kind": operation.kind,
        "state": operation.state,
        "path": operation.path,
        "error_code": operation.error_code,
        "expires_at": operation.expires_at.isoformat(),
        "entries": operation.data.get("entries", []),
        "offset": operation.data.get("offset", 0),
        "has_more": operation.data.get("has_more", False),
        "roots": settings.file_manager_read_roots
        if operation.kind == "session"
        else [],
        "applied_expires_at": operation.data.get("applied_expires_at"),
        "size_bytes": operation.size_bytes,
        "sha256": operation.sha256,
    }


@internal_router.post(
    "/devices/{device_id}/sessions", dependencies=[Depends(fm_service_identity)]
)
async def register_session(
    device_id: int,
    body: SessionRegistration,
    request: Request,
    db: AsyncSession = Depends(get_db),
):
    actor = trusted_actor(request)
    lease = await lease_request(body.lease_id, actor)
    if lease.get("device_id") != device_id:
        raise HTTPException(403, "Lease device mismatch")
    terminal = await db.scalar(
        select(Terminal).where(
            Terminal.device_id == device_id, Terminal.org_id == int(actor["X-Org-Id"])
        )
    )
    if terminal is None:
        raise HTTPException(404, "Device not found")
    agent = await db.get(FileManagerAgent, terminal.id)
    readiness = deployment_readiness(terminal, agent, now=datetime.now(UTC))
    if not readiness.available:
        raise HTTPException(
            409, detail={"code": "fm_agent_unavailable", "state": readiness.state}
        )
    existing = await db.get(FileManagerOperation, body.lease_id)
    if existing:
        if operation_actor(existing) != actor or existing.terminal_id != terminal.id:
            raise HTTPException(409, "Session identity mismatch")
        return public_operation(existing)
    # Acquiring a new shared slot proves the previous worker's deadline/drain
    # has elapsed. A commit with a lost receipt stays unresolved for reconciliation.
    await db.execute(
        update(FileManagerOperation)
        .where(
            FileManagerOperation.terminal_id == terminal.id,
            FileManagerOperation.lease_id != body.lease_id,
            FileManagerOperation.kind.in_(("upload", "download")),
            FileManagerOperation.state.in_(
                ("created", "running", "verifying", "cancelling")
            ),
        )
        .values(
            state="failed", error_code="fm_lease_expired", updated_at=datetime.now(UTC)
        )
    )
    now = datetime.now(UTC)
    operation = FileManagerOperation(
        id=body.lease_id,
        lease_id=body.lease_id,
        terminal_id=terminal.id,
        tenant_id=terminal.org_id,
        owner_user_id=actor["X-User-Id"],
        owner_session_id=actor["X-Session-Id"],
        owner_role=actor["X-Role"],
        kind="session",
        state="created",
        path="",
        data={
            "agent_instance_id": str(agent.agent_instance_id),
            "cert_serial": agent.cert_serial,
        }
        if agent
        else {},
        created_at=now,
        updated_at=now,
        expires_at=datetime.fromisoformat(lease["expires_at"]),
    )
    db.add(operation)
    await db.commit()
    return public_operation(operation)


@internal_router.get(
    "/operations/{identifier}", dependencies=[Depends(fm_service_identity)]
)
async def operation_status(
    identifier: UUID, request: Request, db: AsyncSession = Depends(get_db)
):
    operation = await owned_operation(identifier, request, db)
    await lease_request(operation.lease_id, operation_actor(operation))
    return public_operation(operation)


@internal_router.post(
    "/devices/{device_id}/operations", dependencies=[Depends(fm_service_identity)]
)
async def create_operation(
    device_id: int,
    body: OperationCreate,
    request: Request,
    db: AsyncSession = Depends(get_db),
):
    session = await owned_operation(body.lease_id, request, db)
    terminal = await db.get(Terminal, session.terminal_id)
    if (
        terminal is None
        or terminal.device_id != device_id
        or session.kind != "session"
        or session.state != "active"
    ):
        raise HTTPException(409, detail={"code": "fm_session_not_ready"})
    lease = await lease_request(body.lease_id, operation_actor(session))
    now = datetime.now(UTC)
    if not deployment_readiness(
        terminal, await db.get(FileManagerAgent, terminal.id), now=now
    ).available:
        raise HTTPException(409, detail={"code": "fm_agent_unavailable"})
    existing = await db.get(FileManagerOperation, body.id)
    if existing:
        if (
            operation_actor(existing) != operation_actor(session)
            or existing.lease_id != body.lease_id
            or existing.kind != body.kind
            or existing.path != body.path
            or existing.data.get("offset", 0) != body.offset
        ):
            raise HTTPException(409, "Operation identity mismatch")
        return public_operation(existing)
    operation = FileManagerOperation(
        id=body.id,
        lease_id=body.lease_id,
        terminal_id=terminal.id,
        tenant_id=session.tenant_id,
        owner_user_id=session.owner_user_id,
        owner_session_id=session.owner_session_id,
        owner_role=session.owner_role,
        kind=body.kind,
        state="created",
        path=body.path,
        data={
            "offset": body.offset,
            "agent_instance_id": session.data["agent_instance_id"],
            "cert_serial": session.data["cert_serial"],
        },
        created_at=now,
        updated_at=now,
        expires_at=datetime.fromisoformat(lease["expires_at"]),
    )
    db.add(operation)
    try:
        await db.commit()
    except IntegrityError as exc:
        await db.rollback()
        raise HTTPException(
            409, detail={"code": "fm_transfer_busy_or_commit_unknown"}
        ) from exc
    return public_operation(operation)


async def agent_operation(
    identifier: UUID, terminal: Terminal, db: AsyncSession
) -> FileManagerOperation:
    operation = await db.get(FileManagerOperation, identifier, with_for_update=True)
    if (
        operation is None
        or operation.terminal_id != terminal.id
        or operation.tenant_id != terminal.org_id
        or not terminal.is_active
    ):
        raise HTTPException(404, "Operation not found")
    agent = await db.get(FileManagerAgent, terminal.id)
    if (
        agent is None
        or operation.data.get("agent_instance_id") != str(agent.agent_instance_id)
        or operation.data.get("cert_serial") != terminal.cert_serial
    ):
        raise HTTPException(409, detail={"code": "fm_agent_generation_changed"})
    lease = await lease_request(operation.lease_id, operation_actor(operation))
    if lease.get("device_id") != terminal.device_id:
        raise HTTPException(403, "Lease device mismatch")
    agent.last_seen_at = datetime.now(UTC)
    return operation


@agent_router.get("/operations/{identifier}/ticket")
async def agent_ticket(
    identifier: UUID,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
):
    operation = await agent_operation(identifier, terminal, db)
    if operation.state not in ("created", "active", "running"):
        raise HTTPException(409, "Operation is terminal")
    lease = await lease_request(operation.lease_id, operation_actor(operation))
    remaining = int(
        (
            datetime.fromisoformat(lease["expires_at"]) - datetime.now(UTC)
        ).total_seconds()
    )
    grant_id = str(uuid4())
    if operation.kind == "session":
        operation.data = {
            **operation.data,
            "grant_id": grant_id,
            "ticket_expires_at": lease["expires_at"],
        }
        await db.commit()
    return {
        **public_operation(operation),
        "expires_at": lease["expires_at"],
        "remaining_sec": max(0, min(90, remaining)),
        "grant_id": grant_id,
        "server_time": datetime.now(UTC).isoformat(),
        "read_roots": settings.file_manager_read_roots,
        "write_roots": settings.file_manager_write_roots,
    }


@agent_router.post("/operations/{identifier}/result")
async def agent_result(
    identifier: UUID,
    body: AgentResult,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
):
    operation = await agent_operation(identifier, terminal, db)
    if operation.state in ("completed", "failed", "cancelled"):
        if operation.state != body.state:
            raise HTTPException(409, "Operation is terminal")
        return public_operation(operation)
    if body.state == "active" and operation.kind != "session":
        raise HTTPException(409, "Invalid transition")
    if body.state == "active":
        if str(body.grant_id) != operation.data.get("grant_id"):
            raise HTTPException(409, "Stale session acknowledgement")
        operation.data = {
            **operation.data,
            "applied_expires_at": operation.data["ticket_expires_at"],
        }
    if (
        body.state == "completed"
        and operation.kind in ("upload", "download")
        and not operation.data.get("version_id")
    ):
        raise HTTPException(409, "Verified storage version required")
    if (
        body.state == "completed"
        and operation.kind == "upload"
        and operation.state != "committing"
    ):
        raise HTTPException(409, "Commit permission required")
    if body.state == "completed" and operation.kind == "download":
        raise HTTPException(409, "Browser receipt required")
    if len(json.dumps(body.entries, ensure_ascii=False).encode()) > 61440:
        raise HTTPException(413, "Listing too large")
    if body.entries and (operation.kind != "list" or body.state != "completed"):
        raise HTTPException(409, "Unexpected listing")
    operation.state = body.state
    operation.error_code = body.error_code
    operation.data = {
        **operation.data,
        "entries": body.entries,
        "has_more": body.has_more,
    }
    operation.updated_at = datetime.now(UTC)
    await db.commit()
    return public_operation(operation)


async def source_manifest(
    operation: FileManagerOperation,
    body: SourceManifest,
    db: AsyncSession,
    expected_kind: str,
) -> dict:
    if operation.kind != expected_kind or operation.state not in ("created", "running"):
        raise HTTPException(409, "Source not permitted")
    if operation.sha256 is not None and (
        operation.sha256 != body.sha256 or operation.size_bytes != body.size_bytes
    ):
        raise HTTPException(409, "Manifest is immutable")
    lease = await lease_request(operation.lease_id, operation_actor(operation))
    ttl = min(
        45,
        int(
            (
                datetime.fromisoformat(lease["expires_at"]) - datetime.now(UTC)
            ).total_seconds()
        )
        - 3,
    )
    if ttl < 5:
        raise HTTPException(409, "Insufficient lease lifetime")
    key = f"fm/{operation.tenant_id}/{operation.terminal_id}/{operation.id}"
    grant = await storage.upload_grant(key, body.size_bytes, body.sha256, ttl)
    operation.size_bytes, operation.sha256 = body.size_bytes, body.sha256
    operation.data = {**operation.data, "object_key": key}
    operation.state = "running"
    operation.updated_at = datetime.now(UTC)
    await db.commit()
    return grant


async def source_complete(
    operation: FileManagerOperation, db: AsyncSession, expected_kind: str
) -> dict:
    if (
        operation.kind != expected_kind
        or operation.state != "running"
        or operation.sha256 is None
        or operation.size_bytes is None
    ):
        raise HTTPException(409, "Source not permitted")
    await lease_request(operation.lease_id, operation_actor(operation))
    try:
        version = await storage.verify_object(
            operation.data["object_key"], operation.size_bytes, operation.sha256
        )
    except HTTPException:
        operation.state, operation.error_code = "failed", "fm_integrity_failed"
        await db.commit()
        raise
    # Pin the exact version. A live PUT grant cannot replace the receiver's object.
    operation.data = {**operation.data, "version_id": version}
    operation.state = "running" if expected_kind == "upload" else "verifying"
    operation.updated_at = datetime.now(UTC)
    await db.commit()
    if expected_kind == "upload":
        await lease_request(
            operation.lease_id,
            operation_actor(operation),
            action="transfer",
            operation_id=operation.id,
        )
    return public_operation(operation)


async def receiver_grant(operation: FileManagerOperation, expected_kind: str) -> dict:
    if (
        operation.kind != expected_kind
        or operation.state not in ("running", "verifying")
        or not operation.data.get("version_id")
    ):
        raise HTTPException(409, "Receiver not permitted")
    lease = await lease_request(operation.lease_id, operation_actor(operation))
    ttl = min(
        45,
        int(
            (
                datetime.fromisoformat(lease["expires_at"]) - datetime.now(UTC)
            ).total_seconds()
        )
        - 3,
    )
    if ttl < 5:
        raise HTTPException(409, "Insufficient lease lifetime")
    return {
        **await storage.download_grant(
            operation.data["object_key"], operation.data["version_id"], ttl
        ),
        "size_bytes": operation.size_bytes,
        "sha256": operation.sha256,
    }


@internal_router.post(
    "/operations/{identifier}/manifest", dependencies=[Depends(fm_service_identity)]
)
async def browser_manifest(
    identifier: UUID,
    body: SourceManifest,
    request: Request,
    db: AsyncSession = Depends(get_db),
):
    return await source_manifest(
        await owned_operation(identifier, request, db), body, db, "upload"
    )


@internal_router.post(
    "/operations/{identifier}/source-complete",
    dependencies=[Depends(fm_service_identity)],
)
async def browser_source_complete(
    identifier: UUID, request: Request, db: AsyncSession = Depends(get_db)
):
    return await source_complete(
        await owned_operation(identifier, request, db), db, "upload"
    )


@internal_router.get(
    "/operations/{identifier}/download", dependencies=[Depends(fm_service_identity)]
)
async def browser_download(
    identifier: UUID, request: Request, db: AsyncSession = Depends(get_db)
):
    return await receiver_grant(
        await owned_operation(identifier, request, db), "download"
    )


@internal_router.post(
    "/operations/{identifier}/received", dependencies=[Depends(fm_service_identity)]
)
async def browser_received(
    identifier: UUID,
    body: SourceManifest,
    request: Request,
    db: AsyncSession = Depends(get_db),
):
    operation = await owned_operation(identifier, request, db)
    await lease_request(operation.lease_id, operation_actor(operation))
    if (
        operation.kind != "download"
        or operation.state != "verifying"
        or body.sha256 != operation.sha256
        or body.size_bytes != operation.size_bytes
    ):
        raise HTTPException(409, "Receiver verification failed")
    operation.state = "completed"
    await db.commit()
    return public_operation(operation)


@agent_router.post("/operations/{identifier}/manifest")
async def terminal_manifest(
    identifier: UUID,
    body: SourceManifest,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
):
    return await source_manifest(
        await agent_operation(identifier, terminal, db), body, db, "download"
    )


@agent_router.post("/operations/{identifier}/source-complete")
async def terminal_source_complete(
    identifier: UUID,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
):
    return await source_complete(
        await agent_operation(identifier, terminal, db), db, "download"
    )


@agent_router.get("/operations/{identifier}/download")
async def terminal_download(
    identifier: UUID,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
):
    return await receiver_grant(
        await agent_operation(identifier, terminal, db), "upload"
    )


@agent_router.post("/operations/{identifier}/commit")
async def commit_permission(
    identifier: UUID,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
):
    operation = await agent_operation(identifier, terminal, db)
    if (
        operation.kind != "upload"
        or operation.state != "running"
        or not operation.data.get("version_id")
    ):
        raise HTTPException(409, "Commit not permitted")
    lease = await lease_request(operation.lease_id, operation_actor(operation))
    remaining = int(
        (
            datetime.fromisoformat(lease["expires_at"]) - datetime.now(UTC)
        ).total_seconds()
    )
    if remaining < 5:
        raise HTTPException(409, "Insufficient commit lifetime")
    operation.state = "committing"
    await db.commit()
    return {"remaining_sec": remaining}


@agent_router.post("/operations/{identifier}/reconcile")
async def reconcile_commit(
    identifier: UUID,
    body: CommitReceipt,
    terminal: Terminal = Depends(fm_terminal),
    db: AsyncSession = Depends(get_db),
):
    # Receipt only; never grants filesystem work after a lease expires.
    operation = await db.get(FileManagerOperation, identifier, with_for_update=True)
    if (
        operation is None
        or operation.terminal_id != terminal.id
        or operation.tenant_id != terminal.org_id
        or operation.kind != "upload"
    ):
        raise HTTPException(404, "Operation not found")
    if (
        operation.state not in ("committing", body.state)
        or body.sha256 != operation.sha256
        or body.size_bytes != operation.size_bytes
    ):
        raise HTTPException(409, "Commit receipt mismatch")
    operation.state, operation.error_code = (
        body.state,
        None if body.state == "completed" else "fm_commit_not_applied",
    )
    operation.updated_at = datetime.now(UTC)
    await db.commit()
    return public_operation(operation)


@internal_router.post(
    "/operations/{identifier}/abort", dependencies=[Depends(fm_service_identity)]
)
async def abort_operation(
    identifier: UUID, request: Request, db: AsyncSession = Depends(get_db)
):
    operation = await owned_operation(identifier, request, db)
    if operation.state == "committing":
        operation.error_code = "fm_commit_outcome_unknown"
    elif operation.state not in ("completed", "failed", "cancelled"):
        operation.state, operation.error_code = "cancelled", "fm_user_cancelled"
    await db.commit()
    return public_operation(operation)
