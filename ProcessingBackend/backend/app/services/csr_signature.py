"""Verify terminal proof of possession, including legacy XEnroll RSA/SHA1 CSRs.

This verifies an enrollment request, not a certificate signature. Issuance key
size, PIN/identity admission and the CA's SHA256 certificate policy stay separate.
"""

from cryptography import x509
from cryptography.exceptions import InvalidSignature, UnsupportedAlgorithm
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.x509.oid import SignatureAlgorithmOID


def valid_terminal_csr_signature(csr: x509.CertificateSigningRequest) -> bool:
    try:
        if csr.signature_algorithm_oid != SignatureAlgorithmOID.RSA_WITH_SHA1:
            return csr.is_signature_valid
        key = csr.public_key()
        if not isinstance(key, rsa.RSAPublicKey):
            return False
        # cryptography 50 rejects SHA1 in the generic X.509 verifier. XEnroll
        # on XP uses it for proof of possession; verify exact signed DER bytes.
        key.verify(
            csr.signature, csr.tbs_certrequest_bytes, padding.PKCS1v15(), hashes.SHA1()
        )
        return True
    except InvalidSignature, UnsupportedAlgorithm, ValueError:
        return False
