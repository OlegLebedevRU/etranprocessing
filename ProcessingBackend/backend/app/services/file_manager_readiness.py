from datetime import UTC, datetime, timedelta

from etranprocessing_db.file_manager import FileManagerAgent

from app.config import settings
from app.models import Terminal
from app.schemas.file_manager import AgentReadiness

FM_PROTOCOL_VERSION = 1
FM_REQUIRED_CAPABILITIES = frozenset(
    {"fs.session", "fs.list", "fs.read", "fs.write", "fs.cancel", "fs.proxy"}
)
FM_HEARTBEAT_TTL_SECONDS = 45


def evaluate_readiness(
    terminal: Terminal, agent: FileManagerAgent | None, *, now: datetime
) -> AgentReadiness:
    result = AgentReadiness(state="not_registered", server_time=now)
    if not terminal.is_active:
        result.state = "disabled"
        return result
    if agent is None or agent.tenant_id != terminal.org_id:
        return result
    seen = agent.last_seen_at
    if seen.tzinfo is None:
        seen = seen.replace(tzinfo=UTC)
    result.agent_version = agent.agent_version
    result.protocol_version = agent.protocol_version
    result.capabilities = agent.capabilities
    result.missing_capabilities = sorted(
        FM_REQUIRED_CAPABILITIES - set(agent.capabilities)
    )
    result.last_seen_at = seen
    result.valid_until = seen + timedelta(seconds=FM_HEARTBEAT_TTL_SECONDS)
    result.compatible = (
        agent.protocol_version == FM_PROTOCOL_VERSION
        and not result.missing_capabilities
    )
    if not terminal.cert_serial or agent.cert_serial != terminal.cert_serial:
        result.state = "certificate_changed"
    elif not result.compatible:
        result.state = "incompatible"
    elif (
        seen > now
        or now >= result.valid_until
        or terminal.cert_not_valid_after is not None
        and (
            terminal.cert_not_valid_after.replace(tzinfo=UTC)
            if terminal.cert_not_valid_after.tzinfo is None
            else terminal.cert_not_valid_after
        )
        <= now
    ):
        result.state = "offline"
    elif not agent.filesystem_ready:
        result.state = "filesystem_unavailable"
    else:
        result.state = "ready"
        result.available = True
    return result


def deployment_readiness(
    terminal: Terminal, agent: FileManagerAgent | None, *, now: datetime
) -> AgentReadiness:
    result = evaluate_readiness(terminal, agent, now=now)
    if result.available:
        if not settings.file_manager_read_roots:
            result.state, result.available = "policy_unconfigured", False
        elif not all(
            (
                settings.file_manager_iot_url,
                settings.file_manager_iot_key,
                settings.file_manager_s3_endpoint.startswith("https://"),
                settings.file_manager_s3_region,
                settings.file_manager_s3_bucket,
                settings.file_manager_s3_access_key,
                settings.file_manager_s3_secret_key,
            )
        ):
            result.state, result.available = "storage_unavailable", False
        result.write_available = result.available and bool(
            settings.file_manager_write_roots
        )
    return result
