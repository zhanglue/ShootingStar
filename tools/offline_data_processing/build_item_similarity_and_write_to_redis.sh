#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/k8s_port_forward.sh"

# Build item-item collaborative filtering data and write it into Redis.
#
# Typical scenarios:
#   1. Full rebuild and Redis write. The script opens a temporary Redis
#      port-forward before the write phase:
#        ./build_item_similarity_and_write_to_redis.sh
#   2. Build JSONL only, useful for sampling the generated neighbor rows:
#        ./build_item_similarity_and_write_to_redis.sh --skip-write
#   3. Validate an existing JSONL without mutating Redis:
#        ./build_item_similarity_and_write_to_redis.sh --skip-build --dry-run
#   4. Write to a different logical DB or key prefix for an experiment:
#        REDIS_DB=1 KEY_PREFIX=rec:item_cf:test:neighbors ./build_item_similarity_and_write_to_redis.sh
#
# By default the write phase maps Kubernetes Redis to localhost:56379.
# Set AUTO_PORT_FORWARD=0 to use REDIS_HOST:REDIS_PORT directly.

PYTHON_BIN="${PYTHON_BIN:-python3}"

# Input/output settings. The generated JSONL is the handoff between the
# similarity builder and the Redis writer.
RATINGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv"
OUTPUT_PATH="${SCRIPT_DIR}/item_similarity.jsonl"
OUTPUT_FORMAT="jsonl"

# Similarity builder settings. These thresholds define positive feedback, pair
# eligibility, and the number of neighbors kept per item.
MIN_RATING=4.0
MAX_USER_ITEMS=300
MIN_ITEM_USERS=5
MIN_COOCCURRENCE=3
MIN_SIMILARITY=0.0
TOP_K=100
SHARD_COUNT=512
SHARD_BUFFER_ROWS=100000
TEMP_DIR="${SCRIPT_DIR}/temp"

# Similarity progress settings. Different stages have very different costs, so
# keep their progress cadence separate instead of sharing one generic interval.
ITEM_STATS_LOG_EVERY=2000000
PAIR_MAPPING_LOG_EVERY=200000
SHARD_LOG_DIVISOR=32
USER_LOG_EVERY=30000

# Redis settings. For local runs, the script owns a temporary port-forward.
# KEY_PREFIX is part of the serving contract: final keys are
# "${KEY_PREFIX}:<item_id>" sorted sets.
REDIS_NAMESPACE="${REDIS_NAMESPACE:-recommendation-engine-redis}"
REDIS_SERVICE_NAME="${REDIS_SERVICE_NAME:-}"
REDIS_LOCAL_PORT="${REDIS_LOCAL_PORT:-56379}"
REDIS_REMOTE_PORT="${REDIS_REMOTE_PORT:-6379}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-${REDIS_LOCAL_PORT}}"
REDIS_DB="${REDIS_DB:-0}"
REDIS_USERNAME="${REDIS_USERNAME:-}"
REDIS_PASSWORD_SECRET_NAME="${REDIS_PASSWORD_SECRET_NAME:-redis-auth}"
REDIS_PASSWORD_SECRET_KEY="${REDIS_PASSWORD_SECRET_KEY:-password}"
REDIS_SSL="${REDIS_SSL:-false}"
KEY_PREFIX="${KEY_PREFIX:-rec:item_cf:v1:neighbors}"
BATCH_SIZE=500
SOCKET_TIMEOUT=5
REDIS_SOCKET_TIMEOUT="${REDIS_SOCKET_TIMEOUT:-${SOCKET_TIMEOUT}}"
REDIS_PORT_FORWARD_LOG="${REDIS_PORT_FORWARD_LOG:-/tmp/offline-data-redis-write-port-forward.log}"
LOG_LEVEL="INFO"
REDIS_LOG_EVERY=300000

RUN_BUILD=1
RUN_WRITE=1
NEEDS_REDIS_CONNECTION=1
HELP_REQUESTED=0
HAS_REDIS_CLI_AUTH=0

parse_runtime_flags() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --skip-build)
        RUN_BUILD=0
        ;;
      --skip-write)
        RUN_WRITE=0
        NEEDS_REDIS_CONNECTION=0
        ;;
      --dry-run)
        NEEDS_REDIS_CONNECTION=0
        ;;
      -h|--help)
        HELP_REQUESTED=1
        ;;
      --password)
        HAS_REDIS_CLI_AUTH=1
        if [[ $# -gt 1 ]]; then
          REDIS_PASSWORD="$2"
          shift
        fi
        ;;
      --password=*)
        HAS_REDIS_CLI_AUTH=1
        REDIS_PASSWORD="${1#*=}"
        ;;
      --username)
        if [[ $# -gt 1 ]]; then
          REDIS_USERNAME="$2"
          shift
        fi
        ;;
      --username=*)
        REDIS_USERNAME="${1#*=}"
        ;;
      --ssl)
        REDIS_SSL=true
        ;;
    esac
    shift
  done
}

load_redis_credentials_if_needed() {
  if [[ "${NEEDS_REDIS_CONNECTION}" != "1" ]]; then
    return 0
  fi

  export REDIS_USERNAME
  if [[ -n "${REDIS_PASSWORD:-}" || "${HAS_REDIS_CLI_AUTH}" == "1" ]]; then
    export REDIS_PASSWORD="${REDIS_PASSWORD:-}"
    return 0
  fi

  REDIS_PASSWORD="$(
    offline_pf_read_k8s_secret_value \
      "${REDIS_NAMESPACE}" \
      "${REDIS_PASSWORD_SECRET_NAME}" \
      "${REDIS_PASSWORD_SECRET_KEY}"
  )"
  export REDIS_PASSWORD
}

run_item_similarity_job() {
  local force_redis_endpoint_last="$1"
  shift

  local redis_ssl_args=()
  case "${REDIS_SSL}" in
    1|true|True|TRUE|yes|Yes|YES|y|Y|on|On|ON)
      redis_ssl_args+=(--ssl)
      ;;
  esac

  local args=(
    --ratings "${RATINGS_PATH}"
    --output "${OUTPUT_PATH}"
    --output-format "${OUTPUT_FORMAT}"
    --min-rating "${MIN_RATING}"
    --max-user-items "${MAX_USER_ITEMS}"
    --min-item-users "${MIN_ITEM_USERS}"
    --min-cooccurrence "${MIN_COOCCURRENCE}"
    --min-similarity "${MIN_SIMILARITY}"
    --top-k "${TOP_K}"
    --shard-count "${SHARD_COUNT}"
    --shard-buffer-rows "${SHARD_BUFFER_ROWS}"
    --temp-dir "${TEMP_DIR}"
    --input "${OUTPUT_PATH}"
    --input-format "${OUTPUT_FORMAT}"
    --redis-db "${REDIS_DB}"
    --key-prefix "${KEY_PREFIX}"
    --batch-size "${BATCH_SIZE}"
    --socket-timeout "${SOCKET_TIMEOUT}"
    --log-level "${LOG_LEVEL}"
    --redis-log-every "${REDIS_LOG_EVERY}"
    "${redis_ssl_args[@]}"
    --item-stats-log-every "${ITEM_STATS_LOG_EVERY}"
    --pair-mapping-log-every "${PAIR_MAPPING_LOG_EVERY}"
    --user-log-every "${USER_LOG_EVERY}"
    --shard-log-divisor "${SHARD_LOG_DIVISOR}"
  )

  if [[ "${force_redis_endpoint_last}" == "true" ]]; then
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/item_similarity_to_redis.py" \
      "${args[@]}" \
      "$@" \
      --redis-host "${REDIS_HOST}" \
      --redis-port "${REDIS_PORT}"
  else
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/item_similarity_to_redis.py" \
      "${args[@]}" \
      --redis-host "${REDIS_HOST}" \
      --redis-port "${REDIS_PORT}" \
      "$@"
  fi
}

main() {
  parse_runtime_flags "$@"

  if [[ "${HELP_REQUESTED}" == "1" ]]; then
    run_item_similarity_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" != "1" && "${RUN_WRITE}" != "1" ]]; then
    run_item_similarity_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" == "1" ]]; then
    run_item_similarity_job false --skip-write "$@"
  fi

  if [[ "${RUN_WRITE}" == "1" ]]; then
    load_redis_credentials_if_needed
    if [[ "${NEEDS_REDIS_CONNECTION}" == "1" ]]; then
      offline_pf_start_redis_port_forward
    fi
    if [[ "${NEEDS_REDIS_CONNECTION}" == "1" ]] && offline_pf_auto_port_forward_enabled; then
      run_item_similarity_job true --skip-build "$@"
    else
      run_item_similarity_job false --skip-build "$@"
    fi
  fi
}

main "$@"
