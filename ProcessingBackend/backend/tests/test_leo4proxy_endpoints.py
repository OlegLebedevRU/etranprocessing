"""Routing extensions cannot alter admission or expose invalid addresses."""

import json

import pytest

from app.config import settings
from app.models import Terminal
from app.services.leo4proxy_policy import get_leo4proxy_policy


@pytest.mark.parametrize(
    "active,subscription", [(True, True), (False, True), (True, False)]
)
def test_fm_transport_shares_media_deny(monkeypatch, active, subscription):
    monkeypatch.setattr(settings, "file_manager_service_key", "fixture-only")
    monkeypatch.setattr(settings, "file_manager_s3_bucket", "fixture-bucket")
    monkeypatch.setattr(
        settings, "file_manager_s3_endpoint", "https://storage.example:443"
    )
    policy = get_leo4proxy_policy(Terminal(sn="test", is_active=active), subscription)
    assert policy.fm_allowed is (active and subscription)
    if policy.fm_allowed:
        assert policy.fm_storage_endpoint
        assert policy.fm_storage_endpoint.host == "storage.example"
        assert policy.fm_storage_endpoint.port == 443
    else:
        assert policy.fm_storage_endpoint is None
    assert (
        policy.outgoing_https_allowed
    )  # Payment/certificate HTTPS keeps its own rules.


@pytest.mark.parametrize(
    "endpoint",
    [
        "http://storage.example",
        "https://user:pass@storage.example",
        "https://storage.example/path",
        "https://storage.example?token=x",
        "https://storage.example:bad",
    ],
)
def test_invalid_fm_route_fails_closed(monkeypatch, endpoint):
    monkeypatch.setattr(settings, "file_manager_service_key", "fixture-only")
    monkeypatch.setattr(settings, "file_manager_s3_bucket", "fixture-bucket")
    monkeypatch.setattr(settings, "file_manager_s3_endpoint", endpoint)
    policy = get_leo4proxy_policy(Terminal(sn="test", is_active=True))
    assert not policy.fm_allowed and policy.fm_storage_endpoint is None
    assert policy.mqtt_rtp_allowed


@pytest.mark.parametrize("active", [False, True])
def test_optional_endpoint_extension_preserves_admission(monkeypatch, active):
    monkeypatch.setattr(
        settings,
        "leo4proxy_endpoints",
        json.dumps(
            {
                "https": {
                    "host": "policy.example.com",
                    "port": 443,
                    "fallback_ips": ["8.8.8.8"],
                }
            }
        ),
    )
    terminal = Terminal(sn="test", is_active=active)
    policy = get_leo4proxy_policy(terminal).model_dump(exclude_none=True)
    assert policy["mqtt_rtp_allowed"] is active
    assert policy["endpoints"]["https"]["fallback_ips"] == ["8.8.8.8"]
    assert policy["endpoints_ttl_seconds"] == 86400


@pytest.mark.parametrize(
    "ip", ["127.0.0.1", "10.0.0.1", "224.0.0.1", "not-an-ip", "https://8.8.8.8"]
)
def test_invalid_ip_preserves_valid_host_and_deny(monkeypatch, ip):
    monkeypatch.setattr(
        settings,
        "leo4proxy_endpoints",
        json.dumps(
            {"https": {"host": "policy.example.com", "port": 443, "fallback_ips": [ip]}}
        ),
    )
    policy = get_leo4proxy_policy(Terminal(sn="test", is_active=False))
    assert not policy.mqtt_rtp_allowed
    assert policy.endpoints and policy.endpoints["https"].fallback_ips == []


@pytest.mark.parametrize(
    "config", ["", "{", "[]", '{"https":{"host":"https://bad","port":0}}']
)
def test_unset_or_invalid_extension_preserves_old_wire_format(monkeypatch, config):
    monkeypatch.setattr(settings, "leo4proxy_endpoints", config)
    policy = get_leo4proxy_policy(
        Terminal(sn="test", is_active=True), subscription_allowed=False
    )
    assert policy.model_dump(exclude_none=True) == {
        "v": 1,
        "sn": "test",
        "mqtt_rtp_allowed": False,
        "fm_allowed": False,
        "outgoing_https_allowed": True,
        "stop_facts": ["terminal_inactive"],
    }
