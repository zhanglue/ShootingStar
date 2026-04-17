#!/usr/bin/env python3
"""
Build item-item collaborative filtering neighbors from MovieLens ratings.
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


class ItemSimilarityBuilder:
    """
    Compute item similarity rows using sharded pair-reduction.

    The ratings file can be large, so this builder streams ratings and writes
    intermediate pair/neighbor CSV shards instead of keeping every pair in
    memory. The final output contains one row per item with its top neighbors.
    """

    DEFAULT_CONFIG: dict[str, Any] = {
        "ratings_path": ROOT_DIR / "demo_ratings.csv",
        "output_path": ROOT_DIR / "item_similarity.jsonl",
        "output_format": "jsonl",
        "min_rating": 4.0,
        "use_rating_weight": False,
        "positive_weight_offset": 3.0,
        "max_user_items": 300,
        "min_item_users": 5,
        "min_cooccurrence": 3,
        "min_similarity": 0.0,
        "top_k": 100,
        "shard_count": 512,
        "shard_buffer_rows": 100000,
        "temp_dir": ROOT_DIR / "temp",
        "ratings_sorted_by_user": True,
        "log_level": "INFO",
        "item_stats_log_every": 2000000,
        "pair_mapping_log_every": 200000,
        "shard_log_divisor": 32,
        "user_log_every": 30000,
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
        normalized["max_user_items"] = int(normalized["max_user_items"])
        normalized["min_item_users"] = int(normalized["min_item_users"])
        normalized["min_cooccurrence"] = int(normalized["min_cooccurrence"])
        normalized["min_similarity"] = float(normalized["min_similarity"])
        normalized["top_k"] = int(normalized["top_k"])
        normalized["shard_count"] = int(normalized["shard_count"])
        normalized["shard_buffer_rows"] = int(normalized["shard_buffer_rows"])
        normalized["temp_dir"] = Path(normalized["temp_dir"])
        normalized["ratings_sorted_by_user"] = bool(normalized["ratings_sorted_by_user"])
        normalized["log_level"] = str(normalized["log_level"]).upper()
        normalized["item_stats_log_every"] = int(normalized["item_stats_log_every"])
        normalized["pair_mapping_log_every"] = int(normalized["pair_mapping_log_every"])
        normalized["shard_log_divisor"] = int(normalized["shard_log_divisor"])
        normalized["user_log_every"] = int(normalized["user_log_every"])

        if normalized["top_k"] <= 0:
            raise ValueError("top_k must be positive")
        if normalized["min_item_users"] <= 0:
            raise ValueError("min_item_users must be positive")
        if normalized["min_cooccurrence"] <= 0:
            raise ValueError("min_cooccurrence must be positive")
        if normalized["shard_count"] <= 0:
            raise ValueError("shard_count must be positive")
        if normalized["shard_buffer_rows"] <= 0:
            raise ValueError("shard_buffer_rows must be positive")
        if normalized["item_stats_log_every"] <= 0:
            raise ValueError("item_stats_log_every must be positive")
        if normalized["pair_mapping_log_every"] <= 0:
            raise ValueError("pair_mapping_log_every must be positive")
        if normalized["shard_log_divisor"] <= 0:
            raise ValueError("shard_log_divisor must be positive")
        if normalized["user_log_every"] <= 0:
            raise ValueError("user_log_every must be positive")
        return normalized

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
        """
        Create the standalone CLI parser for similarity generation.
        """
        parser = argparse.ArgumentParser(
            description="Build item-item similarity data from MovieLens ratings."
        )
        parser.add_argument("--ratings", dest="ratings_path", help="Path to ratings.csv.")
        parser.add_argument(
            "--output",
            dest="output_path",
            help="Path to write item similarity data.",
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
            "--max-user-items",
            dest="max_user_items",
            type=int,
            help="Maximum positive items per user used for pair generation. <=0 disables cap.",
        )
        parser.add_argument(
            "--min-item-users",
            dest="min_item_users",
            type=int,
            help="Minimum positive users required for an item to participate.",
        )
        parser.add_argument(
            "--min-cooccurrence",
            dest="min_cooccurrence",
            type=int,
            help="Minimum number of users shared by an item pair.",
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
            help="Maximum neighbors kept per item.",
        )
        parser.add_argument(
            "--shard-count",
            dest="shard_count",
            type=int,
            help="Number of CSV shards used for pair and neighbor intermediate files.",
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
            "--ratings-unsorted",
            dest="ratings_sorted_by_user",
            action="store_false",
            help="Use this if ratings.csv is not grouped by userId.",
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
            "--item-stats-log-every",
            dest="item_stats_log_every",
            type=int,
            help="Emit an item-stats progress log every N scanned rating rows.",
        )
        parser.add_argument(
            "--pair-mapping-log-every",
            dest="pair_mapping_log_every",
            type=int,
            help="Emit a pair-mapping progress log every N scanned rating rows.",
        )
        parser.add_argument(
            "--shard-log-divisor",
            dest="shard_log_divisor",
            type=int,
            help="Emit shard-reduction progress every total_shards / N processed shards.",
        )
        parser.add_argument(
            "--user-log-every",
            dest="user_log_every",
            type=int,
            help="Emit a pair-generation progress log every N processed users.",
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
            elif key in {"use_rating_weight", "ratings_sorted_by_user"}:
                config[key] = value
            elif value is not None and value is not False:
                config[key] = value
        return config

    def build_item_stats(self) -> tuple[set[int], dict[int, float], Counter[int]]:
        """
        Find valid items and compute vector norms from positive ratings.
        """
        self.logger.info(
            "Building item stats with min_rating=%s, min_item_users=%s.",
            self.config["min_rating"],
            self.config["min_item_users"],
        )
        item_norm_sq: dict[int, float] = defaultdict(float)
        item_user_counts: Counter[int] = Counter()
        positive_rating_count = 0

        for _, movie_id, weight, _ in self._iter_positive_ratings(
            "item stats",
            self.config["item_stats_log_every"],
        ):
            item_user_counts[movie_id] += 1
            item_norm_sq[movie_id] += weight * weight
            positive_rating_count += 1

        valid_items = {
            movie_id
            for movie_id, user_count in item_user_counts.items()
            if user_count >= self.config["min_item_users"] and item_norm_sq[movie_id] > 0
        }
        self.logger.info(
            "Kept %s valid items from %s positive ratings across %s items.",
            len(valid_items),
            positive_rating_count,
            len(item_user_counts),
        )
        return valid_items, item_norm_sq, item_user_counts

    def map_user_pairs_to_pair_shards(
        self,
        valid_items: set[int],
        pair_shard_dir: Path,
    ) -> int:
        """
        Map each user's positive items into weighted item-pair shards.
        """
        self.logger.info(
            "Mapping user interactions to %s pair CSV shards.",
            self.config["shard_count"],
        )
        buffers: list[list[str]] = [[] for _ in range(self.config["shard_count"])]
        buffered_rows = 0
        pair_contribution_count = 0
        processed_user_count = 0

        if self.config["ratings_sorted_by_user"]:
            # The full 32M ratings file is sorted by userId, so this path keeps
            # only one user's interactions in memory at a time.
            current_user_id: int | None = None
            current_interactions: list[tuple[int, float, int]] = []

            for user_id, movie_id, weight, timestamp in self._iter_positive_ratings(
                "pair shard mapping",
                self.config["pair_mapping_log_every"],
            ):
                if movie_id not in valid_items:
                    continue

                if current_user_id is None:
                    current_user_id = user_id
                if user_id != current_user_id:
                    written_count = self._write_user_pairs_to_shards(
                        current_interactions,
                        pair_shard_dir,
                        buffers,
                    )
                    processed_user_count += 1
                    pair_contribution_count += written_count
                    buffered_rows += written_count
                    if buffered_rows >= self.config["shard_buffer_rows"]:
                        self._flush_pair_buffers(pair_shard_dir, buffers)
                        buffered_rows = 0
                    self._log_pair_mapping_progress(
                        processed_user_count,
                        pair_contribution_count,
                    )
                    current_user_id = user_id
                    current_interactions = []

                current_interactions.append((movie_id, weight, timestamp))

            if current_user_id is not None:
                written_count = self._write_user_pairs_to_shards(
                    current_interactions,
                    pair_shard_dir,
                    buffers,
                )
                processed_user_count += 1
                pair_contribution_count += written_count
                buffered_rows += written_count
                self._log_pair_mapping_progress(
                    processed_user_count,
                    pair_contribution_count,
                    force=True,
                )
        else:
            self.logger.warning(
                "ratings_unsorted mode groups all users in memory. "
                "Use sorted ratings for large datasets."
            )
            # This fallback is convenient for small ad-hoc files, but avoid it
            # for the full dataset unless memory has been planned carefully.
            user_interactions: dict[int, list[tuple[int, float, int]]] = defaultdict(list)
            for user_id, movie_id, weight, timestamp in self._iter_positive_ratings(
                "unsorted user grouping",
                self.config["pair_mapping_log_every"],
            ):
                if movie_id in valid_items:
                    user_interactions[user_id].append((movie_id, weight, timestamp))

            total_users = len(user_interactions)
            self.logger.info("Grouped positive interactions for %s users.", total_users)
            for processed_user_count, interactions in enumerate(
                user_interactions.values(),
                start=1,
            ):
                written_count = self._write_user_pairs_to_shards(
                    interactions,
                    pair_shard_dir,
                    buffers,
                )
                pair_contribution_count += written_count
                buffered_rows += written_count
                if buffered_rows >= self.config["shard_buffer_rows"]:
                    self._flush_pair_buffers(pair_shard_dir, buffers)
                    buffered_rows = 0
                self._log_pair_mapping_progress(
                    processed_user_count,
                    pair_contribution_count,
                    total_users=total_users,
                )

        self._flush_pair_buffers(pair_shard_dir, buffers)
        self.logger.info(
            "Wrote %s pair contribution rows from %s processed users.",
            pair_contribution_count,
            processed_user_count,
        )
        return pair_contribution_count

    def reduce_pair_shards_to_neighbor_shards(
        self,
        pair_shard_dir: Path,
        neighbor_shard_dir: Path,
        item_norm_sq: dict[int, float],
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
            pair_user_counts: Counter[tuple[int, int]] = Counter()

            with shard_path.open(encoding="utf-8") as handle:
                for line in handle:
                    left_raw, right_raw, weight_raw = line.rstrip("\n").split(",")
                    pair = (int(left_raw), int(right_raw))
                    pair_weight_sums[pair] += float(weight_raw)
                    pair_user_counts[pair] += 1

            kept_pair_count = 0
            for (left_movie_id, right_movie_id), cooccurrence in pair_user_counts.items():
                # Apply all pair-level thresholds before writing two directed
                # neighbor rows, one for each side of the undirected item pair.
                if cooccurrence < self.config["min_cooccurrence"]:
                    continue

                denominator = math.sqrt(
                    item_norm_sq[left_movie_id] * item_norm_sq[right_movie_id]
                )
                if denominator <= 0:
                    continue

                score = pair_weight_sums[(left_movie_id, right_movie_id)] / denominator
                if score < self.config["min_similarity"]:
                    continue

                rounded_score = round(score, 8)
                left_shard_id = self._neighbor_shard_id(left_movie_id)
                right_shard_id = self._neighbor_shard_id(right_movie_id)
                buffers[left_shard_id].append(
                    f"{left_movie_id},{right_movie_id},{rounded_score:.8g},{cooccurrence}\n"
                )
                buffers[right_shard_id].append(
                    f"{right_movie_id},{left_movie_id},{rounded_score:.8g},{cooccurrence}\n"
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
                len(pair_user_counts),
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
        Keep top-K neighbors per item and write the final output file.
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
                # Each neighbor shard owns a disjoint item-id modulo bucket, so
                # top-K can be computed shard by shard.
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
                            "wrote output rows for %s items."
                        ),
                        processed_shard_count,
                        total_neighbor_shards,
                        percent,
                        output_count,
                    )

            if output_format == "json":
                handle.write("\n]\n")

        self.logger.info("Wrote final similarity output for %s items.", output_count)
        return output_count

    def compute_similarities(self) -> int:
        """
        Run the three-stage similarity pipeline: stats, pairs, top-K output.
        """
        pair_shard_dir, neighbor_shard_dir = self._prepare_temp_dirs()
        valid_items, item_norm_sq, _ = self.build_item_stats()
        self.map_user_pairs_to_pair_shards(valid_items, pair_shard_dir)
        self.reduce_pair_shards_to_neighbor_shards(
            pair_shard_dir,
            neighbor_shard_dir,
            item_norm_sq,
        )
        return self.reduce_neighbor_shards_to_output(neighbor_shard_dir)

    def run(self) -> tuple[int, Path]:
        """
        Compute similarities and return the output row count/path.
        """
        self.logger.info("ItemSimilarityBuilder run started.")
        item_count = self.compute_similarities()
        output_path = self.config["output_path"]
        self.logger.info("ItemSimilarityBuilder run finished.")
        return item_count, output_path

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
            logging.getLogger(ItemSimilarityBuilder.__name__).warning(
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

    def _log_pair_mapping_progress(
        self,
        processed_user_count: int,
        pair_contribution_count: int,
        total_users: int | None = None,
        force: bool = False,
    ) -> None:
        """
        Log user-pair generation progress at the configured user interval.
        """
        if not force and processed_user_count % self.config["user_log_every"] != 0:
            return

        if total_users is None:
            self.logger.info(
                "Processed %s users for pair mapping; wrote %s pair contribution rows so far.",
                processed_user_count,
                pair_contribution_count,
            )
            return

        self.logger.info(
            (
                "Processed %s/%s users for pair mapping (%.1f%%); "
                "wrote %s pair contribution rows so far."
            ),
            processed_user_count,
            total_users,
            self._percent(processed_user_count, total_users),
            pair_contribution_count,
        )

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

    def _normalize_user_interactions(
        self,
        interactions: list[tuple[int, float, int]],
    ) -> list[tuple[int, float]]:
        """
        Deduplicate and cap one user's positive item interactions.
        """
        deduped: dict[int, tuple[float, int]] = {}
        for movie_id, weight, timestamp in interactions:
            existing = deduped.get(movie_id)
            if existing is None or (weight, timestamp) > existing:
                deduped[movie_id] = (weight, timestamp)

        ranked = [
            (movie_id, weight, timestamp)
            for movie_id, (weight, timestamp) in deduped.items()
        ]
        ranked.sort(key=lambda item: (-item[1], -item[2], item[0]))

        max_user_items = self.config["max_user_items"]
        if max_user_items > 0 and len(ranked) > max_user_items:
            ranked = ranked[:max_user_items]

        return sorted((movie_id, weight) for movie_id, weight, _ in ranked)

    def _prepare_temp_dirs(self) -> tuple[Path, Path]:
        """
        Create fresh pair/neighbor shard directories for this run.
        """
        temp_dir = self.config["temp_dir"]
        pair_shard_dir = temp_dir / "pair_shards"
        neighbor_shard_dir = temp_dir / "neighbor_shards"

        for shard_dir in (pair_shard_dir, neighbor_shard_dir):
            shard_dir.mkdir(parents=True, exist_ok=True)
            for shard_file in shard_dir.glob("*.csv"):
                shard_file.unlink()

        self.logger.info("Prepared shard temp directory %s.", temp_dir)
        return pair_shard_dir, neighbor_shard_dir

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

    def _pair_shard_id(self, left_movie_id: int, right_movie_id: int) -> int:
        """
        Map an undirected item pair to a stable shard id.
        """
        return ((left_movie_id * 1000003) + right_movie_id) % self.config["shard_count"]

    def _neighbor_shard_id(self, item_id: int) -> int:
        """
        Map all outgoing neighbors for an item to one shard.
        """
        return item_id % self.config["shard_count"]

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

    def _write_user_pairs_to_shards(
        self,
        interactions: list[tuple[int, float, int]],
        pair_shard_dir: Path,
        buffers: list[list[str]],
    ) -> int:
        """
        Write all item pairs contributed by one user into shard buffers.
        """
        normalized = self._normalize_user_interactions(interactions)
        if len(normalized) < 2:
            return 0

        written_count = 0
        for (left_movie_id, left_weight), (right_movie_id, right_weight) in combinations(
            normalized, 2
        ):
            shard_id = self._pair_shard_id(left_movie_id, right_movie_id)
            pair_weight = left_weight * right_weight
            buffers[shard_id].append(f"{left_movie_id},{right_movie_id},{pair_weight:.12g}\n")
            written_count += 1

        return written_count

    def _topk_rows_for_neighbor_shard(
        self,
        shard_path: Path,
    ) -> list[dict[str, Any]]:
        """
        Compute final top-K neighbor JSON rows for one neighbor shard.
        """
        topk_by_item: dict[int, list[tuple[tuple[float, int, int], int, float, int]]] = (
            defaultdict(list)
        )

        with shard_path.open(encoding="utf-8") as handle:
            for line in handle:
                item_raw, neighbor_raw, score_raw, cooccurrence_raw = line.rstrip("\n").split(",")
                item_id = int(item_raw)
                neighbor_id = int(neighbor_raw)
                score = float(score_raw)
                cooccurrence = int(cooccurrence_raw)
                rank = (score, cooccurrence, -neighbor_id)
                heap = topk_by_item[item_id]
                heap_entry = (rank, neighbor_id, score, cooccurrence)
                if len(heap) < self.config["top_k"]:
                    heapq.heappush(heap, heap_entry)
                elif rank > heap[0][0]:
                    heapq.heapreplace(heap, heap_entry)

        output_rows: list[dict[str, Any]] = []
        for item_id, heap in topk_by_item.items():
            neighbors = [
                {
                    "item_id": neighbor_id,
                    "score": round(score, 8),
                    "cooccurrence": cooccurrence,
                }
                for _, neighbor_id, score, cooccurrence in sorted(
                    heap,
                    key=lambda item: (
                        -item[2],
                        -item[3],
                        item[1],
                    ),
                )
            ]
            output_rows.append({"item_id": item_id, "neighbors": neighbors})

        output_rows.sort(key=lambda item: int(item["item_id"]))
        return output_rows


def main() -> None:
    """
    CLI entrypoint for running only the similarity builder.
    """
    parser = ItemSimilarityBuilder.build_arg_parser()
    args = parser.parse_args()
    log_level_name = (
        "DEBUG" if getattr(args, "debug", False) else str(getattr(args, "log_level", "INFO")).upper()
    )
    log_level = getattr(logging, log_level_name, logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    builder = ItemSimilarityBuilder(ItemSimilarityBuilder.config_from_args(args))
    item_count, output_path = builder.run()
    print(f"Wrote {item_count} item similarity rows to {output_path}")


if __name__ == "__main__":
    # Direct builder-only run example:
    #   python3 tools/offline_data_processing/src/builders/item_similarity_builder.py \
    #     --ratings /Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv \
    #     --output tools/offline_data_processing/item_similarity.jsonl \
    #     --top-k 100 --min-rating 4.0 \
    #     --item-stats-log-every 2000000 \
    #     --pair-mapping-log-every 200000 \
    #     --shard-log-divisor 32
    main()
