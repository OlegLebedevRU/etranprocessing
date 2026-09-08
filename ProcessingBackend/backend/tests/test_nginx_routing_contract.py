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


def test_legacy_proxy_routes_payment_techgate_gategauge_to_new_backend_with_legacy_mirror():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")

    # Payment
    assert "location = /payment/ {" in config
    assert (
        "proxy_pass http://new_processing_backend/api/payment/$is_args$args;" in config
    )
    assert "mirror /_mirror_payment;" in config
    assert "proxy_pass http://46.38.51.114/payment/$is_args$args;" in config

    # Payment etran.ashx
    assert "location = /payment/etran.ashx {" in config
    assert (
        "proxy_pass http://new_processing_backend/api/payment/etran.ashx$is_args$args;"
        in config
    )
    assert "mirror /_mirror_payment_etran;" in config
    assert "proxy_pass http://46.38.51.114/payment/etran.ashx$is_args$args;" in config

    # TechGate
    assert "location = /techgate/etran.ashx {" in config
    assert (
        "proxy_pass http://new_processing_backend/api/techgate/etran.ashx$is_args$args;"
        in config
    )
    assert "mirror /_mirror_techgate;" in config
    assert "proxy_pass http://46.38.51.114/techgate/etran.ashx$is_args$args;" in config

    # GateGauge
    assert "location = /GateGauge/main.ashx {" in config
    assert (
        "proxy_pass http://new_processing_backend/api/gategauge$is_args$args;" in config
    )
    assert "mirror /_mirror_gategauge;" in config
    assert "proxy_pass http://46.38.51.114/GateGauge/main.ashx$is_args$args;" in config

    # Licensebilling has no mirror
    assert "location = /_mirror_licensebilling" not in config
    assert "proxy_pass http://new_processing_backend/api/licensebilling/" in config


def test_legacy_proxy_ssl_certificates():
    config = LEGACY_NGINX_CONFIG.read_text(encoding="utf-8")

    assert "ssl_certificate /crt/iot-processing.ru.crt;" in config
    assert "ssl_certificate_key /crt/iot-processing.ru.key;" in config
    assert "iot-processing-ru-selfsigned" not in config
