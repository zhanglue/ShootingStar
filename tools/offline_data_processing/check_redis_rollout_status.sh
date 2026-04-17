#!/usr/bin/env bash

set -euo pipefail

# Inspect RedisReplication/Sentinel rollout state without writing Redis data.
#
# Typical scenarios:
#   1. Print a one-shot rollout snapshot:
#        ./check_redis_rollout_status.sh once
#   2. Wait until Redis and Sentinel StatefulSets are ready:
#        ./check_redis_rollout_status.sh wait
#   3. Use a longer timeout for slow image pulls or storage attachment:
#        WAIT_TIMEOUT_SECONDS=1200 ./check_redis_rollout_status.sh wait
#   4. Show more recent Kubernetes events during investigation:
#        EVENT_LINES=100 ./check_redis_rollout_status.sh once

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

MODE="${1:-once}"
REDIS_NAMESPACE="${REDIS_NAMESPACE:-recommendation-engine-redis}"
REDIS_REPLICATION_NAME="${REDIS_REPLICATION_NAME:-redis-data}"
REDIS_SENTINEL_NAME="${REDIS_SENTINEL_NAME:-redis-sentinel}"
REDIS_SENTINEL_STS_NAME="${REDIS_SENTINEL_STS_NAME:-${REDIS_SENTINEL_NAME}-sentinel}"
WAIT_TIMEOUT_SECONDS="${WAIT_TIMEOUT_SECONDS:-600}"
WAIT_INTERVAL_SECONDS="${WAIT_INTERVAL_SECONDS:-5}"
EVENT_LINES="${EVENT_LINES:-40}"

usage() {
    # This script is read-only; use check_redis_functionality.sh for live
    # write/read validation after rollout is complete.
    cat <<EOF
Usage:
  $(basename "$0") [once|wait]

Environment variables:
  REDIS_NAMESPACE          Kubernetes namespace. Default: ${REDIS_NAMESPACE}
  REDIS_REPLICATION_NAME   RedisReplication name. Default: ${REDIS_REPLICATION_NAME}
  REDIS_SENTINEL_NAME      RedisSentinel name. Default: ${REDIS_SENTINEL_NAME}
  REDIS_SENTINEL_STS_NAME  Sentinel StatefulSet name. Default: ${REDIS_SENTINEL_STS_NAME}
  WAIT_TIMEOUT_SECONDS     Wait timeout in seconds for wait mode. Default: ${WAIT_TIMEOUT_SECONDS}
  WAIT_INTERVAL_SECONDS    Poll interval in seconds for wait mode. Default: ${WAIT_INTERVAL_SECONDS}
  EVENT_LINES              Number of recent event lines to show. Default: ${EVENT_LINES}

Examples:
  $(basename "$0")
  $(basename "$0") once
  $(basename "$0") wait
EOF
}

require_command() {
    local command_name="$1"
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo_error "${command_name} is not installed or not available in PATH."
        exit 2
    fi
}

print_section() {
    local title="$1"
    echo
    echo "################################################################################"
    echo_info "${title}"
    echo "################################################################################"
}

run_kubectl() {
    kubectl "$@"
}

resource_exists() {
    # Keep jsonpath helpers quiet when optional CRDs/resources are absent.
    local kind="$1"
    local name="$2"
    run_kubectl get "${kind}" "${name}" -n "${REDIS_NAMESPACE}" >/dev/null 2>&1
}

jsonpath_or_na() {
    # Read one Kubernetes jsonpath and normalize missing/empty values to n/a.
    local kind="$1"
    local name="$2"
    local jsonpath="$3"

    if ! resource_exists "${kind}" "${name}"; then
        echo "n/a"
        return 0
    fi

    local value
    value="$(run_kubectl get "${kind}" "${name}" -n "${REDIS_NAMESPACE}" -o "jsonpath=${jsonpath}" 2>/dev/null || true)"
    if [[ -z "${value}" ]]; then
        echo "n/a"
    else
        echo "${value}"
    fi
}

show_context() {
    # Print resource names first so copied logs are self-describing.
    print_section "Cluster Context"
    echo_info "Namespace           : ${REDIS_NAMESPACE}"
    echo_info "RedisReplication    : ${REDIS_REPLICATION_NAME}"
    echo_info "RedisSentinel       : ${REDIS_SENTINEL_NAME}"
    echo_info "Sentinel StatefulSet: ${REDIS_SENTINEL_STS_NAME}"
    echo
    echo_info "kubectl current-context"
    run_kubectl config current-context || true
}

show_namespace_overview() {
    # Show the custom resources and StatefulSets that drive the rollout.
    print_section "Namespace Overview"
    run_kubectl get namespace "${REDIS_NAMESPACE}" || true

    echo
    run_kubectl get redisreplication "${REDIS_REPLICATION_NAME}" -n "${REDIS_NAMESPACE}" -o wide || true

    echo
    run_kubectl get redissentinel "${REDIS_SENTINEL_NAME}" -n "${REDIS_NAMESPACE}" -o wide || true

    echo
    run_kubectl get statefulset -n "${REDIS_NAMESPACE}" -o wide || true
}

show_status_summary() {
    # Summarize operator-observed state and StatefulSet readiness in one block.
    local redis_generation
    local redis_observed_generation
    local redis_master
    local redis_connection_host
    local redis_connection_port
    local redis_ready
    local redis_desired
    local sentinel_ready
    local sentinel_desired

    redis_generation="$(jsonpath_or_na redisreplication "${REDIS_REPLICATION_NAME}" '{.metadata.generation}')"
    redis_observed_generation="$(jsonpath_or_na redisreplication "${REDIS_REPLICATION_NAME}" '{.status.observedGeneration}')"
    redis_master="$(jsonpath_or_na redisreplication "${REDIS_REPLICATION_NAME}" '{.status.masterNode}')"
    redis_connection_host="$(jsonpath_or_na redisreplication "${REDIS_REPLICATION_NAME}" '{.status.connectionInfo.host}')"
    redis_connection_port="$(jsonpath_or_na redisreplication "${REDIS_REPLICATION_NAME}" '{.status.connectionInfo.port}')"
    redis_ready="$(jsonpath_or_na statefulset "${REDIS_REPLICATION_NAME}" '{.status.readyReplicas}')"
    redis_desired="$(jsonpath_or_na statefulset "${REDIS_REPLICATION_NAME}" '{.spec.replicas}')"
    sentinel_ready="$(jsonpath_or_na statefulset "${REDIS_SENTINEL_STS_NAME}" '{.status.readyReplicas}')"
    sentinel_desired="$(jsonpath_or_na statefulset "${REDIS_SENTINEL_STS_NAME}" '{.spec.replicas}')"

    print_section "Rollout Summary"
    echo_info "Redis generation           : ${redis_generation}"
    echo_info "Redis observed generation  : ${redis_observed_generation}"
    echo_info "Current master             : ${redis_master}"
    echo_info "Redis connection host      : ${redis_connection_host}"
    echo_info "Redis connection port      : ${redis_connection_port}"
    echo_info "Redis StatefulSet ready    : ${redis_ready}/${redis_desired}"
    echo_info "Sentinel StatefulSet ready : ${sentinel_ready}/${sentinel_desired}"
}

show_pod_layout() {
    # Pod placement and IPs help spot scheduling and node-local failures.
    print_section "Pod Layout"
    run_kubectl get pods -n "${REDIS_NAMESPACE}" -o wide || true
}

show_service_and_storage() {
    # Services tell clients where to connect; PVCs explain pending pods caused by
    # volume scheduling or attachment issues.
    print_section "Services"
    run_kubectl get svc -n "${REDIS_NAMESPACE}" -o wide || true

    echo
    echo_info "ExternalName aliases"
    for service_name in redis-write redis-read redis-sentinel; do
        if resource_exists service "${service_name}"; then
            echo "  ${service_name} -> $(jsonpath_or_na service "${service_name}" '{.spec.externalName}')"
        else
            echo "  ${service_name} -> n/a"
        fi
    done

    echo
    print_section "Persistent Volumes"
    run_kubectl get pvc -n "${REDIS_NAMESPACE}" -o wide || true
}

show_config_resources() {
    # Missing Secret/ConfigMap objects usually means the manifest set was only
    # partially applied.
    print_section "Config Resources"
    run_kubectl get secret redis-auth -n "${REDIS_NAMESPACE}" >/dev/null 2>&1 && \
        echo_info "Secret redis-auth exists." || \
        echo_warning "Secret redis-auth not found."

    run_kubectl get configmap redis-data-config -n "${REDIS_NAMESPACE}" >/dev/null 2>&1 && \
        echo_info "ConfigMap redis-data-config exists." || \
        echo_warning "ConfigMap redis-data-config not found."

    echo
    run_kubectl get configmap -n "${REDIS_NAMESPACE}" || true
}

show_not_ready_pod_details() {
    # Describe only non-ready pods to keep the happy-path output short.
    local not_ready_pods

    not_ready_pods="$(
        run_kubectl get pods -n "${REDIS_NAMESPACE}" --no-headers 2>/dev/null \
            | awk '{
                split($2, readiness, "/")
                if (readiness[1] != readiness[2]) {
                    print $1
                }
            }'
    )"

    if [[ -z "${not_ready_pods}" ]]; then
        print_section "Not-Ready Pod Details"
        echo_info "All pods are currently ready."
        return 0
    fi

    print_section "Not-Ready Pod Details"
    while IFS= read -r pod_name; do
        [[ -z "${pod_name}" ]] && continue
        echo_info "Describing pod/${pod_name}"
        run_kubectl describe pod "${pod_name}" -n "${REDIS_NAMESPACE}" || true
        echo
    done <<< "${not_ready_pods}"
}

show_recent_events() {
    # Recent events provide the timeline around scheduling, image, and probe
    # failures. tail keeps the output bounded.
    print_section "Recent Events"
    run_kubectl get events -n "${REDIS_NAMESPACE}" --sort-by=.lastTimestamp | tail -n "${EVENT_LINES}" || true
}

is_rollout_complete() {
    # Rollout is complete when both Redis and Sentinel StatefulSets report every
    # desired replica as ready.
    local redis_desired
    local redis_ready
    local sentinel_desired
    local sentinel_ready

    redis_desired="$(jsonpath_or_na statefulset "${REDIS_REPLICATION_NAME}" '{.spec.replicas}')"
    redis_ready="$(jsonpath_or_na statefulset "${REDIS_REPLICATION_NAME}" '{.status.readyReplicas}')"
    sentinel_desired="$(jsonpath_or_na statefulset "${REDIS_SENTINEL_STS_NAME}" '{.spec.replicas}')"
    sentinel_ready="$(jsonpath_or_na statefulset "${REDIS_SENTINEL_STS_NAME}" '{.status.readyReplicas}')"

    [[ "${redis_desired}" != "n/a" ]] || return 1
    [[ "${sentinel_desired}" != "n/a" ]] || return 1

    [[ "${redis_ready}" == "${redis_desired}" && "${sentinel_ready}" == "${sentinel_desired}" ]]
}

wait_for_rollout() {
    # Poll readiness until completion or timeout, then the caller prints a fresh
    # status summary so final logs include the terminal state.
    local start_ts
    local now_ts
    local elapsed
    local redis_ready
    local redis_desired
    local sentinel_ready
    local sentinel_desired

    print_section "Waiting For Redis Rollout"
    start_ts="$(date +%s)"

    while true; do
        redis_ready="$(jsonpath_or_na statefulset "${REDIS_REPLICATION_NAME}" '{.status.readyReplicas}')"
        redis_desired="$(jsonpath_or_na statefulset "${REDIS_REPLICATION_NAME}" '{.spec.replicas}')"
        sentinel_ready="$(jsonpath_or_na statefulset "${REDIS_SENTINEL_STS_NAME}" '{.status.readyReplicas}')"
        sentinel_desired="$(jsonpath_or_na statefulset "${REDIS_SENTINEL_STS_NAME}" '{.spec.replicas}')"

        echo_info "Redis ready ${redis_ready}/${redis_desired}, Sentinel ready ${sentinel_ready}/${sentinel_desired}"

        if is_rollout_complete; then
            echo_info "Redis and Sentinel rollout completed successfully."
            return 0
        fi

        now_ts="$(date +%s)"
        elapsed="$((now_ts - start_ts))"
        if (( elapsed >= WAIT_TIMEOUT_SECONDS )); then
            echo_error "Timed out waiting for rollout after ${WAIT_TIMEOUT_SECONDS}s."
            return 1
        fi

        sleep "${WAIT_INTERVAL_SECONDS}"
    done
}

main() {
    # once: read-only snapshot. wait: snapshot, poll readiness, then print the
    # latest summary/layout/events again.
    if [[ "${MODE}" == "--help" || "${MODE}" == "-h" ]]; then
        usage
        exit 0
    fi

    if [[ "${MODE}" != "once" && "${MODE}" != "wait" ]]; then
        echo_error "Unknown mode: ${MODE}"
        usage
        exit 2
    fi

    require_command kubectl
    check_kubectl_is_available

    show_context
    show_namespace_overview
    show_status_summary
    show_pod_layout
    show_service_and_storage
    show_config_resources
    show_not_ready_pod_details
    show_recent_events

    if [[ "${MODE}" == "wait" ]]; then
        wait_for_rollout
        show_status_summary
        show_pod_layout
        show_recent_events
    fi
}

main "$@"
