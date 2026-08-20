import logging

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import require_superuser
from app.database import get_db
from app.models import Org, OrgBillingSettings, OrgStatus
from app.schemas import AdminOrgCreate, AdminOrgRead, AdminOrgUpdate

logger = logging.getLogger(__name__)

router = APIRouter(
    prefix="/api/admin/organizations",
    tags=["admin-organizations"],
    dependencies=[Depends(require_superuser)],
)


@router.get("", response_model=list[AdminOrgRead])
async def list_organizations(
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> list[AdminOrgRead]:
    """List all organizations with their licensing and billing settings (Superuser only)."""
    # Fetch orgs
    org_res = await db.execute(select(Org).order_by(Org.org_id))
    orgs = org_res.scalars().all()

    # Fetch all billing settings
    settings_res = await db.execute(select(OrgBillingSettings))
    settings_map = {s.org_id: s for s in settings_res.scalars().all()}

    result: list[AdminOrgRead] = []
    for org in orgs:
        bs = settings_map.get(org.org_id)
        result.append(
            AdminOrgRead(
                org_id=org.org_id,
                org_name=org.org_name,
                name=org.name,
                status=org.status,
                is_active=org.is_active,
                created_at=org.created_at,
                updated_at=org.updated_at,
                monthly_price_minor=bs.monthly_price_minor if bs else 100_000,
                currency=bs.currency if bs else "RUB",
                cert_billing_mode=bs.cert_billing_mode if bs else "none",
                cert_price_minor=bs.cert_price_minor if bs else None,
                tenant_pin_creation_enabled=(
                    bs.tenant_pin_creation_enabled if bs else False
                ),
                cert_charge_primary_issue=(
                    bs.cert_charge_primary_issue if bs else True
                ),
                cert_charge_reissue=bs.cert_charge_reissue if bs else True,
            )
        )
    return result


@router.post("", response_model=AdminOrgRead, status_code=status.HTTP_201_CREATED)
async def create_organization(
    body: AdminOrgCreate,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> AdminOrgRead:
    """Create a new organization with licensing parameters (Superuser only)."""
    # Determine org_id
    if body.org_id is not None:
        org_id = body.org_id
        # Check if already exists
        existing = await db.get(Org, org_id)
        if existing:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail=f"Organization with ID {org_id} already exists",
            )
    else:
        # Find next available org_id
        res = await db.execute(select(Org.org_id).order_by(Org.org_id))
        existing_ids = set(res.scalars().all())
        org_id = 1
        while org_id in existing_ids:
            org_id += 1

    org = Org(
        org_id=org_id,
        org_name=body.org_name,
        name=body.name,
        status=body.status,
        is_active=body.is_active,
    )
    db.add(org)

    # Billing settings
    billing_settings = OrgBillingSettings(
        org_id=org_id,
        monthly_price_minor=body.monthly_price_minor,
        currency=body.currency,
        cert_billing_mode=body.cert_billing_mode,
        cert_price_minor=body.cert_price_minor,
        tenant_pin_creation_enabled=body.tenant_pin_creation_enabled,
        cert_charge_primary_issue=body.cert_charge_primary_issue,
        cert_charge_reissue=body.cert_charge_reissue,
    )
    db.add(billing_settings)

    # Org status
    status_entry = OrgStatus(
        org_id=org_id,
        status="active" if body.is_active else "inactive",
    )
    db.add(status_entry)

    await db.commit()
    await db.refresh(org)
    await db.refresh(billing_settings)

    return AdminOrgRead(
        org_id=org.org_id,
        org_name=org.org_name,
        name=org.name,
        status=org.status,
        is_active=org.is_active,
        created_at=org.created_at,
        updated_at=org.updated_at,
        monthly_price_minor=billing_settings.monthly_price_minor,
        currency=billing_settings.currency,
        cert_billing_mode=billing_settings.cert_billing_mode,
        cert_price_minor=billing_settings.cert_price_minor,
        tenant_pin_creation_enabled=billing_settings.tenant_pin_creation_enabled,
        cert_charge_primary_issue=billing_settings.cert_charge_primary_issue,
        cert_charge_reissue=billing_settings.cert_charge_reissue,
    )


@router.put("/{org_id}", response_model=AdminOrgRead)
@router.patch("/{org_id}", response_model=AdminOrgRead)
async def update_organization(
    org_id: int,
    body: AdminOrgUpdate,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> AdminOrgRead:
    """Update organization details and licensing policy (Superuser only)."""
    org = await db.get(Org, org_id)
    if not org:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Organization {org_id} not found",
        )

    # Update org fields
    if body.org_name is not None:
        org.org_name = body.org_name
    if body.name is not None:
        org.name = body.name
    if body.status is not None:
        org.status = body.status
    if body.is_active is not None:
        org.is_active = body.is_active

    # Update billing settings
    bs = await db.get(OrgBillingSettings, org_id)
    if not bs:
        bs = OrgBillingSettings(
            org_id=org_id,
            monthly_price_minor=body.monthly_price_minor or 100_000,
            currency=body.currency or "RUB",
            cert_billing_mode=body.cert_billing_mode or "none",
            cert_price_minor=body.cert_price_minor,
            tenant_pin_creation_enabled=body.tenant_pin_creation_enabled or False,
            cert_charge_primary_issue=(
                True
                if body.cert_charge_primary_issue is None
                else body.cert_charge_primary_issue
            ),
            cert_charge_reissue=(
                True if body.cert_charge_reissue is None else body.cert_charge_reissue
            ),
        )
        db.add(bs)
    else:
        if body.monthly_price_minor is not None:
            bs.monthly_price_minor = body.monthly_price_minor
        if body.currency is not None:
            bs.currency = body.currency
        if body.cert_billing_mode is not None:
            bs.cert_billing_mode = body.cert_billing_mode
        if (
            body.cert_price_minor is not None
            or "cert_price_minor" in body.model_fields_set
        ):
            bs.cert_price_minor = body.cert_price_minor
        if body.tenant_pin_creation_enabled is not None:
            bs.tenant_pin_creation_enabled = body.tenant_pin_creation_enabled
        if body.cert_charge_primary_issue is not None:
            bs.cert_charge_primary_issue = body.cert_charge_primary_issue
        if body.cert_charge_reissue is not None:
            bs.cert_charge_reissue = body.cert_charge_reissue

    # Update OrgStatus if exists
    status_entry = (
        await db.execute(select(OrgStatus).where(OrgStatus.org_id == org_id))
    ).scalar_one_or_none()
    if status_entry:
        status_entry.status = "active" if org.is_active else "inactive"

    await db.commit()
    await db.refresh(org)
    await db.refresh(bs)

    return AdminOrgRead(
        org_id=org.org_id,
        org_name=org.org_name,
        name=org.name,
        status=org.status,
        is_active=org.is_active,
        created_at=org.created_at,
        updated_at=org.updated_at,
        monthly_price_minor=bs.monthly_price_minor,
        currency=bs.currency,
        cert_billing_mode=bs.cert_billing_mode,
        cert_price_minor=bs.cert_price_minor,
        tenant_pin_creation_enabled=bs.tenant_pin_creation_enabled,
        cert_charge_primary_issue=bs.cert_charge_primary_issue,
        cert_charge_reissue=bs.cert_charge_reissue,
    )
