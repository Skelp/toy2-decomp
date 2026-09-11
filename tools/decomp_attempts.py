#!/usr/bin/env python3
"""Log reconstruction attempts per target and hint when to stop iterating."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_diff import read_similarity  # noqa: E402


def attempts_directory(root: Path = ROOT) -> Path:
    return root / "build" / "decomp-attempts"


ATTEMPTS_DIR = attempts_directory()
BEST_DIR = ROOT / "build" / "decomp-cache" / "best"
SOURCE_PATHS = ("src", "tools/Resources/functions_map.txt")
DEFAULT_BUDGET = 12
STALL_ATTEMPTS = 3
STALL_MIN_GAIN_POINTS = 0.5
EXTEND_GAIN_POINTS = 1.0


def canonical_address(value: str) -> str:
    try:
        return f"0x{int(value, 16):08X}"
    except (TypeError, ValueError) as error:
        raise argparse.ArgumentTypeError("use an address such as 0x00401000") from error


def read_rows(address: str, directory: Path = ATTEMPTS_DIR) -> list[dict[str, object]]:
    """Return the logged attempt rows of a target in attempt order."""
    path = directory / f"{canonical_address(address)}.jsonl"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    try:
        rows = [json.loads(line) for line in text.splitlines() if line.strip()]
    except json.JSONDecodeError:
        return []
    return [
        row for row in rows
        if isinstance(row, dict) and isinstance(row.get("raw"), (int, float))
    ]


def read_raws(address: str, directory: Path = ATTEMPTS_DIR) -> list[float]:
    """Return the logged similarity percentages of a target in attempt order."""
    return [float(row["raw"]) for row in read_rows(address, directory)]


def parse_diff(text: str) -> tuple[float, bool, bool] | None:
    """Return (percent, exact, effective) from a reccmp verbose diff, else None."""
    similarity = read_similarity(text)
    if similarity is None:
        return None
    effective = re.search(r"effective\s+match|\(effective\)", text, re.I) is not None
    return round(similarity * 100.0, 2), similarity >= 1.0 and not effective, effective


def base_budget() -> int:
    """Return the base attempt budget. `campaigns run --budget N` exports
    DECOMP_BUDGET, so the writer's bc counts against the budget the driver enforces."""
    value = os.environ.get("DECOMP_BUDGET", "")
    return int(value) if value.isdigit() and int(value) >= 2 else DEFAULT_BUDGET


def budget(raws: list[float]) -> tuple[int, bool]:
    """Return (attempt budget, stalled) from the gains of every attempt after the first."""
    gains = [raw - max(raws[:index]) for index, raw in enumerate(raws) if index]
    extended = any(gain >= EXTEND_GAIN_POINTS for gain in gains[-4:])
    recent = gains[-STALL_ATTEMPTS:]
    stalled = len(recent) >= STALL_ATTEMPTS and all(g < STALL_MIN_GAIN_POINTS for g in recent)
    return 2 * base_budget() if extended else base_budget(), stalled


def read_stats(address: str, directory: Path = ATTEMPTS_DIR) -> dict[str, object]:
    raws = read_raws(address, directory)
    best = max(raws, default=None)
    limit, stalled = budget(raws)
    return {
        "attempts": len(raws), "best_attempt": raws.index(best) + 1 if raws else None,
        "best_raw": best, "last_raw": raws[-1] if raws else None,
        "stalled": stalled, "limit": limit,
    }


# Helpers of `tools/decomp campaigns run`, the scripted loop: they turn tool JSON
# and text into the tab-separated fields the bash driver reads.
def subsystem_of(row: dict[str, object]) -> str:
    """Name a campaign subsystem: the source file stem, else the class of the name."""
    source = str(row.get("source") or "")
    if source:
        return Path(source).stem
    parts = str(row.get("name") or "").split("::")
    return parts[-2] if len(parts) >= 2 else (parts[0] or "Unknown")


def pick_row(
    rows: list[dict[str, object]], address: str | None = None,
    coverage: bool = False, skip: tuple[str, ...] = (),
) -> dict[str, object] | None:
    """Return the forced target's row, else the first workable row not in skip."""
    for row in rows:
        here = canonical_address(str(row.get("address")))
        if address is not None:
            if here == address:
                return row
        elif (row.get("work_target") and not row.get("map_defect") and here not in skip
              and (row.get("dependency_ready") or not coverage)):
            return row
    return None


def mapped_row(address: str, root: Path) -> dict[str, object] | None:
    """Describe a forced target that no queue lists, from the map and the source."""
    name = None
    map_path = root / "tools" / "Resources" / "functions_map.txt"
    for line in map_path.read_text(encoding="utf-8").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].upper() == address.upper():
            name = parts[1].strip()
    if name is None:
        return None
    state, source = "NOT_STARTED", ""
    pattern = re.compile(rf"// (FUNCTION|STUB): TOY2 {address}\b", re.I)
    for path in sorted((root / "src").rglob("*.cpp")):
        found = pattern.search(path.read_text(encoding="utf-8", errors="ignore"))
        if found:
            state, source = found.group(1).upper(), path.relative_to(root / "src").as_posix()
            break
    size = 0
    try:
        sizes = json.loads((root / "build" / "decomp-function-sizes.json").read_text())
        size = next(int(i["size"]) for i in sizes if int(i["address"], 16) == int(address, 16))
    except (OSError, ValueError, StopIteration, KeyError, TypeError):
        pass
    return {"address": address, "name": name, "state": state, "size": size, "source": source}


def writer_usage(text: str) -> dict[str, object]:
    """Read the cost, token and turn fields of a `claude -p --output-format json` run."""
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        data = {}
    data = data if isinstance(data, dict) else {}
    usage = data.get("usage") if isinstance(data.get("usage"), dict) else {}
    return {
        "cost": float(data.get("total_cost_usd") or 0.0),
        "input": int(usage.get("input_tokens") or 0),
        "cache_write": int(usage.get("cache_creation_input_tokens") or 0),
        "cache_read": int(usage.get("cache_read_input_tokens") or 0),
        "output": int(usage.get("output_tokens") or 0),
        "turns": int(data.get("num_turns") or 0),
        "api_ms": int(data.get("duration_api_ms") or 0),
        "denials": len(data.get("permission_denials") or []),
        "error": int(bool(data.get("is_error"))),
        "result": str(data.get("result") or ""),
    }


def writer_failure(fields: dict[str, object], status: int) -> str:
    """Say why a writer run did no work, or return "" when it ran and returned."""
    if status == 124:
        return "the writer timed out"
    if status == 127:
        return "the writer command was not found"
    if status:
        return f"the writer exited with status {status}"
    if fields["error"]:
        first = (str(fields["result"]).strip().splitlines() or ["no text"])[0]
        return f"the writer reported an error: {first[:120]}"
    return "" if fields["turns"] else "the writer ran no turn"


def handoff_text(fields: dict[str, object]) -> str:
    """Return a batch writer's final message as handoff text, from its title line on."""
    if fields["error"]:
        return ""
    lines = [line for line in str(fields["result"]).strip().splitlines()
             if not line.lstrip().startswith("```")]
    titles = [index for index, line in enumerate(lines) if line.startswith("# ")]
    return "\n".join(lines[titles[0] if titles else 0:]).strip()


def tried_models(handoff: str) -> list[str]:
    """Return the TRIED lines of a handoff, without the baseline line."""
    models: list[str] = []
    inside = False
    for line in handoff.splitlines():
        if line.startswith("TRIED"):
            inside = True
        elif inside and line[:1] not in (" ", "\t"):
            break
        elif inside and line.strip() and "baseline" not in line.lower():
            models.append(" ".join(line.split()))
    return models


def failure_lines(log: str) -> list[str]:
    """Return the validate problems and lint findings of a validate log."""
    found: list[str] = []
    inside = False
    for line in log.splitlines():
        if line.strip() == "validation failed:":
            inside = True
            continue
        inside = inside and line.startswith("- ")
        if inside or re.search(r": (error|warning): |^lint: failed", line):
            if line not in found:
                found.append(line)
    return found[:40]


def clear_attempts(addresses: list[str], directory: Path = ATTEMPTS_DIR) -> None:
    for address in addresses:
        (directory / f"{canonical_address(address)}.jsonl").unlink(missing_ok=True)


def source_diff(root: Path) -> bytes | None:
    """Return the diff of the source paths against HEAD, or None when git fails."""
    command = ["git", "diff", "HEAD", "--", *SOURCE_PATHS]
    try:
        return subprocess.run(command, cwd=root, capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None


def source_tree(diff: bytes | None) -> str:
    """Return the sha256 of a source diff, or an empty string when git failed."""
    return "" if diff is None else hashlib.sha256(diff).hexdigest()


def save_best_diffs(address: str, diff_path: Path, root: Path) -> list[Path]:
    """Copy the verbose and compact diffs of a new best next to its patch."""
    name = canonical_address(address)
    compact_path = diff_path.with_name(f"{diff_path.stem}.compact.txt")
    copied: list[Path] = []
    for source, suffix in ((diff_path, ".txt"), (compact_path, ".compact.txt")):
        if not source.is_file():
            continue
        target = root / "build" / "decomp-cache" / "best" / f"{name}{suffix}"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        copied.append(target)
    return copied


def save_best_patch(
    address: str, root: Path, diff_path: Path | None = None, diff: bytes | None = None
) -> Path | None:
    """Save the source diff (and the attempt's diffs) as the best of a target."""
    if diff_path is not None:
        save_best_diffs(address, diff_path, root)
    diff = source_diff(root) if diff is None else diff
    if diff is None:
        return None
    path = root / "build" / "decomp-cache" / "best" / f"{canonical_address(address)}.patch"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(diff)
    return path


def unchanged_line(last: dict[str, object], count: int) -> str:
    """Describe the last attempt that already measured the current source tree."""
    return (
        f"source unchanged since attempt {last.get('n', count)} "
        f"(score {float(last['raw']):.2f}%); not logged"
    )


def log_attempt(
    address: str, diff_path: Path, root: Path = ROOT, quiet: bool = False
) -> list[str]:
    """Append one attempt for a target under root and return the lines to print."""
    try:
        parsed = parse_diff(diff_path.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        parsed = None
    if parsed is None:
        return [f"warning: no similarity verdict in {diff_path}; attempt not logged"]
    raw, exact, effective = parsed
    rows = read_rows(address, attempts_directory(root))
    diff = source_diff(root)
    tree = source_tree(diff)
    if rows and tree and rows[-1].get("tree") == tree:
        return [unchanged_line(rows[-1], len(rows))]
    raws = [float(row["raw"]) for row in rows]
    previous_best = max(raws, default=None)
    raws.append(raw)
    at = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    row = {
        "n": len(raws), "at": at, "raw": raw, "exact": exact, "effective": effective,
        "tree": tree,
    }
    path = attempts_directory(root) / f"{canonical_address(address)}.jsonl"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(row) + "\n")
    count, best, (limit, stalled) = len(raws), max(raws), budget(raws)
    lines = [] if quiet else [
        f"attempt {count}/{limit}  raw {raw:.2f}% ({raw - (previous_best or 0.0):+.2f})"
        f"  best {best:.2f}% (attempt {raws.index(best) + 1})"
    ]
    if previous_best is None or raw > previous_best:
        patch = save_best_patch(address, root, diff_path, diff)
        if patch is not None and not quiet:
            lines.append(f"best patch: {patch}")
    if stalled:
        lines.append(f"stall: {STALL_ATTEMPTS} attempts without a {STALL_MIN_GAIN_POINTS:g}-point "
                     "gain; stop this batch")
    if count >= limit:
        lines.append(f"budget: {count} attempts used; finish this campaign")
    return lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    subparsers = parser.add_subparsers(dest="command", required=True)
    attempt = subparsers.add_parser("attempt", help="log one comparison of a target")
    attempt.add_argument("--address", required=True, type=canonical_address)
    attempt.add_argument("--diff", required=True, type=Path)
    attempt.add_argument("--quiet", action="store_true")
    clear = subparsers.add_parser("clear", help="forget the attempts of the given targets")
    clear.add_argument("--address", action="append", required=True, type=canonical_address)
    stats = subparsers.add_parser("stats", help="show the attempt counts of a target")
    stats.add_argument("--address", required=True, type=canonical_address)
    stats.add_argument("--json", action="store_true")
    pick = subparsers.add_parser("pick", help="print the next target of candidates JSON")
    pick.add_argument("files", nargs="+", type=Path)
    pick.add_argument("--address", type=canonical_address)
    pick.add_argument("--coverage", action="store_true")
    pick.add_argument("--skip", action="append", default=[], type=canonical_address)
    usage = subparsers.add_parser("writer-usage", help="print the usage fields of a writer run")
    usage.add_argument("file", type=Path)
    usage.add_argument("--result", type=int, metavar="LINES", help="print the result tail")
    usage.add_argument("--failure", type=int, metavar="STATUS", help="say why it did no work")
    usage.add_argument("--handoff", action="store_true", help="print the result as a handoff")
    for name in ("tried", "failures"):
        subparsers.add_parser(name, help=f"print the {name} lines of a file").add_argument(
            "file", type=Path)
    args = parser.parse_args(argv)
    if args.command == "pick":
        rows = [row for path in args.files for row in json.loads(path.read_text() or "[]")]
        row = pick_row(rows, args.address, args.coverage, tuple(args.skip))
        if row is None and args.address is not None:
            row = mapped_row(args.address, args.root)
        if row is None:
            return 1
        fields = (canonical_address(str(row["address"])), row.get("name"), row.get("state"),
                  row.get("size") or 0, row.get("source") or "", subsystem_of(row))
        print("\n".join(str(field) for field in fields))
        return 0
    if args.command == "writer-usage":
        text = args.file.read_text(errors="replace") if args.file.is_file() else ""
        fields = writer_usage(text)
        if args.failure is not None:
            output = writer_failure(fields, args.failure)
        elif args.handoff:
            output = handoff_text(fields)
        elif args.result is not None:
            output = "\n".join(str(fields["result"]).strip().splitlines()[-args.result:])
        else:
            output = "\t".join(f"{value:.4f}" if name == "cost" else str(value)
                               for name, value in fields.items() if name != "result")
        print(output) if output else None
        return 0
    if args.command in ("tried", "failures"):
        text = args.file.read_text(errors="replace") if args.file.is_file() else ""
        lines = tried_models(text) if args.command == "tried" else failure_lines(text)
        print("\n".join(lines)) if lines else None
        return 0
    if args.command == "attempt":
        for line in log_attempt(args.address, args.diff, args.root, args.quiet):
            print(line)
    elif args.command == "clear":
        clear_attempts(args.address, attempts_directory(args.root))
        print(f"Cleared the attempts of {len(args.address)} target(s).")
    else:
        result = read_stats(args.address, attempts_directory(args.root))
        print(json.dumps(result) if args.json else " ".join(f"{k}={v}" for k, v in result.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
