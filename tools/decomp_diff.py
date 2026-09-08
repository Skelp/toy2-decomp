#!/usr/bin/env python3
"""Print a concise reccmp verbose diff while preserving the complete output."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_annotations import read_source_annotations  # noqa: E402


def mismatch_windows(lines: list[str], maximum: int = 3, context: int = 3) -> list[list[str]]:
    markers = [
        index for index, line in enumerate(lines)
        if re.search(r"(?:^|\s)(?:[-+!<>]|replace|insert|delete|mismatch)", line, re.I)
    ]
    windows: list[tuple[int, int]] = []
    for index in markers:
        start, end = max(0, index - context), min(len(lines), index + context + 1)
        if windows and start <= windows[-1][1]:
            windows[-1] = (windows[-1][0], max(windows[-1][1], end))
        elif len(windows) < maximum:
            windows.append((start, end))
    return [lines[start:end] for start, end in windows[:maximum]]


def read_score_ceiling(
    address: int, functions_map: Path, function_sizes: Path
) -> float | None:
    """Return the analyzed retail-body size divided by the function-map gap."""

    mapped: list[int] = []
    if functions_map.exists():
        for raw in functions_map.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            try:
                mapped.append(int(line.split(None, 1)[0], 16))
            except ValueError:
                continue
    mapped = sorted(set(mapped))
    try:
        index = mapped.index(address)
    except ValueError:
        return None
    if index + 1 >= len(mapped):
        return None
    map_size = mapped[index + 1] - address
    if map_size <= 0 or not function_sizes.exists():
        return None
    try:
        rows = json.loads(function_sizes.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(rows, list):
        return None
    for row in rows:
        if not isinstance(row, dict):
            continue
        raw_address = row.get("address") or row.get("entry_point")
        try:
            row_address = (
                raw_address if isinstance(raw_address, int) else int(str(raw_address), 16)
            )
        except (TypeError, ValueError):
            continue
        size = row.get("original_size", row.get("size"))
        if row_address == address and isinstance(size, int) and size > 1:
            return min(size / map_size, 1.0)
    return None


def is_noncoverage_target(address: int, source_root: Path) -> bool:
    canonical = f"0x{address:x}"
    return any(
        item.address == canonical and item.kind in ("function", "library")
        for item in read_source_annotations(source_root)
    )


def read_similarity(text: str) -> float | None:
    matches = re.findall(
        r"(\d+(?:\.\d+)?)%\s+(?:similar|(?:effective\s+)?match)", text, re.IGNORECASE
    )
    return float(matches[-1]) / 100.0 if matches else None


def score_context(text: str, ceiling: float | None) -> str:
    if ceiling is None:
        return "Score ceiling: unavailable. Ceiling-relative score: unavailable."
    score = read_similarity(text)
    relative = "unavailable" if score is None else f"{score / ceiling * 100:.2f}%"
    return f"Score ceiling: {ceiling * 100:.2f}%. Ceiling-relative score: {relative}."


def comparison_score_context(
    text: str,
    address: int,
    functions_map: Path,
    function_sizes: Path,
    source_root: Path,
) -> str:
    if is_noncoverage_target(address, source_root):
        return "Score ceiling: not applicable. Ceiling-relative score: not applicable."
    return score_context(
        text, read_score_ceiling(address, functions_map, function_sizes)
    )


def summarize(text: str, path: Path, context: str | None = None) -> str:
    lines = text.splitlines()
    verdict = next((line for line in lines if re.search(r"(?:similar|match|effective|error)", line, re.I)), "No similarity verdict found.")
    change_indexes = [
        index for index, line in enumerate(lines)
        if re.search(r"(?:^|\s)(?:[-+!<>]|replace|insert|delete|mismatch)", line, re.I)
    ]
    hunk_count = sum(
        1 for offset, index in enumerate(change_indexes)
        if offset == 0 or index > change_indexes[offset - 1] + 1
    )
    windows = mismatch_windows(lines)
    output = [
        verdict,
        *([context] if context else []),
        f"Saved complete diff: {path}",
        f"Mismatch hunks: {hunk_count}; change lines: {len(change_indexes)}; displayed windows: {len(windows)}",
    ]
    for number, window in enumerate(windows, 1):
        output.extend((f"\n--- mismatch {number} ---", *window))
    return "\n".join(output[:160]) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--address", type=lambda value: int(value, 16))
    parser.add_argument("--functions-map", type=Path)
    parser.add_argument("--function-sizes", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    text = args.path.read_text(encoding="utf-8", errors="replace")
    context = None
    if (
        args.address is not None
        and args.functions_map
        and args.function_sizes
        and args.source_root
    ):
        context = comparison_score_context(
            text,
            args.address,
            args.functions_map,
            args.function_sizes,
            args.source_root,
        )
    output = text if args.full else summarize(text, args.path, context)
    if args.full and context:
        output = output.rstrip("\n") + "\n" + context + "\n"
    print(output, end="" if output.endswith("\n") else "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
