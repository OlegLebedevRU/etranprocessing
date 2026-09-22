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
    cert_pin_ttl_hours: int = 168  # 7 days (7 * 24h)
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
    iot_rpc_stream_start_timeout_seconds: float = 20.0
    iot_rpc_inventory_timeout_seconds: float = 10.0

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
    l4media_service_token: str = ""
    video_port_base: int = 6000
    video_port_slots: int = 50

    @property
    def l4media_effective_token(self) -> str:
        return self.l4media_service_token or self.internal_service_key_value or ""

    # Remote Input Control & Unified Session Orchestration (L4D-08B-MB)
    remote_control_enabled: bool = True
    remote_control_ws_connect_timeout_sec: float = 5.0
    remote_control_click_timeout_sec: float = 7.0  # > app1 click_ack_timeout (5 s)
    l4desk_policy_enforcement_enabled: bool = (
        False  # Disabled in 08B (permissive legacy policy)
    )
    remote_session_start_timeout_sec: float = 20.0
    remote_session_watchdog_ttl_sec: int = 7200

    # IoT Contract Consumer v1 (L4Desk event feed)
    iot_event_feed_base_url: str = ""
    iot_event_feed_service_token: str = ""
    iot_event_feed_timeout_seconds: float = 10.0
    iot_event_feed_max_retries: int = 3
    iot_event_feed_retry_backoff_sec: float = 0.5
    iot_consumer_enabled: bool = False  # Dark consumer disabled by default
    iot_consumer_shadow_mode: bool = (
        True  # Shadow mode: technical ingest only, zero commercial mutation
    )
    iot_consumer_poll_interval_sec: float = 5.0
    iot_consumer_batch_size: int = 100
    iot_consumer_id: str = "menubuilder_iot_event_consumer"

    @property
    def event_feed_effective_url(self) -> str:
        return self.iot_event_feed_base_url or self.internal_api_base_url or ""

    @property
    def event_feed_effective_token(self) -> str:
        return (
            self.iot_event_feed_service_token or self.internal_service_key_value or ""
        )

    # L4Desk Dark Mode Feature Flags & Schema Compatibility (L4D-04C-MB)
    l4desk_enabled: bool = False
    l4desk_registration_enabled: bool = False
    l4desk_billing_enabled: bool = False
    l4desk_ui_enabled: bool = False
    schema_compatibility_check_enabled: bool = True
    required_alembic_revision: str = "027"

    # L4Desk Self-Registration & Security (L4D-05-MB)
    l4desk_registration_token_expire_hours: int = 24
    l4desk_terms_current_version: str = "v1"
    l4desk_rate_limit_ip_max: int = 5
    l4desk_rate_limit_ip_window_sec: int = 600
    l4desk_rate_limit_resend_cooldown_sec: int = 60
    l4desk_rate_limit_resend_max_per_hour: int = 3

    # L4Desk Terminal Onboarding (L4D-06C-MB)
    l4desk_terminal_onboarding_enabled: bool = False
    processing_backend_url: str = ""
    processing_backend_service_token: str = ""
    agent_release_url: str = (
        "https://l4tools-generic.ar.cloud.ru/l4tools/1.7.7/l4setup.exe"
    )
    agent_release_version: str = "1.7.7"

    # L4Desk Financial Core Double-Entry Subledger (L4D-09-MB)
    l4desk_financial_core_enabled: bool = True

    # L4Desk Entitlement, Grace & Notifications (L4D-12-MB)
    l4desk_policy_shadow_mode: bool = True
    l4desk_entitlement_worker_enabled: bool = False
    l4desk_entitlement_worker_interval_sec: float = 60.0
    l4desk_stop_outbox_max_retries: int = 5
    l4desk_stop_outbox_retry_interval_sec: float = 10.0
    l4desk_email_notifications_enabled: bool = True
    l4desk_notification_max_retries: int = 3

    # YooKassa Payments & Fiscal Configuration (L4D-11-MB)
    yookassa_enabled: bool = False
    yookassa_shop_id: str = ""
    yookassa_secret_key: str = ""
    yookassa_api_url: str = "https://api.yookassa.ru/v3"
    yookassa_webhook_secret: str = ""
    yookassa_ip_filter_enabled: bool = False
    yookassa_trusted_ips_raw: str = "185.71.76.0/27,185.71.77.0/27,77.75.153.0/25,77.75.156.11/32,77.75.156.35/32,77.75.154.128/25,2a02:5180::/32"
    yookassa_return_url_base: str = ""
    yookassa_receipt_enabled: bool = True
    yookassa_tax_system_code: int | None = None
    yookassa_vat_code: int = 1  # 1 = without VAT
    yookassa_payment_subject: str = "service"
    yookassa_payment_mode: str = "full_prepayment"
    yookassa_item_description: str = "Пополнение баланса L4Desk"
    yookassa_request_timeout_sec: float = 15.0

    @property
    def yookassa_trusted_ips(self) -> list[str]:
        return [
            ip.strip() for ip in self.yookassa_trusted_ips_raw.split(",") if ip.strip()
        ]

    @property
    def is_yookassa_enabled(self) -> bool:
        return self.yookassa_enabled or self.l4desk_enabled

    @property
    def processing_backend_effective_url(self) -> str:
        return self.processing_backend_url or "http://processing-backend:8000"

    @property
    def processing_backend_effective_token(self) -> str:
        return (
            self.processing_backend_service_token
            or self.internal_service_key_value
            or ""
        )

    @property
    def is_terminal_onboarding_enabled(self) -> bool:
        return self.l4desk_terminal_onboarding_enabled or self.l4desk_enabled

    @property
    def is_financial_core_enabled(self) -> bool:
        return self.l4desk_financial_core_enabled or self.l4desk_enabled

    model_config = {"env_file": ".env", "extra": "ignore"}


settings = Settings()
