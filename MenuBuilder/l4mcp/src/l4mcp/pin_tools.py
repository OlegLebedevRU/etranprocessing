"""Interactive, tenant-scoped certificate PIN tools."""

from __future__ import annotations

from datetime import UTC, datetime, timedelta

import httpx
from fastmcp import Context, FastMCP

from l4mcp.access import current_principal
from l4mcp.config import load_config
from l4mcp.db import Database

PAGE_SIZE = 20
MAX_ISSUE = 5
SOON_DAYS = 30


def _state(expires_at: datetime | None, serial: str | None, now: datetime) -> str:
    if not serial or not expires_at:
        return "unknown"
    if expires_at <= now:
        return "expired"
    if expires_at <= now + timedelta(days=SOON_DAYS):
        return "expiring"
    return "valid"


def _db(ctx: Context) -> Database:
    return ctx.lifespan_context["db"]


def register_pin_tools(mcp: FastMCP) -> None:
    @mcp.tool(annotations={"readOnlyHint": True})
    async def certificate_summary(ctx: Context) -> dict:
        """Count expired, expiring, valid, and unknown certificates in your tenant."""
        principal = await current_principal()
        now = datetime.now(UTC)
        row = await _db(ctx).fetchrow(
            """SELECT
                 count(*) FILTER (WHERE cert_serial IS NULL OR cert_not_valid_after IS NULL) AS unknown,
                 count(*) FILTER (WHERE cert_serial IS NOT NULL AND cert_not_valid_after <= $2) AS expired,
                 count(*) FILTER (WHERE cert_serial IS NOT NULL AND cert_not_valid_after > $2
                                    AND cert_not_valid_after <= $3) AS expiring,
                 count(*) FILTER (WHERE cert_serial IS NOT NULL AND cert_not_valid_after > $3) AS valid
               FROM terminals WHERE org_id = $1""",
            principal.org_id,
            now,
            now + timedelta(days=SOON_DAYS),
        )
        assert row is not None
        counts = {
            name: int(row[name]) for name in ("expired", "expiring", "valid", "unknown")
        }
        return {"org_id": principal.org_id, "expiring_within_days": SOON_DAYS, **counts}

    @mcp.tool(annotations={"readOnlyHint": True})
    async def inspect_terminals(
        ctx: Context,
        device_ids: list[int] | None = None,
        range_start: int | None = None,
        range_end: int | None = None,
        page: int = 1,
    ) -> dict:
        """Inspect exact device IDs or a bounded range; returns at most 20 per page."""
        principal = await current_principal()
        if page < 1:
            raise ValueError("page must be positive")
        exact = device_ids is not None
        bounded_range = range_start is not None and range_end is not None
        if exact == bounded_range:
            raise ValueError("Specify exact device_ids or range_start/range_end")
        if exact:
            if (
                not device_ids
                or len(device_ids) > 100
                or any(n <= 0 for n in device_ids)
            ):
                raise ValueError("Specify 1–100 positive device IDs")
            selection = sorted(set(device_ids))
            rows = await _db(ctx).fetch(
                """SELECT t.id, t.device_id, t.sn, t.cert_serial,
                          t.cert_not_valid_after, p.pin, p.status AS pin_status,
                          p.expires_at AS pin_expires_at
                   FROM terminals t
                   LEFT JOIN LATERAL (
                     SELECT pin, status, expires_at FROM certificate_pins
                     WHERE terminal_id = t.id ORDER BY created_at DESC LIMIT 1
                   ) p ON true
                   WHERE t.org_id = $1 AND t.device_id = ANY($2::int[])
                   ORDER BY t.device_id LIMIT $3 OFFSET $4""",
                principal.org_id,
                selection,
                PAGE_SIZE + 1,
                (page - 1) * PAGE_SIZE,
            )
        else:
            if (
                range_start is None
                or range_end is None
                or range_start <= 0
                or range_end < range_start
                or range_end - range_start >= 100
            ):
                raise ValueError("Specify an ascending range of at most 100 device IDs")
            rows = await _db(ctx).fetch(
                """SELECT t.id, t.device_id, t.sn, t.cert_serial,
                          t.cert_not_valid_after, p.pin, p.status AS pin_status,
                          p.expires_at AS pin_expires_at
                   FROM terminals t
                   LEFT JOIN LATERAL (
                     SELECT pin, status, expires_at FROM certificate_pins
                     WHERE terminal_id = t.id ORDER BY created_at DESC LIMIT 1
                   ) p ON true
                   WHERE t.org_id = $1 AND t.device_id BETWEEN $2 AND $3
                   ORDER BY t.device_id LIMIT $4 OFFSET $5""",
                principal.org_id,
                range_start,
                range_end,
                PAGE_SIZE + 1,
                (page - 1) * PAGE_SIZE,
            )
        now = datetime.now(UTC)
        items = []
        for row in rows[:PAGE_SIZE]:
            pin_active = row["pin_status"] == "pending" and (
                row["pin_expires_at"] is None or row["pin_expires_at"] > now
            )
            items.append(
                {
                    "device_id": row["device_id"],
                    "sn": row["sn"],
                    "certificate_status": _state(
                        row["cert_not_valid_after"], row["cert_serial"], now
                    ),
                    "certificate_expires_at": row["cert_not_valid_after"].isoformat()
                    if row["cert_not_valid_after"]
                    else None,
                    "pin_status": row["pin_status"],
                    "pin_expires_at": row["pin_expires_at"].isoformat()
                    if row["pin_expires_at"]
                    else None,
                    "pending_pin": row["pin"] if pin_active else None,
                }
            )
        return {
            "org_id": principal.org_id,
            "page": page,
            "page_size": PAGE_SIZE,
            "has_more": len(rows) > PAGE_SIZE,
            "items": items,
            "missing_device_ids": sorted(
                set(device_ids) - {x["device_id"] for x in items}
            )
            if exact and page == 1 and len(device_ids) <= PAGE_SIZE
            else [],
        }

    @mcp.tool
    async def issue_certificate_pins(
        ctx: Context,
        device_ids: list[int],
        confirmation: str | None = None,
    ) -> dict:
        """Issue up to five PINs for exact IDs. Active certificates need explicit confirmation."""
        principal = await current_principal()
        if (
            not device_ids
            or len(device_ids) > MAX_ISSUE
            or len(set(device_ids)) != len(device_ids)
        ):
            raise ValueError("Specify 1–5 distinct device IDs")
        if any(n <= 0 for n in device_ids):
            raise ValueError("Device IDs must be positive")
        rows = await _db(ctx).fetch(
            """SELECT id, device_id, cert_serial, cert_not_valid_after
               FROM terminals WHERE org_id = $1 AND device_id = ANY($2::int[])""",
            principal.org_id,
            device_ids,
        )
        found = {row["device_id"]: row for row in rows}
        missing = sorted(set(device_ids) - found.keys())
        if missing:
            return {
                "status": "not_found",
                "device_ids": missing,
                "next_step": "Create the terminal on the site or contact an administrator.",
            }
        now = datetime.now(UTC)
        active = sorted(
            n
            for n, row in found.items()
            if _state(row["cert_not_valid_after"], row["cert_serial"], now) != "expired"
        )
        phrase = "CONFIRM " + ",".join(map(str, active))
        if active and confirmation != phrase:
            return {
                "status": "confirmation_required",
                "active_device_ids": active,
                "confirmation": phrase,
                "warning": "Ask the human user to send this exact confirmation. Installing a new certificate on another machine can disconnect the current terminal.",
            }
        base_url = load_config().menubuilder_url.rstrip("/")
        if not base_url:
            raise RuntimeError("MENUBUILDER_API_URL is required")
        results = []
        async with httpx.AsyncClient(timeout=20.0) as client:
            for device_id in device_ids:
                response = await client.post(
                    f"{base_url}/api/billing/terminals/{found[device_id]['id']}/certificate-pin",
                    headers={"Authorization": principal.bearer},
                )
                if response.status_code != 200:
                    results.append(
                        {
                            "device_id": device_id,
                            "status": "refused",
                            "http_status": response.status_code,
                        }
                    )
                    continue
                data = response.json()
                results.append({"device_id": device_id, **data})
        return {"results": results}

    @mcp.tool
    async def revoke_pending_pin(ctx: Context, pin: str) -> dict:
        """Revoke one pending PIN belonging to your tenant."""
        principal = await current_principal()
        if len(pin) != 6 or not pin.isdigit():
            raise ValueError("PIN must contain exactly six digits")
        db = _db(ctx)
        row = await db.fetchrow(
            "SELECT terminal_id, status FROM certificate_pins WHERE pin = $1 AND org_id = $2",
            pin,
            principal.org_id,
        )
        if row is None or row["status"] != "pending":
            raise ValueError("Pending PIN not found")
        await db.execute(
            "UPDATE certificate_pins SET status = 'revoked' WHERE pin = $1 AND org_id = $2 AND status = 'pending'",
            pin,
            principal.org_id,
        )
        return {"status": "revoked", "terminal_id": row["terminal_id"]}
