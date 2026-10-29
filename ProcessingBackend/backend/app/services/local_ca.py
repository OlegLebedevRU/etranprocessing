"""Terminal issuing CA. Root private keys are never loaded by ProcessingBackend."""

import ipaddress
import re
import secrets
from datetime import UTC, datetime, timedelta
from functools import lru_cache
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
from dotenv import dotenv_values

from app.config import settings
from app.services.ca_trust import terminal_issuer, terminal_root, validate_issuer
from app.services.csr_signature import valid_terminal_csr_signature


def issuing_key_password() -> bytes:
    if settings.ca_issuing_key_password_file:
        text = (
            Path(settings.ca_issuing_key_password_file)
            .read_text(encoding="utf-8-sig")
            .strip()
        )
        if re.match(r"^(?:export\s+)?[A-Za-z_][A-Za-z0-9_]*\s*=", text):
            values = dotenv_values(
                settings.ca_issuing_key_password_file, interpolate=False
            )
            value = values.get("CA_ISSUING_KEY_PASSWORD") or values.get(
                "ISSUER_KEY_PASS"
            )
            if not value:
                raise ValueError(
                    "Issuing key password file lacks the configured password"
                )
            return value.encode()
        if not text or "\n" in text:
            raise ValueError("Issuing key password file must contain one password")
        return text.encode()
    value = settings.ca_issuing_key_password.get_secret_value()
    if not value:
        raise ValueError("Issuing key password is required")
    return value.encode()


@lru_cache(maxsize=1)
def issuing_key() -> rsa.RSAPrivateKey:
    if not settings.ca_issuing_key_path:
        raise ValueError("CA_ISSUING_KEY_PATH is required for local signing")
    issuer, root = terminal_issuer(), terminal_root()
    validate_issuer(issuer, root, datetime.now(UTC))
    try:
        key = serialization.load_pem_private_key(
            Path(settings.ca_issuing_key_path).read_bytes(),
            password=issuing_key_password(),
        )
    except (ValueError, TypeError) as exc:
        # Do not expose password or OpenSSL details through terminal XML errors.
        raise ValueError("Cannot decrypt terminal issuing key") from exc
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size < 3072:
        raise ValueError("Issuing key must be RSA with at least 3072 bits")
    public = key.public_key().public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    expected = issuer.public_key().public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    if public != expected:
        raise ValueError("Issuing key does not match its certificate")
    return key


def sign_terminal_csr(csr_pem: str, sign: str, cn: str, days: int) -> x509.Certificate:
    csr = x509.load_pem_x509_csr(csr_pem.encode())
    if not valid_terminal_csr_signature(csr):
        raise ValueError("Invalid CSR signature")
    public_key = csr.public_key()
    if not isinstance(public_key, rsa.RSAPublicKey) or public_key.key_size < 2048:
        raise ValueError("Terminal CSR must contain an RSA key of at least 2048 bits")
    if not cn or not cn.isascii() or not re.fullmatch(r"[A-Za-z0-9._-]{1,128}", cn):
        raise ValueError("Invalid terminal SN")
    if not 1 <= days <= 366:
        raise ValueError("Terminal validity must be between 1 and 366 days")
    if len(sign) > 256 or any(ord(char) < 32 or ord(char) > 126 for char in sign):
        raise ValueError("Invalid terminal sign attribute")
    # Requested extensions cannot grant CA privileges or arbitrary identities.
    try:
        bc = csr.extensions.get_extension_for_class(x509.BasicConstraints).value
    except x509.ExtensionNotFound:
        bc = None
    if bc is not None and bc.ca:
        raise ValueError("Terminal CSR cannot request CA privileges")
    key = issuing_key()
    issuer, root = terminal_issuer(), terminal_root()
    now = datetime.now(UTC)
    validate_issuer(issuer, root, now)
    subject_attrs = [
        x509.NameAttribute(NameOID.COMMON_NAME, cn)
        if attr.oid == NameOID.COMMON_NAME
        else attr
        for attr in csr.subject
    ]
    if not csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME):
        subject_attrs.insert(0, x509.NameAttribute(NameOID.COMMON_NAME, cn))
    subject = x509.Name(subject_attrs)
    ou = csr.subject.get_attributes_for_oid(NameOID.ORGANIZATIONAL_UNIT_NAME)
    match = re.fullmatch(r"a\d+b(\d+)c\d+d\d+", cn)
    device_code = (
        match.group(1).zfill(7)
        if match
        else (
            "".join(filter(str.isdigit, str(ou[0].value))).zfill(7) if ou else "0000000"
        )
    )
    san: list[x509.GeneralName] = [
        x509.UniformResourceIdentifier(cn),
        x509.UniformResourceIdentifier(f"urn:leo4:terminal:{device_code}"),
        *[
            x509.DNSName(f"leo4-{device_code}.{suffix}")
            for suffix in ("device.leo4.ru", "term.leo4.ru", "internal", "local")
        ],
        x509.DNSName("localhost"),
        x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
        x509.IPAddress(ipaddress.ip_address("::1")),
    ]
    if sign:
        san.append(x509.UniformResourceIdentifier(f"urn:sign:{sign}"))
    # 159 bits, fixed nonzero high bit: 40 hex digits, never mistaken for legacy serials.
    serial = secrets.randbits(158) | (1 << 158)
    not_after = min(
        now + timedelta(days=days), issuer.not_valid_after_utc, root.not_valid_after_utc
    )
    if not_after <= now + timedelta(minutes=5):
        raise ValueError("Issuing CA is too close to expiry")
    return (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer.subject)
        .public_key(public_key)
        .serial_number(serial)
        .not_valid_before(now - timedelta(days=1))
        .not_valid_after(not_after)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=True,
                key_encipherment=True,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=False,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        # Preserve reverse HTTPS and MQTT/mTLS usages of the existing terminal profile.
        .add_extension(
            x509.ExtendedKeyUsage(
                [ExtendedKeyUsageOID.SERVER_AUTH, ExtendedKeyUsageOID.CLIENT_AUTH]
            ),
            critical=False,
        )
        .add_extension(x509.SubjectAlternativeName(san), critical=False)
        .sign(key, hashes.SHA256())
    )
