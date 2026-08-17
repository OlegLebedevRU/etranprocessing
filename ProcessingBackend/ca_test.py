"""
Test script for CA serverless function — both old and new flows.

Usage:
    python ca_test.py [--ca-cert PATH] [--ca-key PATH]

Requires: cryptography
"""

import os
import sys
import urllib.parse
from datetime import datetime

from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID


def generate_test_csr(cn: str = "A1B2C3D4E5F6", device_id: int = 773, org_id: int = 223) -> str:
    """Generate a test PKCS10 CSR matching terminal format."""
    key = rsa.generate_private_key(65537, 2048, default_backend())

    subject = x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, cn),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, str(org_id)),
        x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, str(device_id)),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "msk"),
        x509.NameAttribute(NameOID.COUNTRY_NAME, "ru"),
        x509.NameAttribute(NameOID.LOCALITY_NAME, "42"),
        x509.NameAttribute(NameOID.EMAIL_ADDRESS, "1.terminal@forpay.ru"),
    ])

    builder = (
        x509.CertificateSigningRequestBuilder()
        .subject_name(subject)
        .sign(key, hashes.SHA256(), default_backend())
    )

    return builder.public_bytes(serialization.Encoding.PEM).decode("utf-8")


def setup_env(ca_cert: str, ca_key: str):
    os.environ["CA_CERT_PATH"] = ca_cert
    os.environ["CA_KEY_PATH"] = ca_key
    os.environ["CA_CERTS_DIR"] = "keys/certs/"
    os.makedirs("keys/certs/", exist_ok=True)


def verify_cert(result: dict, expected_cn: str, expected_e: str, expected_san: str = ""):
    """Verify certificate fields. Returns True if all checks pass."""
    if result["statusCode"] != 200:
        print(f"  FAIL: statusCode={result['statusCode']}, error={result.get('body', {})}")
        return False

    body = result["body"]
    cert_pem = urllib.parse.unquote(body["cert"])
    cert = x509.load_pem_x509_certificate(cert_pem.encode("utf-8"), default_backend())

    ok = True

    # CN
    cn = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0].value
    if cn == expected_cn:
        print(f"  CN:      {cn}  ✓")
    else:
        print(f"  CN:      {cn}  ✗ (expected {expected_cn})")
        ok = False

    # E
    try:
        email = cert.subject.get_attributes_for_oid(NameOID.EMAIL_ADDRESS)[0].value
        if email == expected_e:
            print(f"  E:       {email}  ✓")
        else:
            print(f"  E:       {email}  ✗ (expected {expected_e})")
            ok = False
    except Exception:
        print(f"  E:       NOT FOUND  ✗")
        ok = False

    # SAN
    if expected_san:
        try:
            san = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName)
            uris = san.value.get_values_for_type(x509.UniformResourceIdentifier)
            if expected_san in uris:
                print(f"  SAN:     {expected_san}  ✓")
            else:
                print(f"  SAN:     {uris}  ✗ (expected {expected_san})")
                ok = False
        except x509.ExtensionNotFound:
            print(f"  SAN:     NOT FOUND  ✗ (expected {expected_san})")
            ok = False

    # Metadata
    print(f"  Serial:  {body['serial_number']}")
    print(f"  Valid:   {body['not_valid_before']} → {body['not_valid_after']}")
    print(f"  SN:      {body['sn']}")
    print(f"  DevID:   {body['device_id']}")

    return ok


def test_new_flow(ca_cert: str, ca_key: str):
    """Test new flow: X-CN present → CN override + SAN. Uses Title-Case headers (Yandex Cloud)."""
    from ca_sign_csr import sign_csr_from_headers

    print("=" * 60)
    print("NEW FLOW (X-Cn present → CN override + SAN)")
    print("=" * 60)

    csr_pem = generate_test_csr(cn="A1B2C3D4E5F6")  # terminal puts sign as CN

    # Yandex Cloud converts headers to Title-Case
    event = {
        "headers": {
            "X-Ssl-Client-Csr": urllib.parse.quote(csr_pem),
            "X-Ssl-Client-Exp-Days": "365",
            "X-Sign": "A1B2C3D4E5F67890",
            "X-Cn": "a3b0000000c10221d290825",
        }
    }

    result = sign_csr_from_headers(event, None)
    ok = verify_cert(
        result,
        expected_cn="a3b0000000c10221d290825",
        expected_e="1.terminal@forpay.ru",
        expected_san="urn:sign:A1B2C3D4E5F67890",
    )

    print(f"\n{'PASS' if ok else 'FAIL'}\n")
    return ok


def test_old_flow(ca_cert: str, ca_key: str):
    """Test old flow: no X-CN → sign CSR as-is."""
    from ca_sign_csr import sign_csr_from_headers

    print("=" * 60)
    print("OLD FLOW (no X-CN → sign as-is)")
    print("=" * 60)

    csr_pem = generate_test_csr(cn="a3b0000000c10221d290825")  # CN = SN directly

    event = {
        "headers": {
            "X-Ssl-Client-Csr": urllib.parse.quote(csr_pem),
            "X-Ssl-Client-Exp-Days": "365",
        }
    }

    result = sign_csr_from_headers(event, None)
    ok = verify_cert(
        result,
        expected_cn="a3b0000000c10221d290825",
        expected_e="1.terminal@forpay.ru",
        expected_san="",  # no SAN expected
    )

    print(f"\n{'PASS' if ok else 'FAIL'}\n")
    return ok


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--ca-cert", default="keys/ca.crt")
    parser.add_argument("--ca-key", default="keys/ca.key")
    args = parser.parse_args()

    setup_env(args.ca_cert, args.ca_key)

    r1 = test_new_flow(args.ca_cert, args.ca_key)
    r2 = test_old_flow(args.ca_cert, args.ca_key)

    print("=" * 60)
    print(f"SUMMARY: new_flow={'PASS' if r1 else 'FAIL'}, old_flow={'PASS' if r2 else 'FAIL'}")
    print("=" * 60)

    sys.exit(0 if (r1 and r2) else 1)
