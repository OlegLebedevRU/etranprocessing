"""Explicit event arguments; payload is data, never a shell expression."""

import base64
import re
from uuid import UUID

GENERATED_COMMAND_LIMIT = 4096


def validate_correlation_id(value: str | None) -> str | None:
    if value is None:
        return None
    parsed = str(UUID(value))
    if parsed != value.lower():
        raise ValueError("correlation_id must use UUID 8-4-4-4-12 format")
    return parsed


def _quote_windows_arg(value: str) -> str:
    """Always quote one CRT argv value, doubling slashes before quotes and at end."""
    if "\0" in value:
        raise ValueError("Arguments cannot contain NUL")
    escaped = re.sub(r'(\\*)"', lambda m: m[1] * 2 + '\\"', value)
    trailing = len(escaped) - len(escaped.rstrip("\\"))
    return '"' + escaped + "\\" * trailing + '"'


def build_event_command(
    event_code: int,
    payload: str | None,
    event_exit_code: int | str,
    correlation_id: str | None,
) -> tuple[str, dict]:
    correlation_id = validate_correlation_id(correlation_id)
    if not 900 <= event_code <= 999:
        raise ValueError("event_code must be 900–999")
    try:
        code = int(event_exit_code)
        if isinstance(event_exit_code, bool) or not -(2**31) <= code < 2**31:
            code = -1
    except ValueError:
        code = -1
    # Mirror l4con's overflow result before encoding, never discard the event.
    omitted = payload is not None and len(payload.encode("utf-8")) > 1024
    if omitted:
        payload, code = None, -2
    args = ["--send-event", f"-event-code={event_code}", f"-event-exit-code={code}"]
    if payload is not None:
        args.append("-event-payload=" + payload)
    if correlation_id is not None:
        args.append("-event-correlation-id=" + correlation_id)
    encoded = base64.b64encode(
        " ".join(_quote_windows_arg(arg) for arg in args).encode("utf-8")
    ).decode("ascii")
    command = (
        "$ErrorActionPreference='Stop';"
        "$a=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"
        + encoded
        + "'));$p=Start-Process -FilePath (Get-Command l4con.exe -ErrorAction Stop).Source "
        "-ArgumentList $a -NoNewWindow -Wait -PassThru;exit $p.ExitCode"
    )
    if len(command) > GENERATED_COMMAND_LIMIT:
        raise ValueError("Generated event command exceeds its bounded limit")
    return command, {
        "event_code": event_code,
        "event_exit_code": code,
        "payload_omitted": omitted,
    }
