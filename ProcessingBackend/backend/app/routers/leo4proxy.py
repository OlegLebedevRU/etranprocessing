"""Terminal policy polling, including authenticated inactive terminals."""

from datetime import UTC, datetime

from fastapi import APIRouter, Depends, Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import Terminal
from app.schemas.leo4proxy import Leo4ProxyPolicy
from app.services.leo4proxy_policy import get_leo4proxy_policy, subscription_allowance

router = APIRouter()


@router.get("/policy", response_model=Leo4ProxyPolicy, response_model_exclude_none=True)
async def read_policy(
    response: Response,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
) -> Leo4ProxyPolicy:
    response.headers["Cache-Control"] = "no-store"
    at = datetime.now(UTC)
    return get_leo4proxy_policy(
        terminal, await subscription_allowance(db, terminal, now=at), now=at
    )
