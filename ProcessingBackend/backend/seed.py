import asyncio
from datetime import UTC, datetime, timedelta

from app.database import async_session
from app.models import License, OrgStatus, Terminal


async def seed():
    async with async_session() as db:
        # Terminal 773 - OU=773, O=1 from cert
        terminal = Terminal(
            device_id=773,
            sn="A99D2F18001ECC93DF5CBE27F442C8FA",  # CN from cert
            cert_serial="52B8E528000400002E2D",  # Serial from cert
            org_id=1,
            is_active=True,
        )
        db.add(terminal)

        # Org status
        org_status = OrgStatus(
            org_id=1,
            status="active",
        )
        db.add(org_status)

        await db.commit()

        # Get terminal id for license
        from sqlalchemy import select

        result = await db.execute(select(Terminal).where(Terminal.device_id == 773))
        t = result.scalar_one()

        # License
        license_ = License(
            terminal_id=t.id,
            org_id=1,
            license_type="standard",
            expires_at=datetime.now(UTC) + timedelta(days=365),
            balance=97685,
            is_active=True,
        )
        db.add(license_)

        await db.commit()
        print(f"Seed data created: terminal_id={t.id}, device_id=773, org_id=1")


asyncio.run(seed())
