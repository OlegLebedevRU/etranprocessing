"""HTTP client for external CA serverless function."""

import logging
import os
import urllib.parse

import httpx

logger = logging.getLogger(__name__)

CA_URL = os.getenv("CA_URL", "https://ca.internal/sign-csr")
CA_TIMEOUT = int(os.getenv("CA_TIMEOUT", "30"))
DEFAULT_VALIDITY_DAYS = int(os.getenv("CERT_VALIDITY_DAYS", "365"))


class CAResponse:
    """Parsed response from external CA."""

    def __init__(self, cert_pem: str, ca_pem: str, serial_number: str, not_valid_after: str):
        self.cert_pem = cert_pem
        self.ca_pem = ca_pem
        self.serial_number = serial_number
        self.not_valid_after = not_valid_after


async def sign_csr(
    csr_pem: str,
    sign: str,
    cn: str = "",
    exp_days: int | None = None,
) -> CAResponse:
    """Send PKCS10 CSR to external CA for signing.

    Args:
        csr_pem: PEM-encoded PKCS10 CSR
        sign: MD5 hash to embed as SAN URI attribute
        cn: override CN in certificate subject (device SN, ASCII)
        exp_days: certificate validity in days

    Returns:
        CAResponse with cert_pem, ca_pem, serial_number, not_valid_after
    """
    days = exp_days or DEFAULT_VALIDITY_DAYS

    headers = {
        "X-Ssl-Client-Csr": urllib.parse.quote(csr_pem),
        "X-Ssl-Client-Exp-Days": str(days),
        "X-Sign": sign,
    }
    if cn:
        headers["X-CN"] = cn

    logger.info("CA sign_csr: cn=%s, sign=%s, exp_days=%d", cn, sign, days)

    async with httpx.AsyncClient(timeout=CA_TIMEOUT, verify=False) as client:
        resp = await client.post(CA_URL, headers=headers)
        resp.raise_for_status()

    data = resp.json()
    body = data.get("body", data)

    result = CAResponse(
        cert_pem=urllib.parse.unquote(body.get("cert", "")),
        ca_pem=urllib.parse.unquote(body.get("ca_pem", "")),
        serial_number=body.get("serial_number", ""),
        not_valid_after=body.get("not_valid_after", ""),
    )

    logger.info("CA response: serial=%s, valid_until=%s", result.serial_number, result.not_valid_after)
    return result
