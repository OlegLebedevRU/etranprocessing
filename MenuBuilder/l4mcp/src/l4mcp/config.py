import os
from dataclasses import dataclass


@dataclass
class Config:
    database_url: str
    mcp_port: int
    menubuilder_url: str
    iot_url: str
    iot_service_key: str


def load_config() -> Config:
    database_url = os.environ.get("DATABASE_URL")
    if not database_url:
        raise RuntimeError("DATABASE_URL environment variable is required")
    database_url = database_url.replace("postgresql+asyncpg://", "postgresql://", 1)
    return Config(
        database_url=database_url,
        mcp_port=int(os.environ.get("MCP_PORT", "8001")),
        menubuilder_url=os.environ.get("MENUBUILDER_API_URL", ""),
        iot_url=(
            os.environ.get("LEO4_INTERNAL_API_BASE_URL")
            or os.environ.get("IOT_RPC_BASE_URL", "")
        ),
        iot_service_key=(
            os.environ.get("LEO4_INTERNAL_SERVICE_TOKEN")
            or os.environ.get("INTERNAL_SERVICE_KEY")
            or os.environ.get("IOT_RPC_SERVICE_TOKEN", "")
        ),
    )
