# Item Index on Elasticsearch

This directory is used for two things:

1. Build movie item-index documents from MovieLens-style CSV files.
2. Write the generated documents into Elasticsearch.

The current default target index name is `movielens_32m_rating_index`.

## Data Source

This toolchain is currently built and validated against the MovieLens 32M dataset:

> MovieLens Datasets: https://grouplens.org/datasets/movielens

Here is the [README](https://files.grouplens.org/datasets/movielens/ml-32m-README.html) of the 32M dataset. Thanks to the GroupLens team for providing this high-quality dataset for research purposes.


## Folder Layout

```text
tools/item_index_es/
├── build_index_and_write_to_es.sh
├── config/
│   ├── item_index_mapping.json
│   ├── requirements.txt
│   └── schema.json
└── src/
    ├── item_index_builder.py
    ├── elasticsearch_writer.py
    └── main.py
```

- `config/` stores configuration-like files.
- `src/` stores the Python implementation.
- `build_index_and_write_to_es.sh` is the one-command local entrypoint.

## Environment

Create a dedicated environment and install dependencies first:

```bash
conda create -n item_index_es python=3.11 -y
conda activate item_index_es
pip install -r tools/item_index_es/config/requirements.txt
```

If you need to write to Elasticsearch, prepare credentials via environment variables:

```bash
export ES_USERNAME=elastic
export ES_PASSWORD='your-password'
```

## Main Entry

File: [src/main.py](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/src/main.py)

`main.py` orchestrates the two workers:

- build phase: calls `ItemIndexBuilder`
- index phase: calls `ElasticsearchWriter`

It supports two common modes:

1. Build from CSV and write directly into ES
2. Skip build and write an existing `jsonl` file into ES

Common entrypoint parameters:

- `--skip-build`
  Skip document building and only write an existing file to ES
- `--skip-index`
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
- `--log-every`
  Emit a progress log every N indexed documents

Example: write an existing `jsonl` file into ES

```bash
python3 tools/item_index_es/src/main.py \
  --skip-build \
  --input /Volumes/DataBase/Work/ShootingStar/tools/item_index_es/item_index.jsonl \
  --input-format jsonl \
  --es-url https://localhost:9200 \
  --index-name movielens_32m_rating_index \
  --ensure-index \
  --no-verify-certs \
  --log-level INFO \
  --log-every 5000
```

## Shell Wrapper

File: [build_index_and_write_to_es.sh](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/build_index_and_write_to_es.sh)

This script lists the common parameters near the top of the file and is meant to be used as a convenient local wrapper. The usual flow is:

1. Open the script
2. Adjust the input/output/ES settings near the top
3. Run the script

```bash
tools/item_index_es/build_index_and_write_to_es.sh
```

## Workers

### `ItemIndexBuilder`

File: [src/item_index_builder.py](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/src/item_index_builder.py)

Responsibilities:

- Read `movies / tags / links / ratings` CSV files
- Parse title, year, genres, and external IDs
- Aggregate rating statistics
- Compute `top_tags`
- Generate item documents matching [config/schema.json](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/config/schema.json)
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
python3 tools/item_index_es/src/item_index_builder.py \
  --movies /path/to/movies.csv \
  --tags /path/to/tags.csv \
  --links /path/to/links.csv \
  --ratings /path/to/ratings.csv \
  --output /path/to/item_index.jsonl \
  --output-format jsonl
```

### `ElasticsearchWriter`

File: [src/elasticsearch_writer.py](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/src/elasticsearch_writer.py)

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
- The default mapping file is [config/item_index_mapping.json](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/config/item_index_mapping.json)

This worker can also be run independently:

```bash
python3 tools/item_index_es/src/elasticsearch_writer.py \
  --input /path/to/item_index.jsonl \
  --input-format jsonl \
  --es-url https://localhost:9200 \
  --index-name movielens_32m_rating_index \
  --ensure-index \
  --no-verify-certs
```

## Mapping Notes

The explicit mapping file is: [config/item_index_mapping.json](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/config/item_index_mapping.json)

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

For that reason, confirm [config/item_index_mapping.json](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/config/item_index_mapping.json) before creating the final index.

### 3. `main.py --help` requires dependencies to be installed

`main.py` imports `ElasticsearchWriter`, and `ElasticsearchWriter` depends on the official `elasticsearch` package. If [config/requirements.txt](/Volumes/DataBase/Work/ShootingStar/tools/item_index_es/config/requirements.txt) has not been installed yet, `main.py` and `elasticsearch_writer.py` will not run.

### 4. `--no-verify-certs` warnings

If you access ES through:

- `https://localhost:9200`
- `kubectl port-forward`
- a self-signed certificate

and use `--no-verify-certs`, you will see TLS security warnings. This is expected and does not mean indexing failed.

### 5. K8s `ClusterIP` access pattern

The Elasticsearch cluster is created according to the YAML file ```ci_cd/manifests/k3s/elasticsearch/elasticsearch.yaml``` of my K8s on 3 raspberry-pi, or ```ci_cd/manifests/local/elasticsearch/elasticsearch.yaml``` for local.

If Elasticsearch is still exposed as `ClusterIP`, your local machine usually cannot resolve or access the K8s Service name directly.

A common workflow is:

```bash
kubectl port-forward -n recommendation-engine-es service/item-index-es-http 9200
```

Then access ES locally through:

```bash
https://localhost:9200
```

You can check if the ES cluster is reachable with:

```bashbash
curl -u "elastic:$ES_PASSWORD" -k https://localhost:9200
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
curl -u "elastic:$ES_PASSWORD" -k https://localhost:9200/movielens_32m_rating_index/_count?pretty
curl -u "elastic:$ES_PASSWORD" -k https://localhost:9200/movielens_32m_rating_index/_mapping?pretty
curl -u "elastic:$ES_PASSWORD" -k 'https://localhost:9200/movielens_32m_rating_index/_doc/1?pretty'
```
