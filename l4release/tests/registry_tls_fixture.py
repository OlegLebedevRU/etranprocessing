"""Real WinHTTP through loopback CONNECT refuses an ephemeral untrusted peer.

No external server, Windows certificate-store import or owner signing key.
"""

import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from datetime import UTC, datetime, timedelta
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID


def run(executable: Path) -> None:
    host = "l4tools-generic.ar.cloud.ru"
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)])
    now = datetime.now(UTC)
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(minutes=1))
        .not_valid_after(now + timedelta(hours=1))
        .add_extension(x509.SubjectAlternativeName([x509.DNSName(host)]), critical=False)
        .sign(key, hashes.SHA256())
    )
    with tempfile.TemporaryDirectory(prefix="l4registry-tls-") as temporary:
        directory = Path(temporary)
        (directory / "cert.pem").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        (directory / "key.pem").write_bytes(
            key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(directory / "cert.pem", directory / "key.pem")
        requests: list[bytes] = []
        errors: list[str] = []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(8)

            def serve() -> None:
                try:
                    connection, _ = listener.accept()
                    with connection:
                        connection.settimeout(6)
                        headers = b""
                        while b"\r\n\r\n" not in headers and len(headers) < 8192:
                            part = connection.recv(1024)
                            if not part:
                                raise RuntimeError("Missing CONNECT request")
                            headers += part
                        if not headers.startswith(f"CONNECT {host}:443 HTTP/1.1\r\n".encode()):
                            raise RuntimeError("Wrong CONNECT authority")
                        if b"authorization:" in headers.lower():
                            raise RuntimeError("Unexpected authentication")
                        requests.append(headers)
                        connection.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
                        try:
                            with context.wrap_socket(connection, server_side=True) as secure:
                                if secure.recv(1024):
                                    raise RuntimeError("GET reached an untrusted TLS peer")
                        except ssl.SSLError:
                            # Client must report the native secure failure below.
                            pass
                except (OSError, RuntimeError) as error:
                    errors.append(str(error))

            worker = threading.Thread(target=serve, daemon=True)
            worker.start()
            result = subprocess.run(
                [str(executable.resolve()), str(listener.getsockname()[1])],
                capture_output=True,
                text=True,
                timeout=15,
                check=False,
            )
            worker.join(8)
            if worker.is_alive() or errors or len(requests) != 1 or result.returncode:
                raise RuntimeError(
                    f"Isolated Registry TLS gate failed: {errors}; {result.stdout}; {result.stderr}"
                )
            print(result.stdout.strip())


if __name__ == "__main__":
    run(Path(sys.argv[1]))
