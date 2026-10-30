#!/usr/bin/env python3

from __future__ import annotations

import json
from pathlib import Path
import sys

from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIRECTORY = ROOT / "schemas"
FIXTURES = {
    ROOT / "fixtures" / "activity-reservations.sample.json": "activity-reservation-set-v1.schema.json",
    ROOT / "fixtures" / "northshire-basic.replay.json": "replay-envelope-v1.schema.json",
    ROOT / "fixtures" / "minimal-content-graph.input.json": "content-graph-v1.schema.json",
    ROOT / "fixtures" / "northshire-contract.route.json": "route-catalog-v1.schema.json",
    ROOT / "fixtures" / "northshire-earned-gear.plan.json": "gear-plan-v1.schema.json",
    ROOT / "fixtures" / "northshire-item-source.json": "item-source-v1.schema.json",
}


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    schemas = {path.name: load_json(path) for path in sorted(SCHEMA_DIRECTORY.glob("*.schema.json"))}
    registry = Registry().with_resources(
        (schema["$id"], Resource.from_contents(schema)) for schema in schemas.values()
    )

    for name, schema in schemas.items():
        Draft202012Validator.check_schema(schema)
        Draft202012Validator(schema, registry=registry, format_checker=FormatChecker())
        print(f"schema-valid {name}")

    for fixture_path, schema_name in FIXTURES.items():
        validator = Draft202012Validator(
            schemas[schema_name], registry=registry, format_checker=FormatChecker()
        )
        errors = sorted(validator.iter_errors(load_json(fixture_path)), key=lambda error: list(error.path))
        if errors:
            for error in errors:
                location = ".".join(str(part) for part in error.absolute_path) or "<root>"
                print(f"{fixture_path.name}:{location}: {error.message}", file=sys.stderr)
            return 1
        print(f"fixture-valid {fixture_path.name} against {schema_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
