#!/usr/bin/env python3

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("compile_content_graph", ROOT / "tools" / "compile_content_graph.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ContentGraphCompilerTests(unittest.TestCase):
    def setUp(self):
        self.graph = json.loads((ROOT / "fixtures" / "minimal-content-graph.input.json").read_text(encoding="utf-8"))

    def test_compilation_is_deterministic(self):
        first, first_digest = MODULE.compile_graph(copy.deepcopy(self.graph))
        reordered = copy.deepcopy(self.graph)
        reordered["nodes"] = dict(reversed(list(reordered["nodes"].items())))
        second, second_digest = MODULE.compile_graph(reordered)
        first_bytes = json.dumps(first, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        second_bytes = json.dumps(second, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        self.assertEqual(first_bytes, second_bytes)
        self.assertEqual(first_digest, second_digest)

    def test_commutative_conditions_and_collections_are_canonical(self):
        graph = copy.deepcopy(self.graph)
        second_item = copy.deepcopy(graph["nodes"]["items"][0])
        second_item["id"] = 26
        graph["nodes"]["items"].append(second_item)
        second_edge = copy.deepcopy(graph["edges"][0])
        second_edge["edge_id"] = "edge:quest783-reward-item26"
        second_edge["to"]["id"] = 26
        second_edge["provenance"]["natural_key"] = [783, 26]
        graph["edges"].append(second_edge)
        graph["condition_roots"] = [{
            "condition_id": "condition:commutative",
            "expression": {
                "op": "all",
                "arguments": [
                    {"op": "leaf", "condition_type": 2, "values": [3, 4, 5]},
                    {"op": "leaf", "condition_type": 1, "values": [6, 7, 8]},
                ],
            },
        }]

        first, first_digest = MODULE.compile_graph(copy.deepcopy(graph))
        permuted = copy.deepcopy(graph)
        permuted["nodes"]["items"].reverse()
        permuted["edges"].reverse()
        permuted["condition_roots"][0]["expression"]["arguments"].reverse()
        second, second_digest = MODULE.compile_graph(permuted)
        self.assertEqual(
            json.dumps(first, ensure_ascii=False, sort_keys=True, separators=(",", ":")),
            json.dumps(second, ensure_ascii=False, sort_keys=True, separators=(",", ":")),
        )
        self.assertEqual(first_digest, second_digest)

    def test_condition_extra_fields_fail_closed(self):
        graph = copy.deepcopy(self.graph)
        graph["condition_roots"] = [{
            "condition_id": "condition:invalid-extra",
            "expression": {"op": "true", "silently_ignored": True},
        }]
        with self.assertRaisesRegex(MODULE.CompileError, "unexpected condition fields"):
            MODULE.compile_graph(graph)

    def test_missing_endpoint_fails_closed(self):
        graph = copy.deepcopy(self.graph)
        graph["edges"][0]["to"]["id"] = 999999
        with self.assertRaisesRegex(MODULE.CompileError, "missing to endpoint"):
            MODULE.compile_graph(graph)

    def test_condition_reference_cycle_fails_closed(self):
        graph = copy.deepcopy(self.graph)
        graph["condition_roots"] = [
            {"condition_id": "condition:a", "expression": {"op": "reference", "condition_id": "condition:b"}},
            {"condition_id": "condition:b", "expression": {"op": "reference", "condition_id": "condition:a"}},
        ]
        with self.assertRaisesRegex(MODULE.CompileError, "contains a cycle"):
            MODULE.compile_graph(graph)

    def test_availability_range_fails_closed(self):
        graph = copy.deepcopy(self.graph)
        graph["nodes"]["quests"][0]["availability"]["minimum_frontier"] = 20
        graph["nodes"]["quests"][0]["availability"]["maximum_frontier"] = 19
        with self.assertRaisesRegex(MODULE.CompileError, "impossible progression frontier"):
            MODULE.compile_graph(graph)

    def test_availability_ids_require_uint32_before_graph_output(self):
        invalid_values = (True, "7", [7], {"id": 7}, -1, 4_294_967_296)
        for field in ("event_ids", "pool_ids"):
            for value in invalid_values:
                with self.subTest(field=field, value=value), tempfile.TemporaryDirectory() as temporary:
                    graph = copy.deepcopy(self.graph)
                    graph["nodes"]["quests"][0]["availability"][field] = [value]
                    input_path = Path(temporary) / "input.json"
                    output_path = Path(temporary) / "graph.json"
                    digest_path = Path(temporary) / "graph.sha256"
                    input_path.write_text(json.dumps(graph), encoding="utf-8")
                    with patch.object(sys, "argv", ["compile_content_graph.py", str(input_path), str(output_path),
                            "--digest-output", str(digest_path)]):
                        with self.assertRaisesRegex(MODULE.CompileError, rf"availability\.{field}\[0\] is outside uint32"):
                            MODULE.main()
                    self.assertFalse(output_path.exists(), "invalid availability data must not emit graph output")
                    self.assertFalse(digest_path.exists(), "invalid availability data must not emit a digest")

    def test_duplicate_node_id_fails_closed(self):
        graph = copy.deepcopy(self.graph)
        graph["nodes"]["items"].append(copy.deepcopy(graph["nodes"]["items"][0]))
        with self.assertRaisesRegex(MODULE.CompileError, "duplicate item ID"):
            MODULE.compile_graph(graph)


if __name__ == "__main__":
    unittest.main()
