"""Public auth CAPTCHA: verification precedes credentials and registration writes."""

import httpx
from fastapi import HTTPException

from app.config import settings


async def verify_captcha(token: str | None) -> None:
    if not settings.smartcaptcha_enabled:
        return
    if not token or not token.strip() or len(token) > 8192:
        raise HTTPException(400, "Пройдите CAPTCHA перед отправкой формы.")
    if (
        not settings.smartcaptcha_secret_key
        or not settings.smartcaptcha_site_key
        or not settings.smartcaptcha_allowed_hosts
    ):
        raise HTTPException(503, "Проверка CAPTCHA временно недоступна.")
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            response = await client.post(
                "https://smartcaptcha.cloud.yandex.ru/validate",
                data={"secret": settings.smartcaptcha_secret_key, "token": token},
            )
            response.raise_for_status()
            result = response.json()
        if not isinstance(result, dict):
            raise TypeError("Unexpected CAPTCHA response")
    except (httpx.HTTPError, ValueError, TypeError) as exc:
        raise HTTPException(503, "Проверка CAPTCHA временно недоступна.") from exc
    if (
        result.get("status") != "ok"
        or result.get("host") not in settings.smartcaptcha_allowed_hosts
    ):
        raise HTTPException(400, "CAPTCHA не пройдена или истекла. Пройдите её снова.")
