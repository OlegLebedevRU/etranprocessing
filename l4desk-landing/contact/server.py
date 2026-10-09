"""Small contact relay. Only nginx exposes it; all credentials come from env."""

from __future__ import annotations

import json
import logging
import os
import re
import threading
import time
import uuid
from collections import defaultdict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlencode
from urllib.request import Request, urlopen

LOGGER = logging.getLogger("contact")
MAX_BODY = 16_384
EMAIL_PATTERN = re.compile(
    r"[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]{1,64}@[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?\.[A-Za-z]{2,63}\Z"
)
TOPICS = {"экран и консоль", "файловый менеджер", "AI-агент и L4MCP", "API и события"}


class ContactError(Exception):
    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status


def remote_json(request: Request | str, timeout: float) -> dict:
    with urlopen(request, timeout=timeout) as response:
        result = json.load(response)
    if not isinstance(result, dict):
        raise ValueError("Expected JSON object")
    return result


def validate_form(data: dict) -> dict[str, str]:
    fields = {}
    for name, limit in (
        ("name", 100),
        ("email", 254),
        ("message", 2000),
        ("topic", 80),
        ("smart-token", 4096),
        ("request_id", 36),
    ):
        value = data.get(name, "")
        if not isinstance(value, str) or len(value) > limit or "\x00" in value:
            raise ContactError(400, "Проверьте поля формы и длину сообщения.")
        fields[name] = value.strip()
    if not fields["name"] or not fields["message"] or not EMAIL_PATTERN.fullmatch(fields["email"]):
        raise ContactError(400, "Укажите имя, корректный email и сообщение.")
    if fields["topic"] not in TOPICS or data.get("consent") is not True:
        raise ContactError(400, "Выберите тему и согласитесь на обработку обращения.")
    try:
        fields["request_id"] = str(uuid.UUID(fields["request_id"]))
    except ValueError as exc:
        raise ContactError(400, "Обновите страницу и повторите отправку.") from exc
    if not fields["smart-token"]:
        raise ContactError(400, "Пройдите проверку CAPTCHA.")
    return fields


class Relay:
    def __init__(self) -> None:
        self.secret = os.environ.get("SMARTCAPTCHA_SERVER_KEY", "")
        self.gateway = os.environ.get("EMAIL_GATEWAY_URL", "").rstrip("/")
        self.owner = os.environ.get("CONTACT_OWNER_EMAIL", "")
        self.lock = threading.Lock()
        self.attempts: dict[str, list[float]] = defaultdict(list)
        self.results: dict[str, tuple[float, str, str, dict | None]] = {}

    @property
    def configured(self) -> bool:
        return bool(
            self.secret
            and self.gateway.startswith("https://")
            and EMAIL_PATTERN.fullmatch(self.owner)
        )

    def captcha(self, token: str, ip: str) -> None:
        query = urlencode({"secret": self.secret, "token": token, "ip": ip})
        try:
            result = remote_json("https://smartcaptcha.yandexcloud.net/validate?" + query, 3)
        except Exception as exc:
            LOGGER.warning("CAPTCHA provider unavailable: %s", type(exc).__name__)
            raise ContactError(
                503, "Проверка CAPTCHA временно недоступна. Повторите отправку позже."
            ) from exc
        if result.get("status") != "ok":
            raise ContactError(400, "CAPTCHA не подтверждена. Пройдите проверку ещё раз.")

    def send(self, recipients: list[str], subject: str, message: str) -> None:
        request = Request(
            self.gateway + "/backend-api/v1/send-email/l4desk-landing",
            data=json.dumps(
                {"recipients": recipients, "subject": subject, "message": message}
            ).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        response = remote_json(request, 15)
        if response.get("status") != "sent" or not response.get("postbox_message_id"):
            raise ValueError("Email provider did not confirm acceptance")

    def submit(self, data: dict, ip: str) -> dict:
        if not self.configured:
            raise ContactError(
                503, "Отправка временно недоступна. Напишите нам по адресу внизу формы."
            )
        fields = validate_form(data)
        now = time.monotonic()
        key = fields["request_id"]
        with self.lock:
            self.results = {k: v for k, v in self.results.items() if now - v[0] < 3600}
            existing = self.results.get(key)
            if existing:
                if existing[1:3] != (ip, fields["email"].lower()):
                    raise ContactError(409, "Обновите страницу и повторите отправку.")
                if existing[3] is not None:
                    return existing[3]
                raise ContactError(409, "Обращение уже отправляется. Подождите немного.")
            self.attempts = defaultdict(
                list,
                {
                    k: [t for t in v if now - t < 86400]
                    for k, v in self.attempts.items()
                    if v and now - v[-1] < 86400
                },
            )
            limits = (
                ("ip:" + ip, 600, 3),
                ("email:" + fields["email"].lower(), 86400, 3),
                ("global", 86400, 100),
            )
            for bucket, period, limit in limits:
                if sum(now - t < period for t in self.attempts[bucket]) >= limit:
                    raise ContactError(429, "Слишком много обращений. Повторите отправку позже.")
            for bucket, _, _ in limits:
                self.attempts[bucket].append(now)
            self.results[key] = (now, ip, fields["email"].lower(), None)
        try:
            self.captcha(fields["smart-token"], ip)
            body = (
                f"Обращение L4Desk\nID: {key}\nИмя: {fields['name']}\n"
                f"Email: {fields['email']}\nТема: {fields['topic']}\n\n{fields['message']}"
            )
            try:
                self.send([self.owner], "L4Desk — новое обращение", body)
            except Exception as exc:
                LOGGER.warning("Owner email acceptance unconfirmed: %s", type(exc).__name__)
                raise ContactError(
                    502,
                    "Не удалось подтвердить отправку. Письмо могло быть принято; "
                    "перед повтором проверьте доставку у владельца сайта.",
                ) from exc
            copy_sent = True
            if fields["email"].lower() != self.owner.lower():
                try:
                    # A fixed acknowledgement never relays visitor text to third parties.
                    self.send(
                        [fields["email"]],
                        "L4Desk — ваше обращение принято",
                        f"Спасибо за обращение в L4Desk!\n\nТема: {fields['topic']}\n"
                        f"Номер обращения: {key}\nМы получили сообщение и свяжемся с вами "
                        "по этому email.\n\nЕсли вы не оставляли заявку, проигнорируйте "
                        "это письмо.\nКоманда L4Desk · Powered by Platerra",
                    )
                except Exception as exc:
                    LOGGER.warning("Acknowledgement acceptance unconfirmed: %s", type(exc).__name__)
                    copy_sent = False
            result = {
                "ok": True,
                "copy_sent": copy_sent,
                "request_id": key,
                "message": "Обращение отправлено. Подтверждение отправлено на ваш email."
                if copy_sent
                else (
                    "Обращение отправлено владельцу. Подтверждение на ваш email "
                    "отправить не удалось; повторять заявку не нужно."
                ),
            }
            with self.lock:
                self.results[key] = (now, ip, fields["email"].lower(), result)
            return result
        except ContactError:
            with self.lock:
                self.results.pop(key, None)
            raise


class Handler(BaseHTTPRequestHandler):
    relay: Relay

    def log_message(self, format: str, *args: object) -> None:
        pass  # Avoid logging form contents, CAPTCHA tokens or visitor addresses.

    def reply(self, status: int, body: dict) -> None:
        encoded = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:
        if self.path == "/healthz":
            self.reply(200, {"ok": True, "configured": self.relay.configured})
        else:
            self.reply(404, {"ok": False})

    def do_POST(self) -> None:
        if self.path != "/api/contact":
            self.reply(404, {"ok": False})
            return
        try:
            if self.headers.get("Origin") not in {
                "https://l4desk.ru",
                "https://www.l4desk.ru",
            }:
                raise ContactError(403, "Отправляйте обращение с сайта L4Desk.")
            if self.headers.get_content_type() != "application/json":
                raise ContactError(415, "Неподдерживаемый формат запроса.")
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_BODY or self.headers.get("Transfer-Encoding"):
                raise ContactError(413, "Сообщение слишком большое.")
            self.connection.settimeout(10)
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ContactError(400, "Некорректный запрос.")
            # The internal relay is not exposed; nginx overwrites this header.
            result = self.relay.submit(data, self.headers.get("X-Real-IP", self.client_address[0]))
            self.reply(200, result)
        except ContactError as exc:
            self.reply(exc.status, {"ok": False, "message": str(exc)})
        except ValueError, TimeoutError:
            self.reply(
                400,
                {"ok": False, "message": "Некорректный запрос. Повторите отправку."},
            )
        except Exception as exc:
            LOGGER.error("Contact request failed: %s", type(exc).__name__)
            self.reply(
                503,
                {
                    "ok": False,
                    "message": "Отправка временно недоступна. Попробуйте позже.",
                },
            )


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    Handler.relay = Relay()
    ThreadingHTTPServer(("0.0.0.0", 8081), Handler).serve_forever()
