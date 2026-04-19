#!/usr/bin/env python3
"""
Build recommendation user profiles from MovieLens-style CSV files.
"""
from __future__ import annotations

import argparse
import csv
import json
import logging
import math
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

try:
    from builders.common import collect_movie_tag_evidence, compute_movie_top_tags
except ModuleNotFoundError:
    from common import collect_movie_tag_evidence, compute_movie_top_tags


ROOT_DIR = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class RatingInteraction:
    """
    One user-item rating event.
    """

    movie_id: int
    rating: float
    timestamp: int


@dataclass
class InterestAccumulator:
    """
    Accumulated score metadata for a user interest.
    """

    score: float = 0.0
    support: int = 0
    update_time: int = 0


class UserProfileBuilder:
    """
    Convert MovieLens ratings, movies, and tags into user profile documents.

    The ratings file is streamed user by user when it is sorted by userId, which
    is the layout used by the MovieLens 32M ratings.csv file.
    """

    DEFAULT_CONFIG: dict[str, Any] = {
        "ratings_path": ROOT_DIR / "demo_ratings.csv",
        "movies_path": ROOT_DIR / "demo_movies.csv",
        "tags_path": ROOT_DIR / "demo_tags.csv",
        "output_path": ROOT_DIR / "user_profiles.jsonl",
        "output_format": "jsonl",
        "ratings_sorted_by_user": True,
        "positive_min_rating": 4.0,
        "weak_positive_min_rating": 3.0,
        "negative_max_rating": 2.0,
        "max_rating": 5.0,
        "min_rating": 0.5,
        "positive_min_weight": 0.75,
        "weak_min_weight": 0.25,
        "weak_max_weight": 0.65,
        "negative_min_weight": 0.4,
        "recency_half_life_days": 365.0,
        "recency_floor": 0.2,
        "max_liked_items": 100,
        "max_recent_liked_items": 30,
        "max_interested_items": 50,
        "max_rated_items": 300,
        "max_negative_items": 50,
        "top_genres": 10,
        "top_tags": 30,
        "top_negative_genres": 10,
        "top_negative_tags": 20,
        "item_tag_top_k": 10,
        "item_tag_min_weight": 0.0,
        "item_tag_min_relative_weight": 0.3,
        "interest_confidence_support": 5,
        "profile_confidence_rating_count": 50,
        "username_template": "user_{user_id}",
        "display_name_template": "User {user_id}",
        "log_level": "INFO",
        "rating_log_every": 2000000,
        "user_log_every": 50000,
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
        for key in ("ratings_path", "movies_path", "tags_path", "output_path"):
            normalized[key] = Path(normalized[key])

        normalized["output_format"] = str(normalized["output_format"]).lower()
        if normalized["output_format"] not in {"jsonl", "json"}:
            raise ValueError("output_format must be either 'jsonl' or 'json'")

        normalized["ratings_sorted_by_user"] = bool(normalized["ratings_sorted_by_user"])
        for key in (
            "positive_min_rating",
            "weak_positive_min_rating",
            "negative_max_rating",
            "max_rating",
            "min_rating",
            "positive_min_weight",
            "weak_min_weight",
            "weak_max_weight",
            "negative_min_weight",
            "recency_half_life_days",
            "recency_floor",
        ):
            normalized[key] = float(normalized[key])

        for key in (
            "max_liked_items",
            "max_recent_liked_items",
            "max_interested_items",
            "max_rated_items",
            "max_negative_items",
            "top_genres",
            "top_tags",
            "top_negative_genres",
            "top_negative_tags",
            "item_tag_top_k",
            "interest_confidence_support",
            "profile_confidence_rating_count",
            "rating_log_every",
            "user_log_every",
        ):
            normalized[key] = int(normalized[key])

        normalized["item_tag_min_weight"] = float(normalized["item_tag_min_weight"])
        normalized["item_tag_min_relative_weight"] = float(
            normalized["item_tag_min_relative_weight"]
        )
        normalized["log_level"] = str(normalized["log_level"]).upper()
        normalized["username_template"] = str(normalized["username_template"])
        normalized["display_name_template"] = str(normalized["display_name_template"])

        if normalized["positive_min_rating"] <= normalized["weak_positive_min_rating"]:
            raise ValueError("positive_min_rating must be greater than weak_positive_min_rating")
        if normalized["negative_max_rating"] >= normalized["weak_positive_min_rating"]:
            raise ValueError("negative_max_rating must be less than weak_positive_min_rating")
        if normalized["max_rating"] <= normalized["positive_min_rating"]:
            raise ValueError("max_rating must be greater than positive_min_rating")
        if normalized["negative_max_rating"] <= normalized["min_rating"]:
            raise ValueError("negative_max_rating must be greater than min_rating")
        if not 0.0 <= normalized["recency_floor"] <= 1.0:
            raise ValueError("recency_floor must be between 0 and 1")
        if normalized["rating_log_every"] <= 0:
            raise ValueError("rating_log_every must be positive")
        if normalized["user_log_every"] <= 0:
            raise ValueError("user_log_every must be positive")
        return normalized

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
        """
        Create the standalone CLI parser for profile generation.
        """
        parser = argparse.ArgumentParser(
            description="Build user profiles from MovieLens-style CSV inputs."
        )
        parser.add_argument("--ratings", dest="ratings_path", help="Path to ratings.csv.")
        parser.add_argument("--movies", dest="movies_path", help="Path to movies.csv.")
        parser.add_argument("--tags", dest="tags_path", help="Path to tags.csv.")
        parser.add_argument("--output", dest="output_path", help="Path to write profiles.")
        parser.add_argument(
            "--output-format",
            dest="output_format",
            choices=("jsonl", "json"),
            help="Whether to write line-delimited JSON or a JSON array.",
        )
        parser.add_argument(
            "--ratings-unsorted",
            dest="ratings_sorted_by_user",
            action="store_false",
            help="Use this if ratings.csv is not grouped by userId.",
        )
        parser.add_argument("--positive-min-rating", dest="positive_min_rating", type=float)
        parser.add_argument("--weak-positive-min-rating", dest="weak_positive_min_rating", type=float)
        parser.add_argument("--negative-max-rating", dest="negative_max_rating", type=float)
        parser.add_argument("--recency-half-life-days", dest="recency_half_life_days", type=float)
        parser.add_argument("--recency-floor", dest="recency_floor", type=float)
        parser.add_argument("--max-liked-items", dest="max_liked_items", type=int)
        parser.add_argument("--max-recent-liked-items", dest="max_recent_liked_items", type=int)
        parser.add_argument("--max-interested-items", dest="max_interested_items", type=int)
        parser.add_argument("--max-rated-items", dest="max_rated_items", type=int)
        parser.add_argument("--max-negative-items", dest="max_negative_items", type=int)
        parser.add_argument("--top-genres", dest="top_genres", type=int)
        parser.add_argument("--top-tags", dest="top_tags", type=int)
        parser.add_argument("--top-negative-genres", dest="top_negative_genres", type=int)
        parser.add_argument("--top-negative-tags", dest="top_negative_tags", type=int)
        parser.add_argument("--item-tag-top-k", dest="item_tag_top_k", type=int)
        parser.add_argument("--item-tag-min-weight", dest="item_tag_min_weight", type=float)
        parser.add_argument(
            "--item-tag-min-relative-weight",
            dest="item_tag_min_relative_weight",
            type=float,
        )
        parser.add_argument("--username-template", dest="username_template")
        parser.add_argument("--display-name-template", dest="display_name_template")
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
        parser.add_argument("--rating-log-every", dest="rating_log_every", type=int)
        parser.add_argument("--user-log-every", dest="user_log_every", type=int)
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
            elif value is not None:
                config[key] = value
        return config

    @staticmethod
    def parse_genres(raw_genres: str) -> list[str]:
        """
        Split the MovieLens pipe-delimited genre field into a list.
        """
        if not raw_genres or raw_genres == "(no genres listed)":
            return []
        return [genre for genre in raw_genres.split("|") if genre]

    @staticmethod
    def count_csv_data_rows(path: Path) -> int:
        """
        Count CSV data rows, excluding the header row.
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
            logging.getLogger(UserProfileBuilder.__name__).warning(
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

    def read_movie_genres(self) -> dict[int, list[str]]:
        """
        Load movie -> genres metadata from movies.csv.
        """
        movies_path = self.config["movies_path"]
        self.logger.info("Loading movie genres from %s.", movies_path)
        movie_genres: dict[int, list[str]] = {}
        with movies_path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            expected_fields = {"movieId", "genres"}
            missing_fields = expected_fields - set(reader.fieldnames or [])
            if missing_fields:
                raise ValueError(f"Movies CSV is missing required fields: {sorted(missing_fields)}")

            for row in reader:
                movie_genres[int(row["movieId"])] = self.parse_genres(row.get("genres", ""))

        self.logger.info("Loaded genres for %s movies.", len(movie_genres))
        return movie_genres

    def build_movie_top_tags(self, valid_movie_ids: set[int]) -> dict[int, list[dict[str, Any]]]:
        """
        Build movie -> top normalized tags from tags.csv.
        """
        tags_path = self.config["tags_path"]
        if not tags_path.exists():
            self.logger.warning("Tags file %s does not exist; user tag profiles will be empty.", tags_path)
            return {}

        _, tag_user_sets = collect_movie_tag_evidence(
            tags_path=tags_path,
            valid_movie_ids=valid_movie_ids,
            logger=self.logger,
        )
        return compute_movie_top_tags(
            tag_user_sets=tag_user_sets,
            top_k=self.config["item_tag_top_k"],
            min_weight=self.config["item_tag_min_weight"],
            min_relative_weight=self.config["item_tag_min_relative_weight"],
            normalize_weights=True,
            round_digits=None,
            logger=self.logger,
        )

    def _iter_rating_rows(self) -> Iterable[tuple[int, RatingInteraction]]:
        """
        Stream rating rows as userId plus parsed interaction.
        """
        ratings_path = self.config["ratings_path"]
        total_rating_rows = self.get_rating_row_count()
        scanned_rating_count = 0
        self.logger.info("Reading %s rating rows from %s.", total_rating_rows, ratings_path)

        with ratings_path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            expected_fields = {"userId", "movieId", "rating", "timestamp"}
            missing_fields = expected_fields - set(reader.fieldnames or [])
            if missing_fields:
                raise ValueError(f"Ratings CSV is missing required fields: {sorted(missing_fields)}")

            for row_index, row in enumerate(reader, start=1):
                scanned_rating_count = row_index
                if row_index % self.config["rating_log_every"] == 0:
                    self.logger.info(
                        "Scanned %s/%s rating rows (%.1f%%).",
                        row_index,
                        total_rating_rows,
                        self._percent(row_index, total_rating_rows),
                    )
                yield (
                    int(row["userId"]),
                    RatingInteraction(
                        movie_id=int(row["movieId"]),
                        rating=float(row["rating"]),
                        timestamp=int(row["timestamp"]),
                    ),
                )

        self.logger.info("Finished scanning %s rating rows.", scanned_rating_count)

    def iter_user_ratings(self) -> Iterable[tuple[int, list[RatingInteraction]]]:
        """
        Yield each user's ratings.
        """
        if not self.config["ratings_sorted_by_user"]:
            self.logger.warning(
                "ratings_unsorted mode groups all user ratings in memory. "
                "Use sorted ratings for large datasets."
            )
            grouped: dict[int, list[RatingInteraction]] = defaultdict(list)
            for user_id, interaction in self._iter_rating_rows():
                grouped[user_id].append(interaction)
            for user_id in sorted(grouped):
                yield user_id, grouped[user_id]
            return

        current_user_id: int | None = None
        current_ratings: list[RatingInteraction] = []
        completed_user_ids: set[int] = set()
        for user_id, interaction in self._iter_rating_rows():
            if current_user_id is None:
                current_user_id = user_id
            if user_id != current_user_id:
                completed_user_ids.add(current_user_id)
                if user_id in completed_user_ids:
                    raise ValueError(
                        "ratings.csv is not grouped by userId: "
                        f"user_id {user_id} appeared after it was already processed. "
                        "Sort ratings.csv by userId or rerun with --ratings-unsorted for small files."
                    )
                yield current_user_id, current_ratings
                current_user_id = user_id
                current_ratings = []
            current_ratings.append(interaction)

        if current_user_id is not None:
            yield current_user_id, current_ratings

    def _recency_weight(self, timestamp: int, latest_timestamp: int) -> float:
        """
        Compute a per-user recency multiplier with a configurable floor.
        """
        half_life_days = self.config["recency_half_life_days"]
        if half_life_days <= 0:
            return 1.0
        age_days = max(latest_timestamp - timestamp, 0) / 86400.0
        decay = 0.5 ** (age_days / half_life_days)
        floor = self.config["recency_floor"]
        return floor + (1.0 - floor) * decay

    def _positive_rating_weight(self, rating: float) -> float:
        """
        Map a positive rating into [positive_min_weight, 1].
        """
        ratio = (rating - self.config["positive_min_rating"]) / (
            self.config["max_rating"] - self.config["positive_min_rating"]
        )
        ratio = min(max(ratio, 0.0), 1.0)
        floor = self.config["positive_min_weight"]
        return floor + (1.0 - floor) * ratio

    def _weak_positive_rating_weight(self, rating: float) -> float:
        """
        Map a weak-positive rating into [weak_min_weight, weak_max_weight].
        """
        ratio = (rating - self.config["weak_positive_min_rating"]) / (
            self.config["positive_min_rating"] - self.config["weak_positive_min_rating"]
        )
        ratio = min(max(ratio, 0.0), 1.0)
        floor = self.config["weak_min_weight"]
        ceiling = self.config["weak_max_weight"]
        return floor + (ceiling - floor) * ratio

    def _negative_rating_weight(self, rating: float) -> float:
        """
        Map a low rating into [negative_min_weight, 1].
        """
        ratio = (self.config["negative_max_rating"] - rating) / (
            self.config["negative_max_rating"] - self.config["min_rating"]
        )
        ratio = min(max(ratio, 0.0), 1.0)
        floor = self.config["negative_min_weight"]
        return floor + (1.0 - floor) * ratio

    def _weighted_item(
        self,
        interaction: RatingInteraction,
        weight: float,
    ) -> dict[str, Any]:
        """
        Build a proto-JSON-compatible WeightedItem object.
        """
        return {
            "item_id": interaction.movie_id,
            "weight": round(weight, 6),
            "event_time": interaction.timestamp,
            "source": 1,
            "confidence": round(min(max(weight, 0.0), 1.0), 6),
            "raw_score": round(interaction.rating, 6),
        }

    def _dedupe_interactions(
        self,
        interactions: list[RatingInteraction],
    ) -> list[RatingInteraction]:
        """
        Keep the strongest/latest row when duplicate user-item ratings exist.
        """
        deduped: dict[int, RatingInteraction] = {}
        for interaction in interactions:
            existing = deduped.get(interaction.movie_id)
            if existing is None:
                deduped[interaction.movie_id] = interaction
                continue
            if (interaction.timestamp, interaction.rating) > (existing.timestamp, existing.rating):
                deduped[interaction.movie_id] = interaction
        return list(deduped.values())

    def _rank_weighted_items(
        self,
        items: list[dict[str, Any]],
        limit: int,
        recent_first: bool = False,
    ) -> list[dict[str, Any]]:
        """
        Sort and cap weighted item lists.
        """
        if recent_first:
            items.sort(key=lambda item: (-item["event_time"], -item["weight"], item["item_id"]))
        else:
            items.sort(key=lambda item: (-item["weight"], -item["event_time"], item["item_id"]))
        if limit > 0:
            return items[:limit]
        return items

    def _build_interest_map(
        self,
        interactions: list[tuple[RatingInteraction, float]],
        movie_genres: dict[int, list[str]],
        movie_top_tags: dict[int, list[dict[str, Any]]],
    ) -> tuple[
        dict[str, InterestAccumulator],
        dict[str, InterestAccumulator],
    ]:
        """
        Aggregate genre and tag scores from weighted item interactions.
        """
        genre_scores: dict[str, InterestAccumulator] = defaultdict(InterestAccumulator)
        tag_scores: dict[str, InterestAccumulator] = defaultdict(InterestAccumulator)

        for interaction, item_weight in interactions:
            genres = movie_genres.get(interaction.movie_id, [])
            genre_share = item_weight / max(len(genres), 1)
            for genre_name in genres:
                accumulator = genre_scores[genre_name]
                accumulator.score += genre_share
                accumulator.support += 1
                accumulator.update_time = max(accumulator.update_time, interaction.timestamp)

            for tag in movie_top_tags.get(interaction.movie_id, []):
                key = str(tag["tag"])
                accumulator = tag_scores[key]
                accumulator.score += item_weight * float(tag["weight"])
                accumulator.support += 1
                accumulator.update_time = max(accumulator.update_time, interaction.timestamp)

        return genre_scores, tag_scores

    def _materialize_genres(
        self,
        scores: dict[str, InterestAccumulator],
        limit: int,
    ) -> list[dict[str, Any]]:
        """
        Convert accumulated genre scores to proto-JSON-compatible objects.
        """
        if not scores:
            return []

        max_score = max(accumulator.score for accumulator in scores.values())
        support_target = max(self.config["interest_confidence_support"], 1)
        rows: list[dict[str, Any]] = []
        for genre_name, accumulator in scores.items():
            rows.append(
                {
                    "genre": genre_name,
                    "weight": round(accumulator.score / max_score, 6) if max_score > 0 else 0.0,
                    "confidence": round(min(accumulator.support / support_target, 1.0), 6),
                    "update_time": accumulator.update_time,
                }
            )

        rows.sort(key=lambda item: (-item["weight"], -item["confidence"], item["genre"]))
        if limit > 0:
            return rows[:limit]
        return rows

    def _materialize_tags(
        self,
        scores: dict[str, InterestAccumulator],
        limit: int,
    ) -> list[dict[str, Any]]:
        """
        Convert accumulated tag scores to proto-JSON-compatible objects.
        """
        if not scores:
            return []

        max_score = max(accumulator.score for accumulator in scores.values())
        support_target = max(self.config["interest_confidence_support"], 1)
        rows: list[dict[str, Any]] = []
        for tag_name, accumulator in scores.items():
            rows.append(
                {
                    "tag": tag_name,
                    "weight": round(accumulator.score / max_score, 6) if max_score > 0 else 0.0,
                    "confidence": round(min(accumulator.support / support_target, 1.0), 6),
                    "update_time": accumulator.update_time,
                }
            )

        rows.sort(key=lambda item: (-item["weight"], -item["confidence"], item["tag"]))
        if limit > 0:
            return rows[:limit]
        return rows

    def _interest_entropy(self, genre_scores: dict[str, InterestAccumulator]) -> float:
        """
        Compute normalized entropy for positive genre interests.
        """
        positive_scores = [accumulator.score for accumulator in genre_scores.values() if accumulator.score > 0]
        if len(positive_scores) <= 1:
            return 0.0
        total = sum(positive_scores)
        entropy = -sum((score / total) * math.log(score / total) for score in positive_scores)
        return entropy / math.log(len(positive_scores))

    def build_profile(
        self,
        user_id: int,
        ratings: list[RatingInteraction],
        movie_genres: dict[int, list[str]],
        movie_top_tags: dict[int, list[dict[str, Any]]],
    ) -> dict[str, Any]:
        """
        Build one user profile document.
        """
        interactions = self._dedupe_interactions(ratings)
        latest_timestamp = max(interaction.timestamp for interaction in interactions)
        earliest_timestamp = min(interaction.timestamp for interaction in interactions)

        liked_items: list[dict[str, Any]] = []
        recent_liked_items: list[dict[str, Any]] = []
        interested_items: list[dict[str, Any]] = []
        negative_items: list[dict[str, Any]] = []
        positive_interest_inputs: list[tuple[RatingInteraction, float]] = []
        negative_interest_inputs: list[tuple[RatingInteraction, float]] = []
        seen_interactions = sorted(
            interactions,
            key=lambda interaction: (-interaction.timestamp, -interaction.rating, interaction.movie_id),
        )
        rated_items = [interaction.movie_id for interaction in seen_interactions]
        if self.config["max_rated_items"] > 0:
            rated_items = rated_items[: self.config["max_rated_items"]]

        ratings_sum = 0.0
        positive_count = 0
        weak_positive_count = 0
        negative_count = 0

        for interaction in interactions:
            ratings_sum += interaction.rating
            recency = self._recency_weight(interaction.timestamp, latest_timestamp)
            if interaction.rating >= self.config["positive_min_rating"]:
                positive_count += 1
                weight = self._positive_rating_weight(interaction.rating) * recency
                liked_items.append(self._weighted_item(interaction, weight))
                recent_liked_items.append(self._weighted_item(interaction, weight))
                positive_interest_inputs.append((interaction, weight))
            elif interaction.rating >= self.config["weak_positive_min_rating"]:
                weak_positive_count += 1
                weight = self._weak_positive_rating_weight(interaction.rating) * recency
                interested_items.append(self._weighted_item(interaction, weight))
            elif interaction.rating <= self.config["negative_max_rating"]:
                negative_count += 1
                weight = self._negative_rating_weight(interaction.rating) * recency
                negative_items.append(self._weighted_item(interaction, weight))
                negative_interest_inputs.append((interaction, weight))

        liked_items = self._rank_weighted_items(liked_items, self.config["max_liked_items"])
        recent_liked_items = self._rank_weighted_items(
            recent_liked_items,
            self.config["max_recent_liked_items"],
            recent_first=True,
        )
        interested_items = self._rank_weighted_items(
            interested_items,
            self.config["max_interested_items"],
        )
        negative_items = self._rank_weighted_items(negative_items, self.config["max_negative_items"])

        genre_scores, tag_scores = self._build_interest_map(
            positive_interest_inputs,
            movie_genres,
            movie_top_tags,
        )
        negative_genre_scores, negative_tag_scores = self._build_interest_map(
            negative_interest_inputs,
            movie_genres,
            movie_top_tags,
        )

        rating_count = len(interactions)
        avg_rating = ratings_sum / rating_count if rating_count else 0.0
        variance = (
            sum((interaction.rating - avg_rating) ** 2 for interaction in interactions) / rating_count
            if rating_count
            else 0.0
        )
        confidence_denominator = math.log1p(max(self.config["profile_confidence_rating_count"], 1))
        profile_confidence = min(math.log1p(rating_count) / confidence_denominator, 1.0)

        return {
            "user_id": user_id,
            "demographics": {
                "username": self.config["username_template"].format(user_id=user_id),
                "display_name": self.config["display_name_template"].format(user_id=user_id),
                "birth_year": 0,
                "gender": "",
                "location_id": 0,
            },
            "social": {
                "following": [],
                "followers": [],
            },
            "interests": {
                "genres": self._materialize_genres(
                    genre_scores,
                    self.config["top_genres"],
                ),
                "tags": self._materialize_tags(
                    tag_scores,
                    self.config["top_tags"],
                ),
            },
            "negative_feedbacks": {
                "items": negative_items,
                "genres": self._materialize_genres(
                    negative_genre_scores,
                    self.config["top_negative_genres"],
                ),
                "tags": self._materialize_tags(
                    negative_tag_scores,
                    self.config["top_negative_tags"],
                ),
            },
            "behaviors": {
                "liked_items": liked_items,
                "recent_liked_items": recent_liked_items,
                "interested_items": interested_items,
                "rated_items": rated_items,
            },
            "embedding_sets": [],
            "stats": {
                "rating_count": rating_count,
                "positive_rating_count": positive_count,
                "weak_positive_rating_count": weak_positive_count,
                "negative_rating_count": negative_count,
                "avg_rating": round(avg_rating, 6),
                "rating_stddev": round(math.sqrt(variance), 6),
                "first_event_time": earliest_timestamp,
                "last_event_time": latest_timestamp,
                "active_timeframe": latest_timestamp - earliest_timestamp,
                "profile_confidence": round(profile_confidence, 6),
                "interest_entropy": round(self._interest_entropy(genre_scores), 6),
            },
        }

    def iter_profiles(self) -> Iterable[dict[str, Any]]:
        """
        Build profiles lazily from CSV inputs.
        """
        movie_genres = self.read_movie_genres()
        movie_top_tags = self.build_movie_top_tags(set(movie_genres))

        processed_user_count = 0
        for user_id, ratings in self.iter_user_ratings():
            if not ratings:
                continue
            processed_user_count += 1
            if processed_user_count % self.config["user_log_every"] == 0:
                self.logger.info("Built %s user profiles so far.", processed_user_count)
            yield self.build_profile(user_id, ratings, movie_genres, movie_top_tags)

        self.logger.info("Built %s user profiles.", processed_user_count)

    def write_output(self) -> tuple[int, Path]:
        """
        Write profile documents as JSON Lines or a streaming JSON array.
        """
        output_path = self.config["output_path"]
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_format = self.config["output_format"]
        self.logger.info("Writing user profiles to %s as %s.", output_path, output_format)

        profile_count = 0
        with output_path.open("w", encoding="utf-8") as handle:
            if output_format == "json":
                handle.write("[")
                for profile in self.iter_profiles():
                    if profile_count > 0:
                        handle.write(",")
                    handle.write(json.dumps(profile, ensure_ascii=False, separators=(",", ":")))
                    profile_count += 1
                handle.write("]\n")
                self.logger.info("Finished writing %s profiles to %s.", profile_count, output_path)
                return profile_count, output_path

            for profile in self.iter_profiles():
                handle.write(json.dumps(profile, ensure_ascii=False, separators=(",", ":")))
                handle.write("\n")
                profile_count += 1

        self.logger.info("Finished writing %s profiles to %s.", profile_count, output_path)
        return profile_count, output_path

    def run(self) -> tuple[int, Path]:
        """
        Build user profiles and write them to disk.
        """
        self.logger.info("UserProfileBuilder run started.")
        result = self.write_output()
        self.logger.info("UserProfileBuilder run finished.")
        return result


def main() -> None:
    """
    CLI entrypoint for running only the user-profile builder.
    """
    parser = UserProfileBuilder.build_arg_parser()
    args = parser.parse_args()
    log_level_name = (
        "DEBUG" if getattr(args, "debug", False) else str(getattr(args, "log_level", "INFO")).upper()
    )
    log_level = getattr(logging, log_level_name, logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    builder = UserProfileBuilder(UserProfileBuilder.config_from_args(args))
    profile_count, output_path = builder.run()
    print(f"Wrote {profile_count} user profiles to {output_path}")


if __name__ == "__main__":
    # Direct builder-only run example:
    #   python3 tools/offline_data_processing/src/builders/user_profile_builder.py \
    #     --ratings /Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv \
    #     --movies /Volumes/DataBase/Work/raw_dataset_32m_rating/movies.csv \
    #     --tags /Volumes/DataBase/Work/raw_dataset_32m_rating/tags.csv \
    #     --output tools/offline_data_processing/user_profiles.jsonl
    main()
