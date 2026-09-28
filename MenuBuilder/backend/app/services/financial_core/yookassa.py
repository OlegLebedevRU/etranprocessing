from __future__ import annotations

import base64
import ipaddress
import json
import logging
import uuid
from typing import Any, Protocol
from urllib.parse import urlparse

import httpx

from app.config import settings

logger = logging.getLogger(__name__)


class YooKassaError(Exception):
    """Base exception for YooKassa integration."""


class YooKassaNetworkError(YooKassaError):
    """Network failure or timeout communicating with YooKassa."""


class YooKassaApiError(YooKassaError):
    """API-level error returned by YooKassa."""

    def __init__(self, status_code: int, code: str, description: str) -> None:
        super().__init__(f"YooKassa API error {status_code}: [{code}] {description}")
        self.status_code = status_code
        self.code = code
        self.description = description


class YooKassaClientProtocol(Protocol):
    async def create_payment(
        self,
        *,
        amount_kopecks: int,
        idempotence_key: str,
        return_url: str,
        description: str,
        customer_email: str | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]: ...

    async def get_payment(self, provider_payment_id: str) -> dict[str, Any]: ...


class YooKassaClient:
    """Production client for YooKassa REST API v3."""

    def __init__(
        self,
        shop_id: str | None = None,
        secret_key: str | None = None,
        api_url: str | None = None,
        timeout_sec: float | None = None,
    ) -> None:
        self.shop_id = shop_id or settings.yookassa_shop_id
        self.secret_key = secret_key or settings.yookassa_secret_key
        self.api_url = (api_url or settings.yookassa_api_url).rstrip("/")
        self.timeout_sec = (
            timeout_sec
            if timeout_sec is not None
            else settings.yookassa_request_timeout_sec
        )

    def _get_auth_header(self) -> dict[str, str]:
        if not self.shop_id or not self.secret_key:
            raise YooKassaApiError(
                401,
                "missing_credentials",
                "YooKassa shop_id or secret_key is not configured",
            )
        auth_bytes = f"{self.shop_id}:{self.secret_key}".encode()
        b64_auth = base64.b64encode(auth_bytes).decode("ascii")
        return {"Authorization": f"Basic {b64_auth}"}

    async def create_payment(
        self,
        *,
        amount_kopecks: int,
        idempotence_key: str,
        return_url: str,
        description: str,
        customer_email: str | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Create a payment in YooKassa with idempotency key and optional receipt."""
        rubles_str = f"{amount_kopecks / 100:.2f}"
        payload: dict[str, Any] = {
            "amount": {"value": rubles_str, "currency": "RUB"},
            "capture": True,
            "confirmation": {"type": "redirect", "return_url": return_url},
            "description": description,
            "metadata": metadata or {},
        }

        # Fiscal receipt snapshot if enabled and customer email is provided
        if settings.yookassa_receipt_enabled and customer_email:
            receipt: dict[str, Any] = {
                "customer": {"email": customer_email},
                "items": [
                    {
                        "description": settings.yookassa_item_description,
                        "quantity": "1.00",
                        "amount": {"value": rubles_str, "currency": "RUB"},
                        "vat_code": settings.yookassa_vat_code,
                        "payment_subject": settings.yookassa_payment_subject,
                        "payment_mode": settings.yookassa_payment_mode,
                    }
                ],
            }
            if settings.yookassa_tax_system_code is not None:
                receipt["tax_system_code"] = settings.yookassa_tax_system_code
            payload["receipt"] = receipt

        headers = {
            **self._get_auth_header(),
            "Idempotence-Key": idempotence_key,
            "Content-Type": "application/json",
        }

        url = f"{self.api_url}/payments"
        logger.info(
            "Sending create payment request to YooKassa url=%s amount=%s idempotence_key=%s",
            url,
            rubles_str,
            idempotence_key,
        )

        try:
            async with httpx.AsyncClient(timeout=self.timeout_sec) as client:
                resp = await client.post(url, json=payload, headers=headers)
        except (httpx.TimeoutException, httpx.NetworkError) as err:
            logger.error("Network error communicating with YooKassa: %s", err)
            raise YooKassaNetworkError(
                f"Network error contacting YooKassa: {err}"
            ) from err

        if resp.status_code not in (200, 201):
            try:
                err_body = resp.json()
                code = err_body.get("code", "unknown_error")
                desc = err_body.get("description", resp.text)
            except ValueError, json.JSONDecodeError:
                code = "http_error"
                desc = resp.text
            logger.error(
                "YooKassa error HTTP %s: [%s] %s", resp.status_code, code, desc
            )
            raise YooKassaApiError(resp.status_code, code, desc)

        return resp.json()

    async def get_payment(self, provider_payment_id: str) -> dict[str, Any]:
        """Fetch the authoritative payment state from YooKassa."""
        headers = {
            **self._get_auth_header(),
            "Content-Type": "application/json",
        }
        url = f"{self.api_url}/payments/{provider_payment_id}"
        logger.info("Fetching payment details from YooKassa: %s", url)

        try:
            async with httpx.AsyncClient(timeout=self.timeout_sec) as client:
                resp = await client.get(url, headers=headers)
        except (httpx.TimeoutException, httpx.NetworkError) as err:
            logger.error("Network error fetching payment from YooKassa: %s", err)
            raise YooKassaNetworkError(
                f"Network error contacting YooKassa: {err}"
            ) from err

        if resp.status_code != 200:
            try:
                err_body = resp.json()
                code = err_body.get("code", "unknown_error")
                desc = err_body.get("description", resp.text)
            except ValueError, json.JSONDecodeError:
                code = "http_error"
                desc = resp.text
            logger.error(
                "YooKassa GET payment error HTTP %s: [%s] %s",
                resp.status_code,
                code,
                desc,
            )
            raise YooKassaApiError(resp.status_code, code, desc)

        return resp.json()


class MockYooKassaClient:
    """Mock YooKassa client for contract tests and sandbox simulations."""

    def __init__(self, shop_id: str = "mock_shop_123456") -> None:
        self.shop_id = shop_id
        self.payments: dict[str, dict[str, Any]] = {}
        self.idempotency_map: dict[str, str] = {}
        self.simulate_timeout: bool = False
        self.simulate_error_status: int | None = None

    async def create_payment(
        self,
        *,
        amount_kopecks: int,
        idempotence_key: str,
        return_url: str,
        description: str,
        customer_email: str | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        if self.simulate_timeout:
            raise YooKassaNetworkError("Simulated mock timeout")
        if self.simulate_error_status:
            raise YooKassaApiError(
                self.simulate_error_status, "simulated_error", "Mock error trigger"
            )

        if idempotence_key in self.idempotency_map:
            prev_id = self.idempotency_map[idempotence_key]
            return self.payments[prev_id]

        payment_id = f"mock-yoo-{uuid.uuid4()}"
        rubles_str = f"{amount_kopecks / 100:.2f}"
        payment_data: dict[str, Any] = {
            "id": payment_id,
            "status": "pending",
            "paid": False,
            "amount": {"value": rubles_str, "currency": "RUB"},
            "confirmation": {
                "type": "redirect",
                "confirmation_url": f"https://yookassa.ru/checkout/payments/v2/contract?orderId={payment_id}",
            },
            "created_at": "2026-09-20T18:00:00.000Z",
            "description": description,
            "metadata": metadata or {},
            "recipient": {"account_id": self.shop_id, "gateway_id": "mock_gw"},
            "test": True,
        }

        if customer_email:
            payment_data["receipt"] = {
                "customer": {"email": customer_email},
                "items": [
                    {
                        "description": settings.yookassa_item_description,
                        "quantity": "1.00",
                        "amount": {"value": rubles_str, "currency": "RUB"},
                        "vat_code": settings.yookassa_vat_code,
                        "payment_subject": settings.yookassa_payment_subject,
                        "payment_mode": settings.yookassa_payment_mode,
                    }
                ],
            }

        self.payments[payment_id] = payment_data
        self.idempotency_map[idempotence_key] = payment_id
        return payment_data

    async def get_payment(self, provider_payment_id: str) -> dict[str, Any]:
        if self.simulate_timeout:
            raise YooKassaNetworkError("Simulated mock timeout")
        if self.simulate_error_status:
            raise YooKassaApiError(
                self.simulate_error_status, "simulated_error", "Mock error trigger"
            )
        if provider_payment_id not in self.payments:
            raise YooKassaApiError(
                404, "not_found", f"Payment {provider_payment_id} not found"
            )
        return self.payments[provider_payment_id]

    def set_payment_status(
        self,
        provider_payment_id: str,
        status: str,
        cancellation_details: dict[str, Any] | None = None,
    ) -> None:
        if provider_payment_id in self.payments:
            self.payments[provider_payment_id]["status"] = status
            self.payments[provider_payment_id]["paid"] = status == "succeeded"
            if cancellation_details:
                self.payments[provider_payment_id]["cancellation_details"] = (
                    cancellation_details
                )

    def set_payment_amount(
        self, provider_payment_id: str, amount_value: str, currency: str = "RUB"
    ) -> None:
        if provider_payment_id in self.payments:
            self.payments[provider_payment_id]["amount"] = {
                "value": amount_value,
                "currency": currency,
            }


_client_override: YooKassaClientProtocol | None = None


def set_yookassa_client_override(client: YooKassaClientProtocol | None) -> None:
    """Set or clear the global YooKassa client override (useful for testing)."""
    global _client_override
    _client_override = client


def get_yookassa_client() -> YooKassaClientProtocol:
    """Get active YooKassa client instance (or test override if configured)."""
    if _client_override is not None:
        return _client_override
    return YooKassaClient()


def is_ip_trusted(client_ip: str | None, trusted_cidrs: list[str]) -> bool:
    """Verify if client IP belongs to any of the trusted YooKassa CIDR blocks."""
    if not client_ip:
        return False
    # If proxy passes comma-separated IPs in X-Forwarded-For, check the first (client) IP
    first_ip = client_ip.split(",")[0].strip()
    try:
        ip_obj = ipaddress.ip_address(first_ip)
        for cidr in trusted_cidrs:
            net = ipaddress.ip_network(cidr.strip(), strict=False)
            if ip_obj in net:
                return True
    except ValueError:
        return False
    return False


def is_safe_return_url(url: str, allowed_base: str | None = None) -> bool:
    """Validate return URL against open redirect vulnerabilities."""
    if not url:
        return False
    # Relative path is safe if it starts with / and not //
    if url.startswith("/") and not url.startswith("//"):
        return True

    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https"):
        return False

    if allowed_base:
        allowed_parsed = urlparse(allowed_base)
        if allowed_parsed.netloc and parsed.netloc != allowed_parsed.netloc:
            return False

    return bool(parsed.netloc)
