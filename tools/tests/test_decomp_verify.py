import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "decomp_verify.py"
SPEC = importlib.util.spec_from_file_location("decomp_verify", SCRIPT)
VERIFY = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VERIFY)


class VerifyRegressionTests(unittest.TestCase):
    def write_report(self, directory: str, name: str, matching: float) -> Path:
        path = Path(directory) / name
        path.write_text(
            json.dumps({"data": [{"address": "0x401000", "matching": matching}]}),
            encoding="utf-8",
        )
        return path

    def test_rejects_an_untouched_score_regression(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = self.write_report(directory, "before.json", 0.8)
            current = self.write_report(directory, "after.json", 0.7)
            old_artifacts = VERIFY.TOOL_ARTIFACTS
            VERIFY.TOOL_ARTIFACTS = Path(directory) / "none.tsv"
            try:
                self.assertEqual(
                    VERIFY.validate(
                        baseline, current, set(), False, check_annotation_tags=False
                    ),
                    1,
                )
            finally:
                VERIFY.TOOL_ARTIFACTS = old_artifacts

    def test_rejects_full_report_regression_when_target_improves(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = Path(directory) / "before.json"
            current = Path(directory) / "after.json"
            baseline.write_text(
                json.dumps(
                    {"data": [
                        {"address": "0x401000", "matching": 0.5},
                        {"address": "0x402000", "matching": 0.8},
                    ]}
                ),
                encoding="utf-8",
            )
            current.write_text(
                json.dumps(
                    {"data": [
                        {"address": "0x401000", "matching": 0.7},
                        {"address": "0x402000", "matching": 0.7},
                    ]}
                ),
                encoding="utf-8",
            )
            old_artifacts = VERIFY.TOOL_ARTIFACTS
            VERIFY.TOOL_ARTIFACTS = Path(directory) / "none.tsv"
            try:
                self.assertEqual(
                    VERIFY.validate(
                        baseline,
                        current,
                        {0x401000},
                        False,
                        check_annotation_tags=False,
                    ),
                    1,
                )
            finally:
                VERIFY.TOOL_ARTIFACTS = old_artifacts

    def test_target_regression_requires_the_explicit_override(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = self.write_report(directory, "before.json", 0.8)
            current = self.write_report(directory, "after.json", 0.7)
            old_artifacts = VERIFY.TOOL_ARTIFACTS
            VERIFY.TOOL_ARTIFACTS = Path(directory) / "none.tsv"
            ledger = Path(directory) / "audit.tsv"
            ledger.write_text(
                "0x00401000\tprovisional\tpartial\t70.00\tclean\taudit\t"
                "uncertain\trevisit\t\t80.00\t70.00\tremoved raw offset\n",
                encoding="utf-8",
            )
            try:
                self.assertEqual(
                    VERIFY.validate(
                        baseline,
                        current,
                        {0x401000},
                        False,
                        check_annotation_tags=False,
                    ),
                    1,
                )
                self.assertEqual(
                    VERIFY.validate(
                        baseline,
                        current,
                        {0x401000},
                        True,
                        check_annotation_tags=False,
                        audit_ledger=ledger,
                    ),
                    0,
                )
            finally:
                VERIFY.TOOL_ARTIFACTS = old_artifacts

    def test_baseline_metadata_rejects_a_changed_report(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self.write_report(directory, "before.json", 0.8)
            metadata = Path(directory) / "metadata.json"
            VERIFY.write_metadata(metadata, report)
            report.write_text('{"data": []}', encoding="utf-8")
            self.assertIn(
                "baseline report does not match its saved metadata",
                VERIFY.validate_metadata(metadata, report),
            )

    def test_annotation_tag_becomes_stale_after_regression(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "src"
            root.mkdir()
            (root / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [MATCHED]\nvoid Test() {}\n",
                encoding="utf-8",
            )
            report = self.write_report(directory, "report.json", 0.7)
            problems = VERIFY.check_annotations(report, root)
            self.assertTrue(any("requires provisional" in item for item in problems))


if __name__ == "__main__":
    unittest.main()
