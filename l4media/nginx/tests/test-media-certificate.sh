#!/bin/sh
set -eu
gate=${1:-/docker-entrypoint.d/40-verify-media-certificate.sh}
fixture_dir=$(mktemp -d)
trap 'rm -rf "$fixture_dir"' EXIT HUP INT TERM
cd "$fixture_dir"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=TestCA \
    -keyout ca.key -out ca.pem >/dev/null 2>&1
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=OtherCA \
    -keyout other.key -out other.pem >/dev/null 2>&1
openssl req -new -newkey rsa:2048 -nodes -subj /CN=dev.leo4.ru \
    -keyout server.key -out server.csr >/dev/null 2>&1
printf 'subjectAltName=DNS:dev.leo4.ru\nextendedKeyUsage=serverAuth\n' > server.ext
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
    -days 1 -extfile server.ext -out server.pem >/dev/null 2>&1
export MEDIA_SERVER_CERT="$fixture_dir/server.pem"
export MEDIA_SERVER_KEY="$fixture_dir/server.key"
export MEDIA_CA_CERT="$fixture_dir/ca.pem"
sh "$gate"
reject() {
    if sh "$gate" >/dev/null 2>&1; then
        echo "FAIL: media gate accepted $1" >&2
        exit 1
    fi
    echo "PASS: rejected $1"
}
MEDIA_CA_CERT="$fixture_dir/other.pem" reject foreign-ca
MEDIA_SERVER_CERT="$fixture_dir/other.pem" MEDIA_SERVER_KEY="$fixture_dir/other.key" reject self-signed
MEDIA_SERVER_KEY="$fixture_dir/other.key" reject mismatched-key
MEDIA_SERVER_KEY="$fixture_dir/missing.key" reject missing-key
printf 'subjectAltName=DNS:wrong.example\nextendedKeyUsage=serverAuth\n' > wrong.ext
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAserial ca.srl \
    -days 1 -extfile wrong.ext -out wrong.pem >/dev/null 2>&1
MEDIA_SERVER_CERT="$fixture_dir/wrong.pem" reject wrong-dns-name
printf 'subjectAltName=DNS:dev.leo4.ru\nextendedKeyUsage=clientAuth\n' > client.ext
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAserial ca.srl \
    -days 1 -extfile client.ext -out client.pem >/dev/null 2>&1
MEDIA_SERVER_CERT="$fixture_dir/client.pem" reject client-only-purpose
# OpenSSL 3.5 requires positive -days; issue an explicitly dated fixture instead.
: > index.txt
printf '01\n' > serial.txt
cat > expired.cnf <<'EOF'
[ca]
default_ca=fixture
[fixture]
database=index.txt
serial=serial.txt
new_certs_dir=.
certificate=ca.pem
private_key=ca.key
default_md=sha256
default_days=1
policy=subject
[subject]
commonName=supplied
[server]
subjectAltName=DNS:dev.leo4.ru
extendedKeyUsage=serverAuth
EOF
openssl ca -batch -notext -config expired.cnf -extensions server \
    -startdate 20200101000000Z -enddate 20200102000000Z \
    -in server.csr -out expired.pem >/dev/null 2>&1
MEDIA_SERVER_CERT="$fixture_dir/expired.pem" reject expired-certificate
echo "PASS: media certificate gate"
