#!/usr/bin/env python3
"""Extract original developer names from the retail executable's own text.

The retail build shipped its assert and log strings. Those strings quote the
expressions the developers wrote, so they hand over names that no amount of
disassembly can recover:

- `drawb->VerticeCount[i]` gives a structure pointer name, a field name, and
  the index variable.
- `d3dappi.lpFrontBuffer` gives a global's name and one of its members.
- `C:\\projects\\nu3d\\objload.c` with a line number gives the original
  translation unit and a position inside it.

This writes a Markdown report for `.notes/original-names.md`. Run it once and
commit the result. It reads only `original/toy2.exe`, the function map, and the
Ghidra cross-references, and it changes nothing.
"""

from __future__ import annotations

import argparse
import bisect
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import decomp_binary  # noqa: E402
from tools.decomp_candidates import parse_map  # noqa: E402

# `base->member`, the strongest form: it names a pointer and one of its fields.
ARROW_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*->\s*([A-Za-z_][A-Za-z0-9_]*)")
# `base.member`, which names a struct-valued global and a member.
DOT_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]{2,})\.([A-Za-z_][A-Za-z0-9_]*)\b")
# A file extension is not a struct member.
NOT_MEMBERS = {
    "c", "cpp", "cxx", "h", "hpp", "exe", "dll", "dat", "bmp", "raw", "txt",
    "ngn", "wav", "tga", "ini", "avi", "pal", "spt", "obj", "lib", "log",
}
# An indexed access proves the member is an array.
INDEXED_RE = re.compile(r"->\s*([A-Za-z_][A-Za-z0-9_]*)\s*\[")


def ghidra_references(address: int) -> list[int]:
    result = subprocess.run(
        ["ghidra", "x-ref", "to", f"0x{address:08X}"],
        capture_output=True,
        text=True,
        check=False,
        cwd=ROOT,
    )
    if result.returncode != 0:
        return []
    try:
        payload = json.loads(result.stdout or "[]")
    except json.JSONDecodeError:
        return []
    found = []
    for entry in payload:
        try:
            found.append(int(entry["from"], 16))
        except (KeyError, TypeError, ValueError):
            continue
    return found


def owning_function(address: int, starts: list[int], names: dict[int, str]) -> str | None:
    index = bisect.bisect_right(starts, address) - 1
    return names[starts[index]] if index >= 0 else None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract original names from retail string literals."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / ".notes" / "original-names.md",
        help="report path (default: .notes/original-names.md)",
    )
    parser.add_argument(
        "--no-xrefs",
        action="store_true",
        help="skip the Ghidra cross-references for the translation-unit table",
    )
    args = parser.parse_args()

    if not decomp_binary.available():
        print(f"error: {decomp_binary.EXE_PATH} not found", file=sys.stderr)
        return 2

    entries = parse_map()
    starts = [address for address, _ in entries]
    names = dict(entries)

    literals = list(decomp_binary.iter_all_strings())

    arrow: dict[str, dict[str, bool]] = defaultdict(dict)
    dotted: dict[str, set[str]] = defaultdict(set)
    quoted: list[decomp_binary.StringLiteral] = []

    for literal in literals:
        text = literal.text
        indexed = set(INDEXED_RE.findall(text))
        matched = False
        for base, member in ARROW_RE.findall(text):
            arrow[base][member] = arrow[base].get(member, False) or member in indexed
            matched = True
        # A dotted pair is only a struct access when the string is code. In
        # prose or a path it is a filename ('game0.sav') or a sentence end.
        looks_like_code = "->" in text or "(" in text
        for base, member in DOT_RE.findall(text):
            if member.lower() in NOT_MEMBERS or not looks_like_code:
                continue
            if "\\" in text or "/" in text:
                continue
            dotted[base].add(member)
            matched = True
        if matched:
            quoted.append(literal)

    # Translation units, with the functions that reference each path string.
    units: dict[str, dict[str, set[int]]] = defaultdict(lambda: defaultdict(set))
    line_numbers: dict[str, set[int]] = defaultdict(set)
    for literal in literals:
        unit = literal.source_file
        if not unit:
            continue
        if args.no_xrefs:
            units[unit]  # touch so the unit still appears
            continue
        for source in ghidra_references(literal.address):
            owner = owning_function(source, starts, names)
            if owner:
                units[unit][owner].add(source)

    # A `GetErrorHandler(file, line)` site gives the line number as the pushed
    # immediate right before the path. Read them from the committed source,
    # where the pairs are already reconstructed.
    for path in sorted((ROOT / "src").rglob("*.cpp")):
        for base, line in re.findall(
            r'"[A-Za-z]:\\\\projects\\\\[a-z0-9]+\\\\([A-Za-z0-9_]+\.(?:c|cpp))"\s*,\s*(\d+)',
            path.read_text(encoding="utf-8", errors="ignore"),
        ):
            line_numbers[base].add(int(line))

    report: list[str] = []
    add = report.append

    add("# Original developer names recovered from the retail binary\n")
    add(
        "Generated by `tools/decomp_original_names.py`. Do not edit by hand;\n"
        "re-run the tool instead.\n"
    )
    add(
        "\nThe retail build kept its assert and log text. That text quotes the\n"
        "expressions the original developers wrote, so it hands over real names.\n"
        "**A name in this file outranks any name you would invent.** When a\n"
        "structure appears here, declare the structure and use these field names\n"
        "rather than raw byte offsets.\n"
    )

    add("\n## Structures named by `base->member` expressions\n")
    add(
        "\n`[]` marks a member the binary shows indexed, which proves it is an\n"
        "array. A COM interface pointer (`lpDD`, `pDev`) is listed too, because\n"
        "the member list confirms which interface the variable holds.\n"
    )
    for base in sorted(arrow, key=lambda key: (-len(arrow[key]), key)):
        members = arrow[base]
        rendered = ", ".join(
            f"`{member}{'[]' if members[member] else ''}`" for member in sorted(members)
        )
        add(f"\n- **`{base}`** ({len(members)}): {rendered}")

    add("\n\n## Globals named by `base.member` expressions\n")
    for base in sorted(dotted, key=lambda key: (-len(dotted[key]), key)):
        rendered = ", ".join(f"`{member}`" for member in sorted(dotted[base]))
        add(f"\n- **`{base}`** ({len(dotted[base])}): {rendered}")

    add("\n\n## Original translation units\n")
    add(
        "\nEach path string is referenced by the `Logger::GetErrorHandler(file,\n"
        "line)` call sites inside one original translation unit. The functions\n"
        "below therefore **belong to that unit**. Use this table when you choose\n"
        "a destination file: it is evidence, not a judgment call.\n"
    )
    if args.no_xrefs:
        add("\n(Cross-references skipped. Re-run without `--no-xrefs`.)\n")
    for unit in sorted(units):
        seen_lines = sorted(line_numbers.get(unit, ()))
        span = f" — known lines {seen_lines[0]}..{seen_lines[-1]}" if seen_lines else ""
        add(f"\n### `{unit}`{span}\n")
        owners = units[unit]
        if not owners:
            add("\n- no cross-reference resolved to a mapped function\n")
        for owner in sorted(owners):
            sites = ", ".join(f"0x{site:08X}" for site in sorted(owners[owner]))
            add(f"\n- `{owner}` (from {sites})")
        add("\n")

    add("\n## Every quoted expression, verbatim\n")
    add(
        "\nKeep these strings exactly as the binary holds them. They are also the\n"
        "argument to the retail logger, so they must stay byte-identical.\n"
    )
    for literal in quoted:
        add(f"\n- `0x{literal.address:08X}` `{literal.text}`")
    add("\n")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(report), encoding="utf-8")

    print(f"wrote {args.output}")
    print(f"  {len(arrow)} arrow-named structures, {len(dotted)} dot-named globals")
    print(f"  {len(units)} translation units, {len(quoted)} quoted expressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
