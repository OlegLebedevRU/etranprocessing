from app.database import engine, Base
from app.models import Terminal, OrgStatus, License, GateGaugeRecord, TechGateRecord
import asyncio

async def create_tables():
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    print("Tables created")
    await engine.dispose()

asyncio.run(create_tables())
