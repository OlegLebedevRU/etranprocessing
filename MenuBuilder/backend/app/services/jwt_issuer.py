from __future__ import annotations

import asyncio
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

ISSUER_V2_ACCESS_MANDATORY_CLAIMS: frozenset[str] = frozenset(
    {
        "orgId",
        "userId",
        "roleId",
        "sub",
        "org",
        "role",
        "aud",
        "iss",
        "iat",
        "nbf",
        "exp",
        "jti",
        "is_superuser",
        "token_type",
        "is_imp",
    }
)

ISSUER_V2_ACCESS_CLAIMS: frozenset[str] = frozenset(
    ISSUER_V2_ACCESS_MANDATORY_CLAIMS
    | {
        "username",
        "orig_sub",
        "sid",
    }
)


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
        self._inflight: dict[tuple[int, int], asyncio.Future[dict[str, Any]]] = {}
        self._token_cache: dict[tuple[int, int, Any], tuple[dict[str, Any], float]] = {}

    def invalidate_cache_for_user(self, user_id: int) -> None:
        """Invalidate all cached tokens for a specific user upon logout or deactivation."""
        keys_to_remove = [key for key in self._token_cache if key[0] == user_id]
        for key in keys_to_remove:
            self._token_cache.pop(key, None)

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
        *,
        username: str | None = None,
        is_superuser: bool | None = None,
        is_imp: bool | None = None,
        orig_sub: str | None = None,
        sid: str | int | None = None,
        access_ttl_minutes: int | None = None,
        refresh_ttl_days: int | None = None,
    ) -> dict[str, Any]:
        """Build the HMAC-SHA256 signed request payload according to the jwt-issuer contract."""
        timestamp = int(time.time())
        nonce = str(uuid.uuid4())
        effective_org_id = org_id or 0

        request_body_dict: dict[str, Any] = {
            "orgId": effective_org_id,
            "userId": user_id,
            "roleId": role_id,
            "clientId": self.client_id,
            "aud": self.aud,
            "iss": self.iss,
            "nonce": nonce,
            "timestamp": timestamp,
        }
        if username is not None:
            request_body_dict["username"] = username
        if is_superuser is not None:
            request_body_dict["isSuperuser"] = is_superuser
        if is_imp is not None:
            request_body_dict["isImp"] = is_imp
        if orig_sub is not None:
            request_body_dict["origSub"] = orig_sub
        if sid is not None:
            request_body_dict["sid"] = sid
        if access_ttl_minutes is not None:
            request_body_dict["accessTtlMinutes"] = access_ttl_minutes
        if refresh_ttl_days is not None:
            request_body_dict["refreshTtlDays"] = refresh_ttl_days

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
        *,
        sid: int | str | None = None,
        orig_sub: str | None = None,
    ) -> dict[str, Any]:
        """Generate mock JWT tokens matching external issuer v2 contract for local development and unit tests."""
        effective_org_id = org_id or 0
        now = datetime.now(UTC)
        access_exp = now + timedelta(minutes=settings.jwt_expire_minutes)
        refresh_token = secrets.token_urlsafe(64)
        iat = int(now.timestamp())
        nbf = iat
        exp = int(access_exp.timestamp())
        jti = secrets.token_hex(16)
        is_imp = bool(is_superuser and effective_org_id > 0)

        payload: dict[str, Any] = {
            "orgId": effective_org_id,
            "userId": user_id,
            "roleId": role_id,
            "sub": str(user_id),
            "org": str(effective_org_id),
            "role": str(role_id),
            "aud": self.aud,
            "iss": self.iss,
            "iat": iat,
            "nbf": nbf,
            "exp": exp,
            "jti": jti,
            "is_superuser": bool(is_superuser),
            "token_type": "tenant",
            "is_imp": is_imp,
        }
        if username:
            payload["username"] = username
            payload["orig_sub"] = orig_sub or username
        elif orig_sub:
            payload["orig_sub"] = orig_sub

        if sid is not None:
            payload["sid"] = sid

        # For mock testing, sign with RS256 if RSA key is configured, else HS256 with mock_secret
        headers = {"kid": self.kid}
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
        *,
        sid: int | str | None = None,
        orig_sub: str | None = None,
    ) -> dict[str, Any]:
        """Issue access and refresh tokens from external jwt-issuer or mock in test mode."""
        effective_org_id = org_id or 0
        cache_key = (user_id, effective_org_id, sid)
        inflight_key = (user_id, effective_org_id)

        # Optional token cache lookup (serve if > 10 min remaining)
        if settings.jwt_issuer_token_cache_enabled and cache_key in self._token_cache:
            cached_data, exp_timestamp = self._token_cache[cache_key]
            if exp_timestamp - time.time() > 600:
                logger.debug(
                    "Serving cached token for user_id=%s org_id=%s sid=%s",
                    user_id,
                    effective_org_id,
                    sid,
                )
                return cached_data

        if self.mock_enabled or not self.secret:
            logger.info(
                "Using mock JWT issuer (url=%s, mock_enabled=%s, has_secret=%s) for user_id=%s org_id=%s role_id=%s sid=%s",
                self.url,
                self.mock_enabled,
                bool(self.secret),
                user_id,
                org_id,
                role_id,
                sid,
            )
            mock_tokens = self._generate_mock_tokens(
                user_id=user_id,
                org_id=org_id,
                role_id=role_id,
                username=username,
                role=role,
                is_superuser=is_superuser,
                sid=sid,
                orig_sub=orig_sub,
            )
            if settings.jwt_issuer_token_cache_enabled:
                exp_seconds = float(
                    mock_tokens.get("expiresIn") or settings.jwt_expire_minutes * 60
                )
                self._token_cache[cache_key] = (mock_tokens, time.time() + exp_seconds)
            return mock_tokens

        if inflight_key in self._inflight:
            return await asyncio.shield(self._inflight[inflight_key])

        loop = asyncio.get_running_loop()
        future: asyncio.Future[dict[str, Any]] = loop.create_future()
        self._inflight[inflight_key] = future

        try:
            result = await self._issue_tokens_with_retry(
                user_id=user_id,
                org_id=org_id,
                role_id=role_id,
                username=username,
                role=role,
                is_superuser=is_superuser,
                sid=sid,
                orig_sub=orig_sub,
            )
            if settings.jwt_issuer_token_cache_enabled:
                exp_seconds = float(
                    result.get("expiresIn")
                    or result.get("expires_in")
                    or settings.jwt_expire_minutes * 60
                )
                self._token_cache[cache_key] = (result, time.time() + exp_seconds)
            future.set_result(result)
            return result
        except BaseException as exc:
            future.set_exception(exc)
            raise
        finally:
            self._inflight.pop(inflight_key, None)

    async def _issue_tokens_with_retry(
        self,
        user_id: int,
        org_id: int | None,
        role_id: int,
        username: str = "",
        role: str = "user",
        is_superuser: bool = False,
        *,
        sid: int | str | None = None,
        orig_sub: str | None = None,
    ) -> dict[str, Any]:
        start_time = time.monotonic()
        is_imp = bool(is_superuser and (org_id or 0) > 0)
        payload = self.build_signed_request(
            user_id=user_id,
            org_id=org_id,
            role_id=role_id,
            username=username or None,
            is_superuser=is_superuser,
            is_imp=is_imp,
            orig_sub=orig_sub or username or None,
            sid=sid,
            access_ttl_minutes=settings.jwt_expire_minutes,
            refresh_ttl_days=settings.jwt_refresh_expire_days,
        )

        logger.info(
            "Sending request to JWT issuer at %s for user_id=%s org_id=%s role_id=%s (clientId=%s)",
            self.url,
            user_id,
            org_id or 0,
            role_id,
            self.client_id,
        )

        last_error: Exception | None = None
        for attempt in range(2):
            try:
                async with httpx.AsyncClient(timeout=self.timeout) as client:
                    resp = await client.post(self.url, json=payload)
                    if resp.status_code >= 500:
                        logger.warning(
                            "JWT issuer at %s returned HTTP %s on attempt %d: %s",
                            self.url,
                            resp.status_code,
                            attempt + 1,
                            resp.text,
                        )
                        if attempt == 0:
                            await asyncio.sleep(0.5)
                            continue
                        took = time.monotonic() - start_time
                        logger.info(
                            "JWT issuer call failed: user_id=%s org_id=%s took=%.2fs url=%s",
                            user_id,
                            org_id or 0,
                            took,
                            self.url,
                        )
                        raise HTTPException(
                            status_code=status.HTTP_502_BAD_GATEWAY,
                            detail=f"JWT issuer service error: HTTP {resp.status_code}",
                        )
                    if resp.status_code != 200:
                        logger.error(
                            "JWT issuer at %s returned HTTP %s: %s",
                            self.url,
                            resp.status_code,
                            resp.text,
                        )
                        took = time.monotonic() - start_time
                        logger.info(
                            "JWT issuer call failed: user_id=%s org_id=%s took=%.2fs url=%s",
                            user_id,
                            org_id or 0,
                            took,
                            self.url,
                        )
                        raise HTTPException(
                            status_code=status.HTTP_502_BAD_GATEWAY,
                            detail=f"JWT issuer service error: HTTP {resp.status_code}",
                        )
                    data = resp.json()
                    took = time.monotonic() - start_time
                    if (
                        isinstance(data, dict)
                        and "body" in data
                        and isinstance(data["body"], dict)
                    ):
                        data = data["body"]
                    if isinstance(data, dict) and "error" in data:
                        logger.error(
                            "JWT issuer at %s returned error for user_id=%s role_id=%s: %s",
                            self.url,
                            user_id,
                            role_id,
                            data["error"],
                        )
                        raise HTTPException(
                            status_code=status.HTTP_502_BAD_GATEWAY,
                            detail=f"JWT issuer error: {data['error']}",
                        )
                    logger.info(
                        "JWT issuer at %s successfully issued tokens for user_id=%s org_id=%s took=%.2fs",
                        self.url,
                        user_id,
                        org_id or 0,
                        took,
                    )
                    return data
            except httpx.RequestError as exc:
                last_error = exc
                logger.warning(
                    "Failed to connect to JWT issuer at %s on attempt %d: %s",
                    self.url,
                    attempt + 1,
                    exc,
                )
                if attempt == 0:
                    await asyncio.sleep(0.5)
                    continue

        took = time.monotonic() - start_time
        logger.info(
            "jwt issuer call user_id=%s org_id=%s took=%.2fs",
            user_id,
            org_id or 0,
            took,
        )
        if self.mock_enabled:
            return self._generate_mock_tokens(
                user_id=user_id,
                org_id=org_id,
                role_id=role_id,
                username=username,
                role=role,
                is_superuser=is_superuser,
                sid=sid,
                orig_sub=orig_sub,
            )
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="JWT issuer service is temporarily unavailable",
        ) from last_error


jwt_issuer_client = JwtIssuerClient()
