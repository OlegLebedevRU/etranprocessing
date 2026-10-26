from types import SimpleNamespace
from unittest.mock import AsyncMock, patch
from uuid import uuid4

import pytest
import test_hevc_contract as hevc_fixtures
import test_video_stream_permissions as fixtures
from fastapi import HTTPException
from httpx import ASGITransport, AsyncClient

from app.database import get_db
from app.main import app
from app.services.iot_client import iot_client

reset_dependency_overrides = fixtures.reset_dependency_overrides
anyio_backend = fixtures.anyio_backend
mock_db_session = fixtures.mock_db_session
superuser_token = fixtures.superuser_token
viewer_with_permission_token = fixtures.viewer_with_permission_token
context = hevc_fixtures.context


@pytest.mark.anyio
async def test_viewer_can_read_measurements_but_cannot_set_quality(
    mock_db_session, viewer_with_permission_token
):
    app.dependency_overrides[get_db] = lambda: mock_db_session
    stream, lease, writer = str(uuid4()), str(uuid4()), str(uuid4())
    with (
        patch(
            "app.routers.video_control._verify_device_access",
            new=AsyncMock(return_value=SimpleNamespace(sn="fixture", org_id=1)),
        ),
        patch.object(
            iot_client,
            "remote_video_v2",
            new=AsyncMock(return_value={"available": False}),
        ) as provider,
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            headers = {"Authorization": f"Bearer {viewer_with_permission_token}"}
            read = await client.get(
                f"/api/v1/video/devices/1/stream/quality?stream_instance_id={stream}",
                headers=headers,
            )
            write = await client.post(
                "/api/v1/video/devices/1/stream/quality",
                headers=headers,
                json={
                    "lease_id": lease,
                    "stream_instance_id": stream,
                    "target_bitrate_kbps": 1000,
                    "writer_key": writer,
                },
            )
    assert read.status_code == 200 and read.json() == {"available": False}
    assert write.status_code == 403
    assert provider.await_count == 1
    assert provider.call_args.args == ("fixture", "quality-read")


@pytest.mark.anyio
async def test_superuser_keeps_exact_identity_and_provider_can_refuse_other_session(
    mock_db_session, superuser_token
):
    app.dependency_overrides[get_db] = lambda: mock_db_session
    stream, lease, writer = str(uuid4()), str(uuid4()), str(uuid4())
    with (
        patch(
            "app.routers.video_control._verify_device_access",
            new=AsyncMock(return_value=SimpleNamespace(sn="fixture", org_id=1)),
        ),
        patch.object(
            iot_client,
            "remote_video_v2",
            new=AsyncMock(
                side_effect=HTTPException(403, {"code": "quality_publisher_required"})
            ),
        ) as provider,
    ):
        async with AsyncClient(
            transport=ASGITransport(app=app), base_url="http://test"
        ) as client:
            response = await client.post(
                "/api/v1/video/devices/1/stream/quality",
                headers={"Authorization": f"Bearer {superuser_token}"},
                json={
                    "lease_id": lease,
                    "stream_instance_id": stream,
                    "target_bitrate_kbps": 1000,
                    "writer_key": writer,
                },
            )
    assert response.status_code == 403
    assert provider.call_args.args == ("fixture", "quality-set")
    assert provider.call_args.kwargs["lease_id"] == lease
    assert provider.call_args.kwargs["user"]["session_id"] == "sess-su-1"
    assert provider.call_args.kwargs["payload"] == {
        "stream_instance_id": stream,
        "target_bitrate_kbps": 1000,
        "writer_key": writer,
    }


@pytest.mark.anyio
async def test_writer_key_reaches_only_initial_producer_http_start(context):
    service, args, trace, _ = context
    writer = str(uuid4())
    result = await service.start(**args, quality_writer_key=writer)
    assert result["result"] == "started"
    producer = [entry[2] for entry in trace if entry[:2] == ("iot", "start")]
    assert len(producer) == 1 and producer[0]["quality_writer_key"] == writer
    assert "quality_writer_key" not in producer[0]["payload"]
    assert "writer_key" not in producer[0]["payload"]
    for plane, action, payload in trace:
        if plane == "media" or action != "start":
            assert writer not in str(payload)
