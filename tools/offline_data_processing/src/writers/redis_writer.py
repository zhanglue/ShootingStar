#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import logging
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


ROOT_DIR = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class RedisWriteStats:
    item_count: int
    neighbor_count: int


class RedisWriter:
    DEFAULT_CONFIG: dict[str, Any] = {
        "redis_host": "localhost",
        "redis_port": 6379,
        "redis_db": 0,
        "input_path": ROOT_DIR / "item_similarity.jsonl",
        "input_format": "jsonl",
        "key_prefix": "rec:item_cf:v1:neighbors",
        "username": None,
        "password": None,
        "ssl": False,
        "socket_timeout": 5.0,
        "batch_size": 500,
        "expire_seconds": 0,
        "replace_existing": True,
        "dry_run": False,
        "log_level": "INFO",
        "log_every": 5000,
    }

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        merged = dict(self.DEFAULT_CONFIG)
        if config:
            merged.update(config)
        self.config = self._normalize_config(merged)
        self.logger = logging.getLogger(self.__class__.__name__)

    @classmethod
    def _normalize_config(cls, config: dict[str, Any]) -> dict[str, Any]:
        normalized = dict(config)
        normalized["redis_host"] = str(normalized["redis_host"])
        normalized["redis_port"] = int(normalized["redis_port"])
        normalized["redis_db"] = int(normalized["redis_db"])
        normalized["input_path"] = Path(normalized["input_path"])
        normalized["input_format"] = str(normalized["input_format"]).lower()
        if normalized["input_format"] not in {"jsonl", "json"}:
            raise ValueError("input_format must be either 'jsonl' or 'json'")

        normalized["key_prefix"] = str(normalized["key_prefix"]).rstrip(":")
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
        normalized["log_every"] = int(normalized["log_every"])

        if normalized["batch_size"] <= 0:
            raise ValueError("batch_size must be positive")
        if normalized["log_every"] <= 0:
            raise ValueError("log_every must be positive")
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

    def _create_client(self) -> Any:
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

    def _validate_input_file(self) -> None:
        input_path = self.config["input_path"]
        if not input_path.exists():
            raise FileNotFoundError(f"Input file does not exist: {input_path}")
        if not input_path.is_file():
            raise RuntimeError(f"Input path is not a regular file: {input_path}")
        if input_path.stat().st_size <= 0:
            raise RuntimeError(f"Input file is empty: {input_path}")

    def _redis_key(self, item_id: int) -> str:
        return f"{self.config['key_prefix']}:{item_id}"

    def _write_batch(self, client: Any, batch: list[dict[str, Any]]) -> RedisWriteStats:
        pipeline = client.pipeline(transaction=False)
        item_count = 0
        neighbor_count = 0

        for document in batch:
            item_id = int(document["item_id"])
            neighbors = document.get("neighbors", [])
            key = self._redis_key(item_id)

            if self.config["replace_existing"]:
                pipeline.delete(key)

            score_mapping = {
                str(int(neighbor["item_id"])): float(neighbor["score"])
                for neighbor in neighbors
            }
            if score_mapping:
                pipeline.zadd(key, score_mapping)
                neighbor_count += len(score_mapping)

            if self.config["expire_seconds"] > 0:
                pipeline.expire(key, self.config["expire_seconds"])

            item_count += 1

        pipeline.execute()
        return RedisWriteStats(item_count=item_count, neighbor_count=neighbor_count)

    def write_documents(self, documents: Iterable[dict[str, Any]]) -> RedisWriteStats:
        if self.config["dry_run"]:
            item_count = 0
            neighbor_count = 0
            for document in documents:
                item_count += 1
                neighbor_count += len(document.get("neighbors", []))
            self.logger.info(
                "Dry run: would write %s neighbors for %s items.",
                neighbor_count,
                item_count,
            )
            return RedisWriteStats(item_count=item_count, neighbor_count=neighbor_count)

        client = self._create_client()
        client.ping()
        self.logger.info(
            "Connected to Redis at %s:%s db=%s.",
            self.config["redis_host"],
            self.config["redis_port"],
            self.config["redis_db"],
        )

        total = RedisWriteStats(item_count=0, neighbor_count=0)
        batch: list[dict[str, Any]] = []

        for document in documents:
            batch.append(document)
            if len(batch) < self.config["batch_size"]:
                continue

            stats = self._write_batch(client, batch)
            total = RedisWriteStats(
                item_count=total.item_count + stats.item_count,
                neighbor_count=total.neighbor_count + stats.neighbor_count,
            )
            if total.item_count % self.config["log_every"] == 0:
                self.logger.info(
                    "Wrote %s items and %s neighbors so far.",
                    total.item_count,
                    total.neighbor_count,
                )
            batch = []

        if batch:
            stats = self._write_batch(client, batch)
            total = RedisWriteStats(
                item_count=total.item_count + stats.item_count,
                neighbor_count=total.neighbor_count + stats.neighbor_count,
            )

        self.logger.info(
            "Finished Redis write: %s neighbors for %s items.",
            total.neighbor_count,
            total.item_count,
        )
        return total

    def run(self) -> RedisWriteStats:
        self.logger.info("RedisWriter run started.")
        self._validate_input_file()
        return self.write_documents(self.iter_documents())

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
        parser = argparse.ArgumentParser(
            description="Write item similarity data into Redis sorted sets."
        )
        parser.add_argument("--redis-host", dest="redis_host", help="Redis host.")
        parser.add_argument("--redis-port", dest="redis_port", type=int, help="Redis port.")
        parser.add_argument("--redis-db", dest="redis_db", type=int, help="Redis database.")
        parser.add_argument(
            "--input",
            dest="input_path",
            help="Path to generated item similarity data.",
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
            help="Redis key prefix. Final key is '<prefix>:<item_id>'.",
        )
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
            help="Number of item keys per Redis pipeline execution.",
        )
        parser.add_argument(
            "--expire-seconds",
            dest="expire_seconds",
            type=int,
            help="Optional TTL for each similarity key. <=0 means no TTL.",
        )
        parser.add_argument(
            "--no-replace-existing",
            dest="replace_existing",
            action="store_false",
            help="Do not delete an item key before writing its new ZSET values.",
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
            "--log-every",
            dest="log_every",
            type=int,
            help="Emit a progress log every N written item keys.",
        )
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        config: dict[str, Any] = {}
        for key, value in vars(args).items():
            if key in {"ssl", "replace_existing", "dry_run"}:
                config[key] = value
            elif value is not None and value is not False:
                config[key] = value
        return config


def main() -> None:
    parser = RedisWriter.build_arg_parser()
    args = parser.parse_args()
    log_level = getattr(logging, str(getattr(args, "log_level", "INFO")).upper(), logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    writer = RedisWriter(RedisWriter.config_from_args(args))
    stats = writer.run()
    print(
        f"Wrote {stats.neighbor_count} item similarity neighbors "
        f"for {stats.item_count} items into Redis."
    )


if __name__ == "__main__":
    main()
