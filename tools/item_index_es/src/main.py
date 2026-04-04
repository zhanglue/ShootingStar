#!/usr/bin/env python3

from __future__ import annotations

import argparse
import logging
from typing import Any

from elasticsearch_writer import ElasticsearchWriter
from item_index_builder import ItemIndexBuilder


def build_arg_parser() -> argparse.ArgumentParser:
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
        "--skip-index",
        dest="run_index",
        action="store_false",
        help="Skip Elasticsearch writing and only build the output file.",
    )

    for action in ItemIndexBuilder.build_arg_parser()._actions:
        if action.dest == "help":
            continue
        parser._add_action(action)

    for action in ElasticsearchWriter.build_arg_parser()._actions:
        if action.dest == "help":
            continue
        parser._add_action(action)

    return parser


def config_from_args(args: argparse.Namespace) -> dict[str, Any]:
    config: dict[str, Any] = {}
    for key, value in vars(args).items():
        if key in {"run_build", "run_index", "verify_certs", "retry_on_timeout"}:
            config[key] = value
        elif value is not None and value is not False:
            config[key] = value
    return config


def split_config(
    config: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any], bool, bool]:
    builder_keys = set(ItemIndexBuilder.DEFAULT_CONFIG.keys())
    writer_keys = set(ElasticsearchWriter.DEFAULT_CONFIG.keys())

    builder_config = {key: value for key, value in config.items() if key in builder_keys}
    writer_config = {key: value for key, value in config.items() if key in writer_keys}
    run_build = bool(config.get("run_build", True))
    run_index = bool(config.get("run_index", True))

    if "input_path" not in writer_config and "output_path" in config:
        writer_config["input_path"] = config["output_path"]
    if "input_format" not in writer_config and "output_format" in config:
        writer_config["input_format"] = config["output_format"]

    return builder_config, writer_config, run_build, run_index


def main() -> None:
    args = build_arg_parser().parse_args()
    log_level_name = str(getattr(args, "log_level", "INFO")).upper()
    log_level = getattr(logging, log_level_name, logging.INFO)
    logging.basicConfig(
        level=log_level,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    config = config_from_args(args)
    builder_config, writer_config, run_build, run_index = split_config(config)

    builder = ItemIndexBuilder(builder_config)
    writer: ElasticsearchWriter | None = None

    built_count: int | None = None
    indexed_count: int | None = None

    if run_build:
        items, output_path = builder.run()
        built_count = len(items)
        writer_config["input_path"] = output_path
        writer_config["input_format"] = builder.config["output_format"]

    if run_index:
        # The writer will create the index only when it is missing and
        # --ensure-index is enabled. Existing indices are reused as-is.
        writer = ElasticsearchWriter(writer_config)
        indexed_count = writer.run()

    parts: list[str] = []
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
    print(", ".join(parts) if parts else "No work executed.")


if __name__ == "__main__":
    main()
