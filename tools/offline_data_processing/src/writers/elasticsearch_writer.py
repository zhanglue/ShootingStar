#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import logging
import os
from pathlib import Path
from typing import Any, Iterable

from elasticsearch import Elasticsearch
from elasticsearch.helpers import streaming_bulk

ROOT_DIR = Path(__file__).resolve().parents[2]
CONFIG_DIR = ROOT_DIR / "config"


class ElasticsearchWriter:
    DEFAULT_CONFIG: dict[str, Any] = {
        "es_url": "http://localhost:9200",
        "cloud_id": None,
        "index_name": "movielens_32m_rating_index",
        "input_path": ROOT_DIR / "item_index.jsonl",
        "input_format": "jsonl",
        "id_field": "item_id",
        "bulk_size": 500,
        "timeout": 30.0,
        "refresh": False,
        # Only create the target index when it does not already exist.
        # This flag does not update mappings for an existing index.
        "ensure_index": False,
        "mapping": None,
        "mapping_path": CONFIG_DIR / "item_index_mapping.json",
        "username": None,
        "password": None,
        "api_key": None,
        "verify_certs": True,
        "ca_certs": None,
        "max_retries": 3,
        "retry_on_timeout": True,
        "log_level": "INFO",
        "log_every": 5000,
    }

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        merged = dict(self.DEFAULT_CONFIG)
        if config:
            merged.update(config)
        self.config = self._normalize_config(merged)
        self.logger = logging.getLogger(self.__class__.__name__)
        self.client = self._create_client()

    @classmethod
    def _normalize_config(cls, config: dict[str, Any]) -> dict[str, Any]:
        normalized = dict(config)
        normalized["es_url"] = str(normalized["es_url"]).rstrip("/")
        normalized["cloud_id"] = (
            str(normalized["cloud_id"]) if normalized["cloud_id"] else None
        )
        normalized["index_name"] = str(normalized["index_name"])
        normalized["input_path"] = Path(normalized["input_path"])
        normalized["input_format"] = str(normalized["input_format"]).lower()
        if normalized["input_format"] not in {"jsonl", "json"}:
            raise ValueError("input_format must be either 'jsonl' or 'json'")

        normalized["id_field"] = str(normalized["id_field"])
        normalized["bulk_size"] = int(normalized["bulk_size"])
        normalized["timeout"] = float(normalized["timeout"])
        normalized["refresh"] = bool(normalized["refresh"])
        normalized["ensure_index"] = bool(normalized["ensure_index"])
        normalized["mapping_path"] = (
            Path(normalized["mapping_path"]) if normalized["mapping_path"] else None
        )
        normalized["username"] = cls._normalize_optional_string(
            normalized.get("username"),
            env_name="ES_USERNAME",
        )
        normalized["password"] = cls._normalize_optional_string(
            normalized.get("password"),
            env_name="ES_PASSWORD",
        )
        normalized["api_key"] = cls._normalize_optional_string(
            normalized.get("api_key"),
            env_name="ES_API_KEY",
        )
        normalized["verify_certs"] = bool(normalized["verify_certs"])
        normalized["ca_certs"] = (
            Path(normalized["ca_certs"]) if normalized["ca_certs"] else None
        )
        normalized["max_retries"] = int(normalized["max_retries"])
        normalized["retry_on_timeout"] = bool(normalized["retry_on_timeout"])
        normalized["log_level"] = str(normalized["log_level"]).upper()
        normalized["log_every"] = int(normalized["log_every"])
        return normalized

    @staticmethod
    def _normalize_optional_string(
        value: Any,
        env_name: str | None = None,
    ) -> str | None:
        candidate = value
        if candidate in (None, "") and env_name:
            candidate = os.getenv(env_name)
        if candidate in (None, ""):
            return None
        return str(candidate)

    def _create_client(self) -> Elasticsearch:
        client_kwargs: dict[str, Any] = {
            "request_timeout": self.config["timeout"],
            "max_retries": self.config["max_retries"],
            "retry_on_timeout": self.config["retry_on_timeout"],
            "verify_certs": self.config["verify_certs"],
        }

        if self.config["ca_certs"] is not None:
            client_kwargs["ca_certs"] = str(self.config["ca_certs"])

        if self.config["api_key"]:
            client_kwargs["api_key"] = self.config["api_key"]
        elif self.config["username"] and self.config["password"]:
            client_kwargs["basic_auth"] = (
                self.config["username"],
                self.config["password"],
            )

        if self.config["cloud_id"]:
            return Elasticsearch(cloud_id=self.config["cloud_id"], **client_kwargs)

        return Elasticsearch(self.config["es_url"], **client_kwargs)

    def load_mapping(self) -> dict[str, Any] | None:
        mapping = self.config.get("mapping")
        if mapping is not None:
            return mapping

        mapping_path = self.config.get("mapping_path")
        if mapping_path is None:
            return None

        with mapping_path.open(encoding="utf-8") as handle:
            return json.load(handle)

    def ensure_index(self) -> bool:
        index_name = self.config["index_name"]
        if self.client.indices.exists(index=index_name):
            # We intentionally do not try to mutate mappings on an existing index.
            # If mapping changes are needed, create a new index or delete/recreate it.
            if self.config["ensure_index"]:
                self.logger.info(
                    "Target index '%s' already exists; skipping index creation.",
                    index_name,
                )
            return False

        if not self.config["ensure_index"]:
            raise RuntimeError(
                f"Target index '{index_name}' does not exist. "
                "Refusing to rely on dynamic mapping; re-run with --ensure-index "
                "or create the index explicitly first."
            )

        mapping = self.load_mapping() or {}
        mapping_path = self.config.get("mapping_path")
        if mapping_path is not None:
            self.logger.info(
                "Creating index '%s' using mapping file %s.",
                index_name,
                mapping_path,
            )
        else:
            self.logger.info("Creating index '%s' using inline/default mapping.", index_name)
        self.client.indices.create(index=index_name, **mapping)
        self.logger.info("Created index '%s'.", index_name)
        return True

    def iter_documents(self) -> Iterable[dict[str, Any]]:
        input_path = self.config["input_path"]
        if self.config["input_format"] == "json":
            with input_path.open(encoding="utf-8") as handle:
                data = json.load(handle)
            for document in data:
                yield document
            return

        with input_path.open(encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                yield json.loads(line)

    def iter_bulk_actions(
        self, documents: Iterable[dict[str, Any]]
    ) -> Iterable[dict[str, Any]]:
        for document in documents:
            action: dict[str, Any] = {
                "_op_type": "index",
                "_index": self.config["index_name"],
                "_source": document,
            }
            document_id = document.get(self.config["id_field"])
            if document_id is not None:
                action["_id"] = document_id
            yield action

    @staticmethod
    def _format_bytes(size: int) -> str:
        units = ["B", "KB", "MB", "GB", "TB"]
        value = float(size)
        for unit in units:
            if value < 1024.0 or unit == units[-1]:
                if unit == "B":
                    return f"{int(value)}{unit}"
                return f"{value:.1f}{unit}"
            value /= 1024.0
        return f"{size}B"

    def _log_input_file(self) -> None:
        input_path = self.config["input_path"]
        if not input_path.exists():
            raise FileNotFoundError(f"Input file does not exist: {input_path}")
        if not input_path.is_file():
            raise RuntimeError(f"Input path is not a regular file: {input_path}")

        file_size = input_path.stat().st_size
        if file_size <= 0:
            raise RuntimeError(f"Input file is empty: {input_path}")

        self.logger.info(
            "Input file: %s (%s, format=%s).",
            input_path,
            self._format_bytes(file_size),
            self.config["input_format"],
        )

    def _log_cluster_status(self) -> None:
        info = self.client.info()
        version = info.get("version", {}).get("number", "unknown")
        cluster_name = info.get("cluster_name", "unknown")
        self.logger.info(
            "Connected to Elasticsearch cluster '%s' (version %s).",
            cluster_name,
            version,
        )

        health = self.client.cluster.health()
        self.logger.info(
            "Cluster health: status=%s, nodes=%s, data_nodes=%s.",
            health.get("status"),
            health.get("number_of_nodes"),
            health.get("number_of_data_nodes"),
        )

    def _log_index_status(self) -> None:
        count = self.client.count(index=self.config["index_name"]).get("count", 0)
        self.logger.info(
            "Target index '%s' currently contains %s documents.",
            self.config["index_name"],
            count,
        )

    def preflight_for_file(self) -> None:
        self.logger.info("Starting Elasticsearch preflight for file-based indexing.")
        self._log_input_file()
        self._log_cluster_status()
        self.ensure_index()
        self._log_index_status()

    def preflight_for_items(self, item_count: int) -> None:
        self.logger.info("Starting Elasticsearch preflight for in-memory indexing.")
        self.logger.info("Received %s in-memory documents for indexing.", item_count)
        self._log_cluster_status()
        self.ensure_index()
        self._log_index_status()

    def _bulk_write(self, documents: Iterable[dict[str, Any]]) -> int:
        indexed_count = 0
        progress_interval = max(self.config["bulk_size"], self.config["log_every"])
        next_progress = progress_interval
        for ok, result in streaming_bulk(
            client=self.client,
            actions=self.iter_bulk_actions(documents),
            chunk_size=self.config["bulk_size"],
            max_retries=self.config["max_retries"],
            raise_on_error=False,
            raise_on_exception=False,
        ):
            if not ok:
                raise RuntimeError(f"Bulk indexing failed: {result}")
            indexed_count += 1
            if indexed_count >= next_progress:
                self.logger.info(
                    "Indexed %s documents into '%s' so far.",
                    indexed_count,
                    self.config["index_name"],
                )
                next_progress += progress_interval

        if self.config["refresh"]:
            self.client.indices.refresh(index=self.config["index_name"])
            self.logger.info("Refreshed index '%s' after bulk write.", self.config["index_name"])

        self.logger.info(
            "Finished bulk write: %s documents indexed into '%s'.",
            indexed_count,
            self.config["index_name"],
        )
        self._log_index_status()

        return indexed_count

    def write_file(self) -> int:
        self.logger.info("ElasticsearchWriter file write started.")
        self.preflight_for_file()
        return self._bulk_write(self.iter_documents())

    def write_items(self, items: list[dict[str, Any]]) -> int:
        self.logger.info("ElasticsearchWriter in-memory write started.")
        self.preflight_for_items(len(items))
        return self._bulk_write(items)

    def run(self) -> int:
        self.logger.info("ElasticsearchWriter run started.")
        return self.write_file()

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
        parser = argparse.ArgumentParser(
            description="Write item documents into Elasticsearch."
        )
        parser.add_argument("--es-url", dest="es_url", help="Elasticsearch base URL.")
        parser.add_argument(
            "--cloud-id",
            dest="cloud_id",
            help="Optional Elastic Cloud ID. If set, it takes precedence over --es-url.",
        )
        parser.add_argument("--index-name", dest="index_name", help="Index name.")
        parser.add_argument(
            "--input", dest="input_path", help="Path to the generated item data file."
        )
        parser.add_argument(
            "--input-format",
            dest="input_format",
            choices=("jsonl", "json"),
            help="Whether the input file is JSON Lines or a JSON array.",
        )
        parser.add_argument(
            "--id-field",
            dest="id_field",
            help="Document field to use as Elasticsearch _id.",
        )
        parser.add_argument(
            "--bulk-size",
            dest="bulk_size",
            type=int,
            help="Number of documents per bulk request.",
        )
        parser.add_argument(
            "--timeout",
            dest="timeout",
            type=float,
            help="Request timeout in seconds.",
        )
        parser.add_argument(
            "--refresh",
            dest="refresh",
            action="store_true",
            help="Refresh the index after bulk requests complete.",
        )
        parser.add_argument(
            "--ensure-index",
            dest="ensure_index",
            action="store_true",
            help=(
                "Create the target index if it does not exist yet. "
                "This does not modify mappings for an existing index."
            ),
        )
        parser.add_argument(
            "--mapping-path",
            dest="mapping_path",
            help="Optional JSON file used when creating the index.",
        )
        parser.add_argument(
            "--username",
            dest="username",
            help="Optional basic auth username.",
        )
        parser.add_argument(
            "--password",
            dest="password",
            help="Optional basic auth password.",
        )
        parser.add_argument(
            "--api-key",
            dest="api_key",
            help="Optional Elasticsearch API key.",
        )
        parser.add_argument(
            "--no-verify-certs",
            dest="verify_certs",
            action="store_false",
            help="Disable TLS certificate verification.",
        )
        parser.add_argument(
            "--ca-certs",
            dest="ca_certs",
            help="Path to a custom CA bundle.",
        )
        parser.add_argument(
            "--max-retries",
            dest="max_retries",
            type=int,
            help="Maximum retries for bulk/index requests.",
        )
        parser.add_argument(
            "--no-retry-on-timeout",
            dest="retry_on_timeout",
            action="store_false",
            help="Disable retry when request timeout occurs.",
        )
        parser.add_argument(
            "--log-level",
            dest="log_level",
            choices=("DEBUG", "INFO", "WARNING", "ERROR"),
            help="Logging level for writer execution.",
        )
        parser.add_argument(
            "--log-every",
            dest="log_every",
            type=int,
            help="Emit a progress log every N indexed documents.",
        )
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        config: dict[str, Any] = {}
        for key, value in vars(args).items():
            if key in {"verify_certs", "retry_on_timeout"}:
                config[key] = value
            elif value is not None and value is not False:
                config[key] = value
        return config


def main() -> None:
    parser = ElasticsearchWriter.build_arg_parser()
    args = parser.parse_args()
    log_level = getattr(logging, str(getattr(args, "log_level", "INFO")).upper(), logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    writer = ElasticsearchWriter(ElasticsearchWriter.config_from_args(args))
    indexed_count = writer.run()
    location = writer.config["cloud_id"] or writer.config["es_url"]
    print(
        f"Indexed {indexed_count} documents into "
        f"{location}/{writer.config['index_name']}"
    )


if __name__ == "__main__":
    main()
