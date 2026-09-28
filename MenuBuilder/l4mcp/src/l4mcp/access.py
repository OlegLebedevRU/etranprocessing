"""Resolve the caller through MenuBuilder for each MCP tool invocation."""

from __future__ import annotations

from dataclasses import dataclass

import httpx
from fastmcp.server.dependencies import get_http_request

from l4mcp.config import load_config


@dataclass(frozen=True)
class Principal:
    bearer: str
    user_id: int
    username: str
    org_id: int
    role_id: int
    session_id: str

    def scoped_org(self, requested_org: int | None = None) -> int:
        if self.role_id == 1 and requested_org is not None:
            return requested_org
        if requested_org is not None and requested_org != self.org_id:
            raise ValueError("Organization is outside your tenant")
        return self.org_id


async def current_principal() -> Principal:
    request = get_http_request()
    bearer = request.headers.get("authorization", "")
    if not bearer.lower().startswith("bearer "):
        raise PermissionError("L4mcp API token required")
    base_url = load_config().menubuilder_url.rstrip("/")
    if not base_url:
        raise RuntimeError("MENUBUILDER_API_URL is required")
    async with httpx.AsyncClient(timeout=10.0) as client:
        response = await client.get(
            f"{base_url}/api/mcp/identity",
            headers={"Authorization": bearer},
        )
    if response.status_code != 200:
        raise PermissionError("L4mcp API token is invalid or revoked")
    data = response.json()
    role_id = int(data["role_id"])
    org_id = int(data["org_id"])
    if role_id not in (1, 3, 5) or org_id <= 0:
        raise PermissionError("L4mcp access denied")
    return Principal(
        bearer=bearer,
        user_id=int(data["user_id"]),
        username=str(data["username"]),
        org_id=org_id,
        role_id=role_id,
        session_id=str(data["session_id"]),
    )
