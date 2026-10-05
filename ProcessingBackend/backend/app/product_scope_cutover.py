"""Reviewed, idempotent data cutover; dry-run unless --apply is explicit.

Run from the released PB image. The approved census is JSON on stdin, never a
database URL. Base classification precedes feature activation; dev profile is
created only after both consumers run with the split enabled.
"""

import argparse
import asyncio
import json
import sys

from etranprocessing_db.l4desk import (
    L4DeskAuditEvent,
    L4DeskTenantProfile,
    L4DeskTerminal,
)
from sqlalchemy import select, text

from app.config import settings
from app.database import async_session, engine
from app.models import Org, Terminal


async def cutover(db, manifest: dict, *, finalize_dev: bool = False) -> dict:
    approved = {
        int(row["org_id"]): row["approved_product"] for row in manifest["organizations"]
    }
    if not approved or any(
        value not in ("classic", "l4desk") for value in approved.values()
    ):
        raise ValueError("Invalid approved product census")
    orgs = list(
        (await db.scalars(select(Org).order_by(Org.org_id).with_for_update())).all()
    )
    if {org.org_id for org in orgs} != set(approved):
        raise ValueError("Organization census changed; review before cutover")
    profiles = list(
        (await db.scalars(select(L4DeskTenantProfile).with_for_update())).all()
    )
    products = {org.org_id: org for org in orgs}
    l4desk_ids = {org_id for org_id, product in approved.items() if product == "l4desk"}
    if {profile.tenant_id for profile in profiles} - l4desk_ids - {1}:
        raise ValueError("Unreviewed Classic subscription profile")
    if not l4desk_ids <= {profile.tenant_id for profile in profiles}:
        raise ValueError("Approved L4Desk tenant has no profile")
    terminals = list(
        (
            await db.scalars(
                select(Terminal).order_by(Terminal.id).with_for_update(of=Terminal)
            )
        ).all()
    )
    by_id = {terminal.id: terminal for terminal in terminals}
    dev = by_id.get(1)
    if (
        dev is None
        or (dev.org_id, dev.device_id) != (1, 773)
        or approved.get(1) != "classic"
    ):
        raise ValueError("Reviewed dev identity changed")
    technical = list((await db.scalars(select(L4DeskTerminal).with_for_update())).all())
    technical_by_runtime = {}
    for row in technical:
        runtime = by_id.get(row.runtime_terminal_id)
        if runtime is None or (runtime.id, runtime.org_id, runtime.sn) != (
            row.terminal_id,
            row.tenant_id,
            row.sn,
        ):
            raise ValueError(f"Technical identity mismatch: {row.terminal_id}")
        technical_by_runtime[runtime.id] = row
    if any(
        terminal.id not in technical_by_runtime
        for terminal in terminals
        if terminal.org_id in l4desk_ids
    ):
        raise ValueError("L4Desk terminal without reviewed technical identity")
    if 1 not in technical_by_runtime:
        raise ValueError("Dev technical identity missing")
    changes = []

    def assign(row, identity, field, value):
        old = getattr(row, field)
        if old != value:
            changes.append(
                {"entity": identity, "field": field, "before": old, "after": value}
            )
            setattr(row, field, value)

    if finalize_dev:
        if not settings.product_scope_split_enabled:
            raise ValueError(
                "Dev profile requires enabled split on both deployed consumers"
            )
        if any(
            products[org_id].site_mode != product
            for org_id, product in approved.items()
        ):
            raise ValueError("Base classification not completed")
        if not dev.l4desk_subscription_enabled:
            raise ValueError("Dev enrollment not applied")
        if 1 not in {profile.tenant_id for profile in profiles}:
            db.add(
                L4DeskTenantProfile(
                    tenant_id=1, timezone=products[1].timezone or "Europe/Moscow"
                )
            )
            changes.append(
                {
                    "entity": "profile:1",
                    "field": "created",
                    "before": False,
                    "after": True,
                }
            )
        assign(technical_by_runtime[1], "technical:1", "deleted_at", None)
    else:
        for org in orgs:
            product = approved[org.org_id]
            if org.site_mode not in ("both", product):
                raise ValueError(f"Unreviewed product change: {org.org_id}")
            assign(org, f"org:{org.org_id}", "site_mode", product)
            assign(org, f"org:{org.org_id}", "default_site", product)
            assign(
                org,
                f"org:{org.org_id}",
                "classic_licenses_enabled",
                product == "classic",
            )
            assign(
                org, f"org:{org.org_id}", "l4desk_licenses_enabled", product == "l4desk"
            )
        for terminal in terminals:
            assign(
                terminal,
                f"terminal:{terminal.id}",
                "l4desk_subscription_enabled",
                terminal.org_id in l4desk_ids or terminal.id == 1,
            )
        for row in technical:
            assign(
                row,
                f"technical:{row.terminal_id}",
                "device_id",
                by_id[row.runtime_terminal_id].device_id,
            )
    if changes:
        db.add(
            L4DeskAuditEvent(
                tenant_id=1,
                actor="product-scope-cutover",
                event_type="product.scope_cutover",
                subject_type="product_scope",
                subject_id="2026-10-05",
                correlation_id="licensing-cutover-2026-10-05",
                outcome="dev" if finalize_dev else "base",
                details={"changes": json.loads(json.dumps(changes, default=str))},
            )
        )
    return {
        "phase": "dev" if finalize_dev else "base",
        "organizations": len(orgs),
        "l4desk_organizations": sorted(l4desk_ids),
        "changes": changes,
    }


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--finalize-dev", action="store_true")
    args = parser.parse_args()
    manifest = json.load(sys.stdin)
    try:
        async with async_session() as db:
            await db.execute(text("SET LOCAL lock_timeout = '5s'"))
            await db.execute(text("SET LOCAL statement_timeout = '30s'"))
            report = await cutover(db, manifest, finalize_dev=args.finalize_dev)
            if args.apply:
                await db.commit()
            else:
                await db.rollback()
            print(json.dumps({"applied": args.apply, **report}, default=str))
    finally:
        await engine.dispose()


if __name__ == "__main__":
    asyncio.run(main())
