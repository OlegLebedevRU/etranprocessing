from urllib.parse import urlencode

import pytest
from starlette.requests import Request

from app.routers.payment import _parse_request_params
from app.services.payment_service import parse_params_string


@pytest.mark.parametrize("encoding", ["utf-8", "windows-1251"])
@pytest.mark.parametrize("location", ["body", "query"])
def test_cyrillic_urlencoded_payment_params(encoding, location):
    value = "1 Иванов И.И.;2 1 шт.;3 A&B=1+2;4 100%"
    raw = urlencode({"Params": value}, encoding=encoding).encode("ascii")
    request = Request(
        {
            "type": "http",
            "query_string": raw if location == "query" else b"",
            "headers": [],
        }
    )
    params = _parse_request_params(request, raw if location == "body" else b"")
    assert parse_params_string(params["Params"]) == {
        1: "Иванов И.И.",
        2: "1 шт.",
        3: "A&B=1+2",
        4: "100%",
    }
    assert set(params) == {"Params"}


def test_declared_windows1251_resolves_ambiguous_utf8_bytes():
    value = "РЎ"
    body = urlencode({"Params": value}, encoding="cp1251").encode()
    request = Request(
        {
            "type": "http",
            "query_string": b"",
            "headers": [
                (
                    b"content-type",
                    b'application/x-www-form-urlencoded; charset="windows-1251"',
                ),
            ],
        }
    )
    assert _parse_request_params(request, body)["Params"] == value


@pytest.mark.parametrize("encoding", ["utf-8", "windows-1251"])
def test_raw_cyrillic_and_body_override(encoding):
    request = Request(
        {"type": "http", "query_string": b"function=check&Params=old", "headers": []}
    )
    body = "function=payment&Params=1 Тест;2 00001&Amount=100".encode(encoding)
    assert _parse_request_params(request, body) == {
        "function": "payment",
        "Params": "1 Тест;2 00001",
        "Amount": "100",
    }
