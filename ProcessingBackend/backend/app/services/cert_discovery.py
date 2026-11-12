"""Terminal certificate discovery service.

Records unique terminal certificate combinations observed at ingress / mirror points
without spamming application logs, storing structured validation diagnostics in PostgreSQL.
"""

import logging
from collections import OrderedDict
from contextlib import suppress
from datetime import UTC, datetime
from time import monotonic

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import TerminalCertDiscovery

logger = logging.getLogger(__name__)

# Sampling only successful FM hello observations, never authentication decisions.
# Per-process, bounded, reset on restart; counters represent persisted samples.
_fm_samples: OrderedDict[tuple, float] = OrderedDict()
FM_DISCOVERY_SAMPLE_SECONDS = 300
FM_DISCOVERY_SAMPLE_LIMIT = 4096


async def record_terminal_discovery(
    db: AsyncSession,
    *,
    sn: str | None,
    cert_serial: str | None,
    cert_dn: str | None,
    ou: str | None = None,
    o: str | None = None,
    is_valid: bool,
    validation_status: str,
    terminal_id: int | None = None,
    db_cert_serial: str | None = None,
    endpoint: str | None = None,
    client_ip: str | None = None,
) -> TerminalCertDiscovery | None:
    """Record or update terminal certificate discovery in the accumulator table.

    Groups by (sn, cert_serial) to track unique terminal/certificate appearances,
    request counts, and validation status without creating duplicate rows or spamming logs.
    """
    sample_key = None
    if not is_valid:
        for key in list(_fm_samples):
            if key[:2] == (sn, cert_serial):
                _fm_samples.pop(key, None)
    if (
        endpoint == "/api/file-manager/v1/agent/hello"
        and is_valid
        and validation_status == "authenticated"
    ):
        sample_key = (
            sn,
            cert_serial,
            cert_dn,
            ou,
            o,
            terminal_id,
            db_cert_serial,
            client_ip,
        )
        previous = _fm_samples.get(sample_key)
        if (
            previous is not None
            and monotonic() - previous < FM_DISCOVERY_SAMPLE_SECONDS
        ):
            return None
    try:
        stmt = select(TerminalCertDiscovery).where(
            TerminalCertDiscovery.sn == sn,
            TerminalCertDiscovery.cert_serial == cert_serial,
        )
        result = await db.execute(stmt)
        entry = result.scalar_one_or_none()

        now = datetime.now(UTC)
        if entry is not None:
            entry.request_count += 1
            entry.last_seen_at = now
            entry.is_valid = is_valid
            entry.validation_status = validation_status
            if cert_dn:
                entry.cert_dn = cert_dn
            if ou:
                entry.ou = ou
            if o:
                entry.o = o
            if terminal_id is not None:
                entry.terminal_id = terminal_id
            if db_cert_serial is not None:
                entry.db_cert_serial = db_cert_serial
            if endpoint:
                entry.last_endpoint = endpoint
            if client_ip:
                entry.client_ip = client_ip
        else:
            entry = TerminalCertDiscovery(
                sn=sn,
                cert_serial=cert_serial,
                cert_dn=cert_dn,
                ou=ou,
                o=o,
                is_valid=is_valid,
                validation_status=validation_status,
                terminal_id=terminal_id,
                db_cert_serial=db_cert_serial,
                request_count=1,
                last_endpoint=endpoint,
                client_ip=client_ip,
                first_seen_at=now,
                last_seen_at=now,
            )
            db.add(entry)

        await db.commit()
        if sample_key is not None:
            _fm_samples[sample_key] = monotonic()
            _fm_samples.move_to_end(sample_key)
            while len(_fm_samples) > FM_DISCOVERY_SAMPLE_LIMIT:
                _fm_samples.popitem(last=False)
        return entry
    except Exception as e:
        # Best-effort rollback must not obscure the original discovery failure.
        with suppress(Exception):
            await db.rollback()
        logger.debug("Failed to record terminal cert discovery: %s", e)
        if endpoint and endpoint.startswith("/api/file-manager/"):
            # FM must fail closed and use its own short database error response.
            raise
        return None
