from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = ""
    cors_origins: list[str] = []

    # Auto-populate cert_serial on licensebilling if unset (e.g. legacy migrated terminal)
    auto_set_cert_serial_on_licensebilling: bool = True

    # Transition period fallback: allow authenticating terminals by OU (device_id) and O (org_id)
    # when CN does not match DB sn, logging the real certificate details into terminal_cert_discovery.
    transition_ou_fallback_auth: bool = True

    model_config = {"env_file": ".env", "extra": "ignore"}


settings = Settings()
