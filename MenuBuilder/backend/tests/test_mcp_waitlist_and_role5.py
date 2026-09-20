from __future__ import annotations

from datetime import UTC, datetime
from typing import Any, cast

import pytest
from httpx import ASGITransport, AsyncClient

from app.auth import create_access_token
from app.database import get_db
from app.main import app
from app.models_l4desk import L4DeskAuditEvent


class FakeMcpDb:
    def __init__(self):
        self.items: list[Any] = []
        self._next_id = 1

    def add(self, item: Any) -> None:
        if not getattr(item, "id", None):
            item.id = self._next_id
            self._next_id += 1
        if not getattr(item, "occurred_at", None):
            item.occurred_at = datetime.now(UTC)
        self.items.append(item)

    async def commit(self) -> None:
        pass

    async def execute(self, statement: Any) -> Any:
        # Simple scalar/query filtering for L4DeskAuditEvent
        tenant_id = None
        event_type = None

        # Inspect binary expressions in statement whereclause
        whereclause = getattr(statement, "_where_criteria", ())
        for crit in whereclause:
            crit_str = str(crit)
            if "tenant_id" in crit_str:
                tenant_id = getattr(crit.right, "value", None)
            if "event_type" in crit_str:
                event_type = getattr(crit.right, "value", None)

        matched = []
        for it in self.items:
            if isinstance(it, L4DeskAuditEvent):
                if tenant_id is not None and it.tenant_id != tenant_id:
                    continue
                if event_type is not None and it.event_type != event_type:
                    continue
                matched.append(it)

        # Sort desc by id
        matched.sort(key=lambda x: getattr(x, "id", 0), reverse=True)

        class Result:
            def scalar_one_or_none(self):
                return matched[0] if matched else None

            def scalars(self):
                class Scalars:
                    def all(self):
                        return matched

                return Scalars()

        return Result()


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.mark.anyio
async def test_mcp_waitlist_flow():
    fake_db = FakeMcpDb()
    tenant_id = 99

    token_tenant_99 = create_access_token(
        data={
            "sub": "owner1",
            "role": "l4desk_owner",
            "role_id": 5,
            "org_id": tenant_id,
        }
    )
    token_tenant_100 = create_access_token(
        data={"sub": "owner2", "role": "l4desk_owner", "role_id": 5, "org_id": 100}
    )

    async def override_get_db():
        yield cast(Any, fake_db)

    app.dependency_overrides[get_db] = override_get_db

    try:
        transport = ASGITransport(app=app)
        async with AsyncClient(
            transport=transport, base_url="http://testserver"
        ) as client:
            # 1. Initial status -> not registered
            resp = await client.get(
                "/api/v1/mcp/waitlist/status",
                headers={"Authorization": f"Bearer {token_tenant_99}"},
            )
            assert resp.status_code == 200
            assert resp.json()["registered"] is False

            # 2. Register for waitlist
            payload = {
                "use_case": "auto_triage",
                "note": "Парк 20 терминалов",
                "contact_email": "admin@tenant99.ru",
            }
            resp_post = await client.post(
                "/api/v1/mcp/waitlist",
                headers={"Authorization": f"Bearer {token_tenant_99}"},
                json=payload,
            )
            assert resp_post.status_code == 201
            data = resp_post.json()
            assert data["status"] == "registered"
            assert data["tenant_id"] == 99
            assert data["use_case"] == "auto_triage"
            assert data["contact_email"] == "admin@tenant99.ru"

            # 3. Check status again -> registered: true
            resp_status = await client.get(
                "/api/v1/mcp/waitlist/status",
                headers={"Authorization": f"Bearer {token_tenant_99}"},
            )
            assert resp_status.status_code == 200
            status_data = resp_status.json()
            assert status_data["registered"] is True
            assert status_data["tenant_id"] == 99
            assert status_data["use_case"] == "auto_triage"
            assert status_data["note"] == "Парк 20 терминалов"

            # 4. Tenant isolation: tenant 100 sees registered: false
            resp_t100 = await client.get(
                "/api/v1/mcp/waitlist/status",
                headers={"Authorization": f"Bearer {token_tenant_100}"},
            )
            assert resp_t100.status_code == 200
            assert resp_t100.json()["registered"] is False
    finally:
        app.dependency_overrides.pop(get_db, None)


@pytest.mark.anyio
async def test_mcp_waitlist_validation_and_auth():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://testserver") as client:
        # Unauthorized without token
        resp_no_auth = await client.get("/api/v1/mcp/waitlist/status")
        assert resp_no_auth.status_code == 401

        # Token with org_id = 0 (no active tenant)
        token_no_org = create_access_token(
            data={"sub": "user_no_org", "role": "user", "role_id": 5, "org_id": 0}
        )
        resp_forbidden = await client.post(
            "/api/v1/mcp/waitlist",
            headers={"Authorization": f"Bearer {token_no_org}"},
            json={"use_case": "auto_triage"},
        )
        assert resp_forbidden.status_code == 403

        # Invalid body (empty use_case)
        token_valid = create_access_token(
            data={"sub": "user_org", "role": "user", "role_id": 5, "org_id": 77}
        )
        resp_invalid = await client.post(
            "/api/v1/mcp/waitlist",
            headers={"Authorization": f"Bearer {token_valid}"},
            json={"use_case": ""},
        )
        assert resp_invalid.status_code == 422


@pytest.mark.anyio
async def test_role5_l4desk_owner_permissions():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://testserver") as client:
        token_role5 = create_access_token(
            data={"sub": "l4desk_boss", "role": "user", "role_id": 5, "org_id": 12}
        )
        resp = await client.get(
            "/api/auth/me",
            headers={"Authorization": f"Bearer {token_role5}"},
        )
        assert resp.status_code == 200
        user_info = resp.json()
        assert user_info["role"] == "l4desk_owner"
        assert user_info["role_id"] == 5
        assert "settings:terminals:view" in user_info["permissions"]
        assert "billing:view" in user_info["permissions"]
        assert "monitoring:view" in user_info["permissions"]
