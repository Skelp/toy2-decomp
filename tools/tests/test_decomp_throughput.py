from __future__ import annotations

import gzip
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from datetime import datetime, timedelta, timezone
from io import StringIO
from pathlib import Path
from unittest import mock


TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("toy2_decomp_throughput", TOOLS / "decomp_throughput.py")
assert spec is not None and spec.loader is not None
throughput = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = throughput
spec.loader.exec_module(throughput)

OPENED = "2026-09-10T17:30:00Z"
SCOREBOARD_HEADER = "# reccmp_head=unknown generated_by=validate\naddress\tsize\tmatching\texact\teffective\tdebt\n"


def scoreboard(rows: list[tuple]) -> str:
    lines = [f"0x{address:08X}\t{size}\t{matching:.6f}\t{exact}\t{effective}\t{debt}"
             for address, size, matching, exact, effective, debt in rows]
    return SCOREBOARD_HEADER + "\n".join(lines) + "\n"


def git(root: Path, *args: str, date: str | None = None) -> None:
    env = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_AUTHOR_NAME="test", GIT_AUTHOR_EMAIL="test@example.com",
               GIT_COMMITTER_NAME="test", GIT_COMMITTER_EMAIL="test@example.com")
    if date:
        env["GIT_AUTHOR_DATE"] = env["GIT_COMMITTER_DATE"] = date
    subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True, env=env)


def write(root: Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def build_repo(root: Path) -> None:
    """Five first-parent commits: source, source, tooling, other, source."""
    git(root, "init", "-q", "-b", "main")
    write(root, "src/a.cpp", "// FUNCTION: TOY2 0x00401000\nvoid a() {}\n")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "root source", date="2026-09-08T08:00:00Z")
    write(root, "src/a.cpp", "// FUNCTION: TOY2 0x00401000\nvoid a() {}\n// FUNCTION: TOY2 0x00401100\nvoid b() {}\n")
    write(root, "tools/Resources/scoreboard.tsv", scoreboard(
        [(0x401000, 100, 1.0, 1, 0, 0), (0x401100, 50, 0.5, 0, 0, 0), (0x401200, 30, 0.0, 0, 0, 0)]))
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "scoreboard source", date="2026-09-08T09:00:00Z")
    write(root, "docs/notes.md", "notes\n")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "tooling", date="2026-09-08T12:00:00Z")
    write(root, "README.md", "readme\n")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "other", date="2026-09-08T12:30:00Z")
    write(root, "src/b.cpp", "// FUNCTION: TOY2 0x00401200\nvoid c() {}\n")
    write(root, "tools/Resources/scoreboard.tsv", scoreboard(
        [(0x401000, 100, 1.0, 1, 0, 0), (0x401100, 50, 1.0, 0, 1, 1), (0x401200, 30, 0.4, 0, 0, 0)]))
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "more source", date="2026-09-08T13:00:00Z")


def ledger_row(stamp: str, minutes: float, effective: float, result: str = "source",
               mode: str = "refinement", **extra) -> dict:
    return {"result": result, "mode": mode, "timestamp": stamp, "minutes": minutes,
            "effective_bytes": effective, "initialized_bytes": 0.0, **extra}


def ledger_series(count: int, effective: float, start_hour: int = 18) -> list[dict]:
    """Rows every 30 minutes from 18:00 on Sep 10, 20 minutes each: 20 + 30 * (n - 1) all-in minutes."""
    rows = []
    for index in range(count):
        minute = index * 30
        stamp = f"2026-09-10T{start_hour + minute // 60:02d}:{minute % 60:02d}:00Z"
        rows.append(ledger_row(stamp, 20.0, effective))
    return rows


def write_ledger(root: Path, rows: list[dict]) -> None:
    write(root, throughput.LEDGER, "".join(json.dumps(row) + "\n" for row in rows))


def write_baseline(root: Path, sep8: float = 40.0, opens: str = OPENED) -> None:
    write(root, throughput.BASELINE, json.dumps(
        {"sep8_daytime_bytes_per_wall_hour": sep8, "cap_window_opens_after": opens}))


def write_experiments(root: Path, baseline_rate: float = 20.0, outcome: str = "PENDING") -> None:
    write(root, throughput.EXPERIMENTS,
          "# id\topened_commit\topened_at\thypothesis\tbaseline_rate\treview_after\toutcome\tnote\n"
          f"T-00\tabc\t{OPENED}\tRestore the loop\t{baseline_rate}\t10\t{outcome}\tnote\n"
          f"T-09\tabc\t{OPENED}\tAlready kept\t{baseline_rate}\t10\tKEEP\t\n")


class ThroughputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.root = Path(cls.directory.name)
        build_repo(cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def tearDown(self):
        for relative in (throughput.LEDGER, throughput.BASELINE, throughput.EXPERIMENTS):
            path = self.root / relative
            if path.exists():
                path.unlink()

    def run_cli(self, *args: str) -> tuple[int, str]:
        buffer = StringIO()
        with redirect_stdout(buffer):
            code = throughput.main(["--repo", str(self.root), *args])
        return code, buffer.getvalue()

    def run_json(self, *args: str) -> dict:
        code, output = self.run_cli("--json", *args)
        self.assertEqual(code, 0)
        return json.loads(output)

    def test_scoreboard_parsing_counts_implemented_terminal_and_bytes(self):
        text = scoreboard([(0x401000, 100, 1.0, 1, 0, 0), (0x401100, 50, 1.0, 0, 1, 2),
                           (0x401200, 30, 0.25, 0, 0, 0), (0x401300, 40, 0.0, 0, 0, 0)])
        metrics = throughput.parse_scoreboard(text + "garbage\tline\n")
        self.assertEqual(metrics["implemented"], 3)
        self.assertEqual(metrics["terminal"], 1)
        self.assertEqual(metrics["terminal_bytes"], 100)
        self.assertAlmostEqual(metrics["effective_bytes"], 100 + 50 + 7.5)
        self.assertFalse(metrics["fallback"])

    def test_fallback_rows_count_function_markers(self):
        data = self.run_json("--commits", "5")
        self.assertIsNone(data["base"])
        self.assertTrue(data["rows"][0]["fallback"])
        self.assertEqual(data["rows"][0]["implemented"], 1)
        self.assertIsNone(data["rows"][0]["effective_bytes"])
        self.assertFalse(data["rows"][1]["fallback"])
        self.assertEqual(data["deltas"]["functions"], 2)
        self.assertIsNone(data["deltas"]["effective_bytes"])
        code, text = self.run_cli("--commits", "5")
        self.assertEqual(code, 0)
        self.assertIn("effective bytes delta: unknown (fallback)", text)
        self.assertIn("(no base: root commit)", text)

    def test_mixed_windows_compare_functions_on_the_annotation_count(self):
        # A scoreboard row without a src annotation must not inflate the
        # functions delta when the window base predates the scoreboard.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_repo(root)
            write(root, "tools/Resources/scoreboard.tsv", scoreboard(
                [(0x401000, 100, 1.0, 1, 0, 0), (0x401100, 50, 1.0, 0, 1, 1),
                 (0x401200, 30, 0.4, 0, 0, 0), (0x401300, 40, 0.5, 0, 0, 0)]))
            git(root, "add", "-A")
            git(root, "commit", "-q", "-m", "extra row", date="2026-09-08T14:00:00Z")
            buffer = StringIO()
            with redirect_stdout(buffer):
                code = throughput.main(["--repo", str(root), "--json", "--commits", "6"])
            self.assertEqual(code, 0)
            data = json.loads(buffer.getvalue())
            self.assertEqual(data["rows"][-1]["implemented"], 4)
            self.assertEqual(data["rows"][-1]["annotations"], 3)
            self.assertEqual(data["deltas"]["functions"], 2)

    def test_intervals_are_capped_at_ninety_minutes(self):
        rows = self.run_json("--commits", "5")["rows"]
        self.assertEqual([row["interval_hours"] for row in rows], [0.0, 1.0, 1.5, 0.5, 0.5])

    def test_commit_classes(self):
        rows = self.run_json("--commits", "5")["rows"]
        self.assertEqual([row["class"] for row in rows], ["source", "source", "tooling", "other", "source"])
        self.assertEqual(throughput.classify_paths(["tools/Resources/functions_map.txt"]), "source")
        self.assertEqual(throughput.classify_paths(["tools/decomp", "src/x.cpp"]), "source")
        self.assertEqual(throughput.classify_paths(["AGENTS.md", "README.md"]), "tooling")
        self.assertEqual(throughput.classify_paths([".github/workflows/ci.yml"]), "tooling")
        self.assertEqual(throughput.classify_paths(["README.md", "CMakeLists.txt"]), "other")
        self.assertEqual(throughput.classify_paths([]), "other")

    def test_hours_by_class_and_tooling_share(self):
        data = self.run_json("--commits", "4")
        self.assertEqual(data["hours"], {"source": 1.5, "tooling": 1.5, "other": 0.5, "total": 3.5})
        self.assertEqual(data["commits"], {"source": 2, "tooling": 1, "other": 1, "total": 4})
        self.assertAlmostEqual(data["tooling_share"], 1.5 / 3.5)
        code, text = self.run_cli("--commits", "4")
        self.assertIn("hours: source 1.50  tooling 1.50  other 0.50  (total 3.50)", text)
        self.assertIn("tooling share: 0.43", text)
        self.assertIn("bc attempts per source result: n/a", text)

    def test_json_shape_and_deltas_from_base(self):
        data = self.run_json("--commits", "3")
        self.assertEqual(set(data), {"first", "last", "base", "deltas", "hours", "commits", "rows",
                                     "effective_bytes_per_source_hour", "tooling_share",
                                     "bc_attempts_per_source_result", "experiments"})
        self.assertEqual(set(data["deltas"]), {"functions", "terminal", "terminal_bytes", "effective_bytes"})
        self.assertEqual(data["first"]["time"], "2026-09-08T12:00:00Z")
        self.assertEqual(data["last"]["time"], "2026-09-08T13:00:00Z")
        self.assertEqual(data["base"]["time"], "2026-09-08T09:00:00Z")
        self.assertEqual(data["deltas"], {"functions": 1, "terminal": 0, "terminal_bytes": 0,
                                          "effective_bytes": 37.0})
        self.assertAlmostEqual(data["effective_bytes_per_source_hour"], 74.0)
        self.assertIsNone(data["experiments"])
        self.assertEqual(set(data["rows"][0]), {"sha", "time", "interval_hours", "class", "implemented",
                                                "annotations", "terminal", "terminal_bytes",
                                                "effective_bytes", "fallback"})

    def test_since_and_window_days_select_commits(self):
        self.assertEqual(self.run_json("--since", "2026-09-08T12:00:00Z")["commits"]["total"], 3)
        self.assertEqual(self.run_json("--window-days", "1", "--now", "2026-09-08T14:00:00Z")["commits"]["total"], 5)
        self.assertEqual(self.run_json("--window-days", "1", "--now", "2026-09-09T12:15:00Z")["commits"]["total"], 2)
        code, text = self.run_cli("--now", "2026-10-01T00:00:00Z")
        self.assertEqual((code, text.strip()), (0, "no commits in the window"))

    def test_bc_attempts_median_over_source_rows(self):
        write_ledger(self.root, [
            ledger_row("2026-09-10T18:00:00Z", 5, 10, attempts=1), ledger_row("2026-09-10T18:30:00Z", 5, 10, attempts=3),
            ledger_row("2026-09-10T19:00:00Z", 5, 10, attempts=2), ledger_row("2026-09-10T19:30:00Z", 5, 0, attempts=0),
            ledger_row("2026-09-10T20:00:00Z", 5, 0, result="no-source", attempts=9),
        ])
        self.assertEqual(self.run_json("--commits", "2")["bc_attempts_per_source_result"], 2.0)

    def test_ledger_all_in_minutes_cap_the_idle_gap(self):
        write_ledger(self.root, [
            ledger_row("2026-09-10T10:00:00Z", 10, 100), ledger_row("2026-09-10T10:30:00Z", 10, 100),
            ledger_row("2026-09-10T16:30:00Z", 10, 100, result="no-source"),
            ledger_row("2026-09-10T17:00:00Z", 10, 100, mode="meta"),
            ledger_row("2026-09-10T17:10:00Z", 10, 100, result="meta-fix"),
            ledger_row("2026-09-10T17:20:00Z", 10, 100, record_type="delivery"),
        ])
        rows, attempts = throughput.load_ledger(self.root / throughput.LEDGER)
        self.assertEqual([row["_all_in_minutes"] for row in rows], [10.0, 30.0, 100.0])
        self.assertIsNone(attempts)
        self.assertAlmostEqual(throughput.ledger_rate(rows), 300 / (140 / 60))

    def test_experiments_keep(self):
        write_baseline(self.root)
        write_experiments(self.root)
        write_ledger(self.root, ledger_series(10, 20.0))
        code, text = self.run_cli("--commits", "2", "--experiments")
        self.assertEqual(code, 0)
        self.assertIn("T-00  KEEP (rate 41.4 vs baseline 20.0, 10 rows)  Restore the loop", text)
        self.assertNotIn("T-09", text)
        review = self.run_json("--commits", "2", "--experiments")["experiments"]
        self.assertEqual([(r["id"], r["verdict"], r["rows"]) for r in review], [("T-00", "KEEP", 10)])

    def test_experiments_revert_due_below_half_baseline_or_floor(self):
        write_baseline(self.root)
        write_experiments(self.root)
        write_ledger(self.root, ledger_series(10, 2.0))
        code, text = self.run_cli("--commits", "2", "--experiments")
        self.assertIn("T-00  REVERT DUE (rate 4.1 vs baseline 20.0, 10 rows)", text)
        write_baseline(self.root, sep8=60.0)
        write_ledger(self.root, ledger_series(10, 6.0))
        code, text = self.run_cli("--commits", "2", "--experiments")
        self.assertIn("T-00  REVERT DUE (rate 12.4 vs baseline 20.0, 10 rows)", text)

    def test_experiments_pending(self):
        write_baseline(self.root)
        write_experiments(self.root)
        write_ledger(self.root, ledger_series(3, 20.0))
        code, text = self.run_cli("--commits", "2", "--experiments")
        self.assertIn("T-00  PENDING (3 rows so far, rate 45.0)", text)
        write_ledger(self.root, ledger_series(10, 6.0))
        code, text = self.run_cli("--commits", "2", "--experiments")
        self.assertIn("T-00  PENDING (10 rows so far, rate 12.4)", text)
        write_ledger(self.root, ledger_series(10, 6.0, start_hour=10))
        code, text = self.run_cli("--commits", "2", "--experiments")
        self.assertIn("T-00  PENDING (0 rows so far, rate n/a)", text)

    def test_cap_check_window_not_started(self):
        write_baseline(self.root)
        write_ledger(self.root, ledger_series(9, 20.0) + ledger_series(5, 20.0, start_hour=10))
        code, text = self.run_cli("--cap-check", "--now", "2026-09-11T00:00:00Z")
        self.assertEqual((code, text.strip()), (0, "cap window not started (9 of 10 source rows after "
                                                   "cap_window_opens_after=2026-09-10T17:30:00+00:00)"))
        (self.root / throughput.BASELINE).unlink()
        code, text = self.run_cli("--cap-check", "--now", "2026-09-11T00:00:00Z")
        self.assertEqual(code, 0)
        self.assertIn("cap window not started", text)

    def test_cap_check_trips_on_tooling_share_hours_and_floor(self):
        write_baseline(self.root)
        write_ledger(self.root, ledger_series(10, 2.0))
        code, text = self.run_cli("--cap-check", "--now", "2026-09-08T14:00:00Z")
        self.assertEqual(code, 1)
        self.assertIn("cap check failed", text)
        self.assertIn("trailing-7-day tooling share 0.43 exceeds 0.15", text)
        self.assertIn("tooling hours today 1.50 exceed 1.0", text)
        self.assertIn("trailing-10-source-row rate 4.1 is below the floor 10.0 while 1 tooling commits", text)
        code, text = self.run_cli("--cap-check", "--now", "2026-09-11T00:00:00Z")
        self.assertEqual(code, 1)
        self.assertNotIn("tooling hours today", text)

    def test_cap_check_passes_without_tooling_commits_in_the_window(self):
        write_baseline(self.root)
        write_ledger(self.root, ledger_series(10, 2.0))
        code, text = self.run_cli("--cap-check", "--now", "2026-09-20T00:00:00Z")
        self.assertEqual((code, text.strip()), (0, "cap check passed (tooling share n/a, tooling hours today 0.00)"))


START = datetime(2026, 9, 11, 10, 0, tzinfo=timezone.utc)
USAGE_HEADER = ("time\tharness\taddress\tbatch\tattempts\tbest\tseconds\tcost_usd\tinput\tcache_write"
                "\tcache_read\toutput\treasoning\tturns\tapi_ms\tdenials\terror\n")


def at(seconds: float) -> str:
    return (START + timedelta(seconds=seconds)).strftime("%Y-%m-%dT%H:%M:%SZ")


def event(offset: float, name: str, text: str = "", **fields) -> dict:
    return {"ts": at(offset), "event": name, "text": text, "fields": fields}


def bash(identifier: str, seconds: float, command: str) -> dict:
    return {"type": "assistant", "timestamp": at(seconds), "message": {"id": f"m{identifier}", "content": [
        {"type": "tool_use", "id": identifier, "name": "Bash", "input": {"command": command}}]}}


def done(identifier: str, seconds: float, content: object = "") -> dict:
    return {"type": "user", "timestamp": at(seconds),
            "message": {"content": [{"type": "tool_result", "tool_use_id": identifier, "content": content}]}}


class RunReportTests(unittest.TestCase):
    """--runs on fixture events, usage rows and transcripts of both harnesses."""

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.runs = Path(directory.name)

    def write(self, name: str, lines: list, seconds: float | None = None) -> None:
        path = self.runs / name
        text = "".join((line if isinstance(line, str) else json.dumps(line)) + "\n" for line in lines)
        path.write_bytes(gzip.compress(text.encode()) if name.endswith(".gz") else text.encode())
        if seconds is not None:
            stamp = (START + timedelta(seconds=seconds)).timestamp()
            os.utime(path, (stamp, stamp))

    def write_two_campaigns(self) -> None:
        a, b = "0x00401000", "0x00402000"
        self.write("events.jsonl", [
            event(0, "harness", harness="claude"), event(0, "phase", phase="select", campaign=1),
            event(10, "phase", phase="start"), event(40, "phase", phase="baseline"),
            event(60, "campaign", "campaign 1", campaign=1, address=a, mode="refinement", baseline=50.0),
            event(60, "phase", phase="batch"),
            event(360, "batch", "batch 1", label="batch 1", attempts="2-4", seconds=300),
            event(362, "phase", phase="finish"), event(380, "phase", phase="verify"),
            event(420, "phase", phase="record"),
            event(430, "campaign", "campaign 1 done", campaign=1, address=a, result="source",
                  final=60.0, bytes=120.0, commit="abc1234", attempts=4),
            event(430, "phase", phase="select", campaign=2), event(440, "phase", phase="start"),
            event(460, "phase", phase="baseline"),
            event(470, "campaign", "campaign 2: 0x00402000 B refinement, baseline 30.00%",
                  campaign=2, address=b, mode="refinement"),
            event(470, "phase", phase="batch"),
            event(570, "batch", "batch 1", label="batch 1", attempts="2-3", seconds=100),
            event(572, "phase", phase="record"), event(580, "phase", phase="commit"),
            event(590, "campaign", "campaign 2: 0x00402000 B refinement 30.00% -> 30.00% (+0.00 B),"
                  " 3 attempts, 2.7 min, 9k tokens, commit def5678", campaign=2, address=b,
                  result="no-source", final=30.0, bytes=0.0, commit="def5678"),
            event(590, "final", "run: exit 0")])
        self.write("usage.tsv", [USAGE_HEADER.rstrip("\n"),
                                 f"{at(0)}\tclaude\t-\tcheck\tnone\t-\t7\t0.02\t4\t260\t16172\t241\t128\t2\t\t0\t0",
                                 f"{at(361)}\tclaude\t{a}\tbatch 1\t2-4\t60.0\t300\t1.5000\t20\t10000"
                                 "\t200000\t5000\t3000\t12\t250000\t0\t0",
                                 f"{at(571)}\tcodex\t{b}\tbatch 1\t2-3\t30.0\t100\t\t9000\t0\t50000\t1000"
                                 "\t400\t3\t\t0\t0"])
        edit = "python3 - <<'EOF'\np = Path('src/a.cpp')\np.write_text(p.read_text())\nEOF"
        self.write(f"{a}-batch1.jsonl", [
            {"type": "system", "subtype": "init"},
            bash("u1", 70, f"tools/decomp bc {a} 2>&1 | tail -n 30"), done("u1", 110, "x" * 2048),
            {"type": "assistant", "timestamp": at(115), "message": {"content": [
                {"type": "tool_use", "id": "u2", "name": "Bash", "input": {"command": "sed -n 1,9p src/a.cpp"}},
                {"type": "tool_use", "id": "u3", "name": "Bash", "input": {"command": f"tools/decomp bc {a} --hunk 2"}}]}},
            done("u2", 116), done("u3", 118, [{"type": "text", "text": "y" * 512}]),
            bash("u4", 120, edit), done("u4", 121),
            {"type": "result", "duration_ms": 300000, "num_turns": 12,
             "usage": {"output_tokens": 5000, "output_tokens_details": {"thinking_tokens": 3000}}}], 360)
        self.write(f"{b}-batch1.jsonl.gz", [
            {"type": "thread.started"}, {"type": "turn.started"},
            *({"type": "item.completed", "item": {"type": "command_execution", "command": command,
                                                  "aggregated_output": "z" * 100}}
              for command in (f"bash -lc 'tools/decomp bc {b}'", "bash -lc 'git checkout -- src'",
                              'bash -lc "grep -n Foo src/b.cpp"')),
            {"type": "item.completed", "item": {"type": "file_change"}},
            {"type": "turn.completed", "usage": {"input_tokens": 59000, "cached_input_tokens": 50000}}], 570)

    def test_the_report_splits_campaigns_into_phases_writer_runs_and_calls(self):
        self.write_two_campaigns()
        report = throughput.run_report(self.runs)
        first, second = report["campaigns"]
        self.assertEqual(first["phases"], {"select": 10.0, "start": 30.0, "baseline": 20.0,
                                           "writer": 302.0, "finish": 18.0, "verify": 40.0, "record": 10.0})
        run = first["writer_runs"][0]
        self.assertEqual(run["calls"], {"build": 1, "source read": 1, "hunk": 1, "edit": 1})
        self.assertEqual((run["output_bytes"]["build"], run["output_bytes"]["hunk"]), (2048, 512))
        self.assertEqual((run["tool_seconds"], run["model_seconds"]), (44.0, 256.0))
        self.assertEqual((run["input_equivalent"], run["tokens"]["thinking"], run["cost"]),
                         (55020.0, 3000.0, 1.5))
        self.assertEqual((second["baseline"], second["attempts"], second["result"]), (30.0, 3, "no-source"))
        self.assertEqual(second["phases"], {"select": 10.0, "start": 20.0, "baseline": 10.0,
                                            "writer": 102.0, "record": 8.0, "commit": 10.0})
        self.assertEqual(list(second["phases"])[-2:], ["record", "commit"])  # an unknown phase comes last
        run = second["writer_runs"][0]
        self.assertEqual(run["calls"], {"build": 1, "restore": 1, "source read": 1, "edit": 1})
        self.assertEqual((run["transcript"], run["output_bytes"]["restore"]), ("0x00402000-batch1.jsonl.gz", 100))
        self.assertEqual((run["harness"], run["model_seconds"], run["cost"]), ("codex", None, None))
        totals = report["totals"]
        self.assertEqual((totals["bytes"], totals["calls"], totals["attempts"]), (120.0, 8, 5))
        self.assertAlmostEqual(totals["bytes_per_minute"], 120.0 / (590 / 60))
        self.assertAlmostEqual(totals["bytes_per_million_ie"], 120.0 / 74020 * 1e6)
        self.assertEqual((totals["calls_per_attempt"], totals["call_share"]["build"]), (1.6, 0.25))
        self.assertEqual(totals["call_output_bytes"]["build"], 2148)
        self.assertEqual(report["unassigned_runs"], [])
        with mock.patch.object(throughput, "writer_run", wraps=throughput.writer_run) as reader:
            self.assertEqual([c["address"] for c in throughput.run_report(self.runs, 1)["campaigns"]],
                             ["0x00402000"])
        self.assertEqual(reader.call_count, 1)  # --last reads only the kept campaigns' transcripts
        with redirect_stdout(StringIO()) as out:
            self.assertEqual(throughput.main(["--runs", "--dir", str(self.runs)]), 0)
        lines = out.getvalue().splitlines()
        self.assertTrue(lines[0].endswith(throughput.WEIGHTS))
        self.assertEqual(lines[1], "campaign 1: 0x00401000 refinement 50.00 -> 60.00%, +120.00 B,"
                                   " 4 attempts, source, commit abc1234")
        self.assertEqual(lines[2], "  all-in 430 s: select 10, start 30, baseline 20, writer 302,"
                                   " finish 18, verify 40, record 10")
        self.assertIn("build 1: 2.0 KB", lines[3])
        self.assertIn("model 256 s, tool 44 s", lines[3])
        self.assertIn("calls: 8 in 5 logged attempts, 1.6 per attempt; build 25% 2.1 KB, hunk 12% 0.5 KB,"
                      " source read 25% 0.1 KB, edit 25% 0.0 KB, restore 12% 0.1 KB", lines)
        with redirect_stdout(StringIO()) as out:
            throughput.main(["--runs", "--dir", str(self.runs), "--json", "--last", "1"])
        self.assertEqual(json.loads(out.getvalue())["totals"]["campaigns"], 1)

    def test_an_older_log_without_phase_events_still_splits_setup_writer_and_finish(self):
        self.write("events.jsonl", [
            event(20, "campaign", "campaign 1: 0x00401000 A refinement, baseline 50.00%",
                  campaign=1, address="0x00401000", mode="refinement"),
            event(320, "batch", "batch 1", label="batch 1", attempts="2-5", seconds=290),
            event(400, "campaign", "campaign 1: 0x00401000 A refinement 50.00% -> 55.00% (+9.50 B),"
                  " 5 attempts, 6.5 min, $1.00, commit abc", campaign=1, address="0x00401000",
                  result="source", final=55.0, bytes=9.5, commit="abc")])
        self.write("usage.tsv", [USAGE_HEADER.rstrip("\n"),
                                 f"{at(319)}\tclaude\t0x00401000\tbatch 1\t2-5\t55\t290\t1.0\t1\t2\t3\t4\t\t5\t\t0\t0",
                                 f"{at(900)}\tclaude\t0x00409000\tbatch 1\t2-2\t1\t9\t0.1\t1\t2\t3\t4\t\t1\t\t0\t0"])
        self.write("0x00401000-batch1.json", [{"type": "result", "num_turns": 5}], 318)
        report = throughput.run_report(self.runs)
        campaign = report["campaigns"][0]
        self.assertEqual(campaign["phases"], {"writer": 290.0, "finish": 80.0, "setup": 10.0, "other": 10.0})
        self.assertEqual((campaign["writer_runs"][0]["calls"], campaign["writer_runs"][0]["transcript_kind"]),
                         (None, "claude-json"))
        self.assertEqual([run["address"] for run in report["unassigned_runs"]], ["0x00409000"])
        lines = throughput.render_runs(report)
        self.assertEqual(lines[1], "calls: not counted for 2 writer runs (no stream-json or codex transcript)")
        self.assertFalse([line for line in lines[2:] if line.startswith("calls:")])

    def test_shell_calls_fall_into_one_category_each(self):
        cases = {
            "tools/decomp bc 0x1": "build", "clang-format -i src/a.cpp && tools/decomp bc 0x1": "build",
            "tools/decomp bc 0x1 --hunks": "hunk", "tools/decomp bc 0x1 --pack": "pack",
            "sed -i 's/a/b/' src/a.cpp": "edit", "sed -n 1,5p src/a.cpp | nl": "source read",
            "grep -rn Foo src": "source read", "bash -lc 'cat src/a.cpp'": "source read",
            "tools/decomp evidence 0x1 --full": "evidence/notes",
            "tools/decomp notes vtable --source models": "evidence/notes",
            "tools/decomp handoff 0x1 <<'EOF'\n# x\nEOF": "handoff",
            "git checkout -- src && git apply best.patch": "restore", "git diff --stat": "other",
            "git show abc1234:src/a.cpp > src/a.cpp": "restore",
        }
        self.assertEqual({command: throughput.classify(command) for command in cases}, cases)


if __name__ == "__main__":
    unittest.main()
