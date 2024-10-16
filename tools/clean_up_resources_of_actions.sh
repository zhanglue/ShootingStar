#!/bin/bash

# This script will clean up all the docker image and kubernetes resources created by the github actions.

REPO_ROOT_PATH=$(git rev-parse --show-toplevel)
. ${REPO_ROOT_PATH}/tools/common.sh

if [[ $* =~ "--show-account" ]]; then
    # check azure login status
    echo_info "Please make sure the azure login status is correct."
    az account show
    echo -e "${COLOR_YELLOW}"
    read -p "Is the azure login status correct? (y/N)" confirm
    echo -e "${COLOR_END}"
    if [[ ! ${confirm} =~ ^[Yy]$ ]]; then
        echo_error "Please login to azure first."
        exit 1
    fi
fi

# clean up docker images
az acr repository show-tags --name shootingstar --repository grpc-service-demo --output table > /dev/null 2>&1
if [[ $? == 0 ]]; then
    echo_info "All docker images to delete:"
    az acr repository show-tags --name shootingstar --repository grpc-service-demo --output table | tail -n +3 | awk '{print $1}' | grep "^pr-" | while read tag
    do
        echo_warning "grpc-service-demo:${tag}"
        az acr repository delete --name shootingstar --image grpc-service-demo:${tag} --yes

        echo_warning "grpc-client-demo:${tag}"
        az acr repository delete --name shootingstar --image grpc-client-demo:${tag} --yes
    done
fi

# clean up kubernetes resources
pod_namespace_prefix=${1:-grpc-demo}

count_of_namespaces=$(kubectl get namespaces | grep ^${pod_namespace_prefix} | wc -l)
if (( count_of_namespaces == 0 )); then
    echo_error "No namespace found with prefix: ${pod_namespace_prefix}."
    exit 0
fi

echo "################################################################################"
echo_info "All kubernetes namespace to delete:"
kubectl get namespaces | grep ^${pod_namespace_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1
echo ""

echo -n "Are you sure to delete all the above namespaces? (y/N)"
read -r confirm
if [[ ! ${confirm} =~ ^[Yy]$ ]]; then
    echo_info "Exit without deleting."
    exit 0
fi

echo ""
echo "################################################################################"
echo_info "Deleting namespaces:"
kubectl get namespaces | grep ^${pod_namespace_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1 | while read namespace_name
do
    echo_warning "kubectl delete namespace ${namespace_name}"
    kubectl delete namespace ${namespace_name}
done

echo ""
echo_info "Done."
