import sys
import types
import unittest
import contextlib
import io
import json
import tempfile
from pathlib import Path
from unittest import mock

sys.modules.setdefault(
    "colorama",
    types.SimpleNamespace(
        Fore=types.SimpleNamespace(RED=""),
        Style=types.SimpleNamespace(RESET_ALL=""),
        init=lambda **_: None,
    ),
)
sys.modules.setdefault("build", types.SimpleNamespace(track_process=lambda *_: None))

import decomp_utils
from tools.decomp_status import MatchStatus


class ProgressBreakdownTests(unittest.TestCase):
    def test_separates_verified_and_provisional_score_bands(self):
        source_functions = {
            "00401000": {"verification": "matched"},
            "00402000": {"verification": "effective"},
            "00403000": {"verification": "tool"},
            "00404000": {"verification": "provisional"},
            "00405000": {"verification": "provisional"},
            "00406000": {"verification": "provisional"},
            "00407000": {"verification": "provisional"},
        }
        statuses = {
            0x404000: MatchStatus(0.8),
            0x405000: MatchStatus(0.6),
            0x406000: MatchStatus(0.4),
        }

        counts = decomp_utils.progress_breakdown(
            source_functions, set(source_functions), statuses
        )

        self.assertEqual(counts["matched"], 1)
        self.assertEqual(counts["effective"], 1)
        self.assertEqual(counts["tool"], 1)
        self.assertEqual(counts["provisional_75_plus"], 1)
        self.assertEqual(counts["provisional_50_to_75"], 1)
        self.assertEqual(counts["provisional_below_50"], 1)
        self.assertEqual(counts["provisional_unscored"], 1)

    def test_json_progress_is_machine_readable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "tools" / "Resources").mkdir(parents=True)
            (root / "tools" / "Resources" / "functions_map.txt").write_text(
                "0x00401000 N::Done\n0x00402000 N::Stub\n0x00403000 N::New\n",
                encoding="utf-8",
            )
            source = {
                "00401000": {"status": "IMPLEMENTED", "file": "a.cpp", "verification": "matched"},
                "00402000": {"status": "UNFINISHED", "file": "a.cpp", "verification": "provisional"},
            }
            output = io.StringIO()
            old_cwd = Path.cwd()
            try:
                import os
                os.chdir(root)
                with mock.patch.object(decomp_utils, "parse_source_files", return_value=(source, {}, {})), \
                     mock.patch.object(decomp_utils, "read_match_statuses", return_value={}):
                    with contextlib.redirect_stdout(output):
                        decomp_utils.count_progress(json_output=True)
            finally:
                os.chdir(old_cwd)
            payload = json.loads(output.getvalue())
            self.assertEqual(payload["implemented"], 1)
            self.assertEqual(payload["unfinished"], 1)
            self.assertEqual(payload["not_started"], 1)

    def test_terminal_and_effective_byte_metrics_exclude_tool_and_debt(self):
        statuses = {
            0x401000: MatchStatus(1.0),
            0x402000: MatchStatus(0.8, effective=True),
            0x403000: MatchStatus(
                0.9,
                diff=[[0, [{
                    "orig": [[0, "mov eax, (OFFSET) g_first"]],
                    "recomp": [[0, "mov eax, (DATA) g_second"]],
                }]]],
            ),
            0x404000: MatchStatus(1.0),
        }
        metrics = decomp_utils.convergence_metrics(
            {"00401000", "00402000", "00403000", "00404000", "00405000"},
            {"00401000", "00402000", "00403000", "00404000"},
            statuses,
            {
                0x401000: 10,
                0x402000: 20,
                0x403000: 30,
                0x404000: 40,
                0x405000: 50,
            },
            {0x404000: ["raw-layout-access"]},
            {0x403000: "symbol display"},
        )
        self.assertEqual(metrics["terminal"], 2)
        self.assertEqual(metrics["terminal_bytes"], 30)
        self.assertEqual(metrics["effective_bytes"], 97)
        self.assertEqual(metrics["coverage_gap_bytes"], 50)
        self.assertAlmostEqual(metrics["refinement_gap_bytes"], 3)
        self.assertEqual(metrics["source_debt_functions"], 1)


if __name__ == "__main__":
    unittest.main()
