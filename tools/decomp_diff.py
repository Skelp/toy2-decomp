#!/usr/bin/env python3
"""Summarise a reccmp verbose diff: verdict, region index, compact view."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_annotations import read_source_annotations  # noqa: E402

CHANGED_LINE = re.compile(r"^(?:0x[0-9a-f]+)?\s*:\s*[-+]")
ADDRESS_LINE = re.compile(r"^(0x[0-9a-f]+)\s*:")
SOURCE_REF = re.compile(r"\(([A-Za-z0-9_./-]+):(\d+)\)")
HUNK_HEADER = re.compile(r"^@@ ")
REGION_GAP = 2


@dataclass
class Region:
    number: int
    start: int
    end: int  # exclusive
    minus: int = 0
    plus: int = 0
    address: str = ""
    refs: dict[str, list[int]] = field(default_factory=dict)

    def source_span(self) -> str:
        if not self.refs:
            return "-"
        parts = []
        for name, numbers in self.refs.items():
            low, high = min(numbers), max(numbers)
            parts.append(f"{name}:{low}" if low == high else f"{name}:{low}-{high}")
        return ",".join(parts)


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


def regions(lines: list[str]) -> list[Region]:
    """Group changed diff lines into regions separated by unchanged code."""

    result: list[Region] = []
    current: Region | None = None
    gap = 0
    for index, line in enumerate(lines):
        if HUNK_HEADER.match(line):
            current = None
            continue
        if not CHANGED_LINE.match(line):
            gap += 1
            continue
        if current is None or gap > REGION_GAP:
            current = Region(len(result) + 1, index, index + 1)
            result.append(current)
        current.end = index + 1
        gap = 0
        if line.split(":", 1)[1].strip().startswith("-"):
            current.minus += 1
        else:
            current.plus += 1
    for region in result:
        for line in lines[max(0, region.start - 1):region.end + 1]:
            match = ADDRESS_LINE.match(line)
            if match and not region.address:
                region.address = match.group(1)
            for name, number in SOURCE_REF.findall(line):
                region.refs.setdefault(name, []).append(int(number))
    return result


def region_index(found: list[Region]) -> list[str]:
    """Return one row per region; the index is never truncated."""
    return [
        f"{region.number:3d}  {region.address or '-':<10} -{region.minus:<3d}+{region.plus:<3d} "
        f"{region.source_span()}"
        for region in found
    ]


def region_header(region: Region) -> str:
    return (
        f"-- region {region.number}: {region.address or '-'} -{region.minus} +{region.plus} "
        f"{region.source_span()} --"
    )


def compact(lines: list[str], found: list[Region] | None = None) -> str:
    """Return hunk headers, changed lines and one anchor line around each region."""

    found = regions(lines) if found is None else found
    output: list[str] = []
    cursor = 0
    for region in found:
        start, end = max(0, region.start - 1), min(len(lines), region.end + 1)
        output.extend(line for line in lines[cursor:start] if HUNK_HEADER.match(line))
        output.append(region_header(region))
        output.extend(lines[start:end])
        cursor = end
    return "\n".join(output) + "\n"


def region_text(lines: list[str], found: list[Region], number: int, context: int = 3) -> str:
    for region in found:
        if region.number == number:
            start, end = max(0, region.start - context), min(len(lines), region.end + context)
            return "\n".join([region_header(region), *lines[start:end]]) + "\n"
    return f"no region {number}; the index has {len(found)} regions\n"


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


def verdict_line(lines: list[str]) -> str:
    return next(
        (line for line in lines if re.search(r"(?:similar|match|effective|error)", line, re.I)),
        "No similarity verdict found.",
    )


def summarize(
    text: str, path: Path, context: str | None = None, compact_path: Path | None = None
) -> str:
    lines = text.splitlines()
    found = regions(lines)
    changed = sum(region.minus + region.plus for region in found)
    output = [verdict_line(lines), *([context] if context else [])]
    saved = f"Saved: {path}"
    if compact_path is not None:
        saved += f"  compact: {compact_path}"
    output.append(saved)
    output.append(
        f"Regions: {len(found)}; changed lines: {changed} of {len(lines)}"
        "  (regions: `bc ADDRESS --hunk N`)"
    )
    output.extend(region_index(found))
    return "\n".join(output) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--full", action="store_true", help="print the raw diff")
    parser.add_argument("--compact", action="store_true", help="print changed lines only")
    parser.add_argument("--hunks", action="store_true", help="print the region index only")
    parser.add_argument("--hunk", type=int, help="print one region with context")
    parser.add_argument("--write-compact", type=Path, help="also write the compact diff here")
    parser.add_argument("--address", type=lambda value: int(value, 16))
    parser.add_argument("--functions-map", type=Path)
    parser.add_argument("--function-sizes", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    text = args.path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    found = regions(lines)
    if args.write_compact is not None:
        args.write_compact.write_text(compact(lines, found), encoding="utf-8")
    if args.hunk is not None:
        print(region_text(lines, found, args.hunk), end="")
        return 0
    if args.hunks:
        print("\n".join([verdict_line(lines), *region_index(found)]))
        return 0
    if args.compact:
        print(compact(lines, found), end="")
        return 0
    context = None
    if (
        args.address is not None
        and args.functions_map
        and args.function_sizes
        and args.source_root
    ):
        context = comparison_score_context(
            text, args.address, args.functions_map, args.function_sizes, args.source_root
        )
    if args.full:
        output = text.rstrip("\n") + "\n" + (context + "\n" if context else "")
    else:
        output = summarize(text, args.path, context, args.write_compact)
    print(output, end="" if output.endswith("\n") else "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
