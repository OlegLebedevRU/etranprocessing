"""Terminal policy polling, including authenticated inactive terminals."""

from fastapi import APIRouter, Depends, Response

from app.dependencies import get_current_terminal
from app.models import Terminal
from app.schemas.leo4proxy import Leo4ProxyPolicy
from app.services.leo4proxy_policy import get_leo4proxy_policy

router = APIRouter()


@router.get("/policy", response_model=Leo4ProxyPolicy)
async def read_policy(
    response: Response,
    terminal: Terminal = Depends(get_current_terminal),
) -> Leo4ProxyPolicy:
    response.headers["Cache-Control"] = "no-store"
    return get_leo4proxy_policy(terminal)
