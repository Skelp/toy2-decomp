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

    def test_the_best_patch_recreates_a_new_file_and_its_cmake_entry(self):
        self.make_repo()
        git = ["git", "-c", "user.name=t", "-c", "user.email=t@t"]
        (self.root / "CMakeLists.txt").write_text("src/Target.cpp\n", encoding="utf-8")
        subprocess.run([*git, "add", "CMakeLists.txt"], cwd=self.root, check=True)
        subprocess.run([*git, "commit", "-q", "-m", "cmake"], cwd=self.root, check=True)
        new_file = self.root / "src" / "Toy2" / "Ini.cpp"
        new_file.parent.mkdir()
        new_file.write_text("int ini;\n", encoding="utf-8")
        (self.root / "CMakeLists.txt").write_text("src/Target.cpp\nsrc/Toy2/Ini.cpp\n", encoding="utf-8")
        self.log(similar(40))
        patch = self.best_dir / f"{ADDRESS}.patch"
        text = patch.read_text(encoding="utf-8")
        self.assertIn("+++ b/src/Toy2/Ini.cpp", text)
        self.assertIn("+src/Toy2/Ini.cpp", text)
        # The new file is part of the tree hash: another body is a new attempt.
        new_file.write_text("int ini = 1;\n", encoding="utf-8")
        self.assertTrue(self.log(similar(39))[0].startswith("attempt 2/"))
        for command in (["git", "checkout", "-q", "--", "src", "CMakeLists.txt"],
                        ["git", "clean", "-fdq", "src"], ["git", "apply", str(patch)]):
            subprocess.run(command, cwd=self.root, check=True)
        self.assertEqual(new_file.read_text(encoding="utf-8"), "int ini;\n")
        self.assertIn("src/Toy2/Ini.cpp", (self.root / "CMakeLists.txt").read_text(encoding="utf-8"))
        self.assertEqual(self.log(similar(40)), ["source matches attempt 1 (score 40.00%); not logged"])

    def test_an_untracked_non_source_file_stays_out_of_the_patch(self):
        self.make_repo()
        (self.root / "src" / "capture.bin").write_bytes(b"\x00\x01MZ binary")
        self.edit_source("int a;\nint b;\n")
        self.log(similar(40))
        patch = self.best_dir / f"{ADDRESS}.patch"
        # A binary stanza has no full index line, so git apply would refuse the patch.
        self.assertNotIn("capture.bin", patch.read_text(encoding="utf-8"))
        subprocess.run(["git", "checkout", "-q", "--", "src"], cwd=self.root, check=True)
        subprocess.run(["git", "apply", "--check", str(patch)], cwd=self.root, check=True)

    def test_git_failure_skips_the_patch_silently(self):
        lines = self.log(similar(40))
        self.assertEqual(len(lines), 1)
        self.assertFalse((self.best_dir / f"{ADDRESS}.patch").exists())
        # the diff of the best attempt is still kept for the writer
        self.assertEqual(
            (self.best_dir / f"{ADDRESS}.txt").read_text(encoding="utf-8"), similar(40)
        )

    def test_a_restored_earlier_tree_is_not_logged_again(self):
        self.make_repo()
        self.edit_source("int a;\nint b;\n")
        self.log(similar(40))
        self.edit_source("int a;\nint b;\nint c;\n")
        self.log(similar(38))
        self.edit_source("int a;\nint b;\n")
        lines = self.log(similar(40.5))
        self.assertEqual(lines, ["source matches attempt 1 (score 40.00%); not logged"])
        path = self.attempts_dir / f"{ADDRESS}.jsonl"
        self.assertEqual(len(path.read_text().splitlines()), 2)

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
                "attempts": 0, "cleanup_attempts": 0, "best_attempt": None, "best_raw": None, "last_raw": None,
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
                "attempts": 3, "cleanup_attempts": 0, "best_attempt": 2, "best_raw": 58.0, "last_raw": 57.0,
                "stalled": False, "limit": 24,
            },
        )
        code, text = self.run_main("stats", "--address", ADDRESS)
        self.assertEqual(
            text.strip(),
            "attempts=3 cleanup_attempts=0 best_attempt=2 best_raw=58.0 last_raw=57.0 stalled=False"
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
            "reasoning": 0, "turns": 9, "api_ms": 1200, "denials": 1, "error": 0,
            "message": "", "result": "a\nb",
        })
        self.assertIsNone(attempts.writer_usage("not json")["cost"])
        self.assertEqual(attempts.usage_row(attempts.writer_usage(text)),
                         "0.2500\t5\t7\t11\t13\t0\t9\t1200\t1\t0")

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

    def test_writer_failure_reports_the_exact_message_and_a_fix(self):
        fields = attempts.writer_usage("", "text")
        self.assertEqual(attempts.writer_failure(fields, 1, "warn\nError: Not logged in\n"),
                         "the writer exited with status 1: Error: Not logged in")
        failure = attempts.writer_failure(fields, 1, "Error: Not logged in")
        self.assertEqual(attempts.failure_fix(failure, "codex", {}),
                         ("log in: codex login", "codex login"))
        quota = "the writer reported an error: 429: You've hit your usage limit"
        self.assertIn("--harness claude", attempts.failure_fix(quota, "codex", {})[0])
        network = "the writer reported an error: stream disconnected before completion"
        self.assertIn("network_access", attempts.failure_fix(
            network, "claude", {"CODEX_SANDBOX_NETWORK_DISABLED": "1"})[0])
        self.assertEqual(attempts.failure_fix("the writer timed out", "claude", {})[1],
                         "tools/decomp campaigns run --check --live --harness claude")

    def test_codex_usage_sums_turns_and_reads_the_final_message_file(self):
        events = [
            {"type": "thread.started", "thread_id": "t"}, {"type": "turn.started"},
            {"type": "item.completed", "item": {"type": "agent_message", "text": "I will run it."}},
            {"type": "item.completed", "item": {"type": "command_execution", "exit_code": 0,
                                                "aggregated_output": "579f56e Refine\n"}},
            {"type": "item.completed", "item": {"type": "command_execution", "exit_code": 1,
                                                "aggregated_output": "touch: Read-only file system"}},
            {"type": "item.completed", "item": {"type": "agent_message", "text": "done"}},
            {"type": "turn.completed", "usage": {
                "input_tokens": 34985, "cached_input_tokens": 27648,
                "cache_write_input_tokens": 0, "output_tokens": 98, "reasoning_output_tokens": 7}},
        ]
        text = "\n".join(json.dumps(event) for event in events) + "\nnot json\n"
        fields = attempts.writer_usage(text, "codex-jsonl", "# 0x00401000 A - batch 1\n")
        self.assertEqual(fields, {
            "cost": None, "input": 7337, "cache_write": 0, "cache_read": 27648, "output": 98,
            "reasoning": 7, "turns": 3, "api_ms": None, "denials": 1, "error": 0,
            "message": "", "result": "# 0x00401000 A - batch 1"})
        self.assertEqual(attempts.usage_row(fields), "\t7337\t0\t27648\t98\t7\t3\t\t1\t0")
        self.assertEqual(attempts.writer_usage(text, "codex-jsonl")["result"], "done")

    def test_codex_usage_names_a_failed_turn_with_the_provider_message(self):
        body = json.dumps({"type": "error", "status": 400, "error": {
            "type": "invalid_request_error", "message": "The 'x' model is not supported."}})
        events = [
            {"type": "item.completed", "item": {"type": "error", "message": "metadata warning"}},
            {"type": "turn.started"}, {"type": "error", "message": body},
            {"type": "turn.failed", "error": {"message": body}},
        ]
        fields = attempts.writer_usage("\n".join(map(json.dumps, events)), "codex-jsonl")
        self.assertEqual((fields["error"], fields["turns"], fields["message"]),
                         (1, 0, "400: The 'x' model is not supported."))
        self.assertEqual(attempts.writer_failure(fields, 1),
                         "the writer exited with status 1: 400: The 'x' model is not supported.")
        self.assertEqual(attempts.handoff_text(fields), "")

    def test_claude_usage_reads_the_result_event_of_a_stream_transcript(self):
        stream = [{"type": "system", "subtype": "init"},
                  {"type": "assistant", "message": {"content": [{"type": "text", "text": "x"}]}},
                  {"type": "result", "num_turns": 3, "total_cost_usd": 0.5, "result": "# T",
                   "usage": {"input_tokens": 2, "cache_read_input_tokens": 10, "output_tokens": 7,
                             "output_tokens_details": {"thinking_tokens": 4}}}]
        fields = attempts.writer_usage("\n".join(map(json.dumps, stream)) + "\n")
        self.assertEqual((fields["turns"], fields["cost"], fields["cache_read"], fields["reasoning"],
                          fields["result"]), (3, 0.5, 10, 4, "# T"))
        self.assertEqual(attempts.writer_usage(json.dumps(stream[0]) + "\n")["turns"], 0)

    def test_live_verdict_reports_context_denials_and_the_skill(self):
        fields = attempts.writer_usage(json.dumps({
            "num_turns": 2, "total_cost_usd": 0.088, "result": "579f56e x\n# Decomp expert",
            "usage": {"input_tokens": 4, "cache_creation_input_tokens": 8023,
                      "cache_read_input_tokens": 7889, "output_tokens": 117}}))
        verdict = attempts.live_verdict(fields, 0, "", "579f56e", "claude", 6)
        self.assertEqual(verdict, ("ok", "2 turns, context about 8.0k tokens per turn, 0 denials,"
                                         " $0.09, 6 s, skill seen", "", ""))
        fields["denials"] = 1
        self.assertEqual(attempts.live_verdict(fields, 0, "", "579f56e", "claude", 6)[0], "FAIL")
        self.assertEqual(attempts.live_verdict(fields, 127, "", "", "codex", 0),
                         ("FAIL", "the writer command was not found",
                          "install codex or pass --harness claude", "command -v codex"))

    def test_detect_harness_prefers_the_session_then_the_path(self):
        detect = attempts.detect_harness
        never = lambda name: False  # noqa: E731
        self.assertEqual(detect({"CLAUDECODE": "1"}, never)[0], "claude")
        self.assertEqual(detect({"CODEX_THREAD_ID": "t"}, never),
                         ("codex", "a Codex shell: CODEX_THREAD_ID is set"))
        both = {"CLAUDECODE": "1", "CODEX_SESSION_ID": "s"}
        self.assertEqual(detect(both, never, ["bash", "codex", "claude"])[0], "codex")
        self.assertEqual(detect(both, never, ["bash"])[0], "claude")
        self.assertEqual(detect({}, lambda name: name == "codex")[0], "codex")
        self.assertEqual(detect({}, lambda name: True)[0], "claude")
        self.assertEqual(detect({}, never)[0], "")

    def test_writer_commands_come_from_one_place(self):
        self.make_repo()
        claude = attempts.writer_command("claude", self.root)
        self.assertEqual(claude, "claude -p --tools Bash --system-prompt-file "
                         ".agents/skills/decomp-expert/SKILL.md --strict-mcp-config "
                         "--setting-sources project --permission-mode dontAsk "
                         "--no-session-persistence --output-format stream-json --verbose --effort xhigh")
        codex = attempts.writer_command("codex", self.root, "medium", "gpt-x")
        for part in ("codex exec --ephemeral --ignore-user-config --disable apps --disable plugins",
                     "-s workspace-write -C", f"--add-dir {self.root}",
                     "model_reasoning_effort=medium", "-m gpt-x", "tool_output_token_limit=12000",
                     'developer_instructions=$(cat .agents/skills/decomp-expert/SKILL.md)',
                     '-o "$DECOMP_WRITER_LAST" -'):
            self.assertIn(part, codex)
        self.assertNotIn("dangerously", codex)
        # The user config is ignored, but its model is kept unless --model names one.
        home = self.root / "codex-home"
        home.mkdir()
        (home / "config.toml").write_text('model = "gpt-user"\n[projects."/x"]\ntrust_level = "t"\n')
        with mock.patch.dict(os.environ, {"CODEX_HOME": str(home)}):
            self.assertIn("-m gpt-user", attempts.writer_command("codex", self.root))
            self.assertIn("-m gpt-x", attempts.writer_command("codex", self.root, "", "gpt-x"))
        self.assertEqual(attempts.codex_extra_dirs(self.root),
                         [Path(self.root.resolve() / ".git")])
        resolve = attempts.resolve_writer
        self.assertEqual(resolve("auto", "--writer", "codex exec -", "", "", "", self.root)[:3],
                         ("codex", "--writer sets the writer command", "codex-jsonl"))
        self.assertEqual(resolve("auto", "--writer", "my-writer x", "", "", "", self.root)[2],
                         "text")
        self.assertEqual(resolve("claude", "DECOMP_HARNESS", "", "", "high", "", self.root)[1:3],
                         ("DECOMP_HARNESS=claude", "claude-json"))

    def test_events_and_state_record_every_operator_line(self):
        code, text = self.run_main("event", "batch", "batch 1: attempts 2-3", "--field",
                                   "best=85.28", "--field", "address=0x0041C640", "--field",
                                   "range=2-3", "--set", "phase=batch", "--set", "batch=1",
                                   "--state", "--reset")
        self.assertEqual((code, text), (0, "batch 1: attempts 2-3\n"))
        runs = self.root / "build" / "decomp-runs"
        record = json.loads((runs / "events.jsonl").read_text(encoding="utf-8"))
        self.assertEqual(sorted(record), ["event", "fields", "text", "ts"])
        self.assertRegex(record["ts"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")
        self.assertEqual(record["fields"], {"best": 85.28, "address": "0x0041C640", "range": "2-3"})
        state = json.loads((runs / "state.json").read_text(encoding="utf-8"))
        self.assertEqual((state["phase"], state["batch"], state["last_event"]),
                         ("batch", 1, "batch 1: attempts 2-3"))
        # Only the run that owns state.json appends; --check and --dry-run only print.
        code, text = self.run_main("event", "attempt", "  attempt 3: raw 1.00%", "--json")
        self.assertEqual(json.loads(text)["event"], "attempt")
        self.assertEqual(len((runs / "events.jsonl").read_text().splitlines()), 1)
        self.run_main("event", "attempt", "  attempt 4: raw 2.00%", "--state")
        self.assertEqual(len((runs / "events.jsonl").read_text().splitlines()), 2)

    def test_final_line_maps_each_stop_to_its_exit_code(self):
        self.assertEqual(attempts.EXIT_CODES, {"done": 0, "review": 1, "usage": 2, "check": 2,
                                               "writer": 3, "interrupted": 130})
        self.assertEqual(attempts.final_line("writer", "quota", "codex login"),
                         (3, "run: exit 3 (quota); next: codex login"))
        code, text = self.run_main("final", "--kind", "interrupted", "--reason", "interrupted",
                                   "--next", "git diff --stat -- src", "--state")
        self.assertEqual((code, text), (130, "run: exit 130 (interrupted); next: "
                                             "git diff --stat -- src\n"))
        state = json.loads((self.root / "build/decomp-runs/state.json").read_text())
        self.assertEqual((state["phase"], state["exit_code"]), ("stopped", 130))

    def test_status_names_a_live_run_and_a_stale_one(self):
        live = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)",
                                 "decomp-status-test"])
        self.addCleanup(live.wait)
        self.addCleanup(live.kill)
        dead = subprocess.Popen(["true"])
        dead.wait()
        (self.attempts_dir).mkdir(parents=True)
        (self.attempts_dir / f"{ADDRESS}.jsonl").write_text(
            "".join(json.dumps({"n": n, "raw": raw}) + "\n" for n, raw in ((1, 50.0), (2, 61.5))))
        state = {"pid": live.pid, "started": "2026-09-11T10:00:00Z", "harness": "codex",
                 "phase": "batch", "campaign": 1, "count": 2, "target": ADDRESS, "name": "A::B",
                 "mode": "refinement", "batch": 1, "baseline": 50.0, "best": 61.5,
                 "attempts": 2, "last_event": "  attempt 2: raw 61.50% (best 61.50%)",
                 "log": "build/decomp-runs/x.jsonl", "updated": "2026-09-11T10:05:00Z"}
        self.run_main("event", "state", "--state", "--reset",
                      *[item for key, value in state.items() for item in ("--set", f"{key}={value}")])
        report = attempts.run_status(self.root)
        self.assertTrue(report["alive"])
        self.assertIn(f"run: pid {live.pid} alive, codex, phase batch", report["lines"][0])
        self.assertIn("campaign 1/2: 0x00401000 A::B refinement, batch 1", report["lines"])
        self.assertIn("attempts: 1 50.00%, 2 61.50%", report["lines"])
        self.assertIn(f"stop: kill -TERM {live.pid}", report["lines"][0])
        self.assertEqual(self.run_main("run-status", "--busy")[1],
                         f"run pid {live.pid} is still going\ttools/decomp campaigns run --status\n")
        self.assertEqual(self.run_main("run-status", "--busy", "--self", str(live.pid))[1], "")
        self.run_main("event", "state", "--state", "--set", f"pid={dead.pid}")
        report = attempts.run_status(self.root)
        self.assertEqual((report["alive"], report["stale"]), (False, True))
        self.assertIn("not alive; stale state (phase batch", report["lines"][0])
        self.assertIn("; next: tools/decomp campaigns run --check", report["lines"][0])
        self.assertEqual(self.run_main("run-status", "--busy")[1], "")
        code, text = self.run_main("run-status", "--json")
        self.assertEqual((json.loads(text)["stale"], json.loads(text)["next"]),
                         (True, "tools/decomp campaigns run --check"))
        # A writer the dead run left behind comes first: it can still edit src.
        self.run_main("event", "state", "--state", "--set", f"writer_pid={live.pid}")
        self.assertEqual(attempts.run_status(self.root)["next"], f"kill -TERM {live.pid}")
        self.assertEqual(self.run_main("run-status", "--busy")[1],
                         f"writer pid {live.pid} of a dead run is still going\tkill -TERM {live.pid}\n")
        # Then the campaign it left active, with the best patch named.
        self.run_main("event", "state", "--state", "--set", "writer_pid=")
        (self.root / "build" / "decomp-campaign-state.json").write_text("{}", encoding="utf-8")
        self.best_dir.mkdir(parents=True)
        (self.best_dir / f"{ADDRESS}.patch").write_text("diff\n", encoding="utf-8")
        report = attempts.run_status(self.root)
        self.assertEqual(report["next"], 'tools/decomp campaigns abort --reason "campaigns run died"')
        self.assertIn(f"best patch: build/decomp-cache/best/{ADDRESS}.patch", report["lines"])

    def test_a_reused_pid_or_a_finished_run_blocks_nothing(self):
        live = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
        self.addCleanup(live.wait)
        self.addCleanup(live.kill)
        self.run_main("event", "state", "--state", "--reset", "--set", f"pid={live.pid}",
                      "--set", "phase=batch")
        state = json.loads((self.root / "build/decomp-runs/state.json").read_text())
        self.assertEqual(state["pid_start"], attempts.process_start(live.pid))
        self.assertTrue(attempts.run_status(self.root)["alive"])
        self.assertFalse(attempts.pid_alive(live.pid, "1"))
        self.run_main("final", "--kind", "done", "--reason", "done", "--next", "x", "--state")
        self.assertEqual(self.run_main("run-status", "--busy")[1], "")
        self.assertEqual([attempts.is_run_log(name) for name in (
            "candidates.json", "check-live.json", "0x0041C640.prompt", "0x0041C640-batch1.json")],
            [False, False, False, True])

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

    def test_only_a_cleanup_tie_replaces_the_best_and_cleanups_spend_no_budget(self):
        self.make_repo()
        best = self.best_dir / f"{ADDRESS}.patch"
        for line in ("int b;", "int c;", "int e;"):
            self.edit_source(f"int a;\n{line}\n")
            lines = self.log(similar(40))
        self.assertTrue(lines[0].endswith("best 40.00% (attempt 1)"), lines)
        self.assertIn("+int b;", best.read_text(encoding="utf-8"))
        with mock.patch.dict(os.environ, {"DECOMP_PHASE": "cleanup"}):
            self.edit_source("int a;\nint b; // named\n")
            lines = self.log(similar(40))
            self.assertEqual(lines[0], "attempt 4 (cleanup 1/2)  raw 40.00% (+0.00)  best 40.00% (attempt 4)")
            self.assertFalse([line for line in lines if line.startswith(("stall", "budget"))], lines)
            self.assertIn("+int b; // named", best.read_text(encoding="utf-8"))
            self.edit_source("int a;\nint d;\n")
            lines = self.log(similar(39.5))
        self.assertIn("budget: 2 cleanup attempts used; stop and return your lines", lines)
        self.assertIn("+int b; // named", best.read_text(encoding="utf-8"))
        _, text = self.run_main("stats", "--address", ADDRESS, "--json")
        stats = json.loads(text)
        self.assertEqual((stats["attempts"], stats["cleanup_attempts"], stats["best_attempt"]), (5, 2, 4))
        self.assertFalse(stats["stalled"])

    def test_cleanup_todo_lists_literals_and_fixable_findings(self):
        path = self.root / "Level.cpp"
        path.write_text(
            "enum { SOUND_JUMP = 0x3D };\n// FUNCTION: TOY2 0x00401000\nvoid f(Boss* boss)\n{\n"
            "\tif (boss->timer > 0x400)\n\t{\n\t\tPlay(0x3D, 0);\n\t\tboss->angle &= 0xFFF;\n\t}\n"
            "\t// Wave state.\n\tswitch (boss->state)\n\t{\n\tcase 1:\n\t\tboss->angle &= 0xFFF;\n"
            "\t\tPlay(SOUND_JUMP, 0);\n\t\tbreak;\n\t}\n\towner = (void*)1;\n}\n"
            "// FUNCTION: TOY2 0x00402000\nvoid g() { Play(0x3D, 0); }\n", encoding="utf-8")
        lines = attempts.cleanup_todo(path, ADDRESS)
        self.assertEqual(lines[:2], [
            f"Target source: {path}:2-19.",
            "Bare literals (uses, first line): 0xFFF x2 L8, 0x400 x1 L5, 0x3D x1 L7",
        ])
        # The list asks for no comment per block: a quota buys comments that guess.
        self.assertFalse([line for line in lines if "opener" in line.lower()])
        self.assertEqual(lines[2], "Lint findings:")
        self.assertTrue(any("L7 [unnamed-constant]" in line and "SOUND_JUMP" in line for line in lines))
        self.assertFalse([line for line in lines if "magic-pointer" in line])
        self.assertEqual(attempts.cleanup_todo(path, "0x00403000"), [])

    def test_rationale_keeps_tried_and_open_lines_under_the_limit(self):
        handoff = ("# 0x1 f - batch 2\nBEST: 5%\nTRIED (attempt score idea):\n 1 1% baseline\n"
                   + "".join(f" {n} 2%   idea {n}\n" for n in range(2, 20)) + "OPEN (priority):\n"
                   + "".join(f" {n}. region {n}\n" for n in range(1, 7)) + "LINES: a.cpp:1-2\n")
        lines = attempts.rationale_lines(handoff, "Named 3 constants.\n\n" + "x" * 150 + "\n")
        body = lines[: lines.index("Cleanup:")]
        self.assertEqual(len(body), 12)
        self.assertEqual(body[:2], ["Tried:", " 14 2% idea 14"])
        self.assertEqual(body[-5:], ["Open:", " 1. region 1", " 2. region 2", " 3. region 3",
                                     " 4. region 4"])
        self.assertEqual(lines[-2:], [" Named 3 constants.", " " + "x" * 97 + "..."])
        self.assertEqual(attempts.rationale_lines(""), [])
        self.assertEqual(attempts.clip("**Lint fix:** the magic-pointer finding at line 340", 30),
                         "Lint fix: the magic-pointer...")


if __name__ == "__main__":
    unittest.main()
