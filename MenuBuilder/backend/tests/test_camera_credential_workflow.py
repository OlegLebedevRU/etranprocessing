import base64
import json
import time
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock
from uuid import uuid4

import pytest
from fastapi import HTTPException

from app.services import camera_credential_workflow as workflow
from app.services.camera_credential_vault import VaultReceipt


@pytest.fixture
def anyio_backend():
    return "asyncio"


def request_data():
    return {
        "action": "replace",
        "operation_id": str(uuid4()),
        "expected_revision": "a" * 64,
        "expires_at_ms": int(time.time() * 1000) + 60000,
        "allow_disruptive": True,
        "username": "private-fixture-login",
        "password": "private-fixture-password",
    }


@pytest.mark.parametrize(
    "change",
    [
        lambda x: dict(x, extra="private-fixture-password"),
        lambda x: dict(x, username=4),
        lambda x: dict(x, password="x" * 129),
        lambda x: dict(x, password="\nprivate-fixture-password"),
        lambda x: dict(x, expires_at_ms=True),
        lambda x: dict(x, allow_disruptive=False),
    ],
)
def test_secret_input_rejection_never_reflects_request(change):
    raw = json.dumps(change(request_data())).encode()
    with pytest.raises(HTTPException) as error:
        workflow.parse_credential_change(raw)
    assert error.value.detail == {"code": "invalid_camera_credentials"}
    assert "private-fixture" not in str(error.value)


def test_duplicate_fields_and_oversize_are_rejected():
    for raw in [b'{"password":"one","password":"two"}', b" " * 4097]:
        with pytest.raises(HTTPException):
            workflow.parse_credential_change(raw)


@pytest.fixture
def context(monkeypatch):
    raw = request_data()
    change = workflow.parse_credential_change(json.dumps(raw).encode())
    encoded = lambda value: base64.b64encode(value).decode()
    envelope = {
        "v": 1,
        "alg": "RSA-OAEP-256+A256GCM",
        "sn": "fixture",
        "operation_id": raw["operation_id"],
        "recipient_cert_sha256": "b" * 64,
        "expires_at_ms": raw["expires_at_ms"],
        "wrapped_key": encoded(b"x" * 256),
        "nonce": encoded(b"x" * 12),
        "ciphertext": encoded(b"opaque-test-ciphertext"),
        "tag": encoded(b"x" * 16),
    }
    receipt = VaultReceipt(
        change.operation_id, 1, "replace", "pending", change.expires_at_ms, envelope
    )
    vault = SimpleNamespace(
        prepare_pending=AsyncMock(return_value=receipt),
        fail_pending=AsyncMock(),
        confirm=AsyncMock(return_value=True),
    )
    monkeypatch.setattr(workflow, "CameraCredentialVault", lambda: vault)
    monkeypatch.setattr(
        workflow, "fetch_credential_recipient", AsyncMock(return_value=object())
    )
    trace = []
    db = AsyncMock()
    db.scalar.return_value = None
    db.get.return_value = SimpleNamespace(kind="replace")
    db.add = Mock()
    db.commit.side_effect = lambda: trace.append("commit")

    async def upstream(method, path, *args, **kwargs):
        trace.append((method, path))
        if method == "GET":
            raise HTTPException(404)
        assert "private-fixture" not in json.dumps(kwargs["body"])
        return {"id": raw["operation_id"]}

    request = AsyncMock(side_effect=upstream)
    monkeypatch.setattr(workflow, "camera_request", request)
    args = {
        "db": db,
        "terminal": SimpleNamespace(id=1, device_id=4623, org_id=1, sn="fixture"),
        "user": {"sub": "8"},
        "change": change,
        "camera": {"revision": "a" * 64},
    }
    return args, vault, request, trace, envelope


@pytest.mark.anyio
async def test_only_opaque_envelope_leaves_bff_after_encrypted_intent_commit(context):
    args, vault, request, trace, envelope = context
    public = await workflow.dispatch_credentials(**args)
    body = request.await_args.kwargs["body"]
    assert trace.index("commit") < trace.index(("POST", "device-tasks/"))
    assert body["payload"]["dt"][0]["sealed_credentials"] == envelope
    assert (
        body["payload"]["dt"][0]["operation_id"] == body["ext_task_id"] == public["id"]
    )
    assert "private-fixture" not in json.dumps(public)
    assert "private-fixture" not in json.dumps(args["db"].add.call_args.args[0].details)
    vault.fail_pending.assert_not_awaited()


@pytest.mark.anyio
async def test_existing_task_is_read_without_post_or_resealing(context):
    args, _, request, _, _ = context
    request.side_effect = None
    request.return_value = {"header": {"device_id": 4623, "method_code": 166}}
    args["camera"]["revision"] = "b" * 64
    result = await workflow.dispatch_credentials(**args)
    assert result["id"] == str(args["change"].operation_id)
    assert request.await_count == 1 and request.await_args.args[0] == "GET"


@pytest.mark.anyio
async def test_changed_nonsecret_context_conflicts_before_dispatch(context):
    args, _, request, _, _ = context
    args["db"].scalar.return_value = SimpleNamespace(
        details={"expected_revision": "different"}
    )
    with pytest.raises(HTTPException) as error:
        await workflow.dispatch_credentials(**args)
    assert error.value.status_code == 409
    request.assert_not_awaited()
    args["db"].rollback.assert_awaited_once()


@pytest.mark.anyio
@pytest.mark.parametrize("status,failed", [(503, False), (422, True)])
async def test_unknown_delivery_stays_pending_but_definite_rejection_fails_it(
    context, status, failed
):
    args, vault, request, _, _ = context

    async def upstream(method, *args, **kwargs):
        raise HTTPException(404 if method == "GET" else status)

    request.side_effect = upstream
    with pytest.raises(HTTPException):
        await workflow.dispatch_credentials(**args)
    assert bool(vault.fail_pending.await_count) is failed


@pytest.mark.anyio
@pytest.mark.parametrize("matches", [False, True])
async def test_promotion_needs_exact_res_and_gauge_snapshot(context, matches):
    args, vault, request, _, _ = context
    operation = args["change"].operation_id
    snapshot = str(uuid4())
    task = {
        "header": {"device_id": 4623, "method_code": 166},
        "results": [
            {
                "status_code": 200,
                "result": {"operation_id": str(operation), "snapshot_id": snapshot},
            }
        ],
    }
    request.side_effect = None
    request.return_value = {
        "items": [
            {
                "device_id": 4623,
                "type": 84,
                "gauges": {
                    "300": [
                        {
                            "450": {
                                "operation_id": str(operation),
                                "snapshot_id": snapshot if matches else str(uuid4()),
                                "cameras": [
                                    {
                                        "camera_id": "cam:ip000001",
                                        "camera_contract_revision": "siplite-camera/1.0.0",
                                        "auth_source": "custom",
                                        "stale": False,
                                    }
                                ],
                            }
                        }
                    ]
                },
            }
        ]
    }
    await workflow.reconcile_credentials(
        args["db"],
        terminal=args["terminal"],
        user=args["user"],
        operation_id=operation,
        task=task,
    )
    assert bool(vault.confirm.await_count) is matches


@pytest.mark.anyio
@pytest.mark.parametrize(
    "path,role,expected",
    [("credentials", 4, 403), ("credentials", 3, 422), ("commands", 3, 422)],
)
async def test_http_errors_never_reflect_plaintext_or_dispatch(
    monkeypatch, path, role, expected
):
    from httpx import ASGITransport, AsyncClient

    from app.auth import get_current_user
    from app.database import get_db
    from app.main import app
    from app.routers import cameras

    terminal = SimpleNamespace(id=1, device_id=4623, org_id=1, sn="fixture")
    monkeypatch.setattr(cameras, "camera_access", AsyncMock(return_value=terminal))
    monkeypatch.setattr(cameras, "camera_main_app_online", AsyncMock(return_value=True))
    monkeypatch.setattr(
        cameras,
        "require_camera_extension",
        AsyncMock(return_value={"revision": "a" * 64}),
    )
    monkeypatch.setattr(
        cameras.iot_client,
        "remote_input_status",
        AsyncMock(return_value={"lease": {"active": False}}),
    )
    dispatch = AsyncMock()
    monkeypatch.setattr(cameras, "dispatch_credentials", dispatch)
    app.dependency_overrides[get_current_user] = lambda: {
        "role_id": role,
        "role": "viewer" if role == 4 else "user",
        "org_id": 1,
    }
    app.dependency_overrides[get_db] = lambda: AsyncMock()
    try:
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://fixture"
        ) as client:
            response = await client.post(
                f"/api/cameras/4623/{path}",
                json={"password": "private-fixture-password"},
            )
        assert response.status_code == expected
        assert "private-fixture" not in response.text
        dispatch.assert_not_awaited()
    finally:
        app.dependency_overrides.clear()


@pytest.mark.anyio
async def test_generic_clear_cannot_bypass_server_vault():
    from app.routers import cameras

    command = cameras.CameraCommand(
        action="clear_credentials",
        operation_id=uuid4(),
        expected_revision="a" * 64,
        allow_disruptive=True,
    )
    with pytest.raises(HTTPException) as error:
        await cameras.command_camera(4623, command, {"org_id": 1}, AsyncMock())
    assert error.value.status_code == 409
    assert error.value.detail == {"code": "use_camera_credentials_endpoint"}
