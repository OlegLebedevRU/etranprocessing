from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = ""
    cors_origins: list[str] = []

    # Auto-populate cert_serial on licensebilling if unset (e.g. legacy migrated terminal)
    auto_set_cert_serial_on_licensebilling: bool = True

    # Transition period fallback: allow authenticating terminals by OU (device_id) and O (org_id)
    # when CN does not match DB sn, logging the real certificate details into terminal_cert_discovery.
    transition_ou_fallback_auth: bool = True

    # Service-to-service authentication token for internal contracts (e.g. MenuBuilder -> ProcessingBackend)
    service_auth_token: str = ""
    # Optional alias for backward compatibility / env vars (e.g. INTERNAL_SERVICE_KEY)
    internal_service_key: str = ""

    # Default PIN TTL in seconds for certificate enrollment (24 hours)
    cert_pin_ttl_seconds: int = 86400

    model_config = {"env_file": ".env", "extra": "ignore"}


settings = Settings()
