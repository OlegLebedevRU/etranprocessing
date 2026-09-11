import contextlib
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
    BatchProvisionTerminalsRequest,
    BatchProvisionTerminalsResponse,
    GeneratePinResponse,
    NextDeviceIdResponse,
    ProvisionTerminalResponse,
    SetLicenseRequest,
    SetLicenseResponse,
    SetStatusRequest,
    TerminalTypeRead,
)
from app.services.iot_client import iot_client

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


def _build_admin_terminal_read(
    term: Terminal,
    org_name: str | None = None,
    tt_name: str | None = None,
    lic: License | None = None,
    pin_obj: CertificatePin | None = None,
) -> AdminTerminalRead:
    return AdminTerminalRead(
        id=term.id,
        device_id=term.device_id,
        sn=term.sn,
        cert_serial=term.cert_serial,
        cert_not_valid_after=term.cert_not_valid_after,
        org_id=term.org_id,
        org_name=org_name,
        is_active=term.is_active,
        show_in_monitoring=term.show_in_monitoring
        if term.show_in_monitoring is not None
        else True,
        address=term.address,
        note=term.note,
        terminal_type_id=term.terminal_type_id,
        terminal_type_name=tt_name,
        timezone=term.timezone,
        created_at=term.created_at,
        updated_at=term.updated_at,
        license_id=lic.id if lic else None,
        license_expires_at=lic.expires_at if lic else None,
        license_is_active=term.is_active if lic else None,
        license_balance=lic.balance if lic else None,
        license_type=lic.license_type if lic else None,
        billing_period_months=lic.billing_period_months if lic else None,
        monthly_price_override_minor=(
            lic.monthly_price_override_minor if lic else None
        ),
        renewal_enabled=term.is_active if lic else None,
        deactivation_requested_at=None,
        pending_pin=pin_obj.pin if pin_obj else None,
        pin_expires_at=pin_obj.expires_at if pin_obj else None,
        pin_status=pin_obj.status if pin_obj else None,
        iot_provisioned=term.iot_provisioned or False,
        iot_provisioned_at=term.iot_provisioned_at,
        iot_last_sync_at=term.iot_last_sync_at,
        iot_is_online=term.iot_is_online or False,
        iot_last_connected_at=term.iot_last_connected_at,
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
        raw_search = search.strip()
        search_pattern = f"%{raw_search}%"

        # Check if search contains comma-separated numbers (e.g. "101, 102, 105")
        comma_ids = [
            int(part.strip())
            for part in raw_search.split(",")
            if part.strip().isdigit()
        ]

        conditions: list[Any] = [
            Terminal.sn.ilike(search_pattern),
            Terminal.address.ilike(search_pattern),
            Terminal.note.ilike(search_pattern),
            Terminal.cert_serial.ilike(search_pattern),
            Org.org_name.ilike(search_pattern),
            Org.name.ilike(search_pattern),
        ]
        if len(comma_ids) > 1:
            conditions.append(Terminal.device_id.in_(comma_ids))
        elif len(comma_ids) == 1 and raw_search.isdigit():
            conditions.append(Terminal.device_id == comma_ids[0])

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
            _build_admin_terminal_read(
                term=term,
                org_name=org_name,
                tt_name=terminal_type_name,
                lic=lic,
                pin_obj=pin_obj,
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
        timezone=body.timezone,
        is_active=body.is_active,
        show_in_monitoring=body.show_in_monitoring,
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
        billing_period_months=body.billing_period_months,
        monthly_price_override_minor=body.monthly_price_override_minor,
    )
    db.add(license_entry)

    # Sync provisioning if requested
    if body.iot_provisioned:
        with contextlib.suppress(Exception):
            prov_res = await iot_client.provision_terminal(
                device_id=terminal.device_id,
                sn=terminal.sn,
                org_id=terminal.org_id,
            )
            if prov_res.get("success"):
                terminal.iot_provisioned = True
                terminal.iot_provisioned_at = datetime.now(UTC)

    await db.commit()
    await db.refresh(terminal)
    await db.refresh(license_entry)

    # Fetch terminal type name
    tt_name = None
    if terminal.terminal_type_id:
        tt = await db.get(TerminalType, terminal.terminal_type_id)
        if tt:
            tt_name = tt.name

    return _build_admin_terminal_read(
        term=terminal,
        org_name=org.org_name,
        tt_name=tt_name,
        lic=license_entry,
        pin_obj=None,
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
    if body.timezone is not None or "timezone" in body.model_fields_set:
        terminal.timezone = body.timezone
    if body.is_active is not None:
        terminal.is_active = body.is_active
    if body.show_in_monitoring is not None:
        terminal.show_in_monitoring = body.show_in_monitoring
    if body.iot_provisioned is not None:
        if body.iot_provisioned and not terminal.iot_provisioned:
            with contextlib.suppress(Exception):
                prov_res = await iot_client.provision_terminal(
                    device_id=terminal.device_id,
                    sn=terminal.sn,
                    org_id=terminal.org_id,
                )
                if prov_res.get("success"):
                    terminal.iot_provisioned = True
                    terminal.iot_provisioned_at = datetime.now(UTC)
        elif not body.iot_provisioned:
            terminal.iot_provisioned = False

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
        if body.billing_period_months is not None:
            lic.billing_period_months = body.billing_period_months
        if (
            body.monthly_price_override_minor is not None
            or "monthly_price_override_minor" in body.model_fields_set
        ):
            lic.monthly_price_override_minor = body.monthly_price_override_minor
        if body.is_active and lic.expires_at < datetime.now(UTC):
            lic.expires_at = datetime.now(UTC)
    elif body.license_expires_at is not None or body.is_active is not None:
        # Create license if none existed
        lic = License(
            terminal_id=terminal.id,
            org_id=terminal.org_id,
            license_type="standard",
            expires_at=body.license_expires_at
            or (datetime.now(UTC) + timedelta(days=365)),
            billing_period_months=body.billing_period_months or 1,
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

    return _build_admin_terminal_read(
        term=terminal,
        org_name=org.org_name if org else None,
        tt_name=tt_name,
        lic=lic,
        pin_obj=pin_obj,
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
        )
        db.add(lic)
    else:
        lic.expires_at = body.expires_at

    await db.commit()
    await db.refresh(lic)

    return SetLicenseResponse(
        terminal_id=terminal.id,
        license_id=lic.id,
        expires_at=lic.expires_at,
        is_active=terminal.is_active,
        renewal_enabled=terminal.is_active,
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

    lic_res = await db.execute(
        select(License)
        .where(License.terminal_id == terminal_id)
        .order_by(License.id.desc())
    )
    lic = lic_res.scalars().first()
    now = datetime.now(UTC)
    if body.is_active:
        if lic:
            lic.expires_at = max(lic.expires_at, now)
        else:
            lic = License(
                terminal_id=terminal.id,
                org_id=terminal.org_id,
                license_type="standard",
                expires_at=now,
                billing_period_months=1,
            )
            db.add(lic)

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

    return _build_admin_terminal_read(
        term=terminal,
        org_name=org.org_name if org else None,
        tt_name=tt_name,
        lic=lic,
        pin_obj=pin_obj,
    )


@router.post(
    "/terminals/{terminal_id}/provision-iot",
    response_model=ProvisionTerminalResponse,
)
async def provision_terminal_to_iot(
    terminal_id: int,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> ProvisionTerminalResponse:
    """Provision a terminal into Leo4 IoT Platform & RabbitMQ (Superuser only)."""
    terminal = await db.get(Terminal, terminal_id)
    if not terminal:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Terminal with ID {terminal_id} not found",
        )

    org = await db.get(Org, terminal.org_id)
    org_name = org.org_name if org else None

    # Call Leo4 IoT Platform
    try:
        iot_res = await iot_client.provision_terminal(
            device_id=terminal.device_id,
            sn=terminal.sn,
            org_id=terminal.org_id,
            name=org_name or f"Terminal {terminal.device_id}",
            tags={"source": "etranprocessing", "address": terminal.address or ""},
        )
    except Exception as e:
        logger.exception(
            "Failed to provision terminal %d in Leo4 IoT", terminal.device_id
        )
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"Failed to communicate with Leo4 IoT Platform: {e}",
        ) from e

    now = datetime.now(UTC)
    terminal.iot_provisioned = True
    terminal.iot_provisioned_at = terminal.iot_provisioned_at or now
    terminal.iot_last_sync_at = now
    terminal.iot_is_online = bool(iot_res.get("is_online", False))
    if iot_res.get("connected_at"):
        conn_at = iot_res["connected_at"]
        if isinstance(conn_at, str):
            with contextlib.suppress(ValueError):
                terminal.iot_last_connected_at = datetime.fromisoformat(conn_at)

    await db.commit()
    await db.refresh(terminal)

    return ProvisionTerminalResponse(
        success=bool(iot_res.get("success", True)),
        terminal_id=terminal.id,
        device_id=terminal.device_id,
        sn=terminal.sn,
        org_id=terminal.org_id,
        iot_provisioned=terminal.iot_provisioned,
        iot_provisioned_at=terminal.iot_provisioned_at,
        iot_is_online=terminal.iot_is_online,
        rmq_user_status=iot_res.get("rmq_user_status"),
        error=iot_res.get("error"),
    )


@router.post(
    "/terminals/provision-iot-batch",
    response_model=BatchProvisionTerminalsResponse,
)
async def provision_batch_terminals_to_iot(
    body: BatchProvisionTerminalsRequest,
    db: AsyncSession = Depends(get_db),
    user: dict = Depends(require_superuser),
) -> BatchProvisionTerminalsResponse:
    """Batch provision multiple terminals into Leo4 IoT Platform (Superuser only)."""
    if not body.terminal_ids:
        return BatchProvisionTerminalsResponse(results=[])

    res = await db.execute(select(Terminal).where(Terminal.id.in_(body.terminal_ids)))
    terminals = res.scalars().all()
    if not terminals:
        return BatchProvisionTerminalsResponse(results=[])

    terminals_payload = [
        {
            "device_id": t.device_id,
            "sn": t.sn,
            "org_id": t.org_id,
            "tags": {"source": "etranprocessing"},
        }
        for t in terminals
    ]

    try:
        batch_results = await iot_client.provision_batch_terminals(terminals_payload)
    except Exception as e:
        logger.exception("Failed batch provisioning in Leo4 IoT")
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"Failed to communicate with Leo4 IoT Platform: {e}",
        ) from e

    now = datetime.now(UTC)
    results_map = {r["device_id"]: r for r in batch_results if "device_id" in r}

    responses: list[ProvisionTerminalResponse] = []
    for t in terminals:
        r_info = results_map.get(t.device_id, {})
        t.iot_provisioned = True
        t.iot_provisioned_at = t.iot_provisioned_at or now
        t.iot_last_sync_at = now
        t.iot_is_online = bool(r_info.get("is_online", False))
        responses.append(
            ProvisionTerminalResponse(
                success=bool(r_info.get("success", True)),
                terminal_id=t.id,
                device_id=t.device_id,
                sn=t.sn,
                org_id=t.org_id,
                iot_provisioned=t.iot_provisioned,
                iot_provisioned_at=t.iot_provisioned_at,
                iot_is_online=t.iot_is_online,
                rmq_user_status=r_info.get("rmq_user_status"),
                error=r_info.get("error"),
            )
        )
    await db.commit()
    return BatchProvisionTerminalsResponse(results=responses)
