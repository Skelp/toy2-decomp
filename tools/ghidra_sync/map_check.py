"""Consistency check between functions_map.txt, source, and the Ghidra project.

This converts the maintainers' careful-but-manual discipline into an enforced
invariant:

  * the map is sorted ascending, free of duplicate addresses, and every entry
    is named and falls within the retail executable's code section;
  * for every exact-matched function, the map name agrees with the source name
    resolved by reccmp (and thus, after sync, with Ghidra).

Structural checks always run. The name cross-check requires a current build and
degrades gracefully (warning, not failure) when comparison artifacts are stale.
A Ghidra function-set cross-reference is reported as an informational warning
only, because Ghidra's auto-analysis under-detects MSVC functions and is not a
reliable ground truth for "is this address a real function."
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path
from typing import Optional

from colorama import Fore, Style, init

init(autoreset=True)

from .ghidra_client import ensure_bridge_running, get_all_functions
from .manifest import ROOT, build_manifest, canonical_address


MAP_PATH = ROOT / "tools" / "Resources" / "functions_map.txt"
EXE_PATH = ROOT / "original" / "toy2.exe"

# Matches the address/name format produced by decomp_utils.parse_functions_map.
_MAP_LINE_RE = re.compile(r"^(?:0x)?([0-9A-Fa-f]{6,8})(?:\s+(.+))?$")


def _parse_map(path: Path = MAP_PATH) -> list[tuple[int, str, str]]:
    """Return [(address_int, canonical_address, name)] in file order."""
    if not path.is_file():
        raise RuntimeError(f"functions map not found: {path}")
    entries: list[tuple[int, str, str]] = []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        match = _MAP_LINE_RE.match(line)
        if not match:
            raise RuntimeError(f"{path}:{lineno}: unparseable line: {raw!r}")
        value = int(match.group(1), 16)
        name = (match.group(2) or "").strip()
        entries.append((value, canonical_address(value), name))
    return entries


def _code_section_ranges(path: Path = EXE_PATH) -> list[tuple[int, int]]:
    """Return [(start, end)] RVA ranges of executable sections from the PE."""
    data = path.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        raise RuntimeError(f"{path}: not a valid PE image")
    image_base = struct.unpack_from("<I", data, e_lfanew + 24 + 28)[0]
    num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    opt_header_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    table = e_lfanew + 24 + opt_header_size
    ranges: list[tuple[int, int]] = []
    for i in range(num_sections):
        offset = table + i * 40
        name = data[offset : offset + 8].rstrip(b"\0")
        if name != b".text":
            continue
        virtual_size = struct.unpack_from("<I", data, offset + 8)[0]
        virtual_address = struct.unpack_from("<I", data, offset + 12)[0]
        start = image_base + virtual_address
        ranges.append((start, start + virtual_size))
    if not ranges:
        raise RuntimeError(f"{path}: no .text section found")
    return ranges


def _check_structure(entries: list[tuple[int, str, str]]) -> list[str]:
    """Pure checks needing no external tool: sorted, dedup, named, in .text."""
    problems: list[str] = []
    try:
        ranges = _code_section_ranges()
    except (OSError, RuntimeError) as error:
        ranges = None
        print(f"{Fore.YELLOW}Could not read code section: {error}{Style.RESET_ALL}")

    seen: dict[int, str] = {}
    prev: Optional[int] = None
    for value, addr, name in entries:
        if not name:
            problems.append(f"  {addr}  has no name")
        if value in seen:
            problems.append(f"  {addr}  duplicate of {seen[value]}")
        else:
            seen[value] = addr
        if prev is not None and value < prev:
            problems.append(f"  {addr}  out of order (follows 0x{prev:08X})")
        prev = value
        if ranges is not None and not any(lo <= value < hi for lo, hi in ranges):
            problems.append(f"  {addr}  outside the .text section")
    return problems


def _check_addresses_against_ghidra(entries: list[tuple[int, str, str]]) -> Optional[int]:
    """Return how many map addresses Ghidra does not recognize as a function.

    Informational only (Ghidra under-detects MSVC functions); callers should
    report this as a warning, not a failure.
    """
    ghidra = _ghidra_function_set()
    if ghidra is None:
        return None
    return sum(1 for _value, addr, _name in entries if addr.lower() not in ghidra)


def _ghidra_function_set() -> Optional[set[str]]:
    """Return canonical addresses of every Ghidra function, or None if offline."""
    if not ensure_bridge_running():
        return None
    return {function.address.lower() for function in get_all_functions()}


def _check_names(
    entries: list[tuple[int, str, str]], targets: Optional[set[str]]
) -> tuple[list[str], int]:
    """Compare map names with the source/reccmp name for exact functions."""
    manifest = build_manifest(targets)
    exact = {
        record.address.lower(): record.name
        for record in manifest.functions
        if record.reason is None
    }
    by_addr = {addr.lower(): name for _value, addr, name in entries}
    # Normalize targets to lowercase canonical form for the post-filter below;
    # build_manifest already accepts any casing, but this comparison does not.
    norm_targets = {canonical_address(t).lower() for t in targets} if targets else None

    problems: list[str] = []
    checked = 0
    for addr_lower, source_name in sorted(exact.items()):
        if norm_targets is not None and addr_lower not in norm_targets:
            continue
        checked += 1
        map_name = by_addr.get(addr_lower)
        if map_name is None:
            problems.append(f"  {addr_lower}  exact function missing from map")
        elif map_name != source_name:
            problems.append(
                f"  {addr_lower}  map={map_name!r}  source={source_name!r}"
            )
    return problems, checked


def run_check(targets: Optional[set[str]] = None) -> int:
    """Run every applicable check and print a report. Returns an exit code."""
    entries = _parse_map()

    failures: list[tuple[str, list[str]]] = []
    warnings: list[str] = []

    structure = _check_structure(entries)
    failures.append(("structure (sorted / deduplicated / named / in .text)", structure))

    ghidra_missing = _check_addresses_against_ghidra(entries)
    if ghidra_missing is None:
        warnings.append("Ghidra bridge unavailable: skipped Ghidra function-set cross-reference.")
    elif ghidra_missing:
        warnings.append(
            f"Ghidra does not recognize {ghidra_missing} of {len(entries)} map addresses as "
            "functions (informational: Ghidra auto-analysis under-detects MSVC functions)."
        )

    try:
        name_problems, checked = _check_names(entries, targets)
        failures.append((f"matched names agree ({checked} exact functions)", name_problems))
    except RuntimeError as error:
        warnings.append(f"Skipping name check: {error}")

    for title, problems in failures:
        count = len(problems)
        color = Fore.GREEN if count == 0 else Fore.RED
        print(f"\n{color}== {title}: {count} problem(s){Style.RESET_ALL}")
        for line in problems:
            print(f"{Fore.RED}{line}{Style.RESET_ALL}")

    if warnings:
        print(f"\n{Fore.YELLOW}Warnings:{Style.RESET_ALL}")
        for line in warnings:
            print(f"{Fore.YELLOW}  {line}{Style.RESET_ALL}")

    total = sum(len(p) for _, p in failures)
    print("\n" + "=" * 60)
    if total == 0:
        print(f"{Fore.GREEN}functions_map.txt is consistent.{Style.RESET_ALL}")
        return 0
    print(f"{Fore.RED}{total} consistency problem(s) found.{Style.RESET_ALL}")
    print("Update functions_map.txt to match the source/reccmp name for exact")
    print("functions; ensure new addresses fall within .text and keep the list")
    print("sorted and deduplicated.")
    return 1


def main(argv: Optional[list[str]] = None) -> int:
    targets = set(argv) if argv else None
    return run_check(targets)
