#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

MODE="${1:-both}"
ES_NAMESPACE="${ES_NAMESPACE:-recommendation-engine-es}"
ES_CLUSTER_NAME="${ES_CLUSTER_NAME:-item-index}"
ES_SERVICE_NAME="${ES_SERVICE_NAME:-item-index-http}"
ES_LOCAL_PORT="${ES_LOCAL_PORT:-19200}"
ES_REMOTE_PORT="${ES_REMOTE_PORT:-9200}"
PORT_FORWARD_LOG="${PORT_FORWARD_LOG:-/tmp/${ES_CLUSTER_NAME}-port-forward.log}"
WAIT_TIMEOUT_SECONDS="${WAIT_TIMEOUT_SECONDS:-900}"
WAIT_INTERVAL_SECONDS="${WAIT_INTERVAL_SECONDS:-10}"

PORT_FORWARD_PID=""

usage() {
    cat <<EOF
Usage:
  $(basename "$0") [before|after|both]

Environment variables:
  ES_USERNAME         Elasticsearch username. Required.
  ES_PASSWORD         Elasticsearch password. Required.
  ES_NAMESPACE        Kubernetes namespace. Default: ${ES_NAMESPACE}
  ES_CLUSTER_NAME     ECK Elasticsearch resource name. Default: ${ES_CLUSTER_NAME}
  ES_SERVICE_NAME     Elasticsearch HTTP service name. Default: ${ES_SERVICE_NAME}
  ES_LOCAL_PORT       Local port for kubectl port-forward. Default: ${ES_LOCAL_PORT}
  ES_REMOTE_PORT      Remote Elasticsearch port. Default: ${ES_REMOTE_PORT}

Examples:
  ES_USERNAME=elastic ES_PASSWORD=secret $(basename "$0") before
  ES_USERNAME=elastic ES_PASSWORD=secret $(basename "$0") after
EOF
}

cleanup() {
    if [[ -n "${PORT_FORWARD_PID}" ]] && kill -0 "${PORT_FORWARD_PID}" >/dev/null 2>&1; then
        kill "${PORT_FORWARD_PID}" >/dev/null 2>&1 || true
        wait "${PORT_FORWARD_PID}" >/dev/null 2>&1 || true
    fi
}

trap cleanup EXIT

require_command() {
    local command_name="$1"
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo_error "${command_name} is not installed or not available in PATH."
        exit 2
    fi
}

require_env() {
    local env_name="$1"
    if [[ -z "${!env_name:-}" ]]; then
        echo_error "${env_name} is not set."
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

get_es_resource_json() {
    run_kubectl get elasticsearch "${ES_CLUSTER_NAME}" -n "${ES_NAMESPACE}" -o json
}

run_curl() {
    curl --silent --show-error --fail \
        --connect-timeout 3 \
        --max-time 10 \
        --user "${ES_USERNAME}:${ES_PASSWORD}" \
        --insecure \
        "$@"
}

probe_es_endpoint() {
    curl --silent \
        --output /dev/null \
        --connect-timeout 2 \
        --max-time 5 \
        --insecure \
        "https://127.0.0.1:${ES_LOCAL_PORT}/"
}

start_port_forward() {
    : > "${PORT_FORWARD_LOG}"

    echo_info "Starting port-forward to service/${ES_SERVICE_NAME} on localhost:${ES_LOCAL_PORT}..."
    run_kubectl -n "${ES_NAMESPACE}" port-forward \
        "service/${ES_SERVICE_NAME}" \
        "${ES_LOCAL_PORT}:${ES_REMOTE_PORT}" \
        > "${PORT_FORWARD_LOG}" 2>&1 &
    PORT_FORWARD_PID=$!

    local attempt
    for attempt in $(seq 1 15); do
        if probe_es_endpoint >/dev/null 2>&1; then
            echo_info "Port-forward is ready."
            return 0
        fi

        if ! kill -0 "${PORT_FORWARD_PID}" >/dev/null 2>&1; then
            echo_error "kubectl port-forward exited unexpectedly."
            sed -n '1,120p' "${PORT_FORWARD_LOG}" || true
            exit 3
        fi

        sleep 1
    done

    echo_error "Timed out waiting for Elasticsearch port-forward."
    sed -n '1,120p' "${PORT_FORWARD_LOG}" || true
    exit 3
}

show_curl_failure_hint() {
    echo_warning "Elasticsearch endpoint is reachable, but the authenticated API call failed."
    echo_warning "Please verify ES_USERNAME / ES_PASSWORD and whether the service requires different auth settings."
    echo_info "A quick manual check:"
    echo "curl -k -u \"\$ES_USERNAME:\$ES_PASSWORD\" https://127.0.0.1:${ES_LOCAL_PORT}/_cluster/health?pretty"
}

show_k8s_overview() {
    local label="$1"

    print_section "Kubernetes Overview (${label})"
    echo_info "Namespace: ${ES_NAMESPACE}"
    echo_info "Cluster  : ${ES_CLUSTER_NAME}"
    echo_info "Service  : ${ES_SERVICE_NAME}"

    echo
    run_kubectl get elasticsearch "${ES_CLUSTER_NAME}" -n "${ES_NAMESPACE}" || true

    echo
    run_kubectl get pods -n "${ES_NAMESPACE}" -o wide || true

    echo
    run_kubectl get pvc -n "${ES_NAMESPACE}" || true
}

show_top_metrics() {
    local label="$1"

    print_section "Resource Usage (${label})"
    if ! run_kubectl top nodes; then
        echo_warning "kubectl top nodes failed. metrics-server may not be installed."
    fi

    echo
    if ! run_kubectl top pods -n "${ES_NAMESPACE}"; then
        echo_warning "kubectl top pods failed. metrics-server may not be installed."
    fi
}

show_recent_events() {
    local label="$1"

    print_section "Recent Events (${label})"
    if ! run_kubectl get events -n "${ES_NAMESPACE}" --sort-by=.lastTimestamp; then
        echo_warning "Failed to read recent events in namespace ${ES_NAMESPACE}."
    fi
}

show_es_operator_status() {
    local label="$1"
    local es_json

    print_section "ECK Status (${label})"
    es_json="$(get_es_resource_json)"

    echo_info "Summary"
    echo "${es_json}" | jq -r '
        [
            "generation=" + (.metadata.generation | tostring),
            "observedGeneration=" + ((.status.observedGeneration // "null") | tostring),
            "phase=" + (.status.phase // "unknown"),
            "health=" + (.status.health // "unknown"),
            "availableNodes=" + ((.status.availableNodes // "null") | tostring),
            "readyNodes=" + ((.status.readyNodes // "null") | tostring),
            "desiredNodes=" + (([.spec.nodeSets[].count] | add) | tostring)
        ] | join(", ")
    '

    echo
    echo_info "Conditions"
    echo "${es_json}" | jq -r '
        (.status.conditions // [])
        | if length == 0 then
            "No conditions reported."
          else
            .[] | "* " + .type + "=" + .status + ": " + (.message // "")
          end
    '

    echo
    echo_info "In-progress operations"
    echo "${es_json}" | jq -r '
        (.status.inProgressOperations // {})
        | if length == 0 then
            "No in-progress operations reported."
          else
            to_entries[]
            | "* " + .key + ": " + (
                if (.value.nodes // [] | length) > 0 then
                    (.value.nodes | map(.name + "=" + .status + " (" + (.predicate // "n/a") + ")") | join(", "))
                else
                    "lastUpdatedTime=" + (.value.lastUpdatedTime // "unknown")
                end
              )
          end
    '
}

wait_for_es_reconciliation() {
    local start_ts
    local now_ts
    local elapsed
    local summary
    local generation
    local observed_generation
    local phase
    local health
    local desired_nodes
    local available_nodes
    local reconciliation_complete
    local resources_aware_management
    local running_desired_version
    local blocking_conditions

    print_section "Waiting For ECK Reconciliation"
    start_ts="$(date +%s)"

    while true; do
        summary="$(
            get_es_resource_json | jq -r '
                {
                    generation: (.metadata.generation // 0),
                    observedGeneration: (.status.observedGeneration // 0),
                    phase: (.status.phase // "unknown"),
                    health: (.status.health // "unknown"),
                    desiredNodes: ([.spec.nodeSets[].count] | add),
                    availableNodes: (.status.availableNodes // 0),
                    reconciliationComplete: (((.status.conditions // []) | map(select(.type == "ReconciliationComplete")) | .[0].status) // "Unknown"),
                    resourcesAwareManagement: (((.status.conditions // []) | map(select(.type == "ResourcesAwareManagement")) | .[0].status) // "Unknown"),
                    runningDesiredVersion: (((.status.conditions // []) | map(select(.type == "RunningDesiredVersion")) | .[0].status) // "Unknown"),
                    blockingConditions: (
                        (.status.conditions // [])
                        | map(select(.status != "True"))
                        | map(.type + "=" + .status + ": " + (.message // ""))
                        | join(" | ")
                    )
                }
            '
        )"

        generation="$(echo "${summary}" | jq -r '.generation')"
        observed_generation="$(echo "${summary}" | jq -r '.observedGeneration')"
        phase="$(echo "${summary}" | jq -r '.phase')"
        health="$(echo "${summary}" | jq -r '.health')"
        desired_nodes="$(echo "${summary}" | jq -r '.desiredNodes')"
        available_nodes="$(echo "${summary}" | jq -r '.availableNodes')"
        reconciliation_complete="$(echo "${summary}" | jq -r '.reconciliationComplete')"
        resources_aware_management="$(echo "${summary}" | jq -r '.resourcesAwareManagement')"
        running_desired_version="$(echo "${summary}" | jq -r '.runningDesiredVersion')"
        blocking_conditions="$(echo "${summary}" | jq -r '.blockingConditions')"

        echo_info "generation=${generation}, observed=${observed_generation}, phase=${phase}, health=${health}, availableNodes=${available_nodes}/${desired_nodes}, ReconciliationComplete=${reconciliation_complete}, ResourcesAwareManagement=${resources_aware_management}, RunningDesiredVersion=${running_desired_version}"

        if [[ -n "${blocking_conditions}" ]]; then
            echo_warning "Current conditions: ${blocking_conditions}"
        fi

        if [[ "${observed_generation}" == "${generation}" ]] \
            && [[ "${phase}" == "Ready" ]] \
            && [[ "${available_nodes}" == "${desired_nodes}" ]] \
            && [[ "${reconciliation_complete}" == "True" ]]; then
            echo_info "ECK reconciliation looks complete."
            return 0
        fi

        now_ts="$(date +%s)"
        elapsed="$(( now_ts - start_ts ))"
        if (( elapsed >= WAIT_TIMEOUT_SECONDS )); then
            echo_warning "Timed out after ${WAIT_TIMEOUT_SECONDS}s waiting for ECK reconciliation. Continuing with current stats."
            return 1
        fi

        sleep "${WAIT_INTERVAL_SECONDS}"
    done
}

show_es_endpoints() {
    local label="$1"
    local cluster_health
    local cluster_status
    local unassigned_shards

    print_section "Elasticsearch APIs (${label})"
    start_port_forward

    echo_info "GET /_cluster/health?pretty"
    if ! cluster_health="$(run_curl "https://127.0.0.1:${ES_LOCAL_PORT}/_cluster/health?pretty")"; then
        show_curl_failure_hint
        exit 4
    fi
    echo "${cluster_health}"

    cluster_status="$(echo "${cluster_health}" | sed -n 's/.*"status"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
    unassigned_shards="$(echo "${cluster_health}" | sed -n 's/.*"unassigned_shards"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -n 1)"

    echo
    echo_info "GET /_cat/nodes?v&h=name,heap.percent,ram.percent,cpu,load_1m,node.role,master"
    if ! run_curl "https://127.0.0.1:${ES_LOCAL_PORT}/_cat/nodes?v&h=name,heap.percent,ram.percent,cpu,load_1m,node.role,master"; then
        show_curl_failure_hint
        exit 4
    fi

    echo
    echo_info "GET /_cat/indices?v&s=store.size:desc"
    if ! run_curl "https://127.0.0.1:${ES_LOCAL_PORT}/_cat/indices?v&s=store.size:desc"; then
        show_curl_failure_hint
        exit 4
    fi

    echo
    echo_info "GET /_cat/shards?v&s=index,shard,prirep"
    if ! run_curl "https://127.0.0.1:${ES_LOCAL_PORT}/_cat/shards?v&s=index,shard,prirep"; then
        show_curl_failure_hint
        exit 4
    fi

    echo
    if [[ "${cluster_status}" == "green" ]]; then
        echo_info "Cluster health is green."
    else
        echo_warning "Cluster health is ${cluster_status:-unknown}."
    fi

    if [[ -n "${unassigned_shards}" && "${unassigned_shards}" != "0" ]]; then
        echo_warning "Unassigned shards: ${unassigned_shards}"
    fi
}

main() {
    case "${MODE}" in
        before|after|both)
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac

    check_kubectl_is_available
    require_command curl
    require_command jq
    require_env ES_USERNAME
    require_env ES_PASSWORD

    if [[ "${MODE}" == "before" || "${MODE}" == "both" ]]; then
        show_k8s_overview "Before Apply"
        show_top_metrics "Before Apply"
        show_es_operator_status "Before Apply"
        show_es_endpoints "Before Apply"
    fi

    if [[ "${MODE}" == "both" ]]; then
        echo
        echo_warning "Apply your updated manifest in another terminal, then press Enter to continue with post-checks."
        read -r _
        cleanup
    fi

    if [[ "${MODE}" == "after" || "${MODE}" == "both" ]]; then
        wait_for_es_reconciliation || true
        show_k8s_overview "After Apply"
        show_top_metrics "After Apply"
        show_es_operator_status "After Apply"
        show_recent_events "After Apply"
        show_es_endpoints "After Apply"
    fi
}

main "$@"
