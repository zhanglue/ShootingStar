#!/bin/bash

# Generate a self-signed certificate for the server to use.

CertDir="certs"
CertName="localhost"

while (( $# ))
do
    case $1 in
        -p | --cert-path )
            shift
            CertDir=$1
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

if [[ -z $CertDir ]]; then
    echo "Error: CertDir is empty."
    exit 1
fi

if [[ ${CertDir} == ${HOME} ]]; then
    echo "Error: CertDir is \$HOME: ${HOME}"
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
echo "Generate a self-signed certificate for ${CertName} in ${CertDir}."
echo -e "\n################################################################################"

[[ -e $CertDir ]] && rm -rf $CertDir
mkdir -p $CertDir

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
""" > "${CertDir}/${CertName}.conf"

openssl req \
    -x509 -days 3650 -newkey rsa:2048 -subj "/CN=localhost" \
    -passout env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -config "${CertDir}/${CertName}.conf" \
    -keyout "${CertDir}/${CertName}.key" \
    -out "${CertDir}/${CertName}.crt"

openssl pkcs12 \
    -passin env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -inkey "${CertDir}/${CertName}.key" \
    -in "${CertDir}/${CertName}.crt" \
    -passout env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -export -out "${CertDir}/${CertName}.pfx"

[[ -e '/usr/local/share/ca-certificates' ]] || sudo mkdir -p '/usr/local/share/ca-certificates'
[[ -e "/usr/local/share/ca-certificates/${CertName}.crt" ]] && sudo rm -f "/usr/local/share/ca-certificates/${CertName}.crt"
[[ -e "/etc/ssl/certs/${CertName}.pem" ]] && sudo rm -f "/etc/ssl/certs/${CertName}.pem"

sudo cp "${CertDir}/${CertName}.crt" '/usr/local/share/ca-certificates'
sudo update-ca-certificates

echo -e "\n################################################################################"
echo -n "Verify the certificate: "
sudo openssl verify ${CertDir}/${CertName}.crt

echo ""
