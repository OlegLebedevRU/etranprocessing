"""Generate a development CA certificate and key pair.

Usage:
    python generate_ca_keys.py [--out-dir DIR]

Generates:
    ca.crt  — self-signed CA certificate
    ca.key  — CA private key (PEM, no password)
"""

import argparse
import os
from datetime import datetime, timedelta

from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa


def generate_ca(out_dir: str, key_size: int = 2048, valid_years: int = 10) -> None:
    os.makedirs(out_dir, exist_ok=True)

    key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=key_size,
        backend=default_backend(),
    )

    subject = issuer = x509.Name([
        x509.NameAttribute(x509.NameOID.COUNTRY_NAME, "RU"),
        x509.NameAttribute(x509.NameOID.STATE_OR_PROVINCE_NAME, "Moscow"),
        x509.NameAttribute(x509.NameOID.LOCALITY_NAME, "Moscow"),
        x509.NameAttribute(x509.NameOID.ORGANIZATION_NAME, "Platerra"),
        x509.NameAttribute(x509.NameOID.COMMON_NAME, "Platerra CA"),
    ])

    now = datetime.utcnow()
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + timedelta(days=valid_years * 365))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                key_cert_sign=True,
                crl_sign=True,
                content_commitment=False,
                key_encipherment=False,
                data_encipherment=False,
                key_agreement=False,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        .sign(private_key=key, algorithm=hashes.SHA256(), backend=default_backend())
    )

    key_path = os.path.join(out_dir, "ca.key")
    cert_path = os.path.join(out_dir, "ca.crt")

    with open(key_path, "wb") as f:
        f.write(key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption(),
        ))

    with open(cert_path, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))

    print(f"CA key:  {key_path}")
    print(f"CA cert: {cert_path}")
    print(f"Valid:   {now.date()} → {(now + timedelta(days=valid_years * 365)).date()}")
    print(f"Subject: {subject.rfc4514_string()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate dev CA keys")
    parser.add_argument("--out-dir", default="keys", help="Output directory (default: keys)")
    args = parser.parse_args()
    generate_ca(args.out_dir)
