#!/usr/bin/env python3

import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("compile_route_catalog", ROOT / "tools" / "compile_route_catalog.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RouteCatalogCompilerTests(unittest.TestCase):
    def setUp(self):
        self.route = json.loads((ROOT / "fixtures" / "northshire-contract.route.json").read_text(encoding="utf-8"))

    def test_compilation_is_input_order_independent(self):
        first, first_digest = MODULE.compile_catalogue(self.route)
        reordered = copy.deepcopy(self.route)
        reordered["steps"].reverse()
        second, second_digest = MODULE.compile_catalogue(reordered)
        self.assertEqual(first, second)
        self.assertEqual(first_digest, second_digest)

    def test_missing_prerequisite_fails_closed(self):
        route = copy.deepcopy(self.route)
        route["steps"][1]["prerequisites"] = ["quest:missing"]
        with self.assertRaisesRegex(MODULE.RouteCompileError, "missing prerequisite"):
            MODULE.compile_catalogue(route)

    def test_cycle_fails_closed(self):
        route = copy.deepcopy(self.route)
        route["steps"][0]["prerequisites"] = [route["steps"][-1]["stable_id"]]
        with self.assertRaisesRegex(MODULE.RouteCompileError, "contains a cycle"):
            MODULE.compile_catalogue(route)

    def test_singleton_exclusive_group_fails_closed(self):
        route = copy.deepcopy(self.route)
        route["steps"][0]["exclusive_group"] = "exclusive:singleton"
        with self.assertRaisesRegex(MODULE.RouteCompileError, "fewer than two"):
            MODULE.compile_catalogue(route)


if __name__ == "__main__":
    unittest.main()
