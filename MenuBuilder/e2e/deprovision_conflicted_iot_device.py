"""Retire the superseded E2E device from IoT and RabbitMQ.

Run inside app1. Dry run is the default. The historic provisioning and org
binding remain; the connection row is removed so admin reconciliation cannot
recreate the MQTT user. Repeated application is safe after partial failure.
"""

from __future__ import annotations

import asyncio
import importlib
import os

import asyncpg
import httpx

DEVICE_ID = 1000004
TENANT_ID = 4
EXPECTED_SN = "a4b1000004c94276d260926"


async def main() -> None:
    dsn = os.environ["APP_CONFIG__DB__URL"].replace(
        "postgresql+asyncpg://", "postgresql://"
    )
    rmq_admin = importlib.import_module("core.integrations.rmq_admin_api").RmqAdminApi
    db = await asyncpg.connect(dsn)
    try:
        device = await db.fetchrow(
            "SELECT sn, is_deleted FROM tb_devices WHERE device_id=$1", DEVICE_ID
        )
        org_id = await db.fetchval(
            "SELECT org_id FROM tb_device_org_binds WHERE device_id=$1", DEVICE_ID
        )
        connection_count = await db.fetchval(
            "SELECT count(*) FROM tb_device_connections WHERE device_id=$1", DEVICE_ID
        )
        provision_count = await db.fetchval(
            "SELECT count(*) FROM tb_device_provisionings "
            "WHERE device_id=$1 AND tenant_id=$2 AND sn=$3",
            DEVICE_ID,
            TENANT_ID,
            EXPECTED_SN,
        )
        if (
            device is None
            or device["sn"] != EXPECTED_SN
            or org_id != TENANT_ID
            or provision_count != 1
        ):
            raise RuntimeError("IoT identity or historical provisioning differs")
        async with httpx.AsyncClient(
            base_url=rmq_admin._admin_url(), timeout=10
        ) as http:
            path = f"api/users/{rmq_admin._quote_path(EXPECTED_SN)}"
            user_response = await http.get(path)
            if user_response.status_code not in (200, 404):
                user_response.raise_for_status()
            print(
                f"PREFLIGHT device={DEVICE_ID} tenant={TENANT_ID} "
                f"deleted={device['is_deleted']} connections={connection_count} "
                f"mqtt_user={user_response.status_code == 200}",
                flush=True,
            )
            if os.environ.get("E2E_DEPROVISION_APPLY") != "1":
                return

            async with db.transaction():
                locked = await db.fetchrow(
                    "SELECT sn, is_deleted FROM tb_devices "
                    "WHERE device_id=$1 FOR UPDATE",
                    DEVICE_ID,
                )
                if locked is None or locked["sn"] != EXPECTED_SN:
                    raise RuntimeError("Device changed before deprovision")
                if not locked["is_deleted"]:
                    await db.execute(
                        "UPDATE tb_devices SET is_deleted=true, deleted_at=now() "
                        "WHERE device_id=$1",
                        DEVICE_ID,
                    )
                    await db.execute(
                        "INSERT INTO tb_device_audit_logs "
                        "(device_id, org_id, event_type, actor, details) "
                        "VALUES ($1,$2,'DEPROVISIONED','e2e_operator',"
                        "jsonb_build_object('reason','superseded_test_tenant'))",
                        DEVICE_ID,
                        TENANT_ID,
                    )
                await db.execute(
                    "DELETE FROM tb_device_connections WHERE device_id=$1", DEVICE_ID
                )
            print("IOT_RETIRED", flush=True)

            if user_response.status_code == 200:
                await rmq_admin.terminate_user_connections(EXPECTED_SN)
                deleted = await http.delete(path)
                if deleted.status_code not in (204, 404):
                    deleted.raise_for_status()
            verified = await http.get(path)
            if verified.status_code != 404:
                raise RuntimeError("MQTT user still exists after retirement")
            print("MQTT_USER_REMOVED", flush=True)
    finally:
        await db.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    # CLI boundary: report transport errors without an encoded-source traceback.
    except Exception as exc:  # noqa: BLE001
        print(f"FAILED {type(exc).__name__}: {str(exc).splitlines()[0][:160]}")
        raise SystemExit(1) from None
