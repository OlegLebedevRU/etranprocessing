"""S3 control plane: signing and HEAD only, never file bodies."""

import asyncio
import base64
from typing import Any

import boto3
from botocore.config import Config
from botocore.exceptions import BotoCoreError, ClientError
from fastapi import HTTPException

from app.config import settings

MAX_FILE_BYTES = 64 * 1024 * 1024


def client() -> Any:
    values = (
        settings.file_manager_s3_endpoint,
        settings.file_manager_s3_region,
        settings.file_manager_s3_bucket,
        settings.file_manager_s3_access_key,
        settings.file_manager_s3_secret_key,
    )
    if not all(values) or not settings.file_manager_s3_endpoint.startswith("https://"):
        raise HTTPException(503, detail={"code": "fm_storage_unconfigured"})
    return boto3.client(
        "s3",
        endpoint_url=values[0],
        region_name=values[1],
        aws_access_key_id=values[3],
        aws_secret_access_key=values[4],
        config=Config(
            signature_version="s3v4",
            connect_timeout=3,
            read_timeout=5,
            retries={"total_max_attempts": 1},
            s3={"addressing_style": "path"},
        ),
    )


def checksum(sha256: str) -> str:
    return base64.b64encode(bytes.fromhex(sha256)).decode("ascii")


async def upload_grant(key: str, size: int, sha256: str, ttl: int) -> dict:
    def sign() -> dict:
        s3 = client()
        if (
            s3.get_bucket_versioning(Bucket=settings.file_manager_s3_bucket).get(
                "Status"
            )
            != "Enabled"
        ):
            raise HTTPException(503, detail={"code": "fm_storage_versioning_required"})
        digest = checksum(sha256)
        url = s3.generate_presigned_url(
            "put_object",
            Params={
                "Bucket": settings.file_manager_s3_bucket,
                "Key": key,
                "ContentLength": size,
                "ChecksumSHA256": digest,
                "ContentType": "application/octet-stream",
            },
            ExpiresIn=ttl,
            HttpMethod="PUT",
        )
        return {
            "url": url,
            "method": "PUT",
            "headers": {
                "x-amz-checksum-sha256": digest,
                "Content-Type": "application/octet-stream",
            },
            "size_bytes": size,
            "sha256": sha256,
            "expires_in": ttl,
        }

    try:
        return await asyncio.to_thread(sign)
    except (BotoCoreError, ClientError) as exc:
        raise HTTPException(503, detail={"code": "fm_storage_unavailable"}) from exc


async def verify_object(key: str, size: int, sha256: str) -> str:
    try:
        result = await asyncio.to_thread(
            client().head_object,
            Bucket=settings.file_manager_s3_bucket,
            Key=key,
            ChecksumMode="ENABLED",
        )
    except (BotoCoreError, ClientError) as exc:
        raise HTTPException(
            409, detail={"code": "fm_storage_verification_failed"}
        ) from exc
    version = result.get("VersionId")
    if (
        result.get("ContentLength") != size
        or result.get("ChecksumSHA256") != checksum(sha256)
        or not version
        or version == "null"
    ):
        raise HTTPException(409, detail={"code": "fm_integrity_failed"})
    return version


async def download_grant(key: str, version: str, ttl: int) -> dict:
    try:
        url = await asyncio.to_thread(
            client().generate_presigned_url,
            "get_object",
            Params={
                "Bucket": settings.file_manager_s3_bucket,
                "Key": key,
                "VersionId": version,
            },
            ExpiresIn=ttl,
            HttpMethod="GET",
        )
        return {"url": url, "method": "GET", "headers": {}, "expires_in": ttl}
    except (BotoCoreError, ClientError) as exc:
        raise HTTPException(503, detail={"code": "fm_storage_unavailable"}) from exc
