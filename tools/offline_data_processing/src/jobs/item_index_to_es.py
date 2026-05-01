#!/usr/bin/env python3
"""
Orchestrate item document generation and Elasticsearch indexing.
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

from builders.item_index_builder import ItemIndexBuilder
from writers.elasticsearch_writer import ElasticsearchWriter


def build_arg_parser() -> argparse.ArgumentParser:
    """
    Merge builder and writer CLIs into one end-to-end job parser.
    """
    parser = argparse.ArgumentParser(
        description="Build item documents and optionally write them into Elasticsearch."
    )
    parser.add_argument(
        "--skip-build",
        dest="run_build",
        action="store_false",
        help="Skip the build step and only write an existing file.",
    )
    parser.add_argument(
        "--skip-write",
        dest="run_index",
        action="store_false",
        help="Skip Elasticsearch writing and only build the output file.",
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

    add_actions_from(ItemIndexBuilder.build_arg_parser())
    add_actions_from(ElasticsearchWriter.build_arg_parser())

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
        elif key in {"run_build", "run_index", "verify_certs", "retry_on_timeout"}:
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
    builder_keys = set(ItemIndexBuilder.DEFAULT_CONFIG.keys())
    writer_keys = set(ElasticsearchWriter.DEFAULT_CONFIG.keys())

    builder_config = {key: value for key, value in config.items() if key in builder_keys}
    writer_config = {key: value for key, value in config.items() if key in writer_keys}
    run_build = bool(config.get("run_build", True))
    run_index = bool(config.get("run_index", True))

    if "input_path" not in writer_config and "output_path" in config:
        # When build and index are chained, the writer should consume the file
        # that the builder just wrote unless the caller explicitly overrides it.
        writer_config["input_path"] = config["output_path"]
    if "input_format" not in writer_config and "output_format" in config:
        writer_config["input_format"] = config["output_format"]

    return builder_config, writer_config, run_build, run_index


def main() -> None:
    """
    Run the optional build phase followed by the optional ES indexing phase.
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
    builder_config, writer_config, run_build, run_index = split_config(config)

    builder = ItemIndexBuilder(builder_config)
    writer: ElasticsearchWriter | None = None

    built_count: int | None = None
    indexed_count: int | None = None

    if run_build:
        # Build first so indexing receives the exact output path/format emitted
        # by ItemIndexBuilder in this run.
        logger.info("Build phase enabled; starting ItemIndexBuilder.")
        items, output_path = builder.run()
        built_count = len(items)
        writer_config["input_path"] = output_path
        writer_config["input_format"] = builder.config["output_format"]
    else:
        logger.info("Build phase skipped.")

    if run_index:
        # The writer will create the index only when it is missing and
        # --ensure-index is enabled. Existing indices are reused as-is.
        logger.info("Indexing phase enabled; starting ElasticsearchWriter.")
        writer = ElasticsearchWriter(writer_config)
        indexed_count = writer.run()
    else:
        logger.info("Indexing phase skipped.")

    parts: list[str] = [" "]
    if built_count is not None:
        parts.append(f"built {built_count} items -> {builder.config['output_path']}")
    if indexed_count is not None:
        assert writer is not None
        parts.append(
            "indexed "
            f"{indexed_count} docs -> "
            f"{writer.config['cloud_id'] or writer.config['es_url']}/"
            f"{writer.config['index_name']}"
        )
    print("\n".join(parts) if parts else "No work executed.")


if __name__ == "__main__":
    main()
