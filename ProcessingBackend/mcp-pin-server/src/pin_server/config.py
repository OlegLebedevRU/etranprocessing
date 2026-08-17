import os
from dataclasses import dataclass


@dataclass
class Config:
    database_url: str
    mcp_port: int


def load_config() -> Config:
    return Config(
        database_url=os.environ.get(
            "DATABASE_URL",
            "postgresql://etran:etran@pg:5432/etranprocessing",
        ),
        mcp_port=int(os.environ.get("MCP_PORT", "8001")),
    )
