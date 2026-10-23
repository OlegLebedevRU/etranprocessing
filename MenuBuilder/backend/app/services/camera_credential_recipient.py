"""Fetch a current public recipient from the PB authority, never from telemetry."""

from dataclasses import dataclass
from datetime import UTC, datetime

import httpx
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from pydantic import AwareDatetime, BaseModel, ConfigDict, Field, ValidationError

from app.config import settings


class RecipientUnavailable(ValueError):
    def __init__(self) -> None:
        super().__init__("credential_recipient_unavailable")


class _RecipientResponse(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True, hide_input_in_errors=True)
    sn: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9._-]+$")
    device_id: int = Field(gt=0)
    cert_serial: str = Field(min_length=1, max_length=40, pattern=r"^[0-9a-fA-F]+$")
    recipient_cert_sha256: str = Field(pattern=r"^[0-9a-f]{64}$")
    certificate_pem: str = Field(max_length=8192)
    not_valid_after: AwareDatetime


@dataclass(frozen=True)
class CredentialRecipient:
    sn: str
    device_id: int
    cert_serial: str
    recipient_cert_sha256: str
    certificate: x509.Certificate

    @property
    def public_key(self) -> rsa.RSAPublicKey:
        key = self.certificate.public_key()
        if not isinstance(key, rsa.RSAPublicKey):
            raise RecipientUnavailable()
        return key

    def require_current(self, *, device_id: int, sn: str, now_ms: int) -> None:
        cert = self.certificate
        now = datetime.fromtimestamp(now_ms / 1000, UTC)
        cn = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
        if (
            self.device_id != device_id
            or self.sn != sn
            or len(cn) != 1
            or str(cn[0].value) != sn
            or format(cert.serial_number, "X").lstrip("0")
            != self.cert_serial.upper().lstrip("0")
            or cert.fingerprint(hashes.SHA256()).hex() != self.recipient_cert_sha256
            or not cert.not_valid_before_utc <= now < cert.not_valid_after_utc
            or self.public_key.key_size not in (2048, 3072, 4096)
        ):
            raise RecipientUnavailable()


async def fetch_credential_recipient(
    *, tenant_id: int, device_id: int, sn: str, client: httpx.AsyncClient | None = None
) -> CredentialRecipient:
    token = settings.processing_backend_effective_token
    if not token or tenant_id <= 0 or device_id <= 0:
        raise RecipientUnavailable()
    url = f"{settings.processing_backend_effective_url.rstrip('/')}/api/certificates/terminals/{device_id}/credential-recipient"
    try:
        if client is None:
            async with httpx.AsyncClient(
                timeout=10, follow_redirects=False
            ) as own_client:
                return await fetch_credential_recipient(
                    tenant_id=tenant_id, device_id=device_id, sn=sn, client=own_client
                )
        response = await client.get(
            url,
            params={"org_id": tenant_id},
            headers={"X-Internal-Service-Key": token},
            follow_redirects=False,
        )
        if response.status_code != 200 or len(response.content) > 16384:
            raise RecipientUnavailable()
        value = _RecipientResponse.model_validate_json(response.content)
        certificates = x509.load_pem_x509_certificates(value.certificate_pem.encode())
        if (
            len(certificates) != 1
            or value.not_valid_after != certificates[0].not_valid_after_utc
        ):
            raise RecipientUnavailable()
        recipient = CredentialRecipient(
            value.sn,
            value.device_id,
            value.cert_serial,
            value.recipient_cert_sha256,
            certificates[0],
        )
        recipient.require_current(
            device_id=device_id, sn=sn, now_ms=int(datetime.now(UTC).timestamp() * 1000)
        )
        return recipient
    except httpx.HTTPError, ValidationError, ValueError, TypeError, OverflowError:
        raise RecipientUnavailable() from None
