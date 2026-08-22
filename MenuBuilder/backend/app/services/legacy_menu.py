"""Import legacy menu trees and services from MS SQL legacy data into MenuBuilder."""

import asyncio
import json
import logging
from collections import defaultdict
from pathlib import Path
from typing import TypedDict

from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import async_session
from app.models import Group, MenuVariant, Service

logger = logging.getLogger(__name__)

DATA_FILE_PATH = Path(__file__).parent / "legacy_menu_data.json"
LEGACY_VARIANT_NAME = "Легаси меню"


class GroupDef(TypedDict):
    Id: int
    org_id: int
    number: int
    name: str
    parentId: int | None


class ServiceDef(TypedDict):
    Id: int
    group_id: int
    tsp_code: int
    name: str
    printname: str | None
    price: int
    protypenumber: int


def load_legacy_menu_data(
    file_path: Path | str | None = None,
) -> tuple[list[GroupDef], list[ServiceDef]]:
    """Load legacy menu dump from JSON file."""
    path = Path(file_path) if file_path else DATA_FILE_PATH
    if not path.exists():
        msg = f"Legacy menu data file not found: {path}"
        raise FileNotFoundError(msg)

    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    groups: list[GroupDef] = data.get("groups", [])
    services: list[ServiceDef] = data.get("services", [])
    return groups, services


async def import_legacy_menu_for_org(
    session: AsyncSession,
    org_id: int,
    variant_name: str = LEGACY_VARIANT_NAME,
    groups_data: list[GroupDef] | None = None,
    services_data: list[ServiceDef] | None = None,
) -> MenuVariant | None:
    """Import or overwrite legacy menu variant, group tree, and services for a given organization."""
    if groups_data is None or services_data is None:
        all_groups, all_services = load_legacy_menu_data()
        org_groups = [g for g in all_groups if g["org_id"] == org_id]
        group_ids = {g["Id"] for g in org_groups}
        org_services = [s for s in all_services if s["group_id"] in group_ids]
    else:
        org_groups = [g for g in groups_data if g["org_id"] == org_id]
        group_ids = {g["Id"] for g in org_groups}
        org_services = [s for s in services_data if s["group_id"] in group_ids]

    if not org_groups:
        logger.warning("No legacy groups found for organization %s. Skipping.", org_id)
        return None

    # 1. Find or create menu variant
    stmt = select(MenuVariant).where(
        MenuVariant.org_id == org_id, MenuVariant.name == variant_name
    )
    res = await session.execute(stmt)
    variant = res.scalar_one_or_none()

    if variant:
        await session.execute(
            delete(Service).where(Service.menu_variant_id == variant.id)
        )
        await session.execute(delete(Group).where(Group.menu_variant_id == variant.id))
        await session.flush()
    else:
        variant = MenuVariant(org_id=org_id, name=variant_name)
        session.add(variant)
        await session.flush()

    # 2. Locate root group (number == 0 or parentId is None)
    root: GroupDef | None = None
    for g in org_groups:
        if g["parentId"] is None or g["number"] == 0:
            root = g
            break
    if not root:
        root = org_groups[0]

    root_group = Group(
        menu_variant_id=variant.id,
        org_id=org_id,
        number=root["number"],
        name=root["name"],
        parent_id=None,
    )
    session.add(root_group)
    await session.flush()

    id_map: dict[int, int] = {
        root["Id"]: root_group.id,
        0: root_group.id,  # In legacy MSSQL, parentId=0 refers to root group
    }

    # 3. Iteratively insert child / nested groups
    remaining = [g for g in org_groups if g["Id"] != root["Id"]]
    iterations = 0
    while remaining and iterations < 15:
        iterations += 1
        next_remaining: list[GroupDef] = []
        for g in remaining:
            raw_p = g["parentId"]
            p_id = 0 if raw_p is None else raw_p
            if p_id in id_map:
                parent_group_id = id_map[p_id]
                # If parent_group_id is root_group.id and p_id == 0, parent_id is root_group.id
                # (or None if this is top level, but in MenuBuilder hierarchy, subgroups reside under root)
                grp = Group(
                    menu_variant_id=variant.id,
                    org_id=org_id,
                    number=g["number"],
                    name=g["name"],
                    parent_id=parent_group_id,
                )
                session.add(grp)
                await session.flush()
                id_map[g["Id"]] = grp.id
            else:
                next_remaining.append(g)
        remaining = next_remaining

    if remaining:
        logger.error(
            "Org %s: %d groups could not be linked to parent: %s",
            org_id,
            len(remaining),
            remaining,
        )

    # 4. Insert services
    services_by_group: dict[int, list[ServiceDef]] = defaultdict(list)
    for s in org_services:
        services_by_group[s["group_id"]].append(s)

    for ms_group_id, svcs in services_by_group.items():
        pg_group_id = id_map.get(ms_group_id)
        if not pg_group_id:
            logger.warning(
                "Org %s: group_id %s not found in mapping for %d services",
                org_id,
                ms_group_id,
                len(svcs),
            )
            continue

        for s in svcs:
            svc = Service(
                menu_variant_id=variant.id,
                group_id=pg_group_id,
                tsp_code=s["tsp_code"],
                name=s["name"],
                printname=s["printname"],
                price=s["price"],
                protypenumber=s["protypenumber"],
            )
            session.add(svc)

    await session.flush()
    logger.info(
        "Imported menu variant '%s' (id=%s) for org %s: %d groups, %d services",
        variant_name,
        variant.id,
        org_id,
        len(org_groups),
        len(org_services),
    )
    return variant


async def import_all_legacy_menus(
    session: AsyncSession,
    org_ids: list[int] | None = None,
    variant_name: str = LEGACY_VARIANT_NAME,
) -> dict[int, MenuVariant]:
    """Import legacy menus for all organizations found in the data file (or specified list)."""
    all_groups, all_services = load_legacy_menu_data()

    # Determine unique org_ids
    available_org_ids = sorted({g["org_id"] for g in all_groups})
    target_orgs = (
        [o for o in org_ids if o in available_org_ids]
        if org_ids is not None
        else available_org_ids
    )

    imported_variants: dict[int, MenuVariant] = {}
    for org_id in target_orgs:
        variant = await import_legacy_menu_for_org(
            session=session,
            org_id=org_id,
            variant_name=variant_name,
            groups_data=all_groups,
            services_data=all_services,
        )
        if variant:
            imported_variants[org_id] = variant

    await session.commit()
    logger.info(
        "Successfully completed legacy menu import for %d organizations.",
        len(imported_variants),
    )
    return imported_variants


async def main() -> None:
    """CLI runner to import legacy menus into PostgreSQL."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Import legacy menus and services from JSON into PostgreSQL."
    )
    group = parser.add_mutually_exclusive_group(required=False)
    group.add_argument(
        "--org",
        type=int,
        help="Single organization ID to import (e.g. --org 424)",
    )
    group.add_argument(
        "--orgs",
        type=str,
        help="Comma-separated list of organization IDs (e.g. --orgs 1,424,477)",
    )
    group.add_argument(
        "--all",
        action="store_true",
        default=True,
        help="Import all organizations present in the JSON file (default)",
    )
    group.add_argument(
        "--list-orgs",
        action="store_true",
        help="List available organizations in the JSON file and exit",
    )

    parser.add_argument(
        "--file",
        type=str,
        default=None,
        help=f"Path to JSON file (default: {DATA_FILE_PATH})",
    )
    parser.add_argument(
        "--variant-name",
        type=str,
        default=LEGACY_VARIANT_NAME,
        help=f"Name of the MenuVariant (default: '{LEGACY_VARIANT_NAME}')",
    )

    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
    )

    data_file = Path(args.file) if args.file else DATA_FILE_PATH
    groups, _ = load_legacy_menu_data(data_file)
    available_orgs = sorted({g["org_id"] for g in groups})

    if args.list_orgs:
        print(f"Available {len(available_orgs)} organizations in {data_file}:")
        print(", ".join(map(str, available_orgs)))
        return

    target_orgs: list[int] | None = None
    if args.org:
        target_orgs = [args.org]
    elif args.orgs:
        target_orgs = [int(o.strip()) for o in args.orgs.split(",") if o.strip()]

    async with async_session() as session:
        result = await import_all_legacy_menus(
            session=session,
            org_ids=target_orgs,
            variant_name=args.variant_name,
        )
        print(f"Import completed: {len(result)} organizations imported.")
        for org_id, var in result.items():
            print(f"  Org {org_id}: variant_id={var.id}")


if __name__ == "__main__":
    asyncio.run(main())
