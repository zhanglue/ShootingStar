#!/usr/bin/env python3
"""
Shared helpers for offline data builders.
"""
from __future__ import annotations

import csv
import logging
import math
import re
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


MULTI_SPACE_RE = re.compile(r"\s+")


def normalize_spaces(text: str) -> str:
    """
    Collapse repeated whitespace after trimming the input text.
    """
    return MULTI_SPACE_RE.sub(" ", text.strip())


def normalize_tag(text: str) -> str:
    """
    Normalize user-provided tags for grouping and scoring.
    """
    normalized = unicodedata.normalize("NFKC", text).lower()
    return normalize_spaces(normalized)


def collect_movie_tag_evidence(
    tags_path: Path,
    valid_movie_ids: set[int],
    logger: logging.Logger | None = None,
) -> tuple[dict[int, Counter[str]], dict[int, dict[str, set[str]]]]:
    """
    Collect raw tag counts and unique tag-user sets for known movies.
    """
    if logger is not None:
        logger.info("Collecting tags from %s for %s valid movies.", tags_path, len(valid_movie_ids))

    tag_row_counts: dict[int, Counter[str]] = defaultdict(Counter)
    tag_user_sets: dict[int, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))

    with tags_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        expected_fields = {"userId", "movieId", "tag"}
        missing_fields = expected_fields - set(reader.fieldnames or [])
        if missing_fields:
            raise ValueError(f"Tags CSV is missing required fields: {sorted(missing_fields)}")

        for row in reader:
            movie_id = int(row["movieId"])
            if movie_id not in valid_movie_ids:
                continue

            tag = normalize_tag(row.get("tag", ""))
            if not tag:
                continue

            user_id = row.get("userId", "").strip()
            tag_row_counts[movie_id][tag] += 1
            if user_id:
                tag_user_sets[movie_id][tag].add(user_id)

    if logger is not None:
        logger.info("Collected tags for %s movies with tag data.", len(tag_user_sets))
    return tag_row_counts, tag_user_sets


def compute_movie_top_tags(
    tag_user_sets: dict[int, dict[str, set[str]]],
    top_k: int,
    min_weight: float,
    min_relative_weight: float,
    *,
    normalize_weights: bool = False,
    round_digits: int | None = 4,
    logger: logging.Logger | None = None,
) -> dict[int, list[dict[str, Any]]]:
    """
    Score movie tags with a simple TF-IDF variant and keep top tags.
    """
    if logger is not None:
        logger.info(
            "Computing top tags with top_k=%s, min_weight=%s, min_relative_weight=%s.",
            top_k,
            min_weight,
            min_relative_weight,
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
            idf = math.log((1.0 + document_count) / (1.0 + document_frequency[tag])) + 1.0
            scored_tags.append((tag, tf * idf, user_count))

        scored_tags.sort(key=lambda item: (-item[1], -item[2], item[0]))
        if not scored_tags:
            results[movie_id] = []
            continue

        top_score = scored_tags[0][1]
        relative_cutoff = top_score * min_relative_weight
        kept_tags: list[dict[str, Any]] = []

        for index, (tag, score, _) in enumerate(scored_tags):
            if index >= top_k:
                break
            if index > 0 and score < min_weight:
                continue
            if index > 0 and score < relative_cutoff:
                continue
            kept_tags.append({"tag": tag, "weight": score})

        if not kept_tags:
            tag, score, _ = scored_tags[0]
            kept_tags.append({"tag": tag, "weight": score})

        if normalize_weights:
            max_weight = max(tag["weight"] for tag in kept_tags)
            for tag in kept_tags:
                tag["weight"] = tag["weight"] / max_weight if max_weight > 0 else 0.0

        if round_digits is not None:
            for tag in kept_tags:
                tag["weight"] = round(tag["weight"], round_digits)

        results[movie_id] = kept_tags

    if logger is not None:
        logger.info("Computed top tags for %s movies.", len(results))
    return results
