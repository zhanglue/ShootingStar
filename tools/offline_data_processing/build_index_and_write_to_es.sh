#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/k8s_port_forward.sh"

# Build the MovieLens item index file and write it into Elasticsearch.
#
# Typical scenarios:
#   1. Full rebuild into the default index. The script opens a temporary ES
#      port-forward before the indexing phase:
#        ./build_index_and_write_to_es.sh
#   2. Build the JSONL only, useful before inspecting mappings or sample docs:
#        ./build_index_and_write_to_es.sh --skip-write
#   3. Reuse an existing JSONL file and write to a throwaway test index:
#        ./build_index_and_write_to_es.sh --skip-build --index-name movielens_test_index
#
# By default the write phase maps Kubernetes Elasticsearch to localhost:59200.
# Set AUTO_PORT_FORWARD=0 to use ES_URL directly.

PYTHON_BIN="${PYTHON_BIN:-python3}"

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
ES_NAMESPACE="${ES_NAMESPACE:-recommendation-engine-es}"
ES_SERVICE_NAME="${ES_SERVICE_NAME:-item-index-es-http}"
ES_LOCAL_PORT="${ES_LOCAL_PORT:-59200}"
ES_REMOTE_PORT="${ES_REMOTE_PORT:-9200}"
ES_URL="${ES_URL:-https://127.0.0.1:${ES_LOCAL_PORT}}"
ES_USERNAME="${ES_USERNAME:-elastic}"
ES_PASSWORD_SECRET_NAME="${ES_PASSWORD_SECRET_NAME:-item-index-es-elastic-user}"
ES_PASSWORD_SECRET_KEY="${ES_PASSWORD_SECRET_KEY:-elastic}"
ES_PORT_FORWARD_LOG="${ES_PORT_FORWARD_LOG:-/tmp/offline-data-es-write-port-forward.log}"
INDEX_NAME="movielens_32m_item_index"
INPUT_PATH="${OUTPUT_PATH}"
INPUT_FORMAT="${OUTPUT_FORMAT}"
BULK_SIZE=500
TIMEOUT=30
LOG_LEVEL="INFO"
ES_LOG_EVERY=5000

RUN_BUILD=1
RUN_INDEX=1
HELP_REQUESTED=0
HAS_ES_CLI_AUTH=0

parse_runtime_flags() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --skip-build)
        RUN_BUILD=0
        ;;
      --skip-write)
        RUN_INDEX=0
        ;;
      -h|--help)
        HELP_REQUESTED=1
        ;;
      --password|--password=*|--api-key|--api-key=*)
        HAS_ES_CLI_AUTH=1
        ;;
    esac
    shift
  done
}

load_es_credentials_if_needed() {
  if [[ "${RUN_INDEX}" != "1" ]]; then
    return 0
  fi

  export ES_USERNAME
  if [[ -n "${ES_PASSWORD:-}" || -n "${ES_API_KEY:-}" || "${HAS_ES_CLI_AUTH}" == "1" ]]; then
    export ES_PASSWORD="${ES_PASSWORD:-}"
    return 0
  fi

  ES_PASSWORD="$(
    offline_pf_read_k8s_secret_value \
      "${ES_NAMESPACE}" \
      "${ES_PASSWORD_SECRET_NAME}" \
      "${ES_PASSWORD_SECRET_KEY}"
  )"
  export ES_PASSWORD
}

run_item_index_job() {
  local force_es_url_last="$1"
  shift

  local args=(
    --movies "${MOVIES_PATH}"
    --tags "${TAGS_PATH}"
    --links "${LINKS_PATH}"
    --ratings "${RATINGS_PATH}"
    --output "${OUTPUT_PATH}"
    --output-format "${OUTPUT_FORMAT}"
    --top-k "${TOP_K}"
    --min-weight "${MIN_WEIGHT}"
    --min-relative-weight "${MIN_RELATIVE_WEIGHT}"
    --rating-log-every "${RATING_LOG_EVERY}"
    --input "${INPUT_PATH}"
    --input-format "${INPUT_FORMAT}"
    --index-name "${INDEX_NAME}"
    --bulk-size "${BULK_SIZE}"
    --timeout "${TIMEOUT}"
    --ensure-index
    --no-verify-certs
    --log-level "${LOG_LEVEL}"
    --es-log-every "${ES_LOG_EVERY}"
  )

  if [[ "${force_es_url_last}" == "true" ]]; then
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/item_index_to_es.py" \
      "${args[@]}" \
      "$@" \
      --es-url "${ES_URL}"
  else
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/item_index_to_es.py" \
      "${args[@]}" \
      --es-url "${ES_URL}" \
      "$@"
  fi
}

main() {
  parse_runtime_flags "$@"

  if [[ "${HELP_REQUESTED}" == "1" ]]; then
    run_item_index_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" != "1" && "${RUN_INDEX}" != "1" ]]; then
    run_item_index_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" == "1" ]]; then
    run_item_index_job false --skip-write "$@"
  fi

  if [[ "${RUN_INDEX}" == "1" ]]; then
    load_es_credentials_if_needed
    offline_pf_start_es_port_forward
    if offline_pf_auto_port_forward_enabled; then
      run_item_index_job true --skip-build "$@"
    else
      run_item_index_job false --skip-build "$@"
    fi
  fi
}

main "$@"
