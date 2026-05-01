#!/usr/bin/env bash

# Shared Kubernetes port-forward helpers for offline data-processing wrappers.
# Source this file from a shell script, or run it directly with --clean.

OFFLINE_PF_PIDS=()
OFFLINE_PF_DESCRIPTIONS=()
OFFLINE_PF_TRAP_INSTALLED="${OFFLINE_PF_TRAP_INSTALLED:-0}"

offline_pf_echo_info() {
  echo "[INFO] $*"
}

offline_pf_echo_warning() {
  echo "[WARN] $*"
}

offline_pf_echo_error() {
  echo "[ERROR] $*" >&2
}

offline_pf_die() {
  offline_pf_echo_error "$*"
  exit 1
}

offline_pf_require_command() {
  local command_name="$1"
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    offline_pf_die "${command_name} is not installed or not available in PATH."
  fi
}

offline_pf_auto_port_forward_enabled() {
  case "${AUTO_PORT_FORWARD:-1}" in
    0|false|False|FALSE|no|No|NO|off|Off|OFF)
      return 1
      ;;
    *)
      return 0
      ;;
  esac
}

offline_pf_cleanup() {
  local pid
  local description
  local index

  for index in "${!OFFLINE_PF_PIDS[@]}"; do
    pid="${OFFLINE_PF_PIDS[$index]}"
    description="${OFFLINE_PF_DESCRIPTIONS[$index]}"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" >/dev/null 2>&1; then
      offline_pf_echo_info "Stopping ${description} port-forward."
      kill "${pid}" >/dev/null 2>&1 || true
      wait "${pid}" >/dev/null 2>&1 || true
    fi
  done
}

offline_pf_install_cleanup_trap() {
  if [[ "${OFFLINE_PF_TRAP_INSTALLED}" != "1" ]]; then
    trap offline_pf_cleanup EXIT
    OFFLINE_PF_TRAP_INSTALLED=1
  fi
}

offline_pf_register_pid() {
  local pid="$1"
  local description="$2"

  OFFLINE_PF_PIDS+=("${pid}")
  OFFLINE_PF_DESCRIPTIONS+=("${description}")
}

offline_pf_read_k8s_secret_value() {
  local namespace="$1"
  local secret_name="$2"
  local secret_key="$3"

  offline_pf_require_command kubectl
  kubectl get secret "${secret_name}" \
    -n "${namespace}" \
    -o "go-template={{.data.${secret_key} | base64decode}}"
}

offline_pf_probe_es_endpoint() {
  curl --silent \
    --output /dev/null \
    --connect-timeout 2 \
    --max-time 5 \
    --insecure \
    "https://127.0.0.1:${ES_LOCAL_PORT}/"
}

offline_pf_start_es_port_forward() {
  if ! offline_pf_auto_port_forward_enabled; then
    offline_pf_echo_info "AUTO_PORT_FORWARD=0; using configured Elasticsearch URL ${ES_URL}."
    return 0
  fi

  offline_pf_require_command kubectl
  offline_pf_require_command curl
  offline_pf_install_cleanup_trap

  : > "${ES_PORT_FORWARD_LOG}"
  offline_pf_echo_info "Starting Elasticsearch port-forward: ${ES_NAMESPACE}/service/${ES_SERVICE_NAME} ${ES_LOCAL_PORT}:${ES_REMOTE_PORT}"
  kubectl -n "${ES_NAMESPACE}" port-forward \
    "service/${ES_SERVICE_NAME}" \
    "${ES_LOCAL_PORT}:${ES_REMOTE_PORT}" \
    > "${ES_PORT_FORWARD_LOG}" 2>&1 &

  local pid=$!
  offline_pf_register_pid "${pid}" "Elasticsearch"

  for _ in $(seq 1 20); do
    if offline_pf_probe_es_endpoint >/dev/null 2>&1; then
      ES_URL="https://127.0.0.1:${ES_LOCAL_PORT}"
      offline_pf_echo_info "Elasticsearch port-forward is ready at ${ES_URL}."
      return 0
    fi
    if ! kill -0 "${pid}" >/dev/null 2>&1; then
      sed -n '1,120p' "${ES_PORT_FORWARD_LOG}" >&2 || true
      offline_pf_die "Elasticsearch port-forward exited unexpectedly."
    fi
    sleep 1
  done

  sed -n '1,120p' "${ES_PORT_FORWARD_LOG}" >&2 || true
  offline_pf_die "Timed out waiting for Elasticsearch port-forward."
}

offline_pf_redis_ping() {
  REDIS_HOST="${REDIS_HOST}" \
  REDIS_PORT="${REDIS_LOCAL_PORT}" \
  REDIS_DB="${REDIS_DB}" \
  REDIS_USERNAME="${REDIS_USERNAME:-}" \
  REDIS_PASSWORD="${REDIS_PASSWORD:-}" \
  REDIS_SSL="${REDIS_SSL:-false}" \
  REDIS_SOCKET_TIMEOUT="${REDIS_SOCKET_TIMEOUT}" \
  "${PYTHON_BIN}" - <<'PY' >/dev/null 2>&1
import os
import redis

def truthy(value: str) -> bool:
    return value.lower() in {"1", "true", "yes", "y", "on"}

client = redis.Redis(
    host=os.environ["REDIS_HOST"],
    port=int(os.environ["REDIS_PORT"]),
    db=int(os.environ["REDIS_DB"]),
    username=os.environ.get("REDIS_USERNAME") or None,
    password=os.environ.get("REDIS_PASSWORD") or None,
    ssl=truthy(os.environ.get("REDIS_SSL", "false")),
    socket_timeout=float(os.environ.get("REDIS_SOCKET_TIMEOUT", "5")),
    decode_responses=True,
)
client.ping()
PY
}

offline_pf_start_redis_port_forward_to_service() {
  local service_name="$1"
  local pid

  : > "${REDIS_PORT_FORWARD_LOG}"
  offline_pf_echo_info "Starting Redis port-forward: ${REDIS_NAMESPACE}/service/${service_name} ${REDIS_LOCAL_PORT}:${REDIS_REMOTE_PORT}"
  kubectl -n "${REDIS_NAMESPACE}" port-forward \
    "service/${service_name}" \
    "${REDIS_LOCAL_PORT}:${REDIS_REMOTE_PORT}" \
    > "${REDIS_PORT_FORWARD_LOG}" 2>&1 &
  pid=$!
  offline_pf_register_pid "${pid}" "Redis"

  for _ in $(seq 1 20); do
    if offline_pf_redis_ping; then
      REDIS_HOST="127.0.0.1"
      REDIS_PORT="${REDIS_LOCAL_PORT}"
      offline_pf_echo_info "Redis port-forward is ready at ${REDIS_HOST}:${REDIS_PORT}."
      return 0
    fi
    if ! kill -0 "${pid}" >/dev/null 2>&1; then
      sed -n '1,80p' "${REDIS_PORT_FORWARD_LOG}" >&2 || true
      return 1
    fi
    sleep 1
  done

  sed -n '1,80p' "${REDIS_PORT_FORWARD_LOG}" >&2 || true
  if kill -0 "${pid}" >/dev/null 2>&1; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
  return 1
}

offline_pf_start_redis_port_forward() {
  if ! offline_pf_auto_port_forward_enabled; then
    offline_pf_echo_info "AUTO_PORT_FORWARD=0; using configured Redis endpoint ${REDIS_HOST}:${REDIS_PORT}."
    return 0
  fi

  offline_pf_require_command kubectl
  offline_pf_require_command "${PYTHON_BIN}"
  offline_pf_install_cleanup_trap

  if ! "${PYTHON_BIN}" -c "import redis" >/dev/null 2>&1; then
    offline_pf_die "Python package 'redis' is not available in ${PYTHON_BIN}. Activate the offline data processing environment first or set PYTHON_BIN."
  fi

  REDIS_HOST="127.0.0.1"

  local services=()
  if [[ -n "${REDIS_SERVICE_NAME}" ]]; then
    services=("${REDIS_SERVICE_NAME}")
  else
    services=("redis-data-master" "redis-write" "redis-data")
  fi

  local service_name
  for service_name in "${services[@]}"; do
    if ! kubectl get service "${service_name}" -n "${REDIS_NAMESPACE}" >/dev/null 2>&1; then
      offline_pf_echo_warning "Redis service ${REDIS_NAMESPACE}/${service_name} was not found; trying next candidate."
      continue
    fi
    if offline_pf_start_redis_port_forward_to_service "${service_name}"; then
      REDIS_SERVICE_NAME="${service_name}"
      return 0
    fi
    offline_pf_echo_warning "Redis port-forward through service/${service_name} failed; trying next candidate."
  done

  offline_pf_die "Could not establish Redis port-forward. Set REDIS_SERVICE_NAME if your service has a different name."
}

offline_pf_list_all_port_forward_pids() {
  ps -ef | awk '$0 ~ /[k]ubectl/ && $0 ~ /port-forward/ {print $2}'
}

offline_pf_clean_all_port_forwards() {
  local pids=()
  local pid
  local killed_count=0
  local force_killed_count=0

  while IFS= read -r pid; do
    if [[ -n "${pid}" ]]; then
      pids+=("${pid}")
    fi
  done < <(offline_pf_list_all_port_forward_pids)

  if [[ "${#pids[@]}" -eq 0 ]]; then
    offline_pf_echo_info "No kubectl port-forward processes found."
    return 0
  fi

  offline_pf_echo_warning "Stopping ${#pids[@]} kubectl port-forward process(es)."
  for pid in "${pids[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      offline_pf_echo_info "TERM pid ${pid}"
      if kill "${pid}" >/dev/null 2>&1; then
        killed_count=$((killed_count + 1))
      else
        offline_pf_echo_warning "Failed to TERM pid ${pid}; it may belong to another user."
      fi
    fi
  done

  sleep 1

  while IFS= read -r pid; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" >/dev/null 2>&1; then
      offline_pf_echo_warning "KILL pid ${pid}"
      if kill -KILL "${pid}" >/dev/null 2>&1; then
        force_killed_count=$((force_killed_count + 1))
      else
        offline_pf_echo_warning "Failed to KILL pid ${pid}; it may belong to another user."
      fi
    fi
  done < <(offline_pf_list_all_port_forward_pids)

  offline_pf_echo_info "Port-forward cleanup finished: ${killed_count} TERM request(s), ${force_killed_count} KILL request(s)."
}

offline_pf_usage() {
  cat <<EOF
Usage:
  $(basename "$0") --clean

Options:
  --clean       Stop all kubectl port-forward processes.
  -h, --help    Show this help message.
EOF
}

offline_pf_main() {
  case "${1:-}" in
    --clean)
      shift
      if [[ "$#" -gt 0 ]]; then
        offline_pf_usage
        offline_pf_die "Unexpected argument after --clean: $1"
      fi
      offline_pf_clean_all_port_forwards
      ;;
    -h|--help|"")
      offline_pf_usage
      ;;
    *)
      offline_pf_usage
      offline_pf_die "Unknown argument: $1"
      ;;
  esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  offline_pf_main "$@"
fi
