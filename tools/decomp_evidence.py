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
import bisect
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "src"
MAP_PATH = ROOT / "tools" / "Resources" / "functions_map.txt"

sys.path.insert(0, str(ROOT))
from tools import decomp_binary  # noqa: E402
from tools.decomp_annotations import read_source_annotations  # noqa: E402
from tools.decomp_candidates import (  # noqa: E402
    add_dependency_evidence,
    build_candidates,
    parse_map,
    read_match_percentages,
    read_original_sizes,
)
from tools.decomp_dependencies import (  # noqa: E402
    DependencyUnavailable,
    build_call_graph,
)
from tools.decomp_discover import scan_annotated_relocation_targets  # noqa: E402

NEIGHBOR_COUNT = 3
ROW_LIMIT = 12
DECOMP_HEAD = 80
DECOMP_TAIL = 40
DATA_ADDRESS_RE = re.compile(r"0x(00[0-9a-fA-F]{6})")
# A string pointer often appears as a bare `PUSH 0x5014f4` immediate, without
# the leading zeroes the memory-operand form carries. Accept both widths and let
# the section lookup reject anything unmapped.
IMMEDIATE_ADDRESS_RE = re.compile(r"0x([0-9a-fA-F]{5,8})")


@dataclass(frozen=True)
class UnmappedStartEvidence:
    ghidra_size: int | None
    ghidra_name: str
    relocation_references: tuple[int, ...]


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


def bounded(items: list, limit: int, command: str) -> list:
    """Return a display slice and report how to retrieve omitted rows."""

    shown = items if limit == 0 else items[:limit]
    if limit and len(items) > limit:
        print(f"  ... {len(items) - limit} omitted. Run `{command}` for all rows.")
    return shown


def decomp_lines(body: str, full: bool, line_range: tuple[int, int] | None) -> list[str]:
    lines = body.strip().splitlines()
    if line_range:
        start, end = line_range
        return lines[start - 1 : end]
    if full or len(lines) <= DECOMP_HEAD + DECOMP_TAIL:
        return lines
    omitted = len(lines) - DECOMP_HEAD - DECOMP_TAIL
    return [*lines[:DECOMP_HEAD], f"... {omitted} lines omitted ...", *lines[-DECOMP_TAIL:]]


def parse_range(value: str) -> tuple[int, int]:
    try:
        start_text, end_text = value.split(":", 1)
        start, end = int(start_text), int(end_text)
    except (ValueError, TypeError):
        raise argparse.ArgumentTypeError("use START:END with positive line numbers")
    if start < 1 or end < start:
        raise argparse.ArgumentTypeError("use START:END with positive line numbers")
    return start, end


def ghidra_function_containing(address: int) -> tuple[int, int, str] | None:
    """Return the containing Ghidra function start, size, and local name."""
    payload = run_ghidra(["function", "get", f"0x{address:08X}"])
    rows = payload if isinstance(payload, list) else [payload]
    for row in rows:
        if not isinstance(row, dict):
            continue
        raw_address = row.get("address", row.get("entry_point"))
        try:
            entry = int(str(raw_address), 16)
            size = int(row.get("size", 0))
        except (TypeError, ValueError):
            continue
        if size > 0 and entry <= address < entry + size:
            return entry, size, str(row.get("name") or "")
    return None


def ghidra_function_at(address: int) -> tuple[int, str] | None:
    """Return the exact Ghidra function size and its local name."""

    result = ghidra_function_containing(address)
    if result is None or result[0] != address:
        return None
    return result[1], result[2]


def resolve_unmapped_start(
    address: int,
    ghidra_target: tuple[int, str] | None,
    relocation_references: dict[int, frozenset[int]],
) -> UnmappedStartEvidence | None:
    """Accept an exact Ghidra start or an annotated-global relocation target."""

    references = tuple(sorted(relocation_references.get(address, ())))
    if ghidra_target is not None:
        return UnmappedStartEvidence(
            ghidra_target[0], ghidra_target[1], references
        )
    if not references:
        return None
    return UnmappedStartEvidence(None, "", references)


def resolve_padding_alignment(
    address: int,
    mapped_addresses: list[int],
    read_bytes: Callable[[int, int], bytes | None],
) -> int | None:
    """Resolve an arithmetic body end through retail alignment padding."""

    index = bisect.bisect_right(mapped_addresses, address)
    if index >= len(mapped_addresses):
        return None
    following = mapped_addresses[index]
    padding_size = following - address
    if padding_size <= 0 or padding_size > 15:
        return None
    padding = read_bytes(address, padding_size)
    if padding is None or len(padding) != padding_size:
        return None
    if not all(value in (0x90, 0xCC) for value in padding):
        return None
    return following


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect bounded evidence for one target.")
    parser.add_argument("address", help="retail address, e.g. 0x00403640")
    parser.add_argument(
        "--unmapped",
        action="store_true",
        help="inspect a discovered function start before it enters the map",
    )
    parser.add_argument("--disasm", action="store_true", help="also print the raw disassembly")
    parser.add_argument("--full", action="store_true", help="print all evidence rows and text")
    parser.add_argument(
        "--decomp-range", type=parse_range, metavar="START:END",
        help="print only this inclusive decompilation line range",
    )
    parser.add_argument(
        "--disasm-limit",
        type=int,
        default=80,
        help="maximum disassembly instructions to print (0 = all)",
    )
    args = parser.parse_args()
    row_limit = 0 if args.full else ROW_LIMIT
    if args.full:
        args.disasm_limit = 0

    try:
        address = int(args.address, 16)
    except ValueError:
        print(f"error: {args.address!r} is not a hexadecimal address", file=sys.stderr)
        return 2

    entries = parse_map()
    addresses = [item[0] for item in entries]
    names = dict(entries)
    alignment_origin: int | None = None
    if args.unmapped and address not in names:
        aligned = resolve_padding_alignment(
            address, addresses, decomp_binary.read_bytes
        )
        if aligned is not None:
            alignment_origin = address
            address = aligned
    functions, annotated_globals = annotation_index()
    global_names = global_symbol_names()
    matches = read_match_percentages()
    original_sizes = read_original_sizes()

    unmapped = address not in names
    ghidra_target = ghidra_function_at(address) if unmapped and args.unmapped else None
    relocation_targets: dict[int, frozenset[int]] = {}
    if unmapped and args.unmapped:
        try:
            relocation_targets = scan_annotated_relocation_targets()
        except (DependencyUnavailable, OSError, ValueError):
            relocation_targets = {}
    unmapped_evidence = (
        resolve_unmapped_start(address, ghidra_target, relocation_targets)
        if unmapped and args.unmapped
        else None
    )
    if unmapped and not args.unmapped:
        print(f"error: 0x{address:08X} is not in {MAP_PATH.relative_to(ROOT)}", file=sys.stderr)
        print(
            "       Use tools/decomp discover, then pass --unmapped to inspect a result.",
            file=sys.stderr,
        )
        return 1
    if unmapped and unmapped_evidence is None:
        print(
            f"error: 0x{address:08X} has no supported function-start evidence",
            file=sys.stderr,
        )
        return 1

    index = bisect.bisect_left(addresses, address)
    if unmapped:
        assert unmapped_evidence is not None
        ghidra_size = unmapped_evidence.ghidra_size
        ghidra_name = unmapped_evidence.ghidra_name
        following = (
            address + ghidra_size
            if ghidra_size is not None
            else addresses[index] if index < len(addresses) else address
        )
        state, source, line = "UNMAPPED", "", 0
        match = None
        target_name = (
            f"(Ghidra hint: {ghidra_name or '?'})"
            if ghidra_size is not None
            else "(annotated-global relocation target)"
        )
    else:
        following = addresses[index + 1] if index + 1 < len(addresses) else address
        state, source, line = functions.get(address, ("NOT_STARTED", "", 0))
        match = matches.get(address)
        target_name = names[address]

    if alignment_origin is not None:
        print(
            f"== alignment 0x{alignment_origin:08X} has "
            f"{address - alignment_origin} retail padding byte(s); "
            f"using mapped start 0x{address:08X}"
        )
        print()
    print(f"== target 0x{address:08X}  {target_name}")
    print(f"state            {state}" + (f"  ({source}:{line})" if source else ""))
    if unmapped:
        if ghidra_size is not None:
            print(f"Ghidra body size {following - address} bytes")
            if unmapped_evidence.relocation_references:
                print("start evidence   exact Ghidra start and annotated-global relocation")
                for reference in unmapped_evidence.relocation_references:
                    owner_address = max(
                        (item for item in annotated_globals if item <= reference),
                        default=None,
                    )
                    owner = (
                        annotated_globals.get(owner_address)
                        if owner_address is not None
                        else None
                    )
                    symbol = (
                        global_names.get(owner_address, "")
                        if owner_address is not None
                        else ""
                    )
                    detail = (
                        f" in {symbol or '?'} ({owner[0]}:{owner[1]})"
                        if owner is not None
                        else ""
                    )
                    print(f"  relocation 0x{reference:08X}{detail}")
            print(
                "name status      local hint only. "
                "Verify a durable name before insertion."
            )
        else:
            print(f"approximate size {following - address} bytes (map-gap fallback)")
            print("start evidence   annotated-global relocation target")
            for reference in unmapped_evidence.relocation_references:
                owner_address = max(
                    (item for item in annotated_globals if item <= reference),
                    default=None,
                )
                owner = annotated_globals.get(owner_address) if owner_address else None
                symbol = global_names.get(owner_address, "") if owner_address else ""
                detail = (
                    f" in {symbol or '?'} ({owner[0]}:{owner[1]})"
                    if owner is not None
                    else ""
                )
                print(f"  relocation 0x{reference:08X}{detail}")
            print(
                "name status      no retail name. "
                "Verify a durable name before insertion."
            )
    else:
        if address in original_sizes:
            print(f"retail body size  {original_sizes[address]} bytes")
        else:
            print(f"approximate size  {following - address} bytes (map-gap fallback)")
    print(f"current match    {'-' if match is None else f'{match * 100:.2f}%'}")
    section("dependency readiness")
    if unmapped:
        print("unavailable until the function start and map entry are confirmed")
    else:
        try:
            dependency_candidates = build_candidates()
            dependency_graph = build_call_graph(entries)
            add_dependency_evidence(dependency_candidates, dependency_graph)
            dependency_target = next(
                item for item in dependency_candidates if item.address == address
            )
            if dependency_target.state == "FUNCTION":
                print("implemented source. Dependency readiness applies to unfinished targets")
            else:
                print("ready" if dependency_target.dependency_ready else "blocked")
            if dependency_target.blocker_kind:
                print(f"  advisory   {dependency_target.blocker_kind}")
            unresolved = list(dependency_target.unresolved_dependencies)
            for dependency in bounded(
                unresolved, row_limit, f"tools/decomp evidence {args.address} --full"
            ):
                print(f"  unresolved  0x{dependency:08X}  {names.get(dependency, '(not in map)')}")
            weak = list(dependency_target.weak_dependencies)
            for dependency in bounded(
                weak, row_limit, f"tools/decomp evidence {args.address} --full"
            ):
                print(
                    f"  uncertain   0x{dependency:08X}  "
                    f"{names.get(dependency, '(not in map)')}"
                )
            if dependency_target.manual_blocker:
                print(f"  manual      {dependency_target.deferred_reason}")
            print(
                f"  impact      immediately unlocks {dependency_target.immediate_unlocks}, "
                f"reaches {dependency_target.large_goal_reach} large target(s)"
            )
            print(
                f"  indirect    {dependency_target.indirect_calls} call(s), "
                f"{dependency_target.indirect_jumps} jump(s)"
            )
        except (DependencyUnavailable, StopIteration) as error:
            print(f"unavailable: {error}")

    section("map neighbors")
    low = max(index - NEIGHBOR_COUNT, 0)
    high = min(index + NEIGHBOR_COUNT + (0 if unmapped else 1), len(entries))
    neighbors = list(entries[low:high])
    if unmapped:
        neighbors.insert(index - low, (address, "(unmapped candidate)"))
    for neighbor, name in neighbors:
        marker = ">" if neighbor == address else " "
        neighbor_state = (
            "UNMAPPED"
            if unmapped and neighbor == address
            else functions.get(neighbor, ("NOT_STARTED", "", 0))[0]
        )
        print(f"{marker} 0x{neighbor:08X}  {name:<50} {neighbor_state}")

    section("callers (x-refs to this function)")
    callers = run_ghidra(["x-ref", "to", f"0x{address:08X}"])
    if not callers:
        print("none reported by Ghidra (an indirect or table-dispatched call is still possible)")
    else:
        caller_rows = callers if isinstance(callers, list) else []
        for reference in bounded(
            caller_rows, row_limit, f"tools/decomp evidence {args.address} --full"
        ):
            origin = reference.get("from", "")
            try:
                origin_address = int(origin, 16)
            except (TypeError, ValueError):
                origin_address = None
            owner = "?"
            if origin_address is not None:
                ghidra_owner = ghidra_function_containing(origin_address)
                if ghidra_owner and ghidra_owner[0] in names:
                    owner_address = ghidra_owner[0]
                    owner_state = functions.get(owner_address, ("NOT_STARTED", "", 0))[0]
                    owner = f"0x{owner_address:08X} {names[owner_address]} [{owner_state}]"
                elif ghidra_owner:
                    owner_address, _, owner_hint = ghidra_owner
                    owner = (
                        f"0x{owner_address:08X} (unmapped, Ghidra hint: "
                        f"{owner_hint or '?'})"
                    )
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
    for target_address in bounded(
        targets, row_limit, f"tools/decomp evidence {args.address} --full"
    ):
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
        selected_lines = decomp_lines(body, args.full, args.decomp_range)
        print("\n".join(selected_lines) or "(empty)")
        total_lines = len(body.strip().splitlines())
        if args.decomp_range:
            print(
                f"... showing lines {args.decomp_range[0]}:{args.decomp_range[1]} "
                f"of {total_lines}. Run `tools/decomp evidence {args.address} --full` for all."
            )
        elif not args.full and total_lines > DECOMP_HEAD + DECOMP_TAIL:
            print(f"Run `tools/decomp evidence {args.address} --full` for all decompilation lines.")
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
        for literal in bounded(
            literals, row_limit, f"tools/decomp evidence {args.address} --full"
        ):
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
    for data_address in bounded(
        referenced, row_limit, f"tools/decomp evidence {args.address} --full"
    ):
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
            print(
                f"  ... {len(rows) - args.disasm_limit} omitted. Run "
                f"`tools/decomp evidence {args.address} --disasm --full` for all."
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
