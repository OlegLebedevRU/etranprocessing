from __future__ import annotations

import logging
from typing import Any

from app.services.iot_event_feed_client import (
    IotEventFeedClient,
    IotEventFeedTimeoutError,
)
from app.services.media_orchestrator_client import (
    MediaOrchestratorClient,
)

logger = logging.getLogger(__name__)


class RemoteSessionStopPending(Exception):
    """A provider has not confirmed teardown of the exact session."""


def _confirmed_iot_close(
    data: Any, *, session_id: str, tenant_id: int, sn: str
) -> bool:
    return (
        isinstance(data, dict)
        and data.get("session_id") == session_id
        and data.get("tenant_id") == tenant_id
        and data.get("sn") == sn
        and data.get("status") == "closed"
    )


async def confirm_remote_session_stop(
    *,
    session_id: str,
    session_type: str,
    tenant_id: int,
    sn: str,
    operation_id: str,
    reason: str,
    correlation_id: str | None,
    iot_adapter: IotEventFeedClient | Any,
    media_orchestrator: MediaOrchestratorClient | Any,
) -> None:
    """Confirm both owners without closing a newer or unidentified session."""
    iot_error: Exception | None = None
    media_error: Exception | None = None
    try:
        result = await iot_adapter.stop_remote_session(
            session_id=session_id,
            operation_id=operation_id,
            reason=reason,
            correlation_id=correlation_id,
            tenant_id=tenant_id,
            sn=sn,
        )
        if not _confirmed_iot_close(
            result, session_id=session_id, tenant_id=tenant_id, sn=sn
        ):
            raise RemoteSessionStopPending("IoT did not confirm exact closed session")
    except IotEventFeedTimeoutError as exc:
        try:
            result = await iot_adapter.get_remote_session(session_id)
            if not _confirmed_iot_close(
                result, session_id=session_id, tenant_id=tenant_id, sn=sn
            ):
                iot_error = exc
        except Exception as get_exc:  # noqa: BLE001 - an unconfirmed close must remain retryable
            iot_error = get_exc
    except Exception as exc:  # noqa: BLE001 - provider failures must never close SQL state
        iot_error = exc

    if iot_error:
        logger.warning(
            "Remote stop pending before media: session_id=%s operation_id=%s phase=iot error=%s",
            session_id,
            operation_id,
            type(iot_error).__name__,
        )
        raise RemoteSessionStopPending("remote stop pending")

    if session_type == "video":
        if session_id in (sn, f"media-{sn}"):
            raise RemoteSessionStopPending("ambiguous legacy media session ID")
        try:
            # Media's stop endpoint has legacy SN fallbacks. Its GET is exact-ID only.
            health = await media_orchestrator.get_session_health(session_id)
            if (
                not isinstance(health, dict)
                or health.get("session_id") != session_id
                or health.get("sn") != sn
            ):
                raise RemoteSessionStopPending("media identity unverified")
            if health.get("state") != "stopped":
                result = await media_orchestrator.stop_session(
                    session_id=session_id,
                    operation_id=operation_id,
                    reason=reason,
                )
                if (
                    not isinstance(result, dict)
                    or result.get("status") != "success"
                    or result.get("session_id") != session_id
                    or result.get("state") != "stopped"
                ):
                    raise RemoteSessionStopPending("media did not confirm stop")
        except Exception as exc:  # noqa: BLE001 - provider failures must never close SQL state
            media_error = exc

    if media_error:
        logger.warning(
            "Remote stop pending: session_id=%s operation_id=%s phase=media error=%s",
            session_id,
            operation_id,
            type(media_error).__name__,
        )
        raise RemoteSessionStopPending("remote stop pending")
