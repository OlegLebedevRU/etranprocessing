import asyncio
from sqlalchemy import text
from app.database import engine, Base
from app.models import (
    Terminal, OrgStatus, License, GateGaugeRecord, TechGateRecord,
    Org, Tsp, TspParameterCode, ServiceMenu, Payment, PaymentParam, BalanceTerminalTsp
)

async def create_tables():
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
        print("All tables created successfully!")
        result = await conn.execute(text(
            "SELECT table_name FROM information_schema.tables WHERE table_schema = 'public'"
        ))
        tables = [row[0] for row in result]
        print(f"Tables in database: {tables}")

if __name__ == "__main__":
    asyncio.run(create_tables())
