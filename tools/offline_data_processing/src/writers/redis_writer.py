#!/usr/bin/env python3
"""
Write Redis-ready records into Redis.
"""
from __future__ import annotations

import argparse
import json
import logging
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping


ROOT_DIR = Path(__file__).resolve().parents[2]
REDIS_ZSET_SCHEMA = "redis-zset"


@dataclass(frozen=True)
class RedisData:
    """
    One Redis write unit.

    Only sorted sets are implemented today. The explicit data_type field keeps
    the writer dispatchable when other Redis structures are needed later.
    """

    key: str
    data_type: str
    values: Mapping[str, float]

    @classmethod
    def zset(cls, key: str, values: Mapping[str, float]) -> "RedisData":
        """
        Create a sorted-set write unit.
        """
        return cls(key=key, data_type="zset", values=values)

    def __post_init__(self) -> None:
        """
        Normalize the immutable record into writer-friendly primitives.
        """
        key = str(self.key).strip()
        data_type = str(self.data_type).strip().lower()
        if not key:
            raise ValueError("RedisData key must not be empty")
        if not data_type:
            raise ValueError("RedisData data_type must not be empty")

        object.__setattr__(self, "key", key)
        object.__setattr__(self, "data_type", data_type)
        if data_type == "zset":
            object.__setattr__(
                self,
                "values",
                {str(member): float(score) for member, score in self.values.items()},
            )


@dataclass(frozen=True)
class RedisWriteStats:
    """
    Counters returned after validating or writing Redis records.
    """

    record_count: int
    value_count: int


class SimilarityDataAdapter:
    """
    Adapt item/user similarity rows into Redis sorted-set records.

    Builders intentionally emit business-level similarity rows. This adapter is
    the boundary that turns those rows into RedisData, leaving RedisWriter free
    of item/user schema knowledge.
    """

    INPUT_SCHEMAS = ("item-similarity", "user-similarity")
    DEFAULT_CONFIG: dict[str, Any] = {
        "input_schema": "item-similarity",
        "input_path": ROOT_DIR / "item_similarity.jsonl",
        "input_format": "jsonl",
        "key_prefix": "rec:item_cf:v1:neighbors",
        "entity_id_field": "item_id",
        "neighbor_id_field": "item_id",
    }
    PRESET_CONFIGS: dict[str, dict[str, Any]] = {
        "item-similarity": {
            "input_path": ROOT_DIR / "item_similarity.jsonl",
            "key_prefix": "rec:item_cf:v1:neighbors",
            "entity_id_field": "item_id",
            "neighbor_id_field": "item_id",
        },
        "user-similarity": {
            "input_path": ROOT_DIR / "user_similarity.jsonl",
            "key_prefix": "rec:user_cf:v1:neighbors",
            "entity_id_field": "user_id",
            "neighbor_id_field": "user_id",
        },
    }

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        """
        Merge caller overrides with schema defaults and normalize values.
        """
        incoming = dict(config or {})
        input_schema = str(incoming.get("input_schema", self.DEFAULT_CONFIG["input_schema"]))
        if input_schema not in self.INPUT_SCHEMAS:
            raise ValueError(
                f"input_schema must be one of {self.INPUT_SCHEMAS}, got {input_schema!r}"
            )

        merged = dict(self.DEFAULT_CONFIG)
        merged.update(self.PRESET_CONFIGS[input_schema])
        merged.update(incoming)
        self.config = self._normalize_config(merged)

    @classmethod
    def _normalize_config(cls, config: dict[str, Any]) -> dict[str, Any]:
        """
        Coerce CLI/env-style values into adapter settings.
        """
        normalized = dict(config)
        normalized["input_schema"] = str(normalized["input_schema"])
        if normalized["input_schema"] not in cls.INPUT_SCHEMAS:
            raise ValueError(
                f"input_schema must be one of {cls.INPUT_SCHEMAS}, "
                f"got {normalized['input_schema']!r}"
            )
        normalized["input_path"] = Path(normalized["input_path"])
        normalized["input_format"] = str(normalized["input_format"]).lower()
        if normalized["input_format"] not in {"jsonl", "json"}:
            raise ValueError("input_format must be either 'jsonl' or 'json'")
        normalized["key_prefix"] = str(normalized["key_prefix"]).rstrip(":")
        normalized["entity_id_field"] = str(normalized["entity_id_field"]).strip()
        normalized["neighbor_id_field"] = str(normalized["neighbor_id_field"]).strip()

        if not normalized["key_prefix"]:
            raise ValueError("key_prefix must not be empty")
        if not normalized["entity_id_field"]:
            raise ValueError("entity_id_field must not be empty")
        if not normalized["neighbor_id_field"]:
            raise ValueError("neighbor_id_field must not be empty")
        return normalized

    @classmethod
    def build_arg_parser(cls, include_redis_data_schema: bool = False) -> argparse.ArgumentParser:
        """
        Create the CLI parser for input files and similarity-to-Redis adaptation.
        """
        schema_choices = list(cls.INPUT_SCHEMAS)
        input_schema_help = "Input document schema."
        if include_redis_data_schema:
            schema_choices.append(REDIS_ZSET_SCHEMA)
            input_schema_help = (
                "Input document schema. Use redis-zset when the file already contains "
                "RedisData records."
            )

        parser = argparse.ArgumentParser(
            description="Adapt generated similarity data into Redis write records."
        )
        parser.add_argument(
            "--input-schema",
            dest="input_schema",
            choices=tuple(schema_choices),
            help=input_schema_help,
        )
        parser.add_argument(
            "--input",
            dest="input_path",
            help="Path to generated similarity data or RedisData records.",
        )
        parser.add_argument(
            "--input-format",
            dest="input_format",
            choices=("jsonl", "json"),
            help="Whether the input file is JSON Lines or a JSON array.",
        )
        parser.add_argument(
            "--key-prefix",
            dest="key_prefix",
            help="Redis key prefix. Final similarity key is '<prefix>:<entity_id>'.",
        )
        parser.add_argument(
            "--entity-id-field",
            dest="entity_id_field",
            help="Top-level similarity field containing the source entity id.",
        )
        parser.add_argument(
            "--neighbor-id-field",
            dest="neighbor_id_field",
            help="Similarity neighbor field containing the neighbor entity id.",
        )
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        """
        Return only CLI arguments that should override adapter defaults.
        """
        config: dict[str, Any] = {}
        for key in cls.DEFAULT_CONFIG:
            value = getattr(args, key, None)
            if value is not None and value is not False:
                config[key] = value
        return config

    def iter_redis_data(self) -> Iterable[RedisData]:
        """
        Stream RedisData records from the configured similarity input file.
        """
        self._validate_input_file()
        for document in iter_json_documents(
            self.config["input_path"],
            self.config["input_format"],
        ):
            yield self.to_redis_data(document)

    def to_redis_data(self, document: dict[str, Any]) -> RedisData:
        """
        Convert one item/user similarity row into one Redis ZSET write unit.
        """
        entity_id = self._entity_id(document)
        key = f"{self.config['key_prefix']}:{entity_id}"
        return RedisData.zset(key, self._neighbor_score_mapping(document))

    def _validate_input_file(self) -> None:
        """
        Fail fast if the input file is missing, not a file, or empty.
        """
        input_path = self.config["input_path"]
        if not input_path.exists():
            raise FileNotFoundError(f"Input file does not exist: {input_path}")
        if not input_path.is_file():
            raise RuntimeError(f"Input path is not a regular file: {input_path}")
        if input_path.stat().st_size <= 0:
            raise RuntimeError(f"Input file is empty: {input_path}")

    def _entity_id(self, document: dict[str, Any]) -> int:
        """
        Extract and normalize the source entity id from one similarity row.
        """
        field_name = self.config["entity_id_field"]
        if field_name not in document:
            raise ValueError(f"Similarity document is missing '{field_name}': {document}")
        return int(document[field_name])

    def _neighbor_score_mapping(self, document: dict[str, Any]) -> dict[str, float]:
        """
        Convert one row's neighbors into the member->score mapping Redis expects.
        """
        field_name = self.config["neighbor_id_field"]
        score_mapping: dict[str, float] = {}
        for neighbor in document.get("neighbors", []):
            if field_name not in neighbor:
                raise ValueError(f"Similarity neighbor is missing '{field_name}': {neighbor}")
            score_mapping[str(int(neighbor[field_name]))] = float(neighbor["score"])
        return score_mapping


class RedisWriter:
    """
    Write RedisData records into Redis.
    """

    DEFAULT_CONFIG: dict[str, Any] = {
        "redis_host": "localhost",
        "redis_port": 6379,
        "redis_db": 0,
        "username": None,
        "password": None,
        "ssl": False,
        "socket_timeout": 5.0,
        "batch_size": 500,
        "expire_seconds": 0,
        "replace_existing": True,
        "dry_run": False,
        "log_level": "INFO",
        "redis_log_every": 300000,
    }

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        """
        Merge caller overrides with defaults and normalize config values.
        """
        merged = dict(self.DEFAULT_CONFIG)
        if config:
            merged.update(config)
        self.config = self._normalize_config(merged)
        self.logger = logging.getLogger(self.__class__.__name__)

    @classmethod
    def _normalize_config(cls, config: dict[str, Any]) -> dict[str, Any]:
        """
        Coerce CLI/env-style values into Redis writer settings.
        """
        normalized = dict(config)
        normalized["redis_host"] = str(normalized["redis_host"])
        normalized["redis_port"] = int(normalized["redis_port"])
        normalized["redis_db"] = int(normalized["redis_db"])
        normalized["username"] = cls._normalize_optional_string(
            normalized.get("username"),
            env_name="REDIS_USERNAME",
        )
        normalized["password"] = cls._normalize_optional_string(
            normalized.get("password"),
            env_name="REDIS_PASSWORD",
        )
        normalized["ssl"] = bool(normalized["ssl"])
        normalized["socket_timeout"] = float(normalized["socket_timeout"])
        normalized["batch_size"] = int(normalized["batch_size"])
        normalized["expire_seconds"] = int(normalized["expire_seconds"])
        normalized["replace_existing"] = bool(normalized["replace_existing"])
        normalized["dry_run"] = bool(normalized["dry_run"])
        normalized["log_level"] = str(normalized["log_level"]).upper()
        normalized["redis_log_every"] = int(normalized["redis_log_every"])

        if normalized["batch_size"] <= 0:
            raise ValueError("batch_size must be positive")
        if normalized["redis_log_every"] <= 0:
            raise ValueError("redis_log_every must be positive")
        return normalized

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
        """
        Create the standalone CLI parser for Redis writes.
        """
        parser = argparse.ArgumentParser(
            description="Write RedisData records into Redis."
        )
        parser.add_argument("--redis-host", dest="redis_host", help="Redis host.")
        parser.add_argument("--redis-port", dest="redis_port", type=int, help="Redis port.")
        parser.add_argument("--redis-db", dest="redis_db", type=int, help="Redis database.")
        parser.add_argument("--username", dest="username", help="Optional Redis username.")
        parser.add_argument("--password", dest="password", help="Optional Redis password.")
        parser.add_argument("--ssl", dest="ssl", action="store_true", help="Use Redis TLS.")
        parser.add_argument(
            "--socket-timeout",
            dest="socket_timeout",
            type=float,
            help="Redis socket timeout in seconds.",
        )
        parser.add_argument(
            "--batch-size",
            dest="batch_size",
            type=int,
            help="Number of RedisData records per Redis pipeline execution.",
        )
        parser.add_argument(
            "--expire-seconds",
            dest="expire_seconds",
            type=int,
            help="Optional TTL for each written key. <=0 means no TTL.",
        )
        parser.add_argument(
            "--no-replace-existing",
            dest="replace_existing",
            action="store_false",
            help="Do not delete a key before writing its new values.",
        )
        parser.add_argument(
            "--dry-run",
            dest="dry_run",
            action="store_true",
            help="Read and validate input, but do not connect to Redis.",
        )
        parser.add_argument(
            "--log-level",
            dest="log_level",
            choices=("DEBUG", "INFO", "WARNING", "ERROR"),
            help="Logging level for writer execution.",
        )
        parser.add_argument(
            "--debug",
            dest="debug",
            action="store_true",
            help="Shortcut for --log-level DEBUG.",
        )
        parser.add_argument(
            "--redis-log-every",
            dest="redis_log_every",
            type=int,
            help="Emit a progress log every N written RedisData records.",
        )
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        """
        Return only CLI arguments that should override writer defaults.
        """
        config: dict[str, Any] = {}
        for key in cls.DEFAULT_CONFIG:
            value = getattr(args, key, None)
            if key == "log_level":
                if getattr(args, "debug", False):
                    config[key] = "DEBUG"
                elif value is not None:
                    config[key] = value
            elif key in {"ssl", "replace_existing", "dry_run"}:
                if value is not None:
                    config[key] = value
            elif value is not None and value is not False:
                config[key] = value
        return config

    @staticmethod
    def _normalize_optional_string(
        value: Any,
        env_name: str | None = None,
    ) -> str | None:
        """
        Read optional auth values from explicit values first, then env vars.
        """
        candidate = value
        if candidate in (None, "") and env_name:
            candidate = os.getenv(env_name)
        if candidate in (None, ""):
            return None
        return str(candidate)

    def run(self, records: Iterable[RedisData]) -> RedisWriteStats:
        """
        Validate records and run the configured write path.
        """
        self.logger.info("RedisWriter run started.")
        return self.write_records(records)

    def write_records(self, records: Iterable[RedisData]) -> RedisWriteStats:
        """
        Validate or write RedisData records in Redis pipeline batches.
        """
        if self.config["dry_run"]:
            stats = RedisWriteStats(record_count=0, value_count=0)
            for record in records:
                self._validate_record(record)
                stats = RedisWriteStats(
                    record_count=stats.record_count + 1,
                    value_count=stats.value_count + self._record_value_count(record),
                )
            self.logger.info(
                "Dry run: would write %s values across %s Redis records.",
                stats.value_count,
                stats.record_count,
            )
            return stats

        client = self._create_client()
        client.ping()
        self.logger.info(
            "Connected to Redis at %s:%s db=%s.",
            self.config["redis_host"],
            self.config["redis_port"],
            self.config["redis_db"],
        )

        total = RedisWriteStats(record_count=0, value_count=0)
        batch: list[RedisData] = []

        for record in records:
            self._validate_record(record)
            batch.append(record)
            if len(batch) < self.config["batch_size"]:
                continue

            stats = self._write_batch(client, batch)
            total = RedisWriteStats(
                record_count=total.record_count + stats.record_count,
                value_count=total.value_count + stats.value_count,
            )
            if total.record_count % self.config["redis_log_every"] == 0:
                self.logger.info(
                    "Wrote %s Redis records and %s values so far.",
                    total.record_count,
                    total.value_count,
                )
            batch = []

        if batch:
            stats = self._write_batch(client, batch)
            total = RedisWriteStats(
                record_count=total.record_count + stats.record_count,
                value_count=total.value_count + stats.value_count,
            )

        self.logger.info(
            "Finished Redis write: %s values across %s Redis records.",
            total.value_count,
            total.record_count,
        )
        return total

    def _create_client(self) -> Any:
        """
        Create the Redis client lazily so dry-run mode needs no connection.
        """
        try:
            import redis
        except ImportError as exc:
            raise RuntimeError(
                "redis package is required for writing. "
                "Install tools/offline_data_processing/config/requirements.txt first."
            ) from exc

        return redis.Redis(
            host=self.config["redis_host"],
            port=self.config["redis_port"],
            db=self.config["redis_db"],
            username=self.config["username"],
            password=self.config["password"],
            ssl=self.config["ssl"],
            socket_timeout=self.config["socket_timeout"],
            decode_responses=True,
        )

    def _validate_record(self, record: RedisData) -> None:
        """
        Validate a RedisData record before dry-run counting or writing.
        """
        if not isinstance(record, RedisData):
            raise TypeError(f"RedisWriter expects RedisData records, got {type(record)!r}")
        if record.data_type != "zset":
            raise ValueError(f"Unsupported RedisData data_type: {record.data_type}")

    def _record_value_count(self, record: RedisData) -> int:
        """
        Return the number of values that will be written for one record.
        """
        if record.data_type == "zset":
            return len(record.values)
        raise ValueError(f"Unsupported RedisData data_type: {record.data_type}")

    def _write_batch(self, client: Any, batch: list[RedisData]) -> RedisWriteStats:
        """
        Write one batch of RedisData records through a Redis pipeline.
        """
        pipeline = client.pipeline(transaction=False)
        record_count = 0
        value_count = 0

        for record in batch:
            if self.config["replace_existing"]:
                pipeline.delete(record.key)

            if record.data_type == "zset":
                if record.values:
                    pipeline.zadd(record.key, dict(record.values))
                    value_count += len(record.values)
            else:
                raise ValueError(f"Unsupported RedisData data_type: {record.data_type}")

            if self.config["expire_seconds"] > 0:
                pipeline.expire(record.key, self.config["expire_seconds"])

            record_count += 1

        pipeline.execute()
        return RedisWriteStats(record_count=record_count, value_count=value_count)


def iter_json_documents(input_path: Path, input_format: str) -> Iterable[dict[str, Any]]:
    """
    Stream JSON documents from JSON Lines or a JSON array file.
    """
    input_path = Path(input_path)
    normalized_format = str(input_format).lower()
    if normalized_format == "json":
        with input_path.open(encoding="utf-8") as handle:
            data = json.load(handle)
        for document in data:
            yield document
        return

    if normalized_format != "jsonl":
        raise ValueError("input_format must be either 'jsonl' or 'json'")

    with input_path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            yield json.loads(line)


def validate_input_file(input_path: Path) -> None:
    """
    Fail fast if the input file is missing, not a file, or empty.
    """
    input_path = Path(input_path)
    if not input_path.exists():
        raise FileNotFoundError(f"Input file does not exist: {input_path}")
    if not input_path.is_file():
        raise RuntimeError(f"Input path is not a regular file: {input_path}")
    if input_path.stat().st_size <= 0:
        raise RuntimeError(f"Input file is empty: {input_path}")


def redis_data_from_document(document: dict[str, Any]) -> RedisData:
    """
    Convert one RedisData JSON document into a RedisData object.
    """
    key = str(document["key"])
    data_type = str(document.get("data_type", document.get("type", "zset"))).lower()
    if data_type != "zset":
        raise ValueError(f"Unsupported RedisData data_type: {data_type}")

    raw_values = document.get("values", document.get("members"))
    if raw_values is None:
        raise ValueError(f"RedisData document is missing values/members: {document}")
    if isinstance(raw_values, dict):
        values = {str(member): float(score) for member, score in raw_values.items()}
    else:
        values = {
            str(value["member"]): float(value["score"])
            for value in raw_values
        }
    return RedisData.zset(key, values)


def iter_redis_data_file(input_path: Path, input_format: str) -> Iterable[RedisData]:
    """
    Stream RedisData records from a Redis-ready JSON file.
    """
    validate_input_file(input_path)
    for document in iter_json_documents(input_path, input_format):
        yield redis_data_from_document(document)


def build_arg_parser() -> argparse.ArgumentParser:
    """
    Build the module CLI parser.
    """
    parser = argparse.ArgumentParser(
        description="Write RedisData records or adapted similarity data into Redis."
    )
    existing_option_strings = set()

    def add_actions_from(source_parser: argparse.ArgumentParser) -> None:
        for action in source_parser._actions:
            if action.dest == "help":
                continue
            option_strings = set(action.option_strings)
            if option_strings and option_strings & existing_option_strings:
                continue
            parser._add_action(action)
            existing_option_strings.update(option_strings)

    add_actions_from(SimilarityDataAdapter.build_arg_parser(include_redis_data_schema=True))
    add_actions_from(RedisWriter.build_arg_parser())
    return parser


def main() -> None:
    """
    CLI entrypoint for running the Redis writer.
    """
    parser = build_arg_parser()
    args = parser.parse_args()
    log_level_name = (
        "DEBUG" if getattr(args, "debug", False) else str(getattr(args, "log_level", "INFO")).upper()
    )
    log_level = getattr(logging, log_level_name, logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    adapter_config = SimilarityDataAdapter.config_from_args(args)
    input_schema = str(adapter_config.get("input_schema", SimilarityDataAdapter.DEFAULT_CONFIG["input_schema"]))
    if input_schema == REDIS_ZSET_SCHEMA:
        input_path = Path(adapter_config.get("input_path", ROOT_DIR / "redis_data.jsonl"))
        input_format = str(adapter_config.get("input_format", "jsonl"))
        records = iter_redis_data_file(input_path, input_format)
    else:
        adapter = SimilarityDataAdapter(adapter_config)
        records = adapter.iter_redis_data()

    writer = RedisWriter(RedisWriter.config_from_args(args))
    stats = writer.run(records)
    print(
        f"Wrote {stats.value_count} Redis values across "
        f"{stats.record_count} Redis records."
    )


if __name__ == "__main__":
    # Direct item similarity run example, after port-forwarding Redis to localhost:56379:
    #   REDIS_PASSWORD=... \
    #   python3 tools/offline_data_processing/src/writers/redis_writer.py \
    #     --input tools/offline_data_processing/item_similarity.jsonl \
    #     --input-schema item-similarity \
    #     --redis-host 127.0.0.1 --redis-port 56379 --redis-db 0
    main()
