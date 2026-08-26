import json

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
    jwt_expire_minutes: int = 15  # 15 minutes for access token
    jwt_refresh_expire_days: int = 7  # 7 days for refresh token

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

    # Legacy auth users fallback
    auth_users: str = "[]"

    def get_users(self) -> list[dict]:
        return json.loads(self.auth_users)

    # Billing
    billing_due_soon_days: int = 30

    # Certificate PIN billing
    cert_pin_ttl_hours: int = 24
    cert_expiring_soon_days: int = 30

    # Leo4 IoT Platform Provisioning
    iot_rpc_base_url: str = ""
    iot_rpc_service_token: str = ""
    iot_rpc_timeout_seconds: float = 10.0

    model_config = {"env_file": ".env", "extra": "ignore"}


settings = Settings()
