"""Camera API errors never reflect credentials, including misrouted malformed input."""

import json

from fastapi import Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from fastapi.routing import APIRoute

from app.services.camera_contract import _bounded


class CameraRequestRoute(APIRoute):
    def get_route_handler(self):
        original = super().get_route_handler()

        async def handle(request: Request):
            if request.method == "POST" and request.url.path.endswith("/commands"):
                raw = bytearray()
                async for chunk in request.stream():
                    if len(raw) + len(chunk) > 8192:
                        return JSONResponse(
                            status_code=413,
                            content={"detail": {"code": "invalid_camera_request"}},
                        )
                    raw.extend(chunk)

                def unique(pairs):
                    result = {}
                    for key, value in pairs:
                        if key in result:
                            raise ValueError
                        result[key] = value
                    return result

                try:
                    value = json.loads(raw, object_pairs_hook=unique)
                    if not isinstance(value, dict) or not _bounded(value):
                        raise ValueError
                except ValueError, TypeError, RecursionError:
                    return JSONResponse(
                        status_code=422,
                        content={"detail": {"code": "invalid_camera_request"}},
                    )
                request._body = bytes(raw)
            try:
                return await original(request)
            except RequestValidationError:
                return JSONResponse(
                    status_code=422,
                    content={"detail": {"code": "invalid_camera_request"}},
                )

        return handle
