#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT INT TERM
start_date=20261001000000Z
ca_end_date=20461001000000Z
leaf_end_date=20271001000000Z

cat > "$tmp_dir/ca.cnf" <<EOF
[ ca ]
default_ca = signer
[ signer ]
database = $tmp_dir/index.txt
new_certs_dir = $tmp_dir
serial = $tmp_dir/serial
private_key = $tmp_dir/signer.key
certificate = $tmp_dir/signer.crt
default_md = sha256
policy = match_anything
[ match_anything ]
commonName = supplied
EOF
touch "$tmp_dir/index.txt"
printf '0100\n' > "$tmp_dir/serial"

make_key() {
    openssl genrsa -traditional -out "$1" 2048
}

make_key "$tmp_dir/root.key"
openssl req -new -sha256 -key "$tmp_dir/root.key" -out "$tmp_dir/root.csr" \
    -subj /CN=libhv-appletls-test-root
printf '%s\n' \
    '[x509_ext]' \
    'basicConstraints=critical,CA:true' \
    'keyUsage=critical,keyCertSign,cRLSign' \
    'subjectKeyIdentifier=hash' > "$tmp_dir/root.ext"
openssl ca -batch -selfsign -config "$tmp_dir/ca.cnf" \
    -keyfile "$tmp_dir/root.key" -in "$tmp_dir/root.csr" \
    -startdate "$start_date" -enddate "$ca_end_date" \
    -extensions x509_ext -extfile "$tmp_dir/root.ext" \
    -out "$fixture_dir/root.crt" -notext

make_key "$tmp_dir/intermediate.key"
openssl req -new -sha256 -key "$tmp_dir/intermediate.key" \
    -out "$tmp_dir/intermediate.csr" -subj /CN=libhv-appletls-test-intermediate
printf '%s\n' \
    '[x509_ext]' \
    'basicConstraints=critical,CA:true,pathlen:0' \
    'keyUsage=critical,keyCertSign,cRLSign' \
    'subjectKeyIdentifier=hash' \
    'authorityKeyIdentifier=keyid,issuer' > "$tmp_dir/intermediate.ext"
cp "$tmp_dir/root.key" "$tmp_dir/signer.key"
cp "$fixture_dir/root.crt" "$tmp_dir/signer.crt"
printf '1000\n' > "$tmp_dir/serial"
openssl ca -batch -config "$tmp_dir/ca.cnf" -in "$tmp_dir/intermediate.csr" \
    -startdate "$start_date" -enddate "$ca_end_date" \
    -extensions x509_ext -extfile "$tmp_dir/intermediate.ext" \
    -out "$fixture_dir/intermediate.crt" -notext

make_key "$fixture_dir/server-pkcs1.key"
openssl pkcs8 -topk8 -nocrypt -in "$fixture_dir/server-pkcs1.key" \
    -out "$fixture_dir/server-pkcs8.key"
openssl req -new -sha256 -key "$fixture_dir/server-pkcs1.key" \
    -out "$tmp_dir/server.csr" -subj /CN=localhost
printf '%s\n' \
    '[x509_ext]' \
    'basicConstraints=critical,CA:false' \
    'keyUsage=critical,digitalSignature,keyEncipherment' \
    'extendedKeyUsage=serverAuth' \
    'subjectAltName=DNS:localhost' > "$tmp_dir/server.ext"
cp "$tmp_dir/intermediate.key" "$tmp_dir/signer.key"
cp "$fixture_dir/intermediate.crt" "$tmp_dir/signer.crt"
printf '2000\n' > "$tmp_dir/serial"
openssl ca -batch -config "$tmp_dir/ca.cnf" -in "$tmp_dir/server.csr" \
    -startdate "$start_date" -enddate "$leaf_end_date" \
    -extensions x509_ext -extfile "$tmp_dir/server.ext" \
    -out "$fixture_dir/server.crt" -notext

make_key "$fixture_dir/client.key"
openssl req -new -sha256 -key "$fixture_dir/client.key" \
    -out "$tmp_dir/client.csr" -subj /CN=libhv-appletls-test-client
printf '%s\n' \
    '[x509_ext]' \
    'basicConstraints=critical,CA:false' \
    'keyUsage=critical,digitalSignature,keyEncipherment' \
    'extendedKeyUsage=clientAuth' > "$tmp_dir/client.ext"
cp "$tmp_dir/root.key" "$tmp_dir/signer.key"
cp "$fixture_dir/root.crt" "$tmp_dir/signer.crt"
printf '3000\n' > "$tmp_dir/serial"
openssl ca -batch -config "$tmp_dir/ca.cnf" -in "$tmp_dir/client.csr" \
    -startdate "$start_date" -enddate "$leaf_end_date" \
    -extensions x509_ext -extfile "$tmp_dir/client.ext" \
    -out "$fixture_dir/client.crt" -notext

make_key "$tmp_dir/wrong-root.key"
openssl req -new -sha256 -key "$tmp_dir/wrong-root.key" \
    -out "$tmp_dir/wrong-root.csr" -subj /CN=libhv-appletls-wrong-root
cp "$tmp_dir/wrong-root.key" "$tmp_dir/signer.key"
printf '4000\n' > "$tmp_dir/serial"
openssl ca -batch -selfsign -config "$tmp_dir/ca.cnf" \
    -keyfile "$tmp_dir/wrong-root.key" -in "$tmp_dir/wrong-root.csr" \
    -startdate "$start_date" -enddate "$ca_end_date" \
    -extensions x509_ext -extfile "$tmp_dir/root.ext" \
    -out "$fixture_dir/wrong-root.crt" -notext
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
