from __future__ import annotations

import base64
import logging
from collections.abc import Sequence
from typing import Any

import httpx
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import EmailLog

logger = logging.getLogger(__name__)

SERVERLESS_EMAIL_GATEWAY_URL = "https://d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net"


class ServerlessEmailClient:
    def __init__(self, gateway_url: str | None = None) -> None:
        self.gateway_url = (gateway_url or SERVERLESS_EMAIL_GATEWAY_URL).rstrip("/")
        self.timeout = httpx.Timeout(15.0, connect=5.0)

    async def send_email(
        self,
        device_id: str,
        recipients: Sequence[str],
        subject: str,
        message: str,
        file_name: str | None = None,
        file_bytes: bytes | None = None,
    ) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "recipients": list(recipients),
            "subject": subject,
            "message": message,
        }
        if file_bytes is not None and file_name:
            payload["file_name"] = file_name
            payload["file_base64"] = base64.b64encode(file_bytes).decode("ascii")

        url = f"{self.gateway_url}/backend-api/v1/send-email/{device_id}"
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(url, json=payload)
            resp.raise_for_status()
            return resp.json()


async def send_email_with_logging(
    db: AsyncSession,
    *,
    device_id: str,
    recipients: Sequence[str],
    subject: str,
    message: str,
    org_id: int | None = None,
    user_id: int | None = None,
    file_name: str | None = None,
    file_bytes: bytes | None = None,
    client: ServerlessEmailClient | None = None,
) -> dict[str, Any]:
    """Send an email using ServerlessEmailClient and log it to email_logs (without body/files)."""
    email_client = client or ServerlessEmailClient()
    recipients_str = ",".join(recipients)
    try:
        data = await email_client.send_email(
            device_id=device_id,
            recipients=recipients,
            subject=subject,
            message=message,
            file_name=file_name,
            file_bytes=file_bytes,
        )
        storage_path = data.get("storage_path")
        postbox_message_id = data.get("postbox_message_id")
        status = str(data.get("status", "sent"))

        log_entry = EmailLog(
            org_id=org_id,
            user_id=user_id,
            device_id=device_id,
            recipients=recipients_str,
            subject=subject,
            status=status,
            postbox_message_id=postbox_message_id,
            storage_path=storage_path,
            error_message=None,
        )
        db.add(log_entry)
        await db.commit()
        return data
    except Exception as exc:
        logger.error("Failed to send email to %s: %s", recipients_str, exc)
        log_entry = EmailLog(
            org_id=org_id,
            user_id=user_id,
            device_id=device_id,
            recipients=recipients_str,
            subject=subject,
            status="failed",
            postbox_message_id=None,
            storage_path=None,
            error_message=str(exc)[:1000],
        )
        db.add(log_entry)
        await db.commit()
        raise
