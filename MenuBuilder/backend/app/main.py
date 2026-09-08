import asyncio
import logging
from contextlib import asynccontextmanager, suppress

from fastapi import FastAPI, Request, status
from fastapi.middleware.cors import CORSMiddleware
from starlette.responses import JSONResponse

from app.config import settings
from app.routers import (
    admin_organizations,
    admin_tenants,
    admin_terminals,
    admin_users,
    auth,
    billing,
    catalog,
    dashboard,
    groups,
    integrations,
    mcp_proxy,
    menu_variants,
    monitoring,
    profile,
    reports,
    services,
    settings_users,
    terminal_bindings,
    video,
)
from app.routers import (
    settings as settings_router,
)
from app.user_store import get_user_store

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger(__name__)


async def _cleanup_expired_sessions_task() -> None:
    """Background task running every 6 hours to clean up expired/revoked sessions."""
    while True:
        try:
            await asyncio.sleep(6 * 3600)
            store = get_user_store()
            deleted = await store.cleanup_expired_sessions()
            logger.info("Background session cleanup removed %d sessions", deleted)
        except asyncio.CancelledError:
            break
        except Exception as exc:  # noqa: BLE001
            logger.warning("Error in background session cleanup: %s", exc)


@asynccontextmanager
async def lifespan(app: FastAPI):
    cleanup_task: asyncio.Task | None = None
    if settings.session_cleanup_enabled:
        cleanup_task = asyncio.create_task(_cleanup_expired_sessions_task())
    try:
        yield
    finally:
        if cleanup_task is not None:
            cleanup_task.cancel()
            with suppress(asyncio.CancelledError):
                await cleanup_task


app = FastAPI(title="MenuBuilder API", version="0.2.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.middleware("http")
async def csrf_protection_middleware(request: Request, call_next):
    # CSRF check: if authenticated via cookie (no Authorization Bearer header)
    # on mutating methods (POST, PUT, PATCH, DELETE), require X-Requested-With: XMLHttpRequest.
    # Exclude login and refresh.
    auth_header = request.headers.get("Authorization")
    has_bearer = bool(auth_header and auth_header.startswith("Bearer "))
    if (
        not has_bearer
        and "accessToken" in request.cookies
        and request.method in ("POST", "PUT", "PATCH", "DELETE")
        and not request.url.path.endswith(("/auth/login", "/auth/refresh"))
        and request.headers.get("X-Requested-With") != "XMLHttpRequest"
    ):
        return JSONResponse(
            status_code=status.HTTP_403_FORBIDDEN,
            content={"detail": "CSRF check failed"},
        )
    return await call_next(request)


app.include_router(auth.router, prefix="/api", tags=["auth"])
app.include_router(admin_users.router, prefix="/api", tags=["admin-users"])
app.include_router(admin_tenants.router, prefix="/api", tags=["admin-tenants"])
# Browser-facing alias /api/auth/switch-tenant (refreshToken cookie path = /api/auth)
app.include_router(admin_tenants.auth_alias_router, prefix="/api", tags=["auth"])
app.include_router(admin_organizations.router, tags=["admin-organizations"])
app.include_router(admin_terminals.router, tags=["admin-terminals"])
app.include_router(groups.router, prefix="/api/groups", tags=["groups"])
app.include_router(services.router, prefix="/api/services", tags=["services"])
app.include_router(catalog.router)
app.include_router(
    menu_variants.router, prefix="/api/menu-variants", tags=["menu-variants"]
)
app.include_router(terminal_bindings.router, prefix="/api", tags=["terminals"])
app.include_router(billing.router)
app.include_router(profile.router, prefix="/api", tags=["profile"])
app.include_router(settings_router.router, prefix="/api", tags=["settings"])
app.include_router(settings_users.router, prefix="/api", tags=["settings-users"])
app.include_router(integrations.router)
app.include_router(mcp_proxy.router, prefix="/api", tags=["mcp"])
app.include_router(dashboard.router)
app.include_router(monitoring.router)
app.include_router(reports.router)
app.include_router(video.router)
