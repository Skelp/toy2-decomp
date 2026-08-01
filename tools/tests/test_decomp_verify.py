import contextlib
import importlib.util
import io
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
    def write_report(self, directory, name, rows):
        path = Path(directory) / name
        path.write_text(json.dumps({"data": rows}), encoding="utf-8")
        return path

    def validate(self, baseline, current, targets, allow=False, source_root=None):
        old_artifacts = VERIFY.TOOL_ARTIFACTS
        VERIFY.TOOL_ARTIFACTS = baseline.parent / "none.tsv"
        try:
            return VERIFY.validate(
                baseline,
                current,
                set(targets),
                allow,
                source_root=source_root or baseline.parent / "src",
                check_annotation_tags=False,
            )
        finally:
            VERIFY.TOOL_ARTIFACTS = old_artifacts

    def test_rejects_an_untouched_score_regression(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8},
                {"address": "0x402000", "matching": 0.8},
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.9},
                {"address": "0x402000", "matching": 0.7},
            ])
            self.assertEqual(self.validate(baseline, current, {0x401000}), 1)

    def test_target_regression_requires_the_explicit_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.7}
            ])
            self.assertEqual(self.validate(baseline, current, {0x401000}), 1)
            self.assertEqual(self.validate(baseline, current, {0x401000}, True), 0)

    def test_new_target_below_75_is_advisory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.0, "stub": True}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.4}
            ])
            self.assertEqual(self.validate(baseline, current, {0x401000}), 0)

    def test_target_source_debt_still_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src"
            source.mkdir()
            (source / "test.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n"
                "void Test(char* value) { *(int*)((char*)value + 4) = 1; }\n",
                encoding="utf-8",
            )
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.2}
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 0.4}
            ])
            self.assertEqual(
                self.validate(baseline, current, {0x401000}, source_root=source), 1
            )

    def test_baseline_metadata_rejects_a_changed_report(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self.write_report(Path(directory), "before.json", [
                {"address": "0x401000", "matching": 0.8}
            ])
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
            report = self.write_report(Path(directory), "report.json", [
                {"address": "0x401000", "matching": 0.7}
            ])
            self.assertTrue(any(
                "requires provisional" in item
                for item in VERIFY.check_annotations(report, root)
            ))

    def test_session_summary_reports_new_targets_and_distribution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = self.write_report(root, "before.json", [
                {"address": "0x401000", "matching": 0.4, "stub": True},
                {"address": "0x402000", "matching": 0.8},
            ])
            current = self.write_report(root, "after.json", [
                {"address": "0x401000", "matching": 1.0},
                {"address": "0x402000", "matching": 0.9},
            ])
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = VERIFY.session_summary(
                    baseline, current, [0x401000, 0x402000]
                )
            self.assertEqual(result, 0)
            self.assertIn("2 (1 new, 1 exact)", output.getvalue())


if __name__ == "__main__":
    unittest.main()
