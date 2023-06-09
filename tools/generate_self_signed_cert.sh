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
echo "################################################################################"

[[ -e $CertPath ]] && rm -rf $CertPath
mkdir -p $CertPath

openssl req -x509 \
    -newkey rsa:4096 -sha256 -days 3650 \
    -subj "/CN=localhost" -extensions v3_ca -extensions v3_req -passin env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -keyout "${CertPath}/${CertName}.key" -out "${CertPath}/${CertName}.crt" -passout env:SELF_SIGNED_HTTPS_CERT_PASSWORD

openssl pkcs12 \
    -inkey "${CertPath}/${CertName}.key" -in "${CertPath}/${CertName}.crt" -passin env:SELF_SIGNED_HTTPS_CERT_PASSWORD \
    -export -out "${CertPath}/${CertName}.pfx" -passout env:SELF_SIGNED_HTTPS_CERT_PASSWORD
    