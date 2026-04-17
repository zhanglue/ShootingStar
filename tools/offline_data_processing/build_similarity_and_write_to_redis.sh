#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Input/output settings.
RATINGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv"
OUTPUT_PATH="${SCRIPT_DIR}/item_similarity.jsonl"
OUTPUT_FORMAT="jsonl"

# Similarity builder settings.
MIN_RATING=4.0
MAX_USER_ITEMS=300
MIN_ITEM_USERS=5
MIN_COOCCURRENCE=3
MIN_SIMILARITY=0.0
TOP_K=100
SHARD_COUNT=512
SHARD_BUFFER_ROWS=100000
TEMP_DIR="${SCRIPT_DIR}/temp"

# Redis settings. For local runs, port-forward Redis and leave REDIS_HOST=localhost.
REDIS_HOST="${REDIS_HOST:-localhost}"
REDIS_PORT="${REDIS_PORT:-6379}"
REDIS_DB="${REDIS_DB:-0}"
KEY_PREFIX="${KEY_PREFIX:-rec:item_cf:v1:neighbors}"
BATCH_SIZE=500
SOCKET_TIMEOUT=5
LOG_LEVEL="INFO"
LOG_EVERY=5000

if [[ -z "${REDIS_PASSWORD:-}" ]] && command -v kubectl >/dev/null 2>&1; then
  export REDIS_PASSWORD="$(
    kubectl get secret redis-auth \
      -n recommendation-engine-redis \
      -o go-template='{{.data.password | base64decode}}'
  )"
fi

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
  --log-every "${LOG_EVERY}" \
  "$@"
