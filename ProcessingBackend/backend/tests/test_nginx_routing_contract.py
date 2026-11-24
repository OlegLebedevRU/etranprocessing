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
    assert "proxy_pass http://new_processing_backend/api/ListMenuFile" in config


def test_legacy_proxy_overwrites_terminal_certificate_headers():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")
    location = config.split("location = /api/ListMenuFile", 1)[1].split("}", 1)[0]

    assert "proxy_set_header X-Client-Cert-DN $ssl_client_s_dn;" in location
    assert "proxy_set_header X-Client-Cert-Serial $ssl_client_serial;" in location
    assert "$http_x_client_cert" not in location


def test_terminal_routes_reach_processing_without_mirror():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")
    routes = {
        "/payment/": "/api/payment/",
        "/payment/etran.ashx": "/api/payment/etran.ashx",
        "/techgate/etran.ashx": "/api/techgate/etran.ashx",
        "/GateGauge/main.ashx": "/api/gategauge",
    }
    for route, target in routes.items():
        location = config.split(f"location = {route} {{", 1)[1].split("}", 1)[0]
        assert (
            f"proxy_pass http://new_processing_backend{target}$is_args$args;"
            in location
        )

    directives = [line.strip() for line in config.splitlines()]
    assert not any(
        line.startswith(("mirror ", "mirror_request_body ")) for line in directives
    )
    retired = config.split("location ^~ /_mirror_ {", 1)[1].split("}", 1)[0]
    assert "return 404;" in retired
    assert "proxy_pass http://new_processing_backend/api/licensebilling/" in config


def test_legacy_proxy_ssl_certificates():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")

    assert "ssl_certificate /crt/iot-processing.ru.crt;" in config
    assert "ssl_certificate_key /crt/iot-processing.ru.key;" in config
    assert "iot-processing-ru-selfsigned" not in config


def test_legacy_proxy_routes_policy_and_overwrites_client_identity():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")
    location = config.split("location = /api/leo4proxy/policy", 1)[1].split("}", 1)[0]

    assert (
        "proxy_pass http://new_processing_backend/api/leo4proxy/policy$is_args$args;"
        in location
    )
    for header, value in {
        "X-Client-Cert-DN": "$ssl_client_s_dn",
        "X-Client-Cert-Serial": "$ssl_client_serial",
        "X-Client-Cert-Issuer-DN": "$ssl_client_i_dn",
        "X-Client-Cert-Verified": "$ssl_client_verify",
        "X-SSL-Client-Cert": "$ssl_client_escaped_cert",
    }.items():
        assert f"proxy_set_header {header} {value};" in location
    assert "$http_x_client_cert" not in location
    assert "proxy_cache off;" in location
    assert "mirror " not in location
