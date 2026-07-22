import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "generate-decomp-report.py"
SPEC = importlib.util.spec_from_file_location("generate_decomp_report", SCRIPT)
REPORT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(REPORT)


class ReportMetricTests(unittest.TestCase):
    def test_project_and_runtime_metrics_are_separate(self):
        report = {
            "file": "toy2.exe",
            "timestamp": 0,
            "data": [
                {"address": "0x401000", "name": "Game", "matching": 0.5},
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
        )
        metrics = result["metrics"]
        self.assertEqual(metrics["project_total"], 3)
        self.assertEqual(metrics["project_implemented"], 1)
        self.assertEqual(metrics["project_started"], 2)
        self.assertEqual(metrics["project_compared"], 1)
        self.assertEqual(metrics["project_accuracy"], 50.0)
        self.assertEqual(metrics["runtime_compared"], 1)
        self.assertEqual(metrics["runtime_accuracy"], 100.0)
        missing = next(item for item in result["entities"] if item["address"] == "0x401020")
        self.assertEqual(missing["name"], "Missing")
        self.assertEqual(missing["category"], "project")
        self.assertEqual(missing["status"], "unmatched")


if __name__ == "__main__":
    unittest.main()
