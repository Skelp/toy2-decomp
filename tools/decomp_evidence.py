#!/usr/bin/env python3
"""Bounded evidence bundle for one reconstruction target.

This collects, in one call, the evidence an agent needs before it writes
source: the map entry and its neighbors, the annotation state and owning
translation unit, the Ghidra decompilation, the callers and callees resolved to
map names, the referenced data addresses with their `// GLOBAL:` state, and the
current match percent.

The raw disassembly is the largest payload and is therefore opt-in
(`--disasm`). Ask for it when the decompilation looks wrong: an implausible
signature, odd casts, merged variables, or a suspicious `goto`.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "src"
MAP_PATH = ROOT / "tools" / "Resources" / "functions_map.txt"

sys.path.insert(0, str(ROOT))
from tools import decomp_binary  # noqa: E402
from tools.decomp_annotations import read_source_annotations  # noqa: E402
from tools.decomp_candidates import (  # noqa: E402
    parse_map,
    read_caps,
    read_match_percentages,
)

NEIGHBOR_COUNT = 3
DATA_ADDRESS_RE = re.compile(r"0x(00[0-9a-fA-F]{6})")
# A string pointer often appears as a bare `PUSH 0x5014f4` immediate, without
# the leading zeroes the memory-operand form carries. Accept both widths and let
# the section lookup reject anything unmapped.
IMMEDIATE_ADDRESS_RE = re.compile(r"0x([0-9a-fA-F]{5,8})")


def run_ghidra(arguments: list[str]) -> object | None:
    """Run a `ghidra` subcommand and return its parsed JSON, or None."""

    try:
        result = subprocess.run(
            ["ghidra", *arguments],
            capture_output=True,
            text=True,
            check=False,
            cwd=ROOT,
        )
    except FileNotFoundError:
        return None
    if result.returncode != 0:
        return None
    text = result.stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


def annotation_index() -> tuple[dict[int, tuple[str, str, int]], dict[int, tuple[str, int]]]:
    functions: dict[int, tuple[str, str, int]] = {}
    globals_: dict[int, tuple[str, int]] = {}
    for annotation in read_source_annotations(SOURCE_ROOT):
        address = int(annotation.address, 16)
        if annotation.kind in ("function", "stub"):
            state = "FUNCTION" if annotation.kind == "function" else "STUB"
            functions[address] = (state, annotation.source, annotation.line)
        elif annotation.kind == "global":
            globals_[address] = (annotation.source, annotation.line)
    return functions, globals_


def global_symbol_names() -> dict[int, str]:
    """Map an annotated retail data address to the symbol named just below it."""

    names: dict[int, str] = {}
    identifier = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[|=|;|\))")
    for path in sorted((*SOURCE_ROOT.rglob("*.cpp"), *SOURCE_ROOT.rglob("*.h"))):
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        for index, line in enumerate(lines):
            match = re.search(r"//\s*GLOBAL:\s*TOY2\s+0x([0-9a-fA-F]+)", line)
            if not match:
                continue
            for following in lines[index + 1 : index + 3]:
                found = identifier.search(following)
                if found:
                    names[int(match.group(1), 16)] = found.group(1)
                    break
    return names


def section(title: str) -> None:
    print(f"\n== {title}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect bounded evidence for one target.")
    parser.add_argument("address", help="retail address, e.g. 0x00403640")
    parser.add_argument("--disasm", action="store_true", help="also print the raw disassembly")
    parser.add_argument(
        "--disasm-limit",
        type=int,
        default=120,
        help="maximum disassembly instructions to print (0 = all)",
    )
    args = parser.parse_args()

    try:
        address = int(args.address, 16)
    except ValueError:
        print(f"error: {args.address!r} is not a hexadecimal address", file=sys.stderr)
        return 2

    entries = parse_map()
    addresses = [item[0] for item in entries]
    names = dict(entries)
    functions, annotated_globals = annotation_index()
    global_names = global_symbol_names()
    matches = read_match_percentages()
    caps = read_caps()

    if address not in names:
        print(f"error: 0x{address:08X} is not in {MAP_PATH.relative_to(ROOT)}", file=sys.stderr)
        print("       The map holds every real function start. Check the address.", file=sys.stderr)
        return 1

    index = addresses.index(address)
    following = addresses[index + 1] if index + 1 < len(addresses) else address
    state, source, line = functions.get(address, ("NOT_STARTED", "", 0))
    match = matches.get(address)

    print(f"== target 0x{address:08X}  {names[address]}")
    print(f"state            {state}" + (f"  ({source}:{line})" if source else ""))
    print(f"approximate size {following - address} bytes (gap to the next map address)")
    print(f"current match    {'-' if match is None else f'{match * 100:.2f}%'}")
    if caps.get(address):
        print(f"known cap        {caps[address]}  (see .notes/codegen-caps.md)")

    section("map neighbors")
    low = max(index - NEIGHBOR_COUNT, 0)
    high = min(index + NEIGHBOR_COUNT + 1, len(entries))
    for neighbor, name in entries[low:high]:
        marker = ">" if neighbor == address else " "
        neighbor_state = functions.get(neighbor, ("NOT_STARTED", "", 0))[0]
        print(f"{marker} 0x{neighbor:08X}  {name:<50} {neighbor_state}")

    section("callers (x-refs to this function)")
    callers = run_ghidra(["x-ref", "to", f"0x{address:08X}"])
    if not callers:
        print("none reported by Ghidra (an indirect or table-dispatched call is still possible)")
    else:
        for reference in callers if isinstance(callers, list) else []:
            origin = reference.get("from", "")
            try:
                origin_address = int(origin, 16)
            except (TypeError, ValueError):
                origin_address = None
            owner = "?"
            if origin_address is not None:
                earlier = [item for item in addresses if item <= origin_address]
                if earlier:
                    owner_address = earlier[-1]
                    owner_state = functions.get(owner_address, ("NOT_STARTED", "", 0))[0]
                    owner = f"0x{owner_address:08X} {names[owner_address]} [{owner_state}]"
            print(f"  from 0x{origin:>8}  in {owner}  ({reference.get('ref_type', '')})")

    # The disassembly is fetched here for the callee list and the optional
    # listing. Fetching it costs nothing in the agent's context; only the
    # printed part does.
    listing = run_ghidra(["function", "disasm", f"0x{address:08X}"])
    rows = listing if isinstance(listing, list) else []

    section("callees (CALL and JMP targets in the disassembly)")
    targets: list[int] = []
    for instruction in rows:
        if str(instruction.get("mnemonic", "")).upper() not in ("CALL", "JMP"):
            continue
        for operand in instruction.get("operands", []):
            found = re.fullmatch(r"0x([0-9a-fA-F]+)", str(operand).strip())
            if not found:
                continue
            target_address = int(found.group(1), 16)
            # A local JMP inside this function's own body is control flow.
            if address <= target_address < following:
                continue
            if target_address not in targets:
                targets.append(target_address)
    if not targets:
        print("none found (a leaf, or dispatch is indirect through a vtable or table)")
    for target_address in targets:
        if target_address in names:
            target_state = functions.get(target_address, ("NOT_STARTED", "", 0))[0]
            print(f"  0x{target_address:08X}  {names[target_address]:<46} [{target_state}]")
        else:
            print(f"  0x{target_address:08X}  (not in the map: import thunk or CRT)")

    decompiled = run_ghidra(
        ["decompile", f"0x{address:08X}", "--with-params", "--with-vars"]
    )
    section("decompilation (Ghidra guesses signedness, pointer depth, and `this`)")
    body = ""
    if isinstance(decompiled, list) and decompiled:
        body = str(decompiled[0].get("code", ""))
        print(body.strip() or "(empty)")
    else:
        print("unavailable; is the Ghidra bridge running? try `ghidra status`")

    # The retail build kept its assert and log text. Those literals carry the
    # developers' own field names, so they outrank any invented name. Report
    # them before the plain data addresses.
    operand_text = " ".join(
        " ".join(str(operand) for operand in instruction.get("operands", []))
        for instruction in rows
    )

    if decomp_binary.available():
        candidate_addresses = sorted(
            {int(value, 16) for value in IMMEDIATE_ADDRESS_RE.findall(operand_text)}
        )
        literals = decomp_binary.strings_referenced_by(candidate_addresses)
        # MSVC6 reads a short literal in dword pieces, so the interior offsets
        # look like separate strings. Drop a literal that is only the tail of a
        # longer one reported here.
        literals = [
            literal
            for literal in literals
            if not any(
                other is not literal and other.text.endswith(literal.text)
                for other in literals
            )
        ]
        section("referenced strings (original names outrank invented ones)")
        if not literals:
            print("none")
        for literal in literals:
            print(f"  0x{literal.address:08X}  {literal.text!r}")
            for expression in literal.field_expressions:
                print(f"{'':>14}^ original field name: {expression}")
            if literal.source_file:
                print(f"{'':>14}^ original translation unit: {literal.source_file}")

    section("referenced data addresses")
    referenced = sorted(
        {
            int(value, 16)
            for value in DATA_ADDRESS_RE.findall(operand_text + " " + body)
        }
    )
    referenced = [
        item
        for item in referenced
        # Drop the map's own function starts, this function's call targets, and
        # any address inside this function's own body (a branch target).
        if item not in names and item not in targets and not (address <= item < following)
    ]
    if not referenced:
        print("none found in the decompilation text")
    for data_address in referenced:
        if data_address in annotated_globals:
            owner, owner_line = annotated_globals[data_address]
            symbol = global_names.get(data_address, "")
            print(f"  0x{data_address:08X}  annotated as {symbol or '?'}  ({owner}:{owner_line})")
        else:
            print(f"  0x{data_address:08X}  no // GLOBAL: annotation yet")

    if args.disasm:
        section("disassembly")
        shown = rows[: args.disasm_limit] if args.disasm_limit else rows
        for instruction in shown:
            operands = ", ".join(instruction.get("operands", []))
            print(
                f"  {instruction.get('address', ''):>8}  "
                f"{instruction.get('mnemonic', ''):<8} {operands}"
            )
        if args.disasm_limit and len(rows) > args.disasm_limit:
            print(f"  ... {len(rows) - args.disasm_limit} more (raise --disasm-limit)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
