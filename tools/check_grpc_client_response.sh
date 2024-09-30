#!/bin/bash

# This script checks the response of a gRPC client is as expected.

pod_namespace=${1:-grpc-demo}
pod_prefix=${2:-grpc-client-demo}
expected_response=${3:-'Greeting: Hello World'}

echo "################################################################################"
echo "Pod namespace     : ${pod_namespace}"
echo "Pod prefix        : ${pod_prefix}"
echo "Expected response : ${expected_response}"
echo ""

echo "################################################################################"
pod_cnt=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | wc -l)
if [[ $pod_cnt == 0 ]]; then
    echo "No grpc-client-demo pod found."
    exit 1
elif [[ $pod_cnt != 1 ]]; then
    echo "Pod counts of grpc-client-demo is: ${pod_cnt}."
    kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g'
    exit 1
fi

pod_info=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g')
echo $pod_info
pod_status=$(echo ${pod_info} | cut -d ' ' -f 3)
echo "Pod status: ${pod_status}."
if [[ ${pod_status} != 'Completed' ]]; then
    echo "Pod status is not Completed."

    echo "Logs of the pod:"
    pod_name=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1 | tail -n 1)
    kubectl logs --namespace ${pod_namespace} ${pod_name}

    exit 2
fi

pod_name=$(echo ${pod_info} | cut -d ' ' -f 1)
actual_response=$(kubectl logs --namespace ${pod_namespace} --tail 1 ${pod_name})
echo "Expected response: ${expected_response}"
echo "Actual response: ${actual_response}"

echo ""
echo "################################################################################"
if [[ ${actual_response} != ${expected_response} ]]; then
    echo "!!!FAILED!!!"

    echo "Logs of the pod:"
    pod_name=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1 | tail -n 1)
    kubectl logs --namespace ${pod_namespace} ${pod_name}

    exit 3
fi

echo "!!!PASSED!!!"
exit 0
