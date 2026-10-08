from pathlib import Path

import pytest
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

from l4release.common import ReleaseError
from l4release.metadata import load_signing_key, public_blob, sign_bytes, verify_bytes


@pytest.fixture(scope="module")
def key():
    return rsa.generate_private_key(public_exponent=65537, key_size=3072)


def test_exact_bytes_and_public_blob(key):
    document = b'{ "schema": 1 }\n'
    signature = sign_bytes(document, key)
    assert len(signature) == 384
    assert signature == sign_bytes(document, key)
    blob = public_blob(key.public_key())
    assert len(blob) == 411 and blob[:4] == b"RSA1" and blob[24:27] == b"\x01\x00\x01"
    verify_bytes(document, signature, key.public_key())
    with pytest.raises(ReleaseError, match="verification failed"):
        verify_bytes(document.rstrip(), signature, key.public_key())


@pytest.mark.parametrize("document", [b"", b"x" * 65536], ids=["empty", "oversized"])
def test_document_bounds(key, document):
    with pytest.raises(ReleaseError):
        sign_bytes(document, key)


def test_wrong_signature_key_and_algorithms(key):
    data = b"exact bytes"
    signature = sign_bytes(data, key)
    for bad in (signature[:-1], signature + b"x", b"\x00" * 384):
        with pytest.raises(ReleaseError):
            verify_bytes(data, bad, key.public_key())
    wrong = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    with pytest.raises(ReleaseError):
        verify_bytes(data, signature, wrong.public_key())
    for bad in (
        key.sign(
            data, padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32), hashes.SHA256()
        ),
        key.sign(data, padding.PKCS1v15(), hashes.SHA512()),
    ):
        with pytest.raises(ReleaseError):
            verify_bytes(data, bad, key.public_key())


def test_wrong_key_profile():
    weak = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    with pytest.raises(ReleaseError, match="RSA3072"):
        sign_bytes(b"fixture", weak)


def test_external_encrypted_key_loading(key, tmp_path):
    repo = tmp_path / "repo"
    repo.mkdir()
    private = tmp_path / "fixture.pem"
    password = "test-only-fixture-password"
    private.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.BestAvailableEncryption(password.encode()),
        )
    )
    try:
        env = {"L4TOOLS_METADATA_KEY_PATH": private.name, "L4TOOLS_METADATA_KEY_PASSWORD": password}
        loaded = load_signing_key(repo, env, tmp_path / "fixture.env")
        assert loaded.public_key().public_numbers() == key.public_key().public_numbers()
        with pytest.raises(ReleaseError, match="Cannot load"):
            load_signing_key(
                repo, {**env, "L4TOOLS_METADATA_KEY_PASSWORD": "wrong"}, tmp_path / "fixture.env"
            )
        with pytest.raises(ReleaseError, match="outside"):
            load_signing_key(
                repo, {**env, "L4TOOLS_METADATA_KEY_PATH": str(repo / "key.pem")}, Path(".")
            )
    finally:
        private.unlink(missing_ok=True)
