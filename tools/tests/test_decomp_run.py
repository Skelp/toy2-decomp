"""Wrapper-level checks of `tools/decomp handoff` and `tools/decomp campaigns run`.

Each test runs a copy of the wrapper and its Python helpers in a temporary root
with an empty environment script, so no toolchain, build or model is needed.
"""

import json
import os
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class WrapperTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        (self.root / "tools").mkdir()
        for name in ("decomp", "decomp_attempts.py", "decomp_diff.py", "decomp_annotations.py"):
            shutil.copy(ROOT / "tools" / name, self.root / "tools" / name)
        (self.root / "tools" / "linux-decomp-env.sh").write_text("", encoding="utf-8")

    def run_tool(self, *args: str, stdin: str = "", env: dict | None = None
                 ) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(self.root / "tools" / "decomp"), *args], env=env,
            cwd=self.root, input=stdin, capture_output=True, text=True, check=False,
        )

    def make_repo(self) -> None:
        (self.root / "src").mkdir()
        (self.root / "src" / "a.cpp").write_text("int a;\n", encoding="utf-8")
        git = ["git", "-c", "user.name=t", "-c", "user.email=t@t"]
        for command in (["init", "-q"], ["add", "src"], ["commit", "-q", "-m", "base"]):
            subprocess.run(git + command, cwd=self.root, check=True)

    def write_candidates(self, code: str) -> None:
        (self.root / "tools" / "decomp_candidates.py").write_text(code, encoding="utf-8")

    @staticmethod
    def plain_env() -> dict:
        return {key: value for key, value in os.environ.items()
                if not key.startswith(("CLAUDE", "CODEX"))}

    def test_handoff_writes_stdin_under_the_canonical_address(self):
        result = self.run_tool("handoff", "0x1234", stdin="a\nb\n")
        self.assertEqual((result.returncode, result.stdout), (0, "handoff: written (2 lines)\n"))
        path = self.root / "build" / "decomp-cache" / "handoff" / "0x00001234.md"
        self.assertEqual(path.read_text(encoding="utf-8"), "a\nb\n")
        self.assertEqual(self.run_tool("handoff", "0x1234").returncode, 2)
        self.assertEqual(path.read_text(encoding="utf-8"), "a\nb\n")
        self.assertEqual(self.run_tool("handoff", "nope", stdin="x\n").returncode, 2)

    def test_run_prints_its_usage_and_rejects_bad_options(self):
        result = self.run_tool("campaigns", "run", "--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("campaigns run [--count N]", result.stdout)
        self.assertIn("130 interrupted", result.stdout)
        self.assertIn("Stop it with kill -TERM PID", result.stdout)
        for args in (("--count", "0"), ("--mode", "data"), ("--address", "x"), ("--bogus", "1"),
                     ("--budget", "1"), ("--budget", "x"), ("--harness", "pi"),
                     ("--writer-format", "xml")):
            with self.subTest(args=args):
                result = self.run_tool("campaigns", "run", *args)
                self.assertEqual(result.returncode, 2)
                self.assertRegex(result.stdout, r"^run: exit 2 \(.+\); next: tools/decomp "
                                                r"campaigns run --help\n$")

    def test_run_refuses_to_start_on_a_dirty_source_tree(self):
        self.make_repo()
        (self.root / "src" / "a.cpp").write_text("int b;\n", encoding="utf-8")
        result = self.run_tool("campaigns", "run", "--writer", "true")
        self.assertEqual(result.returncode, 2)
        self.assertIn("check: tree FAIL 1 uncommitted changes", result.stdout)
        self.assertNotIn("check: logs", result.stdout)
        self.assertIn("next: git status --short -- src", result.stdout.splitlines()[-1])
        state = json.loads((self.root / "build/decomp-runs/state.json").read_text())
        self.assertEqual((state["phase"], state["exit_code"]), ("stopped", 2))
        events = (self.root / "build/decomp-runs/events.jsonl").read_text().splitlines()
        self.assertEqual([json.loads(line)["event"] for line in events][-1], "final")

    def test_a_missing_writer_binary_is_a_writer_failure(self):
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("CLAUDE", "CODEX"))}
        result = self.run_tool("campaigns", "run", "--writer", "no-such-writer -p", env=env)
        self.assertEqual(result.returncode, 3)
        self.assertIn("check: binary FAIL no-such-writer is not on PATH", result.stdout)
        self.assertTrue(result.stdout.splitlines()[-1].startswith("run: exit 3 (harness failure"))
        result = self.run_tool("campaigns", "run", "--check", "--writer", "no-such-writer", env=env)
        self.assertEqual(result.returncode, 2)

    def test_check_live_runs_a_text_writer_and_sets_an_old_usage_file_aside(self):
        self.make_repo()
        runs = self.root / "build" / "decomp-runs"
        runs.mkdir(parents=True)
        (runs / "usage.tsv").write_text("time\taddress\nold\trow\n", encoding="utf-8")
        writer = 'cat > /dev/null; git log --oneline -1; echo "# Decomp expert"'
        result = self.run_tool("campaigns", "run", "--check", "--live", "--writer", writer)
        self.assertIn("check: harness ok custom (--writer sets the writer command)", result.stdout)
        self.assertIn("check: live ok 1 turns", result.stdout)
        self.assertIn("skill seen", result.stdout)
        kept = list(runs.glob("usage-*.tsv"))
        self.assertEqual([path.read_text() for path in kept], ["time\taddress\nold\trow\n"])
        rows = (runs / "usage.tsv").read_text().splitlines()
        self.assertTrue(rows[0].startswith("time\tharness\taddress"))
        self.assertTrue(rows[1].split("\t")[1:4] == ["custom", "-", "check"])

    def test_a_writer_cannot_push_because_every_push_url_is_rewritten(self):
        self.make_repo()
        writer = ('cat > /dev/null; git config --get-all url.writer-push-disabled://.pushinsteadof'
                  ' > pushguard.txt; git log --oneline -1; echo "# Decomp expert"')
        result = self.run_tool("campaigns", "run", "--check", "--live", "--writer", writer)
        self.assertIn("check: live ok", result.stdout)
        guard = (self.root / "pushguard.txt").read_text(encoding="utf-8").split()
        self.assertEqual(guard, ["https://", "ssh://", "git@"])

    def test_check_names_a_command_and_keeps_its_own_queue_files(self):
        self.make_repo()
        self.write_candidates("import sys\nprint('no report', file=sys.stderr)\nsys.exit(1)\n")
        result = self.run_tool("campaigns", "run", "--check", "--writer", "true",
                               env=self.plain_env())
        self.assertEqual(result.returncode, 2)
        self.assertIn("check: network skip tested only by --live", result.stdout)
        self.assertIn("check: candidates FAIL candidates failed (log: build/decomp-runs/"
                      "check-candidates.log)", result.stdout)
        self.assertEqual(result.stdout.splitlines()[-1], "run: exit 2 (2 failed: build: missing:"
                         " build/toy2.exe build/decomp-function-sizes.json); next: tools/decomp baseline")
        runs = self.root / "build" / "decomp-runs"
        self.assertFalse((runs / "candidates.log").exists())
        self.assertFalse((runs / "events.jsonl").exists())

    def test_dry_run_prints_the_settings_once(self):
        rows = [{"address": f"0x0040{n}000", "name": f"A::f{n}", "state": "FUNCTION", "size": 64,
                 "source": "a.cpp", "work_target": True, "dependency_ready": True}
                for n in (1, 2, 3)]
        self.write_candidates(f"print({json.dumps(json.dumps(rows))})\n")
        result = self.run_tool("campaigns", "run", "--dry-run", "--count", "3", "--writer", "true",
                               env=self.plain_env())
        self.assertEqual(result.returncode, 0, result.stdout)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:4], [
            "harness: custom (--writer sets the writer command), budget 12, batch 4",
            "dry-run 1: 0x00401000 A::f1 refinement, subsystem a",
            "dry-run 2: 0x00402000 A::f2 refinement, subsystem a",
            "dry-run 3: 0x00403000 A::f3 coverage, subsystem a"])
        self.assertTrue(lines[-1].endswith("so start it in the background); next: tools/decomp"
                                           " campaigns run --count 3 --harness custom"))

    def test_a_hangup_during_a_run_ends_it_with_exit_130_and_a_final_state(self):
        self.make_repo()
        (self.root / "build").mkdir()
        for name in ("toy2.exe", "decomp-function-sizes.json"):
            (self.root / "build" / name).write_text("x", encoding="utf-8")
        self.write_candidates("import time\ntime.sleep(2)\nprint('[]')\n")
        process = subprocess.Popen([str(self.root / "tools" / "decomp"), "campaigns", "run",
                                    "--writer", "true"], cwd=self.root, env=self.plain_env(),
                                   stdout=subprocess.PIPE, text=True)
        self.addCleanup(process.kill)
        state = self.root / "build" / "decomp-runs" / "state.json"
        for _ in range(200):
            if state.is_file() and json.loads(state.read_text() or "{}").get("phase") == "select":
                break
            time.sleep(0.05)
        process.send_signal(signal.SIGHUP)
        output, _ = process.communicate(timeout=30)
        self.assertEqual(process.returncode, 130, output)
        self.assertEqual(output.splitlines()[-1], "run: exit 130 (interrupted by SIGHUP in phase"
                         " select); next: tools/decomp campaigns run --check")
        data = json.loads(state.read_text())
        self.assertEqual((data["phase"], data["exit_code"]), ("stopped", 130))


if __name__ == "__main__":
    unittest.main()
