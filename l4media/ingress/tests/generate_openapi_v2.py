"""Generate the service OpenAPI 3.1 projection without changing frozen schemas."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def generate() -> dict:
    raw = (ROOT / "spec/hevc.schema.json").read_text()
    definitions = json.loads(raw.replace("#/$defs/", "#/components/schemas/"))["$defs"]
    def response(name: str) -> dict:
        return {"description": name, "content": {"application/json": {"schema": {"$ref": "#/components/schemas/" + name}}}}
    paths = {}
    for action in ("start", "stop", "renew"):
        paths[f"/api/v2/media/sessions/{action}"] = {"post": {
            "security": [{"ServiceTokenAuth": []}, {"BearerAuth": []}],
            "requestBody": {"required": True, "content": {"application/json": {"schema": {"$ref": "#/components/schemas/media_" + action}}}},
            "responses": {code: response("media_response" if code in ("200", "201") else "rejection")
                          for code in (("200", "201", "400", "401", "409", "410", "422", "502", "503") if action == "start" else ("200", "400", "401", "409", "410", "503"))},
        }}
    common = {"security": [{"ServiceTokenAuth": []}, {"BearerAuth": []}]}
    paths["/api/v2/media/capabilities"] = {"get": dict(common, responses={"200": response("media_capabilities"), "401": response("rejection"), "503": response("rejection")})}
    params = [{"name": "session_id", "in": "path", "required": True, "schema": {"type": "string", "format": "uuid"}}]
    for key in ("sn", "lease_id", "stream_instance_id", "descriptor_revision"):
        params.append({"name": key, "in": "query", "required": True, "schema": definitions["media_identity"]["properties"][key]})
    paths["/api/v2/media/sessions/{session_id}"] = {"get": dict(common, parameters=params, responses={"200": response("media_health"), "400": response("rejection"), "401": response("rejection"), "409": response("rejection")})}
    needed = set()
    def references(value):
        if isinstance(value, dict):
            for key, item in value.items():
                if key == "$ref":
                    name = item.rsplit("/", 1)[-1]
                    if name not in needed:
                        needed.add(name)
                        references(definitions[name])
                else:
                    references(item)
        elif isinstance(value, list):
            for item in value:
                references(item)
    references(paths)
    definitions = {name: definitions[name] for name in sorted(needed)}
    return {"openapi": "3.1.0", "info": {"title": "l4media HEVC service API", "version": "2.0.0"}, "paths": paths,
            "components": {"schemas": definitions, "securitySchemes": {
                "ServiceTokenAuth": {"type": "apiKey", "in": "header", "name": "X-Media-Service-Token"},
                "BearerAuth": {"type": "http", "scheme": "bearer"}}}}


if __name__ == "__main__":
    (ROOT / "openapi-v2.json").write_text(json.dumps(generate(), indent=2) + "\n", newline="\n")
