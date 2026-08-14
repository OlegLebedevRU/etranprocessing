from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.config import settings
from app.database import engine


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

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

from app.routers import health, licensebilling, gate_gauge, tech_gate

app.include_router(health.router, prefix="/api", tags=["health"])
app.include_router(licensebilling.router, prefix="/api/licensebilling", tags=["licensebilling"])
app.include_router(gate_gauge.router, prefix="/api/gategauge", tags=["gategauge"])
app.include_router(tech_gate.router, prefix="/api/techgate", tags=["techgate"])
