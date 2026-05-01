# Offline Data Processing

This directory stores offline data processing jobs that build serving data and write it to middleware. The current jobs are:

1. Build movie item-index documents from MovieLens-style CSV files and write them into Elasticsearch.
2. Build user-profile documents from MovieLens-style CSV files and write them into Elasticsearch.
3. Build item similarity neighbors from MovieLens-style ratings and write them into Redis.
4. Build user similarity neighbors from MovieLens-style ratings and write them into Redis.

The current default target index name is `movielens_32m_item_index`.

## Data Source

This toolchain is currently built and validated against the MovieLens 32M dataset:

> MovieLens Datasets: https://grouplens.org/datasets/movielens

Here is the [README](https://files.grouplens.org/datasets/movielens/ml-32m-README.html) of the 32M dataset. Thanks to the GroupLens team for providing this high-quality dataset for research purposes.


## Folder Layout

```text
tools/offline_data_processing/
├── build_index_and_write_to_es.sh
├── build_profiles_and_write_to_es.sh
├── build_item_similarity_and_write_to_redis.sh
├── build_user_similarity_and_write_to_redis.sh
├── check_es_rollout_stats.sh
├── check_redis_functionality.sh
├── check_redis_rollout_status.sh
├── clear_db.sh
├── k8s_port_forward.sh
├── config/
│   ├── item_index_mapping.json
│   ├── item_similarity_requirements.txt
│   ├── requirements.txt
│   └── schema.json
└── src/
    ├── builders/
    │   ├── item_index_builder.py
    │   ├── item_similarity_builder.py
    │   └── user_similarity_builder.py
    ├── jobs/
    │   ├── item_index_to_es.py
    │   ├── item_similarity_to_redis.py
    │   └── user_similarity_to_redis.py
    └── writers/
        ├── elasticsearch_writer.py
        └── redis_writer.py
```

- `config/` stores configuration-like files.
- `src/jobs/` stores end-to-end offline jobs.
- `src/builders/` stores data builders.
- `src/writers/` stores middleware writers.
- `build_index_and_write_to_es.sh` is the one-command local entrypoint for the item-index ES job.
- `build_profiles_and_write_to_es.sh` is the one-command local entrypoint for the user-profile ES job.
- `build_item_similarity_and_write_to_redis.sh` is the one-command local entrypoint for the item-similarity Redis job.
- `build_user_similarity_and_write_to_redis.sh` is the one-command local entrypoint for the user-similarity Redis job.
- `clear_db.sh` clears ES/Redis data before a full validation run.
- `k8s_port_forward.sh` contains shared helpers used by local write wrappers to open temporary Kubernetes port-forwards.
- `check_*.sh` scripts inspect ES/Redis rollout and runtime functionality.

## Environment

Create a dedicated environment and install dependencies first:

```bash
conda create -n offline_data_processing python=3.11 -y
conda activate offline_data_processing
pip install -r tools/offline_data_processing/config/requirements.txt
```

The shell wrappers that write to ES/Redis open temporary Kubernetes
port-forwards automatically before the remote write phase:

- ES write wrappers default to local port `59200`
- Redis write wrappers default to local port `56379`
- `clear_db.sh` uses its own `4xxxx` ports so cleanup and write workflows do not collide

Set `AUTO_PORT_FORWARD=0` if you already have an endpoint and want the wrappers
to use `ES_URL` or `REDIS_HOST:REDIS_PORT` directly.

If an interrupted run leaves local tunnels behind, clean every
`kubectl port-forward` process with:

```bash
tools/offline_data_processing/k8s_port_forward.sh --clean
```

If you need to write to Elasticsearch outside the wrappers, prepare credentials via environment variables:

```bash
export ES_USERNAME=elastic
export ES_PASSWORD='your-password'
```

## Main Entry

File: [src/jobs/item_index_to_es.py](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/src/jobs/item_index_to_es.py)

`item_index_to_es.py` orchestrates the two workers:

- build phase: calls `ItemIndexBuilder`
- index phase: calls `ElasticsearchWriter`

It supports two common modes:

1. Build from CSV and write directly into ES
2. Skip build and write an existing `jsonl` file into ES

Common entrypoint parameters:

- `--skip-build`
  Skip document building and only write an existing file to ES
- `--skip-write`
  Build the document file only and do not write to ES
- `--movies / --tags / --links / --ratings`
  Input CSV paths
- `--output`
  Output document file path
- `--output-format`
  `jsonl` or `json`
- `--top-k`
  Maximum number of `top_tags` to keep per item
- `--min-weight`
  Absolute weight threshold for `top_tags`
- `--min-relative-weight`
  Relative threshold compared to the strongest tag
- `--input / --input-format`
  Existing document file when skipping build
- `--es-url`
  Elasticsearch URL
- `--index-name`
  Target index name
- `--ensure-index`
  Create the index if it does not exist
- `--mapping-path`
  Explicit mapping JSON path
- `--username / --password`
  Explicit authentication credentials
- `--api-key`
  API key authentication
- `--no-verify-certs`
  Disable TLS certificate verification
- `--bulk-size`
  Bulk batch size
- `--timeout`
  Request timeout in seconds
- `--refresh`
  Refresh the index after bulk indexing
- `--log-level`
  Logging level
- `--rating-log-every`
  Emit a progress log every N scanned rating rows during item-index building
- `--es-log-every`
  Emit a progress log every N indexed documents

Example: write an existing `jsonl` file into ES

```bash
python3 tools/offline_data_processing/src/jobs/item_index_to_es.py \
  --skip-build \
  --input /Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/item_index.jsonl \
  --input-format jsonl \
  --es-url https://127.0.0.1:59200 \
  --index-name movielens_32m_item_index \
  --ensure-index \
  --no-verify-certs \
  --log-level INFO \
  --es-log-every 5000
```

## Shell Wrapper

File: [build_index_and_write_to_es.sh](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/build_index_and_write_to_es.sh)

This script lists the common parameters near the top of the file and is meant to be used as a convenient local wrapper. The usual flow is:

1. Open the script
2. Adjust the input/output/ES settings near the top if needed
3. Run the script

```bash
tools/offline_data_processing/build_index_and_write_to_es.sh
```

The wrapper builds local JSONL first, then opens an ES port-forward only for the
indexing phase. Override `ES_LOCAL_PORT`, `ES_SERVICE_NAME`, or set
`AUTO_PORT_FORWARD=0` for non-Kubernetes endpoints.

## Workers

### `ItemIndexBuilder`

File: [src/builders/item_index_builder.py](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/src/builders/item_index_builder.py)

Responsibilities:

- Read `movies / tags / links / ratings` CSV files
- Parse title, year, genres, and external IDs
- Aggregate rating statistics
- Compute `top_tags`
- Generate item documents matching [config/schema.json](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/config/schema.json)
- Write output as `jsonl` or `json`

Implementation summary:

- `movies.csv` is the primary table and determines which movies are emitted
- `links.csv` fills `ext.imdb_id / ext.tmdb_id`
- `ratings.csv` is aggregated into `rating.avg / rating.count`
- `tags.csv` is used to generate:
  - `tags.top_tags`
  - `tags.tag_count`
  - `tags.unique_tag_count`
  - `search.tag_text`
- `top_tags` uses a TF-IDF-style score based on user counts, combined with:
  - `top_k`
  - `min_weight`
  - `min_relative_weight`

This worker can be run independently:

```bash
python3 tools/offline_data_processing/src/builders/item_index_builder.py \
  --movies /path/to/movies.csv \
  --tags /path/to/tags.csv \
  --links /path/to/links.csv \
  --ratings /path/to/ratings.csv \
  --output /path/to/item_index.jsonl \
  --output-format jsonl
```

### `ElasticsearchWriter`

File: [src/writers/elasticsearch_writer.py](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/src/writers/elasticsearch_writer.py)

Responsibilities:

- Read an already generated `jsonl/json` file
- Connect to Elasticsearch
- Run preflight checks
- Create the index when needed
- Bulk write documents

Implementation summary:

- Uses the official Python `elasticsearch` client
- Uses `streaming_bulk` for bulk indexing
- Before indexing, it checks:
  - whether the input file exists
  - whether the ES cluster is reachable
  - cluster health
  - whether the target index exists
- If the target index does not exist and `--ensure-index` is not provided, the program refuses to continue to avoid accidental dynamic mapping
- The default mapping file is [config/item_index_mapping.json](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/config/item_index_mapping.json)

This worker can also be run independently:

```bash
python3 tools/offline_data_processing/src/writers/elasticsearch_writer.py \
  --input /path/to/item_index.jsonl \
  --input-format jsonl \
  --es-url https://127.0.0.1:59200 \
  --index-name movielens_32m_item_index \
  --ensure-index \
  --no-verify-certs \
  --es-log-every 5000
```

### `RedisWriter` and `SimilarityDataAdapter`

File: [src/writers/redis_writer.py](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/src/writers/redis_writer.py)

Responsibilities:

- `SimilarityDataAdapter` converts item/user similarity rows into Redis-ready records
- `RedisWriter` writes Redis-ready records and does not inspect item/user fields
- Current Redis-ready records support sorted sets:

```json
{"key": "rec:user_cf:v1:neighbors:1", "data_type": "zset", "values": {"2": 0.42}}
```

### `UserSimilarityBuilder`

File: [src/builders/user_similarity_builder.py](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/src/builders/user_similarity_builder.py)

Responsibilities:

- Read MovieLens-style `ratings.csv`
- Treat ratings at or above `min_rating` as positive feedback
- Build user-user cosine similarity from shared positive items
- Use shard files so the full ratings file does not need to be held in memory
- Cap very popular items with deterministic sampling via `max_item_users`
- Write one JSON row per user:

```json
{"user_id": 1, "neighbors": [{"user_id": 2, "score": 0.42, "cooccurrence": 5}]}
```

The local wrapper uses conservative defaults for the 32M dataset and writes the
generated neighbors into Redis sorted sets under
`rec:user_cf:v1:neighbors:<user_id>` by default:

```bash
tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh
```

Useful overrides:

```bash
tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh --skip-write
tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh --skip-build --dry-run
TOP_K=50 MAX_ITEM_USERS=50 tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh
USE_RATING_WEIGHT=1 tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh
REDIS_DB=1 KEY_PREFIX=rec:user_cf:test:neighbors tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh
REDIS_LOCAL_PORT=56380 tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh
AUTO_PORT_FORWARD=0 REDIS_HOST=localhost REDIS_PORT=6379 tools/offline_data_processing/build_user_similarity_and_write_to_redis.sh
```

## Mapping Notes

The explicit mapping file is: [config/item_index_mapping.json](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/config/item_index_mapping.json)

Important design choices:

- `dynamic: strict`
  - prevents Elasticsearch from silently accepting unexpected fields
- `genres: keyword`
  - optimized for filtering and aggregation
- `ext.imdb_id: keyword`
  - optimized for exact match
- `search.all_text / search.tag_text: text`
  - optimized for full-text search
- `tags.top_tags: nested`
  - preserves the `tag` + `weight` relationship

## Troubleshooting / Notes

### 1. `--ensure-index` only means "create if missing"

Its behavior is:

- if the index does not exist: create it
- if the index already exists: reuse it as-is

It does not update the mapping of an existing index.

If the mapping changes, the usual approach is:

- delete and recreate the old index, or
- create a new index name

### 2. Mapping is critical

Many field-type decisions cannot be changed in place once the index has been created, for example:

- `text -> keyword`
- `object -> nested`

For that reason, confirm [config/item_index_mapping.json](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/config/item_index_mapping.json) before creating the final index.

### 3. `item_index_to_es.py --help` requires dependencies to be installed

`item_index_to_es.py` imports `ElasticsearchWriter`, and `ElasticsearchWriter` depends on the official `elasticsearch` package. If [config/requirements.txt](/Volumes/DataBase/Work/ShootingStar/tools/offline_data_processing/config/requirements.txt) has not been installed yet, `item_index_to_es.py` and `elasticsearch_writer.py` will not run.

### 4. `--no-verify-certs` warnings

If you access ES through:

- `https://127.0.0.1:59200`
- `kubectl port-forward`
- a self-signed certificate

and use `--no-verify-certs`, you will see TLS security warnings. This is expected and does not mean indexing failed.

### 5. K8s `ClusterIP` access pattern

The Elasticsearch cluster is created according to the YAML file ```ci_cd/manifests/k3s/elasticsearch/elasticsearch.yaml``` of my K8s on 3 raspberry-pi, or ```ci_cd/manifests/local/elasticsearch/elasticsearch.yaml``` for local.

If Elasticsearch is still exposed as `ClusterIP`, your local machine usually cannot resolve or access the K8s Service name directly.

The write wrappers handle this by opening a short-lived `kubectl port-forward`
after local build work is complete and before the remote write starts. Defaults:

```bash
ES_LOCAL_PORT=59200
REDIS_LOCAL_PORT=56379
```

For direct Python commands or manual curl checks, open the tunnel yourself:

```bash
kubectl port-forward -n recommendation-engine-es service/item-index-es-http 59200:9200
```

Then access ES locally through:

```bash
https://127.0.0.1:59200
```

You can check if the ES cluster is reachable with:

```bash
curl -u "elastic:$ES_PASSWORD" -k https://127.0.0.1:59200
```

### 6. Prefer a test index first

When connecting to a real ES cluster for the first time, it is safer to:

- cut a small sample file
- write it into a test index
- verify `_count / _mapping / _doc`

After that, import the full dataset into the final index.

### 7. Raw tag data may contain encoding artifacts

During validation we observed a small number of tags that look like historical encoding corruption, such as strings containing `ã§`. This is usually a source-data issue, not an ES indexing issue. If search quality becomes a priority later, tag cleanup can be added as a separate step.

## Suggested Verification Commands

After indexing, these commands are useful:

```bash
curl -u "elastic:$ES_PASSWORD" -k https://127.0.0.1:59200/movielens_32m_item_index/_count?pretty
curl -u "elastic:$ES_PASSWORD" -k https://127.0.0.1:59200/movielens_32m_item_index/_mapping?pretty
curl -u "elastic:$ES_PASSWORD" -k 'https://127.0.0.1:59200/movielens_32m_item_index/_doc/1?pretty'
```
