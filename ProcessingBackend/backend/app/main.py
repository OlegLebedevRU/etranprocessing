import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response

from app.config import settings
from app.database import engine

logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    yield
    await engine.dispose()


app = FastAPI(
    title="Processing Backend",
    description="etranprocessing API for terminals",
    version="0.1.0",
    lifespan=lifespan,
)


def _xml_error_response(status_code: int, detail: str) -> Response:
    """Return an XML error response for the licensebilling API.

    The legacy terminal client expects XML; FastAPI defaults to JSON.
    For recognized terminals, we return 200 with Result=ERROR (infrastructure error).
    For unrecognized terminals (401), we return 401 with Result=ERROR.
    """
    content = (
        "<?xml version='1.0' encoding='UTF-8'?>\n"
        "<Response>"
        "<Result>ERROR</Result>"
        f"<Description>{detail}</Description>"
        "</Response>"
    )
    return Response(
        content=content,
        status_code=status_code if status_code == 401 else 200,
        media_type="application/xml",
    )


@app.exception_handler(HTTPException)
async def xml_licensebilling_exception_handler(request: Request, exc: HTTPException):
    """Return XML responses for errors on /api/licensebilling routes."""
    if request.url.path.startswith("/api/licensebilling"):
        logger.warning(
            "XML error for %s %s: %d %s",
            request.method,
            request.url.path,
            exc.status_code,
            exc.detail,
        )
        return _xml_error_response(exc.status_code, str(exc.detail))
    # For all other routes, use default JSON behavior
    from fastapi.exception_handlers import http_exception_handler

    return await http_exception_handler(request, exc)


app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

from app.routers import (
    certificates,
    gate_gauge,
    health,
    licensebilling,
    list_menu,
    payment,
    tech_gate,
)

app.include_router(health.router, prefix="/api", tags=["health"])
app.include_router(
    licensebilling.router, prefix="/api/licensebilling", tags=["licensebilling"]
)
app.include_router(gate_gauge.router, prefix="/api/gategauge", tags=["gategauge"])
app.include_router(tech_gate.router, prefix="/api/techgate", tags=["techgate"])
app.include_router(payment.router, prefix="/api/payment", tags=["payment"])
app.include_router(
    certificates.router, prefix="/api/certificates", tags=["certificates"]
)
app.include_router(list_menu.router, prefix="/api", tags=["menu"])
