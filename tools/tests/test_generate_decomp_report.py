import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "generate-decomp-report.py"
SPEC = importlib.util.spec_from_file_location("generate_decomp_report", SCRIPT)
REPORT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(REPORT)


class ReportMetricTests(unittest.TestCase):
    def test_top_cards_show_change_gate_and_verified_progress(self):
        template = (SCRIPT.parent / "decomp-report-template.html").read_text(
            encoding="utf-8"
        )
        cards = template[template.index("const cards = [") : template.index(
            "];", template.index("const cards = [")
        )]
        self.assertIn('"Change gate"', cards)
        self.assertIn('"Source debt"', cards)
        self.assertIn('"Binary fidelity"', cards)
        self.assertIn('"Verified functions"', cards)
        self.assertIn('"Verified bytes"', cards)
        self.assertNotIn('"Project progress"', cards)
        self.assertNotIn('"Project coverage"', cards)
        self.assertNotIn('"Project accuracy"', cards)

    def test_treemap_renders_the_byte_weighted_hierarchy_recursively(self):
        template = (SCRIPT.parent / "decomp-report-template.html").read_text(
            encoding="utf-8"
        )
        self.assertIn("function buildTreemapHierarchy()", template)
        self.assertIn(
            "renderTreemapNode(child, child, branch, visualDepth + 1)", template
        )
        self.assertIn(
            "layout(focus.children, 0, 0, host.clientWidth, host.clientHeight)",
            template,
        )
        self.assertIn("function worstAspect(row, shortSide)", template)
        self.assertNotIn("tree-mosaic", template)
        self.assertNotIn("tree-dot", template)

    def test_function_map_provides_original_address_spans(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "functions.txt"
            path.write_text("0x00401000 First\n0x00401025 Second\n", encoding="utf-8")
            names, sizes = REPORT.read_function_map(path)
            self.assertEqual(names["0x401000"], "First")
            self.assertEqual(sizes["0x401000"], 0x25)

    def test_reads_original_function_sizes(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sizes.json"
            path.write_text('[{"address":"00401000","size":37}]', encoding="utf-8")
            self.assertEqual(REPORT.read_function_sizes(path), {"0x401000": 37})

    def test_project_and_runtime_metrics_are_separate(self):
        report = {
            "file": "toy2.exe",
            "timestamp": 0,
            "data": [
                {"address": "0x401000", "name": "Game", "matching": 0.5, "effective": True},
                {"address": "0x500000", "name": "memcpy", "matching": 1.0},
            ],
        }
        annotations = {
            "0x401000": {"kind": "function", "source": "Game.cpp", "line": 1},
            "0x401010": {"kind": "stub", "source": "Game.cpp", "line": 2},
        }
        summary = {"implemented": 2, "total": 3, "accuracy": 75.0, "progress": 50.0, "effective_score": 1.5}
        result = REPORT.enrich_report(
            report,
            annotations,
            {"0x401000": "Game", "0x401010": "Stub", "0x401020": "Missing"},
            summary,
            {
                "0x401000": [
                    {"severity": "error", "rule": "raw-layout-access"},
                    {"severity": "warning", "rule": "unknown-symbol"},
                ]
            },
            {"0x401000": 16, "0x401010": 8, "0x401020": 24},
        )
        metrics = result["metrics"]
        self.assertEqual(metrics["project_total"], 3)
        self.assertEqual(metrics["project_implemented"], 1)
        self.assertEqual(metrics["project_started"], 2)
        self.assertEqual(metrics["project_compared"], 1)
        self.assertEqual(metrics["project_accuracy"], 100.0)
        self.assertEqual(metrics["project_original_bytes"], 48)
        self.assertEqual(metrics["project_matched_bytes"], 8)
        self.assertEqual(metrics["project_effective_bytes"], 16)
        self.assertAlmostEqual(metrics["project_byte_progress"], 100 / 6)
        self.assertAlmostEqual(metrics["project_effective_byte_progress"], 100 / 3)
        self.assertTrue(metrics["change_gate_passed"])
        self.assertEqual(metrics["verified_functions"], 0)
        self.assertEqual(metrics["provisional_functions"], 1)
        self.assertEqual(metrics["source_debt_functions"], 1)
        self.assertEqual(metrics["binary_effective_functions"], 1)
        self.assertEqual(metrics["runtime_compared"], 1)
        self.assertEqual(metrics["runtime_accuracy"], 100.0)
        game = next(item for item in result["entities"] if item["address"] == "0x401000")
        self.assertEqual(game["quality_errors"], 1)
        self.assertEqual(game["quality_warnings"], 1)
        self.assertEqual(game["verification"], "provisional")
        self.assertEqual(game["original_size"], 16)
        missing = next(item for item in result["entities"] if item["address"] == "0x401020")
        self.assertEqual(missing["name"], "Missing")
        self.assertEqual(missing["category"], "project")
        self.assertEqual(missing["status"], "unmatched")
        self.assertEqual(missing["original_size"], 24)

    def test_lint_owner_addresses_are_canonicalized(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000\n"
                "void Test() { int *g_unk401000 = 0; }\n",
                encoding="utf-8",
            )
            quality, _ = REPORT.read_lint_quality(source)
            self.assertIn("0x401000", quality)


if __name__ == "__main__":
    unittest.main()
