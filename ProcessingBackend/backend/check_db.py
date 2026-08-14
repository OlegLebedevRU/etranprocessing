import asyncio
from sqlalchemy import select, func
from app.database import async_session
from app.models import GateGaugeRecord, TechGateRecord

async def check():
    async with async_session() as db:
        # Count gauge records
        result = await db.execute(select(func.count(GateGaugeRecord.id)))
        gauge_count = result.scalar()
        print(f"GateGauge records: {gauge_count}")

        # Count techgate records
        result = await db.execute(select(func.count(TechGateRecord.id)))
        techgate_count = result.scalar()
        print(f"TechGate records: {techgate_count}")

        # Last gauge record
        result = await db.execute(select(GateGaugeRecord).order_by(GateGaugeRecord.id.desc()).limit(1))
        last_gauge = result.scalar_one_or_none()
        if last_gauge:
            print(f"Last gauge: device_id={last_gauge.device_id}, data={last_gauge.gauge_data}")

        # Last techgate record
        result = await db.execute(select(TechGateRecord).order_by(TechGateRecord.id.desc()).limit(1))
        last_techgate = result.scalar_one_or_none()
        if last_techgate:
            print(f"Last techgate: device_id={last_techgate.device_id}, function={last_techgate.function_name}")

asyncio.run(check())
