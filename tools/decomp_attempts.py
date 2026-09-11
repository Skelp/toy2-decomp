#!/usr/bin/env python3
"""Log reconstruction attempts per target and hint when to stop iterating."""

from __future__ import annotations

import argparse
import hashlib
import json
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
EXTENDED_BUDGET = 24
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


def budget(raws: list[float]) -> tuple[int, bool]:
    """Return (attempt budget, stalled) from the gains of every attempt after the first."""
    gains = [raw - max(raws[:index]) for index, raw in enumerate(raws) if index]
    extended = any(gain >= EXTEND_GAIN_POINTS for gain in gains[-4:])
    recent = gains[-STALL_ATTEMPTS:]
    stalled = len(recent) >= STALL_ATTEMPTS and all(g < STALL_MIN_GAIN_POINTS for g in recent)
    return (EXTENDED_BUDGET if extended else DEFAULT_BUDGET), stalled


def read_stats(address: str, directory: Path = ATTEMPTS_DIR) -> dict[str, object]:
    raws = read_raws(address, directory)
    best = max(raws, default=None)
    return {
        "attempts": len(raws), "best_attempt": raws.index(best) + 1 if raws else None,
        "best_raw": best, "last_raw": raws[-1] if raws else None,
        "stalled": budget(raws)[1],
    }


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
    args = parser.parse_args(argv)
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
