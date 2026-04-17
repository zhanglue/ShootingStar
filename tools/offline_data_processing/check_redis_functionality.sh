#!/usr/bin/env bash

set -euo pipefail

# Validate Redis service aliases, replication visibility, and Sentinel state.
#
# Typical scenarios:
#   1. Validate the Raspberry Pi HA deployment with RedisReplication/Sentinel:
#        ./check_redis_functionality.sh ha
#   2. Validate the single-node local Kubernetes manifest:
#        ./check_redis_functionality.sh local
#   3. Keep the temporary validation key for manual redis-cli inspection:
#        KEEP_TEST_KEY=true ./check_redis_functionality.sh ha
#   4. Tune replica-read retry timing for a slower cluster:
#        READ_RETRY_COUNT=20 READ_RETRY_INTERVAL_SECONDS=2 ./check_redis_functionality.sh ha

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../common.sh"

MODE="${1:-ha}"
REDIS_NAMESPACE="${REDIS_NAMESPACE:-recommendation-engine-redis}"
REDIS_REPLICATION_NAME="${REDIS_REPLICATION_NAME:-redis-data}"
REDIS_SENTINEL_NAME="${REDIS_SENTINEL_NAME:-redis-sentinel}"
REDIS_WRITE_SERVICE="${REDIS_WRITE_SERVICE:-redis-write}"
REDIS_READ_SERVICE="${REDIS_READ_SERVICE:-redis-read}"
REDIS_SENTINEL_SERVICE="${REDIS_SENTINEL_SERVICE:-redis-sentinel}"
REDIS_PORT="${REDIS_PORT:-6379}"
REDIS_SENTINEL_PORT="${REDIS_SENTINEL_PORT:-26379}"
REDIS_MASTER_GROUP_NAME="${REDIS_MASTER_GROUP_NAME:-myMaster}"
REDIS_CONTAINER_NAME="${REDIS_CONTAINER_NAME:-redis-data}"
REDIS_SENTINEL_CONTAINER_NAME="${REDIS_SENTINEL_CONTAINER_NAME:-redis-sentinel-sentinel}"
READ_RETRY_COUNT="${READ_RETRY_COUNT:-10}"
READ_RETRY_INTERVAL_SECONDS="${READ_RETRY_INTERVAL_SECONDS:-1}"
KEEP_TEST_KEY="${KEEP_TEST_KEY:-false}"
REDIS_SENTINEL_ENABLED="${REDIS_SENTINEL_ENABLED:-true}"

REDIS_PASSWORD=""
EXEC_POD=""
SENTINEL_EXEC_POD=""
TEST_KEY=""
TEST_VALUE=""

usage() {
    # The help text lists the operational knobs; the header comments above show
    # the most common ways to combine them.
    cat <<EOF
Usage:
  $(basename "$0") [ha|local]

Environment variables:
  REDIS_NAMESPACE                 Kubernetes namespace. Default: ${REDIS_NAMESPACE}
  REDIS_REPLICATION_NAME          RedisReplication name. Default: ${REDIS_REPLICATION_NAME}
  REDIS_SENTINEL_NAME             RedisSentinel custom resource name. Default: ${REDIS_SENTINEL_NAME}
  REDIS_WRITE_SERVICE             Write alias service. Default: ${REDIS_WRITE_SERVICE}
  REDIS_READ_SERVICE              Read alias service. Default: ${REDIS_READ_SERVICE}
  REDIS_SENTINEL_SERVICE          Sentinel alias service. Default: ${REDIS_SENTINEL_SERVICE}
  REDIS_PORT                      Redis service port. Default: ${REDIS_PORT}
  REDIS_SENTINEL_PORT             Sentinel service port. Default: ${REDIS_SENTINEL_PORT}
  REDIS_MASTER_GROUP_NAME         Sentinel master group name. Default: ${REDIS_MASTER_GROUP_NAME}
  REDIS_CONTAINER_NAME            Redis container name in the pod. Default: ${REDIS_CONTAINER_NAME}
  REDIS_SENTINEL_CONTAINER_NAME   Sentinel container name in the pod. Default: ${REDIS_SENTINEL_CONTAINER_NAME}
  READ_RETRY_COUNT                Replica read retry count. Default: ${READ_RETRY_COUNT}
  READ_RETRY_INTERVAL_SECONDS     Replica read retry interval. Default: ${READ_RETRY_INTERVAL_SECONDS}
  KEEP_TEST_KEY                   Keep the temporary validation key. Default: ${KEEP_TEST_KEY}
  REDIS_SENTINEL_ENABLED          Force-enable or disable Sentinel checks. Default: ${REDIS_SENTINEL_ENABLED}

Example:
  $(basename "$0")
  $(basename "$0") ha
  $(basename "$0") local
  KEEP_TEST_KEY=true $(basename "$0") local
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

configure_mode() {
    # The local manifest has a single Redis pod and no Sentinel, while HA mode
    # expects the operator-managed replication and Sentinel resources.
    case "${MODE}" in
        ha)
            ;;
        local)
            REDIS_SENTINEL_ENABLED="false"
            REDIS_CONTAINER_NAME="redis"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo_error "Unknown mode: ${MODE}"
            usage
            exit 2
            ;;
    esac
}

get_secret_password() {
    # Redis auth is stored in the same Secret name for local and HA manifests.
    run_kubectl get secret redis-auth -n "${REDIS_NAMESPACE}" -o go-template='{{.data.password | base64decode}}'
}

get_running_pod_by_prefix() {
    # Pick one running pod as the kubectl exec anchor for redis-cli commands.
    local prefix="$1"
    run_kubectl get pods -n "${REDIS_NAMESPACE}" --no-headers \
        | awk -v prefix="${prefix}" '$1 ~ ("^" prefix) && $3 == "Running" {print $1; exit}'
}

redis_exec() {
    # Execute redis-cli inside a Redis pod while targeting a service alias.
    local host="$1"
    shift
    run_kubectl exec -n "${REDIS_NAMESPACE}" "${EXEC_POD}" -c "${REDIS_CONTAINER_NAME}" -- \
        redis-cli -h "${host}" -p "${REDIS_PORT}" -a "${REDIS_PASSWORD}" --no-auth-warning "$@"
}

redis_local_exec() {
    # Execute a command directly inside a named Redis pod.
    local pod_name="$1"
    shift
    run_kubectl exec -n "${REDIS_NAMESPACE}" "${pod_name}" -c "${REDIS_CONTAINER_NAME}" -- "$@"
}

redis_exec_raw() {
    # Escape hatch for commands that should not go through redis-cli wrappers.
    run_kubectl exec -n "${REDIS_NAMESPACE}" "${EXEC_POD}" -c "${REDIS_CONTAINER_NAME}" -- "$@"
}

sentinel_exec() {
    # Execute redis-cli against the Sentinel service from a Sentinel pod.
    shift 0
    run_kubectl exec -n "${REDIS_NAMESPACE}" "${SENTINEL_EXEC_POD}" -c "${REDIS_SENTINEL_CONTAINER_NAME}" -- \
        redis-cli -h "${REDIS_SENTINEL_SERVICE}" -p "${REDIS_SENTINEL_PORT}" -a "${REDIS_PASSWORD}" --no-auth-warning "$@"
}

cleanup() {
    # Remove the validation key unless the caller intentionally kept it.
    if [[ -n "${TEST_KEY}" && "${KEEP_TEST_KEY}" != "true" && -n "${EXEC_POD}" ]]; then
        redis_exec "${REDIS_WRITE_SERVICE}" DEL "${TEST_KEY}" >/dev/null 2>&1 || true
    fi
}

trap cleanup EXIT

show_context() {
    # Print the exact cluster resources this validation run is about to touch.
    print_section "Cluster Context"
    echo_info "Mode                 : ${MODE}"
    echo_info "Namespace            : ${REDIS_NAMESPACE}"
    echo_info "RedisReplication     : ${REDIS_REPLICATION_NAME}"
    echo_info "RedisSentinel        : ${REDIS_SENTINEL_NAME}"
    echo_info "Redis write service  : ${REDIS_WRITE_SERVICE}"
    echo_info "Redis read service   : ${REDIS_READ_SERVICE}"
    echo_info "Redis sentinel svc   : ${REDIS_SENTINEL_SERVICE}"
    echo_info "Sentinel master group: ${REDIS_MASTER_GROUP_NAME}"
    echo
    echo_info "kubectl current-context"
    run_kubectl config current-context
}

load_runtime_context() {
    # Resolve credentials and exec pods once so later checks fail consistently if
    # the deployment is not ready.
    REDIS_PASSWORD="$(get_secret_password)"
    EXEC_POD="$(get_running_pod_by_prefix "${REDIS_REPLICATION_NAME}-")"

    if [[ "${REDIS_SENTINEL_ENABLED}" == "true" ]]; then
        SENTINEL_EXEC_POD="$(get_running_pod_by_prefix "${REDIS_SENTINEL_NAME}-sentinel-")"
    fi

    if [[ -z "${REDIS_PASSWORD}" ]]; then
        echo_error "Failed to read redis-auth password from namespace ${REDIS_NAMESPACE}."
        exit 3
    fi

    if [[ -z "${EXEC_POD}" ]]; then
        echo_error "Could not find a running Redis pod with prefix ${REDIS_REPLICATION_NAME}-."
        exit 3
    fi

    if [[ "${REDIS_SENTINEL_ENABLED}" == "true" && -z "${SENTINEL_EXEC_POD}" ]]; then
        echo_error "Could not find a running Sentinel pod with prefix ${REDIS_SENTINEL_NAME}-sentinel-."
        exit 3
    fi
}

show_overview() {
    # Show CR and pod inventory before testing traffic paths.
    print_section "Overview"
    echo_info "Exec pod             : ${EXEC_POD}"
    if [[ "${REDIS_SENTINEL_ENABLED}" == "true" ]]; then
        echo_info "Sentinel exec pod    : ${SENTINEL_EXEC_POD}"
    else
        echo_info "Sentinel exec pod    : <disabled in local mode>"
    fi
    echo_info "Password source      : secret/redis-auth"

    echo
    if [[ "${MODE}" == "ha" ]]; then
        run_kubectl get redisreplication "${REDIS_REPLICATION_NAME}" -n "${REDIS_NAMESPACE}" -o wide || true
        echo
        run_kubectl get redissentinel "${REDIS_SENTINEL_NAME}" -n "${REDIS_NAMESPACE}" -o wide || true
    else
        echo_info "Redis custom resource checks skipped in local mode."
    fi

    echo
    run_kubectl get pods -n "${REDIS_NAMESPACE}" -o wide || true
}

show_service_overview() {
    # Confirm the client-facing aliases point at the expected generated services.
    local service_name
    local service_type
    local external_name
    local cluster_ip

    print_section "Service Overview"
    run_kubectl get svc -n "${REDIS_NAMESPACE}" -o wide || true

    echo
    echo_info "Client-facing service targets"
    for service_name in "${REDIS_WRITE_SERVICE}" "${REDIS_READ_SERVICE}"; do
        service_type="$(run_kubectl get svc "${service_name}" -n "${REDIS_NAMESPACE}" -o jsonpath='{.spec.type}' 2>/dev/null || echo 'n/a')"
        if [[ "${service_type}" == "ExternalName" ]]; then
            external_name="$(run_kubectl get svc "${service_name}" -n "${REDIS_NAMESPACE}" -o jsonpath='{.spec.externalName}' 2>/dev/null || echo 'n/a')"
            echo "  ${service_name} -> ${external_name} (ExternalName)"
        else
            cluster_ip="$(run_kubectl get svc "${service_name}" -n "${REDIS_NAMESPACE}" -o jsonpath='{.spec.clusterIP}' 2>/dev/null || echo 'n/a')"
            echo "  ${service_name} -> ClusterIP ${cluster_ip}"
        fi
    done
    if [[ "${REDIS_SENTINEL_ENABLED}" == "true" ]]; then
        service_type="$(run_kubectl get svc "${REDIS_SENTINEL_SERVICE}" -n "${REDIS_NAMESPACE}" -o jsonpath='{.spec.type}' 2>/dev/null || echo 'n/a')"
        if [[ "${service_type}" == "ExternalName" ]]; then
            external_name="$(run_kubectl get svc "${REDIS_SENTINEL_SERVICE}" -n "${REDIS_NAMESPACE}" -o jsonpath='{.spec.externalName}' 2>/dev/null || echo 'n/a')"
            echo "  ${REDIS_SENTINEL_SERVICE} -> ${external_name} (ExternalName)"
        else
            cluster_ip="$(run_kubectl get svc "${REDIS_SENTINEL_SERVICE}" -n "${REDIS_NAMESPACE}" -o jsonpath='{.spec.clusterIP}' 2>/dev/null || echo 'n/a')"
            echo "  ${REDIS_SENTINEL_SERVICE} -> ClusterIP ${cluster_ip}"
        fi
    fi
}

show_replication_roles() {
    # Read INFO replication from every Redis pod so master/replica roles are
    # visible even if the operator status is stale.
    local redis_pods
    local pod_name
    local role_line

    print_section "Replication Roles"
    redis_pods="$(run_kubectl get pods -n "${REDIS_NAMESPACE}" --no-headers | awk '$1 ~ /^'"${REDIS_REPLICATION_NAME}"'-/ {print $1}')"

    while IFS= read -r pod_name; do
        [[ -z "${pod_name}" ]] && continue
        role_line="$(run_kubectl exec -n "${REDIS_NAMESPACE}" "${pod_name}" -c "${REDIS_CONTAINER_NAME}" -- sh -lc \
            "redis-cli -h 127.0.0.1 -p ${REDIS_PORT} -a '${REDIS_PASSWORD}' --no-auth-warning INFO replication | grep '^role:'" || true)"
        echo_info "${pod_name}"
        if [[ -n "${role_line}" ]]; then
            echo "  ${role_line}"
        else
            echo "  role: <unable to determine>"
        fi
    done <<< "${redis_pods}"
}

check_alias_connectivity() {
    # PING all service aliases before attempting a write/read validation.
    print_section "Alias Connectivity"

    echo_info "Pinging write alias ${REDIS_WRITE_SERVICE}"
    redis_exec "${REDIS_WRITE_SERVICE}" PING

    echo
    echo_info "Pinging read alias ${REDIS_READ_SERVICE}"
    redis_exec "${REDIS_READ_SERVICE}" PING

    if [[ "${REDIS_SENTINEL_ENABLED}" == "true" ]]; then
        echo
        echo_info "Pinging sentinel alias ${REDIS_SENTINEL_SERVICE}"
        sentinel_exec PING
    fi
}

run_write_read_validation() {
    # Write through the write alias and read through the read alias. This catches
    # broken ExternalName targets and replica lag beyond the configured retries.
    local read_result=""
    local attempt

    TEST_KEY="redis-validation:$(date +%s)"
    TEST_VALUE="ok-$(date +%s)"

    print_section "Write / Read Validation"
    echo_info "Writing temporary key through ${REDIS_WRITE_SERVICE}"
    echo "  key   = ${TEST_KEY}"
    echo "  value = ${TEST_VALUE}"
    redis_exec "${REDIS_WRITE_SERVICE}" SET "${TEST_KEY}" "${TEST_VALUE}"

    echo
    echo_info "Reading the temporary key back through ${REDIS_READ_SERVICE}"
    for attempt in $(seq 1 "${READ_RETRY_COUNT}"); do
        read_result="$(redis_exec "${REDIS_READ_SERVICE}" GET "${TEST_KEY}" 2>/dev/null || true)"
        if [[ "${read_result}" == "${TEST_VALUE}" ]]; then
            echo "  attempt ${attempt}/${READ_RETRY_COUNT}: success"
            break
        fi

        echo "  attempt ${attempt}/${READ_RETRY_COUNT}: not visible yet"
        sleep "${READ_RETRY_INTERVAL_SECONDS}"
    done

    if [[ "${read_result}" != "${TEST_VALUE}" ]]; then
        echo_error "Replica read check failed. Expected '${TEST_VALUE}', got '${read_result:-<empty>}'"
        exit 4
    fi

    echo
    echo_info "Reading the temporary key directly from each Redis pod"
    while IFS= read -r pod_name; do
        [[ -z "${pod_name}" ]] && continue
        echo "  ${pod_name}: $(redis_local_exec "${pod_name}" redis-cli -h 127.0.0.1 -p "${REDIS_PORT}" -a "${REDIS_PASSWORD}" --no-auth-warning GET "${TEST_KEY}" || true)"
    done < <(run_kubectl get pods -n "${REDIS_NAMESPACE}" --no-headers | awk '$1 ~ /^'"${REDIS_REPLICATION_NAME}"'-/ {print $1}')

    if [[ "${KEEP_TEST_KEY}" == "true" ]]; then
        echo
        echo_warning "KEEP_TEST_KEY=true, so the temporary key was left in Redis."
    else
        echo
        echo_info "Cleaning up the temporary key"
        redis_exec "${REDIS_WRITE_SERVICE}" DEL "${TEST_KEY}"
        TEST_KEY=""
    fi
}

show_sentinel_state() {
    # Compare Sentinel's master view with the RedisReplication status.
    local sentinel_master_reply
    local expected_master

    if [[ "${REDIS_SENTINEL_ENABLED}" != "true" ]]; then
        print_section "Sentinel Validation"
        echo_info "Sentinel validation skipped in local mode."
        return 0
    fi

    print_section "Sentinel Validation"
    expected_master="$(run_kubectl get redisreplication "${REDIS_REPLICATION_NAME}" -n "${REDIS_NAMESPACE}" -o jsonpath='{.status.masterNode}' 2>/dev/null || true)"

    echo_info "Sentinel view of master group ${REDIS_MASTER_GROUP_NAME}"
    sentinel_master_reply="$(sentinel_exec SENTINEL get-master-addr-by-name "${REDIS_MASTER_GROUP_NAME}" | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g')"
    echo "  ${sentinel_master_reply}"

    echo
    echo_info "RedisReplication reported master"
    echo "  ${expected_master:-<empty>}"

    echo
    echo_info "Sentinel master listing"
    sentinel_exec SENTINEL masters || true
}

main() {
    # Run increasingly specific checks: cluster context, resources, service
    # aliases, an actual write/read, then Sentinel state when enabled.
    require_command kubectl
    check_kubectl_is_available
    configure_mode

    show_context
    load_runtime_context
    show_overview
    show_service_overview
    show_replication_roles
    check_alias_connectivity
    run_write_read_validation
    show_sentinel_state
}

main "$@"
