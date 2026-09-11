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
# While changed regions print, a second one must leave up to this many
# characters for the largest region the writer has not seen.
VIEW_UNSEEN_RESERVE = 3000
# References more than this many lines apart print as separate windows, because
# one stray reference (an inlined helper, say) would otherwise stretch a window
# over hundreds of lines. Most gaps inside a region are 7 lines or fewer.
SOURCE_GAP = 12
# --hunk has no budget, so it prints at most this many source lines per region.
HUNK_SOURCE_LINES = 60
# The pack prints at most PACK_REGIONS of the largest regions. The pack and the
# pick of an unseen region try only this many; the index names the others.
PACK_REGIONS = 4
LARGEST_REGIONS = 8


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


def region_size(region: Region) -> int:
    return region.minus + region.plus


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


def source_windows(
    region: Region, root: Path, address: int | None
) -> list[tuple[Path, list[str], int, int, int]]:
    """Return (path, file lines, first, last, references) for each window of a region."""
    windows = []
    for name, numbers in (region.refs or region.nearby).items():
        path = resolve_source(root, name, address)
        if path is None:
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        clusters: list[list[int]] = []
        for number in sorted(numbers):
            if clusters and number - clusters[-1][-1] <= SOURCE_GAP:
                clusters[-1].append(number)
            else:
                clusters.append([number])
        for cluster in clusters:
            first = max(1, cluster[0] - VIEW_SOURCE_CONTEXT)
            last = min(len(lines), cluster[-1] + VIEW_SOURCE_CONTEXT)
            if first <= last:
                windows.append((path, lines, first, last, len(cluster)))
    return windows


def source_text(
    region: Region,
    root: Path,
    address: int | None,
    cap: int | None = None,
    printed: dict[Path, set[int]] | None = None,
) -> str:
    """Return the raw source lines around a region's references, a few lines wider.

    The lines keep their tabs and carry no line numbers, so a writer can copy
    them into an edit anchor without another read. With a cap, the windows
    with the most references get their lines first and one line names each
    part that did not print. printed holds the lines of each file that this
    call already printed; they do not print again.
    """
    parts = []
    for path, lines, first, last, weight in source_windows(region, root, address):
        done = set() if printed is None else printed.setdefault(path, set())
        start = None
        for number in range(first, last + 2):
            if number <= last and number not in done:
                start = number if start is None else start
            elif start is not None:
                parts.append((path, lines, start, number - 1, weight))
                start = None
    counts = [last - first + 1 for _, _, first, last, _ in parts]
    if cap is not None:
        room = cap
        for index in sorted(range(len(parts)), key=lambda index: -parts[index][4]):
            counts[index] = min(counts[index], room)
            room -= counts[index]
    output: list[str] = []
    for (path, lines, first, last, _), count in zip(parts, counts):
        end = first + count - 1
        if count:
            output.append(f"--- source {path.as_posix()}:{first}-{end} ---")
            output.extend(lines[first - 1:end])
            if printed is not None:
                printed[path].update(range(first, end + 1))
        if end < last:
            output.append(
                f"--- {last - end} more lines: sed -n {end + 1},{last}p {path.as_posix()} ---"
            )
    return "".join(line + "\n" for line in output)


def read_seen(path: Path | None) -> list[tuple[int, int]]:
    """Return the retail ranges of the regions a writer was shown in this batch."""
    if path is None or not path.is_file():
        return []
    seen = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            low, high = (int(value, 16) for value in line.split())
        except ValueError:
            continue
        seen.append((low, high))
    return seen


def mark_seen(path: Path | None, shown: list[Region], fresh: bool = False) -> None:
    """Add the ranges of the shown regions to the seen set; fresh starts a new set."""
    if path is None:
        return
    rows = "".join(f"0x{r.low:x} 0x{r.high:x}\n" for r in shown if r.low is not None)
    with path.open("w" if fresh else "a", encoding="utf-8") as handle:
        handle.write(rows)


def is_seen(region: Region, seen: list[tuple[int, int]]) -> bool:
    """Region numbers change between diffs, so a region is seen when its retail
    range overlaps the range of a region shown earlier."""
    if region.low is None or region.high is None:
        return False
    return any(region.low <= high and low <= region.high for low, high in seen)


TARGET_SOURCE_LIMIT = 30000


def target_source(source_root: Path, address: int, limit: int = TARGET_SOURCE_LIMIT) -> str:
    """Return the target function's whole current source, from its annotation to the next
    annotation, when it fits the limit. A writer reads it once from the pack instead of
    paging it with sed between attempts; the pack regions then need no source lines."""
    marker = re.compile(rf"// (?:FUNCTION|STUB): TOY2 0x{address:08X}\b", re.I)
    annotation = re.compile(r"// (?:FUNCTION|STUB|GLOBAL|LIBRARY): TOY2 ")
    for path in sorted(source_root.rglob("*.cpp")):
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        first = next((n for n, line in enumerate(lines, 1) if marker.search(line)), 0)
        if not first:
            continue
        last = next((n - 1 for n in range(first + 1, len(lines) + 1)
                     if annotation.search(lines[n - 1])), len(lines))
        while last > first and not lines[last - 1].strip():
            last -= 1
        body = "\n".join(lines[first - 1:last])
        if len(body) > limit:
            return ""
        return f"--- target source {path.as_posix()}:{first}-{last} (current tree) ---\n{body}\n"
    return ""


def pack_regions(
    text: str, source_root: Path, address: int | None, label: str, budget: int,
    with_source: bool = True,
) -> tuple[str, list[Region]]:
    """Return the pack's largest regions with their source lines, and the regions shown.

    The regions go in, largest first, while the text stays within the budget.
    The other large regions are named in a last line.
    """
    lines = text.splitlines()
    found = regions(lines)
    output, shown, skipped = "", [], []
    for region in sorted(found, key=region_size, reverse=True)[:LARGEST_REGIONS]:
        part = region_text(lines, found, region.number) + (
            source_text(region, source_root, address) if with_source else ""
        )
        if len(shown) >= PACK_REGIONS or len(output) + len(part) > budget:
            skipped.append(str(region.number))
            continue
        output += part
        shown.append(region)
    if skipped:
        output += (
            f"--- not included (tools/decomp bc {label} --hunk N): regions {' '.join(skipped)} ---\n"
        )
    return output, shown


def attempt_view(
    text: str,
    previous: str | None,
    source_root: Path,
    address: int | None = None,
    label: str = "ADDRESS",
    budget: int = VIEW_BUDGET,
    seen: list[tuple[int, int]] = (),
) -> tuple[str, list[Region]]:
    """Show what an attempt changed, so a writer needs no separate --hunk or source read.

    The view names the changed and resolved regions and prints the largest
    changed ones, each as --hunk prints it and followed by its source lines.
    Then, if room, it prints the largest region the writer has not seen in
    this batch (seen holds the retail ranges whose source the pack, a view or
    --hunk printed), because a writer otherwise fetches the next large region
    from the index. With no previous diff only that region is printed. To keep
    room for it, a second changed region must leave VIEW_UNSEEN_RESERVE
    characters, and the source of a changed region the writer saw before goes
    in last: the writer has just edited those lines. A last line names the
    regions that do not fit. Returns the view and the regions whose source it
    printed.
    """
    if previous is not None and text == previous:
        return "", []  # bc already said "source unchanged"
    lines = text.splitlines()
    found = regions(lines)
    header, label_line, changed = "", "", []
    if previous is not None:
        changed, resolved = changed_regions(found, regions(previous.splitlines()))
        header = f"changed since the last diff: {len(changed)} region(s)"
        header += f"; resolved: {', '.join(resolved)}\n" if resolved else "\n"
        label_line = "not seen yet:\n"
    changed = sorted(changed, key=region_size, reverse=True)
    numbers = {region.number for region in changed}
    unseen = [
        region for region in sorted(found, key=region_size, reverse=True)
        if region.number not in numbers and not is_seen(region, seen)
    ][:LARGEST_REGIONS]
    hunks: dict[int, str] = {}
    sources: dict[int, str] = {}

    def hunk(region: Region) -> str:
        if region.number not in hunks:
            hunks[region.number] = region_text(lines, found, region.number)
        return hunks[region.number]

    def source(region: Region) -> str:
        if region.number not in sources:
            sources[region.number] = source_text(region, source_root, address)
        return sources[region.number]

    def not_shown(skipped: list[int], missed: list[int]) -> str:
        parts = [
            f"{name} {' '.join(str(number) for number in group)}"
            for name, group in (("changed", skipped), ("not seen", missed)) if group
        ]
        if not parts:
            return ""
        return f"not shown: {'; '.join(parts)} (tools/decomp bc {label} --hunk N)\n"

    def fill(limit: int) -> tuple[str, list[int], list[int], list[Region]]:
        cost, slots, skipped, sourced = len(header), [], [], set()
        reserve = 0
        if unseen:
            first = unseen[0]
            reserve = min(VIEW_UNSEEN_RESERVE, len(label_line + hunk(first) + source(first)))

        def add_source(region: Region, room: int) -> None:
            nonlocal cost
            if region.number not in sourced and cost + len(source(region)) <= room:
                sourced.add(region.number)
                cost += len(source(region))

        for region in changed:
            room = limit - reserve if slots else limit
            if len(slots) < VIEW_CHANGED_REGIONS and cost + len(hunk(region)) <= room:
                slots.append(region)
                cost += len(hunk(region))
            else:
                skipped.append(region.number)
        for region in slots:
            if not is_seen(region, seen):
                add_source(region, limit - reserve)
        # The unseen region prints with its source; its asm alone only when
        # no candidate fits with its source.
        extra = None
        for with_source in (True, False):
            for region in unseen:
                size = len(label_line + hunk(region))
                size += len(source(region)) if with_source else 0
                if cost + size <= limit:
                    extra, cost = region, cost + size
                    if with_source:
                        sourced.add(region.number)
                    break
            if extra is not None:
                break
        for region in slots:
            add_source(region, limit)
        printed = slots + ([extra] if extra is not None else [])
        output = header + "".join(
            (label_line if region is extra else "")
            + hunk(region)
            + (source(region) if region.number in sourced else "")
            for region in printed
        )
        missed = unseen[:unseen.index(extra)] if extra is not None else unseen
        shown = [region for region in printed if region.number in sourced or not source(region)]
        return output, skipped, [region.number for region in missed], shown

    # Keep room for the last line so the whole view stays within the budget.
    # The room grows on each pass and the line has a maximum length, so the
    # loop ends; a last line that no longer grows means nothing more can go.
    reserve = 0
    while True:
        output, skipped, missed, shown = fill(budget - reserve)
        tail = not_shown(skipped, missed)
        if len(output) + len(tail) <= budget or len(tail) <= reserve:
            return output + tail, shown
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


def region_numbers(value: str) -> list[int]:
    return [int(number) for number in value.split(",")]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--full", action="store_true", help="print the raw diff")
    parser.add_argument("--compact", action="store_true", help="print changed lines only")
    parser.add_argument("--hunks", action="store_true", help="print the region index only")
    parser.add_argument(
        "--hunk", type=region_numbers, help="print regions N[,N...] with context and source"
    )
    parser.add_argument("--write-compact", type=Path, help="also write the compact diff here")
    parser.add_argument("--address", type=lambda value: int(value, 16))
    parser.add_argument("--functions-map", type=Path)
    parser.add_argument("--function-sizes", type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument(
        "--attempt-view", action="store_true", help="print what changed since --previous"
    )
    parser.add_argument("--previous", type=Path, help="the diff of the last attempt")
    parser.add_argument(
        "--pack-regions", type=int, metavar="BUDGET",
        help="print the largest regions within BUDGET characters; starts a new --seen set",
    )
    parser.add_argument("--seen", type=Path, help="the regions a writer was shown in this batch")
    parser.add_argument("--target-source", action="store_true",
                        help="print the target function's whole source (needs --address)")
    parser.add_argument("--no-region-source", action="store_true",
                        help="pack regions without source lines (the pack holds the target source)")
    args = parser.parse_args()
    text = args.path.read_text(encoding="utf-8", errors="replace")
    # The saved diff is named by the address the writer typed, and
    # `bc ADDRESS --hunk N` looks it up by that name.
    label = args.path.stem
    if args.target_source:
        if args.address is not None:
            print(target_source(args.source_root or Path("src"), args.address), end="")
        return 0
    if args.attempt_view:
        previous = None
        if args.previous is not None and args.previous.is_file():
            previous = args.previous.read_text(encoding="utf-8", errors="replace")
        view, shown = attempt_view(
            text, previous, args.source_root or Path("src"), args.address, label,
            seen=read_seen(args.seen),
        )
        print(view, end="")
        mark_seen(args.seen, shown)
        return 0
    if args.pack_regions is not None:
        view, shown = pack_regions(
            text, args.source_root or Path("src"), args.address, label, args.pack_regions,
            with_source=not args.no_region_source,
        )
        print(view, end="")
        mark_seen(args.seen, shown, fresh=True)  # a new batch starts with the pack
        return 0
    lines = text.splitlines()
    found = regions(lines)
    if args.write_compact is not None:
        args.write_compact.write_text(compact(lines, found), encoding="utf-8")
    if args.hunk is not None:
        # A source line prints once per call, so neighbouring regions do not
        # repeat the lines they share.
        shown, printed = [], {}
        for number in args.hunk:
            print(region_text(lines, found, number), end="")
            region = next((region for region in found if region.number == number), None)
            if region is None:
                continue
            shown.append(region)
            if args.source_root is not None:
                print(
                    source_text(region, args.source_root, args.address, HUNK_SOURCE_LINES, printed),
                    end="",
                )
        mark_seen(args.seen, shown)
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
