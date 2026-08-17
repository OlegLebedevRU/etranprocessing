import httpx
from datetime import datetime, timezone

from fastapi import APIRouter, HTTPException, Request
from sqlalchemy import text

from app.database import async_session

router = APIRouter()

MCP_SERVER_URL = "http://mcp-pin-server:8001"


@router.post("/mcp/proxy")
async def mcp_proxy(request: Request):
    """Proxy MCP JSON-RPC requests to the MCP server.

    JWT is validated by nginx. X-Auth-Jti header contains the token's jti claim.
    We check the jti against api_tokens table (not revoked, not expired).
    """
    jti = request.headers.get("X-Auth-Jti")
    if not jti:
        raise HTTPException(status_code=401, detail="Missing token ID (X-Auth-Jti header)")

    async with async_session() as session:
        result = await session.execute(
            text("SELECT jti, user_id, expires_at, revoked_at FROM api_tokens WHERE jti = :jti"),
            {"jti": jti},
        )
        row = result.fetchone()

    if not row:
        raise HTTPException(status_code=401, detail="Token not found")

    if row[3] is not None:  # revoked_at
        raise HTTPException(status_code=401, detail="Token has been revoked")

    if row[2] < datetime.now(timezone.utc):  # expires_at
        raise HTTPException(status_code=401, detail="Token has expired")

    async with async_session() as session:
        await session.execute(
            text("UPDATE api_tokens SET last_used_at = NOW() WHERE jti = :jti"),
            {"jti": jti},
        )
        await session.commit()

    body = await request.body()
    async with httpx.AsyncClient() as client:
        try:
            resp = await client.post(
                f"{MCP_SERVER_URL}/mcp",
                content=body,
                headers={"Content-Type": "application/json"},
                timeout=30.0,
            )
            return resp.json()
        except httpx.ConnectError:
            raise HTTPException(
                status_code=502,
                detail="MCP server is not available. Check if mcp-pin-server container is running.",
            )
        except Exception as e:
            raise HTTPException(status_code=502, detail=f"MCP server error: {e}")


@router.get("/mcp/tools")
async def mcp_tools_list(request: Request):
    """List available MCP tools (convenience endpoint)."""
    jti = request.headers.get("X-Auth-Jti")
    if not jti:
        raise HTTPException(status_code=401, detail="Missing token ID")

    async with async_session() as session:
        result = await session.execute(
            text("SELECT revoked_at, expires_at FROM api_tokens WHERE jti = :jti"),
            {"jti": jti},
        )
        row = result.fetchone()

    if not row or row[0] is not None or row[1] < datetime.now(timezone.utc):
        raise HTTPException(status_code=401, detail="Invalid or expired token")

    async with httpx.AsyncClient() as client:
        try:
            resp = await client.post(
                f"{MCP_SERVER_URL}/mcp",
                json={"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}},
                headers={"Content-Type": "application/json"},
                timeout=10.0,
            )
            return resp.json()
        except httpx.ConnectError:
            raise HTTPException(status_code=502, detail="MCP server is not available")
