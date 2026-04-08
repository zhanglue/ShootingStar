#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Input/output settings.
MOVIES_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/movies.csv"
TAGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/tags.csv"
LINKS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/links.csv"
RATINGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv"
OUTPUT_PATH="${SCRIPT_DIR}/item_index.jsonl"
OUTPUT_FORMAT="jsonl"

# Builder settings.
TOP_K=10
MIN_WEIGHT=0.0
MIN_RELATIVE_WEIGHT=0.3

# Elasticsearch settings.
ES_URL="https://localhost:9200"
INDEX_NAME="movielens_32m_rating_index"
INPUT_PATH="${OUTPUT_PATH}"
INPUT_FORMAT="${OUTPUT_FORMAT}"
BULK_SIZE=500
TIMEOUT=30
LOG_LEVEL="INFO"
LOG_EVERY=5000

# Expect credentials to come from the environment.
export ES_USERNAME=elastic
export ES_PASSWORD=$(kubectl get secret -n recommendation-engine-es item-index-es-elastic-user -o go-template='{{.data.elastic | base64decode}}')

python3 "${SCRIPT_DIR}/src/main.py" \
  --movies "${MOVIES_PATH}" \
  --tags "${TAGS_PATH}" \
  --links "${LINKS_PATH}" \
  --ratings "${RATINGS_PATH}" \
  --output "${OUTPUT_PATH}" \
  --output-format "${OUTPUT_FORMAT}" \
  --top-k "${TOP_K}" \
  --min-weight "${MIN_WEIGHT}" \
  --min-relative-weight "${MIN_RELATIVE_WEIGHT}" \
  --input "${INPUT_PATH}" \
  --input-format "${INPUT_FORMAT}" \
  --es-url "${ES_URL}" \
  --index-name "${INDEX_NAME}" \
  --bulk-size "${BULK_SIZE}" \
  --timeout "${TIMEOUT}" \
  --ensure-index \
  --no-verify-certs \
  --log-level "${LOG_LEVEL}" \
  --log-every "${LOG_EVERY}" \
  "$@"
