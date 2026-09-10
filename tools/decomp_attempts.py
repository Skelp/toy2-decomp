#!/usr/bin/env python3
"""Log reconstruction attempts per target and hint when to stop iterating."""

from __future__ import annotations

import argparse
import json
import re
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
DEFAULT_BUDGET = 12
EXTENDED_BUDGET = 24
STALL_ATTEMPTS = 5
STALL_MIN_GAIN_POINTS = 0.5
EXTEND_GAIN_POINTS = 1.0


def canonical_address(value: str) -> str:
    try:
        return f"0x{int(value, 16):08X}"
    except (TypeError, ValueError) as error:
        raise argparse.ArgumentTypeError("use an address such as 0x00401000") from error


def read_raws(address: str, directory: Path = ATTEMPTS_DIR) -> list[float]:
    """Return the logged similarity percentages of a target in attempt order."""
    path = directory / f"{canonical_address(address)}.jsonl"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    try:
        rows = [json.loads(line) for line in text.splitlines() if line.strip()]
    except json.JSONDecodeError:
        return []
    return [float(row["raw"]) for row in rows if isinstance(row.get("raw"), (int, float))]


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
    }


def clear_attempts(addresses: list[str], directory: Path = ATTEMPTS_DIR) -> None:
    for address in addresses:
        (directory / f"{canonical_address(address)}.jsonl").unlink(missing_ok=True)


def save_best_patch(address: str, root: Path) -> Path | None:
    command = ["git", "diff", "HEAD", "--", "src", "tools/Resources/functions_map.txt"]
    try:
        diff = subprocess.run(command, cwd=root, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    path = root / "build" / "decomp-cache" / "best" / f"{canonical_address(address)}.patch"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(diff.stdout, encoding="utf-8")
    return path


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
    raws = read_raws(address, attempts_directory(root))
    previous_best = max(raws, default=None)
    raws.append(raw)
    at = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    row = {"n": len(raws), "at": at, "raw": raw, "exact": exact, "effective": effective}
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
        patch = save_best_patch(address, root)
        if patch is not None and not quiet:
            lines.append(f"best patch: {patch}")
    if stalled:
        lines.append(f"stall: {STALL_ATTEMPTS} attempts without a {STALL_MIN_GAIN_POINTS:g}-point "
                     "gain; commit the best model or record no-source")
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
