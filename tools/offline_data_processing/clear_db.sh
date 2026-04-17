#!/usr/bin/env bash

set -euo pipefail

ASSUME_YES=false

ES_NAMESPACE="${ES_NAMESPACE:-recommendation-engine-es}"
ES_SERVICE_NAME="${ES_SERVICE_NAME:-item-index-es-http}"
ES_LOCAL_PORT="${ES_LOCAL_PORT:-49200}"
ES_REMOTE_PORT="${ES_REMOTE_PORT:-9200}"
ES_USERNAME="${ES_USERNAME:-elastic}"
ES_PASSWORD="${ES_PASSWORD:-}"
ES_PASSWORD_SECRET_NAME="${ES_PASSWORD_SECRET_NAME:-item-index-es-elastic-user}"
ES_PASSWORD_SECRET_KEY="${ES_PASSWORD_SECRET_KEY:-elastic}"
ES_PORT_FORWARD_LOG="${ES_PORT_FORWARD_LOG:-/tmp/clear-db-es-port-forward.log}"
ES_URL=""

REDIS_NAMESPACE="${REDIS_NAMESPACE:-recommendation-engine-redis}"
REDIS_SERVICE_NAME="${REDIS_SERVICE_NAME:-}"
REDIS_LOCAL_PORT="${REDIS_LOCAL_PORT:-46379}"
REDIS_REMOTE_PORT="${REDIS_REMOTE_PORT:-6379}"
REDIS_HOST="127.0.0.1"
REDIS_DB="${REDIS_DB:-0}"
REDIS_PASSWORD="${REDIS_PASSWORD:-}"
REDIS_PASSWORD_SECRET_NAME="${REDIS_PASSWORD_SECRET_NAME:-redis-auth}"
REDIS_PASSWORD_SECRET_KEY="${REDIS_PASSWORD_SECRET_KEY:-password}"
REDIS_SSL="${REDIS_SSL:-false}"
REDIS_SOCKET_TIMEOUT="${REDIS_SOCKET_TIMEOUT:-5}"
REDIS_PORT_FORWARD_LOG="${REDIS_PORT_FORWARD_LOG:-/tmp/clear-db-redis-port-forward.log}"

PYTHON_BIN="${PYTHON_BIN:-python3}"

ES_PORT_FORWARD_PID=""
REDIS_PORT_FORWARD_PID=""

usage() {
  cat <<EOF
Usage:
  $(basename "$0") [-y]

Options:
  -y, --yes    Clear without interactive confirmation.
  -h, --help   Show this help message.

Environment variables:
  ES_NAMESPACE                Default: ${ES_NAMESPACE}
  ES_SERVICE_NAME             Default: ${ES_SERVICE_NAME}
  ES_LOCAL_PORT               Default: ${ES_LOCAL_PORT}
  ES_REMOTE_PORT              Default: ${ES_REMOTE_PORT}
  ES_USERNAME                 Default: ${ES_USERNAME}
  ES_PASSWORD                 Optional. If empty, read from Kubernetes Secret.
  ES_PASSWORD_SECRET_NAME     Default: ${ES_PASSWORD_SECRET_NAME}
  ES_PASSWORD_SECRET_KEY      Default: ${ES_PASSWORD_SECRET_KEY}

  REDIS_NAMESPACE             Default: ${REDIS_NAMESPACE}
  REDIS_SERVICE_NAME          Optional. If empty, try redis-data-master, redis-write, redis-data.
  REDIS_LOCAL_PORT            Default: ${REDIS_LOCAL_PORT}
  REDIS_REMOTE_PORT           Default: ${REDIS_REMOTE_PORT}
  REDIS_DB                    Default: ${REDIS_DB}
  REDIS_PASSWORD              Optional. If empty, read from Kubernetes Secret.
  REDIS_PASSWORD_SECRET_NAME  Default: ${REDIS_PASSWORD_SECRET_NAME}
  REDIS_PASSWORD_SECRET_KEY   Default: ${REDIS_PASSWORD_SECRET_KEY}
  REDIS_SSL                   Default: ${REDIS_SSL}
  PYTHON_BIN                  Default: ${PYTHON_BIN}
EOF
}

echo_info() {
  echo "[INFO] $*"
}

echo_warning() {
  echo "[WARN] $*"
}

echo_error() {
  echo "[ERROR] $*" >&2
}

die() {
  echo_error "$*"
  exit 1
}

cleanup() {
  if [[ -n "${ES_PORT_FORWARD_PID}" ]] && kill -0 "${ES_PORT_FORWARD_PID}" >/dev/null 2>&1; then
    kill "${ES_PORT_FORWARD_PID}" >/dev/null 2>&1 || true
    wait "${ES_PORT_FORWARD_PID}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${REDIS_PORT_FORWARD_PID}" ]] && kill -0 "${REDIS_PORT_FORWARD_PID}" >/dev/null 2>&1; then
    kill "${REDIS_PORT_FORWARD_PID}" >/dev/null 2>&1 || true
    wait "${REDIS_PORT_FORWARD_PID}" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

parse_args() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -y|--yes)
        ASSUME_YES=true
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        usage
        die "Unknown argument: $1"
        ;;
    esac
    shift
  done
}

require_command() {
  local command_name="$1"
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    die "${command_name} is not installed or not available in PATH."
  fi
}

check_environment() {
  echo_info "Checking local environment..."
  require_command kubectl
  require_command curl
  require_command "${PYTHON_BIN}"

  if ! "${PYTHON_BIN}" -c "import redis" >/dev/null 2>&1; then
    die "Python package 'redis' is not available in ${PYTHON_BIN}. Activate the item_index environment first or set PYTHON_BIN."
  fi
  echo_info "Environment check passed."
}

read_k8s_secret_value() {
  local namespace="$1"
  local secret_name="$2"
  local secret_key="$3"

  kubectl get secret "${secret_name}" \
    -n "${namespace}" \
    -o "go-template={{.data.${secret_key} | base64decode}}"
}

load_credentials() {
  echo_info "Loading credentials..."
  if [[ -z "${ES_PASSWORD}" ]]; then
    ES_PASSWORD="$(read_k8s_secret_value "${ES_NAMESPACE}" "${ES_PASSWORD_SECRET_NAME}" "${ES_PASSWORD_SECRET_KEY}")"
  fi
  if [[ -z "${REDIS_PASSWORD}" ]]; then
    REDIS_PASSWORD="$(read_k8s_secret_value "${REDIS_NAMESPACE}" "${REDIS_PASSWORD_SECRET_NAME}" "${REDIS_PASSWORD_SECRET_KEY}")"
  fi
  echo_info "Credentials loaded."
}

probe_es_endpoint() {
  curl --silent \
    --output /dev/null \
    --connect-timeout 2 \
    --max-time 5 \
    --insecure \
    "https://127.0.0.1:${ES_LOCAL_PORT}/"
}

start_es_port_forward() {
  : > "${ES_PORT_FORWARD_LOG}"
  echo_info "Starting Elasticsearch port-forward: ${ES_NAMESPACE}/service/${ES_SERVICE_NAME} ${ES_LOCAL_PORT}:${ES_REMOTE_PORT}"
  kubectl -n "${ES_NAMESPACE}" port-forward \
    "service/${ES_SERVICE_NAME}" \
    "${ES_LOCAL_PORT}:${ES_REMOTE_PORT}" \
    > "${ES_PORT_FORWARD_LOG}" 2>&1 &
  ES_PORT_FORWARD_PID=$!

  for _ in $(seq 1 20); do
    if probe_es_endpoint >/dev/null 2>&1; then
      ES_URL="https://127.0.0.1:${ES_LOCAL_PORT}"
      echo_info "Elasticsearch port-forward is ready at ${ES_URL}."
      return
    fi
    if ! kill -0 "${ES_PORT_FORWARD_PID}" >/dev/null 2>&1; then
      sed -n '1,120p' "${ES_PORT_FORWARD_LOG}" >&2 || true
      die "Elasticsearch port-forward exited unexpectedly."
    fi
    sleep 1
  done

  sed -n '1,120p' "${ES_PORT_FORWARD_LOG}" >&2 || true
  die "Timed out waiting for Elasticsearch port-forward."
}

redis_ping() {
  REDIS_HOST="${REDIS_HOST}" \
  REDIS_PORT="${REDIS_LOCAL_PORT}" \
  REDIS_DB="${REDIS_DB}" \
  REDIS_PASSWORD="${REDIS_PASSWORD}" \
  REDIS_SSL="${REDIS_SSL}" \
  REDIS_SOCKET_TIMEOUT="${REDIS_SOCKET_TIMEOUT}" \
  "${PYTHON_BIN}" - <<'PY' >/dev/null 2>&1
import os
import redis

def truthy(value: str) -> bool:
    return value.lower() in {"1", "true", "yes", "y"}

client = redis.Redis(
    host=os.environ["REDIS_HOST"],
    port=int(os.environ["REDIS_PORT"]),
    db=int(os.environ["REDIS_DB"]),
    password=os.environ.get("REDIS_PASSWORD") or None,
    ssl=truthy(os.environ.get("REDIS_SSL", "false")),
    socket_timeout=float(os.environ.get("REDIS_SOCKET_TIMEOUT", "5")),
    decode_responses=True,
)
client.ping()
PY
}

start_redis_port_forward_to_service() {
  local service_name="$1"

  : > "${REDIS_PORT_FORWARD_LOG}"
  echo_info "Starting Redis port-forward: ${REDIS_NAMESPACE}/service/${service_name} ${REDIS_LOCAL_PORT}:${REDIS_REMOTE_PORT}"
  kubectl -n "${REDIS_NAMESPACE}" port-forward \
    "service/${service_name}" \
    "${REDIS_LOCAL_PORT}:${REDIS_REMOTE_PORT}" \
    > "${REDIS_PORT_FORWARD_LOG}" 2>&1 &
  REDIS_PORT_FORWARD_PID=$!

  for _ in $(seq 1 20); do
    if redis_ping; then
      echo_info "Redis port-forward is ready at ${REDIS_HOST}:${REDIS_LOCAL_PORT}."
      return 0
    fi
    if ! kill -0 "${REDIS_PORT_FORWARD_PID}" >/dev/null 2>&1; then
      sed -n '1,80p' "${REDIS_PORT_FORWARD_LOG}" >&2 || true
      REDIS_PORT_FORWARD_PID=""
      return 1
    fi
    sleep 1
  done

  sed -n '1,80p' "${REDIS_PORT_FORWARD_LOG}" >&2 || true
  if kill -0 "${REDIS_PORT_FORWARD_PID}" >/dev/null 2>&1; then
    kill "${REDIS_PORT_FORWARD_PID}" >/dev/null 2>&1 || true
    wait "${REDIS_PORT_FORWARD_PID}" >/dev/null 2>&1 || true
  fi
  REDIS_PORT_FORWARD_PID=""
  return 1
}

start_redis_port_forward() {
  local services=()
  if [[ -n "${REDIS_SERVICE_NAME}" ]]; then
    services=("${REDIS_SERVICE_NAME}")
  else
    services=("redis-data-master" "redis-write" "redis-data")
  fi

  local service_name
  for service_name in "${services[@]}"; do
    if ! kubectl get service "${service_name}" -n "${REDIS_NAMESPACE}" >/dev/null 2>&1; then
      echo_warning "Redis service ${REDIS_NAMESPACE}/${service_name} was not found; trying next candidate."
      continue
    fi
    if start_redis_port_forward_to_service "${service_name}"; then
      REDIS_SERVICE_NAME="${service_name}"
      return
    fi
    echo_warning "Redis port-forward through service/${service_name} failed; trying next candidate."
  done

  die "Could not establish Redis port-forward. Set REDIS_SERVICE_NAME if your service has a different name."
}

es_curl() {
  local path="$1"
  curl --silent --show-error --fail \
    --insecure \
    --user "${ES_USERNAME}:${ES_PASSWORD}" \
    "${ES_URL}${path}"
}

es_curl_request() {
  local method="$1"
  local path="$2"
  curl --silent --show-error --fail \
    --request "${method}" \
    --insecure \
    --user "${ES_USERNAME}:${ES_PASSWORD}" \
    "${ES_URL}${path}"
}

print_es_stats() {
  local label="$1"

  echo
  echo_info "Elasticsearch usage stats (${label})"
  echo_info "GET /_cluster/health?pretty"
  es_curl "/_cluster/health?pretty"

  echo
  echo_info "GET /_cat/indices?expand_wildcards=all&v&s=store.size:desc"
  es_curl "/_cat/indices?expand_wildcards=all&v&s=store.size:desc" || true

  echo
  echo_info "GET /_data_stream?pretty"
  es_curl "/_data_stream?pretty" || true
}

print_redis_stats() {
  local label="$1"

  echo
  echo_info "Redis usage stats (${label})"
  REDIS_HOST="${REDIS_HOST}" \
  REDIS_PORT="${REDIS_LOCAL_PORT}" \
  REDIS_DB="${REDIS_DB}" \
  REDIS_PASSWORD="${REDIS_PASSWORD}" \
  REDIS_SSL="${REDIS_SSL}" \
  REDIS_SOCKET_TIMEOUT="${REDIS_SOCKET_TIMEOUT}" \
  "${PYTHON_BIN}" - <<'PY'
import os
import redis

def truthy(value: str) -> bool:
    return value.lower() in {"1", "true", "yes", "y"}

client = redis.Redis(
    host=os.environ["REDIS_HOST"],
    port=int(os.environ["REDIS_PORT"]),
    db=int(os.environ["REDIS_DB"]),
    password=os.environ.get("REDIS_PASSWORD") or None,
    ssl=truthy(os.environ.get("REDIS_SSL", "false")),
    socket_timeout=float(os.environ.get("REDIS_SOCKET_TIMEOUT", "5")),
    decode_responses=True,
)
print(f"PING: {client.ping()}")
print(f"Target DB: {os.environ['REDIS_DB']}")
print(f"DBSIZE: {client.dbsize()}")

keyspace = client.info("keyspace")
memory = client.info("memory")
print(f"KEYSPACE: {keyspace}")
print(f"USED_MEMORY_HUMAN: {memory.get('used_memory_human')}")
print(f"USED_MEMORY_PEAK_HUMAN: {memory.get('used_memory_peak_human')}")
PY
}

redis_dbsize() {
  REDIS_HOST="${REDIS_HOST}" \
  REDIS_PORT="${REDIS_LOCAL_PORT}" \
  REDIS_DB="${REDIS_DB}" \
  REDIS_PASSWORD="${REDIS_PASSWORD}" \
  REDIS_SSL="${REDIS_SSL}" \
  REDIS_SOCKET_TIMEOUT="${REDIS_SOCKET_TIMEOUT}" \
  "${PYTHON_BIN}" - <<'PY'
import os
import redis

def truthy(value: str) -> bool:
    return value.lower() in {"1", "true", "yes", "y"}

client = redis.Redis(
    host=os.environ["REDIS_HOST"],
    port=int(os.environ["REDIS_PORT"]),
    db=int(os.environ["REDIS_DB"]),
    password=os.environ.get("REDIS_PASSWORD") or None,
    ssl=truthy(os.environ.get("REDIS_SSL", "false")),
    socket_timeout=float(os.environ.get("REDIS_SOCKET_TIMEOUT", "5")),
    decode_responses=True,
)
print(client.dbsize())
PY
}

confirm_action() {
  local prompt="$1"

  if [[ "${ASSUME_YES}" == "true" ]]; then
    echo_info "-y was provided; skipping confirmation for this step."
    return 0
  fi

  local answer
  read -r -p "${prompt} (y/n): " answer
  if [[ "${answer}" == "y" || "${answer}" == "Y" ]]; then
    return 0
  fi

  return 1
}

warn_clear_targets() {
  echo
  echo_warning "This will permanently clear the Elasticsearch cluster data reachable at ${ES_URL}."
  echo_warning "This will permanently FLUSHDB Redis DB ${REDIS_DB} reachable at ${REDIS_HOST}:${REDIS_LOCAL_PORT}."
}

list_es_data_streams() {
  local output
  output="$(es_curl "/_data_stream" 2>/dev/null || true)"
  if [[ -z "${output}" ]]; then
    return
  fi
  printf '%s' "${output}" | "${PYTHON_BIN}" -c '
import json
import sys

try:
    payload = json.load(sys.stdin)
except json.JSONDecodeError:
    sys.exit(0)

for data_stream in payload.get("data_streams", []):
    name = data_stream.get("name")
    if name:
        print(name)
'
}

list_es_indices() {
  local output
  output="$(es_curl "/_cat/indices?expand_wildcards=all&h=index" 2>/dev/null || true)"
  printf '%s\n' "${output}" | sed '/^[[:space:]]*$/d'
}

list_es_user_data_streams() {
  list_es_data_streams | sed '/^\./d'
}

list_es_system_data_streams() {
  list_es_data_streams | sed -n '/^\./p'
}

list_es_user_indices() {
  list_es_indices | sed '/^\./d'
}

list_es_system_indices() {
  list_es_indices | sed -n '/^\./p'
}

clear_elasticsearch() {
  echo
  echo_info "Clearing Elasticsearch data..."

  local data_streams=()
  local system_data_streams=()
  local indices=()
  local system_indices=()

  mapfile -t data_streams < <(list_es_user_data_streams)
  mapfile -t system_data_streams < <(list_es_system_data_streams)
  mapfile -t indices < <(list_es_user_indices)
  mapfile -t system_indices < <(list_es_system_indices)

  if [[ "${#data_streams[@]}" -eq 0 && "${#indices[@]}" -eq 0 ]]; then
    echo_info "No user Elasticsearch data streams or indices found; skipping Elasticsearch delete requests."
    if [[ "${#system_data_streams[@]}" -gt 0 || "${#system_indices[@]}" -gt 0 ]]; then
      echo_info "Only Elasticsearch system/hidden resources remain; they are cluster-managed and are not deleted by this script."
      if [[ "${#system_data_streams[@]}" -gt 0 ]]; then
        printf '  system data streams: %s\n' "${system_data_streams[@]}"
      fi
      if [[ "${#system_indices[@]}" -gt 0 ]]; then
        printf '  system indices: %s\n' "${system_indices[@]}"
      fi
    fi
    return
  fi

  echo_warning "Elasticsearch will delete ${#data_streams[@]} user data stream(s) and ${#indices[@]} user index/indices."
  if [[ "${#system_data_streams[@]}" -gt 0 || "${#system_indices[@]}" -gt 0 ]]; then
    echo_info "Elasticsearch system/hidden resources will be left in place."
  fi
  if ! confirm_action "Clear Elasticsearch user data streams and indices"; then
    echo_info "Skipping Elasticsearch cleanup."
    return
  fi

  if [[ "${#data_streams[@]}" -gt 0 ]]; then
    echo_info "Deleting ${#data_streams[@]} Elasticsearch data stream(s)."
    local stream
    for stream in "${data_streams[@]}"; do
      echo_info "DELETE /_data_stream/${stream}"
      es_curl_request DELETE "/_data_stream/${stream}"
    done
  else
    echo_info "No Elasticsearch data streams found."
  fi

  indices=()
  mapfile -t indices < <(list_es_user_indices)
  if [[ "${#indices[@]}" -gt 0 ]]; then
    echo_info "Deleting ${#indices[@]} Elasticsearch index/indices."
    local old_ifs="${IFS}"
    IFS=,
    local index_path="/${indices[*]}?ignore_unavailable=true&expand_wildcards=all"
    IFS="${old_ifs}"
    es_curl_request DELETE "${index_path}"
    echo
  else
    echo_info "No Elasticsearch indices found."
  fi

  echo_info "Elasticsearch clear step finished."
}

clear_redis() {
  echo
  local before
  before="$(redis_dbsize)"

  if [[ "${before}" -eq 0 ]]; then
    echo_info "Redis DB ${REDIS_DB} is already empty; skipping FLUSHDB."
    return
  fi

  echo_warning "Redis DB ${REDIS_DB} currently has ${before} key(s)."
  if ! confirm_action "Clear Redis DB ${REDIS_DB} with FLUSHDB"; then
    echo_info "Skipping Redis cleanup."
    return
  fi

  echo_info "Clearing Redis DB ${REDIS_DB} with FLUSHDB..."
  REDIS_HOST="${REDIS_HOST}" \
  REDIS_PORT="${REDIS_LOCAL_PORT}" \
  REDIS_DB="${REDIS_DB}" \
  REDIS_PASSWORD="${REDIS_PASSWORD}" \
  REDIS_SSL="${REDIS_SSL}" \
  REDIS_SOCKET_TIMEOUT="${REDIS_SOCKET_TIMEOUT}" \
  "${PYTHON_BIN}" - <<'PY'
import os
import redis

def truthy(value: str) -> bool:
    return value.lower() in {"1", "true", "yes", "y"}

client = redis.Redis(
    host=os.environ["REDIS_HOST"],
    port=int(os.environ["REDIS_PORT"]),
    db=int(os.environ["REDIS_DB"]),
    password=os.environ.get("REDIS_PASSWORD") or None,
    ssl=truthy(os.environ.get("REDIS_SSL", "false")),
    socket_timeout=float(os.environ.get("REDIS_SOCKET_TIMEOUT", "5")),
    decode_responses=True,
)
before = client.dbsize()
result = client.flushdb()
after = client.dbsize()
print(f"DBSIZE_BEFORE: {before}")
print(f"FLUSHDB_RESULT: {result}")
print(f"DBSIZE_AFTER: {after}")
PY
  echo_info "Redis clear step finished."
}

main() {
  parse_args "$@"
  check_environment
  load_credentials
  start_es_port_forward
  start_redis_port_forward

  print_es_stats "before cleanup"
  print_redis_stats "before cleanup"
  warn_clear_targets

  clear_elasticsearch
  clear_redis

  print_es_stats "after cleanup"
  print_redis_stats "after cleanup"

  echo
  echo_info "Database cleanup finished."
}

main "$@"
