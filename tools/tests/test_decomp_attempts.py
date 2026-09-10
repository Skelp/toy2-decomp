from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "toy2_decomp_attempts", TOOLS / "decomp_attempts.py"
)
assert spec is not None and spec.loader is not None
attempts = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = attempts
spec.loader.exec_module(attempts)

ADDRESS = "0x00401000"


def similar(percent: float) -> str:
    return f"Target is only {percent:.2f}% similar to the original, diff above\n"


class AttemptTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.attempts_dir = self.root / "build" / "decomp-attempts"
        self.best_dir = self.root / "build" / "decomp-cache" / "best"
        self.diff = self.root / "diff.txt"

    def log(self, text: str, quiet: bool = False) -> list[str]:
        self.diff.write_text(text, encoding="utf-8")
        return attempts.log_attempt(ADDRESS, self.diff, self.root, quiet)

    def run_main(self, *argv: str) -> tuple[int, str]:
        output = StringIO()
        with redirect_stdout(output):
            code = attempts.main(["--root", str(self.root), *argv])
        return code, output.getvalue()

    def make_repo(self) -> None:
        (self.root / "src").mkdir(parents=True)
        (self.root / "src" / "Target.cpp").write_text("int a;\n", encoding="utf-8")
        for command in (
            ["git", "init", "-q"],
            ["git", "-c", "user.name=t", "-c", "user.email=t@t", "add", "src"],
            ["git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "base"],
        ):
            subprocess.run(command, cwd=self.root, check=True)

    def test_first_attempt_prints_the_default_budget_line(self):
        lines = self.log(similar(60))
        self.assertEqual(
            lines, ["attempt 1/12  raw 60.00% (+60.00)  best 60.00% (attempt 1)"]
        )
        rows = [
            json.loads(line)
            for line in (self.attempts_dir / f"{ADDRESS}.jsonl").read_text().splitlines()
        ]
        self.assertEqual(rows[0]["n"], 1)
        self.assertEqual(rows[0]["raw"], 60.0)
        self.assertFalse(rows[0]["exact"] or rows[0]["effective"])
        self.assertTrue(rows[0]["at"].endswith("+00:00"))

    def test_a_one_point_gain_extends_the_budget_for_four_attempts(self):
        self.log(similar(60))
        second = self.log(similar(61.5))
        self.assertTrue(second[0].startswith("attempt 2/24  raw 61.50% (+1.50)"), second)
        for _ in range(3):
            lines = self.log(similar(61))
        self.assertTrue(lines[0].startswith("attempt 5/24"), lines)
        sixth = self.log(similar(61))
        self.assertTrue(sixth[0].startswith("attempt 6/12"), sixth)
        self.assertIn("best 61.50% (attempt 2)", sixth[0])

    def test_stall_hint_after_five_attempts_without_a_half_point_gain(self):
        for _ in range(5):
            lines = self.log(similar(70))
        self.assertFalse(any(line.startswith("stall") for line in lines), lines)
        lines = self.log(similar(70.4))
        self.assertIn(
            "stall: 5 attempts without a 0.5-point gain; "
            "commit the best model or record no-source",
            lines,
        )
        lines = self.log(similar(71))
        self.assertFalse(any(line.startswith("stall") for line in lines), lines)

    def test_budget_hint_when_the_attempts_are_used_up(self):
        for _ in range(11):
            lines = self.log(similar(50))
        self.assertFalse(any(line.startswith("budget") for line in lines), lines)
        lines = self.log(similar(50))
        self.assertIn("budget: 12 attempts used; finish this campaign", lines)

    def test_a_new_best_saves_the_source_patch_from_git(self):
        self.make_repo()
        (self.root / "src" / "Target.cpp").write_text("int a;\nint b;\n", encoding="utf-8")
        lines = self.log(similar(40))
        patch = self.best_dir / f"{ADDRESS}.patch"
        self.assertIn(f"best patch: {patch}", lines)
        self.assertIn("+int b;", patch.read_text(encoding="utf-8"))
        patch.write_text("stale", encoding="utf-8")
        self.log(similar(40))
        self.assertEqual(patch.read_text(encoding="utf-8"), "stale")
        self.log(similar(41))
        self.assertIn("+int b;", patch.read_text(encoding="utf-8"))

    def test_git_failure_skips_the_patch_silently(self):
        lines = self.log(similar(40))
        self.assertEqual(len(lines), 1)
        self.assertFalse(self.best_dir.exists())

    def test_stats_reports_the_best_attempt_as_json(self):
        code, text = self.run_main("stats", "--address", ADDRESS, "--json")
        self.assertEqual(code, 0)
        self.assertEqual(
            json.loads(text),
            {"attempts": 0, "best_attempt": None, "best_raw": None, "last_raw": None},
        )
        for percent in (55, 58, 57):
            self.log(similar(percent))
        code, text = self.run_main("stats", "--address", "0x401000", "--json")
        self.assertEqual(code, 0)
        self.assertEqual(
            json.loads(text),
            {"attempts": 3, "best_attempt": 2, "best_raw": 58.0, "last_raw": 57.0},
        )
        code, text = self.run_main("stats", "--address", ADDRESS)
        self.assertEqual(
            text.strip(), "attempts=3 best_attempt=2 best_raw=58.0 last_raw=57.0"
        )

    def test_unparsable_or_missing_diff_warns_and_exits_zero(self):
        self.diff.write_text("reccmp: error: nothing here\n", encoding="utf-8")
        code, text = self.run_main("attempt", "--address", ADDRESS, "--diff", str(self.diff))
        self.assertEqual(code, 0)
        self.assertIn("warning: no similarity verdict", text)
        code, text = self.run_main(
            "attempt", "--address", ADDRESS, "--diff", str(self.root / "absent.txt")
        )
        self.assertEqual(code, 0)
        self.assertIn("warning: no similarity verdict", text)
        self.assertFalse(self.attempts_dir.exists())

    def test_exact_and_effective_verdicts_are_flagged(self):
        self.assertEqual(attempts.parse_diff("Target 100% match.\n"), (100.0, True, False))
        self.assertEqual(
            attempts.parse_diff(
                "Target 100% effective match (differs, but only in ways that "
                "don't affect behavior).\n"
            ),
            (100.0, False, True),
        )
        self.assertEqual(attempts.parse_diff(similar(87.65)), (87.65, False, False))

    def test_clear_removes_only_the_named_attempt_files(self):
        self.log(similar(60))
        other = self.attempts_dir / "0x00402000.jsonl"
        other.write_text("{}\n", encoding="utf-8")
        code, text = self.run_main("clear", "--address", ADDRESS, "--address", "0x00403000")
        self.assertEqual(code, 0)
        self.assertIn("Cleared the attempts of 2 target(s).", text)
        self.assertFalse((self.attempts_dir / f"{ADDRESS}.jsonl").exists())
        self.assertTrue(other.exists())

    def test_quiet_keeps_only_the_actionable_hints(self):
        for _ in range(11):
            lines = self.log(similar(30), quiet=True)
            self.assertTrue(
                all(line.startswith("stall") for line in lines), lines
            )
        code, text = self.run_main(
            "attempt", "--address", ADDRESS, "--diff", str(self.diff), "--quiet"
        )
        self.assertEqual(code, 0)
        self.assertEqual(
            text.splitlines(),
            [
                "stall: 5 attempts without a 0.5-point gain; commit the best model or "
                "record no-source",
                "budget: 12 attempts used; finish this campaign",
            ],
        )


if __name__ == "__main__":
    unittest.main()
