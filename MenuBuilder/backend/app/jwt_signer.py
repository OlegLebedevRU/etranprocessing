"""Private HTTP signer; run one worker in a dedicated unprivileged container."""

import base64
import hashlib
import hmac
import secrets
import threading
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from fastapi import FastAPI, HTTPException, Request
from jose import jwt
from pydantic import AliasChoices, BaseModel, ConfigDict, Field, ValidationError
from pydantic_settings import BaseSettings, SettingsConfigDict

from app.jwt_public_key import DEFAULT_JWT_PUBLIC_KEY


class SignerSettings(BaseSettings):
    model_config = SettingsConfigDict(extra="ignore")

    private_key_file: str = Field(
        default="", validation_alias="JWT_SIGNER_PRIVATE_KEY_FILE"
    )
    public_key: str = Field(
        default=DEFAULT_JWT_PUBLIC_KEY, validation_alias="JWT_PUBLIC_KEY"
    )
    service_secret: str = Field(
        default="",
        validation_alias=AliasChoices(
            "JWT_ISSUER_INTERNAL_SECRET", "SERVICE_TO_YC_SERVICE_SECRET"
        ),
    )
    client_id: str = Field(
        default="menubuilder-backend", validation_alias="JWT_ISSUER_CLIENT_ID"
    )
    audience: str = Field(default="menubuilder", validation_alias="JWT_ISSUER_AUD")
    issuer: str = Field(
        default="external-jwt-issuer", validation_alias="JWT_ISSUER_ISS"
    )
    kid: str = Field(
        default="menubuilder-rs256-key-1", validation_alias="JWT_ISSUER_KID"
    )
    access_minutes: int = Field(default=60, ge=1, validation_alias="JWT_EXPIRE_MINUTES")
    refresh_days: int = Field(
        default=30, ge=1, validation_alias="JWT_REFRESH_EXPIRE_DAYS"
    )
    request_max_age_seconds: int = Field(
        default=120, ge=1, le=300, validation_alias="JWT_SIGNER_REQUEST_MAX_AGE_SECONDS"
    )
    nonce_capacity: int = Field(
        default=4096, ge=1, validation_alias="JWT_SIGNER_NONCE_CAPACITY"
    )


class SignedBody(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)

    orgId: int = Field(ge=0)
    userId: int = Field(gt=0)
    roleId: int = Field(gt=0)
    clientId: str
    aud: str
    iss: str
    nonce: str = Field(min_length=1, max_length=128)
    timestamp: int
    username: str | None = Field(default=None, max_length=256)
    isSuperuser: bool = False
    isImp: bool | None = None
    origSub: str | None = Field(default=None, max_length=256)
    sid: int | str | None = None
    accessTtlMinutes: int | None = Field(default=None, ge=1)
    refreshTtlDays: int | None = Field(default=None, ge=1)


class SignedParams(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)

    orgId: int
    userId: int
    roleId: int
    clientId: str
    aud: str
    iss: str
    nonce: str = Field(min_length=1, max_length=128)
    timestamp: int
    signature: str = Field(max_length=128)
    requestBody: str = Field(max_length=8192)
    kid: str


class SignedRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    params: SignedParams


class JwtSigner:
    def __init__(self, config: SignerSettings) -> None:
        self.config = config
        if (
            not config.service_secret
            or config.service_secret == "CHANGE_ME_SERVICE_TO_SERVICE_SECRET"
        ):
            raise ValueError("JWT signer requires a service authentication secret")
        key = serialization.load_pem_private_key(
            Path(config.private_key_file).read_bytes(), password=None
        )
        public = serialization.load_pem_public_key(config.public_key.encode())
        if not isinstance(key, rsa.RSAPrivateKey) or key.key_size < 2048:
            raise ValueError(
                "JWT signer requires an RSA private key of at least 2048 bits"
            )
        if (
            not isinstance(public, rsa.RSAPublicKey)
            or key.public_key().public_numbers() != public.public_numbers()
        ):
            raise ValueError("JWT signing and verification keys do not match")
        self._key = key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
        self._lock = threading.Lock()
        self._nonces: dict[str, tuple[float, str, dict[str, Any]]] = {}

    def issue(self, params: SignedParams) -> dict[str, Any]:
        config = self.config
        now = int(time.time())
        signed = f"{params.timestamp}.{params.nonce}.{params.requestBody}".encode()
        expected = "sha256=" + base64.b64encode(
            hmac.new(config.service_secret.encode(), signed, hashlib.sha256).digest()
        ).decode("ascii")
        if not hmac.compare_digest(expected.encode(), params.signature.encode()):
            raise HTTPException(401, "Invalid request signature")
        if not now - config.request_max_age_seconds <= params.timestamp <= now + 30:
            raise HTTPException(401, "Expired or future request timestamp")
        try:
            body = SignedBody.model_validate_json(params.requestBody)
        except ValidationError as exc:
            raise HTTPException(400, "Invalid signed request body") from exc
        for field in (
            "orgId",
            "userId",
            "roleId",
            "clientId",
            "aud",
            "iss",
            "nonce",
            "timestamp",
        ):
            if getattr(body, field) != getattr(params, field):
                raise HTTPException(400, "Request envelope differs from signed body")
        if (body.clientId, body.aud, body.iss, params.kid) != (
            config.client_id,
            config.audience,
            config.issuer,
            config.kid,
        ):
            raise HTTPException(403, "Unsupported issuer context")
        is_imp = body.isSuperuser and body.orgId > 0
        if body.isImp is not None and body.isImp != is_imp:
            raise HTTPException(400, "Invalid impersonation context")
        access_minutes = body.accessTtlMinutes or config.access_minutes
        refresh_days = body.refreshTtlDays or config.refresh_days
        if access_minutes > config.access_minutes or refresh_days > config.refresh_days:
            raise HTTPException(400, "Requested token lifetime exceeds issuer policy")
        digest = hashlib.sha256(signed).hexdigest()
        # Identical retries are idempotent. A nonce reused for another signed body is rejected.
        # One process/worker owns the bounded cache for this service instance.
        with self._lock:
            expired = [nonce for nonce, entry in self._nonces.items() if entry[0] < now]
            for nonce in expired:
                del self._nonces[nonce]
            previous = self._nonces.get(body.nonce)
            if previous:
                if previous[1] != digest:
                    raise HTTPException(409, "Nonce already used")
                return dict(previous[2])
            if len(self._nonces) >= config.nonce_capacity:
                raise HTTPException(429, "Issuer request capacity reached")
            claims: dict[str, Any] = {
                "orgId": body.orgId,
                "userId": body.userId,
                "roleId": body.roleId,
                "sub": str(body.userId),
                "org": str(body.orgId),
                "role": str(body.roleId),
                "aud": config.audience,
                "iss": config.issuer,
                "iat": now,
                "nbf": now,
                "exp": now + access_minutes * 60,
                "jti": secrets.token_hex(16),
                "is_superuser": body.isSuperuser,
                "token_type": "tenant",
                "is_imp": is_imp,
            }
            if body.username:
                claims["username"] = body.username
                claims["orig_sub"] = body.origSub or body.username
            elif body.origSub:
                claims["orig_sub"] = body.origSub
            if body.sid is not None:
                claims["sid"] = body.sid
            result = {
                "accessToken": jwt.encode(
                    claims, self._key, algorithm="RS256", headers={"kid": config.kid}
                ),
                "refreshToken": secrets.token_urlsafe(64),
                "tokenType": "Bearer",
                "expiresIn": access_minutes * 60,
                "refreshExpiresIn": refresh_days * 86400,
                "orgId": body.orgId,
                "userId": body.userId,
                "roleId": body.roleId,
            }
            self._nonces[body.nonce] = (
                params.timestamp + config.request_max_age_seconds,
                digest,
                result,
            )
            return dict(result)


@asynccontextmanager
async def lifespan(app: FastAPI):
    app.state.signer = JwtSigner(SignerSettings())
    yield


app = FastAPI(lifespan=lifespan, docs_url=None, redoc_url=None, openapi_url=None)


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.post("/etranprocessing/jwt")
def issue_tokens(body: SignedRequest, request: Request) -> dict[str, Any]:
    return request.app.state.signer.issue(body.params)
