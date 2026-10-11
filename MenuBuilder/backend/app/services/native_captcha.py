"""Stateless native CAPTCHA v1; secrets are supplied by the caller."""

import base64
import hashlib
import hmac
import json
import re
import secrets
import time

TTL_SECONDS = 600


def challenge(secret: str) -> dict[str, str | int]:
    if not secret:
        raise RuntimeError("Native CAPTCHA secret missing")
    a, b = secrets.randbelow(20) + 1, secrets.randbelow(20) + 1
    expires = int(time.time()) + TTL_SECONDS
    payload = (
        base64.urlsafe_b64encode(
            json.dumps(
                {"v": 1, "a": a, "b": b, "exp": expires}, separators=(",", ":")
            ).encode()
        )
        .decode()
        .rstrip("=")
    )
    signature = hmac.new(secret.encode(), payload.encode(), hashlib.sha256).hexdigest()
    return {
        "question": f"{a} + {b}",
        "token": f"{payload}.{signature}",
        "expires_at": expires,
    }


def verify(secret: str, token: str | None) -> None:
    if not secret:
        raise RuntimeError("Native CAPTCHA secret missing")
    if not token or len(token) > 2048:
        raise ValueError("Missing CAPTCHA answer")
    try:
        payload, signature, answer = token.split(".")
        expected = hmac.new(
            secret.encode(), payload.encode(), hashlib.sha256
        ).hexdigest()
        if not hmac.compare_digest(signature, expected):
            raise ValueError("Invalid signature")
        if not re.fullmatch(r"[A-Za-z0-9_-]+", payload) or not re.fullmatch(
            r"[0-9]{1,3}", answer
        ):
            raise ValueError("Invalid CAPTCHA format")
        data = json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)))
        if not isinstance(data, dict) or set(data) != {"v", "a", "b", "exp"}:
            raise ValueError("Invalid CAPTCHA payload")
        if any(type(value) is not int for value in data.values()):
            raise ValueError("Invalid CAPTCHA values")
        now = int(time.time())
        if data["v"] != 1 or not now < data["exp"] <= now + TTL_SECONDS:
            raise ValueError("Expired CAPTCHA")
        if not (1 <= data["a"] <= 20 and 1 <= data["b"] <= 20) or data["a"] + data[
            "b"
        ] != int(answer):
            raise ValueError("Incorrect CAPTCHA answer")
    except (ValueError, TypeError, KeyError) as exc:
        raise ValueError("Invalid CAPTCHA") from exc
