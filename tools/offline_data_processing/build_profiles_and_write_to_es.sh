#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/k8s_port_forward.sh"

# Build MovieLens user profiles and write them into Elasticsearch.
#
# Typical scenarios:
#   1. Full rebuild into the default user profile index. The script opens a
#      temporary ES port-forward before the indexing phase:
#        ./build_profiles_and_write_to_es.sh
#   2. Build the JSONL only, useful before inspecting sample profiles:
#        ./build_profiles_and_write_to_es.sh --skip-write
#   3. Reuse an existing JSONL file and write it to ES:
#        ./build_profiles_and_write_to_es.sh --skip-build
#
# By default the write phase maps Kubernetes Elasticsearch to localhost:59200.
# Set AUTO_PORT_FORWARD=0 to use ES_URL directly.

PYTHON_BIN="${PYTHON_BIN:-python3}"

# Input/output settings. These paths point at the full MovieLens 32M dataset
# and the generated profile document file consumed by the writer phase.
RATINGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv"
MOVIES_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/movies.csv"
TAGS_PATH="/Volumes/DataBase/Work/raw_dataset_32m_rating/tags.csv"
OUTPUT_PATH="${SCRIPT_DIR}/user_profiles.jsonl"
OUTPUT_FORMAT="jsonl"

# Builder settings. These are intentionally bounded because the profile is used
# as an online lookup document rather than a full behavior-history warehouse.
MAX_LIKED_ITEMS=100
MAX_RECENT_LIKED_ITEMS=30
MAX_INTERESTED_ITEMS=50
MAX_RATED_ITEMS=300
MAX_NEGATIVE_ITEMS=50
TOP_GENRES=10
TOP_TAGS=30
TOP_NEGATIVE_GENRES=10
TOP_NEGATIVE_TAGS=20
RATING_LOG_EVERY=2000000
USER_LOG_EVERY=50000

# Elasticsearch settings. The profile mapping stores _source only and disables
# field indexing because online reads are by _id=user_id.
ES_NAMESPACE="${ES_NAMESPACE:-recommendation-engine-es}"
ES_SERVICE_NAME="${ES_SERVICE_NAME:-item-index-es-http}"
ES_LOCAL_PORT="${ES_LOCAL_PORT:-59200}"
ES_REMOTE_PORT="${ES_REMOTE_PORT:-9200}"
ES_URL="${ES_URL:-https://127.0.0.1:${ES_LOCAL_PORT}}"
ES_USERNAME="${ES_USERNAME:-elastic}"
ES_PASSWORD_SECRET_NAME="${ES_PASSWORD_SECRET_NAME:-item-index-es-elastic-user}"
ES_PASSWORD_SECRET_KEY="${ES_PASSWORD_SECRET_KEY:-elastic}"
ES_PORT_FORWARD_LOG="${ES_PORT_FORWARD_LOG:-/tmp/offline-data-es-write-port-forward.log}"
INDEX_NAME="movielens_32m_user_profile"
INPUT_PATH="${OUTPUT_PATH}"
INPUT_FORMAT="${OUTPUT_FORMAT}"
ID_FIELD="user_id"
BULK_SIZE=200
TIMEOUT=60
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

run_user_profile_job() {
  local force_es_url_last="$1"
  shift

  local args=(
    --ratings "${RATINGS_PATH}"
    --movies "${MOVIES_PATH}"
    --tags "${TAGS_PATH}"
    --output "${OUTPUT_PATH}"
    --output-format "${OUTPUT_FORMAT}"
    --max-liked-items "${MAX_LIKED_ITEMS}"
    --max-recent-liked-items "${MAX_RECENT_LIKED_ITEMS}"
    --max-interested-items "${MAX_INTERESTED_ITEMS}"
    --max-rated-items "${MAX_RATED_ITEMS}"
    --max-negative-items "${MAX_NEGATIVE_ITEMS}"
    --top-genres "${TOP_GENRES}"
    --top-tags "${TOP_TAGS}"
    --top-negative-genres "${TOP_NEGATIVE_GENRES}"
    --top-negative-tags "${TOP_NEGATIVE_TAGS}"
    --rating-log-every "${RATING_LOG_EVERY}"
    --user-log-every "${USER_LOG_EVERY}"
    --input "${INPUT_PATH}"
    --input-format "${INPUT_FORMAT}"
    --index-name "${INDEX_NAME}"
    --id-field "${ID_FIELD}"
    --bulk-size "${BULK_SIZE}"
    --timeout "${TIMEOUT}"
    --ensure-index
    --no-verify-certs
    --log-level "${LOG_LEVEL}"
    --es-log-every "${ES_LOG_EVERY}"
  )

  if [[ "${force_es_url_last}" == "true" ]]; then
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/user_profile_to_es.py" \
      "${args[@]}" \
      "$@" \
      --es-url "${ES_URL}"
  else
    "${PYTHON_BIN}" "${SCRIPT_DIR}/src/jobs/user_profile_to_es.py" \
      "${args[@]}" \
      --es-url "${ES_URL}" \
      "$@"
  fi
}

main() {
  parse_runtime_flags "$@"

  if [[ "${HELP_REQUESTED}" == "1" ]]; then
    run_user_profile_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" != "1" && "${RUN_INDEX}" != "1" ]]; then
    run_user_profile_job false "$@"
    return
  fi

  if [[ "${RUN_BUILD}" == "1" ]]; then
    run_user_profile_job false --skip-write "$@"
  fi

  if [[ "${RUN_INDEX}" == "1" ]]; then
    load_es_credentials_if_needed
    offline_pf_start_es_port_forward
    if offline_pf_auto_port_forward_enabled; then
      run_user_profile_job true --skip-build "$@"
    else
      run_user_profile_job false --skip-build "$@"
    fi
  fi
}

main "$@"
