from pathlib import Path

NGINX_CONFIG = Path(__file__).parents[2] / "nginx.conf"


def _location(config: str, marker: str) -> str:
    return config.split(marker, 1)[1].split("}", 1)[0]


def test_menu_builder_does_not_route_terminal_list_menu():
    config = NGINX_CONFIG.read_text(encoding="utf-8")

    assert "location = /api/ListMenuFile" not in config


def test_menu_builder_uses_canonical_tenant_identity_headers():
    config = NGINX_CONFIG.read_text(encoding="utf-8")

    for marker in ("\n    location /api/billing/ {", "\n    location /api/ {"):
        location = _location(config, marker)
        assert "proxy_set_header X-User-Id $jwt_claim_userId;" in location
        assert "proxy_set_header X-Org-Id $jwt_claim_orgId;" in location
        assert "proxy_set_header X-User-Role $jwt_claim_role;" in location
        assert 'proxy_set_header jwt-sub "";' in location
        assert 'proxy_set_header jwt-org "";' in location
        assert 'proxy_set_header jwt-role "";' in location


def test_public_auth_routes_clear_forwarded_identity_headers():
    config = NGINX_CONFIG.read_text(encoding="utf-8")
    location = _location(config, "location /api/auth/")

    assert 'proxy_set_header X-User-Id "";' in location
    assert 'proxy_set_header X-Org-Id "";' in location
    assert 'proxy_set_header X-User-Role "";' in location
