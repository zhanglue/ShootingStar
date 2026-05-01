#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build user-user collaborative filtering similarity data and write it into Redis.
#
# Typical scenarios:
#   1. Full rebuild and Redis write after port-forwarding Redis to 6379:
#        ./build_user_similarity_and_write_to_redis.sh
#   2. Build JSONL only, useful for sampling the generated neighbor rows:
#        ./build_user_similarity_and_write_to_redis.sh --skip-write
#   3. Validate an existing JSONL without mutating Redis:
#        ./build_user_similarity_and_write_to_redis.sh --skip-build --dry-run
#   4. Write to a different logical DB or key prefix for an experiment:
#        REDIS_DB=1 KEY_PREFIX=rec:user_cf:test:neighbors ./build_user_similarity_and_write_to_redis.sh
#   5. Keep rating-strength information instead of binary positive feedback:
#        USE_RATING_WEIGHT=1 ./build_user_similarity_and_write_to_redis.sh
#
# Assumption: Redis is reachable at REDIS_HOST:REDIS_PORT, usually through:
#   kubectl -n recommendation-engine-redis port-forward svc/redis-data-master 6379:6379

RATINGS_PATH="${RATINGS_PATH:-/Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv}"
OUTPUT_PATH="${OUTPUT_PATH:-${SCRIPT_DIR}/user_similarity.jsonl}"
OUTPUT_FORMAT="${OUTPUT_FORMAT:-jsonl}"

# Similarity builder settings. max_item_users is intentionally conservative:
# popular MovieLens items otherwise generate very large user-pair expansions.
MIN_RATING="${MIN_RATING:-4.0}"
MIN_USER_ITEMS="${MIN_USER_ITEMS:-5}"
MIN_ITEM_USERS="${MIN_ITEM_USERS:-2}"
MAX_ITEM_USERS="${MAX_ITEM_USERS:-30}"
MIN_COOCCURRENCE="${MIN_COOCCURRENCE:-2}"
MIN_SIMILARITY="${MIN_SIMILARITY:-0.0}"
TOP_K="${TOP_K:-10}"
SHARD_COUNT="${SHARD_COUNT:-512}"
SHARD_BUFFER_ROWS="${SHARD_BUFFER_ROWS:-100000}"
TEMP_DIR="${TEMP_DIR:-${SCRIPT_DIR}/temp}"

# Progress settings. The first two stages rescan ratings.csv, while the later
# stages work over generated shards.
USER_STATS_LOG_EVERY="${USER_STATS_LOG_EVERY:-2000000}"
ITEM_MAPPING_LOG_EVERY="${ITEM_MAPPING_LOG_EVERY:-200000}"
SHARD_LOG_DIVISOR="${SHARD_LOG_DIVISOR:-32}"
LOG_LEVEL="${LOG_LEVEL:-INFO}"

# Redis settings. For local runs, port-forward Redis and leave REDIS_HOST=localhost.
# KEY_PREFIX is part of the serving contract: final keys are
# "${KEY_PREFIX}:<user_id>" sorted sets.
REDIS_HOST="${REDIS_HOST:-localhost}"
REDIS_PORT="${REDIS_PORT:-6379}"
REDIS_DB="${REDIS_DB:-0}"
KEY_PREFIX="${KEY_PREFIX:-rec:user_cf:v1:neighbors}"
BATCH_SIZE="${BATCH_SIZE:-500}"
SOCKET_TIMEOUT="${SOCKET_TIMEOUT:-5}"
REDIS_LOG_EVERY="${REDIS_LOG_EVERY:-300000}"

NEEDS_REDIS_AUTH=1
for arg in "$@"; do
  case "${arg}" in
    --skip-write|--dry-run|-h|--help)
      NEEDS_REDIS_AUTH=0
      ;;
  esac
done

ARGS=()
if [[ "${USE_RATING_WEIGHT:-0}" == "1" ]]; then
  ARGS+=(--use-rating-weight)
fi

# Load Redis auth automatically from Kubernetes when REDIS_PASSWORD was not
# provided by the caller. This keeps the common local workflow one-command.
if [[ "${NEEDS_REDIS_AUTH}" == "1" && -z "${REDIS_PASSWORD:-}" ]] && command -v kubectl >/dev/null 2>&1; then
  export REDIS_PASSWORD="$(
    kubectl get secret redis-auth \
      -n recommendation-engine-redis \
      -o go-template='{{.data.password | base64decode}}'
  )"
fi

# Run the combined job so the builder output path/format is passed directly to
# the Redis writer. Additional arguments are appended last for one-off overrides.
python3 "${SCRIPT_DIR}/src/jobs/user_similarity_to_redis.py" \
  --ratings "${RATINGS_PATH}" \
  --output "${OUTPUT_PATH}" \
  --output-format "${OUTPUT_FORMAT}" \
  --min-rating "${MIN_RATING}" \
  --min-user-items "${MIN_USER_ITEMS}" \
  --min-item-users "${MIN_ITEM_USERS}" \
  --max-item-users "${MAX_ITEM_USERS}" \
  --min-cooccurrence "${MIN_COOCCURRENCE}" \
  --min-similarity "${MIN_SIMILARITY}" \
  --top-k "${TOP_K}" \
  --shard-count "${SHARD_COUNT}" \
  --shard-buffer-rows "${SHARD_BUFFER_ROWS}" \
  --temp-dir "${TEMP_DIR}" \
  --log-level "${LOG_LEVEL}" \
  --user-stats-log-every "${USER_STATS_LOG_EVERY}" \
  --item-mapping-log-every "${ITEM_MAPPING_LOG_EVERY}" \
  --shard-log-divisor "${SHARD_LOG_DIVISOR}" \
  --input-schema user-similarity \
  --input "${OUTPUT_PATH}" \
  --input-format "${OUTPUT_FORMAT}" \
  --redis-host "${REDIS_HOST}" \
  --redis-port "${REDIS_PORT}" \
  --redis-db "${REDIS_DB}" \
  --key-prefix "${KEY_PREFIX}" \
  --batch-size "${BATCH_SIZE}" \
  --socket-timeout "${SOCKET_TIMEOUT}" \
  --redis-log-every "${REDIS_LOG_EVERY}" \
  "${ARGS[@]}" \
  "$@"
