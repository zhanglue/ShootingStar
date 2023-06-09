#!/bin/bash

# Generate a self-signed certificate for the server to use.

CertPath="certs"
CertName="localhost"

while (( $# ))
do
    case $1 in
        -p | --cert-path )
            shift
            CertPath=$1
            ;;
        -n | --cert-name )
            shift
            CertName=$1
            ;;
        *)
            ;;
    esac
    shift
done

if [[ -z $CertPath ]]; then
    echo "Error: CertPath is empty."
    exit 1
fi

if [[ ${CertPath} == ${HOME} ]]; then
    echo "Error: CertPath is \$HOME: ${HOME}"
    exit 1
fi

if [[ -z $CertName ]]; then
    echo "Error: CertName is empty."
    exit 1
fi

if [[ -z ${SELF_SIGNED_HTTPS_CERT_PASSWORD} ]]; then
    echo "Error: set SELF_SIGNED_HTTPS_CERT_PASSWORD environment variable."
    exit 1
fi

echo "################################################################################"
echo "Generate a self-signed certificate for ${CertName} in ${CertPath}."
echo -e "\n################################################################################"

[[ -e $CertPath ]] && rm -rf $CertPath
mkdir -p $CertPath

echo """[req]
default_bits       = 2048
default_keyfile    = localhost.key
distinguished_name = req_distinguished_name
req_extensions     = req_ext
x509_extensions    = v3_ca

[req_distinguished_name]
commonName         = Common Name aka CN
commonName_default = localhost
commonName_max     = 64

[req_ext]
subjectAltName = @alt_names

[v3_ca]
subjectAltName = @alt_names
basicConstraints = critical, CA:false
keyUsage = keyCertSign, cRLSign, digitalSignature,keyEncipherment

[alt_names]
DNS.1   = localhost
DNS.2   = 127.0.0.1
""" > "${CertPath}/${CertName}.conf"

openssl req \
    -x509 -days 3650 -newkey rsa:2048 -subj "/CN=localhost" \
    -passout env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -config "${CertPath}/${CertName}.conf" \
    -keyout "${CertPath}/${CertName}.key" \
    -out "${CertPath}/${CertName}.crt"

openssl pkcs12 \
    -passin env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -inkey "${CertPath}/${CertName}.key" \
    -in "${CertPath}/${CertName}.crt" \
    -passout env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -export -out "${CertPath}/${CertName}.pfx"

[[ -e '/usr/local/share/ca-certificates' ]] || sudo mkdir -p '/usr/local/share/ca-certificates'
[[ -e "/usr/local/share/ca-certificates/${CertName}.crt" ]] && sudo rm -f "/usr/local/share/ca-certificates/${CertName}.crt"
[[ -e "/etc/ssl/certs/${CertName}.pem" ]] && sudo rm -f "/etc/ssl/certs/${CertName}.pem"

sudo cp "${CertPath}/${CertName}.crt" '/usr/local/share/ca-certificates'
sudo update-ca-certificates

echo -e "\n################################################################################"
echo -n "Verify the certificate: "
sudo openssl verify ${CertPath}/${CertName}.crt

echo ""
