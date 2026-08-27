import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.main import app
from app.services.iot_client import IotPlatformClient, iot_client, mask_api_key


@pytest.fixture(autouse=True)
def reset_iot_client_state():
    """Reset simulated API keys in memory before each test."""
    iot_client._simulated_api_keys.clear()
    app.dependency_overrides.clear()
    yield
    iot_client._simulated_api_keys.clear()
    app.dependency_overrides.clear()


@pytest.fixture
def anyio_backend():
    return "asyncio"


def test_mask_api_key():
    assert mask_api_key("") == ""
    assert mask_api_key("12345678") == "********"
    key_41 = "sec_live_9f83b2a1c4e74829ad01f83c7e1920aa"
    masked = mask_api_key(key_41)
    assert masked.startswith("sec_")
    assert masked.endswith("20aa")
    assert len(masked) == len(key_41)
    assert masked == f"sec_{'*' * 33}20aa"
    assert mask_api_key("leo4_1234567890_test") == "leo4************test"


@pytest.mark.anyio
async def test_iot_client_simulation_flow():
    client = IotPlatformClient(base_url="", service_token="secret")

    # Initially empty
    assert await client.get_api_key(org_id=42) is None

    # Provision key
    created = await client.provision_api_key(
        org_id=42,
        api_key="sec_live_abcdef1234567890abcdef1234567890",
        name="Test Org Key",
        is_active=True,
    )
    assert created["org_id"] == 42
    assert created["api_key"] == "sec_live_abcdef1234567890abcdef1234567890"
    assert created["is_active"] is True

    # Get unmasked
    unmasked = await client.get_api_key(org_id=42, mask=False)
    assert unmasked is not None
    assert unmasked["api_key"] == "sec_live_abcdef1234567890abcdef1234567890"

    # Get masked
    masked = await client.get_api_key(org_id=42, mask=True)
    assert masked is not None
    assert masked["api_key"].startswith("sec_")
    assert "*" in masked["api_key"]
    assert masked["api_key"].endswith("7890")

    # Delete
    del_res = await client.delete_api_key(org_id=42)
    assert del_res == {"status": "deleted", "org_id": 42}
    assert await client.get_api_key(org_id=42) is None


@pytest.mark.anyio
async def test_api_key_endpoints_workflow():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://testserver") as ac:
        token = create_access_token(
            {"sub": "manager", "org": "200", "org_id": 200, "role": "user"}
        )
        headers = {"Authorization": f"Bearer {token}"}

        # 1. Initially no key
        res = await ac.get("/api/integrations/api-key", headers=headers)
        assert res.status_code == 200
        data = res.json()
        assert data["has_key"] is False
        assert data["org_id"] == 200
        assert data["api_key"] is None

        # 2. Provision new key (auto-generated)
        prov_res = await ac.post(
            "/api/integrations/api-key/provision",
            json={"name": "Основной API ключ"},
            headers=headers,
        )
        assert prov_res.status_code == 200
        prov_data = prov_res.json()
        assert prov_data["has_key"] is True
        assert prov_data["org_id"] == 200
        assert prov_data["name"] == "Основной API ключ"
        assert prov_data["is_active"] is True
        assert prov_data["api_key"].startswith("sec_live_")

        created_raw_key = prov_data["api_key"]

        # 3. Get masked key
        get_res = await ac.get("/api/integrations/api-key", headers=headers)
        assert get_res.status_code == 200
        get_data = get_res.json()
        assert get_data["has_key"] is True
        assert get_data["api_key"].startswith("sec_")
        assert "*" in get_data["api_key"]

        # 4. Reveal key
        reveal_res = await ac.get("/api/integrations/api-key/reveal", headers=headers)
        assert reveal_res.status_code == 200
        reveal_data = reveal_res.json()
        assert reveal_data["api_key"] == created_raw_key

        # 5. Toggle active (deactivate)
        toggle_res = await ac.post(
            "/api/integrations/api-key/toggle-active",
            json={"is_active": False},
            headers=headers,
        )
        assert toggle_res.status_code == 200
        assert toggle_res.json()["is_active"] is False

        # 6. Delete key
        del_res = await ac.delete("/api/integrations/api-key", headers=headers)
        assert del_res.status_code == 200
        assert del_res.json() == {"status": "deleted", "org_id": 200}

        # Verify key is gone
        check_res = await ac.get("/api/integrations/api-key", headers=headers)
        assert check_res.status_code == 200
        assert check_res.json()["has_key"] is False


@pytest.mark.anyio
async def test_tenant_isolation_and_superuser_override():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://testserver") as ac:
        user_token = create_access_token(
            {"sub": "user_org1", "org": "1", "org_id": 1, "role": "user"}
        )
        su_token = create_access_token(
            {
                "sub": "admin",
                "org": "1",
                "org_id": 1,
                "role": "superuser",
                "is_superuser": True,
            }
        )

        user_headers = {"Authorization": f"Bearer {user_token}"}
        su_headers = {"Authorization": f"Bearer {su_token}"}

        # Regular user attempts to access org 2 -> 403 Forbidden
        res_forbidden = await ac.get(
            "/api/integrations/api-key?org_id=2", headers=user_headers
        )
        assert res_forbidden.status_code == 403

        # Superuser creates key for org 2 -> 200 OK
        res_su_create = await ac.post(
            "/api/integrations/api-key/provision",
            json={
                "org_id": 2,
                "api_key": "sec_custom_1234567890abcdef",
                "name": "Org 2 Partner Key",
            },
            headers=su_headers,
        )
        assert res_su_create.status_code == 200
        assert res_su_create.json()["org_id"] == 2
        assert res_su_create.json()["api_key"] == "sec_custom_1234567890abcdef"

        # Superuser reveals key for org 2 -> 200 OK
        res_su_reveal = await ac.get(
            "/api/integrations/api-key/reveal?org_id=2", headers=su_headers
        )
        assert res_su_reveal.status_code == 200
        assert res_su_reveal.json()["api_key"] == "sec_custom_1234567890abcdef"


@pytest.mark.anyio
async def test_iot_client_remote_http_mock():
    import httpx
    from fastapi import HTTPException

    # Test real client with mocked HTTP transport
    mock_responses = {
        "POST /api/v1/provisioning/api-keys": httpx.Response(
            200,
            json={
                "org_id": 105,
                "api_key": "sec_live_remote1234567890abcdef",
                "name": "Remote Key",
                "is_active": True,
                "created_at": "2026-08-28T00:00:00Z",
                "updated_at": "2026-08-28T00:00:00Z",
            },
        ),
        "GET /api/v1/provisioning/api-keys/105": httpx.Response(
            200,
            json={
                "org_id": 105,
                "api_key": "sec_************************cdef",
                "name": "Remote Key",
                "is_active": True,
            },
        ),
        "DELETE /api/v1/provisioning/api-keys/105": httpx.Response(
            200,
            json={"status": "deleted", "org_id": 105},
        ),
    }

    class MockTransport(httpx.AsyncBaseTransport):
        async def handle_async_request(self, request: httpx.Request) -> httpx.Response:
            key = f"{request.method} {request.url.path}"
            if (
                request.url.path == "/api/v1/provisioning/api-keys"
                and request.method == "POST"
            ):
                content = request.read().decode()
                if "conflict_key" in content:
                    return httpx.Response(
                        409,
                        json={
                            "detail": "API key is already assigned to organization 77"
                        },
                    )
            if key in mock_responses:
                return mock_responses[key]
            if request.url.path == "/api/v1/provisioning/api-keys/999":
                return httpx.Response(404, json={"detail": "Not found"})
            return httpx.Response(404)

    client = IotPlatformClient(
        base_url="https://dev.leo4.ru", service_token="sec_token"
    )

    with pytest.MonkeyPatch.context() as mp:
        orig_async_client = httpx.AsyncClient

        def custom_async_client(*args, **kwargs):
            kwargs["transport"] = MockTransport()
            return orig_async_client(*args, **kwargs)

        mp.setattr(httpx, "AsyncClient", custom_async_client)

        # 1. Test provision
        res = await client.provision_api_key(
            org_id=105,
            api_key="sec_live_remote1234567890abcdef",
            name="Remote Key",
        )
        assert res["org_id"] == 105
        assert res["api_key"] == "sec_live_remote1234567890abcdef"

        # 2. Test get masked
        res_get = await client.get_api_key(org_id=105, mask=True)
        assert res_get is not None
        assert res_get["org_id"] == 105

        # 3. Test 404
        assert await client.get_api_key(org_id=999) is None

        # 4. Test delete
        res_del = await client.delete_api_key(org_id=105)
        assert res_del == {"status": "deleted", "org_id": 105}

        # 5. Test 409 conflict
        with pytest.raises(HTTPException) as exc_info:
            await client.provision_api_key(
                org_id=105,
                api_key="conflict_key",
            )
        assert exc_info.value.status_code == 409
