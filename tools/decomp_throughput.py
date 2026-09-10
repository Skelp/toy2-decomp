#!/usr/bin/env python3
"""Git-derived throughput meter for the decompilation loop.

Walks the first-parent commits of HEAD inside a window, reads the committed
scoreboard at each commit (falling back to counting ``// FUNCTION:`` markers),
and reports function and byte deltas, hours per commit class, and the tooling
share. ``--experiments`` reviews pending tooling experiments against the
campaign ledger; ``--cap-check`` enforces the tooling cap once its window opens.
Deltas run from the first-parent of the oldest window commit (the base) to the
newest commit. Commit intervals and ledger idle gaps cap at 90 minutes.
"""
from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

SCOREBOARD = "tools/Resources/scoreboard.tsv"
FUNCTIONS_MAP = "tools/Resources/functions_map.txt"
LEDGER = "tools/Resources/campaign-ledger.jsonl"
EXPERIMENTS = "tools/Resources/tooling-experiments.tsv"
BASELINE = "tools/Resources/throughput-baseline.json"
EXPERIMENT_COLUMNS = "id opened_commit opened_at hypothesis baseline_rate review_after outcome note".split()
TOOLING_PREFIXES = ("tools/", "AGENTS.md", "CLAUDE.md", ".agents/", "docs/", "ROADMAP.md", ".github/")
CLASSES = ("source", "tooling", "other")
METRICS = ("implemented", "terminal", "terminal_bytes", "effective_bytes")
CAP_MINUTES = 90.0
REVIEW_ROWS = 10
TOOLING_SHARE_CAP = 0.15
TOOLING_HOURS_TODAY_CAP = 1.0
FLOOR_FRACTION = 0.25
UNKNOWN = "unknown (fallback)"


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=False)
    return result.stdout if result.returncode == 0 else ""


def parse_time(value: object) -> datetime | None:
    try:
        stamp = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
    except ValueError:
        return None
    return (stamp if stamp.tzinfo else stamp.replace(tzinfo=timezone.utc)).astimezone(timezone.utc)


def iso(seconds: float) -> str:
    return datetime.fromtimestamp(seconds, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def fmt(value: object, digits: int = 1, missing: str = "n/a") -> str:
    return missing if value is None else f"{value:.{digits}f}" if isinstance(value, float) else str(value)


def parse_scoreboard(text: str) -> dict:
    counts = dict.fromkeys(METRICS, 0)
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) < 6 or line.startswith(("#", "address")):
            continue
        try:
            size, matching = int(fields[1]), float(fields[2])
            done, debt = bool(int(fields[3]) or int(fields[4])), int(fields[5])
        except ValueError:
            continue
        counts["implemented"] += 1 if matching > 0 or done else 0
        counts["terminal"] += 1 if done and debt == 0 else 0
        counts["terminal_bytes"] += size if done and debt == 0 else 0
        counts["effective_bytes"] += size * matching
    return counts | {"fallback": False}


def commit_metrics(repo: Path, sha: str) -> dict:
    text = git(repo, "show", f"{sha}:{SCOREBOARD}")
    if text.strip():
        return parse_scoreboard(text)
    markers = git(repo, "grep", "-c", "// FUNCTION:", sha, "--", "src").splitlines()
    counted = sum(int(c) for c in (line.rpartition(":")[2] for line in markers) if c.isdigit())
    return dict.fromkeys(METRICS) | {"implemented": counted, "fallback": True}


def classify_paths(paths: list[str]) -> str:
    klass = "other"
    for path in paths:
        if path.startswith("src/") or path == FUNCTIONS_MAP:
            return "source"
        if path.startswith(TOOLING_PREFIXES):
            klass = "tooling"
    return klass


def list_commits(repo: Path) -> list[dict]:
    rows = []  # first-parent commits of HEAD, newest first
    for line in git(repo, "log", "--first-parent", "--format=%H%x09%ct%x09%P", "HEAD").splitlines():
        fields = line.split("\t")
        if len(fields) >= 2 and fields[1].isdigit():
            rows.append({"sha": fields[0], "time": int(fields[1])})
    return rows


def describe_window(repo: Path, rows: list[dict], count: int) -> tuple[list[dict], dict | None]:
    """Describe the newest ``count`` commits (returned oldest first) and their base."""
    window = []
    for index in range(min(count, len(rows))):
        row = dict(rows[index])
        parent = rows[index + 1] if index + 1 < len(rows) else None
        gap = (row["time"] - parent["time"]) / 60 if parent else 0.0
        row["interval_hours"] = min(CAP_MINUTES, max(0.0, gap)) / 60
        paths = git(repo, "show", "--format=", "--name-only", "--first-parent", row["sha"])
        row["class"] = classify_paths([p.strip() for p in paths.splitlines() if p.strip()])
        window.append(row | commit_metrics(repo, row["sha"]))
    window.reverse()
    base = rows[count] | commit_metrics(repo, rows[count]["sha"]) if window and count < len(rows) else None
    return window, base


def summarise(window: list[dict], base: dict | None, attempts: float | None) -> dict:
    hours, counts = dict.fromkeys(CLASSES, 0.0), dict.fromkeys(CLASSES, 0)
    for row in window:
        hours[row["class"]] += row["interval_hours"]
        counts[row["class"]] += 1
    total, last, start = sum(hours.values()), window[-1], base or window[0]
    deltas = {k: None if last[k] is None or start[k] is None else last[k] - start[k] for k in METRICS}
    per_hour = deltas["effective_bytes"] / hours["source"] if (
        deltas["effective_bytes"] is not None and hours["source"] > 0) else None
    briefs = {k: r and {"sha": r["sha"], "time": iso(r["time"]), "fallback": r["fallback"]}
              for k, r in (("first", window[0]), ("last", last), ("base", base))}
    return {
        **briefs,
        "deltas": {"functions": deltas["implemented"], **{k: deltas[k] for k in METRICS[1:]}},
        "hours": hours | {"total": total}, "commits": counts | {"total": len(window)},
        "effective_bytes_per_source_hour": per_hour,
        "tooling_share": hours["tooling"] / total if total > 0 else None,
        "bc_attempts_per_source_result": attempts,
        "rows": [row | {"time": iso(row["time"])} for row in window],
    }


def render_summary(summary: dict) -> list[str]:
    d, h, c, base = summary["deltas"], summary["hours"], summary["commits"], summary["base"]
    base_note = f" (base {base['sha'][:10]})" if base else " (no base: root commit)"
    return [
        f"first commit: {summary['first']['sha'][:10]} {summary['first']['time']}{base_note}",
        f"last commit:  {summary['last']['sha'][:10]} {summary['last']['time']}",
        f"functions delta: {fmt(d['functions'], missing=UNKNOWN)}",
        f"terminal delta: {fmt(d['terminal'], missing=UNKNOWN)}",
        f"terminal bytes delta: {fmt(d['terminal_bytes'], missing=UNKNOWN)}",
        f"effective bytes delta: {fmt(d['effective_bytes'], missing=UNKNOWN)}",
        "hours: " + "  ".join(f"{k} {h[k]:.2f}" for k in CLASSES) + f"  (total {h['total']:.2f})",
        f"effective bytes per source active hour: {fmt(summary['effective_bytes_per_source_hour'])}",
        f"tooling share: {fmt(summary['tooling_share'], 2)}",
        "commits: " + "  ".join(f"{k} {c[k]}" for k in CLASSES) + f"  (total {c['total']})",
        f"bc attempts per source result: {fmt(summary['bc_attempts_per_source_result'])}",
    ]


def load_ledger(path: Path) -> tuple[list[dict], float | None]:
    """Source-mode rows sorted by timestamp with all-in minutes, plus median bc attempts."""
    rows, attempts = [], []
    for line in path.read_text(encoding="utf-8").splitlines() if path.is_file() else []:
        try:
            row = json.loads(line) if line.strip() else None
        except json.JSONDecodeError:
            row = None
        if not isinstance(row, dict):
            continue
        if row.get("result") == "source" and str(row.get("attempts", "")).isdigit() and int(row["attempts"]) > 0:
            attempts.append(int(row["attempts"]))
        stamp = parse_time(row.get("timestamp"))
        if (stamp and row.get("record_type") in (None, "campaign") and row.get("mode") != "meta"
                and row.get("result") in ("source", "no-source")):
            rows.append(row | {"_time": stamp, "_minutes": float(row.get("minutes") or 0.0)})
    rows.sort(key=lambda row: row["_time"])
    previous = None
    for row in rows:
        gap = (row["_time"] - previous["_time"]).total_seconds() / 60 - row["_minutes"] if previous else 0.0
        row["_all_in_minutes"] = row["_minutes"] + min(CAP_MINUTES, max(0.0, gap))
        previous = row
    return rows, (float(statistics.median(attempts)) if attempts else None)


def ledger_rate(rows: list[dict]) -> float | None:
    hours = sum(row["_all_in_minutes"] for row in rows) / 60
    produced = sum(float(r.get("effective_bytes") or 0) + float(r.get("initialized_bytes") or 0) for r in rows)
    return produced / hours if hours > 0 else None


def load_baseline(repo: Path) -> tuple[dict, float | None]:
    path, data = repo / BASELINE, {}
    try:
        data = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}
        floor = FLOOR_FRACTION * float(data["sep8_daytime_bytes_per_wall_hour"])
    except (json.JSONDecodeError, KeyError, TypeError, ValueError):
        floor = None
    return (data if isinstance(data, dict) else {}), floor


def review_experiments(repo: Path, ledger: list[dict], floor: float | None) -> list[dict]:
    path, reviews = repo / EXPERIMENTS, []
    for line in path.read_text(encoding="utf-8").splitlines() if path.is_file() else []:
        if not line.strip() or line.startswith(("#", "id\t")):
            continue
        exp = dict(zip(EXPERIMENT_COLUMNS, line.split("\t") + [""] * len(EXPERIMENT_COLUMNS)))
        if exp["outcome"] != "PENDING":
            continue
        opened = parse_time(exp["opened_at"])
        rows = [row for row in ledger if opened and row["_time"] > opened]
        rate, verdict = ledger_rate(rows), "PENDING"
        baseline = float(exp["baseline_rate"]) if exp["baseline_rate"].replace(".", "", 1).isdigit() else None
        needed = int(exp["review_after"]) if exp["review_after"].isdigit() else REVIEW_ROWS
        if len(rows) >= needed and rate is not None and baseline is not None:
            verdict = "KEEP" if rate >= baseline else "REVERT DUE" if (
                rate < 0.5 * baseline or (floor is not None and rate < floor)) else "PENDING"
        reviews.append({"id": exp["id"], "verdict": verdict, "rows": len(rows), "rate": rate,
                        "baseline_rate": baseline, "floor": floor, "hypothesis": exp["hypothesis"]})
    return reviews


def render_experiments(reviews: list[dict]) -> list[str]:
    lines = ["experiments:" if reviews else "experiments: none pending"]
    for r in reviews:
        detail = (f"{r['rows']} rows so far, rate {fmt(r['rate'])}" if r["verdict"] == "PENDING"
                  else f"rate {fmt(r['rate'])} vs baseline {fmt(r['baseline_rate'])}, {r['rows']} rows")
        lines.append(f"  {r['id']}  {r['verdict']} ({detail})  {r['hypothesis']}")
    return lines


def cap_check(window: list[dict], summary: dict | None, ledger: list[dict], baseline: dict,
              floor: float | None, now: datetime) -> tuple[int, list[str]]:
    opens = parse_time(baseline.get("cap_window_opens_after"))
    counted = [row for row in ledger if opens and row["_time"] > opens]
    if opens is None or len(counted) < REVIEW_ROWS:
        return 0, [f"cap window not started ({len(counted)} of {REVIEW_ROWS} source rows after "
                   f"cap_window_opens_after={fmt(opens and opens.isoformat())})"]
    share, tooling_commits = (summary["tooling_share"], summary["commits"]["tooling"]) if summary else (None, 0)
    today = sum(row["interval_hours"] for row in window
                if row["class"] == "tooling" and iso(row["time"])[:10] == now.strftime("%Y-%m-%d"))
    rate, problems = ledger_rate(ledger[-REVIEW_ROWS:]), []
    if share is not None and share > TOOLING_SHARE_CAP:
        problems.append(f"trailing-7-day tooling share {share:.2f} exceeds {TOOLING_SHARE_CAP:.2f}")
    if today > TOOLING_HOURS_TODAY_CAP:
        problems.append(f"tooling hours today {today:.2f} exceed {TOOLING_HOURS_TODAY_CAP:.1f}")
    if floor is not None and rate is not None and rate < floor and tooling_commits > 0:
        problems.append(f"trailing-{REVIEW_ROWS}-source-row rate {rate:.1f} is below the floor "
                        f"{floor:.1f} while {tooling_commits} tooling commits are in the window")
    if problems:
        return 1, ["cap check failed: stop tooling work and return to source"] + [f"  - {p}" for p in problems]
    return 0, [f"cap check passed (tooling share {fmt(share, 2)}, tooling hours today {today:.2f})"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Git-derived throughput meter (default window: 7 days).")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--since", help="ISO date; count first-parent commits at or after it")
    group.add_argument("--commits", type=int, help="count the newest N first-parent commits")
    group.add_argument("--window-days", type=float, help="count commits in the trailing N days")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of text")
    parser.add_argument("--experiments", action="store_true", help="review pending tooling experiments")
    parser.add_argument("--cap-check", action="store_true", help="enforce the 7-day tooling cap (exit 1 when tripped)")
    parser.add_argument("--repo", default=".", help="repository root (default: current directory)")
    parser.add_argument("--now", help="ISO timestamp to treat as the current time (testing aid)")
    args = parser.parse_args(argv)
    now = parse_time(args.now) if args.now else datetime.now(timezone.utc)
    since = parse_time(args.since) if args.since else None
    if now is None or (args.since and since is None) or (args.commits is not None and args.commits < 1):
        parser.error("--now and --since need ISO timestamps; --commits needs a positive count")
    repo = Path(args.repo).resolve()
    rows = list_commits(repo)
    if not rows:
        print(f"error: no commits found under {repo}", file=sys.stderr)
        return 2
    days = args.window_days if args.window_days is not None and not args.cap_check else 7.0
    cutoff = since if since and not args.cap_check else now - timedelta(days=days)
    count = args.commits if args.commits and not args.cap_check else sum(
        1 for row in rows if row["time"] >= cutoff.timestamp())
    window, base = describe_window(repo, rows, count)
    ledger, attempts = load_ledger(repo / LEDGER)
    summary = summarise(window, base, attempts) if window else None
    baseline, floor = load_baseline(repo)
    if args.cap_check:
        code, lines = cap_check(window, summary, ledger, baseline, floor, now)
        print("\n".join(lines))
        return code
    reviews = review_experiments(repo, ledger, floor) if args.experiments else None
    if args.json:
        print(json.dumps({**(summary or {"rows": []}), "experiments": reviews}, indent=2))
        return 0
    lines = render_summary(summary) if summary else ["no commits in the window"]
    print("\n".join(lines + (render_experiments(reviews) if reviews is not None else [])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
