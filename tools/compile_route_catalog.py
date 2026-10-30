#!/usr/bin/env python3
"""Validate and canonicalize route-catalog-v1 with a self-verifying digest."""

from __future__ import annotations

import argparse
import copy
import hashlib
import heapq
import json
from pathlib import Path
from typing import Any


ZERO_DIGEST = "0" * 64
MAX_STEPS = 10_000
MAX_PREREQUISITES = 64


class RouteCompileError(ValueError):
    pass


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise RouteCompileError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_keys)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RouteCompileError(f"cannot read route catalogue {path}: {error}") from error
    if not isinstance(value, dict):
        raise RouteCompileError("route catalogue root must be an object")
    return value


def _canonical_bytes(catalogue: dict[str, Any]) -> bytes:
    return json.dumps(catalogue, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def compile_catalogue(source: dict[str, Any]) -> tuple[dict[str, Any], str]:
    catalogue = copy.deepcopy(source)
    if catalogue.get("schema_version") != "cpi-207-route-v1":
        raise RouteCompileError("schema_version must be cpi-207-route-v1")
    steps = catalogue.get("steps")
    if not isinstance(steps, list) or not steps or len(steps) > MAX_STEPS:
        raise RouteCompileError("route step count is outside the supported range")

    by_id: dict[str, dict[str, Any]] = {}
    exclusive_groups: dict[str, list[str]] = {}
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise RouteCompileError(f"steps[{index}] must be an object")
        stable_id = step.get("stable_id")
        if not isinstance(stable_id, str) or not stable_id:
            raise RouteCompileError(f"steps[{index}].stable_id must be a non-empty string")
        if stable_id in by_id:
            raise RouteCompileError(f"duplicate route step ID: {stable_id}")
        by_id[stable_id] = step
        for name in ("prerequisites", "required_capabilities"):
            values = step.get(name)
            if not isinstance(values, list) or len(values) > MAX_PREREQUISITES or len(values) != len(set(values)):
                raise RouteCompileError(f"{stable_id}.{name} is invalid or contains duplicates")
            values.sort()
        groups = step.get("prerequisite_any_of")
        if not isinstance(groups, list) or len(groups) > MAX_PREREQUISITES:
            raise RouteCompileError(f"{stable_id}.prerequisite_any_of is invalid")
        for group in groups:
            if not isinstance(group, list) or not group or len(group) > MAX_PREREQUISITES or len(group) != len(set(group)):
                raise RouteCompileError(f"{stable_id} has an invalid alternative prerequisite group")
            group.sort()
        groups.sort()
        content_refs = step.get("content_refs")
        if not isinstance(content_refs, list):
            raise RouteCompileError(f"{stable_id}.content_refs must be an array")
        content_keys = [(reference.get("kind"), reference.get("id")) for reference in content_refs]
        if len(content_keys) != len(set(content_keys)):
            raise RouteCompileError(f"{stable_id}.content_refs contains duplicates")
        content_refs.sort(key=lambda reference: (reference["kind"], reference["id"]))
        exclusive_group = step.get("exclusive_group")
        if exclusive_group is not None:
            exclusive_groups.setdefault(exclusive_group, []).append(stable_id)

    for group_id, members in exclusive_groups.items():
        if len(members) < 2:
            raise RouteCompileError(f"exclusive group has fewer than two members: {group_id}")

    indegree = {stable_id: 0 for stable_id in by_id}
    dependents: dict[str, list[str]] = {stable_id: [] for stable_id in by_id}
    for stable_id, step in by_id.items():
        dependencies = list(step["prerequisites"])
        for alternatives in step["prerequisite_any_of"]:
            dependencies.extend(alternatives)
        for dependency in dependencies:
            if dependency not in by_id:
                raise RouteCompileError(f"missing prerequisite {dependency} for {stable_id}")
            if dependency == stable_id:
                raise RouteCompileError(f"route step cannot require itself: {stable_id}")
            indegree[stable_id] += 1
            dependents[dependency].append(stable_id)

    ready = [stable_id for stable_id, degree in indegree.items() if degree == 0]
    heapq.heapify(ready)
    visited = 0
    while ready:
        current = heapq.heappop(ready)
        visited += 1
        for dependent in sorted(dependents[current]):
            indegree[dependent] -= 1
            if indegree[dependent] == 0:
                heapq.heappush(ready, dependent)
    if visited != len(by_id):
        raise RouteCompileError("route prerequisite graph contains a cycle")

    steps.sort(key=lambda step: step["stable_id"])
    provenance = catalogue.get("provenance")
    if isinstance(provenance, list):
        provenance.sort(key=lambda item: (item.get("source", ""), item.get("kind", ""), item.get("license_status", "")))

    catalogue["catalogue_digest"] = ZERO_DIGEST
    digest = hashlib.sha256(_canonical_bytes(catalogue)).hexdigest()
    catalogue["catalogue_digest"] = digest
    return catalogue, digest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()

    source = load_json(arguments.input)
    declared_digest = source.get("catalogue_digest")
    catalogue, digest = compile_catalogue(source)
    if arguments.verify and declared_digest != digest:
        raise RouteCompileError(f"catalogue digest mismatch: declared {declared_digest}, computed {digest}")
    if arguments.output is not None:
        arguments.output.write_bytes(_canonical_bytes(catalogue))
    print(digest)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RouteCompileError as error:
        raise SystemExit(f"route catalogue compilation failed: {error}") from error
