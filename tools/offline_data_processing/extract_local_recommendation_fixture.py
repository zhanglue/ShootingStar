#!/usr/bin/env python3
"""
Extract a tiny recommendation-engine fixture from large offline JSONL files.

The script streams the source files line by line. It never loads the large
JSONL inputs into memory; only selected users/items and small similarity rows
are retained.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


ROOT_DIR = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIR = Path("tests/testdata/recommendation_engine/local_recommendation_fixture")


@dataclass
class SimilarityRow:
    entity_id: int
    neighbors: list[dict[str, Any]]


@dataclass
class CandidateData:
    user_id: int
    similarity_row: SimilarityRow
    profile: dict[str, Any]
    trigger_seeds: list[tuple[int, float]]
    filter_items: set[int]
    item_cf_candidates: list[tuple[int, float]]
    user_cf_candidates: list[tuple[int, float]]
    neighbor_profile_hits: int
    neighbor_profile_misses: int
    item_cf_filtered_count: int
    user_cf_filtered_count: int
    overlap_count: int
    profile_item_count: int
    bucket_scores: dict[str, float] = field(default_factory=dict)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a small local recommendation fixture from large JSONL data."
    )
    parser.add_argument("--item-index", type=Path, default=ROOT_DIR / "item_index.jsonl")
    parser.add_argument("--item-similarity", type=Path, default=ROOT_DIR / "item_similarity.jsonl")
    parser.add_argument("--user-profiles", type=Path, default=ROOT_DIR / "user_profiles.jsonl")
    parser.add_argument("--user-similarity", type=Path, default=ROOT_DIR / "user_similarity.jsonl")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--target-user-count", type=int, default=10)
    parser.add_argument("--candidate-pool-size", type=int, default=2500)
    parser.add_argument("--max-user-neighbors", type=int, default=10)
    parser.add_argument("--max-trigger-seeds", type=int, default=24)
    parser.add_argument("--max-candidates", type=int, default=20)
    parser.add_argument("--progress-every", type=int, default=100000)
    return parser.parse_args()


def extract_top_level_int(line: str, field_name: str) -> int | None:
    marker = f'"{field_name}":'
    index = line.find(marker)
    if index < 0:
        return None

    index += len(marker)
    while index < len(line) and line[index].isspace():
        index += 1

    sign = 1
    if index < len(line) and line[index] == "-":
        sign = -1
        index += 1

    start = index
    while index < len(line) and line[index].isdigit():
        index += 1
    if index == start:
        return None
    return sign * int(line[start:index])


def deterministic_rank(value: int) -> int:
    digest = hashlib.blake2b(str(value).encode("utf-8"), digest_size=8).digest()
    return int.from_bytes(digest, byteorder="big", signed=False)


def iter_jsonl(path: Path, id_field: str | None = None) -> Iterable[tuple[int | None, dict[str, Any]]]:
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            entity_id = extract_top_level_int(stripped, id_field) if id_field else None
            try:
                yield entity_id, json.loads(stripped)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"Invalid JSON in {path} line {line_number}: {exc}") from exc


def normalize_similarity_row(document: dict[str, Any], entity_field: str, neighbor_field: str) -> SimilarityRow:
    entity_id = int(document[entity_field])
    neighbors: list[dict[str, Any]] = []
    seen: set[int] = set()
    for neighbor in document.get("neighbors", []):
        neighbor_id = int(neighbor[neighbor_field])
        score = float(neighbor.get("score", 0.0))
        if neighbor_id <= 0 or neighbor_id == entity_id or score <= 0.0:
            continue
        if neighbor_id in seen:
            continue
        seen.add(neighbor_id)
        normalized = dict(neighbor)
        normalized[neighbor_field] = neighbor_id
        normalized["score"] = score
        neighbors.append(normalized)
    return SimilarityRow(entity_id=entity_id, neighbors=neighbors)


def push_bounded_min_heap(heap: list[tuple[float, int, SimilarityRow]], limit: int, entry: tuple[float, int, SimilarityRow]) -> None:
    if len(heap) < limit:
        heapq.heappush(heap, entry)
        return
    if entry[0] > heap[0][0]:
        heapq.heapreplace(heap, entry)


def push_bounded_max_rank_heap(heap: list[tuple[int, int, SimilarityRow]], limit: int, entry: tuple[int, int, SimilarityRow]) -> None:
    # Keep the smallest deterministic ranks using a max-heap encoded as negative rank.
    encoded = (-entry[0], entry[1], entry[2])
    if len(heap) < limit:
        heapq.heappush(heap, encoded)
        return
    if encoded > heap[0]:
        heapq.heapreplace(heap, encoded)


def collect_candidate_similarity_rows(args: argparse.Namespace) -> dict[int, SimilarityRow]:
    quality_heap: list[tuple[float, int, SimilarityRow]] = []
    sample_heap: list[tuple[int, int, SimilarityRow]] = []
    quality_limit = max(args.candidate_pool_size, args.target_user_count * 50)
    sample_limit = max(args.candidate_pool_size // 2, args.target_user_count * 25)

    processed = 0
    for _, document in iter_jsonl(args.user_similarity, "user_id"):
        processed += 1
        row = normalize_similarity_row(document, "user_id", "user_id")
        if not row.neighbors:
            continue

        top_neighbors = row.neighbors[: args.max_user_neighbors]
        score_sum = sum(float(neighbor["score"]) for neighbor in top_neighbors)
        quality = score_sum + min(len(row.neighbors), args.max_user_neighbors) * 0.05
        push_bounded_min_heap(quality_heap, quality_limit, (quality, row.entity_id, row))
        push_bounded_max_rank_heap(
            sample_heap,
            sample_limit,
            (deterministic_rank(row.entity_id), row.entity_id, row),
        )

        if args.progress_every > 0 and processed % args.progress_every == 0:
            print(f"scanned user_similarity rows: {processed}", flush=True)

    rows: dict[int, SimilarityRow] = {}
    for _, user_id, row in quality_heap:
        rows[user_id] = row
    for encoded_rank, user_id, row in sample_heap:
        _ = encoded_rank
        rows[user_id] = row
    print(f"candidate gateway users from user_similarity: {len(rows)}", flush=True)
    return rows


def collect_item_ids_from_weighted_items(items: Iterable[dict[str, Any]]) -> list[int]:
    item_ids: list[int] = []
    for item in items:
        item_id = int(item.get("item_id", 0))
        if item_id > 0:
            item_ids.append(item_id)
    return item_ids


def profile_behavior(profile: dict[str, Any]) -> dict[str, Any]:
    return profile.get("behaviors") or {}


def weighted_item_score(item: dict[str, Any]) -> float:
    weight = float(item.get("weight", 0.0))
    return weight if weight > 0.0 else 1.0


def append_trigger_seeds(
    items: Iterable[dict[str, Any]],
    base_score: float,
    seen: set[int],
    seeds: list[tuple[int, float]],
) -> None:
    rank = 0
    for item in items:
        rank += 1
        item_id = int(item.get("item_id", 0))
        if item_id <= 0 or item_id in seen:
            continue
        seen.add(item_id)
        seeds.append((item_id, base_score * weighted_item_score(item) / float(rank)))


def collect_trigger_seeds(profile: dict[str, Any], max_trigger_seeds: int) -> list[tuple[int, float]]:
    behaviors = profile_behavior(profile)
    seen: set[int] = set()
    seeds: list[tuple[int, float]] = []
    append_trigger_seeds(behaviors.get("recent_liked_items", []), 1.0, seen, seeds)
    append_trigger_seeds(behaviors.get("liked_items", []), 0.5, seen, seeds)
    append_trigger_seeds(behaviors.get("interested_items", []), 0.3, seen, seeds)
    seeds.sort(key=lambda item: (-item[1], item[0]))
    return seeds[:max_trigger_seeds]


def collect_filter_items(profile: dict[str, Any]) -> set[int]:
    behaviors = profile_behavior(profile)
    item_ids: set[int] = set()
    for field_name in ("recent_liked_items", "liked_items", "interested_items"):
        item_ids.update(collect_item_ids_from_weighted_items(behaviors.get(field_name, [])))
    for item_id in behaviors.get("rated_items", []):
        if int(item_id) > 0:
            item_ids.add(int(item_id))
    negative_feedbacks = profile.get("negative_feedbacks") or {}
    item_ids.update(collect_item_ids_from_weighted_items(negative_feedbacks.get("items", [])))
    return item_ids


def profile_behavior_item_count(profile: dict[str, Any]) -> int:
    behaviors = profile_behavior(profile)
    item_ids: set[int] = set()
    for field_name in ("recent_liked_items", "liked_items", "interested_items"):
        item_ids.update(collect_item_ids_from_weighted_items(behaviors.get(field_name, [])))
    for item_id in behaviors.get("rated_items", []):
        if int(item_id) > 0:
            item_ids.add(int(item_id))
    return len(item_ids)


def collect_required_profile_ids(candidate_rows: dict[int, SimilarityRow], max_user_neighbors: int) -> set[int]:
    user_ids = set(candidate_rows)
    for row in candidate_rows.values():
        for neighbor in row.neighbors[:max_user_neighbors]:
            neighbor_user_id = int(neighbor["user_id"])
            if neighbor_user_id > 0:
                user_ids.add(neighbor_user_id)
    return user_ids


def collect_profiles(args: argparse.Namespace, user_ids: set[int]) -> dict[int, dict[str, Any]]:
    profiles: dict[int, dict[str, Any]] = {}
    scanned = 0
    with args.user_profiles.open("r", encoding="utf-8") as handle:
        for line in handle:
            scanned += 1
            user_id = extract_top_level_int(line, "user_id")
            if user_id not in user_ids:
                continue
            profiles[user_id] = json.loads(line)
            if len(profiles) == len(user_ids):
                break
            if args.progress_every > 0 and scanned % args.progress_every == 0:
                print(
                    f"scanned user_profiles rows: {scanned}; matched: {len(profiles)}",
                    flush=True,
                )
    print(
        f"profile rows collected: {len(profiles)} of requested {len(user_ids)}",
        flush=True,
    )
    return profiles


def collect_required_trigger_item_ids(
    args: argparse.Namespace,
    candidate_rows: dict[int, SimilarityRow],
    profiles: dict[int, dict[str, Any]],
) -> dict[int, list[tuple[int, float]]]:
    trigger_seeds_by_user: dict[int, list[tuple[int, float]]] = {}
    for user_id in candidate_rows:
        profile = profiles.get(user_id)
        if profile is None:
            continue
        seeds = collect_trigger_seeds(profile, args.max_trigger_seeds)
        if seeds:
            trigger_seeds_by_user[user_id] = seeds
    return trigger_seeds_by_user


def collect_item_similarity_rows(args: argparse.Namespace, item_ids: set[int]) -> dict[int, SimilarityRow]:
    rows: dict[int, SimilarityRow] = {}
    scanned = 0
    with args.item_similarity.open("r", encoding="utf-8") as handle:
        for line in handle:
            scanned += 1
            item_id = extract_top_level_int(line, "item_id")
            if item_id not in item_ids:
                continue
            rows[item_id] = normalize_similarity_row(json.loads(line), "item_id", "item_id")
            if len(rows) == len(item_ids):
                break
            if args.progress_every > 0 and scanned % args.progress_every == 0:
                print(
                    f"scanned item_similarity rows: {scanned}; matched: {len(rows)}",
                    flush=True,
                )
    print(
        f"item similarity rows collected: {len(rows)} of requested {len(item_ids)}",
        flush=True,
    )
    return rows


def add_candidate_score(scores: dict[int, float], item_id: int, contribution: float) -> None:
    if item_id <= 0 or contribution <= 0.0:
        return
    scores[item_id] = scores.get(item_id, 0.0) + contribution


def aggregate_item_cf(
    trigger_seeds: list[tuple[int, float]],
    filter_items: set[int],
    item_similarity_rows: dict[int, SimilarityRow],
    max_candidates: int,
) -> tuple[list[tuple[int, float]], int]:
    scores: dict[int, float] = {}
    filtered_count = 0
    for trigger_item_id, trigger_score in trigger_seeds:
        row = item_similarity_rows.get(trigger_item_id)
        if row is None:
            continue
        for neighbor in row.neighbors[:max_candidates]:
            item_id = int(neighbor["item_id"])
            if item_id in filter_items:
                filtered_count += 1
                continue
            add_candidate_score(scores, item_id, trigger_score * float(neighbor["score"]))
    return sorted(scores.items(), key=lambda item: (-item[1], item[0]))[:max_candidates], filtered_count


def append_neighbor_candidate_items(
    items: Iterable[dict[str, Any]],
    base_score: float,
    seen: set[int],
    scored_items: list[tuple[int, float]],
) -> None:
    rank = 0
    for item in items:
        rank += 1
        item_id = int(item.get("item_id", 0))
        if item_id <= 0 or item_id in seen:
            continue
        seen.add(item_id)
        scored_items.append((item_id, base_score * weighted_item_score(item) / float(rank)))


def collect_neighbor_candidate_items(profile: dict[str, Any]) -> list[tuple[int, float]]:
    behaviors = profile_behavior(profile)
    seen: set[int] = set()
    scored_items: list[tuple[int, float]] = []
    append_neighbor_candidate_items(behaviors.get("recent_liked_items", []), 1.0, seen, scored_items)
    append_neighbor_candidate_items(behaviors.get("liked_items", []), 0.7, seen, scored_items)
    append_neighbor_candidate_items(behaviors.get("interested_items", []), 0.3, seen, scored_items)
    return scored_items


def aggregate_user_cf(
    user_id: int,
    similarity_row: SimilarityRow,
    profiles: dict[int, dict[str, Any]],
    filter_items: set[int],
    max_user_neighbors: int,
    max_candidates: int,
) -> tuple[list[tuple[int, float]], int, int, int]:
    scores: dict[int, float] = {}
    profile_hits = 0
    profile_misses = 0
    filtered_count = 0
    seen_users: set[int] = set()
    for neighbor in similarity_row.neighbors[:max_user_neighbors]:
        neighbor_user_id = int(neighbor["user_id"])
        similarity_score = float(neighbor["score"])
        if neighbor_user_id <= 0 or neighbor_user_id == user_id or similarity_score <= 0.0:
            continue
        if neighbor_user_id in seen_users:
            continue
        seen_users.add(neighbor_user_id)
        profile = profiles.get(neighbor_user_id)
        if profile is None:
            profile_misses += 1
            continue
        profile_hits += 1
        for item_id, item_score in collect_neighbor_candidate_items(profile):
            if item_id in filter_items:
                filtered_count += 1
                continue
            add_candidate_score(scores, item_id, similarity_score * item_score)
    return (
        sorted(scores.items(), key=lambda item: (-item[1], item[0]))[:max_candidates],
        profile_hits,
        profile_misses,
        filtered_count,
    )


def build_candidate_data(
    args: argparse.Namespace,
    candidate_rows: dict[int, SimilarityRow],
    profiles: dict[int, dict[str, Any]],
    trigger_seeds_by_user: dict[int, list[tuple[int, float]]],
    item_similarity_rows: dict[int, SimilarityRow],
) -> list[CandidateData]:
    candidates: list[CandidateData] = []
    for user_id, similarity_row in candidate_rows.items():
        profile = profiles.get(user_id)
        trigger_seeds = trigger_seeds_by_user.get(user_id, [])
        if profile is None or not trigger_seeds:
            continue
        filter_items = collect_filter_items(profile)
        item_cf_candidates, item_cf_filtered = aggregate_item_cf(
            trigger_seeds,
            filter_items,
            item_similarity_rows,
            args.max_candidates,
        )
        (
            user_cf_candidates,
            neighbor_profile_hits,
            neighbor_profile_misses,
            user_cf_filtered,
        ) = aggregate_user_cf(
            user_id,
            similarity_row,
            profiles,
            filter_items,
            args.max_user_neighbors,
            args.max_candidates,
        )
        item_cf_ids = {item_id for item_id, _ in item_cf_candidates}
        user_cf_ids = {item_id for item_id, _ in user_cf_candidates}
        data = CandidateData(
            user_id=user_id,
            similarity_row=similarity_row,
            profile=profile,
            trigger_seeds=trigger_seeds,
            filter_items=filter_items,
            item_cf_candidates=item_cf_candidates,
            user_cf_candidates=user_cf_candidates,
            neighbor_profile_hits=neighbor_profile_hits,
            neighbor_profile_misses=neighbor_profile_misses,
            item_cf_filtered_count=item_cf_filtered,
            user_cf_filtered_count=user_cf_filtered,
            overlap_count=len(item_cf_ids & user_cf_ids),
            profile_item_count=profile_behavior_item_count(profile),
        )
        data.bucket_scores = compute_bucket_scores(data)
        candidates.append(data)
    print(f"candidate users with usable profiles and triggers: {len(candidates)}", flush=True)
    return candidates


def compute_bucket_scores(data: CandidateData) -> dict[str, float]:
    item_count = len(data.item_cf_candidates)
    user_count = len(data.user_cf_candidates)
    total_count = len({item_id for item_id, _ in data.item_cf_candidates} | {item_id for item_id, _ in data.user_cf_candidates})
    filter_pressure = data.item_cf_filtered_count + data.user_cf_filtered_count
    return {
        "dual": min(item_count, user_count) * 10.0 + total_count + data.neighbor_profile_hits,
        "item_strong": item_count * 10.0 - user_count * 2.0 + filter_pressure,
        "user_strong": user_count * 10.0 - item_count * 2.0 + data.neighbor_profile_hits * 2.0,
        "sparse": (100.0 / max(data.profile_item_count, 1)) + total_count,
        "overlap_filter": data.overlap_count * 8.0 + filter_pressure * 3.0 + total_count,
        "content": total_count * 5.0 + len(data.trigger_seeds) + data.neighbor_profile_hits,
    }


def choose_users(candidates: list[CandidateData], target_count: int) -> list[CandidateData]:
    buckets = [
        ("dual", 3),
        ("item_strong", 2),
        ("user_strong", 2),
        ("sparse", 1),
        ("overlap_filter", 1),
        ("content", 1),
    ]
    selected: list[CandidateData] = []
    selected_ids: set[int] = set()

    def add_from_bucket(bucket_name: str, quota: int) -> None:
        nonlocal selected
        ranked = sorted(
            candidates,
            key=lambda data: (-data.bucket_scores[bucket_name], data.user_id),
        )
        for data in ranked:
            if len(selected) >= target_count or quota <= 0:
                return
            if data.user_id in selected_ids:
                continue
            if not data.item_cf_candidates and not data.user_cf_candidates:
                continue
            selected.append(data)
            selected_ids.add(data.user_id)
            quota -= 1

    for bucket_name, quota in buckets:
        add_from_bucket(bucket_name, quota)

    if len(selected) < target_count:
        ranked = sorted(
            candidates,
            key=lambda data: (
                -len({item_id for item_id, _ in data.item_cf_candidates} | {item_id for item_id, _ in data.user_cf_candidates}),
                -data.neighbor_profile_hits,
                data.user_id,
            ),
        )
        for data in ranked:
            if len(selected) >= target_count:
                break
            if data.user_id in selected_ids:
                continue
            if not data.item_cf_candidates and not data.user_cf_candidates:
                continue
            selected.append(data)
            selected_ids.add(data.user_id)

    return selected[:target_count]


def row_to_jsonl(row: SimilarityRow, entity_field: str) -> str:
    return json.dumps(
        {
            entity_field: row.entity_id,
            "neighbors": row.neighbors,
        },
        ensure_ascii=False,
        separators=(",", ":"),
    )


def collect_related_user_ids(selected: list[CandidateData], max_user_neighbors: int) -> set[int]:
    user_ids: set[int] = set()
    for data in selected:
        user_ids.add(data.user_id)
        for neighbor in data.similarity_row.neighbors[:max_user_neighbors]:
            neighbor_user_id = int(neighbor["user_id"])
            if neighbor_user_id > 0:
                user_ids.add(neighbor_user_id)
    return user_ids


def collect_profile_related_item_ids(profile: dict[str, Any]) -> set[int]:
    item_ids = collect_filter_items(profile)
    behaviors = profile_behavior(profile)
    for field_name in ("recent_liked_items", "liked_items", "interested_items"):
        item_ids.update(collect_item_ids_from_weighted_items(behaviors.get(field_name, [])))
    return item_ids


def collect_related_item_ids(
    selected: list[CandidateData],
    profiles_to_write: dict[int, dict[str, Any]],
    item_similarity_rows: dict[int, SimilarityRow],
    max_candidates: int,
) -> set[int]:
    item_ids: set[int] = set()
    for data in selected:
        item_ids.update(item_id for item_id, _ in data.trigger_seeds)
        item_ids.update(data.filter_items)
        item_ids.update(item_id for item_id, _ in data.item_cf_candidates)
        item_ids.update(item_id for item_id, _ in data.user_cf_candidates)
        for trigger_item_id, _ in data.trigger_seeds:
            row = item_similarity_rows.get(trigger_item_id)
            if row is not None:
                item_ids.update(int(neighbor["item_id"]) for neighbor in row.neighbors[:max_candidates])
    for profile in profiles_to_write.values():
        item_ids.update(collect_profile_related_item_ids(profile))
    return item_ids


def collect_item_index_rows(args: argparse.Namespace, item_ids: set[int]) -> dict[int, dict[str, Any]]:
    rows: dict[int, dict[str, Any]] = {}
    scanned = 0
    with args.item_index.open("r", encoding="utf-8") as handle:
        for line in handle:
            scanned += 1
            item_id = extract_top_level_int(line, "item_id")
            if item_id not in item_ids:
                continue
            rows[item_id] = json.loads(line)
            if len(rows) == len(item_ids):
                break
            if args.progress_every > 0 and scanned % args.progress_every == 0:
                print(
                    f"scanned item_index rows: {scanned}; matched: {len(rows)}",
                    flush=True,
                )
    print(
        f"item index rows collected: {len(rows)} of requested {len(item_ids)}",
        flush=True,
    )
    return rows


def write_outputs(
    args: argparse.Namespace,
    selected: list[CandidateData],
    profiles_to_write: dict[int, dict[str, Any]],
    item_similarity_rows: dict[int, SimilarityRow],
    item_index_rows: dict[int, dict[str, Any]],
    related_item_ids: set[int],
) -> None:
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    selected_user_ids = {data.user_id for data in selected}
    trigger_item_ids = {
        item_id
        for data in selected
        for item_id, _ in data.trigger_seeds
        if item_id in item_similarity_rows
    }

    profiles_path = output_dir / "profiles.jsonl"
    with profiles_path.open("w", encoding="utf-8") as handle:
        for user_id in sorted(profiles_to_write):
            handle.write(json.dumps(profiles_to_write[user_id], ensure_ascii=False, separators=(",", ":")))
            handle.write("\n")

    user_similarity_path = output_dir / "user_similarity.jsonl"
    with user_similarity_path.open("w", encoding="utf-8") as handle:
        for data in sorted(selected, key=lambda item: item.user_id):
            row = SimilarityRow(
                entity_id=data.user_id,
                neighbors=data.similarity_row.neighbors[: args.max_user_neighbors],
            )
            handle.write(row_to_jsonl(row, "user_id"))
            handle.write("\n")

    item_similarity_path = output_dir / "item_similarity.jsonl"
    with item_similarity_path.open("w", encoding="utf-8") as handle:
        for item_id in sorted(trigger_item_ids):
            row = item_similarity_rows[item_id]
            handle.write(row_to_jsonl(row, "item_id"))
            handle.write("\n")

    item_index_path = output_dir / "item_index.jsonl"
    with item_index_path.open("w", encoding="utf-8") as handle:
        for item_id in sorted(item_index_rows):
            handle.write(json.dumps(item_index_rows[item_id], ensure_ascii=False, separators=(",", ":")))
            handle.write("\n")

    manifest = {
        "description": "Tiny local fixture extracted from offline recommendation JSONL files.",
        "parameters": {
            "target_user_count": args.target_user_count,
            "max_user_neighbors": args.max_user_neighbors,
            "max_trigger_seeds": args.max_trigger_seeds,
            "max_candidates": args.max_candidates,
        },
        "paths": {
            "profiles": str(profiles_path),
            "user_similarity": str(user_similarity_path),
            "item_similarity": str(item_similarity_path),
            "item_index": str(item_index_path),
        },
        "formats": {
            "profiles": "jsonl",
            "user_similarity": "jsonl",
            "item_similarity": "jsonl",
            "item_index": "jsonl",
        },
        "selected_user_ids": sorted(selected_user_ids),
        "counts": {
            "profiles": len(profiles_to_write),
            "gateway_users": len(selected),
            "user_similarity_rows": len(selected),
            "item_similarity_rows": len(trigger_item_ids),
            "related_item_ids": len(related_item_ids),
            "item_index_rows": len(item_index_rows),
            "missing_item_index_rows": len(related_item_ids - set(item_index_rows)),
        },
        "users": [
            {
                "user_id": data.user_id,
                "profile_item_count": data.profile_item_count,
                "trigger_seed_count": len(data.trigger_seeds),
                "filter_item_count": len(data.filter_items),
                "user_similarity_neighbor_count": len(data.similarity_row.neighbors[: args.max_user_neighbors]),
                "neighbor_profile_hits": data.neighbor_profile_hits,
                "neighbor_profile_misses": data.neighbor_profile_misses,
                "item_cf_candidate_count": len(data.item_cf_candidates),
                "user_cf_candidate_count": len(data.user_cf_candidates),
                "overlap_count": data.overlap_count,
                "item_cf_filtered_count": data.item_cf_filtered_count,
                "user_cf_filtered_count": data.user_cf_filtered_count,
                "top_item_cf_items": [item_id for item_id, _ in data.item_cf_candidates[:5]],
                "top_user_cf_items": [item_id for item_id, _ in data.user_cf_candidates[:5]],
            }
            for data in selected
        ],
    }
    manifest_path = output_dir / "manifest.json"
    with manifest_path.open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    print(f"wrote fixture to: {output_dir}", flush=True)
    print(
        "selected users: "
        + ", ".join(str(user_id) for user_id in manifest["selected_user_ids"]),
        flush=True,
    )


def main() -> None:
    args = parse_args()
    candidate_rows = collect_candidate_similarity_rows(args)
    required_profile_ids = collect_required_profile_ids(candidate_rows, args.max_user_neighbors)
    profiles = collect_profiles(args, required_profile_ids)
    trigger_seeds_by_user = collect_required_trigger_item_ids(args, candidate_rows, profiles)
    required_trigger_item_ids = {
        item_id
        for trigger_seeds in trigger_seeds_by_user.values()
        for item_id, _ in trigger_seeds
    }
    item_similarity_rows = collect_item_similarity_rows(args, required_trigger_item_ids)
    candidate_data = build_candidate_data(
        args,
        candidate_rows,
        profiles,
        trigger_seeds_by_user,
        item_similarity_rows,
    )
    selected = choose_users(candidate_data, args.target_user_count)
    if len(selected) < args.target_user_count:
        raise RuntimeError(
            f"Only selected {len(selected)} usable users; requested {args.target_user_count}."
        )

    related_user_ids = collect_related_user_ids(selected, args.max_user_neighbors)
    profiles_to_write = {
        user_id: profiles[user_id]
        for user_id in related_user_ids
        if user_id in profiles
    }
    related_item_ids = collect_related_item_ids(
        selected,
        profiles_to_write,
        item_similarity_rows,
        args.max_candidates,
    )
    item_index_rows = collect_item_index_rows(args, related_item_ids)
    write_outputs(
        args,
        selected,
        profiles_to_write,
        item_similarity_rows,
        item_index_rows,
        related_item_ids,
    )


if __name__ == "__main__":
    main()
