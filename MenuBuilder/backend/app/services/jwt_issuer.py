from __future__ import annotations

import base64
import hashlib
import hmac
import json
import logging
import secrets
import time
import uuid
from datetime import UTC, datetime, timedelta
from typing import Any

import httpx
from fastapi import HTTPException, status
from jose import jwt

from app.config import settings

logger = logging.getLogger(__name__)


class JwtIssuerClient:
    """Client for external RS256 JWT Issuer hosted on Yandex Cloud Function."""

    def __init__(
        self,
        url: str | None = None,
        secret: str | None = None,
        mock_enabled: bool | None = None,
    ) -> None:
        self._url = url
        self._secret = secret
        self._mock_enabled = mock_enabled
        self._client_id = settings.jwt_issuer_client_id
        self._aud = settings.jwt_issuer_aud
        self._iss = settings.jwt_issuer_iss
        self._kid = settings.jwt_issuer_kid
        self._timeout = settings.jwt_issuer_timeout_seconds

    @property
    def url(self) -> str:
        return self._url if self._url is not None else settings.jwt_issuer_url

    @url.setter
    def url(self, value: str) -> None:
        self._url = value

    @property
    def secret(self) -> str:
        return (
            self._secret
            if self._secret is not None
            else settings.service_to_yc_service_secret
        )

    @secret.setter
    def secret(self, value: str) -> None:
        self._secret = value

    @property
    def client_id(self) -> str:
        return self._client_id

    @property
    def aud(self) -> str:
        return self._aud

    @property
    def iss(self) -> str:
        return self._iss

    @property
    def kid(self) -> str:
        return self._kid

    @property
    def timeout(self) -> float:
        return self._timeout

    @property
    def mock_enabled(self) -> bool:
        return (
            self._mock_enabled
            if self._mock_enabled is not None
            else settings.jwt_issuer_mock_enabled
        )

    @mock_enabled.setter
    def mock_enabled(self, value: bool) -> None:
        self._mock_enabled = value

    def build_signed_request(
        self,
        user_id: int,
        org_id: int | None,
        role_id: int,
    ) -> dict[str, Any]:
        """Build the HMAC-SHA256 signed request payload according to the jwt-issuer contract."""
        timestamp = int(time.time())
        nonce = str(uuid.uuid4())
        effective_org_id = org_id or 0

        request_body_dict = {
            "orgId": effective_org_id,
            "userId": user_id,
            "roleId": role_id,
            "clientId": self.client_id,
            "aud": self.aud,
            "iss": self.iss,
            "nonce": nonce,
            "timestamp": timestamp,
        }
        # Compact JSON without whitespace
        request_body = json.dumps(request_body_dict, separators=(",", ":"))

        signed_payload = f"{timestamp}.{nonce}.{request_body}"
        secret_bytes = self.secret.encode("utf-8")
        h = hmac.new(secret_bytes, signed_payload.encode("utf-8"), hashlib.sha256)
        signature = "sha256=" + base64.b64encode(h.digest()).decode("ascii")

        return {
            "params": {
                "orgId": effective_org_id,
                "userId": user_id,
                "roleId": role_id,
                "clientId": self.client_id,
                "aud": self.aud,
                "iss": self.iss,
                "nonce": nonce,
                "timestamp": timestamp,
                "signature": signature,
                "requestBody": request_body,
                "kid": self.kid,
            }
        }

    def _generate_mock_tokens(
        self,
        user_id: int,
        org_id: int | None,
        role_id: int,
        username: str = "",
        role: str = "user",
        is_superuser: bool = False,
    ) -> dict[str, Any]:
        """Generate mock JWT tokens for local development and unit tests."""
        effective_org_id = org_id or 0
        now = datetime.now(UTC)
        access_exp = now + timedelta(minutes=settings.jwt_expire_minutes)
        refresh_token = secrets.token_urlsafe(64)

        payload = {
            "sub": username or str(user_id),
            "username": username or str(user_id),
            "userId": user_id,
            "user_id": user_id,
            "orgId": effective_org_id,
            "org_id": effective_org_id,
            "org": str(effective_org_id),
            "roleId": role_id,
            "role_id": role_id,
            "role": role,
            "is_superuser": is_superuser,
            "token_type": "tenant",
            "orig_sub": username or str(user_id),
            "is_imp": bool(is_superuser and effective_org_id > 1),
            "aud": self.aud,
            "iss": self.iss,
            "iat": int(now.timestamp()),
            "exp": int(access_exp.timestamp()),
        }

        # For mock testing, sign with unverified/symmetric or test key if RSA key is unavailable
        headers = {"kid": self.kid, "alg": "none"}
        priv_key = settings.jwt_private_key
        mock_access_token = (
            jwt.encode(
                payload,
                priv_key,
                algorithm="RS256",
                headers=headers,
            )
            if priv_key
            else jwt.encode(payload, "mock_secret", algorithm="HS256")
        )

        return {
            "accessToken": mock_access_token,
            "refreshToken": refresh_token,
            "tokenType": "Bearer",
            "expiresIn": settings.jwt_expire_minutes * 60,
            "refreshExpiresIn": settings.jwt_refresh_expire_days * 86400,
            "orgId": effective_org_id,
            "userId": user_id,
            "roleId": role_id,
        }

    async def issue_tokens(
        self,
        user_id: int,
        org_id: int | None,
        role_id: int,
        username: str = "",
        role: str = "user",
        is_superuser: bool = False,
    ) -> dict[str, Any]:
        """Issue access and refresh tokens from external jwt-issuer or mock in test mode."""
        if self.mock_enabled or not self.secret:
            logger.debug(
                "Using mock JWT issuer for user_id=%s org_id=%s role_id=%s",
                user_id,
                org_id,
                role_id,
            )
            return self._generate_mock_tokens(
                user_id=user_id,
                org_id=org_id,
                role_id=role_id,
                username=username,
                role=role,
                is_superuser=is_superuser,
            )

        payload = self.build_signed_request(
            user_id=user_id,
            org_id=org_id,
            role_id=role_id,
        )

        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                resp = await client.post(self.url, json=payload)
                if resp.status_code != 200:
                    logger.error("JWT issuer HTTP %s: %s", resp.status_code, resp.text)
                    raise HTTPException(
                        status_code=status.HTTP_502_BAD_GATEWAY,
                        detail=f"JWT issuer service error: HTTP {resp.status_code}",
                    )
                data = resp.json()
                if (
                    isinstance(data, dict)
                    and "body" in data
                    and isinstance(data["body"], dict)
                ):
                    return data["body"]
                return data
        except httpx.RequestError as exc:
            logger.error("Failed to connect to JWT issuer at %s: %s", self.url, exc)
            if self.mock_enabled:
                return self._generate_mock_tokens(
                    user_id=user_id,
                    org_id=org_id,
                    role_id=role_id,
                    username=username,
                    role=role,
                    is_superuser=is_superuser,
                )
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="JWT issuer service is temporarily unavailable",
            ) from exc


jwt_issuer_client = JwtIssuerClient()
