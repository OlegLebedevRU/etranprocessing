from contextlib import asynccontextmanager

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
    terminal_bindings,
)
from app.services.gauge_bus import gauge_mqtt_bus


@asynccontextmanager
async def lifespan(app: FastAPI):
    gauge_mqtt_bus.start()
    yield
    gauge_mqtt_bus.stop()


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
app.include_router(integrations.router)
app.include_router(mcp_proxy.router, prefix="/api", tags=["mcp"])
app.include_router(dashboard.router)
app.include_router(monitoring.router)
app.include_router(reports.router)
