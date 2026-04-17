#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import heapq
import json
import logging
import math
from collections import Counter, defaultdict
from itertools import combinations
from pathlib import Path
from typing import Any, Iterable


ROOT_DIR = Path(__file__).resolve().parents[2]


class ItemSimilarityBuilder:
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
        "log_every": 1000000,
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
        normalized["log_every"] = int(normalized["log_every"])

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
        return normalized

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
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
            "--log-every",
            dest="log_every",
            type=int,
            help="Emit a progress log every N scanned rating rows.",
        )
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        config: dict[str, Any] = {}
        for key, value in vars(args).items():
            if key in {"use_rating_weight", "ratings_sorted_by_user"}:
                config[key] = value
            elif value is not None and value is not False:
                config[key] = value
        return config

    def build_item_stats(self) -> tuple[set[int], dict[int, float], Counter[int]]:
        self.logger.info(
            "Building item stats with min_rating=%s, min_item_users=%s.",
            self.config["min_rating"],
            self.config["min_item_users"],
        )
        item_norm_sq: dict[int, float] = defaultdict(float)
        item_user_counts: Counter[int] = Counter()
        positive_rating_count = 0

        for _, movie_id, weight, _ in self._iter_positive_ratings():
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
        self.logger.info(
            "Mapping user interactions to %s pair CSV shards.",
            self.config["shard_count"],
        )
        buffers: list[list[str]] = [[] for _ in range(self.config["shard_count"])]
        buffered_rows = 0
        pair_contribution_count = 0

        if self.config["ratings_sorted_by_user"]:
            current_user_id: int | None = None
            current_interactions: list[tuple[int, float, int]] = []

            for user_id, movie_id, weight, timestamp in self._iter_positive_ratings():
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
                    pair_contribution_count += written_count
                    buffered_rows += written_count
                    if buffered_rows >= self.config["shard_buffer_rows"]:
                        self._flush_pair_buffers(pair_shard_dir, buffers)
                        buffered_rows = 0
                    current_user_id = user_id
                    current_interactions = []

                current_interactions.append((movie_id, weight, timestamp))

            written_count = self._write_user_pairs_to_shards(
                current_interactions,
                pair_shard_dir,
                buffers,
            )
            pair_contribution_count += written_count
            buffered_rows += written_count
        else:
            self.logger.warning(
                "ratings_unsorted mode groups all users in memory. "
                "Use sorted ratings for large datasets."
            )
            user_interactions: dict[int, list[tuple[int, float, int]]] = defaultdict(list)
            for user_id, movie_id, weight, timestamp in self._iter_positive_ratings():
                if movie_id in valid_items:
                    user_interactions[user_id].append((movie_id, weight, timestamp))

            for interactions in user_interactions.values():
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

        self._flush_pair_buffers(pair_shard_dir, buffers)
        self.logger.info("Wrote %s pair contribution rows.", pair_contribution_count)
        return pair_contribution_count

    def reduce_pair_shards_to_neighbor_shards(
        self,
        pair_shard_dir: Path,
        neighbor_shard_dir: Path,
        item_norm_sq: dict[int, float],
    ) -> int:
        self.logger.info("Reducing pair shards into neighbor CSV shards.")
        buffers: list[list[str]] = [[] for _ in range(self.config["shard_count"])]
        buffered_rows = 0
        neighbor_row_count = 0

        for shard_id in range(self.config["shard_count"]):
            shard_path = self._pair_shard_path(pair_shard_dir, shard_id)
            if not shard_path.exists():
                continue

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

            self.logger.info(
                "Reduced pair shard %s: %s unique pairs, %s kept pairs.",
                shard_id,
                len(pair_user_counts),
                kept_pair_count,
            )

        self._flush_neighbor_buffers(neighbor_shard_dir, buffers)
        self.logger.info("Wrote %s neighbor candidate rows.", neighbor_row_count)
        return neighbor_row_count

    def reduce_neighbor_shards_to_output(self, neighbor_shard_dir: Path) -> int:
        output_path = self.config["output_path"]
        output_format = self.config["output_format"]
        output_path.parent.mkdir(parents=True, exist_ok=True)

        self.logger.info(
            "Reducing neighbor shards to final %s output at %s.",
            output_format,
            output_path,
        )
        output_count = 0
        with output_path.open("w", encoding="utf-8") as handle:
            if output_format == "json":
                handle.write("[\n")

            first_json_item = True
            for shard_id in range(self.config["shard_count"]):
                shard_path = self._neighbor_shard_path(neighbor_shard_dir, shard_id)
                if not shard_path.exists():
                    continue

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

            if output_format == "json":
                handle.write("\n]\n")

        self.logger.info("Wrote final similarity output for %s items.", output_count)
        return output_count

    def compute_similarities(self) -> int:
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
        self.logger.info("ItemSimilarityBuilder run started.")
        item_count = self.compute_similarities()
        output_path = self.config["output_path"]
        self.logger.info("ItemSimilarityBuilder run finished.")
        return item_count, output_path

    def _rating_to_weight(self, rating: float) -> float | None:
        if rating < self.config["min_rating"]:
            return None
        if not self.config["use_rating_weight"]:
            return 1.0

        weight = rating - self.config["positive_weight_offset"]
        if weight <= 0:
            return None
        return weight

    def _iter_positive_ratings(self) -> Iterable[tuple[int, int, float, int]]:
        ratings_path = self.config["ratings_path"]
        self.logger.info("Reading MovieLens ratings from %s.", ratings_path)
        with ratings_path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            expected_fields = {"userId", "movieId", "rating", "timestamp"}
            missing_fields = expected_fields - set(reader.fieldnames or [])
            if missing_fields:
                raise ValueError(
                    f"Ratings CSV is missing required fields: {sorted(missing_fields)}"
                )

            for row_index, row in enumerate(reader, start=1):
                if row_index % self.config["log_every"] == 0:
                    self.logger.info("Scanned %s rating rows.", row_index)

                rating = float(row["rating"])
                weight = self._rating_to_weight(rating)
                if weight is None:
                    continue

                yield (
                    int(row["userId"]),
                    int(row["movieId"]),
                    weight,
                    int(row["timestamp"]),
                )

    def _normalize_user_interactions(
        self,
        interactions: list[tuple[int, float, int]],
    ) -> list[tuple[int, float]]:
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
        return pair_shard_dir / f"pair_shard_{shard_id:05d}.csv"

    def _neighbor_shard_path(self, neighbor_shard_dir: Path, shard_id: int) -> Path:
        return neighbor_shard_dir / f"neighbor_shard_{shard_id:05d}.csv"

    def _pair_shard_id(self, left_movie_id: int, right_movie_id: int) -> int:
        return ((left_movie_id * 1000003) + right_movie_id) % self.config["shard_count"]

    def _neighbor_shard_id(self, item_id: int) -> int:
        return item_id % self.config["shard_count"]

    def _flush_pair_buffers(
        self,
        pair_shard_dir: Path,
        buffers: list[list[str]],
    ) -> None:
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
    parser = ItemSimilarityBuilder.build_arg_parser()
    args = parser.parse_args()
    log_level = getattr(logging, str(getattr(args, "log_level", "INFO")).upper(), logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    builder = ItemSimilarityBuilder(ItemSimilarityBuilder.config_from_args(args))
    item_count, output_path = builder.run()
    print(f"Wrote {item_count} item similarity rows to {output_path}")


if __name__ == "__main__":
    main()
