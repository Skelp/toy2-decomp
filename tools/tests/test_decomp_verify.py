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
                self.assertEqual(VERIFY.validate(baseline, current, set(), False), 1)
            finally:
                VERIFY.TOOL_ARTIFACTS = old_artifacts

    def test_target_regression_requires_the_explicit_override(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = self.write_report(directory, "before.json", 0.8)
            current = self.write_report(directory, "after.json", 0.7)
            old_artifacts = VERIFY.TOOL_ARTIFACTS
            VERIFY.TOOL_ARTIFACTS = Path(directory) / "none.tsv"
            try:
                self.assertEqual(VERIFY.validate(baseline, current, {0x401000}, False), 1)
                self.assertEqual(VERIFY.validate(baseline, current, {0x401000}, True), 0)
            finally:
                VERIFY.TOOL_ARTIFACTS = old_artifacts


if __name__ == "__main__":
    unittest.main()
