"""
CA serverless function — unified entry point for old and new flows.

Routes by presence of X-CN header:
  - X-CN present  → new flow (ProcessingBackend): override CN, add sign as SAN
  - X-CN absent   → old flow (legacy): sign CSR as-is

Request headers:
  X-Ssl-Client-Csr:      URL-encoded PKCS10 PEM
  X-Ssl-Client-Exp-Days: validity in days (default 365)
  X-Sign:                (new) MD5 hash to embed as SAN URI attribute
  X-CN:                  (new) override CN in certificate subject (device SN, ASCII)

Response JSON (both flows):
  {
    "cert":          "<URL-encoded cert PEM>",
    "ca_pem":        "<URL-encoded CA cert PEM>",
    "serial_number": "<hex serial>",
    "not_valid_before": "YYYY-MM-DD HH:MM:SS",
    "not_valid_after":  "YYYY-MM-DD HH:MM:SS",
    "valid_days":    <int>,
    "sn":            "<device SN>",
    "device_id":     <int>
  }
"""

import logging
import os
import re
import urllib.parse
from datetime import UTC, datetime, timedelta

from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.x509.oid import NameOID

log = logging.getLogger(__name__)


def _extract_device_code(device_sn: str, device_id: int = 0) -> str:
    """Extract 7-digit device code from SN (characters between 'b' and 'c') or fallback to device_id."""
    m = re.search(r"^a\d+b(\d+)c", device_sn, re.IGNORECASE)
    if m:
        return m.group(1).zfill(7)
    m = re.search(r"b(\d+)c", device_sn, re.IGNORECASE)
    if m:
        return m.group(1).zfill(7)
    if device_id:
        return f"{device_id:07d}"
    return "0000000"


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------


def _load_ca():
    ca_cert_path = os.getenv("CA_CERT_PATH", "/function/storage/keys/ca.crt")
    ca_key_path = os.getenv("CA_KEY_PATH", "/function/storage/keys/ca.key")

    with open(ca_cert_path, "rb") as f:
        ca_cert = x509.load_pem_x509_certificate(f.read(), backend=default_backend())
    with open(ca_key_path, "rb") as f:
        ca_key = serialization.load_pem_private_key(
            f.read(), password=None, backend=default_backend()
        )

    return ca_cert, ca_key


def _save_cert(cert_pem_bytes: bytes, filename: str):
    certs_dir = os.getenv("CA_CERTS_DIR", "/function/storage/certs/")
    cert_path = certs_dir + filename
    try:
        with open(cert_path, "wb") as f:
            f.write(cert_pem_bytes)
    except OSError as e:
        log.warning("Could not save cert to %s: %s", cert_path, e)


def _build_response(
    certificate, ca_cert, exp_days, not_before, not_after, device_sn, device_id
):
    cert_pem = certificate.public_bytes(encoding=serialization.Encoding.PEM).decode(
        "utf-8"
    )
    ca_pem = ca_cert.public_bytes(encoding=serialization.Encoding.PEM).decode("utf-8")

    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": {
            "cert": urllib.parse.quote(cert_pem),
            "ca_pem": urllib.parse.quote(ca_pem),
            "serial_number": format(certificate.serial_number, "X"),
            "not_valid_after": not_after.strftime("%Y-%m-%d %H:%M:%S"),
            "not_valid_before": not_before.strftime("%Y-%m-%d %H:%M:%S"),
            "valid_days": exp_days,
            "sn": device_sn,
            "device_id": device_id,
        },
    }


# ---------------------------------------------------------------------------
# Old flow (legacy) — sign CSR as-is
# ---------------------------------------------------------------------------


def _sign_legacy(csr, ca_cert, ca_key, exp_days):
    """Sign CSR preserving original subject (no CN override, no SAN injection)."""
    ou_attrs = csr.subject.get_attributes_for_oid(NameOID.ORGANIZATIONAL_UNIT_NAME)
    device_id = 0
    if ou_attrs:
        digits = "".join(filter(str.isdigit, str(ou_attrs[0].value)))
        device_id = int(digits) if digits else 0

    cn_attrs = csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    device_sn = cn_attrs[0].value if cn_attrs else ""

    not_before = datetime.now(UTC).replace(tzinfo=None)
    not_after = not_before + timedelta(days=exp_days)

    builder = (
        x509.CertificateBuilder()
        .subject_name(csr.subject)
        .issuer_name(ca_cert.subject)
        .public_key(csr.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
    )

    for ext in csr.extensions:
        builder = builder.add_extension(ext.value, critical=ext.critical)

    try:
        csr.extensions.get_extension_for_class(x509.BasicConstraints)
    except x509.ExtensionNotFound:
        builder = builder.add_extension(
            x509.BasicConstraints(ca=False, path_length=None), critical=True
        )

    certificate = builder.sign(
        private_key=ca_key, algorithm=hashes.SHA256(), backend=default_backend()
    )
    _save_cert(
        certificate.public_bytes(encoding=serialization.Encoding.PEM),
        f"{device_sn}.crt",
    )

    return certificate, device_sn, device_id, not_before, not_after


# ---------------------------------------------------------------------------
# New flow (ProcessingBackend) — override CN + add SAN entries
# ---------------------------------------------------------------------------


def _sign_new(csr, ca_cert, ca_key, exp_days, override_cn, sign):
    """Sign CSR with CN override and SAN injection."""
    ou_attrs = csr.subject.get_attributes_for_oid(NameOID.ORGANIZATIONAL_UNIT_NAME)
    device_id = 0
    if ou_attrs:
        digits = "".join(filter(str.isdigit, str(ou_attrs[0].value)))
        device_id = int(digits) if digits else 0

    # Override CN, preserve all other subject attrs (O, OU, S, C, L, E)
    if override_cn:
        subject_attrs = []
        for attr in csr.subject:
            if attr.oid == NameOID.COMMON_NAME:
                subject_attrs.append(
                    x509.NameAttribute(NameOID.COMMON_NAME, override_cn)
                )
            else:
                subject_attrs.append(attr)
        subject = x509.Name(subject_attrs)
        device_sn = override_cn
    else:
        subject = csr.subject
        cn_attrs = csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
        device_sn = cn_attrs[0].value if cn_attrs else ""

    not_before = datetime.now(UTC).replace(tzinfo=None)
    not_after = not_before + timedelta(days=exp_days)

    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(ca_cert.subject)
        .public_key(csr.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
    )

    # Copy extensions from CSR (excluding SAN since we construct it)
    for ext in csr.extensions:
        if ext.oid == x509.ExtensionOID.SUBJECT_ALTERNATIVE_NAME:
            continue
        builder = builder.add_extension(ext.value, critical=ext.critical)

    # Ensure BasicConstraints
    try:
        csr.extensions.get_extension_for_class(x509.BasicConstraints)
    except x509.ExtensionNotFound:
        builder = builder.add_extension(
            x509.BasicConstraints(ca=False, path_length=None), critical=True
        )

    # Build SAN entries
    san_list: list[x509.GeneralName] = []
    try:
        existing_san = csr.extensions.get_extension_for_class(
            x509.SubjectAlternativeName
        )
        san_list.extend(existing_san.value)
    except x509.ExtensionNotFound:
        pass

    # Add device SN as URI SAN and hostname as DNS SAN
    if device_sn:
        san_list.append(x509.UniformResourceIdentifier(device_sn))
        dev_code = _extract_device_code(device_sn, device_id)
        san_list.append(x509.DNSName(f"leo4-{dev_code}.local"))

    # Add sign as SAN URI if present
    if sign:
        san_list.append(x509.UniformResourceIdentifier(f"urn:sign:{sign}"))

    if san_list:
        builder = builder.add_extension(
            x509.SubjectAlternativeName(san_list),
            critical=False,
        )

    certificate = builder.sign(
        private_key=ca_key, algorithm=hashes.SHA256(), backend=default_backend()
    )
    _save_cert(
        certificate.public_bytes(encoding=serialization.Encoding.PEM),
        f"{device_sn}.crt",
    )

    return certificate, device_sn, device_id, not_before, not_after


# ---------------------------------------------------------------------------
# Unified entry point
# ---------------------------------------------------------------------------


def sign_csr_from_headers(event, context):
    """
    Serverless entry point.

    Routes by X-CN header:
      present → new flow (CN override + SAN)
      absent  → old flow (sign as-is)
    """
    headers = event.get("headers", {})

    # Yandex Cloud converts headers to Title-Case (X-CN → X-Cn).
    h = {k.lower(): v for k, v in headers.items()}

    try:
        # --- Parse common inputs ---
        encoded_csr = h.get("x-ssl-client-csr")
        if not encoded_csr:
            raise ValueError("Header X-Ssl-Client-Csr is missing")

        csr_pem = urllib.parse.unquote(encoded_csr)

        exp_days_str = h.get("x-ssl-client-exp-days", "365")
        try:
            exp_days = int(exp_days_str)
        except ValueError, TypeError:
            log.warning("Invalid Exp-Days header: %s, using default 365", exp_days_str)
            exp_days = 365

        # --- Route by X-CN ---
        override_cn = h.get("x-cn", "")
        sign = h.get("x-sign", "")

        ca_cert, ca_key = _load_ca()
        csr = x509.load_pem_x509_csr(csr_pem.encode("utf-8"), backend=default_backend())

        if override_cn:
            # New flow: ProcessingBackend — CN override + SAN
            log.info("New flow: X-CN=%s, X-Sign=%s", override_cn, sign)
            certificate, device_sn, device_id, not_before, not_after = _sign_new(
                csr,
                ca_cert,
                ca_key,
                exp_days,
                override_cn,
                sign,
            )
        else:
            # Old flow: legacy — sign as-is
            log.info("Legacy flow: no X-CN header")
            certificate, device_sn, device_id, not_before, not_after = _sign_legacy(
                csr,
                ca_cert,
                ca_key,
                exp_days,
            )

        result = _build_response(
            certificate, ca_cert, exp_days, not_before, not_after, device_sn, device_id
        )
        log.info("Signed: sn=%s, serial=%s", device_sn, result["body"]["serial_number"])
        return result

    except Exception as e:
        log.error("Failed to sign CSR from headers: %s", e)
        return {
            "statusCode": 400,
            "body": {"error": "Certificate signing failed", "details": str(e)},
        }
