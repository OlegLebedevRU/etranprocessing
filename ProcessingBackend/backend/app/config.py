from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = ""
    cors_origins: list[str] = []

    # JWT validation (shared secret with MenuBuilder, hex-encoded)
    jwt_secret_hex: str = ""
    jwt_algorithm: str = "HS256"

    # Billing
    billing_due_soon_days: int = 30

    # Certificate PIN billing
    cert_pin_ttl_hours: int = 24
    cert_expiring_soon_days: int = 30

    model_config = {"env_file": ".env", "extra": "ignore"}

    @property
    def jwt_secret_bytes(self) -> bytes:
        return bytes.fromhex(self.jwt_secret_hex)


settings = Settings()
