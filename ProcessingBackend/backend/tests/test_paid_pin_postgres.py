"""Destructive fixtures are confined to the disposable subscription_test DB."""

import asyncio
import os
from datetime import UTC, datetime, timedelta
from uuid import uuid4

import etranprocessing_db.l4desk  # noqa: F401
import pytest
from alembic.config import Config
from etranprocessing_db import Base
from etranprocessing_db.l4desk import L4DeskTenantProfile, L4DeskTerminal
from fastapi import HTTPException, Response
from sqlalchemy import create_mock_engine, func, select, text
from sqlalchemy.engine import make_url
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine

from alembic import command
from app.config import settings
from app.models import (
    BillingOrder,
    BillingOrderItem,
    CertificatePin,
    License,
    Org,
    OrgBillingSettings,
    Terminal,
    TerminalType,
)
from app.routers.certificates import issue_certificate_pin_endpoint, issue_renewal_pin
from app.schemas.certificates import IssueCertificatePinRequest, RenewalPinRequest


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_paid_pin_races_purpose_isolation_and_real_expand(monkeypatch):
    url = os.environ.get("SUBSCRIPTION_TEST_DATABASE_URL")
    if not url:
        pytest.skip("Requires a disposable PostgreSQL test database")
    if make_url(url).database != "subscription_test":
        pytest.fail("Destructive fixture requires dedicated subscription_test")
    engine = create_async_engine(url)
    factory = async_sessionmaker(engine, expire_on_commit=False)
    monkeypatch.setattr(settings, "product_scope_split_enabled", True)
    monkeypatch.setattr(settings, "service_auth_token", "test-fixture-only")
    now = datetime.now(UTC)
    try:
        async with engine.begin() as conn:
            statements = ["DROP SCHEMA public CASCADE", "CREATE SCHEMA public"]

            def capture(statement, *args, **kwargs):
                statements.append(str(statement.compile(dialect=ddl_engine.dialect)))

            ddl_engine = create_mock_engine(
                "postgresql://", capture, paramstyle="named"
            )
            Base.metadata.create_all(ddl_engine, checkfirst=False)
            await conn.execute(text("SELECT 1"))
            raw = await conn.get_raw_connection()
            await raw.driver_connection.execute(";\n".join(statements))
        async with factory() as db:
            db.add(
                Org(org_id=1, org_name="Fixture", name="Fixture", site_mode="classic")
            )
            db.add(TerminalType(id=0, name="Fixture"))
            await db.flush()
            db.add(
                Terminal(
                    id=1,
                    device_id=773,
                    org_id=1,
                    sn="fixture-sn",
                    is_active=True,
                    l4desk_subscription_enabled=False,
                    cert_serial="A" * 40,
                    cert_not_valid_after=now + timedelta(days=30),
                )
            )
            db.add(
                OrgBillingSettings(
                    org_id=1,
                    billing_mode="prepaid",
                    monthly_price_minor=0,
                    cert_billing_mode="per_operation",
                    cert_price_minor=100,
                    tenant_pin_creation_enabled=True,
                    cert_charge_primary_issue=True,
                    cert_charge_reissue=True,
                )
            )
            order = BillingOrder(
                id=uuid4(),
                org_id=1,
                status="paid",
                currency="RUB",
                amount_minor=200,
                provider="simulation",
                paid_at=now,
            )
            db.add(order)
            await db.flush()
            items = {}
            for purpose in ("setup", "renew"):
                item = BillingOrderItem(
                    order_id=order.id,
                    terminal_id=1,
                    operation="cert_pin",
                    periods_due=0,
                    advance_periods=0,
                    billing_period_months=1,
                    monthly_price_minor=0,
                    amount_minor=100,
                    old_expires_at=now,
                    new_expires_at=now,
                    cert_policy_snapshot={"purpose": purpose},
                )
                db.add(item)
                await db.flush()
                items[purpose] = item.id
            setup = CertificatePin(
                pin="000001",
                terminal_id=1,
                org_id=1,
                order_item_id=items["setup"],
                purpose="setup",
                status="pending",
                payment_required=True,
                expires_at=now + timedelta(days=1),
            )
            db.add(setup)
            await db.commit()
            setup_id = setup.id

        async def renew():
            async with factory() as db:
                return await issue_renewal_pin(
                    RenewalPinRequest(
                        tenant_id=1,
                        terminal_id=1,
                        sn="fixture-sn",
                        order_item_id=items["renew"],
                    ),
                    db,
                    "fixture",
                )

        first, duplicate = await asyncio.gather(renew(), renew())
        assert first.pin_id == duplicate.pin_id
        async with factory() as db:
            assert (
                await db.scalar(
                    select(func.count())
                    .select_from(CertificatePin)
                    .where(CertificatePin.purpose == "renew")
                )
                == 1
            )
            assert (await db.get(CertificatePin, setup_id)).status == "pending"
            response = await issue_certificate_pin_endpoint(
                IssueCertificatePinRequest(
                    operation_id="fixture-paid-setup",
                    tenant_id=1,
                    terminal_id=1,
                    sn="fixture-sn",
                    order_item_id=items["setup"],
                ),
                Response(),
                "fixture",
                db,
            )
            assert response.pin == "000001"
            assert (await db.get(CertificatePin, first.pin_id)).status == "pending"
        async with factory() as db:
            with pytest.raises(HTTPException) as exc:
                await issue_renewal_pin(
                    RenewalPinRequest(
                        tenant_id=1,
                        terminal_id=1,
                        sn="fixture-sn",
                        order_item_id=items["setup"],
                    ),
                    db,
                    "fixture",
                )
            assert exc.value.status_code == 403
        async with factory() as db:
            pin = await db.get(CertificatePin, first.pin_id)
            pin.expires_at = now - timedelta(seconds=1)
            await db.commit()
        with pytest.raises(HTTPException) as exc:
            await renew()
        assert exc.value.status_code == 410
        async with factory() as db:
            assert (
                await db.scalar(select(func.count()).select_from(CertificatePin)) == 2
            )
            assert (await db.get(Terminal, 1)).cert_serial == "A" * 40

        # Reconstruct only the 030/031 delta in this disposable schema.
        async with engine.begin() as conn:
            await conn.execute(
                text("ALTER TABLE terminals DROP COLUMN l4desk_subscription_enabled")
            )
            await conn.execute(
                text(
                    "CREATE TABLE alembic_version (version_num VARCHAR(32) NOT NULL PRIMARY KEY)"
                )
            )
            await conn.execute(text("INSERT INTO alembic_version VALUES ('030')"))
        monkeypatch.setattr(settings, "database_url", url)
        await asyncio.to_thread(command.upgrade, Config("alembic.ini"), "031")
        async with engine.connect() as conn:
            assert (
                await conn.scalar(text("SELECT version_num FROM alembic_version"))
                == "031"
            )
            assert (
                await conn.scalar(
                    text("SELECT l4desk_subscription_enabled FROM terminals WHERE id=1")
                )
                is False
            )
            assert await conn.scalar(text("SELECT count(*) FROM certificate_pins")) == 2

        from app.product_scope_cutover import cutover

        manifest = {
            "organizations": [
                {"org_id": 1, "approved_product": "classic"},
                {"org_id": 339, "approved_product": "classic"},
                {"org_id": 10000, "approved_product": "l4desk"},
            ]
        }
        async with factory() as db:
            (await db.get(Org, 1)).site_mode = "both"
            db.add_all(
                [
                    Org(
                        org_id=339,
                        org_name="Fixture339",
                        name="Fixture339",
                        site_mode="both",
                    ),
                    Org(
                        org_id=10000,
                        org_name="Fixture10000",
                        name="Fixture10000",
                        site_mode="both",
                    ),
                ]
            )
            await db.flush()
            db.add(
                Terminal(
                    id=2,
                    device_id=1000009,
                    org_id=10000,
                    sn="fixture-10000",
                    is_active=False,
                )
            )
            db.add(
                License(terminal_id=1, org_id=1, expires_at=now + timedelta(days=30))
            )
            db.add(L4DeskTenantProfile(tenant_id=10000, timezone="UTC"))
            await db.flush()
            for terminal_id, tenant_id, sn in (
                (1, 1, "fixture-sn"),
                (2, 10000, "fixture-10000"),
            ):
                db.add(
                    L4DeskTerminal(
                        terminal_id=terminal_id,
                        runtime_terminal_id=terminal_id,
                        tenant_id=tenant_id,
                        sn=sn,
                        device_id=terminal_id,
                        ordinal=1,
                        external_terminal_id=str(terminal_id),
                        operation_id=f"fixture-{terminal_id}",
                        correlation_id="fixture",
                        deleted_at=now if terminal_id == 1 else None,
                    )
                )
            await db.commit()
        async with factory() as db:
            assert (await cutover(db, manifest))["changes"]
            await db.rollback()
        async with factory() as db:
            assert (await db.get(Org, 1)).site_mode == "both"
            assert (await db.get(Terminal, 1)).l4desk_subscription_enabled is False
            await cutover(db, manifest)
            await db.commit()
        async with factory() as db:
            assert (await cutover(db, manifest))["changes"] == []
            assert await db.get(L4DeskTenantProfile, 1) is None
            assert (await db.get(Terminal, 2)).is_active is False
            await cutover(db, manifest, finalize_dev=True)
            await db.commit()
        async with factory() as db:
            assert (await cutover(db, manifest, finalize_dev=True))["changes"] == []
            assert (await db.get(L4DeskTerminal, 1)).deleted_at is None
            assert (await db.get(L4DeskTerminal, 1)).device_id == 773
            assert (await db.get(Terminal, 1)).cert_serial == "A" * 40
            assert await db.scalar(
                select(License.expires_at).where(License.terminal_id == 1)
            ) == now + timedelta(days=30)
    finally:
        await engine.dispose()
