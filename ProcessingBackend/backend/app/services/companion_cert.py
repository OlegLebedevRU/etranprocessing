"""Companion PFX certificate generation service for terminals."""

import base64
import logging
import secrets
from datetime import UTC, datetime

from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

from app.models import Terminal
from app.schemas.devices_legacy import LegacyPfxResponse
from app.services.ca import sign_csr
from app.utils.pfx import create_pfx_bundle

logger = logging.getLogger(__name__)

# Standard DN fields matching etranprocessing convention
DN_STATE = "msk"
DN_COUNTRY = "ru"
DN_EMAIL = "1.terminal@forpay.ru"


def generate_key_and_csr(terminal: Terminal) -> tuple[str, str]:
    """Generate RSA 2048 private key and PKCS#10 CSR with standard terminal Subject DN.

    Subject format:
      CN={sn}, O={org_id}, OU={device_id}, S=msk, C=ru, L={terminal_id}, E=1.terminal@forpay.ru

    Returns:
        tuple of (private_key_pem, csr_pem)
    """
    key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
        backend=default_backend(),
    )

    subject = x509.Name(
        [
            x509.NameAttribute(NameOID.COMMON_NAME, terminal.sn),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, str(terminal.org_id)),
            x509.NameAttribute(
                NameOID.ORGANIZATIONAL_UNIT_NAME, str(terminal.device_id)
            ),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, DN_STATE),
            x509.NameAttribute(NameOID.COUNTRY_NAME, DN_COUNTRY),
            x509.NameAttribute(NameOID.LOCALITY_NAME, str(terminal.id)),
            x509.NameAttribute(NameOID.EMAIL_ADDRESS, DN_EMAIL),
        ]
    )

    csr = (
        x509.CertificateSigningRequestBuilder()
        .subject_name(subject)
        .sign(key, hashes.SHA256(), default_backend())
    )

    private_key_pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    ).decode("utf-8")

    csr_pem = csr.public_bytes(serialization.Encoding.PEM).decode("utf-8")

    return private_key_pem, csr_pem


async def issue_companion_pfx_certificate(
    terminal: Terminal,
    exp_days: int = 365,
    sign: str = "temporary",
) -> LegacyPfxResponse:
    """Issue a companion PFX certificate for an authenticated terminal.

    Stateless operation: does not mutate or persist cert_serial in the database.
    Sends CSR to external CA with X-CN header for new-flow standard signing.
    Packs certificate, private key, and CA into a password-protected PFX bundle.

    Args:
        terminal: Authenticated Terminal instance
        exp_days: Certificate validity in days
        sign: Signature token embedded in SAN URI attribute (default: "temporary")

    Returns:
        LegacyPfxResponse containing base64 PFX and metadata
    """
    logger.info(
        "Issuing companion PFX for terminal id=%d, sn=%s, device_id=%d, exp_days=%d",
        terminal.id,
        terminal.sn,
        terminal.device_id,
        exp_days,
    )

    private_key_pem, csr_pem = generate_key_and_csr(terminal)

    ca_resp = await sign_csr(
        csr_pem=csr_pem,
        sign=sign,
        cn=terminal.sn,
        exp_days=exp_days,
    )

    password = f"pfx-{terminal.device_id}-{secrets.randbelow(9000) + 1000}"

    pfx_bytes = create_pfx_bundle(
        private_key_pem=private_key_pem,
        cert_pem=ca_resp.cert_pem,
        ca_pem=ca_resp.ca_pem,
        password=password,
    )

    pfx_b64 = base64.b64encode(pfx_bytes).decode("ascii")

    leaf_cert = x509.load_pem_x509_certificate(
        ca_resp.cert_pem.encode("utf-8"), default_backend()
    )

    not_before_dt = getattr(leaf_cert, "not_valid_before_utc", None)
    if not_before_dt is None:
        not_before_dt = leaf_cert.not_valid_before.replace(tzinfo=UTC)

    not_after_dt = getattr(leaf_cert, "not_valid_after_utc", None)
    if not_after_dt is None:
        not_after_dt = leaf_cert.not_valid_after.replace(tzinfo=UTC)

    now_utc = datetime.now(UTC)
    days_left = max(0, (not_after_dt - now_utc).days)

    return LegacyPfxResponse(
        pfx=pfx_b64,
        pfx_password=password,
        not_valid_before=not_before_dt.strftime("%Y-%m-%d %H:%M:%S"),
        not_valid_after=not_after_dt.strftime("%Y-%m-%d %H:%M:%S"),
        valid_days=exp_days,
        sn=terminal.sn,
        device_id=terminal.device_id,
        days_left=days_left,
    )
