#!/usr/bin/env python3
"""Summarise a reccmp verbose diff: verdict, region index, compact view."""

from __future__ import annotations

import argparse
import functools
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
# The attempt view bc prints after each attempt. It stays under this many
# characters because a writer pays for every line again on each later call.
VIEW_BUDGET = 8000
VIEW_CHANGED_REGIONS = 2
VIEW_SOURCE_CONTEXT = 3


@dataclass
class Region:
    number: int
    start: int
    end: int  # exclusive
    minus: int = 0
    plus: int = 0
    address: str = ""
    refs: dict[str, list[int]] = field(default_factory=dict)
    # Retail addresses from the line before the region to the line after it.
    low: int | None = None
    high: int | None = None
    # With no reference of its own, the nearest one in the same hunk.
    nearby: dict[str, list[int]] = field(default_factory=dict)

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
            if match:
                region.address = region.address or match.group(1)
                value = int(match.group(1), 16)
                region.low = value if region.low is None else min(region.low, value)
                region.high = value if region.high is None else max(region.high, value)
            for name, number in SOURCE_REF.findall(line):
                region.refs.setdefault(name, []).append(int(number))
        if not region.refs:
            region.nearby = nearest_refs(lines, region)
    return result


def nearest_refs(lines: list[str], region: Region) -> dict[str, list[int]]:
    """Return the reference nearest a region in its hunk: earlier first, else later.

    reccmp marks only the first instruction of a source line, so the closest
    earlier reference names the line that holds the region's instructions.
    """
    earlier = range(region.start - 1, -1, -1)
    later = range(region.end, len(lines))
    for indexes in (earlier, later):
        for index in indexes:
            if HUNK_HEADER.match(lines[index]):
                break
            found = SOURCE_REF.findall(lines[index])
            if found:
                return {name: [int(number)] for name, number in found}
    return {}


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


def overlaps(one: Region, other: Region) -> bool:
    if one.low is None or other.low is None:
        return False
    return one.low <= other.high and other.low <= one.high


def changed_regions(current: list[Region], previous: list[Region]) -> tuple[list[Region], list[str]]:
    """Compare two diffs by retail address range, because region numbers shift
    and a region that shrinks or merges with another gets a new start address.

    A region is unchanged when a previous region over the same retail addresses
    had the same -a/+b counts. A previous region is resolved only when no
    current region covers any of its retail addresses.
    """
    changed = [
        region for region in current
        if not any(
            overlaps(region, old) and (old.minus, old.plus) == (region.minus, region.plus)
            for old in previous
        )
    ]
    resolved = [
        old.address for old in previous
        if old.address and not any(overlaps(old, region) for region in current)
    ]
    return changed, resolved


@functools.lru_cache(maxsize=None)
def source_files(root: Path) -> dict[str, tuple[Path, ...]]:
    """Map each file name under the source root to its paths (one walk per run)."""
    found: dict[str, list[Path]] = {}
    for path in sorted(root.rglob("*")):
        if path.is_file():
            found.setdefault(path.name, []).append(path)
    return {name: tuple(paths) for name, paths in found.items()}


def resolve_source(root: Path, name: str, address: int | None) -> Path | None:
    """Find the file a diff names; two files with one name resolve by the target's annotation."""
    candidates = [
        path for path in source_files(root).get(Path(name).name, ())
        if ("/" + path.relative_to(root).as_posix()).endswith("/" + name)
    ]
    if len(candidates) > 1 and address is not None:
        marker = f"toy2 0x{address:08x}"
        candidates = [
            path for path in candidates
            if marker in path.read_text(encoding="utf-8", errors="replace").lower()
        ]
    return candidates[0] if len(candidates) == 1 else None


def source_text(region: Region, root: Path, address: int | None) -> str:
    """Return the raw source lines of a region's span, widened by a few lines.

    The lines keep their tabs and carry no line numbers, so a writer can copy
    them into an edit anchor without another read.
    """
    output: list[str] = []
    for name, numbers in (region.refs or region.nearby).items():
        path = resolve_source(root, name, address)
        if path is None:
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        first = max(1, min(numbers) - VIEW_SOURCE_CONTEXT)
        last = min(len(lines), max(numbers) + VIEW_SOURCE_CONTEXT)
        if first > last:
            continue
        output.append(f"--- source {path.as_posix()}:{first}-{last} ---")
        output.extend(lines[first - 1:last])
    return "".join(line + "\n" for line in output)


def attempt_view(
    text: str,
    previous: str | None,
    source_root: Path,
    address: int | None = None,
    label: str = "ADDRESS",
    budget: int = VIEW_BUDGET,
) -> str:
    """Show what an attempt changed, so a writer needs no separate --hunk or source read.

    The view names the changed and resolved regions and prints the largest
    changed ones, each as --hunk prints it and followed by its source lines.
    Unchanged regions are not printed again: the pack or an earlier attempt
    already showed them. With no previous diff it prints the largest region
    that fits. A region that does not fit is named in a last line.
    """
    if previous is not None and text == previous:
        return ""  # bc already said "source unchanged"
    lines = text.splitlines()
    found = regions(lines)
    header = ""
    if previous is None:
        candidates, slots = found, 1
    else:
        candidates, resolved = changed_regions(found, regions(previous.splitlines()))
        header = f"changed since the last diff: {len(candidates)} region(s)"
        header += f"; resolved: {', '.join(resolved)}\n" if resolved else "\n"
        slots = VIEW_CHANGED_REGIONS
    candidates = sorted(candidates, key=lambda region: region.minus + region.plus, reverse=True)

    def not_shown(numbers: list[int]) -> str:
        if not numbers:
            return ""
        listed = " ".join(str(number) for number in numbers)
        return f"not shown: regions {listed} (tools/decomp bc {label} --hunk N)\n"

    rendered: dict[int, tuple[str, str]] = {}

    def fill(limit: int) -> tuple[str, list[int]]:
        output, skipped, printed = header, [], 0
        for region in candidates:
            if printed == slots:
                if previous is None:
                    break  # the index lists the smaller regions
                skipped.append(region.number)
                continue
            if region.number not in rendered:
                rendered[region.number] = (
                    region_text(lines, found, region.number),
                    source_text(region, source_root, address),
                )
            hunk, source = rendered[region.number]
            if len(output) + len(hunk) + len(source) <= limit:
                output += hunk + source
            elif len(output) + len(hunk) <= limit:
                output += hunk  # the asm is worth more than its source lines
            else:
                skipped.append(region.number)
                continue
            printed += 1
        return output, skipped

    # Keep room for the last line so the whole view stays within the budget.
    # The room grows on each pass and the line has a maximum length, so the
    # loop ends; a last line that no longer grows means nothing more can go.
    reserve = 0
    while True:
        output, skipped = fill(budget - reserve)
        tail = not_shown(skipped)
        if len(output) + len(tail) <= budget or len(tail) <= reserve:
            return output + tail
        reserve = len(tail)


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
    parser.add_argument(
        "--attempt-view", action="store_true", help="print what changed since --previous"
    )
    parser.add_argument("--previous", type=Path, help="the diff of the last attempt")
    args = parser.parse_args()
    text = args.path.read_text(encoding="utf-8", errors="replace")
    if args.attempt_view:
        previous = None
        if args.previous is not None and args.previous.is_file():
            previous = args.previous.read_text(encoding="utf-8", errors="replace")
        # The saved diff is named by the address the writer typed, and
        # `bc ADDRESS --hunk N` looks it up by that name.
        view = attempt_view(
            text, previous, args.source_root or Path("src"), args.address, args.path.stem
        )
        print(view, end="")
        return 0
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
