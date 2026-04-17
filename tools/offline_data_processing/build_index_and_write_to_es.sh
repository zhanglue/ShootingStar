#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build the MovieLens item index file and write it into Elasticsearch.
#
# Typical scenarios:
#   1. Full rebuild into the default index after port-forwarding ES to 9200:
#        ./build_index_and_write_to_es.sh
#   2. Build the JSONL only, useful before inspecting mappings or sample docs:
#        ./build_index_and_write_to_es.sh --skip-index
#   3. Reuse an existing JSONL file and write to a throwaway test index:
#        ./build_index_and_write_to_es.sh --skip-build --index-name movielens_test_index
#
# Assumption: Elasticsearch is reachable at ES_URL, usually through:
#   kubectl port-forward -n recommendation-engine-es service/item-index-es-http 9200:9200

# Input/output settings. These paths point at the full MovieLens 32M dataset
# and the generated item document file consumed by the writer phase.
MOVIES_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/movies.csv"
TAGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/tags.csv"
LINKS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/links.csv"
RATINGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv"
OUTPUT_PATH="${SCRIPT_DIR}/item_index.jsonl"
OUTPUT_FORMAT="jsonl"

# Builder settings. top_k/min thresholds control how many weighted user tags
# become searchable metadata in each item document.
TOP_K=10
MIN_WEIGHT=0.0
MIN_RELATIVE_WEIGHT=0.3
RATING_LOG_EVERY=2000000

# Elasticsearch settings. --ensure-index below creates the target index when it
# is missing, using the mapping file configured in ElasticsearchWriter.
ES_URL="https://localhost:9200"
INDEX_NAME="movielens_32m_rating_index"
INPUT_PATH="${OUTPUT_PATH}"
INPUT_FORMAT="${OUTPUT_FORMAT}"
BULK_SIZE=500
TIMEOUT=30
LOG_LEVEL="INFO"
ES_LOG_EVERY=5000

# Load the elastic user's password from the ECK-managed Kubernetes Secret.
# Callers may still pass writer CLI flags after "$@" below for one-off changes.
export ES_USERNAME=elastic
export ES_PASSWORD=$(kubectl get secret -n recommendation-engine-es item-index-es-elastic-user -o go-template='{{.data.elastic | base64decode}}')

# Run the combined job so the builder output path/format is passed directly to
# the Elasticsearch writer. Additional arguments are appended last to allow
# per-run overrides without editing this script.
python3 "${SCRIPT_DIR}/src/jobs/item_index_to_es.py" \
  --movies "${MOVIES_PATH}" \
  --tags "${TAGS_PATH}" \
  --links "${LINKS_PATH}" \
  --ratings "${RATINGS_PATH}" \
  --output "${OUTPUT_PATH}" \
  --output-format "${OUTPUT_FORMAT}" \
  --top-k "${TOP_K}" \
  --min-weight "${MIN_WEIGHT}" \
  --min-relative-weight "${MIN_RELATIVE_WEIGHT}" \
  --rating-log-every "${RATING_LOG_EVERY}" \
  --input "${INPUT_PATH}" \
  --input-format "${INPUT_FORMAT}" \
  --es-url "${ES_URL}" \
  --index-name "${INDEX_NAME}" \
  --bulk-size "${BULK_SIZE}" \
  --timeout "${TIMEOUT}" \
  --ensure-index \
  --no-verify-certs \
  --log-level "${LOG_LEVEL}" \
  --es-log-every "${ES_LOG_EVERY}" \
  "$@"
