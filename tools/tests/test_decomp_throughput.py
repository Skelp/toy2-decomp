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
                                                "terminal", "terminal_bytes", "effective_bytes", "fallback"})

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


if __name__ == "__main__":
    unittest.main()
