import json
from contextlib import suppress

from pydantic_settings import BaseSettings

DEFAULT_JWT_PUBLIC_KEY = """-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAqX393A/1A+OW9ISPrWbK
/qodWp7xMd0clA7VwGoOK6fW0Q0m7iwUoBk6PfyaUZPToIlMMn07x3OR3Idf9rdH
ZBTxYaIVBOTAC3/a9uLMi/ElLf8u1xxNffByUEbWL6lMViQgoSkxh2zLckSyvsyH
S2TMrSh4Lcq8d+QOVupgh/dOguLn8BXgVHgLtsrGJ7DqL78mwbJ6emp+LV+zeUot
xT03UwxmgFKr1AZnRGZI1Zp/xEUSYc1hX9ZzrF01jiNi1ln3NNxTqCWxpnlICxhh
QNF68AUG012qLnykaKak+9TclImCG+i+Giuj+Qc5B4j6J46kfLVZA/Ra64ugTbZy
nQIDAQAB
-----END PUBLIC KEY-----"""


class Settings(BaseSettings):
    database_url: str = ""
    cors_origins: list[str] = []

    # JWT Settings (RS256 terminated at Nginx and verified in backend)
    jwt_algorithm: str = "RS256"
    jwt_public_key: str = DEFAULT_JWT_PUBLIC_KEY
    jwt_private_key: str | None = None
    jwt_secret_hex: str = ""
    jwt_expire_minutes: int = 60  # 60 minutes for access token
    jwt_refresh_expire_days: int = 30  # 30 days for refresh token
    trust_proxy_identity_headers: bool = False
    # Verify aud/iss of RS256 tokens against jwt_issuer_aud / jwt_issuer_iss
    jwt_verify_audience: bool = True

    @property
    def jwt_secret_bytes(self) -> bytes:
        if self.jwt_secret_hex:
            return bytes.fromhex(self.jwt_secret_hex)
        return b""

    # External JWT Issuer (Yandex Cloud Function / API Gateway)
    jwt_issuer_url: str = (
        "https://d5ducjnc3s38o5qqk84q.apigw.yandexcloud.net/etranprocessing/jwt"
    )
    service_to_yc_service_secret: str = "CHANGE_ME_SERVICE_TO_SERVICE_SECRET"
    jwt_issuer_client_id: str = "menubuilder-backend"
    jwt_issuer_aud: str = "menubuilder"
    jwt_issuer_iss: str = "external-jwt-issuer"
    jwt_issuer_kid: str = "menubuilder-rs256-key-1"
    jwt_issuer_timeout_seconds: float = 10.0
    jwt_issuer_mock_enabled: bool = False
    jwt_issuer_token_cache_enabled: bool = False
    session_cleanup_enabled: bool = True

    # Legacy auth users fallback
    auth_users: str = "[]"

    def get_users(self) -> list[dict]:
        raw = self.auth_users
        if not raw:
            return []
        parsed = raw
        if isinstance(raw, str):
            with suppress(json.JSONDecodeError, ValueError, TypeError):
                parsed = json.loads(raw)
            if not isinstance(parsed, (dict, list)):
                return []

        # Handle double-encoded JSON string
        if isinstance(parsed, str):
            with suppress(json.JSONDecodeError, ValueError, TypeError):
                parsed = json.loads(parsed)
            if not isinstance(parsed, (dict, list)):
                return []

        users: list[dict] = []
        if isinstance(parsed, list):
            for item in parsed:
                if isinstance(item, dict):
                    users.append(item)
                elif isinstance(item, str):
                    is_su = item.lower() in ("admin", "superuser")
                    users.append(
                        {
                            "username": item,
                            "role": "superuser" if is_su else "user",
                            "is_superuser": is_su,
                            "role_id": 1 if is_su else 3,
                            "id": 1 if is_su else 0,
                        }
                    )
        elif isinstance(parsed, dict):
            if "username" in parsed:
                users.append(parsed)
            else:
                for uname, val in parsed.items():
                    if isinstance(val, dict):
                        rec = dict(val)
                        rec.setdefault("username", uname)
                        users.append(rec)
                    elif isinstance(val, str):
                        is_su = uname.lower() in ("admin", "superuser")
                        users.append(
                            {
                                "username": uname,
                                "md5_password": val,
                                "role": "superuser" if is_su else "user",
                                "is_superuser": is_su,
                                "role_id": 1 if is_su else 3,
                                "id": 1 if is_su else 0,
                            }
                        )
        return users

    # Billing
    billing_due_soon_days: int = 30

    # Certificate PIN billing
    cert_pin_ttl_hours: int = 24
    cert_expiring_soon_days: int = 30

    # Serverless Email Gateway
    serverless_email_gateway_url: str = (
        "https://d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net"
    )
    frontend_base_url: str = "https://leo4.ru"

    # Leo4 IoT Platform Provisioning & Internal API
    leo4_internal_api_base_url: str = ""
    leo4_internal_service_token: str = ""
    internal_service_key: str = ""
    iot_rpc_base_url: str = ""
    iot_rpc_service_token: str = ""
    iot_rpc_timeout_seconds: float = 10.0

    @property
    def internal_api_base_url(self) -> str:
        return self.leo4_internal_api_base_url or self.iot_rpc_base_url or ""

    @property
    def internal_service_key_value(self) -> str:
        return (
            self.leo4_internal_service_token
            or self.internal_service_key
            or self.iot_rpc_service_token
        )

    # L4media Video Surveillance
    l4media_ingress_url: str = "http://l4media-ingress:9100"
    l4media_janus_url: str = "http://l4media-janus:8088/janus"
    video_port_base: int = 6000
    video_port_slots: int = 50

    model_config = {"env_file": ".env", "extra": "ignore"}


settings = Settings()
