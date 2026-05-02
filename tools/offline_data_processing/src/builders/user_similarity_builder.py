#!/usr/bin/env python3
"""
Build user-user collaborative filtering neighbors from MovieLens ratings.
"""
from __future__ import annotations

import argparse
import csv
import heapq
import json
import logging
import math
import subprocess
from collections import Counter, defaultdict
from itertools import combinations
from pathlib import Path
from typing import Any, Iterable


ROOT_DIR = Path(__file__).resolve().parents[2]


class UserSimilarityBuilder:
    """
    Compute user similarity rows using sharded item and pair reduction.

    MovieLens ratings are sorted by userId, while user-user CF needs users to be
    grouped by item first. This builder therefore writes positive ratings into
    item shards, expands each item's sampled user set into user-pair
    contributions, then reduces those pair shards into top-K user neighbors.
    """

    DEFAULT_CONFIG: dict[str, Any] = {
        "ratings_path": ROOT_DIR / "demo_ratings.csv",
        "output_path": ROOT_DIR / "user_similarity.jsonl",
        "output_format": "jsonl",
        "min_rating": 4.0,
        "use_rating_weight": False,
        "positive_weight_offset": 3.0,
        "min_user_items": 5,
        "min_item_users": 2,
        "max_item_users": 30,
        "coverage_anchor_items_per_user": 0,
        "coverage_pair_sample_size": 0,
        "min_cooccurrence": 2,
        "min_similarity": 0.0,
        "top_k": 10,
        "shard_count": 512,
        "shard_buffer_rows": 100000,
        "temp_dir": ROOT_DIR / "temp",
        "log_level": "INFO",
        "user_stats_log_every": 2000000,
        "item_mapping_log_every": 200000,
        "shard_log_divisor": 32,
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
        self._rating_row_count: int | None = None

    @classmethod
    def _normalize_config(cls, config: dict[str, Any]) -> dict[str, Any]:
        """
        Coerce incoming config values and validate tunables.
        """
        normalized = dict(config)
        normalized["ratings_path"] = Path(normalized["ratings_path"])
        normalized["output_path"] = Path(normalized["output_path"])
        normalized["output_format"] = str(normalized["output_format"]).lower()
        if normalized["output_format"] not in {"jsonl", "json"}:
            raise ValueError("output_format must be either 'jsonl' or 'json'")

        normalized["min_rating"] = float(normalized["min_rating"])
        normalized["use_rating_weight"] = bool(normalized["use_rating_weight"])
        normalized["positive_weight_offset"] = float(normalized["positive_weight_offset"])
        normalized["min_user_items"] = int(normalized["min_user_items"])
        normalized["min_item_users"] = int(normalized["min_item_users"])
        normalized["max_item_users"] = int(normalized["max_item_users"])
        normalized["coverage_anchor_items_per_user"] = int(
            normalized["coverage_anchor_items_per_user"]
        )
        normalized["coverage_pair_sample_size"] = int(normalized["coverage_pair_sample_size"])
        normalized["min_cooccurrence"] = int(normalized["min_cooccurrence"])
        normalized["min_similarity"] = float(normalized["min_similarity"])
        normalized["top_k"] = int(normalized["top_k"])
        normalized["shard_count"] = int(normalized["shard_count"])
        normalized["shard_buffer_rows"] = int(normalized["shard_buffer_rows"])
        normalized["temp_dir"] = Path(normalized["temp_dir"])
        normalized["log_level"] = str(normalized["log_level"]).upper()
        normalized["user_stats_log_every"] = int(normalized["user_stats_log_every"])
        normalized["item_mapping_log_every"] = int(normalized["item_mapping_log_every"])
        normalized["shard_log_divisor"] = int(normalized["shard_log_divisor"])

        if normalized["top_k"] <= 0:
            raise ValueError("top_k must be positive")
        if normalized["min_user_items"] <= 0:
            raise ValueError("min_user_items must be positive")
        if normalized["min_item_users"] <= 0:
            raise ValueError("min_item_users must be positive")
        if normalized["coverage_anchor_items_per_user"] < 0:
            raise ValueError("coverage_anchor_items_per_user must be non-negative")
        if normalized["coverage_pair_sample_size"] < 0:
            raise ValueError("coverage_pair_sample_size must be non-negative")
        if normalized["min_cooccurrence"] <= 0:
            raise ValueError("min_cooccurrence must be positive")
        if normalized["shard_count"] <= 0:
            raise ValueError("shard_count must be positive")
        if normalized["shard_buffer_rows"] <= 0:
            raise ValueError("shard_buffer_rows must be positive")
        if normalized["user_stats_log_every"] <= 0:
            raise ValueError("user_stats_log_every must be positive")
        if normalized["item_mapping_log_every"] <= 0:
            raise ValueError("item_mapping_log_every must be positive")
        if normalized["shard_log_divisor"] <= 0:
            raise ValueError("shard_log_divisor must be positive")
        return normalized

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
        """
        Create the standalone CLI parser for user-similarity generation.
        """
        parser = argparse.ArgumentParser(
            description="Build user-user similarity data from MovieLens ratings."
        )
        parser.add_argument("--ratings", dest="ratings_path", help="Path to ratings.csv.")
        parser.add_argument(
            "--output",
            dest="output_path",
            help="Path to write user similarity data.",
        )
        parser.add_argument(
            "--output-format",
            dest="output_format",
            choices=("jsonl", "json"),
            help="Whether to write line-delimited JSON or a JSON array.",
        )
        parser.add_argument(
            "--min-rating",
            dest="min_rating",
            type=float,
            help="Minimum rating treated as positive feedback.",
        )
        parser.add_argument(
            "--use-rating-weight",
            dest="use_rating_weight",
            action="store_true",
            help="Use rating-derived weights instead of binary positive feedback.",
        )
        parser.add_argument(
            "--positive-weight-offset",
            dest="positive_weight_offset",
            type=float,
            help="Rating offset used when --use-rating-weight is enabled.",
        )
        parser.add_argument(
            "--min-user-items",
            dest="min_user_items",
            type=int,
            help="Minimum positive items required for a user to participate.",
        )
        parser.add_argument(
            "--min-item-users",
            dest="min_item_users",
            type=int,
            help="Minimum positive users required for an item to emit user pairs.",
        )
        parser.add_argument(
            "--max-item-users",
            dest="max_item_users",
            type=int,
            help="Maximum positive users sampled per item. <=0 disables cap.",
        )
        parser.add_argument(
            "--coverage-anchor-items-per-user",
            dest="coverage_anchor_items_per_user",
            type=int,
            help=(
                "Number of low-popularity positive items each valid user may anchor "
                "when item user caps would otherwise drop them. 0 disables."
            ),
        )
        parser.add_argument(
            "--coverage-pair-sample-size",
            dest="coverage_pair_sample_size",
            type=int,
            help=(
                "Optional per-item sample size used only for linear coverage-anchor "
                "pairs. 0 uses max_item_users."
            ),
        )
        parser.add_argument(
            "--min-cooccurrence",
            dest="min_cooccurrence",
            type=int,
            help="Minimum number of shared items required for a user pair.",
        )
        parser.add_argument(
            "--min-similarity",
            dest="min_similarity",
            type=float,
            help="Minimum cosine similarity kept in the output.",
        )
        parser.add_argument(
            "--top-k",
            dest="top_k",
            type=int,
            help="Maximum neighbors kept per user.",
        )
        parser.add_argument(
            "--shard-count",
            dest="shard_count",
            type=int,
            help="Number of CSV shards used for item, pair, and neighbor intermediate files.",
        )
        parser.add_argument(
            "--shard-buffer-rows",
            dest="shard_buffer_rows",
            type=int,
            help="Maximum buffered intermediate CSV rows before flushing to disk.",
        )
        parser.add_argument(
            "--temp-dir",
            dest="temp_dir",
            help="Directory used for intermediate CSV shard files.",
        )
        parser.add_argument(
            "--log-level",
            dest="log_level",
            choices=("DEBUG", "INFO", "WARNING", "ERROR"),
            help="Logging level for builder execution.",
        )
        parser.add_argument(
            "--debug",
            dest="debug",
            action="store_true",
            help="Shortcut for --log-level DEBUG.",
        )
        parser.add_argument(
            "--user-stats-log-every",
            dest="user_stats_log_every",
            type=int,
            help="Emit a user-stats progress log every N scanned rating rows.",
        )
        parser.add_argument(
            "--item-mapping-log-every",
            dest="item_mapping_log_every",
            type=int,
            help="Emit an item-shard mapping progress log every N scanned rating rows.",
        )
        parser.add_argument(
            "--shard-log-divisor",
            dest="shard_log_divisor",
            type=int,
            help="Emit shard-reduction progress every total_shards / N processed shards.",
        )
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        """
        Return only CLI arguments that were explicitly provided.
        """
        config: dict[str, Any] = {}
        for key, value in vars(args).items():
            if key == "debug":
                if value:
                    config["log_level"] = "DEBUG"
            elif key == "use_rating_weight":
                config[key] = value
            elif value is not None and value is not False:
                config[key] = value
        return config

    def build_user_stats(self) -> tuple[set[int], dict[int, float], Counter[int], Counter[int]]:
        """
        Find valid users and compute vector norms and item positive-user counts.
        """
        self.logger.info(
            "Building user stats with min_rating=%s, min_user_items=%s.",
            self.config["min_rating"],
            self.config["min_user_items"],
        )
        user_norm_sq: dict[int, float] = defaultdict(float)
        user_item_counts: Counter[int] = Counter()
        item_user_counts: Counter[int] = Counter()
        positive_rating_count = 0

        for user_id, movie_id, weight, _ in self._iter_positive_ratings(
            "user stats",
            self.config["user_stats_log_every"],
        ):
            user_item_counts[user_id] += 1
            item_user_counts[movie_id] += 1
            user_norm_sq[user_id] += weight * weight
            positive_rating_count += 1

        valid_users = {
            user_id
            for user_id, item_count in user_item_counts.items()
            if item_count >= self.config["min_user_items"] and user_norm_sq[user_id] > 0
        }
        self.logger.info(
            "Kept %s valid users from %s positive ratings across %s users.",
            len(valid_users),
            positive_rating_count,
            len(user_item_counts),
        )
        return valid_users, user_norm_sq, user_item_counts, item_user_counts

    def map_ratings_to_item_shards(
        self,
        valid_users: set[int],
        item_shard_dir: Path,
    ) -> int:
        """
        Map each positive user-item interaction into an item-owned CSV shard.
        """
        self.logger.info(
            "Mapping positive ratings to %s item CSV shards.",
            self.config["shard_count"],
        )
        buffers: list[list[str]] = [[] for _ in range(self.config["shard_count"])]
        buffered_rows = 0
        written_count = 0

        for user_id, movie_id, weight, timestamp in self._iter_positive_ratings(
            "item shard mapping",
            self.config["item_mapping_log_every"],
        ):
            if user_id not in valid_users:
                continue

            shard_id = self._item_shard_id(movie_id)
            buffers[shard_id].append(f"{movie_id},{user_id},{weight:.12g},{timestamp}\n")
            buffered_rows += 1
            written_count += 1
            if buffered_rows >= self.config["shard_buffer_rows"]:
                self._flush_item_buffers(item_shard_dir, buffers)
                buffered_rows = 0

        self._flush_item_buffers(item_shard_dir, buffers)
        self.logger.info("Wrote %s positive rating rows to item shards.", written_count)
        return written_count

    def build_coverage_anchor_users_by_item(
        self,
        valid_users: set[int],
        item_user_counts: Counter[int],
    ) -> dict[int, set[int]]:
        """
        Pick a few lower-popularity positive items per user as coverage anchors.

        The item cap protects pair expansion from very popular titles, but a
        pure item-local cap can erase mainstream active users from every item
        they touched. Anchors let those users contribute linear user-item pairs
        against each item's sampled baseline without raising the quadratic cap.
        """
        anchor_count = self.config["coverage_anchor_items_per_user"]
        if anchor_count <= 0:
            self.logger.info("Coverage anchors disabled.")
            return {}
        if self.config["max_item_users"] <= 0:
            self.logger.info("Coverage anchors skipped because max_item_users is disabled.")
            return {}

        self.logger.info(
            "Building up to %s coverage anchor item(s) per valid user.",
            anchor_count,
        )
        anchors_by_user: dict[int, list[tuple[tuple[int, float, int, int, int], int]]] = (
            defaultdict(list)
        )
        positive_rating_count = 0
        for user_id, movie_id, weight, timestamp in self._iter_positive_ratings(
            "coverage anchor selection",
            self.config["item_mapping_log_every"],
        ):
            if user_id not in valid_users:
                continue

            item_user_count = item_user_counts[movie_id]
            sample_rank = self._stable_sample_rank(movie_id, user_id)
            rank = (-item_user_count, weight, timestamp, -sample_rank, -movie_id)
            heap = anchors_by_user[user_id]
            entry = (rank, movie_id)
            if len(heap) < anchor_count:
                heapq.heappush(heap, entry)
            elif rank > heap[0][0]:
                heapq.heapreplace(heap, entry)
            positive_rating_count += 1

        anchor_users_by_item: dict[int, set[int]] = defaultdict(set)
        for user_id, heap in anchors_by_user.items():
            for _, movie_id in heap:
                anchor_users_by_item[movie_id].add(user_id)

        anchor_assignment_count = sum(
            len(user_ids) for user_ids in anchor_users_by_item.values()
        )
        self.logger.info(
            (
                "Built %s coverage anchor assignments for %s users across %s items "
                "from %s valid positive ratings."
            ),
            anchor_assignment_count,
            len(anchors_by_user),
            len(anchor_users_by_item),
            positive_rating_count,
        )
        return anchor_users_by_item

    def reduce_item_shards_to_pair_shards(
        self,
        item_shard_dir: Path,
        pair_shard_dir: Path,
        coverage_anchor_users_by_item: dict[int, set[int]] | None = None,
    ) -> int:
        """
        Expand item-owned user lists into weighted user-pair contribution rows.
        """
        item_shard_paths = [
            (shard_id, self._item_shard_path(item_shard_dir, shard_id))
            for shard_id in range(self.config["shard_count"])
            if self._item_shard_path(item_shard_dir, shard_id).exists()
        ]
        total_item_shards = len(item_shard_paths)
        self.logger.info(
            "Reducing %s existing item shards out of %s configured shards into pair CSV shards.",
            total_item_shards,
            self.config["shard_count"],
        )
        buffers: list[list[str]] = [[] for _ in range(self.config["shard_count"])]
        buffered_rows = 0
        pair_contribution_count = 0
        contributing_item_count = 0
        shard_progress_every = self._progress_interval(total_item_shards)

        for processed_shard_count, (shard_id, shard_path) in enumerate(
            item_shard_paths,
            start=1,
        ):
            item_interactions: dict[int, list[tuple[int, float, int]]] = defaultdict(list)

            with shard_path.open(encoding="utf-8") as handle:
                for line in handle:
                    movie_raw, user_raw, weight_raw, timestamp_raw = line.rstrip("\n").split(",")
                    item_interactions[int(movie_raw)].append(
                        (int(user_raw), float(weight_raw), int(timestamp_raw))
                    )

            kept_item_count = 0
            kept_pair_count = 0
            for movie_id, interactions in item_interactions.items():
                written_count = self._write_item_pairs_to_shards(
                    movie_id,
                    interactions,
                    buffers,
                    (coverage_anchor_users_by_item or {}).get(movie_id, set()),
                )
                if written_count == 0:
                    continue

                kept_item_count += 1
                kept_pair_count += written_count
                contributing_item_count += 1
                pair_contribution_count += written_count
                buffered_rows += written_count
                if buffered_rows >= self.config["shard_buffer_rows"]:
                    self._flush_pair_buffers(pair_shard_dir, buffers)
                    buffered_rows = 0

            self.logger.debug(
                "Reduced item shard %s: %s items, %s contributing items, %s pair rows.",
                shard_id,
                len(item_interactions),
                kept_item_count,
                kept_pair_count,
            )
            if (
                processed_shard_count % shard_progress_every == 0
                or processed_shard_count == total_item_shards
            ):
                percent = self._percent(processed_shard_count, total_item_shards)
                self.logger.info(
                    (
                        "Reduced %s/%s item shards so far (%.1f%%); "
                        "wrote %s pair contribution rows from %s items."
                    ),
                    processed_shard_count,
                    total_item_shards,
                    percent,
                    pair_contribution_count,
                    contributing_item_count,
                )

        self._flush_pair_buffers(pair_shard_dir, buffers)
        self.logger.info(
            "Wrote %s pair contribution rows from %s contributing items.",
            pair_contribution_count,
            contributing_item_count,
        )
        return pair_contribution_count

    def reduce_pair_shards_to_neighbor_shards(
        self,
        pair_shard_dir: Path,
        neighbor_shard_dir: Path,
        user_norm_sq: dict[int, float],
    ) -> int:
        """
        Aggregate pair shards into bidirectional scored neighbor rows.
        """
        pair_shard_paths = [
            (shard_id, self._pair_shard_path(pair_shard_dir, shard_id))
            for shard_id in range(self.config["shard_count"])
            if self._pair_shard_path(pair_shard_dir, shard_id).exists()
        ]
        total_pair_shards = len(pair_shard_paths)
        self.logger.info(
            "Reducing %s existing pair shards out of %s configured shards into neighbor CSV shards.",
            total_pair_shards,
            self.config["shard_count"],
        )
        buffers: list[list[str]] = [[] for _ in range(self.config["shard_count"])]
        buffered_rows = 0
        neighbor_row_count = 0
        shard_progress_every = self._progress_interval(total_pair_shards)

        for processed_shard_count, (shard_id, shard_path) in enumerate(
            pair_shard_paths,
            start=1,
        ):
            pair_weight_sums: dict[tuple[int, int], float] = defaultdict(float)
            pair_item_counts: Counter[tuple[int, int]] = Counter()

            with shard_path.open(encoding="utf-8") as handle:
                for line in handle:
                    left_raw, right_raw, weight_raw = line.rstrip("\n").split(",")
                    pair = (int(left_raw), int(right_raw))
                    pair_weight_sums[pair] += float(weight_raw)
                    pair_item_counts[pair] += 1

            kept_pair_count = 0
            for (left_user_id, right_user_id), cooccurrence in pair_item_counts.items():
                if cooccurrence < self.config["min_cooccurrence"]:
                    continue

                denominator = math.sqrt(
                    user_norm_sq[left_user_id] * user_norm_sq[right_user_id]
                )
                if denominator <= 0:
                    continue

                score = pair_weight_sums[(left_user_id, right_user_id)] / denominator
                if score < self.config["min_similarity"]:
                    continue

                rounded_score = round(score, 8)
                left_shard_id = self._neighbor_shard_id(left_user_id)
                right_shard_id = self._neighbor_shard_id(right_user_id)
                buffers[left_shard_id].append(
                    f"{left_user_id},{right_user_id},{rounded_score:.8g},{cooccurrence}\n"
                )
                buffers[right_shard_id].append(
                    f"{right_user_id},{left_user_id},{rounded_score:.8g},{cooccurrence}\n"
                )
                neighbor_row_count += 2
                buffered_rows += 2
                kept_pair_count += 1

                if buffered_rows >= self.config["shard_buffer_rows"]:
                    self._flush_neighbor_buffers(neighbor_shard_dir, buffers)
                    buffered_rows = 0

            self.logger.debug(
                "Reduced pair shard %s: %s unique pairs, %s kept pairs.",
                shard_id,
                len(pair_item_counts),
                kept_pair_count,
            )
            if (
                processed_shard_count % shard_progress_every == 0
                or processed_shard_count == total_pair_shards
            ):
                percent = self._percent(processed_shard_count, total_pair_shards)
                self.logger.info(
                    (
                        "Reduced %s/%s pair shards so far (%.1f%%); "
                        "wrote %s neighbor candidate rows."
                    ),
                    processed_shard_count,
                    total_pair_shards,
                    percent,
                    neighbor_row_count,
                )

        self._flush_neighbor_buffers(neighbor_shard_dir, buffers)
        self.logger.info("Wrote %s neighbor candidate rows.", neighbor_row_count)
        return neighbor_row_count

    def reduce_neighbor_shards_to_output(self, neighbor_shard_dir: Path) -> int:
        """
        Keep top-K neighbors per user and write the final output file.
        """
        output_path = self.config["output_path"]
        output_format = self.config["output_format"]
        output_path.parent.mkdir(parents=True, exist_ok=True)

        neighbor_shard_paths = [
            (shard_id, self._neighbor_shard_path(neighbor_shard_dir, shard_id))
            for shard_id in range(self.config["shard_count"])
            if self._neighbor_shard_path(neighbor_shard_dir, shard_id).exists()
        ]
        total_neighbor_shards = len(neighbor_shard_paths)
        shard_progress_every = self._progress_interval(total_neighbor_shards)
        self.logger.info(
            (
                "Reducing %s existing neighbor shards out of %s configured shards "
                "to final %s output at %s."
            ),
            total_neighbor_shards,
            self.config["shard_count"],
            output_format,
            output_path,
        )
        output_count = 0
        with output_path.open("w", encoding="utf-8") as handle:
            if output_format == "json":
                handle.write("[\n")

            first_json_item = True
            for processed_shard_count, (shard_id, shard_path) in enumerate(
                neighbor_shard_paths,
                start=1,
            ):
                rows = self._topk_rows_for_neighbor_shard(shard_path)
                for row in rows:
                    if output_format == "json":
                        if not first_json_item:
                            handle.write(",\n")
                        handle.write(json.dumps(row, ensure_ascii=False))
                        first_json_item = False
                    else:
                        handle.write(json.dumps(row, ensure_ascii=False))
                        handle.write("\n")
                    output_count += 1

                if (
                    processed_shard_count % shard_progress_every == 0
                    or processed_shard_count == total_neighbor_shards
                ):
                    percent = self._percent(processed_shard_count, total_neighbor_shards)
                    self.logger.info(
                        (
                            "Reduced %s/%s neighbor shards so far (%.1f%%); "
                            "wrote output rows for %s users."
                        ),
                        processed_shard_count,
                        total_neighbor_shards,
                        percent,
                        output_count,
                    )

            if output_format == "json":
                handle.write("\n]\n")

        self.logger.info("Wrote final similarity output for %s users.", output_count)
        return output_count

    def compute_similarities(self) -> int:
        """
        Run the four-stage user-similarity pipeline.
        """
        item_shard_dir, pair_shard_dir, neighbor_shard_dir = self._prepare_temp_dirs()
        valid_users, user_norm_sq, _, item_user_counts = self.build_user_stats()
        coverage_anchor_users_by_item = self.build_coverage_anchor_users_by_item(
            valid_users,
            item_user_counts,
        )
        self.map_ratings_to_item_shards(valid_users, item_shard_dir)
        self.reduce_item_shards_to_pair_shards(
            item_shard_dir,
            pair_shard_dir,
            coverage_anchor_users_by_item,
        )
        self.reduce_pair_shards_to_neighbor_shards(
            pair_shard_dir,
            neighbor_shard_dir,
            user_norm_sq,
        )
        return self.reduce_neighbor_shards_to_output(neighbor_shard_dir)

    def run(self) -> tuple[int, Path]:
        """
        Compute similarities and return the output row count/path.
        """
        self.logger.info("UserSimilarityBuilder run started.")
        user_count = self.compute_similarities()
        output_path = self.config["output_path"]
        self.logger.info("UserSimilarityBuilder run finished.")
        return user_count, output_path

    def _rating_to_weight(self, rating: float) -> float | None:
        """
        Convert a rating into a positive-feedback weight or drop it.
        """
        if rating < self.config["min_rating"]:
            return None
        if not self.config["use_rating_weight"]:
            return 1.0

        weight = rating - self.config["positive_weight_offset"]
        if weight <= 0:
            return None
        return weight

    @staticmethod
    def count_csv_data_rows(path: Path) -> int:
        """
        Count CSV data rows with wc -l, excluding the header row.
        """
        try:
            result = subprocess.run(
                ["wc", "-l", str(path)],
                check=True,
                capture_output=True,
                text=True,
            )
            row_count = int(result.stdout.strip().split()[0])
            return max(row_count - 1, 0)
        except (FileNotFoundError, IndexError, ValueError, subprocess.CalledProcessError):
            logging.getLogger(UserSimilarityBuilder.__name__).warning(
                "Failed to count rows with wc -l; falling back to Python line scan."
            )

        with path.open(newline="", encoding="utf-8") as handle:
            row_count = sum(1 for _ in csv.reader(handle))
        return max(row_count - 1, 0)

    def get_rating_row_count(self) -> int:
        """
        Return the cached ratings.csv data-row count.
        """
        if self._rating_row_count is None:
            self.logger.info("Counting rating rows from %s.", self.config["ratings_path"])
            self._rating_row_count = self.count_csv_data_rows(self.config["ratings_path"])
        return self._rating_row_count

    @staticmethod
    def _percent(part: int, total: int) -> float:
        """
        Return a display-safe progress percentage.
        """
        if total <= 0:
            return 100.0
        return part / total * 100.0

    def _progress_interval(self, total: int) -> int:
        """
        Pick an INFO progress interval from the configured shard divisor.
        """
        return max(1, total // self.config["shard_log_divisor"])

    def _iter_positive_ratings(
        self,
        stage_name: str,
        log_every: int,
    ) -> Iterable[tuple[int, int, float, int]]:
        """
        Stream positive ratings as user, movie, weight, timestamp tuples.
        """
        ratings_path = self.config["ratings_path"]
        total_rating_rows = self.get_rating_row_count()
        self.logger.info(
            "Reading %s rating rows from %s for %s; progress every %s rows.",
            total_rating_rows,
            ratings_path,
            stage_name,
            log_every,
        )
        scanned_rating_count = 0
        positive_rating_count = 0
        with ratings_path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            expected_fields = {"userId", "movieId", "rating", "timestamp"}
            missing_fields = expected_fields - set(reader.fieldnames or [])
            if missing_fields:
                raise ValueError(
                    f"Ratings CSV is missing required fields: {sorted(missing_fields)}"
                )

            for row_index, row in enumerate(reader, start=1):
                scanned_rating_count = row_index
                if row_index % log_every == 0:
                    self.logger.info(
                        (
                            "Scanned %s/%s rating rows for %s (%.1f%%); "
                            "yielded %s positive ratings."
                        ),
                        row_index,
                        total_rating_rows,
                        stage_name,
                        self._percent(row_index, total_rating_rows),
                        positive_rating_count,
                    )

                rating = float(row["rating"])
                weight = self._rating_to_weight(rating)
                if weight is None:
                    continue

                positive_rating_count += 1
                yield (
                    int(row["userId"]),
                    int(row["movieId"]),
                    weight,
                    int(row["timestamp"]),
                )

        self.logger.info(
            "Finished scanning %s rating rows for %s; yielded %s positive ratings.",
            scanned_rating_count,
            stage_name,
            positive_rating_count,
        )

    def _normalize_item_interactions(
        self,
        movie_id: int,
        interactions: list[tuple[int, float, int]],
    ) -> list[tuple[int, float]]:
        """
        Deduplicate and cap one item's positive user interactions.
        """
        sampled, _, _ = self._select_item_pair_participants(movie_id, interactions, set())
        return sampled

    def _select_item_pair_participants(
        self,
        movie_id: int,
        interactions: list[tuple[int, float, int]],
        coverage_anchor_users: set[int],
    ) -> tuple[list[tuple[int, float]], list[tuple[int, float]], list[tuple[int, float]]]:
        """
        Select baseline sampled users and uncapped coverage extras for one item.

        The baseline remains capped and is fully pair-expanded. Coverage extras
        are users whose personal anchor item is this item; they pair only with
        the baseline sample, which adds coverage at linear cost.
        """
        deduped: dict[int, tuple[float, int]] = {}
        for user_id, weight, timestamp in interactions:
            existing = deduped.get(user_id)
            if existing is None or (weight, timestamp) > existing:
                deduped[user_id] = (weight, timestamp)

        ranked = [
            (
                user_id,
                weight,
                timestamp,
                self._stable_sample_rank(movie_id, user_id),
            )
            for user_id, (weight, timestamp) in deduped.items()
        ]

        if len(ranked) < self.config["min_item_users"]:
            return [], [], []

        max_item_users = self.config["max_item_users"]
        if max_item_users > 0 and len(ranked) > max_item_users:
            sampled_ranked = self._stratified_sample_item_interactions(
                ranked,
                max_item_users,
            )
        else:
            sampled_ranked = ranked

        sampled_ids = {user_id for user_id, _, _, _ in sampled_ranked}
        coverage_extra_ranked = [
            item
            for item in ranked
            if item[0] in coverage_anchor_users and item[0] not in sampled_ids
        ]

        coverage_pair_sample_size = self.config["coverage_pair_sample_size"]
        if (
            coverage_extra_ranked
            and max_item_users > 0
            and coverage_pair_sample_size > len(sampled_ranked)
        ):
            coverage_sample_ranked = self._stratified_sample_item_interactions(
                ranked,
                coverage_pair_sample_size,
            )
        else:
            coverage_sample_ranked = sampled_ranked

        sampled = sorted((user_id, weight) for user_id, weight, _, _ in sampled_ranked)
        coverage_extra = sorted(
            (user_id, weight) for user_id, weight, _, _ in coverage_extra_ranked
        )
        coverage_sample = sorted(
            (user_id, weight) for user_id, weight, _, _ in coverage_sample_ranked
        )
        return sampled, coverage_extra, coverage_sample

    @staticmethod
    def _stratified_sample_item_interactions(
        ranked: list[tuple[int, float, int, int]],
        sample_size: int,
    ) -> list[tuple[int, float, int, int]]:
        """
        Sample evenly across rating-weight buckets using stable item-user ranks.
        """
        if sample_size <= 0 or len(ranked) <= sample_size:
            return list(ranked)

        buckets: dict[float, list[tuple[int, float, int, int]]] = defaultdict(list)
        for item in ranked:
            buckets[item[1]].append(item)

        bucket_keys = sorted(buckets.keys(), reverse=True)
        for bucket in buckets.values():
            bucket.sort(key=lambda item: (item[3], -item[2], item[0]))

        selected: list[tuple[int, float, int, int]] = []
        offsets = {weight: 0 for weight in bucket_keys}
        while len(selected) < sample_size:
            progressed = False
            for weight in bucket_keys:
                offset = offsets[weight]
                bucket = buckets[weight]
                if offset >= len(bucket):
                    continue
                selected.append(bucket[offset])
                offsets[weight] = offset + 1
                progressed = True
                if len(selected) >= sample_size:
                    break
            if not progressed:
                break

        return selected

    @staticmethod
    def _stable_sample_rank(movie_id: int, user_id: int) -> int:
        """
        Return a deterministic mixed integer for representative item-user caps.
        """
        value = (movie_id * 1000003) ^ (user_id * 9176)
        value ^= value >> 16
        value *= 0x7FEB352D
        value ^= value >> 15
        value *= 0x846CA68B
        value ^= value >> 16
        return value & 0xFFFFFFFF

    def _prepare_temp_dirs(self) -> tuple[Path, Path, Path]:
        """
        Create fresh item, pair, and neighbor shard directories for this run.
        """
        temp_dir = self.config["temp_dir"]
        item_shard_dir = temp_dir / "user_similarity_item_shards"
        pair_shard_dir = temp_dir / "user_similarity_pair_shards"
        neighbor_shard_dir = temp_dir / "user_similarity_neighbor_shards"

        for shard_dir in (item_shard_dir, pair_shard_dir, neighbor_shard_dir):
            shard_dir.mkdir(parents=True, exist_ok=True)
            for shard_file in shard_dir.glob("*.csv"):
                shard_file.unlink()

        self.logger.info("Prepared shard temp directory %s.", temp_dir)
        return item_shard_dir, pair_shard_dir, neighbor_shard_dir

    def _item_shard_path(self, item_shard_dir: Path, shard_id: int) -> Path:
        """
        Return the CSV path for an item shard id.
        """
        return item_shard_dir / f"item_shard_{shard_id:05d}.csv"

    def _pair_shard_path(self, pair_shard_dir: Path, shard_id: int) -> Path:
        """
        Return the CSV path for a pair shard id.
        """
        return pair_shard_dir / f"pair_shard_{shard_id:05d}.csv"

    def _neighbor_shard_path(self, neighbor_shard_dir: Path, shard_id: int) -> Path:
        """
        Return the CSV path for a neighbor shard id.
        """
        return neighbor_shard_dir / f"neighbor_shard_{shard_id:05d}.csv"

    def _item_shard_id(self, movie_id: int) -> int:
        """
        Map all positive users for one item to one shard.
        """
        return movie_id % self.config["shard_count"]

    def _pair_shard_id(self, left_user_id: int, right_user_id: int) -> int:
        """
        Map an undirected user pair to a stable shard id.
        """
        return ((left_user_id * 1000003) + right_user_id) % self.config["shard_count"]

    def _neighbor_shard_id(self, user_id: int) -> int:
        """
        Map all outgoing neighbors for a user to one shard.
        """
        return user_id % self.config["shard_count"]

    def _flush_item_buffers(
        self,
        item_shard_dir: Path,
        buffers: list[list[str]],
    ) -> None:
        """
        Append buffered item rows to their shard files and clear buffers.
        """
        for shard_id, rows in enumerate(buffers):
            if not rows:
                continue
            with self._item_shard_path(item_shard_dir, shard_id).open(
                "a",
                encoding="utf-8",
            ) as handle:
                handle.writelines(rows)
            rows.clear()

    def _flush_pair_buffers(
        self,
        pair_shard_dir: Path,
        buffers: list[list[str]],
    ) -> None:
        """
        Append buffered pair rows to their shard files and clear buffers.
        """
        for shard_id, rows in enumerate(buffers):
            if not rows:
                continue
            with self._pair_shard_path(pair_shard_dir, shard_id).open(
                "a",
                encoding="utf-8",
            ) as handle:
                handle.writelines(rows)
            rows.clear()

    def _flush_neighbor_buffers(
        self,
        neighbor_shard_dir: Path,
        buffers: list[list[str]],
    ) -> None:
        """
        Append buffered neighbor rows to their shard files and clear buffers.
        """
        for shard_id, rows in enumerate(buffers):
            if not rows:
                continue
            with self._neighbor_shard_path(neighbor_shard_dir, shard_id).open(
                "a",
                encoding="utf-8",
            ) as handle:
                handle.writelines(rows)
            rows.clear()

    def _write_item_pairs_to_shards(
        self,
        movie_id: int,
        interactions: list[tuple[int, float, int]],
        buffers: list[list[str]],
        coverage_anchor_users: set[int] | None = None,
    ) -> int:
        """
        Write all user pairs contributed by one item into shard buffers.
        """
        sampled, coverage_extra, coverage_sample = self._select_item_pair_participants(
            movie_id,
            interactions,
            coverage_anchor_users or set(),
        )
        if len(sampled) < 2:
            return 0

        written_count = 0
        for (left_user_id, left_weight), (right_user_id, right_weight) in combinations(sampled, 2):
            self._append_pair_contribution(
                left_user_id,
                left_weight,
                right_user_id,
                right_weight,
                buffers,
            )
            written_count += 1

        for left_user_id, left_weight in coverage_extra:
            for right_user_id, right_weight in coverage_sample:
                if self._append_pair_contribution(
                    left_user_id,
                    left_weight,
                    right_user_id,
                    right_weight,
                    buffers,
                ):
                    written_count += 1

        return written_count

    def _append_pair_contribution(
        self,
        left_user_id: int,
        left_weight: float,
        right_user_id: int,
        right_weight: float,
        buffers: list[list[str]],
    ) -> bool:
        """
        Append one undirected user-pair contribution to the proper pair shard.
        """
        if left_user_id == right_user_id:
            return False
        if left_user_id > right_user_id:
            left_user_id, right_user_id = right_user_id, left_user_id
            left_weight, right_weight = right_weight, left_weight

        shard_id = self._pair_shard_id(left_user_id, right_user_id)
        pair_weight = left_weight * right_weight
        buffers[shard_id].append(f"{left_user_id},{right_user_id},{pair_weight:.12g}\n")
        return True

    def _topk_rows_for_neighbor_shard(
        self,
        shard_path: Path,
    ) -> list[dict[str, Any]]:
        """
        Compute final top-K neighbor JSON rows for one neighbor shard.
        """
        topk_by_user: dict[int, list[tuple[tuple[float, int, int], int, float, int]]] = (
            defaultdict(list)
        )

        with shard_path.open(encoding="utf-8") as handle:
            for line in handle:
                user_raw, neighbor_raw, score_raw, cooccurrence_raw = line.rstrip("\n").split(",")
                user_id = int(user_raw)
                neighbor_user_id = int(neighbor_raw)
                score = float(score_raw)
                cooccurrence = int(cooccurrence_raw)
                rank = (score, cooccurrence, -neighbor_user_id)
                heap = topk_by_user[user_id]
                heap_entry = (rank, neighbor_user_id, score, cooccurrence)
                if len(heap) < self.config["top_k"]:
                    heapq.heappush(heap, heap_entry)
                elif rank > heap[0][0]:
                    heapq.heapreplace(heap, heap_entry)

        output_rows: list[dict[str, Any]] = []
        for user_id, heap in topk_by_user.items():
            neighbors = [
                {
                    "user_id": neighbor_user_id,
                    "score": round(score, 8),
                    "cooccurrence": cooccurrence,
                }
                for _, neighbor_user_id, score, cooccurrence in sorted(
                    heap,
                    key=lambda item: (
                        -item[2],
                        -item[3],
                        item[1],
                    ),
                )
            ]
            output_rows.append({"user_id": user_id, "neighbors": neighbors})

        output_rows.sort(key=lambda item: int(item["user_id"]))
        return output_rows


def main() -> None:
    """
    CLI entrypoint for running only the user-similarity builder.
    """
    parser = UserSimilarityBuilder.build_arg_parser()
    args = parser.parse_args()
    log_level_name = (
        "DEBUG" if getattr(args, "debug", False) else str(getattr(args, "log_level", "INFO")).upper()
    )
    log_level = getattr(logging, log_level_name, logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    builder = UserSimilarityBuilder(UserSimilarityBuilder.config_from_args(args))
    user_count, output_path = builder.run()
    print(f"Wrote {user_count} user similarity rows to {output_path}")


if __name__ == "__main__":
    # Direct builder-only run example:
    #   python3 tools/offline_data_processing/src/builders/user_similarity_builder.py \
    #     --ratings /Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv \
    #     --output tools/offline_data_processing/user_similarity.jsonl \
    #     --top-k 50 --min-rating 3.5 --max-item-users 100 \
    #     --coverage-anchor-items-per-user 3 --coverage-pair-sample-size 300 \
    #     --user-stats-log-every 2000000 \
    #     --item-mapping-log-every 200000 \
    #     --shard-log-divisor 32
    main()
