#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
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
ROOT_DIR = Path(__file__).resolve().parent.parent


class ItemIndexBuilder:
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
    }

    def __init__(self, config: dict[str, Any] | None = None) -> None:
        merged = dict(self.DEFAULT_CONFIG)
        if config:
            merged.update(config)
        self.config = self._normalize_config(merged)

    @classmethod
    def _normalize_config(cls, config: dict[str, Any]) -> dict[str, Any]:
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
        return normalized

    @staticmethod
    def normalize_spaces(text: str) -> str:
        return MULTI_SPACE_RE.sub(" ", text.strip())

    def normalize_tag(self, text: str) -> str:
        normalized = unicodedata.normalize("NFKC", text).lower()
        return self.normalize_spaces(normalized)

    def normalize_search_text(self, text: str) -> str:
        normalized = unicodedata.normalize("NFKC", text).lower()
        normalized = NON_WORD_RE.sub(" ", normalized)
        return self.normalize_spaces(normalized)

    @staticmethod
    def move_trailing_article(title: str) -> str:
        match = TRAILING_ARTICLE_RE.match(title)
        if not match:
            return title
        return f"{match.group('article')} {match.group('base')}"

    def parse_title_and_year(self, title_raw: str) -> tuple[str, int | None]:
        match = TITLE_YEAR_RE.match(title_raw)
        if not match:
            return self.move_trailing_article(title_raw), None
        return self.move_trailing_article(match.group("title")), int(match.group("year"))

    @staticmethod
    def parse_genres(raw_genres: str) -> list[str]:
        if not raw_genres or raw_genres == "(no genres listed)":
            return []
        return [genre for genre in raw_genres.split("|") if genre]

    @staticmethod
    def format_imdb_id(raw_imdb_id: str) -> str | None:
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
        value = raw_tmdb_id.strip()
        if not value:
            return None
        try:
            return int(value)
        except ValueError:
            return None

    def read_movies(self) -> list[dict[str, str]]:
        movies: list[dict[str, str]] = []
        with self.config["movies_path"].open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                movies.append(row)
        return movies

    def build_links_map(self) -> dict[int, dict[str, Any]]:
        links: dict[int, dict[str, Any]] = {}
        with self.config["links_path"].open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                movie_id = int(row["movieId"])
                links[movie_id] = {
                    "imdb_id": self.format_imdb_id(row.get("imdbId", "")),
                    "tmdb_id": self.format_tmdb_id(row.get("tmdbId", "")),
                }
        return links

    def build_ratings_map(self) -> dict[int, dict[str, Any]]:
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
        return ratings

    def collect_tags(
        self, valid_movie_ids: set[int]
    ) -> tuple[dict[int, Counter[str]], dict[int, dict[str, set[str]]]]:
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

        return tag_row_counts, tag_user_sets

    def compute_top_tags(
        self, tag_user_sets: dict[int, dict[str, set[str]]]
    ) -> dict[int, list[dict[str, Any]]]:
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

        return results

    def build_search_fields(
        self, title_norm: str, genres: list[str], top_tags: list[dict[str, Any]]
    ) -> dict[str, str]:
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
        movies = self.read_movies()
        valid_movie_ids = {int(row["movieId"]) for row in movies}
        links_map = self.build_links_map()
        ratings_map = self.build_ratings_map()
        tag_row_counts, tag_user_sets = self.collect_tags(valid_movie_ids)
        top_tags_map = self.compute_top_tags(tag_user_sets)

        items: list[dict[str, Any]] = []
        for row in movies:
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

        return items

    def write_output(self, items: list[dict[str, Any]]) -> Path:
        output_path = self.config["output_path"]
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8") as handle:
            if self.config["output_format"] == "json":
                json.dump(items, handle, ensure_ascii=False, indent=2)
                handle.write("\n")
                return output_path

            for item in items:
                handle.write(json.dumps(item, ensure_ascii=False))
                handle.write("\n")
        return output_path

    def run(self) -> tuple[list[dict[str, Any]], Path]:
        items = self.build_items()
        output_path = self.write_output(items)
        return items, output_path

    @classmethod
    def build_arg_parser(cls) -> argparse.ArgumentParser:
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
        return parser

    @classmethod
    def config_from_args(cls, args: argparse.Namespace) -> dict[str, Any]:
        config: dict[str, Any] = {}
        for key, value in vars(args).items():
            if value is not None:
                config[key] = value
        return config


def main() -> None:
    parser = ItemIndexBuilder.build_arg_parser()
    args = parser.parse_args()
    builder = ItemIndexBuilder(ItemIndexBuilder.config_from_args(args))
    items, output_path = builder.run()
    print(f"Wrote {len(items)} items to {output_path}")


if __name__ == "__main__":
    main()
