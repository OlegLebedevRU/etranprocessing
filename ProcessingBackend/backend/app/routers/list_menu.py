"""Terminal ListMenuFile router for ProcessingBackend."""

import logging
from datetime import UTC, datetime

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.dependencies import get_current_terminal
from app.models import (
    Group,
    MenuVariant,
    MenuVariantSnapshot,
    ServiceMenu,
    Terminal,
    TerminalMenuBinding,
)

logger = logging.getLogger(__name__)

router = APIRouter()


def _build_menu_tree(groups_list, services_list):
    groups_by_parent = {}
    for g in groups_list:
        groups_by_parent.setdefault(g.parent_id, []).append(g)

    services_by_group = {}
    for s in services_list:
        services_by_group.setdefault(s.group_id, []).append(s)

    root_groups = groups_by_parent.get(None, [])
    if not root_groups:
        return {"name": "root", "items": []}

    if len(root_groups) > 1:
        msg = (
            f"Menu variant cannot have more than 1 root group, found {len(root_groups)}"
        )
        raise ValueError(msg)

    root_group = root_groups[0]

    def _build_nodes(parent_group_id):
        items = []
        for s in sorted(
            services_by_group.get(parent_group_id, []), key=lambda x: x.tsp_code
        ):
            svc = {
                "name": s.name,
                "code": s.tsp_code,
                "prototypeid": s.protypenumber,
            }
            if s.printname:
                svc["printname"] = s.printname
            if s.price:
                svc["price"] = str(s.price)
            items.append(svc)

        for g in sorted(
            groups_by_parent.get(parent_group_id, []), key=lambda x: x.number
        ):
            node = {
                "name": g.name,
                "items": _build_nodes(g.id),
            }
            items.append(node)
        return items

    return {"name": "root", "items": _build_nodes(root_group.id)}


@router.get("/ListMenuFile")
async def list_menu_file(
    current_terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    """Serve menu tree configuration to terminals."""
    binding = await db.scalar(
        select(TerminalMenuBinding).where(
            TerminalMenuBinding.device_id == current_terminal.device_id
        )
    )
    if not binding:
        return {"name": "root", "items": []}

    variant = await db.get(MenuVariant, binding.menu_variant_id)

    if not variant:
        return {"name": "root", "items": []}

    groups_result = await db.execute(
        select(Group).where(Group.menu_variant_id == variant.id).order_by(Group.number)
    )
    groups_list = groups_result.scalars().all()

    group_ids = [g.id for g in groups_list]
    services_result = await db.execute(
        select(ServiceMenu)
        .where(ServiceMenu.group_id.in_(group_ids))
        .order_by(ServiceMenu.tsp_code)
    )
    services_list = services_result.scalars().all()

    try:
        menu_data = _build_menu_tree(groups_list, services_list)
    except ValueError as e:
        logger.error("Error building menu tree for variant %s: %s", variant.id, e)
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=str(e),
        ) from e

    snapshot_payload = {
        "menu_variant_id": variant.id,
        "version": variant.version,
        "tree": menu_data,
        "services_by_tsp": {
            str(s.tsp_code): {
                "name": s.name,
                "printname": s.printname,
                "price": s.price,
                "protypenumber": s.protypenumber,
            }
            for s in services_list
        },
    }

    # Check and create or update snapshot A{x}
    snapshot = await db.scalar(
        select(MenuVariantSnapshot).where(
            MenuVariantSnapshot.menu_variant_id == variant.id,
            MenuVariantSnapshot.version == variant.version,
        )
    )
    if not snapshot:
        snapshot = MenuVariantSnapshot(
            menu_variant_id=variant.id,
            version=variant.version,
            snapshot_data=snapshot_payload,
        )
        db.add(snapshot)
    else:
        snapshot.snapshot_data = snapshot_payload

    await db.flush()

    binding.loaded_version = variant.version
    binding.loaded_at = datetime.now(UTC)

    await db.commit()
    return menu_data
