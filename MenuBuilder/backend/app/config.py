import json

from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = ""
    cors_origins: list[str] = []

    # JWT secret in hex format (shared with nginx)
    jwt_secret_hex: str = ""
    jwt_algorithm: str = "HS256"
    jwt_expire_minutes: int = 480  # 8 hours

    # Auth — JSON array of users: [{"username":"...","md5_password":"...","org_id":1}]
    auth_users: str = "[]"

    def get_users(self) -> list[dict]:
        return json.loads(self.auth_users)

    @property
    def jwt_secret_bytes(self) -> bytes:
        return bytes.fromhex(self.jwt_secret_hex)

    # Billing
    billing_due_soon_days: int = 30

    # Certificate PIN billing
    cert_pin_ttl_hours: int = 24
    cert_expiring_soon_days: int = 30

    model_config = {"env_file": ".env", "extra": "ignore"}


settings = Settings()
