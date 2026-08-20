import logging
import secrets
from datetime import UTC, datetime, timedelta
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy import func, or_, select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import require_superuser
from app.database import get_db
from app.models import (
    CertificatePin,
    License,
    Org,
    Terminal,
    TerminalType,
)
from app.schemas import (
    AdminTerminalCreate,
    AdminTerminalListResponse,
    AdminTerminalRead,
    AdminTerminalUpdate,
    GeneratePinResponse,
    NextDeviceIdResponse,
    SetLicenseRequest,
    SetLicenseResponse,
    SetStatusRequest,
    TerminalTypeRead,
)

logger = logging.getLogger(__name__)

router = APIRouter(
    prefix="/api/admin",
    tags=["admin-terminals"],
    dependencies=[Depends(require_superuser)],
)


def generate_device_sn(device_id: int) -> str:
    """Generate automatic serial number (sn) according to platform standard:
    a4b<7-digit device_id>c<5-digit random>d<DDMMYY>
    """
    device_part = f"{device_id:07d}"
    rand_first = str(secrets.randbelow(9) + 1)  # 1-9
    rand_rest = "".join(str(secrets.randbelow(10)) for _ in range(4))  # 4 digits
    random_part = rand_first + rand_rest
    date_part = datetime.now(UTC).strftime("%d%m%y")
    return f"a4b{device_part}c{random_part}d{date_part}"


async def generate_unique_cert_pin(db: AsyncSession) -> str:
    """Generate a unique 6-digit numeric PIN."""
    for _ in range(50):
        pin = "".join(secrets.choice("0123456789") for _ in range(6))
        res = await db.execute(
            select(CertificatePin.id).where(CertificatePin.pin == pin)
        )
        if not res.scalar_one_or_none():
            return pin
    raise HTTPException(
        status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
        detail="Failed to generate unique PIN after multiple attempts",
    )


@router.get("/terminal-types", response_model=list[TerminalTypeRead])
@router.get("/dictionaries/terminal-types", response_model=list[TerminalTypeRead])
async def list_terminal_types(
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> list[TerminalTypeRead]:
    """Get list of all terminal types (Superuser only)."""
    res = await db.execute(select(TerminalType).order_by(TerminalType.id))
    types = res.scalars().all()
    if not types:
        return [
            TerminalTypeRead(
                id=0, name="Обычный терминал", description="Стандартный терминал"
            ),
            TerminalTypeRead(
                id=1,
                name="Платёжный терминал",
                description="Платёжный терминал самообслуживания",
            ),
            TerminalTypeRead(id=2, name="Касса", description="Кассовый терминал"),
        ]
    return [
        TerminalTypeRead(id=t.id, name=t.name, description=t.description) for t in types
    ]


@router.get("/terminals/next-device-id", response_model=NextDeviceIdResponse)
async def get_next_device_id(
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> NextDeviceIdResponse:
    """Find the first available/unused device_id (Superuser only)."""
    res = await db.execute(select(Terminal.device_id).order_by(Terminal.device_id))
    taken_ids = set(res.scalars().all())

    candidate = 1
    while candidate in taken_ids:
        candidate += 1

    return NextDeviceIdResponse(next_device_id=candidate)


@router.get("/terminals", response_model=AdminTerminalListResponse)
async def list_terminals(
    org_id: int | None = Query(None, description="Filter by organization ID"),
    search: str | None = Query(
        None,
        description="Search by device_id, sn, address, note, cert_serial, org_name",
    ),
    is_active: bool | None = Query(None, description="Filter by active status"),
    page: int = Query(1, ge=1, description="Page number"),
    page_size: int = Query(50, ge=1, le=500, description="Items per page"),
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> AdminTerminalListResponse:
    """Get comprehensive list of all terminals with all details and filters (Superuser only)."""
    # Base query joining Terminal, Org, TerminalType
    query = (
        select(
            Terminal,
            Org.org_name.label("org_name"),
            TerminalType.name.label("terminal_type_name"),
        )
        .outerjoin(Org, Org.org_id == Terminal.org_id)
        .outerjoin(TerminalType, TerminalType.id == Terminal.terminal_type_id)
    )

    if org_id is not None:
        query = query.where(Terminal.org_id == org_id)

    if is_active is not None:
        query = query.where(Terminal.is_active == is_active)

    if search:
        search_pattern = f"%{search.strip()}%"
        # Check if search is purely numeric for exact or pattern device_id matching
        search_int = None
        if search.strip().isdigit():
            search_int = int(search.strip())

        conditions: list[Any] = [
            Terminal.sn.ilike(search_pattern),
            Terminal.address.ilike(search_pattern),
            Terminal.note.ilike(search_pattern),
            Terminal.cert_serial.ilike(search_pattern),
            Org.org_name.ilike(search_pattern),
            Org.name.ilike(search_pattern),
        ]
        if search_int is not None:
            conditions.append(Terminal.device_id == search_int)

        query = query.where(or_(*conditions))

    # Total count query
    count_query = select(func.count()).select_from(query.subquery())
    total_count = (await db.execute(count_query)).scalar_one()

    # Pagination & ordering
    query = (
        query.order_by(Terminal.device_id)
        .offset((page - 1) * page_size)
        .limit(page_size)
    )
    rows = (await db.execute(query)).all()

    terminal_ids = [row[0].id for row in rows]

    # Fetch licenses for these terminals
    licenses_map: dict[int, License] = {}
    if terminal_ids:
        lic_res = await db.execute(
            select(License)
            .where(License.terminal_id.in_(terminal_ids))
            .order_by(License.id.desc())
        )
        for lic in lic_res.scalars().all():
            if lic.terminal_id not in licenses_map:
                licenses_map[lic.terminal_id] = lic

    # Fetch pending certificate pins for these terminals
    pins_map: dict[int, CertificatePin] = {}
    if terminal_ids:
        pins_res = await db.execute(
            select(CertificatePin)
            .where(
                CertificatePin.terminal_id.in_(terminal_ids),
                CertificatePin.status == "pending",
            )
            .order_by(CertificatePin.id.desc())
        )
        for pin_obj in pins_res.scalars().all():
            if pin_obj.terminal_id not in pins_map:
                pins_map[pin_obj.terminal_id] = pin_obj

    items: list[AdminTerminalRead] = []
    for term, org_name, terminal_type_name in rows:
        lic = licenses_map.get(term.id)
        pin_obj = pins_map.get(term.id)

        items.append(
            AdminTerminalRead(
                id=term.id,
                device_id=term.device_id,
                sn=term.sn,
                cert_serial=term.cert_serial,
                cert_not_valid_after=term.cert_not_valid_after,
                org_id=term.org_id,
                org_name=org_name,
                is_active=term.is_active,
                address=term.address,
                note=term.note,
                terminal_type_id=term.terminal_type_id,
                terminal_type_name=terminal_type_name,
                created_at=term.created_at,
                updated_at=term.updated_at,
                license_id=lic.id if lic else None,
                license_expires_at=lic.expires_at if lic else None,
                license_is_active=lic.is_active if lic else None,
                license_balance=lic.balance if lic else None,
                license_type=lic.license_type if lic else None,
                billing_period_months=lic.billing_period_months if lic else None,
                monthly_price_override_minor=(
                    lic.monthly_price_override_minor if lic else None
                ),
                renewal_enabled=lic.renewal_enabled if lic else None,
                deactivation_requested_at=(
                    lic.deactivation_requested_at if lic else None
                ),
                pending_pin=pin_obj.pin if pin_obj else None,
                pin_expires_at=pin_obj.expires_at if pin_obj else None,
                pin_status=pin_obj.status if pin_obj else None,
            )
        )

    return AdminTerminalListResponse(
        total=total_count,
        page=page,
        page_size=page_size,
        items=items,
    )


@router.post(
    "/terminals", response_model=AdminTerminalRead, status_code=status.HTTP_201_CREATED
)
async def create_terminal(
    body: AdminTerminalCreate,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> AdminTerminalRead:
    """Create a new terminal with auto-generated SN formula and optional initial license (Superuser only)."""
    # Validate device_id range (1 to 9999999)
    if body.device_id < 1 or body.device_id > 9999999:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="device_id must be between 1 and 9999999 (not more than 7 digits)",
        )

    # Check if device_id already taken
    existing_term = await db.execute(
        select(Terminal).where(Terminal.device_id == body.device_id)
    )
    if existing_term.scalar_one_or_none():
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"Terminal with device_id {body.device_id} already exists",
        )

    # Check organization existence
    org = await db.get(Org, body.org_id)
    if not org:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Organization {body.org_id} does not exist",
        )

    # Generate SN by platform formula
    sn = generate_device_sn(body.device_id)
    # Ensure SN is unique in rare case
    for _ in range(10):
        existing_sn = await db.execute(select(Terminal.id).where(Terminal.sn == sn))
        if not existing_sn.scalar_one_or_none():
            break
        sn = generate_device_sn(body.device_id)

    terminal = Terminal(
        device_id=body.device_id,
        sn=sn,
        org_id=body.org_id,
        terminal_type_id=body.terminal_type_id,
        address=body.address,
        note=body.note,
        is_active=body.is_active,
    )
    db.add(terminal)
    await db.flush()

    # Initial License
    expires_at = body.license_expires_at or (datetime.now(UTC) + timedelta(days=365))
    license_entry = License(
        terminal_id=terminal.id,
        org_id=body.org_id,
        license_type="standard",
        expires_at=expires_at,
        is_active=body.is_active,
        billing_period_months=body.billing_period_months,
        renewal_enabled=body.renewal_enabled,
    )
    db.add(license_entry)

    await db.commit()
    await db.refresh(terminal)
    await db.refresh(license_entry)

    # Fetch terminal type name
    tt_name = None
    if terminal.terminal_type_id:
        tt = await db.get(TerminalType, terminal.terminal_type_id)
        if tt:
            tt_name = tt.name

    return AdminTerminalRead(
        id=terminal.id,
        device_id=terminal.device_id,
        sn=terminal.sn,
        cert_serial=terminal.cert_serial,
        cert_not_valid_after=terminal.cert_not_valid_after,
        org_id=terminal.org_id,
        org_name=org.org_name,
        is_active=terminal.is_active,
        address=terminal.address,
        note=terminal.note,
        terminal_type_id=terminal.terminal_type_id,
        terminal_type_name=tt_name,
        created_at=terminal.created_at,
        updated_at=terminal.updated_at,
        license_id=license_entry.id,
        license_expires_at=license_entry.expires_at,
        license_is_active=license_entry.is_active,
        license_balance=license_entry.balance,
        license_type=license_entry.license_type,
        billing_period_months=license_entry.billing_period_months,
        monthly_price_override_minor=license_entry.monthly_price_override_minor,
        renewal_enabled=license_entry.renewal_enabled,
        deactivation_requested_at=license_entry.deactivation_requested_at,
    )


@router.put("/terminals/{terminal_id}", response_model=AdminTerminalRead)
@router.patch("/terminals/{terminal_id}", response_model=AdminTerminalRead)
async def update_terminal(
    terminal_id: int,
    body: AdminTerminalUpdate,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> AdminTerminalRead:
    """Update non-key, non-validation fields of terminal and license (Superuser only)."""
    terminal = await db.get(Terminal, terminal_id)
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Terminal with ID {terminal_id} not found",
        )

    if body.org_id is not None:
        org = await db.get(Org, body.org_id)
        if not org:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail=f"Organization {body.org_id} does not exist",
            )
        terminal.org_id = body.org_id

    if body.terminal_type_id is not None:
        terminal.terminal_type_id = body.terminal_type_id
    if body.address is not None:
        terminal.address = body.address
    if body.note is not None:
        terminal.note = body.note
    if body.is_active is not None:
        terminal.is_active = body.is_active

    # Fetch/update license
    lic_res = await db.execute(
        select(License)
        .where(License.terminal_id == terminal_id)
        .order_by(License.id.desc())
    )
    lic = lic_res.scalars().first()

    if lic:
        if body.org_id is not None:
            lic.org_id = body.org_id
        if body.license_expires_at is not None:
            lic.expires_at = body.license_expires_at
        if body.license_is_active is not None:
            lic.is_active = body.license_is_active
        elif body.is_active is not None:
            # Sync license active state if terminal state changed
            lic.is_active = body.is_active
        if body.renewal_enabled is not None:
            lic.renewal_enabled = body.renewal_enabled
        if body.billing_period_months is not None:
            lic.billing_period_months = body.billing_period_months
        if (
            body.monthly_price_override_minor is not None
            or "monthly_price_override_minor" in body.model_fields_set
        ):
            lic.monthly_price_override_minor = body.monthly_price_override_minor
    elif body.license_expires_at is not None or body.is_active is not None:
        # Create license if none existed
        lic = License(
            terminal_id=terminal.id,
            org_id=terminal.org_id,
            license_type="standard",
            expires_at=body.license_expires_at
            or (datetime.now(UTC) + timedelta(days=365)),
            is_active=body.license_is_active
            if body.license_is_active is not None
            else terminal.is_active,
            billing_period_months=body.billing_period_months or 1,
            renewal_enabled=body.renewal_enabled
            if body.renewal_enabled is not None
            else True,
            monthly_price_override_minor=body.monthly_price_override_minor,
        )
        db.add(lic)

    await db.commit()
    await db.refresh(terminal)
    if lic:
        await db.refresh(lic)

    # Fetch org & terminal type
    org = await db.get(Org, terminal.org_id)
    tt_name = None
    if terminal.terminal_type_id:
        tt = await db.get(TerminalType, terminal.terminal_type_id)
        if tt:
            tt_name = tt.name

    # Fetch pending pin if any
    pin_res = await db.execute(
        select(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal.id,
            CertificatePin.status == "pending",
        )
        .order_by(CertificatePin.id.desc())
    )
    pin_obj = pin_res.scalars().first()

    return AdminTerminalRead(
        id=terminal.id,
        device_id=terminal.device_id,
        sn=terminal.sn,
        cert_serial=terminal.cert_serial,
        cert_not_valid_after=terminal.cert_not_valid_after,
        org_id=terminal.org_id,
        org_name=org.org_name if org else None,
        is_active=terminal.is_active,
        address=terminal.address,
        note=terminal.note,
        terminal_type_id=terminal.terminal_type_id,
        terminal_type_name=tt_name,
        created_at=terminal.created_at,
        updated_at=terminal.updated_at,
        license_id=lic.id if lic else None,
        license_expires_at=lic.expires_at if lic else None,
        license_is_active=lic.is_active if lic else None,
        license_balance=lic.balance if lic else None,
        license_type=lic.license_type if lic else None,
        billing_period_months=lic.billing_period_months if lic else None,
        monthly_price_override_minor=(
            lic.monthly_price_override_minor if lic else None
        ),
        renewal_enabled=lic.renewal_enabled if lic else None,
        deactivation_requested_at=(lic.deactivation_requested_at if lic else None),
        pending_pin=pin_obj.pin if pin_obj else None,
        pin_expires_at=pin_obj.expires_at if pin_obj else None,
        pin_status=pin_obj.status if pin_obj else None,
    )


@router.post(
    "/terminals/{terminal_id}/generate-pin", response_model=GeneratePinResponse
)
async def generate_terminal_pin(
    terminal_id: int,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> GeneratePinResponse:
    """Generate a new certificate PIN for the specified terminal (Superuser only)."""
    terminal = await db.get(Terminal, terminal_id)
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Terminal with ID {terminal_id} not found",
        )

    # Cancel previous pending pins for this terminal
    await db.execute(
        update(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal_id,
            CertificatePin.status == "pending",
        )
        .values(status="cancelled")
    )

    pin = await generate_unique_cert_pin(db)
    expires_at = datetime.now(UTC) + timedelta(hours=24)

    cert_pin = CertificatePin(
        pin=pin,
        terminal_id=terminal.id,
        org_id=terminal.org_id,
        created_by=user.get("username", "superuser"),
        creation_source="global_admin",
        payment_required=False,
        status="pending",
        expires_at=expires_at,
    )
    db.add(cert_pin)
    await db.commit()

    return GeneratePinResponse(
        pin=pin,
        terminal_id=terminal.id,
        device_id=terminal.device_id,
        expires_at=expires_at,
    )


@router.post("/terminals/{terminal_id}/set-license", response_model=SetLicenseResponse)
async def set_terminal_license(
    terminal_id: int,
    body: SetLicenseRequest,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> SetLicenseResponse:
    """Set or update license expiration date and state for a terminal (Superuser only)."""
    terminal = await db.get(Terminal, terminal_id)
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Terminal with ID {terminal_id} not found",
        )

    lic_res = await db.execute(
        select(License)
        .where(License.terminal_id == terminal_id)
        .order_by(License.id.desc())
    )
    lic = lic_res.scalars().first()

    if not lic:
        lic = License(
            terminal_id=terminal.id,
            org_id=terminal.org_id,
            license_type="standard",
            expires_at=body.expires_at,
            is_active=body.is_active,
            renewal_enabled=body.renewal_enabled,
        )
        db.add(lic)
    else:
        lic.expires_at = body.expires_at
        lic.is_active = body.is_active
        lic.renewal_enabled = body.renewal_enabled

    await db.commit()
    await db.refresh(lic)

    return SetLicenseResponse(
        terminal_id=terminal.id,
        license_id=lic.id,
        expires_at=lic.expires_at,
        is_active=lic.is_active,
        renewal_enabled=lic.renewal_enabled,
    )


@router.post("/terminals/{terminal_id}/set-status", response_model=AdminTerminalRead)
async def set_terminal_status(
    terminal_id: int,
    body: SetStatusRequest,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> AdminTerminalRead:
    """Change terminal status between active / inactive (from any state to any state) (Superuser only)."""
    terminal = await db.get(Terminal, terminal_id)
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Terminal with ID {terminal_id} not found",
        )

    terminal.is_active = body.is_active

    # Also update license status
    lic_res = await db.execute(
        select(License)
        .where(License.terminal_id == terminal_id)
        .order_by(License.id.desc())
    )
    lic = lic_res.scalars().first()
    if lic:
        lic.is_active = body.is_active

    await db.commit()
    await db.refresh(terminal)
    if lic:
        await db.refresh(lic)

    org = await db.get(Org, terminal.org_id)
    tt_name = None
    if terminal.terminal_type_id:
        tt = await db.get(TerminalType, terminal.terminal_type_id)
        if tt:
            tt_name = tt.name

    pin_res = await db.execute(
        select(CertificatePin)
        .where(
            CertificatePin.terminal_id == terminal.id,
            CertificatePin.status == "pending",
        )
        .order_by(CertificatePin.id.desc())
    )
    pin_obj = pin_res.scalars().first()

    return AdminTerminalRead(
        id=terminal.id,
        device_id=terminal.device_id,
        sn=terminal.sn,
        cert_serial=terminal.cert_serial,
        cert_not_valid_after=terminal.cert_not_valid_after,
        org_id=terminal.org_id,
        org_name=org.org_name if org else None,
        is_active=terminal.is_active,
        address=terminal.address,
        note=terminal.note,
        terminal_type_id=terminal.terminal_type_id,
        terminal_type_name=tt_name,
        created_at=terminal.created_at,
        updated_at=terminal.updated_at,
        license_id=lic.id if lic else None,
        license_expires_at=lic.expires_at if lic else None,
        license_is_active=lic.is_active if lic else None,
        license_balance=lic.balance if lic else None,
        license_type=lic.license_type if lic else None,
        billing_period_months=lic.billing_period_months if lic else None,
        monthly_price_override_minor=(
            lic.monthly_price_override_minor if lic else None
        ),
        renewal_enabled=lic.renewal_enabled if lic else None,
        deactivation_requested_at=(lic.deactivation_requested_at if lic else None),
        pending_pin=pin_obj.pin if pin_obj else None,
        pin_expires_at=pin_obj.expires_at if pin_obj else None,
        pin_status=pin_obj.status if pin_obj else None,
    )
