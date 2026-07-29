import importlib.util
import csv
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

    def test_unmatched_function_requires_provisional_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "src"
            root.mkdir()
            (root / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [MATCHED]\nvoid Test() {}\n",
                encoding="utf-8",
            )
            report = Path(directory) / "report.json"
            report.write_text('{"data": []}', encoding="utf-8")
            problems = VERIFY.check_annotations(report, root)
            self.assertTrue(any("requires provisional" in item for item in problems))

    def test_ledger_refresh_preserves_a_manual_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (root / ".notes").mkdir()
            (root / ".notes" / "caps-registry.tsv").write_text("", encoding="utf-8")
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\nvoid Test() {}\n",
                encoding="utf-8",
            )
            report = self.write_report(directory, "report.json", 0.4)
            ledger = root / "audit.tsv"
            ledger.write_text(
                "0x00401000\tprovisional\tpartial\t30.00\tclean\tmanual-audit\t"
                "the ABI is known but the branch model is not\t"
                "revisit when caller evidence identifies the branch role\t40.00\t-\t-\t-\t"
                "audited\tsub-50\n",
                encoding="utf-8",
            )
            old_root = VERIFY.ROOT
            VERIFY.ROOT = root
            try:
                VERIFY.write_audit_ledger(report, source, ledger)
            finally:
                VERIFY.ROOT = old_root
            with ledger.open(encoding="utf-8", newline="") as handle:
                row = next(row for row in csv.reader(handle, delimiter="\t") if row and not row[0].startswith("#"))
            self.assertEqual(row[3], "40.00")
            self.assertEqual(row[5], "manual-audit")
            self.assertEqual(row[6], "the ABI is known but the branch model is not")
            self.assertEqual(row[12], "audited")
            self.assertEqual(row[13], "sub-50")

    def test_audit_status_rejects_placeholder_classification(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (root / ".notes").mkdir()
            (root / ".notes" / "caps-registry.tsv").write_text("", encoding="utf-8")
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\nvoid Test() {}\n",
                encoding="utf-8",
            )
            report = self.write_report(directory, "report.json", 0.4)
            ledger = root / "audit.tsv"
            ledger.write_text(
                "0x00401000\tprovisional\tpartial\t40.00\tclean\tinitial-audit\t"
                "binary or source model is not verified\t"
                "recheck ABI, layout, control flow, and natural source forms\t-\t-\t-\t-\t"
                "pending\tsub-50\n",
                encoding="utf-8",
            )
            old_root = VERIFY.ROOT
            VERIFY.ROOT = root
            try:
                result = VERIFY.audit_status(report, source, ledger)
            finally:
                VERIFY.ROOT = old_root
            self.assertEqual(result["required"], 1)
            self.assertEqual(result["pending"], 1)

    def test_audit_status_rejects_an_unmeasured_compiler_excuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (root / ".notes").mkdir()
            (root / ".notes" / "caps-registry.tsv").write_text("", encoding="utf-8")
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\nvoid Test() {}\n",
                encoding="utf-8",
            )
            report = self.write_report(directory, "report.json", 0.4)
            ledger = root / "audit.tsv"
            ledger.write_text(
                "0x00401000\tprovisional\tpartial\t40.00\tclean\tmanual-audit\t"
                "the remaining mismatch is probably a compiler quirk\t"
                "try again later\t-\t-\t-\t-\taudited\tsub-50\n",
                encoding="utf-8",
            )
            old_root = VERIFY.ROOT
            VERIFY.ROOT = root
            try:
                result = VERIFY.audit_status(report, source, ledger)
            finally:
                VERIFY.ROOT = old_root
            self.assertEqual(result["required"], 1)
            self.assertEqual(result["pending"], 1)


if __name__ == "__main__":
    unittest.main()
