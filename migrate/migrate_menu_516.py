"""One-time migration script for menu of org_id = 516 from legacy MSSQL to PostgreSQL (etran).

Rules:
1. Extracts menu tree (groups & services) for org_id=516 from MS SQL (172.17.100.1).
2. Fetches current PostgreSQL state for org_id=516.
3. Computes and displays DIRTY DIFF (list of what differs) BEFORE applying any updates.
4. If discrepancies exist, updates the legacy menu following versioning rules:
   - Increments MenuVariant.version (e.g. 1 -> 2).
   - Updates groups and services to match MSSQL (in-place reconciliation).
   - Preserves historical snapshots and creates a new MenuVariantSnapshot for the new version.
   - Updates local and remote legacy_menu_data.json datasets.
5. Verifies the updated state in PostgreSQL.
"""

import argparse
import base64
import json
import logging
import subprocess
from typing import Any

from migrate.sync_menu_from_mssql import (
    BACKEND_DATA_JSON,
    DEFAULT_DB,
    DEFAULT_PASSWORD,
    DEFAULT_SERVER,
    DEFAULT_USER,
    REMOTE_SERVER,
    REMOTE_SSH_KEY,
    fetch_mssql_menu_data,
    save_dataset_files,
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("migrate_menu_516")

ORG_ID = 516


def fetch_postgres_state(org_id: int) -> dict[str, Any]:
    """Fetch current variant, groups, services, and snapshots from PostgreSQL over SSH."""
    pg_dump_code = f"""
import asyncio, json
from sqlalchemy import select
from app.database import async_session
from app.models import MenuVariant, Group, Service, MenuVariantSnapshot

async def dump():
    async with async_session() as s:
        v = (await s.execute(select(MenuVariant).where(MenuVariant.org_id == {org_id}))).scalar_one_or_none()
        if not v:
            print(json.dumps({{"variant": None, "groups": [], "services": [], "snapshots": []}}))
            return
        g = (await s.execute(select(Group).where(Group.menu_variant_id == v.id).order_by(Group.number))).scalars().all()
        sv = (await s.execute(select(Service).where(Service.menu_variant_id == v.id).order_by(Service.tsp_code))).scalars().all()
        sn = (await s.execute(select(MenuVariantSnapshot).where(MenuVariantSnapshot.menu_variant_id == v.id).order_by(MenuVariantSnapshot.version))).scalars().all()
        data = {{
            "variant": {{"id": v.id, "name": v.name, "version": v.version, "org_id": v.org_id}},
            "groups": [{{"id": x.id, "number": x.number, "name": x.name, "parent_id": x.parent_id, "org_id": x.org_id}} for x in g],
            "services": [{{"id": x.id, "group_id": x.group_id, "tsp_code": x.tsp_code, "name": x.name, "printname": x.printname, "price": x.price, "protypenumber": x.protypenumber}} for x in sv],
            "snapshots": [{{"version": x.version, "created_at": str(x.created_at)}} for x in sn]
        }}
        print(json.dumps(data, ensure_ascii=False))

asyncio.run(dump())
"""
    b64 = base64.b64encode(pg_dump_code.encode("utf-8")).decode("ascii")
    ssh_cmd = [
        "ssh",
        "-n",
        "-i",
        REMOTE_SSH_KEY,
        "-o",
        "StrictHostKeyChecking=no",
        REMOTE_SERVER,
        f"echo {b64} | base64 -d | sudo docker exec -i menubuilder-backend python",
    ]
    res = subprocess.run(
        ssh_cmd, capture_output=True, text=True, encoding="utf-8", check=True
    )
    return json.loads(res.stdout.strip())


def compute_dirty_diff(
    ms_groups: list[dict],
    ms_services: list[dict],
    pg_state: dict[str, Any],
) -> dict[str, Any]:
    """Compute detailed dirty diff between MSSQL and PostgreSQL."""
    pg_variant = pg_state.get("variant")
    pg_groups = pg_state.get("groups", [])
    pg_services = pg_state.get("services", [])

    diffs = {
        "variant": [],
        "groups_added": [],
        "groups_removed": [],
        "groups_modified": [],
        "services_added": [],
        "services_removed": [],
        "services_modified": [],
    }

    if not pg_variant:
        diffs["variant"].append("MenuVariant does not exist in PostgreSQL")
        return diffs

    ms_root = next(
        (g for g in ms_groups if g["parentId"] is None or g["number"] == 0),
        ms_groups[0],
    )
    ms_groups_by_id = {g["Id"]: g for g in ms_groups}
    ms_groups_by_num = {g["number"]: g for g in ms_groups}

    pg_groups_by_id = {g["id"]: g for g in pg_groups}
    pg_groups_by_num = {g["number"]: g for g in pg_groups}

    ms_nums = set(ms_groups_by_num.keys())
    pg_nums = set(pg_groups_by_num.keys())

    for num in sorted(ms_nums - pg_nums):
        diffs["groups_added"].append(ms_groups_by_num[num])

    for num in sorted(pg_nums - ms_nums):
        diffs["groups_removed"].append(pg_groups_by_num[num])

    for num in sorted(ms_nums & pg_nums):
        mg = ms_groups_by_num[num]
        pg = pg_groups_by_num[num]
        changes = []
        if mg["name"] != pg["name"]:
            changes.append(f"название: '{pg['name']}' -> '{mg['name']}'")

        mg_parent_id = mg["parentId"]
        mg_parent_num = None
        if mg_parent_id not in (None, 0):
            mg_parent_num = ms_groups_by_id.get(mg_parent_id, {}).get("number")
        elif mg["Id"] != ms_root["Id"]:
            mg_parent_num = ms_root["number"]

        pg_parent_id = pg["parent_id"]
        pg_parent_num = None
        if pg_parent_id is not None:
            pg_parent_num = pg_groups_by_id.get(pg_parent_id, {}).get("number")

        if mg_parent_num != pg_parent_num:
            changes.append(
                f"родительская группа (номер): {pg_parent_num} -> {mg_parent_num}"
            )

        if changes:
            diffs["groups_modified"].append(
                {
                    "number": num,
                    "name": mg["name"],
                    "changes": changes,
                }
            )

    ms_services_by_tsp = {s["tsp_code"]: s for s in ms_services}
    pg_services_by_tsp = {s["tsp_code"]: s for s in pg_services}

    ms_tsps = set(ms_services_by_tsp.keys())
    pg_tsps = set(pg_services_by_tsp.keys())

    for tsp in sorted(ms_tsps - pg_tsps):
        diffs["services_added"].append(ms_services_by_tsp[tsp])

    for tsp in sorted(pg_tsps - ms_tsps):
        diffs["services_removed"].append(pg_services_by_tsp[tsp])

    for tsp in sorted(ms_tsps & pg_tsps):
        ms = ms_services_by_tsp[tsp]
        ps = pg_services_by_tsp[tsp]
        changes = []
        if ms["name"] != ps["name"]:
            changes.append(f"название: '{ps['name']}' -> '{ms['name']}'")
        if (ms["printname"] or None) != (ps["printname"] or None):
            changes.append(f"печать: '{ps['printname']}' -> '{ms['printname']}'")
        if ms["price"] != ps["price"]:
            changes.append(f"цена: {ps['price']} -> {ms['price']}")
        if ms["protypenumber"] != ps["protypenumber"]:
            changes.append(f"прототип: {ps['protypenumber']} -> {ms['protypenumber']}")

        ms_grp = ms_groups_by_id.get(ms["group_id"], {})
        pg_grp = pg_groups_by_id.get(ps["group_id"], {})
        if ms_grp.get("number") != pg_grp.get("number"):
            changes.append(
                f"номер группы: {pg_grp.get('number')} -> {ms_grp.get('number')}"
            )

        if changes:
            diffs["services_modified"].append(
                {
                    "tsp_code": tsp,
                    "service_name": ms["name"],
                    "changes": changes,
                    "postgres_price": ps["price"],
                    "mssql_price": ms["price"],
                }
            )

    return diffs


def print_dirty_variant(
    diffs: dict[str, Any], pg_variant: dict[str, Any] | None
) -> bool:
    """Print dirty variant report in human-readable format. Return True if changes exist."""
    print("=" * 80)
    print(
        "DIRTY ВАРИАНТ — СПИСОК НЕСОВПАДЕНИЙ МЕЖДУ ЛЕГАСИ MSSQL И БД ETRAN (ОРГ = 516)"
    )
    print("=" * 80)
    if pg_variant:
        print(
            f"Текущий вариант в PostgreSQL: ID={pg_variant['id']}, Название='{pg_variant['name']}', Версия={pg_variant['version']}"
        )
    else:
        print("Текущий вариант в PostgreSQL: ОТСУТСТВУЕТ")

    has_changes = False

    if diffs["variant"]:
        has_changes = True
        print("\n[ВАРИАНТ МЕНЮ]:")
        for v in diffs["variant"]:
            print(f"  * {v}")

    if diffs["groups_added"]:
        has_changes = True
        print(f"\n[ГРУППЫ: ДОБАВЛЕНЫ В MSSQL ({len(diffs['groups_added'])} шт.)]:")
        for g in diffs["groups_added"]:
            print(f"  + Номер {g['number']}: {g['name']}")

    if diffs["groups_removed"]:
        has_changes = True
        print(f"\n[ГРУППЫ: УДАЛЕНЫ В MSSQL ({len(diffs['groups_removed'])} шт.)]:")
        for g in diffs["groups_removed"]:
            print(f"  - Номер {g['number']}: {g['name']}")

    if diffs["groups_modified"]:
        has_changes = True
        print(f"\n[ГРУППЫ: ИЗМЕНЕНЫ ({len(diffs['groups_modified'])} шт.)]:")
        for g in diffs["groups_modified"]:
            print(f"  ~ Номер {g['number']} ({g['name']}):")
            for c in g["changes"]:
                print(f"      - {c}")

    if diffs["services_added"]:
        has_changes = True
        print(f"\n[УСЛУГИ: ДОБАВЛЕНЫ В MSSQL ({len(diffs['services_added'])} шт.)]:")
        for s in diffs["services_added"]:
            print(f"  + TSP {s['tsp_code']}: {s['name']} (цена: {s['price']})")

    if diffs["services_removed"]:
        has_changes = True
        print(f"\n[УСЛУГИ: УДАЛЕНЫ В MSSQL ({len(diffs['services_removed'])} шт.)]:")
        for s in diffs["services_removed"]:
            print(f"  - TSP {s['tsp_code']}: {s['name']} (цена: {s['price']})")

    if diffs["services_modified"]:
        has_changes = True
        print(f"\n[УСЛУГИ: ИЗМЕНЕНЫ ({len(diffs['services_modified'])} шт.)]:")
        for s in diffs["services_modified"]:
            print(f"  ~ TSP {s['tsp_code']}: '{s['service_name']}'")
            for c in s["changes"]:
                print(f"      - {c}")

    if not has_changes:
        print(
            "\nНЕСОВПАДЕНИЙ НЕ ОБНАРУЖЕНО: Меню в БД etran полностью совпадает с легаси MSSQL."
        )
    else:
        print("\nИТОГО НЕСОВПАДЕНИЙ:")
        print(f"  - Измененных групп: {len(diffs['groups_modified'])}")
        print(f"  - Добавленных групп: {len(diffs['groups_added'])}")
        print(f"  - Удаленных групп: {len(diffs['groups_removed'])}")
        print(f"  - Измененных услуг: {len(diffs['services_modified'])}")
        print(f"  - Добавленных услуг: {len(diffs['services_added'])}")
        print(f"  - Удаленных услуг: {len(diffs['services_removed'])}")

    print("=" * 80)
    return has_changes


def apply_menu_update_with_versioning(
    org_id: int,
    ms_groups: list[dict],
    ms_services: list[dict],
) -> dict[str, Any]:
    """Apply menu update in PostgreSQL according to versioning rules."""
    logger.info("Applying update in PostgreSQL according to versioning rules...")

    update_payload = {
        "org_id": org_id,
        "groups": ms_groups,
        "services": ms_services,
    }
    payload_json = json.dumps(update_payload, ensure_ascii=False)
    payload_b64 = base64.b64encode(payload_json.encode("utf-8")).decode("ascii")

    pg_update_code = f"""
import asyncio, json, base64
from datetime import datetime, timezone
from sqlalchemy import select, delete, func
from app.database import async_session
from app.models import MenuVariant, Group, Service, MenuVariantSnapshot

payload = json.loads(base64.b64decode('{payload_b64}').decode('utf-8'))
org_id = payload['org_id']
ms_groups = payload['groups']
ms_services = payload['services']

def build_tree(groups_list, services_list):
    groups_by_parent = {{}}
    for g in groups_list:
        groups_by_parent.setdefault(g.parent_id, []).append(g)

    services_by_group = {{}}
    for s in services_list:
        services_by_group.setdefault(s.group_id, []).append(s)

    def build(parent_id):
        items = []
        for g in sorted(groups_by_parent.get(parent_id, []), key=lambda x: x.number):
            node = {{"name": g.name}}
            child_items = build(g.id)
            for s in sorted(services_by_group.get(g.id, []), key=lambda x: x.tsp_code):
                svc = {{
                    "name": s.name,
                    "code": s.tsp_code,
                    "prototypeid": s.protypenumber,
                }}
                if s.printname:
                    svc["printname"] = s.printname
                if s.price:
                    svc["price"] = str(s.price)
                child_items.append(svc)
            node["items"] = child_items
            items.append(node)
        return items

    return {{"name": "root", "items": build(None)}}

async def apply_update():
    async with async_session() as session:
        variant = (await session.execute(
            select(MenuVariant).where(MenuVariant.org_id == org_id)
        )).scalar_one_or_none()

        if not variant:
            variant = MenuVariant(org_id=org_id, name="Легаси меню", version=1)
            session.add(variant)
            await session.flush()
            old_version = 0
            new_version = 1
        else:
            old_version = variant.version or 1
            new_version = old_version + 1
            variant.version = new_version
            variant.updated_at = datetime.now(timezone.utc)

        # Existing groups and services in PostgreSQL
        existing_groups = (await session.execute(
            select(Group).where(Group.menu_variant_id == variant.id)
        )).scalars().all()
        existing_groups_by_num = {{g.number: g for g in existing_groups}}

        existing_services = (await session.execute(
            select(Service).where(Service.menu_variant_id == variant.id)
        )).scalars().all()
        existing_services_by_tsp = {{s.tsp_code: s for s in existing_services}}

        # 1. Reconcile groups
        ms_id_to_num = {{g["Id"]: g["number"] for g in ms_groups}}
        ms_nums = {{g["number"] for g in ms_groups}}

        # Delete removed groups
        for g in existing_groups:
            if g.number not in ms_nums:
                await session.delete(g)
        await session.flush()

        # Update or create groups
        num_to_group = {{}}
        for g in ms_groups:
            num = g["number"]
            if num in existing_groups_by_num:
                grp = existing_groups_by_num[num]
                grp.name = g["name"]
            else:
                grp = Group(
                    menu_variant_id=variant.id,
                    org_id=org_id,
                    number=num,
                    name=g["name"],
                )
                session.add(grp)
            num_to_group[num] = grp
        await session.flush()

        # Link parent_ids for groups
        ms_root = next((g for g in ms_groups if g["parentId"] is None or g["number"] == 0), ms_groups[0])
        root_num = ms_root["number"]
        root_grp = num_to_group[root_num]
        root_grp.parent_id = None

        for g in ms_groups:
            num = g["number"]
            if num == root_num:
                continue
            grp = num_to_group[num]
            p_id = g["parentId"]
            if p_id is not None and p_id in ms_id_to_num:
                parent_num = ms_id_to_num[p_id]
                grp.parent_id = num_to_group[parent_num].id
            else:
                grp.parent_id = root_grp.id
        await session.flush()

        # 2. Reconcile services
        ms_tsps = {{s["tsp_code"] for s in ms_services}}

        # Delete removed services
        for s in existing_services:
            if s.tsp_code not in ms_tsps:
                await session.delete(s)
        await session.flush()

        # Update or create services
        for s in ms_services:
            tsp = s["tsp_code"]
            grp_num = ms_id_to_num.get(s["group_id"], root_num)
            target_group = num_to_group.get(grp_num, root_grp)

            if tsp in existing_services_by_tsp:
                svc = existing_services_by_tsp[tsp]
                svc.name = s["name"]
                svc.printname = s["printname"]
                svc.price = s["price"]
                svc.protypenumber = s["protypenumber"]
                svc.group_id = target_group.id
            else:
                svc = Service(
                    menu_variant_id=variant.id,
                    group_id=target_group.id,
                    tsp_code=tsp,
                    name=s["name"],
                    printname=s["printname"],
                    price=s["price"],
                    protypenumber=s["protypenumber"],
                )
                session.add(svc)
        await session.flush()

        # 3. Query all updated groups and services for snapshot tree
        all_groups = (await session.execute(
            select(Group).where(Group.menu_variant_id == variant.id).order_by(Group.number)
        )).scalars().all()
        all_services = (await session.execute(
            select(Service).where(Service.menu_variant_id == variant.id).order_by(Service.tsp_code)
        )).scalars().all()

        menu_tree = build_tree(all_groups, all_services)
        services_by_tsp = {{
            str(s.tsp_code): {{
                "name": s.name,
                "printname": s.printname,
                "price": s.price,
                "protypenumber": s.protypenumber,
            }}
            for s in all_services
        }}

        # 4. Create snapshot for new_version
        snapshot = MenuVariantSnapshot(
            menu_variant_id=variant.id,
            version=new_version,
            snapshot_data={{
                "menu_variant_id": variant.id,
                "version": new_version,
                "tree": menu_tree,
                "services_by_tsp": services_by_tsp,
            }},
        )
        session.add(snapshot)
        await session.commit()

        result = {{
            "variant_id": variant.id,
            "old_version": old_version,
            "new_version": new_version,
            "groups_count": len(all_groups),
            "services_count": len(all_services),
            "snapshot_version": snapshot.version,
        }}
        print(json.dumps(result))

asyncio.run(apply_update())
"""
    b64_script = base64.b64encode(pg_update_code.encode("utf-8")).decode("ascii")
    ssh_cmd = [
        "ssh",
        "-n",
        "-i",
        REMOTE_SSH_KEY,
        "-o",
        "StrictHostKeyChecking=no",
        REMOTE_SERVER,
        f"echo {b64_script} | base64 -d | sudo docker exec -i menubuilder-backend python",
    ]
    res = subprocess.run(
        ssh_cmd, capture_output=True, text=True, encoding="utf-8", check=True
    )
    update_result = json.loads(res.stdout.strip())
    logger.info("PostgreSQL update completed successfully: %s", update_result)

    # Sync dataset files locally
    save_dataset_files(
        groups=ms_groups,
        services=ms_services,
        merge_with_existing=True,
        org_ids=[org_id],
    )

    # Upload updated dataset file to remote server and container
    logger.info("Uploading updated legacy_menu_data.json to cloud server...")
    scp_cmd = [
        "scp",
        "-i",
        REMOTE_SSH_KEY,
        "-o",
        "StrictHostKeyChecking=no",
        str(BACKEND_DATA_JSON),
        f"{REMOTE_SERVER}:/tmp/legacy_menu_data.json",
    ]
    subprocess.run(scp_cmd, check=True)

    ssh_cp_cmd = [
        "ssh",
        "-n",
        "-i",
        REMOTE_SSH_KEY,
        "-o",
        "StrictHostKeyChecking=no",
        REMOTE_SERVER,
        "sudo docker cp /tmp/legacy_menu_data.json menubuilder-backend:/app/app/services/legacy_menu_data.json",
    ]
    subprocess.run(ssh_cp_cmd, check=True)
    logger.info("Uploaded legacy_menu_data.json into menubuilder-backend container.")

    return update_result


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Migrate menu for org=516 from legacy MSSQL to PostgreSQL with versioning."
    )
    parser.add_argument(
        "--dry-run",
        "--diff-only",
        action="store_true",
        help="Only display the dirty diff without applying updates",
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

    logger.info(
        "Step 1: Fetching menu data for org %d from MS SQL (%s)...", ORG_ID, args.server
    )
    ms_groups, ms_services = fetch_mssql_menu_data(
        server=args.server,
        database=args.database,
        user=args.user,
        password=args.password,
        org_ids=[ORG_ID],
    )
    logger.info(
        "Fetched %d groups and %d services from MS SQL.",
        len(ms_groups),
        len(ms_services),
    )

    logger.info("Step 2: Fetching current menu state from PostgreSQL (etran)...")
    pg_state = fetch_postgres_state(org_id=ORG_ID)

    logger.info("Step 3: Computing dirty diff...")
    diffs = compute_dirty_diff(ms_groups, ms_services, pg_state)

    logger.info("Step 4: Displaying dirty variant...")
    has_changes = print_dirty_variant(diffs, pg_state.get("variant"))

    if args.dry_run:
        logger.info("DRY-RUN completed. No changes applied.")
        return

    if not has_changes:
        logger.info("No discrepancies found. No update needed.")
        return

    logger.info("Step 5: Launching update by versioning rules...")
    update_result = apply_menu_update_with_versioning(
        org_id=ORG_ID,
        ms_groups=ms_groups,
        ms_services=ms_services,
    )

    logger.info("Step 6: Verifying updated PostgreSQL state...")
    updated_pg_state = fetch_postgres_state(org_id=ORG_ID)
    updated_diffs = compute_dirty_diff(ms_groups, ms_services, updated_pg_state)

    print("\n" + "=" * 80)
    print("ВЕРИФИКАЦИЯ ПОСЛЕ ОБНОВЛЕНИЯ:")
    print("=" * 80)
    v = updated_pg_state["variant"]
    print(f"Вариант меню ID: {v['id']}")
    print(
        f"Новая версия (версионирование): {v['version']} (было: {update_result['old_version']})"
    )
    print(f"Групп в БД: {len(updated_pg_state['groups'])}")
    print(f"Услуг в БД: {len(updated_pg_state['services'])}")
    print(
        f"Снапшотов версий: {len(updated_pg_state['snapshots'])} (версии: {[s['version'] for s in updated_pg_state['snapshots']]})"
    )

    # Check the 2 modified services
    s_by_tsp = {s["tsp_code"]: s for s in updated_pg_state["services"]}
    print("\nПроверка обновленных услуг:")
    print(
        f"  - TSP 1000304 ({s_by_tsp[1000304]['name']}): цена = {s_by_tsp[1000304]['price']} (ожидалось 300)"
    )
    print(
        f"  - TSP 1000305 ({s_by_tsp[1000305]['name']}): цена = {s_by_tsp[1000305]['price']} (ожидалось 450)"
    )

    remaining_diffs = (
        len(updated_diffs["groups_added"])
        + len(updated_diffs["groups_removed"])
        + len(updated_diffs["groups_modified"])
        + len(updated_diffs["services_added"])
        + len(updated_diffs["services_removed"])
        + len(updated_diffs["services_modified"])
    )
    if remaining_diffs == 0:
        print("\nУСПЕШНО: Все данные меню синхронизированы с легаси MSSQL! Отличий 0.")
    else:
        print(f"\nВНИМАНИЕ: Остались несовпадения ({remaining_diffs}).")
    print("=" * 80)


if __name__ == "__main__":
    main()
