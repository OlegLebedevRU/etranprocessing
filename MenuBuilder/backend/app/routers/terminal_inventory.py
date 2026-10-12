"""Private allowlisted delivery; independent of public IoT API webhooks."""

import hmac
from collections.abc import Callable, Coroutine
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Request, Response
from fastapi.routing import APIRoute
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import settings
from app.database import get_db
from app.services.terminal_inventory import InventoryDelivery, apply_inventory


async def inventory_auth(request: Request, x_internal_service_key: str) -> None:
    expected = settings.inventory_delivery_token
    if not expected or not hmac.compare_digest(x_internal_service_key, expected):
        raise HTTPException(403, "inventory_service_auth_required")
    chunks = []
    size = 0
    async for chunk in request.stream():
        size += len(chunk)
        if size > 64 * 1024:
            raise HTTPException(413, "inventory_payload_too_large")
        chunks.append(chunk)
    # Cache the bounded body for Starlette/FastAPI JSON parsing.
    request._body = b"".join(chunks)


class InventoryRoute(APIRoute):
    def get_route_handler(self) -> Callable[[Request], Coroutine[Any, Any, Response]]:
        handler = super().get_route_handler()

        async def bounded(request: Request) -> Response:
            await inventory_auth(
                request, request.headers.get("x-internal-service-key", "")
            )
            return await handler(request)

        return bounded


router = APIRouter(
    prefix="/api/internal/v1/terminal-inventory",
    tags=["terminal-inventory"],
    include_in_schema=False,
    route_class=InventoryRoute,
)


@router.post("/events")
async def receive(delivery: InventoryDelivery, db: AsyncSession = Depends(get_db)):
    return {"outcome": await apply_inventory(db, delivery)}
