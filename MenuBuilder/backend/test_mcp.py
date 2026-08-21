import httpx

try:
    r = httpx.post(
        "http://mcp-pin-server:8001/mcp",
        json={"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}},
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        },
        timeout=10.0,
    )
    print(f"Status: {r.status_code}")
    print(f"Content-Type: {r.headers.get('content-type', '')}")
    print(f"Body (first 200): {r.text[:200]}")
except Exception as e:  # noqa: BLE001
    print(f"Error: {e}")
