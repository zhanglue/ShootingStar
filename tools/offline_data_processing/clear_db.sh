#!/usr/bin/env bash

set -euo pipefail

# Clear the offline-processing backing stores before a full validation run.
#
# Typical scenarios:
#   1. Inspect current ES/Redis usage, then confirm each cleanup interactively:
#        ./clear_db.sh
#   2. CI/manual reset where confirmations should be skipped:
#        ./clear_db.sh -y
#   3. Use non-default local ports or a specific Redis service:
#        ES_LOCAL_PORT=49201 REDIS_LOCAL_PORT=46380 REDIS_SERVICE_NAME=redis-write ./clear_db.sh
#   4. Run from a shell where python3 is not the item_index environment:
#        PYTHON_BIN=/Volumes/DataBase/Work/miniconda3/envs/item_index/bin/python ./clear_db.sh

ASSUME_YES=false

# Elasticsearch connection settings. The script opens its own port-forward so it
# does not depend on a manually prepared localhost:9200 session.
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

# Redis connection settings. Local port 46379 intentionally avoids the common
# 6379 port used by ad-hoc manual port-forwards.
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

# Use the already-activated Python by default. Callers can set PYTHON_BIN when
# the active shell is not the environment containing the redis package.
PYTHON_BIN="${PYTHON_BIN:-python3}"

ES_PORT_FORWARD_PID=""
REDIS_PORT_FORWARD_PID=""

usage() {
  # Keep examples near the top-level comments so --help stays compact.
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
  # Always tear down port-forward subprocesses that this script started,
  # including early exits caused by failed checks or user cancellation.
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
  # The script intentionally supports only a small flag surface; target details
  # are controlled via environment variables to match the other shell entrypoints.
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
  # Fail before opening port-forwards or reading secrets if the local runtime is
  # missing a required command/library.
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
  # Secrets are read just-in-time instead of being printed or exported.
  local namespace="$1"
  local secret_name="$2"
  local secret_key="$3"

  kubectl get secret "${secret_name}" \
    -n "${namespace}" \
    -o "go-template={{.data.${secret_key} | base64decode}}"
}

load_credentials() {
  # Environment-provided passwords win, which makes the script usable outside
  # Kubernetes as long as port-forward targets and credentials are provided.
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
  # This endpoint probe does not authenticate; readiness only means that the
  # local tunnel accepts TLS connections.
  curl --silent \
    --output /dev/null \
    --connect-timeout 2 \
    --max-time 5 \
    --insecure \
    "https://127.0.0.1:${ES_LOCAL_PORT}/"
}

start_es_port_forward() {
  # Run kubectl in the background and wait until the local tunnel is usable.
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
  # Redis auth is verified through Python because this script assumes the redis
  # Python package, not redis-cli, is available in the active environment.
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
  # Try one Redis service name and report failure to the caller so it can fall
  # through to the next candidate.
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
  # k3s and local manifests can expose different service names, so probe the
  # common candidates unless the caller pins REDIS_SERVICE_NAME.
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
  # Common wrapper for authenticated, self-signed-friendly ES GET requests.
  local path="$1"
  curl --silent --show-error --fail \
    --insecure \
    --user "${ES_USERNAME}:${ES_PASSWORD}" \
    "${ES_URL}${path}"
}

es_curl_request() {
  # Common wrapper for authenticated ES mutation requests.
  local method="$1"
  local path="$2"
  curl --silent --show-error --fail \
    --request "${method}" \
    --insecure \
    --user "${ES_USERNAME}:${ES_PASSWORD}" \
    "${ES_URL}${path}"
}

print_es_stats() {
  # Print before/after ES state so the operator can see whether anything needs
  # deletion and whether cleanup changed the expected resources.
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
  # Print Redis DB and memory stats before/after cleanup. Memory can remain
  # non-zero after FLUSHDB because Redis keeps allocator/process overhead.
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
  # Return only a number so clear_redis can make a skip/delete decision.
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
  # Each backing store confirms independently; this avoids one "yes" clearing
  # both ES and Redis when only one side needs work.
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
  # High-level warning printed after usage stats, before any store-specific
  # prompt or deletion is attempted.
  echo
  echo_warning "This will permanently clear the Elasticsearch cluster data reachable at ${ES_URL}."
  echo_warning "This will permanently FLUSHDB Redis DB ${REDIS_DB} reachable at ${REDIS_HOST}:${REDIS_LOCAL_PORT}."
}

list_es_data_streams() {
  # Use the JSON data stream API instead of _cat so the script works across ES
  # versions where _cat/data_streams may be unavailable or restricted.
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
  # Include hidden/system names in the listing so stats are honest; filtering
  # happens in the user/system split helpers below.
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
  # Only user-created indices/data streams are deleted. Dot-prefixed system
  # resources such as .security-7 are reported but left intact.
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
  # Redis cleanup is an actual FLUSHDB for the selected logical DB. If DBSIZE is
  # zero, skip the mutation entirely.
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
  # Main flow: check local requirements, open tunnels, show state, confirm per
  # backing store, clear if needed, then show final state.
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
