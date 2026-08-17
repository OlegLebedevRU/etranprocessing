import os
from dataclasses import dataclass


@dataclass
class Config:
    database_url: str
    mcp_port: int


def load_config() -> Config:
    database_url = os.environ.get("DATABASE_URL")
    if not database_url:
        raise RuntimeError("DATABASE_URL environment variable is required")
    return Config(
        database_url=database_url,
        mcp_port=int(os.environ.get("MCP_PORT", "8001")),
    )
