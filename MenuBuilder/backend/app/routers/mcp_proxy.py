import json

import httpx
from datetime import datetime, timezone
from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import Response
from sqlalchemy import text

from app.database import async_session

router = APIRouter()

MCP_SERVER_URL = "http://mcp-pin-server:8001"


async def _validate_jti(request: Request) -> str:
    """Validate JWT jti from nginx header. Returns jti or raises 401."""
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

    # Update last_used_at
    async with async_session() as session:
        await session.execute(
            text("UPDATE api_tokens SET last_used_at = NOW() WHERE jti = :jti"),
            {"jti": jti},
        )
        await session.commit()

    return jti


@router.api_route("/mcp/proxy", methods=["POST", "GET", "DELETE"])
async def mcp_proxy(request: Request):
    """Proxy MCP requests to the MCP server.

    Supports Streamable HTTP: POST for JSON-RPC, GET for SSE stream, DELETE for session termination.
    JWT is validated by nginx. X-Auth-Jti header contains the token's jti claim.
    """
    await _validate_jti(request)

    # Build forward headers
    forward_headers = {
        "Accept": "application/json, text/event-stream",
    }

    if request.method == "POST":
        forward_headers["Content-Type"] = "application/json"
        body = await request.body()
    else:
        body = None

    # Forward Mcp-Session-Id if present
    session_id = request.headers.get("mcp-session-id")
    if session_id:
        forward_headers["mcp-session-id"] = session_id

    async with httpx.AsyncClient() as client:
        try:
            resp = await client.request(
                method=request.method,
                url=f"{MCP_SERVER_URL}/mcp",
                content=body,
                headers=forward_headers,
                timeout=30.0,
            )

            # Return raw response with correct content-type
            content_type = resp.headers.get("content-type", "application/json")
            return Response(
                content=resp.content,
                status_code=resp.status_code,
                media_type=content_type,
            )

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
                headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"},
                timeout=10.0,
            )

            content_type = resp.headers.get("content-type", "")
            if "text/event-stream" in content_type:
                # Parse SSE response to extract JSON result
                text_body = resp.text
                for line in text_body.split("\n"):
                    if line.startswith("data: "):
                        import json

                        data = json.loads(line[6:])
                        if "result" in data:
                            return data["result"]
                return {"tools": []}

            return resp.json()
        except httpx.ConnectError:
            raise HTTPException(status_code=502, detail="MCP server is not available")
