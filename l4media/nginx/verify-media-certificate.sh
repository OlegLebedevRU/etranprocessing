#!/bin/sh
# Startup and reload gate. Private keys stay in their mounted files.
set -eu

cert=${MEDIA_SERVER_CERT:-/crt/server_certificate.pem}
key=${MEDIA_SERVER_KEY:-/crt/server_key.pem}
ca=${MEDIA_CA_CERT:-/crt/ca_certificate.pem}
# This logical TLS identity also applies when TCP connects to a fallback IP.
name=dev.leo4.ru

for file in "$cert" "$key" "$ca"; do
    if [ ! -s "$file" ]; then
        echo "media TLS: required certificate/key/CA file missing" >&2
        exit 1
    fi
done

openssl verify -CAfile "$ca" -no-CApath -no-CAstore \
    -purpose sslserver -verify_hostname "$name" "$cert" >/dev/null
openssl x509 -in "$cert" -checkend 0 -noout >/dev/null

public_dir=$(mktemp -d)
trap 'rm -rf "$public_dir"' EXIT HUP INT TERM
openssl x509 -in "$cert" -pubkey -noout > "$public_dir/cert.pem"
openssl pkey -pubin -in "$public_dir/cert.pem" -outform DER > "$public_dir/cert.der"
openssl pkey -in "$key" -pubout -outform DER > "$public_dir/key.der"
if ! cmp -s "$public_dir/cert.der" "$public_dir/key.der"; then
    echo "media TLS: certificate and private key do not match" >&2
    exit 1
fi
echo "media TLS: CA chain, dev.leo4.ru identity, validity and key verified"
