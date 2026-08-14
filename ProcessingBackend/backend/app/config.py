from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = "postgresql+asyncpg://etran:etran@pg:5432/etranprocessing"
    cors_origins: list[str] = ["http://localhost:8080", "https://dev.leo4.ru:4443"]

    model_config = {"env_file": ".env"}


settings = Settings()
