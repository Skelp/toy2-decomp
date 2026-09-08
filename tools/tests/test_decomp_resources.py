from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import decomp_binary, decomp_resources, decomp_verify


class ResourceCampaignTests(unittest.TestCase):
    def test_direct_verifier_and_wrapper_reject_targetless_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "report.json"
            report.write_text(json.dumps({"data": []}), encoding="utf-8")
            self.assertEqual(
                decomp_verify.validate(
                    report,
                    report,
                    set(),
                    False,
                    source_root=root,
                    mode="coverage",
                    check_annotation_tags=False,
                ),
                1,
            )
        wrapper = subprocess.run(
            ["tools/decomp", "validate", "--mode", "coverage"],
            cwd=Path(__file__).resolve().parents[2],
            text=True,
            capture_output=True,
        )
        self.assertEqual(wrapper.returncode, 2)
        self.assertIn("at least one --target", wrapper.stderr)

    def test_campaign_status_supports_direct_script_invocation(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [
                    "python", "tools/decomp_campaigns.py", "--state-file",
                    str(Path(directory) / "missing.json"), "status",
                ],
                cwd=Path(__file__).resolve().parents[2],
                text=True,
                capture_output=True,
            )
        self.assertEqual(result.returncode, 0)
        self.assertIn("No active campaign", result.stdout)

    def test_exact_identity_uses_duplicate_multiplicity(self):
        entry = decomp_binary.ResourceEntry
        original = (
            entry((2, 127, 2057), b"same", 0),
            entry((2, 127, 2057), b"same", 0),
        )
        recompiled = (
            entry((2, 127, 2057), b"same", 0),
            entry((2, 127, 0), b"same", 0),
        )
        with patch.object(
            decomp_resources.decomp_binary,
            "read_resources",
            side_effect=(original, recompiled),
        ):
            rows = decomp_resources.resource_rows(Path("old"), Path("new"))

        self.assertEqual([row["match"] for row in rows], [True, True])
        self.assertEqual([row["identity_match"] for row in rows], [True, False])
        evidence = decomp_resources.selected_evidence(rows, (2, 127, 2057))
        self.assertEqual(evidence["leaf_count"], 2)
        self.assertEqual(evidence["matched_leaves"], 1)
        self.assertEqual(evidence["explained_bytes"], 4)

    def test_staging_rejects_an_unstaged_input_despite_a_staged_decoy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            resources = root / "resources"
            resources.mkdir()
            measured = resources / "measured.bmp"
            decoy = resources / "decoy.bmp"
            measured.write_bytes(b"old")
            decoy.write_bytes(b"old")
            subprocess.run(["git", "add", "resources"], cwd=root, check=True)
            baseline = decomp_resources.resource_source_snapshot(root)

            measured.write_bytes(b"new")
            decoy.write_bytes(b"new")
            subprocess.run(["git", "add", "resources/decoy.bmp"], cwd=root, check=True)
            problems = decomp_resources.staged_resource_source_problems(root, baseline)
            self.assertTrue(any("measured.bmp" in problem for problem in problems))

            subprocess.run(["git", "add", "resources/measured.bmp"], cwd=root, check=True)
            self.assertEqual(
                decomp_resources.staged_resource_source_problems(root, baseline), []
            )

    def test_selected_identity_and_scored_sections_must_improve(self):
        before_rows = [
            {"path": ["2", "127", "2057"], "size": 8, "identity_match": False},
            {"path": ["2", "128", "2057"], "size": 4, "identity_match": True},
        ]
        after_rows = [
            {"path": ["2", "127", "2057"], "size": 8, "identity_match": True},
            {"path": ["2", "128", "2057"], "size": 4, "identity_match": False},
        ]
        before_data = {
            "sections": {"sections": [
                {"name": ".data", "explained_bytes": 10, "score": 1.0},
                {"name": ".rdata", "explained_bytes": 20, "score": 1.0},
                {"name": ".other", "explained_bytes": 30, "score": 1.0},
            ]}
        }
        after_data = {
            "sections": {"sections": [
                {"name": ".data", "explained_bytes": 9, "score": 0.9},
                {"name": ".rdata", "explained_bytes": 19, "score": 0.95},
                {"name": ".other", "explained_bytes": 29, "score": 0.96},
            ]}
        }
        problems = decomp_verify.validate_resource_campaign(
            before_rows, after_rows, (2, 127, 2057), before_data, after_data
        )
        self.assertTrue(
            any("unrelated resource bytes regressed" in item for item in problems)
        )
        self.assertTrue(
            any(".data: explained section bytes regressed" in item for item in problems)
        )
        self.assertTrue(
            any(".rdata: explained section bytes regressed" in item for item in problems)
        )
        self.assertTrue(
            any(".other: explained section bytes regressed" in item for item in problems)
        )


if __name__ == "__main__":
    unittest.main()
