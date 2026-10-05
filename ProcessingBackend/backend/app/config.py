from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    database_url: str = ""
    product_scope_split_enabled: bool = False
    cors_origins: list[str] = []
    file_manager_service_key: str = ""
    file_manager_iot_url: str = ""
    file_manager_iot_key: str = ""
    file_manager_s3_endpoint: str = ""
    file_manager_s3_region: str = ""
    file_manager_s3_bucket: str = ""
    file_manager_s3_access_key: str = ""
    file_manager_s3_secret_key: str = ""
    file_manager_read_roots: list[str] = []
    file_manager_write_roots: list[str] = []
    file_manager_local_drives: bool = False
    file_manager_privileged_read: bool = False
    yookassa_enabled: bool = False
    # Optional JSON endpoints map; production addresses are provisioned via env.
    leo4proxy_endpoints: str = ""
    leo4proxy_endpoints_ttl_seconds: int = 86400

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
