#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build MovieLens user profiles and write them into Elasticsearch.
#
# Typical scenarios:
#   1. Full rebuild into the default user profile index after port-forwarding ES:
#        ./build_profiles_and_write_to_es.sh
#   2. Build the JSONL only, useful before inspecting sample profiles:
#        ./build_profiles_and_write_to_es.sh --skip-index
#   3. Reuse an existing JSONL file and write it to ES:
#        ./build_profiles_and_write_to_es.sh --skip-build
#
# Assumption: Elasticsearch is reachable at ES_URL, usually through:
#   kubectl port-forward -n recommendation-engine-es service/item-index-es-http 9200:9200

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
ES_URL="https://localhost:9200"
INDEX_NAME="movielens_32m_user_profile"
INPUT_PATH="${OUTPUT_PATH}"
INPUT_FORMAT="${OUTPUT_FORMAT}"
ID_FIELD="user_id"
BULK_SIZE=200
TIMEOUT=60
LOG_LEVEL="INFO"
ES_LOG_EVERY=5000

# Load the elastic user's password from the ECK-managed Kubernetes Secret only
# when indexing is enabled and the caller has not already supplied ES_PASSWORD.
RUN_INDEX=1
for arg in "$@"; do
  if [[ "${arg}" == "--skip-index" ]]; then
    RUN_INDEX=0
  fi
done

if [[ "${RUN_INDEX}" == "1" && -z "${ES_PASSWORD:-}" ]]; then
  export ES_USERNAME="${ES_USERNAME:-elastic}"
  export ES_PASSWORD
  ES_PASSWORD=$(kubectl get secret -n recommendation-engine-es item-index-es-elastic-user -o go-template='{{.data.elastic | base64decode}}')
fi

# Run the combined job so the builder output path/format is passed directly to
# the Elasticsearch writer. Additional arguments are appended last to allow
# per-run overrides without editing this script.
python3 "${SCRIPT_DIR}/src/jobs/user_profile_to_es.py" \
  --ratings "${RATINGS_PATH}" \
  --movies "${MOVIES_PATH}" \
  --tags "${TAGS_PATH}" \
  --output "${OUTPUT_PATH}" \
  --output-format "${OUTPUT_FORMAT}" \
  --max-liked-items "${MAX_LIKED_ITEMS}" \
  --max-recent-liked-items "${MAX_RECENT_LIKED_ITEMS}" \
  --max-interested-items "${MAX_INTERESTED_ITEMS}" \
  --max-rated-items "${MAX_RATED_ITEMS}" \
  --max-negative-items "${MAX_NEGATIVE_ITEMS}" \
  --top-genres "${TOP_GENRES}" \
  --top-tags "${TOP_TAGS}" \
  --top-negative-genres "${TOP_NEGATIVE_GENRES}" \
  --top-negative-tags "${TOP_NEGATIVE_TAGS}" \
  --rating-log-every "${RATING_LOG_EVERY}" \
  --user-log-every "${USER_LOG_EVERY}" \
  --input "${INPUT_PATH}" \
  --input-format "${INPUT_FORMAT}" \
  --es-url "${ES_URL}" \
  --index-name "${INDEX_NAME}" \
  --id-field "${ID_FIELD}" \
  --bulk-size "${BULK_SIZE}" \
  --timeout "${TIMEOUT}" \
  --ensure-index \
  --no-verify-certs \
  --log-level "${LOG_LEVEL}" \
  --es-log-every "${ES_LOG_EVERY}" \
  "$@"
