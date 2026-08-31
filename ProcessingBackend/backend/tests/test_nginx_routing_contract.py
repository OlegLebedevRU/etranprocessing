from pathlib import Path

LEGACY_NGINX_CONFIG = (
    Path(__file__).parents[2]
    / "nginx-mutual-legacy"
    / "nginx-configs"
    / "legacy_ssl.conf"
)


def test_legacy_proxy_routes_list_menu_to_processing_ingress():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")

    assert "location = /api/ListMenuFile" in config
    assert "proxy_pass https://new_processing_backend/api/ListMenuFile" in config


def test_legacy_proxy_overwrites_terminal_certificate_headers():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")
    location = config.split("location = /api/ListMenuFile", 1)[1].split("}", 1)[0]

    assert "proxy_set_header X-Client-Cert-DN $ssl_client_s_dn;" in location
    assert "proxy_set_header X-Client-Cert-Serial $ssl_client_serial;" in location
    assert "$http_x_client_cert" not in location
