#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/k8s_port_forward.sh"

# Build user-user collaborative filtering similarity data and write it into Redis.
#
# Typical scenarios:
#   1. Full rebuild and Redis write. The script opens a temporary Redis
#      port-forward before the write phase:
#        ./build_user_similarity_and_write_to_redis.sh
#   2. Build JSONL only, useful for sampling the generated neighbor rows:
#        ./build_user_similarity_and_write_to_redis.sh --skip-write
#   3. Validate an existing JSONL without mutating Redis:
#        ./build_user_similarity_and_write_to_redis.sh --skip-build --dry-run
#   4. Write to a different logical DB or key prefix for an experiment:
#        ./build_user_similarity_and_write_to_redis.sh --redis-db 1 --key-prefix rec:user_cf:test:neighbors
#
# By default the write phase maps Kubernetes Redis to localhost:56379.
# Set AUTO_PORT_FORWARD=0 and pass --redis-host/--redis-port to use an existing tunnel.

PYTHON_BIN="python3"

RATINGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv"
OUTPUT_PATH="${SCRIPT_DIR}/user_similarity.jsonl"
OUTPUT_FORMAT="jsonl"

# Similarity builder settings. This experimental default keeps more signal so
# downstream scoring can inspect coverage and score/cooccurrence distributions.
MIN_RATING=3.5
USE_RATING_WEIGHT=1
POSITIVE_WEIGHT_OFFSET=3.0
MIN_USER_ITEMS=5
MIN_ITEM_USERS=2
MAX_ITEM_USERS=100
COVERAGE_ANCHOR_ITEMS_PER_USER=3
COVERAGE_PAIR_SAMPLE_SIZE=300
MIN_COOCCURRENCE=2
MIN_SIMILARITY=0.0
TOP_K=50
SHARD_COUNT=512
SHARD_BUFFER_ROWS=100000
TEMP_DIR="${SCRIPT_DIR}/temp/user_similarity"

# Progress settings. The first two stages rescan ratings.csv, while the later
# stages work over generated shards.
USER_STATS_LOG_EVERY=2000000
ITEM_MAPPING_LOG_EVERY=2000000
SHARD_LOG_DIVISOR=32
LOG_LEVEL="INFO"

# Redis settings. For local runs, the script owns a temporary port-forward.
# KEY_PREFIX is part of the serving contract: final keys are
# "${KEY_PREFIX}:<user_id>" sorted sets.
REDIS_NAMESPACE="recommendation-engine-redis"
REDIS_SERVICE_NAME="redis-data-master"
REDIS_LOCAL_PORT=56379
REDIS_REMOTE_PORT=6379
REDIS_HOST="127.0.0.1"
REDIS_PORT="${REDIS_LOCAL_PORT}"
REDIS_DB=0
REDIS_USERNAME=""
REDIS_PASSWORD=""
REDIS_PASSWORD_SECRET_NAME="redis-auth"
REDIS_PASSWORD_SECRET_KEY="password"
REDIS_SSL=false
KEY_PREFIX="rec:user_cf:v1:neighbors"
BATCH_SIZE=500
SOCKET_TIMEOUT=5
REDIS_SOCKET_TIMEOUT="${SOCKET_TIMEOUT}"
REDIS_PORT_FORWARD_LOG="/tmp/offline-data-redis-write-port-forward.log"
REDIS_LOG_EVERY=300000

RUN_BUILD=1
RUN_WRITE=1
NEEDS_REDIS_CONNECTION=1
HELP_REQUESTED=0
HAS_REDIS_CLI_AUTH=0

ARGS=()
if [[ "${USE_RATING_WEIGHT}" == "1" ]]; then
  ARGS+=(--use-rating-weight)
fi

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
  if [[ -n "${REDIS_PASSWORD}" || "${HAS_REDIS_CLI_AUTH}" == "1" ]]; then
    export REDIS_PASSWORD
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

run_user_similarity_job() {
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
    --positive-weight-offset "${POSITIVE_WEIGHT_OFFSET}"
    --min-user-items "${MIN_USER_ITEMS}"
    --min-item-users "${MIN_ITEM_USERS}"
    --max-item-users "${MAX_ITEM_USERS}"
    --coverage-anchor-items-per-user "${COVERAGE_ANCHOR_ITEMS_PER_USER}"
    --coverage-pair-sample-size "${COVERAGE_PAIR_SAMPLE_SIZE}"
    --min-cooccurrence "${MIN_COOCCURRENCE}"
    --min-similarity "${MIN_SIMILARITY}"
    --top-k "${TOP_K}"
    --shard-count "${SHARD_COUNT}"
    --shard-buffer-rows "${SHARD_BUFFER_ROWS}"
    --temp-dir "${TEMP_DIR}"
    --log-level "${LOG_LEVEL}"
    --user-stats-log-every "${USER_STATS_LOG_EVERY}"
    --item-mapping-log-every "${ITEM_MAPPING_LOG_EVERY}"
    --shard-log-divisor "${SHARD_LOG_DIVISOR}"
    --input-schema user-similarity
    --input "${OUTPUT_PATH}"
    --input-format "${OUTPUT_FORMAT}"
    --redis-db "${REDIS_DB}"
    --key-prefix "${KEY_PREFIX}"
    --batch-size "${BATCH_SIZE}"
    --socket-timeout "${SOCKET_TIMEOUT}"
    --redis-log-every "${REDIS_LOG_EVERY}"
    "${redis_ssl_args[@]}"
    "${ARGS[@]}"
  )

  if [[ "${force_redis_endpoint_last}" == "true" ]]; then
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/user_similarity_to_redis.py" \
      "${args[@]}" \
      "$@" \
      --redis-host "${REDIS_HOST}" \
      --redis-port "${REDIS_PORT}"
  else
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/user_similarity_to_redis.py" \
      "${args[@]}" \
      --redis-host "${REDIS_HOST}" \
      --redis-port "${REDIS_PORT}" \
      "$@"
  fi
}

main() {
  parse_runtime_flags "$@"

  if [[ "${HELP_REQUESTED}" == "1" ]]; then
    run_user_similarity_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" != "1" && "${RUN_WRITE}" != "1" ]]; then
    run_user_similarity_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" == "1" ]]; then
    run_user_similarity_job false --skip-write "$@"
  fi

  if [[ "${RUN_WRITE}" == "1" ]]; then
    load_redis_credentials_if_needed
    if [[ "${NEEDS_REDIS_CONNECTION}" == "1" ]]; then
      offline_pf_start_redis_port_forward
    fi
    if [[ "${NEEDS_REDIS_CONNECTION}" == "1" ]] && offline_pf_auto_port_forward_enabled; then
      run_user_similarity_job true --skip-build "$@"
    else
      run_user_similarity_job false --skip-build "$@"
    fi
  fi
}

main "$@"
