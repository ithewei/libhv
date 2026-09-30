#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT INT TERM

make_key() {
    openssl genrsa -traditional -out "$1" 2048
}

make_key "$tmp_dir/root.key"
openssl req -new -x509 -sha256 -days 36500 \
    -key "$tmp_dir/root.key" -out "$fixture_dir/root.crt" \
    -subj /CN=libhv-appletls-test-root \
    -addext basicConstraints=critical,CA:true \
    -addext keyUsage=critical,keyCertSign,cRLSign

make_key "$tmp_dir/intermediate.key"
openssl req -new -sha256 -key "$tmp_dir/intermediate.key" \
    -out "$tmp_dir/intermediate.csr" -subj /CN=libhv-appletls-test-intermediate
printf '%s\n' \
    'basicConstraints=critical,CA:true,pathlen:0' \
    'keyUsage=critical,keyCertSign,cRLSign' \
    'subjectKeyIdentifier=hash' \
    'authorityKeyIdentifier=keyid,issuer' > "$tmp_dir/intermediate.ext"
openssl x509 -req -sha256 -days 36500 \
    -in "$tmp_dir/intermediate.csr" -CA "$fixture_dir/root.crt" \
    -CAkey "$tmp_dir/root.key" -CAserial "$tmp_dir/root.srl" -CAcreateserial \
    -out "$fixture_dir/intermediate.crt" -extfile "$tmp_dir/intermediate.ext"

make_key "$fixture_dir/server-pkcs1.key"
openssl pkcs8 -topk8 -nocrypt -in "$fixture_dir/server-pkcs1.key" \
    -out "$fixture_dir/server-pkcs8.key"
openssl req -new -sha256 -key "$fixture_dir/server-pkcs1.key" \
    -out "$tmp_dir/server.csr" -subj /CN=localhost
printf '%s\n' \
    'basicConstraints=critical,CA:false' \
    'keyUsage=critical,digitalSignature,keyEncipherment' \
    'extendedKeyUsage=serverAuth' \
    'subjectAltName=DNS:localhost' > "$tmp_dir/server.ext"
openssl x509 -req -sha256 -days 36500 \
    -in "$tmp_dir/server.csr" -CA "$fixture_dir/intermediate.crt" \
    -CAkey "$tmp_dir/intermediate.key" -CAserial "$tmp_dir/intermediate.srl" -CAcreateserial \
    -out "$fixture_dir/server.crt" -extfile "$tmp_dir/server.ext"

make_key "$fixture_dir/client.key"
openssl req -new -sha256 -key "$fixture_dir/client.key" \
    -out "$tmp_dir/client.csr" -subj /CN=libhv-appletls-test-client
printf '%s\n' \
    'basicConstraints=critical,CA:false' \
    'keyUsage=critical,digitalSignature,keyEncipherment' \
    'extendedKeyUsage=clientAuth' > "$tmp_dir/client.ext"
openssl x509 -req -sha256 -days 36500 \
    -in "$tmp_dir/client.csr" -CA "$fixture_dir/root.crt" \
    -CAkey "$tmp_dir/root.key" -CAserial "$tmp_dir/root.srl" \
    -out "$fixture_dir/client.crt" -extfile "$tmp_dir/client.ext"

make_key "$tmp_dir/wrong-root.key"
openssl req -new -x509 -sha256 -days 36500 \
    -key "$tmp_dir/wrong-root.key" -out "$fixture_dir/wrong-root.crt" \
    -subj /CN=libhv-appletls-wrong-root \
    -addext basicConstraints=critical,CA:true \
    -addext keyUsage=critical,keyCertSign,cRLSign
make_key "$fixture_dir/wrong-server.key"

openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 \
    -out "$fixture_dir/ec-pkcs8.key"
openssl pkey -in "$fixture_dir/ec-pkcs8.key" -traditional \
    -out "$fixture_dir/ec-sec1.key"
openssl pkcs8 -topk8 -v2 aes-256-cbc -passout pass:test-only \
    -in "$fixture_dir/server-pkcs1.key" -out "$fixture_dir/encrypted-key.pem"

cat "$fixture_dir/server.crt" "$fixture_dir/intermediate.crt" \
    > "$fixture_dir/server-chain.pem"
cat "$fixture_dir/client.crt" > "$fixture_dir/client-chain.pem"
cat "$fixture_dir/root.crt" "$fixture_dir/wrong-root.crt" \
    > "$fixture_dir/multi-ca.pem"

mkdir -p "$fixture_dir/ca-dir"
cp "$fixture_dir/root.crt" "$fixture_dir/ca-dir/root.crt"
printf '%s\n' 'not a certificate' > "$fixture_dir/ca-dir/README.txt"
mkdir -p "$fixture_dir/empty-ca-dir"
printf '%s\n' 'not a certificate' > "$fixture_dir/empty-ca-dir/README.txt"

printf '%s\n' \
    '-----BEGIN CERTIFICATE-----' \
    'not-base64!' \
    '-----END CERTIFICATE-----' > "$fixture_dir/malformed-base64.pem"
printf '%s\n' \
    '-----BEGIN PRIVATE KEY-----' \
    'MAMCAQE=' \
    '-----END PRIVATE KEY-----' > "$fixture_dir/malformed-pkcs8.pem"

echo "Apple TLS fixtures regenerated in $fixture_dir"
