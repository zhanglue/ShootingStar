#!/usr/bin/env python3
"""
Orchestrate user-similarity generation and Redis writes.
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path
from typing import Any

SRC_DIR = Path(__file__).resolve().parents[1]
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

from builders.user_similarity_builder import UserSimilarityBuilder
from writers.redis_writer import RedisWriter, SimilarityDataAdapter


def build_arg_parser() -> argparse.ArgumentParser:
    """
    Merge builder and writer CLIs into one end-to-end job parser.
    """
    parser = argparse.ArgumentParser(
        description="Build user similarity data and optionally write it into Redis."
    )
    parser.add_argument(
        "--skip-build",
        dest="run_build",
        action="store_false",
        help="Skip the build step and only write an existing file.",
    )
    parser.add_argument(
        "--skip-write",
        dest="run_write",
        action="store_false",
        help="Skip Redis writing and only build the output file.",
    )

    existing_option_strings = set()

    def add_actions_from(source_parser: argparse.ArgumentParser) -> None:
        """
        Copy non-conflicting options from a component parser.
        """
        for action in source_parser._actions:
            if action.dest == "help":
                continue

            option_strings = set(action.option_strings)
            if option_strings and option_strings & existing_option_strings:
                continue

            parser._add_action(action)
            existing_option_strings.update(option_strings)

    add_actions_from(UserSimilarityBuilder.build_arg_parser())
    add_actions_from(SimilarityDataAdapter.build_arg_parser())
    add_actions_from(RedisWriter.build_arg_parser())

    return parser


def config_from_args(args: argparse.Namespace) -> dict[str, Any]:
    """
    Convert parsed CLI args into a sparse config override map.
    """
    config: dict[str, Any] = {}
    for key, value in vars(args).items():
        if key == "debug":
            if value:
                config["log_level"] = "DEBUG"
        elif key in {
            "run_build",
            "run_write",
            "use_rating_weight",
            "ssl",
            "replace_existing",
            "dry_run",
        }:
            config[key] = value
        elif value is not None and value is not False:
            config[key] = value
    return config


def split_config(
    config: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], bool, bool]:
    """
    Split combined job config into builder, adapter, writer config, and flags.
    """
    builder_keys = set(UserSimilarityBuilder.DEFAULT_CONFIG.keys())
    adapter_keys = set(SimilarityDataAdapter.DEFAULT_CONFIG.keys())
    writer_keys = set(RedisWriter.DEFAULT_CONFIG.keys())

    builder_config = {key: value for key, value in config.items() if key in builder_keys}
    adapter_config = {key: value for key, value in config.items() if key in adapter_keys}
    writer_config = {key: value for key, value in config.items() if key in writer_keys}
    run_build = bool(config.get("run_build", True))
    run_write = bool(config.get("run_write", True))

    if "input_path" not in adapter_config and "output_path" in config:
        # When build and write are chained, the adapter should consume the file
        # that UserSimilarityBuilder just emitted unless explicitly overridden.
        adapter_config["input_path"] = config["output_path"]
    if "input_format" not in adapter_config and "output_format" in config:
        adapter_config["input_format"] = config["output_format"]

    adapter_config.setdefault("input_schema", "user-similarity")

    return builder_config, adapter_config, writer_config, run_build, run_write


def main() -> None:
    """
    Run the optional build phase followed by the optional Redis write phase.
    """
    args = build_arg_parser().parse_args()
    log_level_name = (
        "DEBUG" if getattr(args, "debug", False) else str(getattr(args, "log_level", "INFO")).upper()
    )
    log_level = getattr(logging, log_level_name, logging.INFO)
    logging.basicConfig(
        level=log_level,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    logger = logging.getLogger("Main")

    config = config_from_args(args)
    builder_config, adapter_config, writer_config, run_build, run_write = split_config(config)

    builder = UserSimilarityBuilder(builder_config)
    built_count: int | None = None
    written_user_count: int | None = None
    written_neighbor_count: int | None = None

    if run_build:
        # Build first so the writer sees the exact output path/format from this
        # run, which matters when callers override --output or --output-format.
        logger.info("Build phase enabled; starting UserSimilarityBuilder.")
        built_count, output_path = builder.run()
        adapter_config["input_path"] = output_path
        adapter_config["input_format"] = builder.config["output_format"]
    else:
        logger.info("Build phase skipped.")

    if run_write:
        # Redis writes can also be dry-run validated via RedisWriter's --dry-run
        # option while still exercising this orchestration path.
        logger.info("Redis write phase enabled; adapting user similarity data.")
        adapter = SimilarityDataAdapter(adapter_config)
        logger.info("Starting RedisWriter.")
        writer = RedisWriter(writer_config)
        stats = writer.run(adapter.iter_redis_data())
        written_user_count = stats.record_count
        written_neighbor_count = stats.value_count
    else:
        logger.info("Redis write phase skipped.")

    parts: list[str] = ["\n"]
    if built_count is not None:
        parts.append(f"built {built_count} user rows -> {builder.config['output_path']}")
    if written_user_count is not None and written_neighbor_count is not None:
        if writer_config.get("dry_run", RedisWriter.DEFAULT_CONFIG["dry_run"]):
            parts.append(
                f"validated {written_neighbor_count} neighbors for "
                f"{written_user_count} users in Redis dry-run"
            )
        else:
            parts.append(
                f"wrote {written_neighbor_count} neighbors for "
                f"{written_user_count} users -> Redis "
                f"{writer_config.get('redis_host', RedisWriter.DEFAULT_CONFIG['redis_host'])}:"
                f"{writer_config.get('redis_port', RedisWriter.DEFAULT_CONFIG['redis_port'])}"
            )
    print("\n".join(parts) if parts else "No work executed.")


if __name__ == "__main__":
    main()
