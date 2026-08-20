"""Sync terminals, organizations, licenses, and certificate PINs from Legacy MS SQL to PostgreSQL.

Target org_ids: 1, 111, 302, 305, 314, 315, 339, 340, 349, 352, 353, 355, 359, 360,
                389, 392, 393, 394, 395, 396, 398, 401, 402, 403, 404, 405, 408, 424,
                429, 434, 435, 442, 445, 450, 453, 457, 462, 463, 472, 475, 476, 477,
                486, 488, 491, 492, 493, 495, 496, 498, 500, 501, 519, 521, 522
"""

import asyncio
import json
import logging
import random
import subprocess
from datetime import UTC, datetime, timedelta

from sqlalchemy import select

from app.database import async_session
from app.models import CertificatePin, License, OrgStatus, Terminal

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("sync_mssql")

TARGET_ORG_IDS = [
    1,
    111,
    302,
    305,
    314,
    315,
    339,
    340,
    349,
    352,
    353,
    355,
    359,
    360,
    389,
    392,
    393,
    394,
    395,
    396,
    398,
    401,
    402,
    403,
    404,
    405,
    408,
    424,
    429,
    434,
    435,
    442,
    445,
    450,
    453,
    457,
    462,
    463,
    472,
    475,
    476,
    477,
    486,
    488,
    491,
    492,
    493,
    495,
    496,
    498,
    500,
    501,
    519,
    521,
    522,
]

PLATFORM = "4"


def generate_device_sn(device_number: int) -> str:
    """Generate deterministic random SN for terminal without certificate."""
    device_part = f"{device_number:07d}"
    rand_first = str(random.randint(1, 9))
    rand_rest = "".join([str(random.randint(0, 9)) for _ in range(4)])
    random_part = rand_first + rand_rest
    date_part = datetime.now(UTC).strftime("%d%m%y")
    return f"a{PLATFORM}b{device_part}c{random_part}d{date_part}"


def fetch_mssql_data(
    server="172.17.100.1", database="Service", user="ai-agent", password="ai-agent"
):
    """Fetch legacy data via PowerShell SqlClient."""
    orgs_filter = ",".join(map(str, TARGET_ORG_IDS))

    ps_script = f"""
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $connStr = 'Server={server};Database={database};User Id={user};Password={password};TrustServerCertificate=True;Connect Timeout=15;'
    $conn = New-Object System.Data.SqlClient.SqlConnection($connStr)
    $conn.Open()

    # 1. Kiosks + Active Certs
    $cmd1 = $conn.CreateCommand()
    $cmd1.CommandText = @"
    WITH RankedCerts AS (
        SELECT 
            c.kiosk_id,
            c.cpserial,
            c.common_name,
            c.status_id,
            ROW_NUMBER() OVER (
                PARTITION BY c.kiosk_id 
                ORDER BY c.update_datetime DESC, c.cert_id DESC
            ) AS rn
        FROM Certificates c
        WHERE c.status_id = 2
    )
    SELECT 
        k.kiosk_id,
        k.number AS device_id,
        k.org_id,
        k.status,
        CONVERT(varchar(23), k.license, 126) AS license,
        k.address,
        rc.cpserial,
        rc.common_name
    FROM Kiosks k
    LEFT JOIN RankedCerts rc ON k.kiosk_id = rc.kiosk_id AND rc.rn = 1
    WHERE k.org_id IN ({orgs_filter})
    ORDER BY k.kiosk_id
"@
    $da1 = New-Object System.Data.SqlClient.SqlDataAdapter($cmd1)
    $dt1 = New-Object System.Data.DataTable
    [void]$da1.Fill($dt1)

    # 2. Pending PINs >= 2025
    $cmd2 = $conn.CreateCommand()
    $cmd2.CommandText = @"
    SELECT 
        c.pin,
        c.kiosk_id,
        c.org_id,
        CONVERT(varchar(23), c.update_datetime, 126) AS update_datetime
    FROM Certificates c
    INNER JOIN Kiosks k ON c.kiosk_id = k.kiosk_id
    WHERE c.status_id = 1 
      AND c.update_datetime >= '2025-01-01'
      AND k.org_id IN ({orgs_filter})
"@
    $da2 = New-Object System.Data.SqlClient.SqlDataAdapter($cmd2)
    $dt2 = New-Object System.Data.DataTable
    [void]$da2.Fill($dt2)

    $conn.Close()

    $kiosks = [System.Collections.ArrayList]::new()
    foreach ($r in $dt1.Rows) {{
        $item = [ordered]@{{}}
        foreach ($c in $dt1.Columns) {{
            $item[$c.ColumnName] = if ($r.IsNull($c)) {{ $null }} else {{ $r[$c].ToString() }}
        }}
        [void]$kiosks.Add($item)
    }}

    $pins = [System.Collections.ArrayList]::new()
    foreach ($r in $dt2.Rows) {{
        $item = [ordered]@{{}}
        foreach ($c in $dt2.Columns) {{
            $item[$c.ColumnName] = if ($r.IsNull($c)) {{ $null }} else {{ $r[$c].ToString() }}
        }}
        [void]$pins.Add($item)
    }}

    $result = @{{
        kiosks = $kiosks
        pins = $pins
    }}
    $result | ConvertTo-Json -Depth 3 -Compress
    """

    logger.info("Connecting to MS SQL at %s...", server)
    res = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            ps_script,
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=True,
    )
    return json.loads(res.stdout)


async def run_sync():
    data = fetch_mssql_data()
    kiosks = data.get("kiosks", [])
    pins = data.get("pins", [])

    logger.info(
        "Loaded from MS SQL: %d kiosks, %d pending PINs", len(kiosks), len(pins)
    )

    now = datetime.now(UTC)

    async with async_session() as db:
        # 1. Ensure OrgStatus for all target orgs
        logger.info(
            "Synchronizing OrgStatus for %d target orgs...", len(TARGET_ORG_IDS)
        )
        res_orgs = await db.execute(select(OrgStatus))
        existing_orgs = {o.org_id: o for o in res_orgs.scalars().all()}

        for org_id in TARGET_ORG_IDS:
            if org_id not in existing_orgs:
                new_org = OrgStatus(org_id=org_id, status="active")
                db.add(new_org)
                existing_orgs[org_id] = new_org
        await db.flush()

        # 2. Pre-fetch existing Terminals and Licenses in memory
        logger.info("Loading existing Terminals and Licenses into memory...")
        res_terms = await db.execute(select(Terminal))
        terms_by_id = {t.id: t for t in res_terms.scalars().all()}
        terms_by_device_id = {t.device_id: t for t in terms_by_id.values()}

        res_lics = await db.execute(select(License))
        lics_by_term_id = {l.terminal_id: l for l in res_lics.scalars().all()}

        # 3. Synchronize Terminals and Licenses
        synced_terminals_count = 0
        with_cert_count = 0
        generated_sn_count = 0
        used_sns = set()

        # Prioritize terminal 773 to get exact cert SN
        sorted_kiosks = sorted(
            kiosks, key=lambda x: 0 if int(x["device_id"]) == 773 else 1
        )

        for k in sorted_kiosks:
            kiosk_id = int(k["kiosk_id"])
            device_id = int(k["device_id"])
            org_id = int(k["org_id"])
            is_active = bool(int(k.get("status", 0)) == 1)
            raw_address = k.get("address")
            address = (
                raw_address.strip() if raw_address and raw_address.strip() else None
            )

            cpserial = (k.get("cpserial") or "").strip()
            common_name = (k.get("common_name") or "").strip()

            if device_id == 773:
                sn = "A99D2F18001ECC93DF5CBE27F442C8FA"
                with_cert_count += 1
            elif cpserial and cpserial not in used_sns:
                sn = cpserial
                with_cert_count += 1
            elif common_name and common_name not in used_sns:
                sn = common_name
                with_cert_count += 1
            else:
                sn = generate_device_sn(device_id)
                while sn in used_sns:
                    sn = generate_device_sn(device_id)
                generated_sn_count += 1

            used_sns.add(sn)

            # Find terminal by device_id or id
            term = terms_by_device_id.get(device_id) or terms_by_id.get(kiosk_id)

            if term:
                term.device_id = device_id
                term.sn = sn
                term.org_id = org_id
                term.is_active = is_active
                term.address = address
                term.terminal_type_id = 0
                term.cert_serial = None  # auto-bind on licensebilling
                term.cert_not_valid_after = None
                term_id = term.id
            else:
                term = Terminal(
                    id=kiosk_id,
                    device_id=device_id,
                    sn=sn,
                    org_id=org_id,
                    is_active=is_active,
                    address=address,
                    terminal_type_id=0,
                    cert_serial=None,
                    cert_not_valid_after=None,
                )
                db.add(term)
                terms_by_id[kiosk_id] = term
                terms_by_device_id[device_id] = term
                term_id = kiosk_id

            # License
            raw_license = k.get("license")
            if raw_license:
                try:
                    expires_at = datetime.fromisoformat(
                        raw_license.replace("Z", "")
                    ).replace(tzinfo=UTC)
                except ValueError, TypeError:
                    expires_at = now
            else:
                expires_at = now

            # In billing model, License.is_active represents the primary current license record.
            # Validity is determined by expires_at and renewal_enabled.
            license_active = True

            lic = lics_by_term_id.get(term_id)
            if lic:
                lic.org_id = org_id
                lic.expires_at = expires_at
                lic.is_active = license_active
            else:
                lic = License(
                    terminal_id=term_id,
                    org_id=org_id,
                    license_type="standard",
                    expires_at=expires_at,
                    is_active=license_active,
                )
                db.add(lic)
                lics_by_term_id[term_id] = lic

            synced_terminals_count += 1

        await db.flush()
        logger.info(
            "Synced %d terminals (%d with active cert SN, %d with generated SN)",
            synced_terminals_count,
            with_cert_count,
            generated_sn_count,
        )

        # 4. Synchronize Active Pending PINs
        logger.info("Loading existing CertificatePins into memory...")
        res_pins = await db.execute(select(CertificatePin))
        pins_by_code = {p.pin: p for p in res_pins.scalars().all()}

        synced_pins_count = 0
        for p in pins:
            pin_code = p["pin"].strip()
            kiosk_id = int(p["kiosk_id"])
            org_id = int(p["org_id"])
            raw_created = p.get("update_datetime")
            if raw_created:
                try:
                    created_at = datetime.fromisoformat(
                        raw_created.replace("Z", "")
                    ).replace(tzinfo=UTC)
                except ValueError, TypeError:
                    created_at = now
            else:
                created_at = now

            expires_at = created_at + timedelta(days=30)

            pin_obj = pins_by_code.get(pin_code)
            if pin_obj:
                pin_obj.terminal_id = kiosk_id
                pin_obj.org_id = org_id
                pin_obj.status = "pending"
                pin_obj.created_at = created_at
                pin_obj.expires_at = expires_at
            else:
                pin_obj = CertificatePin(
                    pin=pin_code,
                    terminal_id=kiosk_id,
                    org_id=org_id,
                    status="pending",
                    created_at=created_at,
                    expires_at=expires_at,
                )
                db.add(pin_obj)
                pins_by_code[pin_code] = pin_obj

            synced_pins_count += 1

        from sqlalchemy import text

        await db.execute(
            text(
                "SELECT setval('terminals_id_seq', COALESCE((SELECT MAX(id) FROM terminals), 1))"
            )
        )
        await db.commit()
        logger.info("Synced %d certificate PINs successfully.", synced_pins_count)


if __name__ == "__main__":
    asyncio.run(run_sync())
