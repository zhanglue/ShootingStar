#!/usr/bin/env python3
"""
Build Elasticsearch-ready item index documents from MovieLens CSV files.
"""
from __future__ import annotations

import argparse
import csv
import json
import logging
import math
import re
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


TITLE_YEAR_RE = re.compile(r"^(?P<title>.+?)\s+\((?P<year>\d{4})\)$")
TRAILING_ARTICLE_RE = re.compile(r"^(?P<base>.+), (?P<article>The|A|An)$")
MULTI_SPACE_RE = re.compile(r"\s+")
NON_WORD_RE = re.compile(r"[^\w\s]+", re.UNICODE)
ROOT_DIR = Path(__file__).resolve().parents[2]


class ItemIndexBuilder:
    """
    Convert MovieLens movies/tags/links/ratings CSVs into item documents.

    The builder keeps all final item documents in memory because the expected
    item cardinality is movie-sized, while the potentially large ratings file is
    streamed when aggregating rating statistics.
    """

    DEFAULT_CONFIG: dict[str, Any] = {
        "movies_path": ROOT_DIR / "demo_movies.csv",
        "tags_path": ROOT_DIR / "demo_tags.csv",
        "links_path": ROOT_DIR / "demo_links.csv",
        "ratings_path": ROOT_DIR / "demo_ratings.csv",
        "output_path": ROOT_DIR / "item_index.jsonl",
        "output_format": "jsonl",
        "top_k": 10,
        "min_weight": 0.0,
        "min_relative_weight": 0.3,
        "log_level": "INFO",
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
        Coerce CLI/env-style values into the concrete types used internally.
        """
        normalized = dict(config)
        path_keys = (
            "movies_path",
            "tags_path",
            "links_path",
            "ratings_path",
            "output_path",
        )
        for key in path_keys:
            normalized[key] = Path(normalized[key])

        normalized["output_format"] = str(normalized["output_format"]).lower()
        if normalized["output_format"] not in {"jsonl", "json"}:
            raise ValueError("output_format must be either 'jsonl' or 'json'")

        normalized["top_k"] = int(normalized["top_k"])
        normalized["min_weight"] = float(normalized["min_weight"])
        normalized["min_relative_weight"] = float(normalized["min_relative_weight"])
        normalized["log_level"] = str(normalized["log_level"]).upper()
        return normalized

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
        """
        Create the standalone CLI parser for the item-index build step.
        """
        parser = argparse.ArgumentParser(
            description="Build item index documents from MovieLens-style CSV inputs."
        )
        parser.add_argument("--movies", dest="movies_path", help="Path to the movies CSV.")
        parser.add_argument("--tags", dest="tags_path", help="Path to the tags CSV.")
        parser.add_argument("--links", dest="links_path", help="Path to the links CSV.")
        parser.add_argument(
            "--ratings", dest="ratings_path", help="Path to the ratings CSV."
        )
        parser.add_argument(
            "--output", dest="output_path", help="Path to the output file."
        )
        parser.add_argument(
            "--output-format",
            dest="output_format",
            choices=("jsonl", "json"),
            help="Whether to write line-delimited JSON or a JSON array.",
        )
        parser.add_argument(
            "--top-k",
            dest="top_k",
            type=int,
            help="Maximum number of top tags to keep per movie.",
        )
        parser.add_argument(
            "--min-weight",
            dest="min_weight",
            type=float,
            help="Absolute minimum TF-IDF weight for non-leading tags.",
        )
        parser.add_argument(
            "--min-relative-weight",
            dest="min_relative_weight",
            type=float,
            help="Relative minimum weight versus the strongest tag for non-leading tags.",
        )
        parser.add_argument(
            "--log-level",
            dest="log_level",
            choices=("DEBUG", "INFO", "WARNING", "ERROR"),
            help="Logging level for builder execution.",
        )
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        """
        Return only CLI arguments that were explicitly provided.
        """
        config: dict[str, Any] = {}
        for key, value in vars(args).items():
            if value is not None:
                config[key] = value
        return config

    @staticmethod
    def normalize_spaces(text: str) -> str:
        """
        Collapse repeated whitespace after trimming the input text.
        """
        return MULTI_SPACE_RE.sub(" ", text.strip())

    @staticmethod
    def move_trailing_article(title: str) -> str:
        """
        Convert MovieLens titles like 'Matrix, The' into 'The Matrix'.
        """
        match = TRAILING_ARTICLE_RE.match(title)
        if not match:
            return title
        return f"{match.group('article')} {match.group('base')}"

    @staticmethod
    def parse_genres(raw_genres: str) -> list[str]:
        """
        Split the MovieLens pipe-delimited genre field into a list.
        """
        if not raw_genres or raw_genres == "(no genres listed)":
            return []
        return [genre for genre in raw_genres.split("|") if genre]

    @staticmethod
    def format_imdb_id(raw_imdb_id: str) -> str | None:
        """
        Normalize MovieLens IMDb IDs into the canonical 'tt0000000' shape.
        """
        value = raw_imdb_id.strip()
        if not value:
            return None
        if value.startswith("tt"):
            return value
        if value.isdigit():
            return f"tt{value.zfill(7)}"
        return f"tt{value}"

    @staticmethod
    def format_tmdb_id(raw_tmdb_id: str) -> int | None:
        """
        Parse TMDb IDs, returning None for blank or malformed values.
        """
        value = raw_tmdb_id.strip()
        if not value:
            return None
        try:
            return int(value)
        except ValueError:
            return None

    def normalize_tag(self, text: str) -> str:
        """
        Normalize user-provided tags for grouping and scoring.
        """
        normalized = unicodedata.normalize("NFKC", text).lower()
        return self.normalize_spaces(normalized)

    def normalize_search_text(self, text: str) -> str:
        """
        Normalize text fields into token-friendly search text.
        """
        normalized = unicodedata.normalize("NFKC", text).lower()
        normalized = NON_WORD_RE.sub(" ", normalized)
        return self.normalize_spaces(normalized)

    def parse_title_and_year(self, title_raw: str) -> tuple[str, int | None]:
        """
        Extract a trailing release year from a MovieLens title when present.
        """
        match = TITLE_YEAR_RE.match(title_raw)
        if not match:
            return self.move_trailing_article(title_raw), None
        return self.move_trailing_article(match.group("title")), int(match.group("year"))

    def read_movies(self) -> list[dict[str, str]]:
        """
        Load the movie rows that define the final item universe.
        """
        self.logger.info("Loading movies from %s.", self.config["movies_path"])
        movies: list[dict[str, str]] = []
        with self.config["movies_path"].open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                movies.append(row)
        self.logger.info("Loaded %s movies.", len(movies))
        return movies

    def build_links_map(self) -> dict[int, dict[str, Any]]:
        """
        Build per-movie external ID metadata from links.csv.
        """
        self.logger.info("Loading links from %s.", self.config["links_path"])
        links: dict[int, dict[str, Any]] = {}
        with self.config["links_path"].open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                movie_id = int(row["movieId"])
                links[movie_id] = {
                    "imdb_id": self.format_imdb_id(row.get("imdbId", "")),
                    "tmdb_id": self.format_tmdb_id(row.get("tmdbId", "")),
                }
        self.logger.info("Built links map for %s movies.", len(links))
        return links

    def build_ratings_map(self) -> dict[int, dict[str, Any]]:
        """
        Stream ratings.csv and compute average rating/count per movie.
        """
        self.logger.info("Aggregating ratings from %s.", self.config["ratings_path"])
        totals: dict[int, float] = defaultdict(float)
        counts: Counter[int] = Counter()

        with self.config["ratings_path"].open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                movie_id = int(row["movieId"])
                rating = float(row["rating"])
                totals[movie_id] += rating
                counts[movie_id] += 1

        ratings: dict[int, dict[str, Any]] = {}
        for movie_id, count in counts.items():
            ratings[movie_id] = {
                "avg": round(totals[movie_id] / count, 4),
                "count": count,
            }
        self.logger.info("Built ratings map for %s movies.", len(ratings))
        return ratings

    def collect_tags(
        self, valid_movie_ids: set[int]
    ) -> tuple[dict[int, Counter[str]], dict[int, dict[str, set[str]]]]:
        """
        Collect raw tag counts and unique tag-user sets for known movies.
        """
        self.logger.info(
            "Collecting tags from %s for %s valid movies.",
            self.config["tags_path"],
            len(valid_movie_ids),
        )
        tag_row_counts: dict[int, Counter[str]] = defaultdict(Counter)
        tag_user_sets: dict[int, dict[str, set[str]]] = defaultdict(
            lambda: defaultdict(set)
        )

        with self.config["tags_path"].open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                movie_id = int(row["movieId"])
                if movie_id not in valid_movie_ids:
                    continue

                tag = self.normalize_tag(row.get("tag", ""))
                if not tag:
                    continue

                user_id = row.get("userId", "").strip()
                tag_row_counts[movie_id][tag] += 1
                if user_id:
                    tag_user_sets[movie_id][tag].add(user_id)

        self.logger.info(
            "Collected tags for %s movies with tag data.",
            len(tag_user_sets),
        )
        return tag_row_counts, tag_user_sets

    def compute_top_tags(
        self, tag_user_sets: dict[int, dict[str, set[str]]]
    ) -> dict[int, list[dict[str, Any]]]:
        """
        Score movie tags with a simple TF-IDF variant and keep top tags.
        """
        self.logger.info(
            "Computing top tags with top_k=%s, min_weight=%s, min_relative_weight=%s.",
            self.config["top_k"],
            self.config["min_weight"],
            self.config["min_relative_weight"],
        )
        document_frequency: Counter[str] = Counter()
        for movie_tags in tag_user_sets.values():
            for tag in movie_tags:
                document_frequency[tag] += 1

        document_count = max(len(tag_user_sets), 1)
        results: dict[int, list[dict[str, Any]]] = {}

        for movie_id, movie_tags in tag_user_sets.items():
            scored_tags: list[tuple[str, float, int]] = []
            for tag, users in movie_tags.items():
                user_count = max(len(users), 1)
                tf = 1.0 + math.log(user_count)
                idf = (
                    math.log((1.0 + document_count) / (1.0 + document_frequency[tag]))
                    + 1.0
                )
                score = tf * idf
                scored_tags.append((tag, score, user_count))

            scored_tags.sort(key=lambda item: (-item[1], -item[2], item[0]))
            if not scored_tags:
                results[movie_id] = []
                continue

            top_score = scored_tags[0][1]
            relative_cutoff = top_score * self.config["min_relative_weight"]
            kept_tags: list[dict[str, Any]] = []

            for index, (tag, score, _) in enumerate(scored_tags):
                if index >= self.config["top_k"]:
                    break
                if index > 0 and score < self.config["min_weight"]:
                    continue
                if index > 0 and score < relative_cutoff:
                    continue
                kept_tags.append({"tag": tag, "weight": round(score, 4)})

            if not kept_tags:
                tag, score, _ = scored_tags[0]
                kept_tags.append({"tag": tag, "weight": round(score, 4)})

            results[movie_id] = kept_tags

        self.logger.info("Computed top tags for %s movies.", len(results))
        return results

    def build_search_fields(
        self, title_norm: str, genres: list[str], top_tags: list[dict[str, Any]]
    ) -> dict[str, str]:
        """
        Assemble denormalized search strings consumed by the ES mapping.
        """
        genre_text = " ".join(self.normalize_search_text(genre) for genre in genres)
        tag_text = " ".join(tag["tag"] for tag in top_tags)
        all_text = self.normalize_spaces(
            " ".join(part for part in (title_norm, genre_text, tag_text) if part)
        )
        return {
            "tag_text": tag_text,
            "all_text": all_text,
        }

    def build_items(self) -> list[dict[str, Any]]:
        """
        Run all input transforms and assemble the final item documents.
        """
        self.logger.info("Starting item document build.")
        # Load side data first so each movie row can be assembled in one pass.
        movies = self.read_movies()
        valid_movie_ids = {int(row["movieId"]) for row in movies}
        links_map = self.build_links_map()
        ratings_map = self.build_ratings_map()
        tag_row_counts, tag_user_sets = self.collect_tags(valid_movie_ids)
        top_tags_map = self.compute_top_tags(tag_user_sets)

        items: list[dict[str, Any]] = []
        for row in movies:
            # Preserve raw title/IDs, but also write normalized search fields.
            movie_id = int(row["movieId"])
            title_raw = row["title"]
            title, year = self.parse_title_and_year(title_raw)
            title_norm = self.normalize_search_text(title)
            genres = self.parse_genres(row.get("genres", ""))
            top_tags = top_tags_map.get(movie_id, [])
            search = self.build_search_fields(
                title_norm=title_norm,
                genres=genres,
                top_tags=top_tags,
            )

            items.append(
                {
                    "item_id": movie_id,
                    "title": title,
                    "title_raw": title_raw,
                    "title_norm": title_norm,
                    "year": year,
                    "genres": genres,
                    "tags": {
                        "top_tags": top_tags,
                        "tag_count": sum(tag_row_counts.get(movie_id, Counter()).values()),
                        "unique_tag_count": len(tag_user_sets.get(movie_id, {})),
                    },
                    "rating": ratings_map.get(movie_id, {"avg": None, "count": 0}),
                    "search": search,
                    "ext": links_map.get(movie_id, {"imdb_id": None, "tmdb_id": None}),
                    }
                )

        self.logger.info("Built %s item documents.", len(items))
        return items

    def write_output(self, items: list[dict[str, Any]]) -> Path:
        """
        Write item documents as JSON Lines or a single JSON array.
        """
        output_path = self.config["output_path"]
        self.logger.info(
            "Writing %s items to %s as %s.",
            len(items),
            output_path,
            self.config["output_format"],
        )
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8") as handle:
            if self.config["output_format"] == "json":
                json.dump(items, handle, ensure_ascii=False, indent=2)
                handle.write("\n")
                self.logger.info("Finished writing output to %s.", output_path)
                return output_path

            for item in items:
                handle.write(json.dumps(item, ensure_ascii=False))
                handle.write("\n")
        self.logger.info("Finished writing output to %s.", output_path)
        return output_path

    def run(self) -> tuple[list[dict[str, Any]], Path]:
        """
        Build item documents and write them to disk.
        """
        self.logger.info("ItemIndexBuilder run started.")
        items = self.build_items()
        output_path = self.write_output(items)
        self.logger.info("ItemIndexBuilder run finished.")
        return items, output_path


def main() -> None:
    """
    CLI entrypoint for running only the item-index builder.
    """
    parser = ItemIndexBuilder.build_arg_parser()
    args = parser.parse_args()
    log_level = getattr(logging, str(getattr(args, "log_level", "INFO")).upper(), logging.INFO)
    logging.basicConfig(level=log_level, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    builder = ItemIndexBuilder(ItemIndexBuilder.config_from_args(args))
    items, output_path = builder.run()
    print(f"Wrote {len(items)} items to {output_path}")


if __name__ == "__main__":
    # Direct builder-only run example:
    #   python3 tools/offline_data_processing/src/builders/item_index_builder.py \
    #     --movies /Volumes/DataBase/Work/raw_dataset_32m_rating/movies.csv \
    #     --tags /Volumes/DataBase/Work/raw_dataset_32m_rating/tags.csv \
    #     --links /Volumes/DataBase/Work/raw_dataset_32m_rating/links.csv \
    #     --ratings /Volumes/DataBase/Work/raw_dataset_32m_rating/ratings.csv \
    #     --output tools/offline_data_processing/item_index.jsonl
    main()
