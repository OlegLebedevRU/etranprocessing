from __future__ import annotations

import logging
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel, Field
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import require_tenant_context
from app.database import get_db
from app.services.remote_session_use_case import (
    RemoteSessionResponse,
    RemoteSessionStatusResponse,
    RemoteSessionUseCase,
)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/remote-sessions", tags=["remote-sessions"])


class SessionStartRequest(BaseModel):
    device_id: int
    session_type: str = Field(..., pattern="^(console|video)$")
    operation_id: str | None = None
    correlation_id: str | None = None
    mode: str = "desktop"
    source_id: str = "0"
    profile: str = "480p"
    start_terminal_stream: bool = True


class SessionStopRequest(BaseModel):
    device_id: int | None = None
    session_id: str | None = None
    reason: str = "user_closed"


class SessionStopResponse(BaseModel):
    status: str
    session_id: str | None = None
    state: str = "closed"
    detail: str | None = None


@router.post(
    "/start",
    response_model=RemoteSessionResponse,
    status_code=status.HTTP_200_OK,
)
async def start_remote_session(
    body: SessionStartRequest,
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> RemoteSessionResponse:
    """Start or replay a remote session (console or video) through unified RemoteSessionUseCase."""
    use_case = RemoteSessionUseCase(db)
    return await use_case.start_session(
        device_id=body.device_id,
        session_type=body.session_type,
        user=user,
        operation_id=body.operation_id,
        correlation_id=body.correlation_id,
        mode=body.mode,
        source_id=body.source_id,
        profile=body.profile,
        start_terminal_stream=body.start_terminal_stream,
    )


@router.post(
    "/stop",
    response_model=SessionStopResponse,
    status_code=status.HTTP_200_OK,
)
async def stop_remote_session(
    body: SessionStopRequest,
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> SessionStopResponse:
    """Stop active remote session by device_id or session_id."""
    if not body.device_id and not body.session_id:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Необходимо указать device_id или session_id для завершения сессии",
        )
    use_case = RemoteSessionUseCase(db)
    res = await use_case.stop_session(
        device_id=body.device_id,
        session_id=body.session_id,
        reason=body.reason,
        user=user,
    )
    return SessionStopResponse(**res)


@router.post(
    "/{session_id}/stop",
    response_model=SessionStopResponse,
    status_code=status.HTTP_200_OK,
)
async def stop_remote_session_by_id(
    session_id: str,
    body: SessionStopRequest | None = None,
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> SessionStopResponse:
    """Stop active remote session by path session_id."""
    reason = body.reason if body else "user_closed"
    use_case = RemoteSessionUseCase(db)
    res = await use_case.stop_session(
        session_id=session_id,
        reason=reason,
        user=user,
    )
    return SessionStopResponse(**res)


@router.get(
    "/devices/{device_id}/active",
    response_model=RemoteSessionStatusResponse,
    status_code=status.HTTP_200_OK,
)
async def get_active_device_session(
    device_id: int,
    user: dict[str, Any] = Depends(require_tenant_context),
    db: AsyncSession = Depends(get_db),
) -> RemoteSessionStatusResponse:
    """Get active session status and health for a device."""
    use_case = RemoteSessionUseCase(db)
    return await use_case.get_session_status(device_id=device_id, user=user)
