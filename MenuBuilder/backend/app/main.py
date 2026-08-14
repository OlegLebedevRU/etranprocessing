from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.config import settings
from app.database import Base, engine
from app.routers import groups, services


@asynccontextmanager
async def lifespan(app: FastAPI):
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield


app = FastAPI(title="MenuBuilder API", version="0.1.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(groups.router, prefix="/api/groups", tags=["groups"])
app.include_router(services.router, prefix="/api/services", tags=["services"])


@app.get("/api/stats")
async def get_stats():
    from sqlalchemy import func, select

    from app.database import async_session
    from app.models import Group, Service

    async with async_session() as session:
        groups_count = await session.scalar(select(func.count(Group.id)))
        services_count = await session.scalar(select(func.count(Service.id)))
        tsp_count = await session.scalar(
            select(func.count(func.distinct(Service.tsp_code)))
        )
        avg_price = await session.scalar(select(func.avg(Service.price)))

    return {
        "groups": groups_count or 0,
        "services": services_count or 0,
        "tsp_codes": tsp_count or 0,
        "avg_price": round(float(avg_price or 0), 2),
    }
