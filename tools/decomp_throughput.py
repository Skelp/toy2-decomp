#!/usr/bin/env python3
"""Git-derived throughput meter for the decompilation loop.

Walks the first-parent commits of HEAD inside a window, reads the committed
scoreboard at each commit (falling back to counting ``// FUNCTION:`` markers),
and reports function and byte deltas, hours per commit class, and the tooling
share. ``--experiments`` reviews pending tooling experiments against the
campaign ledger; ``--cap-check`` enforces the tooling cap once its window opens.
Deltas run from the first-parent of the oldest window commit (the base) to the
newest commit. Commit intervals and ledger idle gaps cap at 90 minutes.
``--runs`` reports the scripted campaigns of build/decomp-runs instead: phases,
writer runs, tool calls by category and token use per retained byte.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import json
import re
import statistics
import subprocess
import sys
from collections import Counter
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


def annotation_count(repo: Path, sha: str) -> int:
    markers = git(repo, "grep", "-c", "// FUNCTION:", sha, "--", "src").splitlines()
    return sum(int(c) for c in (line.rpartition(":")[2] for line in markers) if c.isdigit())


def commit_metrics(repo: Path, sha: str) -> dict:
    # The annotation count is always recorded so that a window whose base
    # predates the scoreboard still compares functions on one definition.
    annotations = annotation_count(repo, sha)
    text = git(repo, "show", f"{sha}:{SCOREBOARD}")
    if text.strip():
        return parse_scoreboard(text) | {"annotations": annotations}
    return dict.fromkeys(METRICS) | {
        "implemented": annotations, "annotations": annotations, "fallback": True
    }


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
    if last["fallback"] or start["fallback"]:
        # Mixed definitions would compare reccmp rows against annotations.
        deltas["implemented"] = last["annotations"] - start["annotations"]
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

# The run report (--runs) of `tools/decomp campaigns run`. It reads events.jsonl, the
# usage*.tsv rows and the writer transcripts of build/decomp-runs, and splits each
# scripted campaign into phases, writer runs and tool calls.
RUNS = "build/decomp-runs"
WEIGHTS = "ie = new + 0.1 cache read + 5 output"
# Phase events in campaign order; finish and validate log their steps (annotate to
# patcher) only in a driver run. An unknown phase follows these.
PHASES = ("select", "start", "baseline", "writer", "finish", "annotate", "build", "reports", "verify",
          "scoreboard", "lint", "patcher", "record", "repair", "push", "setup", "other")
CATEGORIES = ("build", "hunk", "pack", "source read", "edit", "evidence/notes", "handoff",
              "restore", "other")
PRIORITY = ("build", "restore", "handoff", "pack", "hunk", "edit", "evidence/notes", "source read")
TRANSCRIPTS = (".jsonl", ".json", ".txt", ".jsonl.gz", ".json.gz", ".txt.gz")
TRANSCRIPT_SLACK = 180.0  # seconds between the last write of a transcript and its usage row
HARNESS_OF = {"claude-stream": "claude", "claude-json": "claude", "codex": "codex"}
SHELL_WRAPPER = re.compile(r"^\s*(?:/usr)?(?:/bin/)?(?:ba)?sh\s+-l?c\s+")
SEGMENTS = re.compile(r"&&|\|\||[;|\n]")
EDIT = re.compile(r"\bsed\s+(?:-[a-zA-Z]*i|--in-place)|\bperl\s+-[a-z]*i|clang-format\s+-i"
                  r"|apply_patch|>\s*['\"]?(?:\./)?src/|\btee\s+(?:-a\s+)?['\"]?src/"
                  r"|write_text\(|\.write\(")
BC = re.compile(r"\bdecomp\s+bc\b(.*)")
SOURCE_PATH = re.compile(r"(?:^|[\s'\"=:(])(?:\./)?src(?:/|\s|$)")
RULES = (
    ("restore", re.compile(r"\bgit\s+(?:checkout|apply|restore|stash)\b"
                           r"|\bgit\s+show\s+\S+:\S+\s*>\s*['\"]?(?:\./)?src/")),
    ("handoff", re.compile(r"\bdecomp\s+handoff\b")),
    ("evidence/notes", re.compile(r"\bdecomp(?:\s+campaigns)?\s+(?:evidence|notes|candidates|blockers)\b")),
    ("source read", re.compile(r"^\s*['\"]?(?:sed|awk|grep|egrep|rg|cat|head|tail|nl)\b")),
)
CAMPAIGN_TEXT = {"baseline": r"baseline (-?[\d.]+)%", "final": r"-> (-?[\d.]+)%",
                 "attempts": r"(\d+) attempts", "minutes": r"([\d.]+) min\b"}


def classify(command: str) -> str:
    """Name the category of one shell call. A call that also builds (bc) is a build."""
    command = SHELL_WRAPPER.sub("", command)
    found = {"edit"} if EDIT.search(command) else set()
    for segment in SEGMENTS.split(command):
        bc = BC.search(segment)
        if bc:
            view = re.search(r"--(pack|hunks?|compact)\b", bc.group(1))
            found.add("build" if view is None else "pack" if view.group(1) == "pack" else "hunk")
        for name, pattern in RULES:
            if pattern.search(segment) and (name != "source read" or SOURCE_PATH.search(segment)):
                found.add(name)
    return next((name for name in PRIORITY if name in found), "other")


def call_category(name: str, arguments: dict) -> str:
    if name.lower() in ("bash", "shell"):
        return classify(str(arguments.get("command") or ""))
    if name in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
        return "edit"
    return "source read" if name in ("Read", "Grep", "Glob") and "src" in json.dumps(arguments) else "other"


def merged_seconds(spans: list[tuple[datetime, datetime]]) -> float:
    """Return the seconds covered by the spans; parallel tool calls count once."""
    total, reached = 0.0, None
    for first, last in sorted(spans):
        first = max(first, reached) if reached else first
        total += max(0.0, (last - first).total_seconds())
        reached = max(reached, last) if reached else last
    return total


def result_size(content: object) -> int:
    """Return the bytes of text in one tool result: a string or a list of text blocks."""
    if isinstance(content, str):
        return len(content.encode("utf-8"))
    if isinstance(content, list):
        return sum(result_size(item.get("text")) for item in content if isinstance(item, dict))
    return 0


def read_transcript(path: Path) -> dict:
    """Read one writer transcript (claude stream-json or json, codex JSONL, text; any of them
    gzipped). Calls, and the bytes of the output they put into the context, count by category."""
    data = path.read_bytes()
    data = gzip.decompress(data) if path.suffix == ".gz" else data
    kind, calls, output, starts, spans, result, timed = "text", Counter(), Counter(), {}, [], {}, False
    for line in data.decode("utf-8", errors="replace").splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(event, dict):
            continue
        what, stamp = event.get("type"), parse_time(event["timestamp"]) if event.get("timestamp") else None
        message = event.get("message") if isinstance(event.get("message"), dict) else {}
        content = message.get("content") if isinstance(message.get("content"), list) else []
        content = [item for item in content if isinstance(item, dict)]
        if what == "assistant":
            kind, timed = "claude-stream", timed or stamp is not None
            for item in content:
                if item.get("type") == "tool_use":
                    arguments = item.get("input") if isinstance(item.get("input"), dict) else {}
                    category = call_category(str(item.get("name")), arguments)
                    calls[category] += 1
                    starts[item.get("id")] = (stamp, category)
        elif what == "user":
            for item in content:
                begin, category = starts.get(item.get("tool_use_id"), (None, None))
                if item.get("type") == "tool_result" and category:
                    output[category] += result_size(item.get("content"))
                    if begin and stamp:
                        spans.append((begin, stamp))
        elif what == "result":
            kind, result = ("claude-stream" if kind == "claude-stream" else "claude-json"), event
        elif what == "item.completed" and isinstance(event.get("item"), dict):
            kind, item = "codex", event["item"]
            if item.get("type") == "command_execution":
                category = classify(str(item.get("command") or ""))
                calls[category] += 1
                output[category] += result_size(item.get("aggregated_output"))
            elif item.get("type") in ("file_change", "mcp_tool_call", "web_search"):
                calls["edit" if item["type"] == "file_change" else "other"] += 1
        elif str(what).startswith(("thread.", "turn.")):
            kind = "codex"
    usage = result.get("usage") if isinstance(result.get("usage"), dict) else {}
    details = usage.get("output_tokens_details")
    thinking = details.get("thinking_tokens") if isinstance(details, dict) else 0
    wall = result.get("duration_ms")
    counted = kind in ("claude-stream", "codex")
    return {"kind": kind, "calls": dict(calls) if counted else None, "output": dict(output) if counted else None,
            "thinking": float(thinking or 0), "wall": float(wall) / 1000 if isinstance(wall, (int, float)) else None,
            "tool_seconds": merged_seconds(spans) if timed else None, "bytes": len(data)}


def number(value: object) -> float:
    try:
        return float(value)  # type: ignore[arg-type]
    except (TypeError, ValueError):
        return 0.0


def read_events(directory: Path) -> list[dict]:
    path, events = directory / "events.jsonl", []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines() if path.is_file() else []:
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        stamp = parse_time(event.get("ts")) if isinstance(event, dict) else None
        if stamp:
            fields = event.get("fields") if isinstance(event.get("fields"), dict) else {}
            events.append(event | {"fields": fields, "text": str(event.get("text") or ""), "_time": stamp})
    return events


def text_number(text: str, key: str) -> float | int | None:
    found = re.search(CAMPAIGN_TEXT[key], text)
    return None if found is None else int(found.group(1)) if key == "attempts" else float(found.group(1))


def split_campaigns(events: list[dict]) -> list[dict]:
    """Cut the event log into campaigns: a select phase (or, in an older log, the campaign
    line) opens one; the closing campaign line, an error or the final line ends it."""
    campaigns: list[dict] = []
    current: dict | None = None
    for event in events:
        kind, fields, when, text = event.get("event"), event["fields"], event["_time"], event["text"]
        opened = current is not None and "end" not in current
        if kind == "phase":
            if fields.get("phase") == "select" or not opened:
                current = {"begin": when, "phases": [], "batches": [], "index": fields.get("campaign")}
                campaigns.append(current)
            current["phases"].append((str(fields.get("phase")), when))
        elif kind == "campaign" and "result" not in fields:
            if not opened:
                current = {"begin": None, "phases": [], "batches": []}
                campaigns.append(current)
            baseline = fields.get("baseline", text_number(text, "baseline"))
            current.update(index=fields.get("campaign"), address=fields.get("address"),
                           mode=fields.get("mode"), baseline=baseline, started=when)
        elif not opened:
            continue
        elif kind == "campaign":
            minutes = fields.get("minutes", text_number(text, "minutes"))
            current.update(end=when, result=fields.get("result"), final=fields.get("final"),
                           bytes=number(fields.get("bytes")), commit=fields.get("commit"),
                           attempts=fields.get("attempts", text_number(text, "attempts")))
            current["address"] = current.get("address") or fields.get("address")
            if current["begin"] is None and minutes:
                current["begin"] = when - timedelta(minutes=float(minutes))
        elif kind == "batch" and fields.get("label"):
            current["batches"].append((str(fields["label"]), when, number(fields.get("seconds"))))
        elif kind in ("error", "final"):
            current.update(end=when, result="stopped", bytes=0.0)
    last = events[-1]["_time"] if events else None
    for campaign in campaigns:
        if "end" not in campaign:
            campaign.update(end=last, result="open", bytes=0.0)
        campaign["begin"] = campaign["begin"] or campaign.get("started") or campaign["end"]
    return [campaign for campaign in campaigns if campaign.get("address")]


def campaign_phases(campaign: dict) -> dict[str, float]:
    """Seconds per phase from the phase events; an older log without them gives setup
    (select, start and baseline), writer (the batch seconds) and finish."""
    phases: Counter = Counter()
    marks, end = campaign["phases"], campaign["end"]
    for (name, when), (_, following) in zip(marks, marks[1:] + [("", end)]):
        phases["writer" if name == "batch" else name] += max(0.0, (following - when).total_seconds())
    if not marks:
        if campaign.get("started"):
            phases["setup"] = (campaign["started"] - campaign["begin"]).total_seconds()
        phases["writer"] = sum(seconds for _, _, seconds in campaign["batches"])
        if campaign["batches"] and campaign["result"] in ("source", "no-source"):
            phases["finish"] = (end - campaign["batches"][-1][1]).total_seconds()
    rest = (end - campaign["begin"]).total_seconds() - sum(phases.values())
    if rest >= 2.0:  # whole-second stamps round each phase boundary by under a second
        phases["other"] += rest
    return {name: round(phases[name], 1) for name in ordered(phases)}


def ordered(phases: dict) -> list[str]:
    """Return the phases with time in PHASES order, then any other phase by name."""
    return [name for name in PHASES + tuple(sorted(set(phases) - set(PHASES))) if phases.get(name)]


def read_usage(directory: Path) -> list[dict]:
    """Return the writer runs of every usage*.tsv file (a set-aside file keeps its own header)."""
    rows = []
    for path in sorted(directory.glob("usage*.tsv")):
        with path.open(encoding="utf-8", newline="") as handle:
            for row in csv.DictReader(handle, delimiter="\t"):
                stamp = parse_time(row.get("time"))
                if stamp and row.get("batch") not in (None, "check"):
                    rows.append(row | {"_time": stamp})
    return sorted(rows, key=lambda row: row["_time"])


def find_transcript(directory: Path, address: str, label: str, when: datetime) -> Path | None:
    """Return the transcript a usage row belongs to; a later run of the target replaces it."""
    found = [path for path in directory.glob(f"{address}-{label.replace(' ', '')}.*")
             if path.name.endswith(TRANSCRIPTS) and ".last." not in path.name
             and abs(path.stat().st_mtime - when.timestamp()) <= TRANSCRIPT_SLACK]
    return min(found, key=lambda path: next(i for i, end in enumerate(TRANSCRIPTS)
                                            if path.name.endswith(end))) if found else None


def writer_run(row: dict, directory: Path) -> dict:
    label, address = str(row.get("batch") or ""), str(row.get("address") or "")
    path = find_transcript(directory, address, label, row["_time"])
    info = read_transcript(path) if path else {"kind": "none", "calls": None, "output": None, "thinking": 0.0,
                                                "wall": None, "tool_seconds": None, "bytes": None}
    span = re.fullmatch(r"(\d+)-(\d+)", str(row.get("attempts") or ""))
    new = number(row.get("input")) + number(row.get("cache_write"))
    cache, output = number(row.get("cache_read")), number(row.get("output"))
    seconds, tool = number(row.get("seconds")), info["tool_seconds"]
    return {
        "address": address, "label": label, "harness": row.get("harness") or HARNESS_OF.get(info["kind"], info["kind"]),
        "time": row["_time"].strftime("%Y-%m-%dT%H:%M:%SZ"), "_time": row["_time"],
        "seconds": seconds, "turns": int(number(row.get("turns"))),
        "attempts": int(span.group(2)) - int(span.group(1)) + 1 if span else 0,
        "tokens": {"new": new, "cache_read": cache, "output": output,
                   "thinking": number(row.get("reasoning")) or info["thinking"]},
        "input_equivalent": new + 0.1 * cache + 5 * output,
        "cost": number(row["cost_usd"]) if row.get("cost_usd") else None,
        "calls": info["calls"], "output_bytes": info["output"], "transcript_kind": info["kind"],
        "tool_seconds": tool, "model_seconds": None if tool is None else max(0.0, (info["wall"] or seconds) - tool),
        "transcript": path.name if path else None, "transcript_bytes": info["bytes"],
    }


def run_totals(campaigns: list[dict]) -> dict:
    runs = [run for campaign in campaigns for run in campaign["writer_runs"]]
    calls: Counter = Counter()
    output: Counter = Counter()
    phases: Counter = Counter()
    for run in runs:
        calls.update(run["calls"] or {})
        output.update(run["output_bytes"] or {})
    for campaign in campaigns:
        phases.update(campaign["phases"])
    minutes = sum(campaign["minutes"] for campaign in campaigns)
    produced = sum(campaign["bytes"] for campaign in campaigns)
    ie = sum(run["input_equivalent"] for run in runs)
    counted = sum(run["attempts"] for run in runs if run["calls"] is not None)
    costs = [run["cost"] for run in runs if run["cost"] is not None]
    return {
        "campaigns": len(campaigns), "writer_runs": len(runs), "bytes": produced, "minutes": minutes,
        "bytes_per_minute": produced / minutes if minutes else None,
        "input_equivalent": ie, "bytes_per_million_ie": produced / ie * 1e6 if ie else None,
        "cost": sum(costs) if costs else None, "calls": sum(calls.values()), "attempts": counted,
        "calls_per_attempt": sum(calls.values()) / counted if counted else None,
        "call_share": {name: calls[name] / sum(calls.values()) for name in CATEGORIES if calls[name]},
        "call_output_bytes": {name: output[name] for name in CATEGORIES if calls[name]},
        "phase_share": {name: phases[name] / sum(phases.values()) for name in ordered(phases)},
    }


def run_report(directory: Path, last: int | None = None) -> dict:
    campaigns, rows = split_campaigns(read_events(directory)), read_usage(directory)
    if last:  # read only the transcripts of the kept campaigns
        campaigns = campaigns[-last:]
        since = campaigns[0]["begin"] - timedelta(seconds=5) if campaigns else None
        rows = [row for row in rows if since is not None and row["_time"] >= since]
    runs = [writer_run(row, directory) for row in rows]
    for campaign in campaigns:
        begin, end = campaign["begin"] - timedelta(seconds=5), campaign["end"] + timedelta(seconds=5)
        campaign["writer_runs"] = [run for run in runs if run["address"] == campaign["address"]
                                   and begin <= run["_time"] <= end]
        campaign["minutes"] = (campaign["end"] - campaign["begin"]).total_seconds() / 60
        campaign["phases"] = campaign_phases(campaign)
    assigned = {id(run) for campaign in campaigns for run in campaign["writer_runs"]}
    unassigned = [run for run in runs if id(run) not in assigned] if last is None else []
    clean = lambda row: {k: v for k, v in row.items() if not k.startswith("_")}  # noqa: E731
    for campaign in campaigns:
        campaign.update(begin=iso(campaign["begin"].timestamp()), end=iso(campaign["end"].timestamp()),
                        batches=len(campaign["batches"]), writer_runs=[clean(r) for r in campaign["writer_runs"]])
        campaign.pop("started", None)
    return {"directory": str(directory), "weights": {"new": 1.0, "cache_read": 0.1, "output": 5.0},
            "campaigns": campaigns, "totals": run_totals(campaigns),
            "unassigned_runs": [clean(run) for run in unassigned]}


def kilo(value: float) -> str:
    return f"{value / 1e6:.2f}M" if value >= 1e6 else f"{value / 1e3:.1f}k"


def shares(values: dict) -> str:
    return ", ".join(f"{name} {share * 100:.0f}%" for name, share in values.items()) or "none"


def kb(value: float) -> str:
    return f"{value / 1024:.1f} KB"


def render_run(run: dict) -> str:
    tokens, calls, output = run["tokens"], run["calls"], run["output_bytes"] or {}
    parts = [f"  {run['label']}: {run['harness']}, {run['seconds']:.0f} s, {run['turns']} turns,"
             f" {run['attempts']} attempts"]
    if calls is not None:
        parts.append(f"{sum(calls.values())} calls (" + ", ".join(
            f"{name} {calls[name]}: {kb(output.get(name, 0))}" for name in CATEGORIES if calls.get(name)) + ")")
    parts.append(f"tokens new {kilo(tokens['new'])}, cache read {kilo(tokens['cache_read'])},"
                 f" output {kilo(tokens['output'])}, ie {kilo(run['input_equivalent'])}")
    if run["cost"] is not None:
        parts.append(f"${run['cost']:.2f}")
    if run["model_seconds"] is not None:
        parts.append(f"model {run['model_seconds']:.0f} s, tool {run['tool_seconds']:.0f} s")
    if run["transcript_bytes"] is not None:
        parts.append(f"transcript {run['transcript_bytes'] / 1024:.0f} KB")
    return ", ".join(parts)


def render_runs(report: dict) -> list[str]:
    totals = report["totals"]
    runs = [run for campaign in report["campaigns"] for run in campaign["writer_runs"]]
    lines = [f"runs: {report['directory']}; {totals['campaigns']} campaigns,"
             f" {totals['writer_runs']} writer runs; {WEIGHTS}"]
    uncounted = sum(run["calls"] is None for run in runs + report["unassigned_runs"])
    if uncounted:
        lines.append(f"calls: not counted for {uncounted} writer runs (no stream-json or codex transcript)")
    for campaign in report["campaigns"]:
        final = campaign.get("final")
        lines.append(f"campaign {campaign.get('index')}: {campaign['address']} {campaign.get('mode')}"
                     f" {fmt(campaign.get('baseline'), 2)} -> {fmt(final, 2)}%, {campaign['bytes']:+.2f} B,"
                     f" {fmt(campaign.get('attempts'), 0)} attempts, {campaign['result']},"
                     f" commit {campaign.get('commit') or '-'}")
        lines.append(f"  all-in {campaign['minutes'] * 60:.0f} s: " + ", ".join(
            f"{name} {seconds:.0f}" for name, seconds in campaign["phases"].items()))
        lines += [render_run(run) for run in campaign["writer_runs"]]
    lines.append(f"totals: {totals['bytes']:+.2f} B in {totals['minutes']:.1f} min all-in,"
                 f" {fmt(totals['bytes_per_minute'], 2)} B per all-in minute")
    cost = "n/a" if totals["cost"] is None else f"${totals['cost']:.2f}"
    lines.append(f"tokens: {kilo(totals['input_equivalent'])} ie,"
                 f" {fmt(totals['bytes_per_million_ie'], 1)} B per million ie, cost {cost}")
    if totals["calls"]:
        lines.append(f"calls: {totals['calls']} in {totals['attempts']} logged attempts,"
                     f" {fmt(totals['calls_per_attempt'], 1)} per attempt; " + ", ".join(
                         f"{name} {share * 100:.0f}% {kb(totals['call_output_bytes'][name])}"
                         for name, share in totals["call_share"].items()))
    lines.append("phases: " + shares(totals["phase_share"]))
    if report["unassigned_runs"]:
        lines.append("writer runs without a campaign in events.jsonl:")
        lines += [f"  {run['address']} {run['time']}" + render_run(run)[1:] for run in report["unassigned_runs"]]
    return lines


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
    parser.add_argument("--runs", action="store_true", help="report the scripted campaigns of build/decomp-runs")
    parser.add_argument("--last", type=int, help="with --runs: only the last N campaigns")
    parser.add_argument("--dir", help="with --runs: the run directory (default: REPO/build/decomp-runs)")
    args = parser.parse_args(argv)
    if args.runs:
        if args.last is not None and args.last < 1:
            parser.error("--last needs a positive count")
        report = run_report(Path(args.dir) if args.dir else Path(args.repo).resolve() / RUNS, args.last)
        print(json.dumps(report, indent=2) if args.json else "\n".join(render_runs(report)))
        return 0
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
