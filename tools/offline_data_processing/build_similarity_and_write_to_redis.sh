#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build item-item collaborative filtering data and write it into Redis.
#
# Typical scenarios:
#   1. Full rebuild and Redis write after port-forwarding Redis to 6379:
#        ./build_similarity_and_write_to_redis.sh
#   2. Build JSONL only, useful for sampling the generated neighbor rows:
#        ./build_similarity_and_write_to_redis.sh --skip-write
#   3. Validate an existing JSONL without mutating Redis:
#        ./build_similarity_and_write_to_redis.sh --skip-build --dry-run
#   4. Write to a different logical DB or key prefix for an experiment:
#        REDIS_DB=1 KEY_PREFIX=rec:item_cf:test:neighbors ./build_similarity_and_write_to_redis.sh
#
# Assumption: Redis is reachable at REDIS_HOST:REDIS_PORT, usually through:
#   kubectl -n recommendation-engine-redis port-forward svc/redis-data-master 6379:6379

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

# Redis settings. For local runs, port-forward Redis and leave REDIS_HOST=localhost.
# KEY_PREFIX is part of the serving contract: final keys are
# "${KEY_PREFIX}:<item_id>" sorted sets.
REDIS_HOST="${REDIS_HOST:-localhost}"
REDIS_PORT="${REDIS_PORT:-6379}"
REDIS_DB="${REDIS_DB:-0}"
KEY_PREFIX="${KEY_PREFIX:-rec:item_cf:v1:neighbors}"
BATCH_SIZE=500
SOCKET_TIMEOUT=5
LOG_LEVEL="INFO"
REDIS_LOG_EVERY=300000

# Load Redis auth automatically from Kubernetes when REDIS_PASSWORD was not
# provided by the caller. This keeps the common local workflow one-command.
if [[ -z "${REDIS_PASSWORD:-}" ]] && command -v kubectl >/dev/null 2>&1; then
  export REDIS_PASSWORD="$(
    kubectl get secret redis-auth \
      -n recommendation-engine-redis \
      -o go-template='{{.data.password | base64decode}}'
  )"
fi

# Run the combined job so the builder output path/format is passed directly to
# the Redis writer. Additional arguments are appended last for one-off overrides.
python3 "${SCRIPT_DIR}/src/jobs/item_similarity_to_redis.py" \
  --ratings "${RATINGS_PATH}" \
  --output "${OUTPUT_PATH}" \
  --output-format "${OUTPUT_FORMAT}" \
  --min-rating "${MIN_RATING}" \
  --max-user-items "${MAX_USER_ITEMS}" \
  --min-item-users "${MIN_ITEM_USERS}" \
  --min-cooccurrence "${MIN_COOCCURRENCE}" \
  --min-similarity "${MIN_SIMILARITY}" \
  --top-k "${TOP_K}" \
  --shard-count "${SHARD_COUNT}" \
  --shard-buffer-rows "${SHARD_BUFFER_ROWS}" \
  --temp-dir "${TEMP_DIR}" \
  --input "${OUTPUT_PATH}" \
  --input-format "${OUTPUT_FORMAT}" \
  --redis-host "${REDIS_HOST}" \
  --redis-port "${REDIS_PORT}" \
  --redis-db "${REDIS_DB}" \
  --key-prefix "${KEY_PREFIX}" \
  --batch-size "${BATCH_SIZE}" \
  --socket-timeout "${SOCKET_TIMEOUT}" \
  --log-level "${LOG_LEVEL}" \
  --redis-log-every "${REDIS_LOG_EVERY}" \
  --item-stats-log-every "${ITEM_STATS_LOG_EVERY}" \
  --pair-mapping-log-every "${PAIR_MAPPING_LOG_EVERY}" \
  --user-log-every "${USER_LOG_EVERY}" \
  --shard-log-divisor "${SHARD_LOG_DIVISOR}" \
  "$@"
