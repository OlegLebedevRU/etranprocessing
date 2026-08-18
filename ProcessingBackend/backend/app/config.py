from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = ""
    cors_origins: list[str] = []

    # Billing
    billing_due_soon_days: int = 30

    model_config = {"env_file": ".env", "extra": "ignore"}


settings = Settings()
