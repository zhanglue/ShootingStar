#!/bin/bash

# This script will clean up all the kubernetes resources created by the github actions.

pod_namespace_prefix=${1:-grpc-demo}

count_of_namespaces=$(kubectl get namespaces | grep ^${pod_namespace_prefix} | wc -l)
if (( count_of_namespaces == 0 )); then
    echo "No namespace found with prefix: ${pod_namespace_prefix}."
    exit 0
fi

echo "################################################################################"
echo "All kubernetes namespace to delete:"
kubectl get namespaces | grep ^${pod_namespace_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1
echo ""

echo -n "Are you sure to delete all the above namespaces? (y/N)"
read -r confirm
if [[ ! ${confirm} =~ ^[Yy]$ ]]; then
    echo "Exit without deleting."
    exit 0
fi

echo ""
echo "################################################################################"
echo "Deleting namespaces:"
kubectl get namespaces | grep ^${pod_namespace_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1 | while read namespace_name
do
    kubectl delete namespace ${namespace_name}
done

echo ""
echo "Done."
