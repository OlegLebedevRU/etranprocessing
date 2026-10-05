"""Operator-side synthetic S3 probe; no production server handles file bytes."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
from uuid import uuid4

import boto3
import httpx
from botocore.config import Config
from botocore.exceptions import ClientError
from dotenv import dotenv_values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--bucket", required=True)
    parser.add_argument("--region", required=True)
    parser.add_argument("--configure", action="store_true")
    parser.add_argument("--origin", action="append", default=[])
    args = parser.parse_args()
    values = dotenv_values(args.profile)
    s3 = boto3.client(
        "s3",
        endpoint_url=values["S3_ENDPOINT"],
        region_name=args.region,
        aws_access_key_id=values["S3_TENANT_ID"] + ":" + values["S3_KEY_ID"],
        aws_secret_access_key=values["S3_KEY_SECRET"],
        config=Config(
            signature_version="s3v4",
            s3={"addressing_style": "path"},
            connect_timeout=5,
            read_timeout=8,
            retries={"total_max_attempts": 1},
        ),
    )
    bucket = args.bucket
    versioning = s3.get_bucket_versioning(Bucket=bucket).get("Status")
    if args.configure:
        # Only the explicitly selected FM bucket; preserve existing policies/rules.
        if versioning != "Enabled":
            s3.put_bucket_versioning(
                Bucket=bucket, VersioningConfiguration={"Status": "Enabled"}
            )
        cors = s3.get_bucket_cors(Bucket=bucket)["CORSRules"]
        rule = next(r for r in cors if r.get("ID") == "fm-browser-transfer")
        rule["AllowedOrigins"] = sorted(set(rule["AllowedOrigins"] + args.origin))
        s3.put_bucket_cors(Bucket=bucket, CORSConfiguration={"CORSRules": cors})
        rules = s3.get_bucket_lifecycle_configuration(Bucket=bucket)["Rules"]
        cleanup = next(
            r
            for r in rules
            if r.get("ID") == "fm-temp-cleanup" and r.get("Prefix") == "fm/"
        )
        cleanup["NoncurrentVersionExpiration"] = {"NoncurrentDays": 1}
        s3.put_bucket_lifecycle_configuration(
            Bucket=bucket, LifecycleConfiguration={"Rules": rules}
        )
    assert s3.get_bucket_versioning(Bucket=bucket).get("Status") == "Enabled", (
        "Versioning required"
    )
    block = s3.get_public_access_block(Bucket=bucket)["PublicAccessBlockConfiguration"]
    assert all(block.values()), "Public access must remain blocked"
    key = f"fm/_probe/{uuid4()}/object"
    versions = []
    try:
        with httpx.Client(
            timeout=15, follow_redirects=False, trust_env=False
        ) as browser:
            for payload in (b"", b"fm1", b"fm2"):
                digest = base64.b64encode(hashlib.sha256(payload).digest()).decode()
                url = s3.generate_presigned_url(
                    "put_object",
                    Params={
                        "Bucket": bucket,
                        "Key": key,
                        "ContentLength": len(payload),
                        "ChecksumSHA256": digest,
                        "ContentType": "application/octet-stream",
                    },
                    HttpMethod="PUT",
                    ExpiresIn=45,
                )
                headers = {
                    "Content-Type": "application/octet-stream",
                    "x-amz-checksum-sha256": digest,
                }
                if payload:
                    corrupt = browser.put(url, headers=headers, content=b"bad")
                    assert corrupt.status_code >= 400, "Provider accepted corrupt body"
                response = browser.put(url, headers=headers, content=payload)
                assert response.status_code in (200, 204), "Signed PUT rejected"
                head = s3.head_object(Bucket=bucket, Key=key, ChecksumMode="ENABLED")
                version = head.get("VersionId")
                if version and version != "null":
                    versions.append(version)
                assert version and version != "null", "Missing immutable version"
                assert head.get("ContentLength") == len(payload), "HEAD length mismatch"
                assert head.get("ChecksumSHA256") == digest, (
                    "HEAD SHA256 unavailable/mismatch"
                )
            pinned = s3.generate_presigned_url(
                "get_object",
                Params={"Bucket": bucket, "Key": key, "VersionId": versions[1]},
                HttpMethod="GET",
                ExpiresIn=45,
            )
            response = browser.get(pinned)
            assert response.status_code == 200 and response.content == b"fm1", (
                "Late PUT changed pinned GET"
            )
        print(
            json.dumps(
                {
                    "versioning": "Enabled",
                    "signed_checksum_rejection": "passed",
                    "zero_byte": "passed",
                    "head_checksum": "passed",
                    "pinned_version": "passed",
                }
            )
        )
    finally:
        # Delete only versions created by this probe. No bucket-wide cleanup.
        for version in versions:
            s3.delete_object(Bucket=bucket, Key=key, VersionId=version)


if __name__ == "__main__":
    try:
        main()
    except ClientError as error:
        raise SystemExit(
            "S3 probe failed: " + error.response["Error"]["Code"]
        ) from None
    except Exception as error:  # noqa: BLE001 - redact URLs/credentials at the CLI boundary
        # Never print signed URLs, SDK request details or credentials.
        raise SystemExit("S3 probe failed: " + type(error).__name__) from None
