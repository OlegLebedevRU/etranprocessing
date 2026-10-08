"""Unsigned isolated producer fixtures for native descriptor tests; never publish."""

from pathlib import Path

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa

from l4release.layout_payload import build_layout_payload
from l4release.metadata import public_blob, sign_bytes
from l4release.tests.test_layout_payload import make_stage

if __name__ == "__main__":
    output = Path("tools/dist/.release/manifest-fixture")
    stage = output / "stage"
    make_stage(stage)
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    (output / "fixture-public.blob").write_bytes(public_blob(key.public_key()))
    wrong = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    (output / "fixture-wrong-public.blob").write_bytes(public_blob(wrong.public_key()))
    malformed = b'{"schema":'
    (output / "malformed.json").write_bytes(malformed)
    (output / "malformed.json.sig").write_bytes(sign_bytes(malformed, key))
    maximum = b"x" * 65535
    (output / "maximum.bin").write_bytes(maximum)
    (output / "maximum.bin.sig").write_bytes(sign_bytes(maximum, key))
    for arch in ("x86", "x64"):
        build_layout_payload(stage, output, "1.13.3", arch, "11" * 32)
        name = output / f"l4tools-layout-{arch}.json"
        document = name.read_bytes()
        name.with_suffix(".json.sig").write_bytes(sign_bytes(document, key))
        (output / f"{arch}-pss.sig").write_bytes(
            key.sign(
                document,
                padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32),
                hashes.SHA256(),
            )
        )
        (output / f"{arch}-sha512.sig").write_bytes(
            key.sign(document, padding.PKCS1v15(), hashes.SHA512())
        )
