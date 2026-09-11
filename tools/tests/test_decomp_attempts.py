from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock


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
        self.assertEqual(rows[0]["tree"], "")  # no git repo: every attempt is logged

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

    def test_stall_hint_after_three_attempts_without_a_half_point_gain(self):
        for _ in range(3):
            lines = self.log(similar(70))
        self.assertFalse(any(line.startswith("stall") for line in lines), lines)
        lines = self.log(similar(70.4))
        self.assertIn(
            "stall: 3 attempts without a 0.5-point gain; stop this batch", lines
        )
        lines = self.log(similar(71))
        self.assertFalse(any(line.startswith("stall") for line in lines), lines)

    def test_budget_hint_when_the_attempts_are_used_up(self):
        for _ in range(11):
            lines = self.log(similar(50))
        self.assertFalse(any(line.startswith("budget") for line in lines), lines)
        lines = self.log(similar(50))
        self.assertIn("budget: 12 attempts used; finish this campaign", lines)

    def edit_source(self, text: str) -> None:
        (self.root / "src" / "Target.cpp").write_text(text, encoding="utf-8")

    def test_a_new_best_saves_the_source_patch_from_git(self):
        self.make_repo()
        self.edit_source("int a;\nint b;\n")
        lines = self.log(similar(40))
        patch = self.best_dir / f"{ADDRESS}.patch"
        self.assertIn(f"best patch: {patch}", lines)
        self.assertIn("+int b;", patch.read_text(encoding="utf-8"))
        patch.write_text("stale", encoding="utf-8")
        self.edit_source("int a;\nint b;\nint c;\n")
        self.log(similar(40))
        self.assertEqual(patch.read_text(encoding="utf-8"), "stale")
        self.edit_source("int a;\nint b;\nint d;\n")
        self.log(similar(41))
        self.assertIn("+int d;", patch.read_text(encoding="utf-8"))

    def test_git_failure_skips_the_patch_silently(self):
        lines = self.log(similar(40))
        self.assertEqual(len(lines), 1)
        self.assertFalse((self.best_dir / f"{ADDRESS}.patch").exists())
        # the diff of the best attempt is still kept for the writer
        self.assertEqual(
            (self.best_dir / f"{ADDRESS}.txt").read_text(encoding="utf-8"), similar(40)
        )

    def test_an_unchanged_source_tree_is_not_logged_again(self):
        self.make_repo()
        self.edit_source("int a;\nint b;\n")
        self.log(similar(40))
        lines = self.log(similar(40.5))
        self.assertEqual(
            lines, ["source unchanged since attempt 1 (score 40.00%); not logged"]
        )
        lines = self.log(similar(40.5), quiet=True)
        self.assertEqual(
            lines, ["source unchanged since attempt 1 (score 40.00%); not logged"]
        )
        path = self.attempts_dir / f"{ADDRESS}.jsonl"
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        self.assertEqual(len(rows), 1)
        self.assertEqual(len(rows[0]["tree"]), 64)
        self.edit_source("int a;\nint b;\nint c;\n")
        lines = self.log(similar(40.8))
        self.assertTrue(lines[0].startswith("attempt 2/12  raw 40.80% (+0.80)"), lines)
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        self.assertEqual(len(rows), 2)
        self.assertNotEqual(rows[0]["tree"], rows[1]["tree"])

    def test_a_row_without_a_tree_never_blocks_the_next_attempt(self):
        self.make_repo()
        self.attempts_dir.mkdir(parents=True)
        (self.attempts_dir / f"{ADDRESS}.jsonl").write_text(
            json.dumps({"n": 1, "raw": 30.0}) + "\n", encoding="utf-8"
        )
        lines = self.log(similar(30.5))
        self.assertTrue(lines[0].startswith("attempt 2/12  raw 30.50% (+0.50)"), lines)

    def test_a_new_best_keeps_its_verbose_and_compact_diffs(self):
        diffs = self.root / "build" / "decomp-diffs"
        diffs.mkdir(parents=True)
        verbose, compact = diffs / f"{ADDRESS}.txt", diffs / f"{ADDRESS}.compact.txt"
        verbose.write_text(similar(50) + "0x401000 : -mov eax, edi\n", encoding="utf-8")
        compact.write_text("-- region 1 --\n", encoding="utf-8")
        attempts.log_attempt(ADDRESS, verbose, self.root)
        best_verbose = self.best_dir / f"{ADDRESS}.txt"
        best_compact = self.best_dir / f"{ADDRESS}.compact.txt"
        self.assertEqual(best_verbose.read_text(encoding="utf-8"), verbose.read_text())
        self.assertEqual(best_compact.read_text(encoding="utf-8"), "-- region 1 --\n")
        verbose.write_text(similar(45) + "0x401000 : -push ebx\n", encoding="utf-8")
        compact.write_text("-- region 1 -- worse\n", encoding="utf-8")
        attempts.log_attempt(ADDRESS, verbose, self.root)
        self.assertIn("-mov eax, edi", best_verbose.read_text(encoding="utf-8"))
        self.assertEqual(best_compact.read_text(encoding="utf-8"), "-- region 1 --\n")
        compact.unlink()
        verbose.write_text(similar(55), encoding="utf-8")
        attempts.log_attempt(ADDRESS, verbose, self.root)
        self.assertEqual(best_verbose.read_text(encoding="utf-8"), similar(55))
        self.assertEqual(best_compact.read_text(encoding="utf-8"), "-- region 1 --\n")

    def test_stats_reports_the_best_attempt_as_json(self):
        code, text = self.run_main("stats", "--address", ADDRESS, "--json")
        self.assertEqual(code, 0)
        self.assertEqual(
            json.loads(text),
            {
                "attempts": 0, "best_attempt": None, "best_raw": None, "last_raw": None,
                "stalled": False, "limit": 12,
            },
        )
        for percent in (55, 58, 57):
            self.log(similar(percent))
        code, text = self.run_main("stats", "--address", "0x401000", "--json")
        self.assertEqual(code, 0)
        self.assertEqual(
            json.loads(text),
            {
                "attempts": 3, "best_attempt": 2, "best_raw": 58.0, "last_raw": 57.0,
                "stalled": False, "limit": 24,
            },
        )
        code, text = self.run_main("stats", "--address", ADDRESS)
        self.assertEqual(
            text.strip(),
            "attempts=3 best_attempt=2 best_raw=58.0 last_raw=57.0 stalled=False"
            " limit=24",
        )
        for percent in (57, 57):
            self.log(similar(percent))
        code, text = self.run_main("stats", "--address", ADDRESS, "--json")
        self.assertEqual(json.loads(text)["stalled"], True)

    def test_budget_doubles_the_exported_base_on_a_one_point_gain(self):
        small_gains = [50.0, 50.3, 50.6, 50.9, 51.2]
        self.assertEqual(attempts.budget(small_gains)[0], 12)
        self.assertEqual(attempts.budget([50.0, 51.0])[0], 24)
        with mock.patch.dict(os.environ, {"DECOMP_BUDGET": "6"}):
            self.assertEqual(attempts.budget(small_gains)[0], 6)
            self.assertEqual(attempts.budget([50.0, 51.0])[0], 12)
        with mock.patch.dict(os.environ, {"DECOMP_BUDGET": "1"}):
            self.assertEqual(attempts.budget([50.0])[0], 12)

    def test_pick_skips_map_defects_tried_and_blocked_coverage_rows(self):
        rows = [
            {"address": "0x401000", "work_target": False, "map_defect": True},
            {"address": "0x00402000", "work_target": True, "map_defect": False,
             "dependency_ready": False, "name": "A::B::Run", "source": ""},
            {"address": "0x00403000", "work_target": True, "map_defect": False,
             "dependency_ready": True, "name": "C::Go", "source": "Toy2/Barn.cpp"},
        ]
        self.assertEqual(attempts.pick_row(rows)["address"], "0x00402000")
        self.assertEqual(attempts.pick_row(rows, coverage=True)["address"], "0x00403000")
        self.assertIsNone(attempts.pick_row(rows, skip=("0x00402000", "0x00403000")))
        self.assertEqual(attempts.pick_row(rows, address="0x00401000")["address"], "0x401000")
        self.assertEqual(attempts.subsystem_of(rows[1]), "B")
        self.assertEqual(attempts.subsystem_of(rows[2]), "Barn")

    def test_pick_describes_a_forced_target_from_the_map_and_source(self):
        (self.root / "tools" / "Resources").mkdir(parents=True)
        (self.root / "tools" / "Resources" / "functions_map.txt").write_text(
            "0x00401000 Toy2::Barn::Update\n", encoding="utf-8")
        (self.root / "src" / "Toy2").mkdir(parents=True)
        (self.root / "src" / "Toy2" / "Barn.cpp").write_text(
            "// STUB: TOY2 0x00401000\nvoid f() {}\n", encoding="utf-8")
        empty = self.root / "rows.json"
        empty.write_text("[]", encoding="utf-8")
        code, text = self.run_main("pick", str(empty), "--address", "0x401000")
        self.assertEqual(code, 0)
        self.assertEqual(text.splitlines(), [
            "0x00401000", "Toy2::Barn::Update", "STUB", "0", "Toy2/Barn.cpp", "Barn"])
        code, text = self.run_main("pick", str(empty))
        self.assertEqual((code, text), (1, ""))

    def test_writer_usage_reads_the_cost_and_token_fields(self):
        text = json.dumps({
            "total_cost_usd": 0.25, "num_turns": 9, "duration_api_ms": 1200, "result": "a\nb",
            "permission_denials": [{"tool_name": "Bash"}],
            "usage": {"input_tokens": 5, "cache_creation_input_tokens": 7,
                      "cache_read_input_tokens": 11, "output_tokens": 13},
        })
        self.assertEqual(attempts.writer_usage(text), {
            "cost": 0.25, "input": 5, "cache_write": 7, "cache_read": 11, "output": 13,
            "turns": 9, "api_ms": 1200, "denials": 1, "error": 0, "result": "a\nb",
        })
        self.assertEqual(attempts.writer_usage("not json")["cost"], 0.0)

    def test_writer_failure_names_a_run_that_did_no_work(self):
        ran = attempts.writer_usage(json.dumps({"num_turns": 3, "result": "done"}))
        self.assertEqual(attempts.writer_failure(ran, 0), "")
        self.assertEqual(attempts.writer_failure(ran, 124), "the writer timed out")
        self.assertEqual(attempts.writer_failure(ran, 127), "the writer command was not found")
        self.assertEqual(attempts.writer_failure(ran, 1), "the writer exited with status 1")
        failed = attempts.writer_usage(json.dumps(
            {"is_error": True, "num_turns": 1, "result": "Invalid API key\nrun /login"}))
        self.assertEqual(attempts.writer_failure(failed, 0),
                         "the writer reported an error: Invalid API key")
        self.assertEqual(attempts.writer_failure(attempts.writer_usage(""), 0),
                         "the writer ran no turn")

    def test_handoff_text_keeps_the_final_message_from_its_title(self):
        result = "Done.\n```\n# 0x00401000 A::B - batch 1 (refinement)\nBEST: 60%\n```"
        fields = attempts.writer_usage(json.dumps({"num_turns": 4, "result": result}))
        self.assertEqual(attempts.handoff_text(fields),
                         "# 0x00401000 A::B - batch 1 (refinement)\nBEST: 60%")
        fields["error"] = 1
        self.assertEqual(attempts.handoff_text(fields), "")

    def test_tried_models_and_failure_lines_parse_the_handoff_and_log(self):
        handoff = ("BEST: 60%\nTRIED (attempt score idea):\n 1 55.00% baseline\n"
                   " 2 58.10% signed  index loop\nOPEN:\n 1. regions 3\n")
        self.assertEqual(attempts.tried_models(handoff), ["2 58.10% signed index loop"])
        log = ("[1/2] build\nvalidation failed:\n- 0x00401000: source debt\n\n"
               "src/a.cpp:3: warning: [raw-offset] x\nlint: failed: 1 new warning(s)\n")
        self.assertEqual(attempts.failure_lines(log), [
            "- 0x00401000: source debt", "src/a.cpp:3: warning: [raw-offset] x",
            "lint: failed: 1 new warning(s)",
        ])

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
                "stall: 3 attempts without a 0.5-point gain; stop this batch",
                "budget: 12 attempts used; finish this campaign",
            ],
        )


if __name__ == "__main__":
    unittest.main()
