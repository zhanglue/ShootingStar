#!/bin/bash

pod_namespace=${1:-weather-forecast}
pod_prefix=${2:-weather-forecast-fetcher-client}
max_retries=3
retry_interval=3

echo "################################################################################"
echo "Pod namespace     : ${pod_namespace}"
echo "Pod prefix        : ${pod_prefix}"
echo ""

echo "################################################################################"
pod_cnt=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | wc -l)
if [[ $pod_cnt == 0 ]]; then
    echo "No pod found with prefix ${pod_prefix}."
    exit 1
elif [[ $pod_cnt != 1 ]]; then
    echo "Pod counts with prefix ${pod_prefix} is: ${pod_cnt}."
    kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g'
    exit 1
fi

sleep ${retry_interval}
attempt=1
while (( attempt <= max_retries )); do
    pod_info=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g')
    echo "Attempt for ${attempt} time: $pod_info"

    pod_status=$(echo ${pod_info} | cut -d ' ' -f 3)
    echo "Pod status: ${pod_status}."

    if [[ ${pod_status} == 'Completed' ]]; then
        pod_name=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1 | tail -n 1)
        kubectl logs --namespace ${pod_namespace} ${pod_name} > ${pod_prefix}.log
        echo "Pod completed successfully."
        exit 0
    fi

    if (( attempt < max_retries )); then
        echo "Pod not completed yet, retrying in ${retry_interval} seconds..."
        sleep ${retry_interval}
    fi

    ((attempt++))
done

echo "Pod status is not Completed after ${max_retries} attempts."
echo "Logs of the pod:"
pod_name=$(kubectl get pods --namespace ${pod_namespace} 2>/dev/null | grep -- ${pod_prefix} | sed 's/[[:space:]]\+/ /g' | cut -d ' ' -f 1 | tail -n 1)
kubectl logs --namespace ${pod_namespace} ${pod_name}

exit 2
