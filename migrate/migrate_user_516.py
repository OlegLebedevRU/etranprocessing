"""Migrate user for org=516 directly from legacy MS SQL to PostgreSQL (etran).

Source: MS SQL Server 172.17.100.1, Database: Service, Table: Users, User: ai-agent
Target: PostgreSQL etran database (on 87.242.100.34)
"""

from __future__ import annotations

import argparse
import base64
import json
import logging
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("migrate_user_516")

DEFAULT_SERVER = os.environ.get("MSSQL_SERVER", "172.17.100.1")
DEFAULT_DB = os.environ.get("MSSQL_DATABASE", "Service")
DEFAULT_USER = os.environ.get("MSSQL_USER", "ai-agent")
DEFAULT_PASSWORD = os.environ.get("MSSQL_PASSWORD", "ai-agent")
DEFAULT_ORG_ID = int(os.environ.get("MIGRATE_ORG_ID", "516"))
DEFAULT_PG_DB = os.environ.get("POSTGRES_DB", "etran")
DEFAULT_PG_USER = os.environ.get("POSTGRES_USER", "etran_db_user")

DEFAULT_SSH_HOST = os.environ.get("SSH_HOST", "87.242.100.34")
DEFAULT_SSH_USER = os.environ.get("SSH_USER", "user1")
DEFAULT_SSH_KEY = os.environ.get("SSH_KEY", r"d:\.ssh\id_ed25519")


def fetch_mssql_user_data(
    server: str = DEFAULT_SERVER,
    database: str = DEFAULT_DB,
    user: str = DEFAULT_USER,
    password: str = DEFAULT_PASSWORD,
    org_id: int = DEFAULT_ORG_ID,
) -> list[dict[str, Any]]:
    """Query users from MS SQL using PowerShell System.Data.SqlClient."""
    escaped_user = user.replace("'", "''")
    escaped_pw = password.replace("'", "''")
    escaped_db = database.replace("'", "''")
    escaped_srv = server.replace("'", "''")

    ps_script = f"""
    $connStr = "Server={escaped_srv};Database={escaped_db};User Id={escaped_user};Password={escaped_pw};TrustServerCertificate=True;Connect Timeout=15;"
    $conn = New-Object System.Data.SqlClient.SqlConnection($connStr)
    $conn.Open()

    $cmd = $conn.CreateCommand()
    $cmd.CommandText = @"
        SELECT 
            u.user_id,
            u.org_id,
            u.fio,
            u.post,
            u.phone,
            u.email,
            u.login,
            u.md5pwd,
            u.pwd
        FROM Users u
        WHERE u.org_id = {org_id}
"@

    $adapter = New-Object System.Data.SqlClient.SqlDataAdapter($cmd)
    $dt = New-Object System.Data.DataTable
    [void]$adapter.Fill($dt)

    if ($dt.Rows.Count -eq 0) {{
        $cmd.CommandText = @"
            SELECT 
                u.user_id,
                uo.org_id,
                u.fio,
                u.post,
                u.phone,
                u.email,
                u.login,
                u.md5pwd,
                u.pwd
            FROM Users u
            INNER JOIN User_Orgs uo ON u.user_id = uo.user_id
            WHERE uo.org_id = {org_id}
"@
        [void]$adapter.Fill($dt)
    }}
    $conn.Close()

    $users = New-Object System.Collections.ArrayList
    foreach ($r in $dt.Rows) {{
        $item = [ordered]@{{
            user_id = [int]$r['user_id']
            org_id = [int]$r['org_id']
            fio = if ($r['fio'] -is [System.DBNull]) {{ "" }} else {{ [string]$r['fio'] }}
            post = if ($r['post'] -is [System.DBNull]) {{ "" }} else {{ [string]$r['post'] }}
            phone = if ($r['phone'] -is [System.DBNull]) {{ "" }} else {{ [string]$r['phone'] }}
            email = if ($r['email'] -is [System.DBNull]) {{ "" }} else {{ [string]$r['email'] }}
            login = if ($r['login'] -is [System.DBNull]) {{ "" }} else {{ [string]$r['login'] }}
            md5pwd = if ($r['md5pwd'] -is [System.DBNull]) {{ "" }} else {{ [string]$r['md5pwd'] }}
            pwd = if ($r['pwd'] -is [System.DBNull]) {{ "" }} else {{ [string]$r['pwd'] }}
        }}
        [void]$users.Add($item)
    }}

    $tempFile = [System.IO.Path]::GetTempFileName()
    $json = $users | ConvertTo-Json -Depth 3
    [System.IO.File]::WriteAllText($tempFile, $json, [System.Text.Encoding]::UTF8)
    Write-Output $tempFile
    """

    encoded_script = base64.b64encode(ps_script.encode("utf-16le")).decode("ascii")

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

    if isinstance(data, dict):
        return [data]
    return data if isinstance(data, list) else []


def execute_postgres_sql(
    sql: str,
    ssh_host: str = DEFAULT_SSH_HOST,
    ssh_user: str = DEFAULT_SSH_USER,
    ssh_key: str = DEFAULT_SSH_KEY,
    database: str = DEFAULT_PG_DB,
    pg_user: str = DEFAULT_PG_USER,
) -> str:
    """Execute raw SQL in PostgreSQL on the remote server via SSH docker exec."""
    b64_sql = base64.b64encode(sql.encode("utf-8")).decode("ascii")
    remote_cmd = (
        f"echo {b64_sql} | base64 -d | "
        f"sudo docker exec -i iot-rpc-rest-app-pg-1 psql -U {pg_user} -d {database} -q -t"
    )
    ssh_args = [
        "ssh",
        "-n",
        "-i",
        ssh_key,
        "-o",
        "StrictHostKeyChecking=no",
        f"{ssh_user}@{ssh_host}",
        remote_cmd,
    ]
    res = subprocess.run(
        ssh_args,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=True,
    )
    return res.stdout


def fetch_postgres_state(
    org_id: int = DEFAULT_ORG_ID,
    user_id: int | None = None,
    username: str | None = None,
    ssh_host: str = DEFAULT_SSH_HOST,
    ssh_user: str = DEFAULT_SSH_USER,
    ssh_key: str = DEFAULT_SSH_KEY,
    database: str = DEFAULT_PG_DB,
    pg_user: str = DEFAULT_PG_USER,
) -> dict[str, Any]:
    """Fetch user and org state from PostgreSQL."""
    clauses = [f"org_id = {org_id}"]
    if user_id is not None:
        clauses.append(f"id = {user_id}")
    if username:
        escaped_u = username.replace("'", "''")
        clauses.append(f"username = '{escaped_u}'")
    where_cond = " OR ".join(clauses)

    sql = f"""
    SELECT json_build_object(
        'org', (
            SELECT row_to_json(o) FROM (
                SELECT org_id, org_name, name, status, is_active FROM orgs WHERE org_id = {org_id}
            ) o
        ),
        'users', COALESCE((
            SELECT json_agg(row_to_json(u)) FROM (
                SELECT id, org_id, username, md5_password, role_id, role, is_superuser, is_active, full_name, last_org_id 
                FROM users 
                WHERE {where_cond}
            ) u
        ), '[]'::json)
    );
    """
    out = execute_postgres_sql(
        sql,
        ssh_host=ssh_host,
        ssh_user=ssh_user,
        ssh_key=ssh_key,
        database=database,
        pg_user=pg_user,
    )
    # The output from psql contains header/footer, parse JSON from the body
    lines = [
        line.strip()
        for line in out.splitlines()
        if line.strip() and not line.startswith("-") and line != "json_build_object"
    ]
    raw_json = "".join(lines)
    return json.loads(raw_json)


def compute_dirty_diff(
    ms_user: dict[str, Any],
    pg_users: list[dict[str, Any]],
) -> dict[str, Any]:
    """Compute difference between legacy MSSQL user and PostgreSQL user."""
    ms_id = ms_user["user_id"]
    ms_login = ms_user["login"]
    ms_md5 = ms_user["md5pwd"].strip().lower()
    ms_fio = ms_user["fio"]
    ms_org = ms_user["org_id"]

    matched_pg = None
    for u in pg_users:
        if u.get("id") == ms_id or u.get("username") == ms_login:
            matched_pg = u
            break

    diff: dict[str, Any] = {
        "exists_in_pg": matched_pg is not None,
        "matched_pg": matched_pg,
        "discrepancies": [],
        "target_user": {
            "id": ms_id,
            "org_id": ms_org,
            "username": ms_login,
            "md5_password": ms_md5,
            "role_id": 3,
            "role": "user",
            "is_superuser": False,
            "is_active": True,
            "full_name": ms_fio or ms_login,
            "last_org_id": ms_org,
        },
    }

    if not matched_pg:
        diff["discrepancies"].append("Пользователь отсутствует в базе данных etran")
    else:
        if matched_pg.get("id") != ms_id:
            diff["discrepancies"].append(
                f"ID не совпадает: PG={matched_pg.get('id')} vs MSSQL={ms_id}"
            )
        if matched_pg.get("username") != ms_login:
            diff["discrepancies"].append(
                f"Логин не совпадает: PG='{matched_pg.get('username')}' vs MSSQL='{ms_login}'"
            )
        if (matched_pg.get("md5_password") or "").lower() != ms_md5:
            diff["discrepancies"].append(
                f"MD5 пароль не совпадает: PG='{matched_pg.get('md5_password')}' vs MSSQL='{ms_md5}'"
            )
        if matched_pg.get("org_id") != ms_org:
            diff["discrepancies"].append(
                f"Организация не совпадает: PG={matched_pg.get('org_id')} vs MSSQL={ms_org}"
            )
        if not matched_pg.get("is_active"):
            diff["discrepancies"].append(
                "Пользователь в PG деактивирован (is_active=False)"
            )

    return diff


def apply_migration(
    user_data: dict[str, Any],
    ssh_host: str = DEFAULT_SSH_HOST,
    ssh_user: str = DEFAULT_SSH_USER,
    ssh_key: str = DEFAULT_SSH_KEY,
    database: str = DEFAULT_PG_DB,
    pg_user: str = DEFAULT_PG_USER,
) -> None:
    """Apply direct user migration into PostgreSQL."""
    org_id = user_data["org_id"]
    user_id = user_data["id"]
    username = user_data["username"].replace("'", "''")
    md5_password = user_data["md5_password"].replace("'", "''")
    role_id = user_data["role_id"]
    role = user_data["role"].replace("'", "''")
    is_su = "TRUE" if user_data["is_superuser"] else "FALSE"
    is_active = "TRUE" if user_data["is_active"] else "FALSE"
    full_name = (user_data["full_name"] or username).replace("'", "''")
    last_org = user_data["last_org_id"]

    sql = f"""
    BEGIN;

    -- 1. Ensure target organization exists
    INSERT INTO orgs (org_id, org_name, name, status, is_active)
    VALUES ({org_id}, 'Организация {org_id}', 'Организация {org_id}', 1, TRUE)
    ON CONFLICT (org_id) DO NOTHING;

    -- 2. Insert or update user
    INSERT INTO users (
        id,
        org_id,
        username,
        md5_password,
        role_id,
        role,
        is_superuser,
        is_active,
        full_name,
        last_org_id
    ) VALUES (
        {user_id},
        {org_id},
        '{username}',
        '{md5_password}',
        {role_id},
        '{role}',
        {is_su},
        {is_active},
        '{full_name}',
        {last_org}
    )
    ON CONFLICT (id) DO UPDATE SET
        username = EXCLUDED.username,
        md5_password = EXCLUDED.md5_password,
        org_id = EXCLUDED.org_id,
        role_id = EXCLUDED.role_id,
        role = EXCLUDED.role,
        is_superuser = EXCLUDED.is_superuser,
        is_active = EXCLUDED.is_active,
        full_name = EXCLUDED.full_name,
        last_org_id = EXCLUDED.last_org_id;

    -- 3. Sync sequence
    SELECT setval(pg_get_serial_sequence('users', 'id'), GREATEST((SELECT max(id) FROM users), 1));

    COMMIT;
    """
    execute_postgres_sql(
        sql,
        ssh_host=ssh_host,
        ssh_user=ssh_user,
        ssh_key=ssh_key,
        database=database,
        pg_user=pg_user,
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Direct user migration from legacy MSSQL to PostgreSQL (etran) for org=516."
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
    parser.add_argument(
        "--org-id",
        type=int,
        default=DEFAULT_ORG_ID,
        help=f"Target organization ID (default: {DEFAULT_ORG_ID})",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Display diff and target data without modifying PostgreSQL",
    )
    parser.add_argument(
        "--ssh-host",
        type=str,
        default=DEFAULT_SSH_HOST,
        help=f"SSH host (default: {DEFAULT_SSH_HOST})",
    )
    parser.add_argument(
        "--ssh-user",
        type=str,
        default=DEFAULT_SSH_USER,
        help=f"SSH user (default: {DEFAULT_SSH_USER})",
    )
    parser.add_argument(
        "--ssh-key",
        type=str,
        default=DEFAULT_SSH_KEY,
        help=f"SSH key path (default: {DEFAULT_SSH_KEY})",
    )
    parser.add_argument(
        "--pg-db",
        type=str,
        default=DEFAULT_PG_DB,
        help=f"PostgreSQL database name (default: {DEFAULT_PG_DB})",
    )
    parser.add_argument(
        "--pg-user",
        type=str,
        default=DEFAULT_PG_USER,
        help=f"PostgreSQL user (default: {DEFAULT_PG_USER})",
    )
    args = parser.parse_args()

    print("=" * 80)
    print(f"МИГРАЦИЯ ПОЛЬЗОВАТЕЛЯ ИЗ MS SQL В ETRAN (ОРГАНИЗАЦИЯ {args.org_id})")
    print("=" * 80)

    logger.info(
        "Шаг 1: Извлечение данных пользователя из MS SQL (%s, БД %s)...",
        args.server,
        args.database,
    )
    ms_users = fetch_mssql_user_data(
        server=args.server,
        database=args.database,
        user=args.user,
        password=args.password,
        org_id=args.org_id,
    )
    if not ms_users:
        logger.error("Пользователь для org_id=%d не найден в MS SQL!", args.org_id)
        sys.exit(1)

    ms_user = ms_users[0]
    logger.info(
        "Найдена запись в MS SQL: user_id=%d, login='%s', fio='%s', md5pwd='%s', org_id=%d",
        ms_user["user_id"],
        ms_user["login"],
        ms_user["fio"],
        ms_user["md5pwd"],
        ms_user["org_id"],
    )

    logger.info("Шаг 2: Получение текущего состояния из PostgreSQL (etran)...")
    pg_state = fetch_postgres_state(
        org_id=args.org_id,
        user_id=ms_user["user_id"],
        username=ms_user["login"],
        ssh_host=args.ssh_host,
        ssh_user=args.ssh_user,
        ssh_key=args.ssh_key,
        database=args.pg_db,
        pg_user=args.pg_user,
    )
    pg_users = pg_state.get("users", [])
    pg_org = pg_state.get("org")

    logger.info("Шаг 3: Вычисление dirty diff...")
    diff = compute_dirty_diff(ms_user, pg_users)

    print("\n" + "-" * 80)
    print("АНАЛИЗ И DIRTY DIFF:")
    print("-" * 80)
    print(
        f"Организация {args.org_id} в PostgreSQL: {'Существует' if pg_org else 'ОТСУТСТВУЕТ (будет создана)'}"
    )
    print(
        f"Пользователь в MS SQL: user_id={ms_user['user_id']}, login={ms_user['login']}, fio={ms_user['fio']}"
    )
    print(f"Пользователь в PostgreSQL: {pg_users[0] if pg_users else 'НЕ НАЙДЕН'}")
    print(f"Расхождения ({len(diff['discrepancies'])}):")
    for d in diff["discrepancies"]:
        print(f"  * {d}")

    target = diff["target_user"]
    print("\nЦелевая запись для вставки в PostgreSQL (etran.users):")
    for k, v in target.items():
        print(f"  {k}: {v}")
    print("-" * 80)

    if args.dry_run:
        logger.info("Режим DRY-RUN: изменения не вносились.")
        return

    if not diff["discrepancies"]:
        logger.info(
            "Пользователь уже синхронизирован с PostgreSQL. Изменения не требуются."
        )
    else:
        logger.info(
            "Шаг 4: Применение прямого переноса пользователя в PostgreSQL (etran)..."
        )
        apply_migration(
            user_data=target,
            ssh_host=args.ssh_host,
            ssh_user=args.ssh_user,
            ssh_key=args.ssh_key,
            database=args.pg_db,
            pg_user=args.pg_user,
        )
        logger.info("Перенос успешно выполнен.")

    logger.info("Шаг 5: Верификация обновленного состояния в PostgreSQL (etran)...")
    updated_state = fetch_postgres_state(
        org_id=args.org_id,
        user_id=ms_user["user_id"],
        username=ms_user["login"],
        ssh_host=args.ssh_host,
        ssh_user=args.ssh_user,
        ssh_key=args.ssh_key,
        database=args.pg_db,
        pg_user=args.pg_user,
    )
    updated_users = updated_state.get("users", [])
    updated_diff = compute_dirty_diff(ms_user, updated_users)

    print("\n" + "=" * 80)
    print("РЕЗУЛЬТАТ ВЕРИФИКАЦИИ В POSTGRESQL (ETRAN):")
    print("=" * 80)
    if updated_users:
        u = updated_users[0]
        print(f"ID:              {u['id']}")
        print(f"Username:        {u['username']}")
        print(f"Full Name:       {u['full_name']}")
        print(f"Org ID:          {u['org_id']}")
        print(f"Role:            {u['role']} (role_id={u['role_id']})")
        print(f"Is Superuser:    {u['is_superuser']}")
        print(f"Is Active:       {u['is_active']}")
        print(f"MD5 Password:    {u['md5_password']}")
        print(f"Last Org ID:     {u['last_org_id']}")

        # Validate password hash matches MSSQL
        ms_expected_hash = ms_user["md5pwd"].strip().lower()
        pwd_matches = u["md5_password"].lower() == ms_expected_hash
        print(
            f"Сверка MD5 хеша с легаси MSSQL ({u['md5_password']}): {'СОВПАДАЕТ' if pwd_matches else 'РАСХОЖДЕНИЕ'}"
        )

        if not updated_diff["discrepancies"]:
            print(
                "\n СТАТУС: УСПЕШНО! Пользователь полностью перенесен и готов к работе в etran."
            )
        else:
            print(f"\n ОШИБКА: Остались расхождения: {updated_diff['discrepancies']}")
    else:
        print(
            "\n КРИТИЧЕСКАЯ ОШИБКА: Пользователь не обнаружен в PostgreSQL после переноса!"
        )
        sys.exit(1)
    print("=" * 80)


if __name__ == "__main__":
    main()
