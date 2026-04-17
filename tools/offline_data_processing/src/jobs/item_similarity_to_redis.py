#!/usr/bin/env python3
"""
Orchestrate item-similarity generation and Redis writes.
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

from builders.item_similarity_builder import ItemSimilarityBuilder
from writers.redis_writer import RedisWriter


def build_arg_parser() -> argparse.ArgumentParser:
    """
    Merge builder and writer CLIs into one end-to-end job parser.
    """
    parser = argparse.ArgumentParser(
        description="Build item similarity data and optionally write it into Redis."
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

    add_actions_from(ItemSimilarityBuilder.build_arg_parser())
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
            "ratings_sorted_by_user",
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
) -> tuple[dict[str, Any], dict[str, Any], bool, bool]:
    """
    Split combined job config into builder config, writer config, and flags.
    """
    builder_keys = set(ItemSimilarityBuilder.DEFAULT_CONFIG.keys())
    writer_keys = set(RedisWriter.DEFAULT_CONFIG.keys())

    builder_config = {key: value for key, value in config.items() if key in builder_keys}
    writer_config = {key: value for key, value in config.items() if key in writer_keys}
    run_build = bool(config.get("run_build", True))
    run_write = bool(config.get("run_write", True))

    if "input_path" not in writer_config and "output_path" in config:
        # When build and write are chained, RedisWriter should consume the file
        # that ItemSimilarityBuilder just emitted unless explicitly overridden.
        writer_config["input_path"] = config["output_path"]
    if "input_format" not in writer_config and "output_format" in config:
        writer_config["input_format"] = config["output_format"]

    return builder_config, writer_config, run_build, run_write


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
    builder_config, writer_config, run_build, run_write = split_config(config)

    builder = ItemSimilarityBuilder(builder_config)
    built_count: int | None = None
    written_item_count: int | None = None
    written_neighbor_count: int | None = None

    if run_build:
        # Build first so the writer sees the exact output path/format from this
        # run, which matters when callers override --output or --output-format.
        logger.info("Build phase enabled; starting ItemSimilarityBuilder.")
        built_count, output_path = builder.run()
        writer_config["input_path"] = output_path
        writer_config["input_format"] = builder.config["output_format"]
    else:
        logger.info("Build phase skipped.")

    if run_write:
        # Redis writes can also be dry-run validated via RedisWriter's --dry-run
        # option while still exercising this orchestration path.
        logger.info("Redis write phase enabled; starting RedisWriter.")
        writer = RedisWriter(writer_config)
        stats = writer.run()
        written_item_count = stats.item_count
        written_neighbor_count = stats.neighbor_count
    else:
        logger.info("Redis write phase skipped.")

    parts: list[str] = ["\n"]
    if built_count is not None:
        parts.append(f"built {built_count} item rows -> {builder.config['output_path']}")
    if written_item_count is not None and written_neighbor_count is not None:
        if writer_config.get("dry_run", RedisWriter.DEFAULT_CONFIG["dry_run"]):
            parts.append(
                f"validated {written_neighbor_count} neighbors for "
                f"{written_item_count} items in Redis dry-run"
            )
        else:
            parts.append(
                f"wrote {written_neighbor_count} neighbors for "
                f"{written_item_count} items -> Redis "
                f"{writer_config.get('redis_host', RedisWriter.DEFAULT_CONFIG['redis_host'])}:"
                f"{writer_config.get('redis_port', RedisWriter.DEFAULT_CONFIG['redis_port'])}"
            )
    print("\n".join(parts) if parts else "No work executed.")


if __name__ == "__main__":
    main()
