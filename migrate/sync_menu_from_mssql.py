"""Sync legacy menu tree and services from MS SQL (Service DB) to PostgreSQL.

Network topology note:
MS SQL (172.17.100.1) is in a protected internal network accessible from local environment.
Target cloud backend (87.242.100.34) does not have direct route to 172.17.100.1.
This script extracts from MS SQL, formats/validates data, and applies locally or deploys to cloud server.
"""

import argparse
import base64
import json
import logging
import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

if sys.platform == "win32":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("sync_menu_mssql")

DEFAULT_SERVER = os.getenv("MSSQL_SERVER", "172.17.100.1")
DEFAULT_DB = os.getenv("MSSQL_DATABASE", "Service")
DEFAULT_USER = os.getenv("MSSQL_USER", "ai-agent")
DEFAULT_PASSWORD = os.getenv("MSSQL_PASSWORD", "ai-agent")

PROJECT_ROOT = Path(__file__).resolve().parent.parent
LOCAL_DATA_JSON = PROJECT_ROOT / "migrate" / "legacy_menu_data.json"
BACKEND_DATA_JSON = (
    PROJECT_ROOT
    / "MenuBuilder"
    / "backend"
    / "app"
    / "services"
    / "legacy_menu_data.json"
)

REMOTE_SERVER = "user1@87.242.100.34"
REMOTE_SSH_KEY = r"d:\.ssh\id_ed25519"
REMOTE_DATA_DEST = "/home/user1/MenuBuilder/backend/app/services/legacy_menu_data.json"


def fetch_mssql_menu_data(
    server: str = DEFAULT_SERVER,
    database: str = DEFAULT_DB,
    user: str = DEFAULT_USER,
    password: str = DEFAULT_PASSWORD,
    org_ids: list[int] | None = None,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Fetch groups and services from MS SQL tb_GS_Group and tb_GS_Service via PowerShell SqlClient."""
    where_groups = ""
    if org_ids:
        org_list_str = ",".join(map(str, org_ids))
        where_groups = f"WHERE org_id IN ({org_list_str})"

    ps_script = f"""
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom
    $connStr = 'Server={server};Database={database};User Id={user};Password={password};TrustServerCertificate=True;Connect Timeout=15;'
    $conn = New-Object System.Data.SqlClient.SqlConnection($connStr)
    $conn.Open()

    $cmdG = $conn.CreateCommand()
    $cmdG.CommandText = "SELECT Id, org_id, number, name, parentId FROM tb_GS_Group {where_groups} ORDER BY org_id, Id"
    $daG = New-Object System.Data.SqlClient.SqlDataAdapter($cmdG)
    $dtG = New-Object System.Data.DataTable
    [void]$daG.Fill($dtG)

    $cmdS = $conn.CreateCommand()
    $cmdS.CommandText = @"
    SELECT s.Id, s.group_id, s.tsp_code, s.name, s.printname, s.price, s.protypenumber
    FROM tb_GS_Service s
    INNER JOIN tb_GS_Group g ON s.group_id = g.Id
    {where_groups}
    ORDER BY s.group_id, s.tsp_code
"@
    $daS = New-Object System.Data.SqlClient.SqlDataAdapter($cmdS)
    $dtS = New-Object System.Data.DataTable
    [void]$daS.Fill($dtS)

    $conn.Close()

    $groups = [System.Collections.ArrayList]::new()
    foreach ($r in $dtG.Rows) {{
        $item = [ordered]@{{
            Id = [int]$r['Id']
            org_id = [int]$r['org_id']
            number = [int]$r['number']
            name = [string]$r['name']
            parentId = if ($r['parentId'] -is [System.DBNull]) {{ $null }} else {{ [int]$r['parentId'] }}
        }}
        [void]$groups.Add($item)
    }}

    $services = [System.Collections.ArrayList]::new()
    foreach ($r in $dtS.Rows) {{
        $item = [ordered]@{{
            Id = [int]$r['Id']
            group_id = [int]$r['group_id']
            tsp_code = [int]$r['tsp_code']
            name = [string]$r['name']
            printname = if ($r['printname'] -is [System.DBNull]) {{ $null }} else {{ [string]$r['printname'] }}
            price = [int]$r['price']
            protypenumber = [int]$r['protypenumber']
        }}
        [void]$services.Add($item)
    }}

    $result = [ordered]@{{
        groups = $groups
        services = $services
    }}
    $tempFile = [System.IO.Path]::GetTempFileName()
    $json = $result | ConvertTo-Json -Depth 5
    [System.IO.File]::WriteAllText($tempFile, $json, [System.Text.Encoding]::UTF8)
    Write-Output $tempFile
    """

    encoded_script = base64.b64encode(ps_script.encode("utf-16le")).decode("ascii")

    logger.info("Connecting to MS SQL at %s (%s)...", server, database)
    res = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-EncodedCommand",
            encoded_script,
        ],
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=True,
    )

    temp_path = Path(res.stdout.strip().splitlines()[-1].strip())
    with open(temp_path, encoding="utf-8-sig") as f:
        data = json.load(f)
    temp_path.unlink(missing_ok=True)
    groups: list[dict[str, Any]] = data.get("groups", [])
    services: list[dict[str, Any]] = data.get("services", [])
    return groups, services


def validate_and_analyze(
    groups: list[dict[str, Any]],
    services: list[dict[str, Any]],
) -> dict[int, dict[str, Any]]:
    """Validate tree structures and group services by organization."""
    groups_by_org: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for g in groups:
        groups_by_org[g["org_id"]].append(g)

    services_by_group: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for s in services:
        services_by_group[s["group_id"]].append(s)

    stats: dict[int, dict[str, Any]] = {}

    for org_id, org_groups in sorted(groups_by_org.items()):
        root = None
        for g in org_groups:
            if g["parentId"] is None or g["number"] == 0:
                root = g
                break
        if not root:
            root = org_groups[0]

        root_id = root["Id"]
        mapped_group_ids = {root_id, 0}
        remaining = [g for g in org_groups if g["Id"] != root_id]

        depth = 1
        iterations = 0
        while remaining and iterations < 15:
            iterations += 1
            next_remaining = []
            for g in remaining:
                p_id = 0 if g["parentId"] is None else g["parentId"]
                if p_id in mapped_group_ids:
                    mapped_group_ids.add(g["Id"])
                    if iterations + 1 > depth:
                        depth = iterations + 1
                else:
                    next_remaining.append(g)
            remaining = next_remaining

        org_services_count = sum(len(services_by_group[g["Id"]]) for g in org_groups)
        unmapped_groups = len(remaining)

        stats[org_id] = {
            "root_name": root["name"],
            "total_groups": len(org_groups),
            "total_services": org_services_count,
            "max_depth": depth,
            "unmapped_groups": unmapped_groups,
        }

    return stats


def save_dataset_files(
    groups: list[dict[str, Any]],
    services: list[dict[str, Any]],
    merge_with_existing: bool = True,
    org_ids: list[int] | None = None,
) -> None:
    """Save groups and services to JSON dataset files (migrate and MenuBuilder)."""
    final_groups = groups
    final_services = services

    if merge_with_existing and org_ids and LOCAL_DATA_JSON.exists():
        with open(LOCAL_DATA_JSON, encoding="utf-8") as f:
            existing = json.load(f)
        target_set = set(org_ids)
        kept_groups = [
            g for g in existing.get("groups", []) if g["org_id"] not in target_set
        ]
        kept_group_ids = {g["Id"] for g in kept_groups}
        kept_services = [
            s for s in existing.get("services", []) if s["group_id"] in kept_group_ids
        ]
        final_groups = kept_groups + groups
        final_services = kept_services + services

    dataset = {
        "groups": final_groups,
        "services": final_services,
    }

    for path in [LOCAL_DATA_JSON, BACKEND_DATA_JSON]:
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(dataset, f, ensure_ascii=False, indent=2)
        logger.info("Updated dataset file at %s", path)


def deploy_and_run_remote(org_ids: list[int] | None = None) -> None:
    """Upload dataset to remote cloud app server and execute import."""
    logger.info(
        "Uploading %s to %s:%s...", BACKEND_DATA_JSON, REMOTE_SERVER, REMOTE_DATA_DEST
    )
    scp_cmd = [
        "scp",
        "-i",
        REMOTE_SSH_KEY,
        "-o",
        "ConnectTimeout=30",
        "-o",
        "StrictHostKeyChecking=no",
        str(BACKEND_DATA_JSON),
        f"{REMOTE_SERVER}:{REMOTE_DATA_DEST}",
    ]
    subprocess.run(scp_cmd, check=True)

    org_arg = ""
    if org_ids:
        org_arg = f"--orgs {','.join(map(str, org_ids))}"

    logger.info("Triggering remote import inside menubuilder-backend container...")
    ssh_cmd = [
        "ssh",
        "-n",
        "-i",
        REMOTE_SSH_KEY,
        "-o",
        "ConnectTimeout=30",
        "-o",
        "StrictHostKeyChecking=no",
        REMOTE_SERVER,
        f"sudo docker exec menubuilder-backend python -m app.services.legacy_menu {org_arg}",
    ]
    subprocess.run(ssh_cmd, check=True)
    logger.info("Remote import execution completed successfully.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Sync legacy menu trees and services from MS SQL to PostgreSQL."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--org",
        type=int,
        help="Single organization ID to sync (e.g. --org 424)",
    )
    group.add_argument(
        "--orgs",
        type=str,
        help="Comma-separated list of organization IDs (e.g. --orgs 1,424,477)",
    )
    group.add_argument(
        "--all",
        action="store_true",
        help="Sync all organizations present in MS SQL",
    )

    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Fetch and validate menu structure without modifying files or databases",
    )
    parser.add_argument(
        "--deploy",
        action="store_true",
        help="Upload extracted data to cloud server and execute import in PostgreSQL",
    )
    parser.add_argument(
        "--server",
        type=str,
        default=DEFAULT_SERVER,
        help=f"MS SQL Server host (default: {DEFAULT_SERVER})",
    )
    parser.add_argument(
        "--database",
        type=str,
        default=DEFAULT_DB,
        help=f"MS SQL Database name (default: {DEFAULT_DB})",
    )
    parser.add_argument(
        "--user",
        type=str,
        default=DEFAULT_USER,
        help=f"MS SQL User (default: {DEFAULT_USER})",
    )
    parser.add_argument(
        "--password",
        type=str,
        default=DEFAULT_PASSWORD,
        help="MS SQL Password",
    )

    args = parser.parse_args()

    target_orgs: list[int] | None = None
    if args.org:
        target_orgs = [args.org]
    elif args.orgs:
        target_orgs = [int(o.strip()) for o in args.orgs.split(",") if o.strip()]

    try:
        groups, services = fetch_mssql_menu_data(
            server=args.server,
            database=args.database,
            user=args.user,
            password=args.password,
            org_ids=target_orgs,
        )
    except Exception as e:
        logger.error("Failed to fetch data from MS SQL: %s", e)
        sys.exit(1)

    logger.info(
        "Extracted %d groups and %d services from MS SQL.",
        len(groups),
        len(services),
    )

    stats = validate_and_analyze(groups, services)
    logger.info("Summary of extracted organizations (%d total):", len(stats))
    for org_id, info in stats.items():
        logger.info(
            "  Org %4d | Root: '%s' | Groups: %3d | Services: %4d | Max depth: %d | Unmapped: %d",
            org_id,
            info["root_name"],
            info["total_groups"],
            info["total_services"],
            info["max_depth"],
            info["unmapped_groups"],
        )

    if args.dry_run:
        logger.info("DRY-RUN completed. No files or database records were changed.")
        return

    save_dataset_files(
        groups=groups,
        services=services,
        merge_with_existing=(target_orgs is not None),
        org_ids=target_orgs,
    )

    if args.deploy:
        deploy_and_run_remote(org_ids=target_orgs)
    else:
        logger.info(
            "Data saved locally. Use --deploy to upload and apply to cloud server, "
            "or run `python -m app.services.legacy_menu`."
        )


if __name__ == "__main__":
    main()
