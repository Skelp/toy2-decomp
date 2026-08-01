#!/usr/bin/env python3
"""Find credible retail function starts that are absent from the function map."""

from __future__ import annotations

import argparse
import bisect
import json
import subprocess
import sys
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
EXCLUSIONS_PATH = ROOT / "tools" / "Resources" / "function-discovery-exclusions.tsv"
sys.path.insert(0, str(ROOT))

from tools import decomp_binary  # noqa: E402
from tools.decomp_candidates import parse_map  # noqa: E402
from tools.decomp_dependencies import DependencyUnavailable, _capstone  # noqa: E402


CONFIDENCE_ORDER = {"high": 0, "medium": 1, "low": 2}


@dataclass(frozen=True)
class GhidraFunction:
    address: int
    size: int
    name: str


@dataclass(frozen=True)
class TransferEvidence:
    call_callers: frozenset[int]
    jump_callers: frozenset[int]


@dataclass(frozen=True)
class Discovery:
    address: int
    size: int
    confidence: str
    ghidra_name: str
    call_callers: tuple[int, ...]
    jump_callers: tuple[int, ...]
    previous_map_address: int | None
    previous_map_name: str
    next_map_address: int | None
    next_map_name: str

    @property
    def reason(self) -> str:
        if self.call_callers:
            count = len(self.call_callers)
            return f"{count} direct retail CALL caller{'s' if count != 1 else ''}"
        if self.jump_callers:
            count = len(self.jump_callers)
            return f"{count} cross-function retail JMP caller{'s' if count != 1 else ''}"
        return "Ghidra function start only"


def read_exclusions(path: Path = EXCLUSIONS_PATH) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    if not path.exists():
        return ranges
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t", 2)
        if len(fields) < 2:
            raise ValueError(f"{path}:{line_number}: expected start and end addresses")
        try:
            start, end = int(fields[0], 16), int(fields[1], 16)
        except ValueError as error:
            raise ValueError(f"{path}:{line_number}: invalid address") from error
        if start >= end:
            raise ValueError(f"{path}:{line_number}: range is empty or reversed")
        ranges.append((start, end))
    return ranges


def parse_address(value: object) -> int | None:
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def parse_ghidra_functions(payload: object) -> list[GhidraFunction]:
    functions: list[GhidraFunction] = []
    if not isinstance(payload, list):
        return functions
    for row in payload:
        if not isinstance(row, dict):
            continue
        address = parse_address(row.get("address"))
        try:
            size = int(row.get("size", 0))
        except (TypeError, ValueError):
            size = 0
        if address is None or size <= 0:
            continue
        functions.append(
            GhidraFunction(address, size, str(row.get("name") or ""))
        )
    return sorted(functions, key=lambda item: item.address)


def read_ghidra_functions() -> list[GhidraFunction]:
    try:
        result = subprocess.run(
            [
                "ghidra",
                "function",
                "list",
                "--json",
                "--limit",
                "0",
                "--fields",
                "address,size,name",
            ],
            capture_output=True,
            text=True,
            check=False,
            cwd=ROOT,
        )
    except FileNotFoundError as error:
        raise DependencyUnavailable(
            "The Ghidra CLI is unavailable. Start the configured bridge first."
        ) from error
    if result.returncode != 0:
        detail = result.stderr.strip() or "the Ghidra query failed"
        raise DependencyUnavailable(detail)
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise DependencyUnavailable("The Ghidra CLI returned invalid JSON.") from error
    functions = parse_ghidra_functions(payload)
    if not functions:
        raise DependencyUnavailable("Ghidra reported no functions.")
    return functions


def scan_transfers(functions: list[GhidraFunction]) -> dict[int, TransferEvidence]:
    """Collect direct transfers whose targets are Ghidra function starts."""

    if not decomp_binary.available():
        raise DependencyUnavailable(
            "The retail executable is unavailable. Run the repository setup command first."
        )
    disassembler, immediate_operand = _capstone()
    starts = {function.address for function in functions}
    calls: dict[int, set[int]] = defaultdict(set)
    jumps: dict[int, set[int]] = defaultdict(set)

    for function in functions:
        code = decomp_binary.read_bytes(function.address, function.size)
        if code is None:
            continue
        end = function.address + function.size
        for instruction in disassembler.disasm(code, function.address):
            mnemonic = instruction.mnemonic.lower()
            if mnemonic not in ("call", "jmp"):
                continue
            operands = instruction.operands
            if not operands or operands[0].type != immediate_operand:
                continue
            target = operands[0].imm & 0xFFFFFFFF
            if target not in starts or target == function.address:
                continue
            if mnemonic == "call":
                calls[target].add(function.address)
            elif not function.address <= target < end:
                jumps[target].add(function.address)

    return {
        target: TransferEvidence(
            frozenset(calls.get(target, set())),
            frozenset(jumps.get(target, set())),
        )
        for target in calls.keys() | jumps.keys()
    }


def import_thunk_addresses(functions: list[GhidraFunction]) -> frozenset[int]:
    """Return six-byte x86 indirect jumps through the import address table."""

    addresses: set[int] = set()
    for function in functions:
        if function.size != 6:
            continue
        code = decomp_binary.read_bytes(function.address, function.size)
        if code is not None and code[:2] == b"\xFF\x25":
            addresses.add(function.address)
    return frozenset(addresses)


def discover(
    entries: list[tuple[int, str]],
    functions: list[GhidraFunction],
    transfers: dict[int, TransferEvidence],
    minimum_confidence: str = "medium",
    exclusions: tuple[tuple[int, int], ...] = (),
    excluded_addresses: frozenset[int] = frozenset(),
) -> list[Discovery]:
    """Return ranked unmapped starts inside the confirmed game/engine range."""

    if not entries:
        return []
    mapped_names = dict(entries)
    mapped_addresses = [address for address, _ in entries]
    functions_by_address = {function.address: function for function in functions}
    mapped_ghidra = [
        functions_by_address[address]
        for address in mapped_addresses
        if address in functions_by_address
    ]
    if not mapped_ghidra:
        return []
    range_start = mapped_addresses[0]
    range_end = max(
        function.address + function.size
        for function in mapped_ghidra
        if function.address <= mapped_addresses[-1]
    )
    mapped_ranges = [
        (function.address, function.address + function.size)
        for function in mapped_ghidra
    ]
    threshold = CONFIDENCE_ORDER[minimum_confidence]
    results: list[Discovery] = []

    for function in functions:
        if function.address in mapped_names:
            continue
        if function.address in excluded_addresses:
            continue
        if not range_start <= function.address < range_end:
            continue
        if any(start <= function.address < end for start, end in exclusions):
            continue
        if any(start < function.address < end for start, end in mapped_ranges):
            continue
        evidence = transfers.get(
            function.address, TransferEvidence(frozenset(), frozenset())
        )
        if evidence.call_callers:
            confidence = "high"
        elif evidence.jump_callers:
            confidence = "medium"
        else:
            confidence = "low"
        if CONFIDENCE_ORDER[confidence] > threshold:
            continue

        insertion = bisect.bisect_left(mapped_addresses, function.address)
        previous = entries[insertion - 1] if insertion else (None, "")
        following = entries[insertion] if insertion < len(entries) else (None, "")
        if following[0] is not None and function.address + function.size > following[0]:
            continue
        results.append(
            Discovery(
                address=function.address,
                size=function.size,
                confidence=confidence,
                ghidra_name=function.name,
                call_callers=tuple(sorted(evidence.call_callers)),
                jump_callers=tuple(sorted(evidence.jump_callers)),
                previous_map_address=previous[0],
                previous_map_name=previous[1],
                next_map_address=following[0],
                next_map_name=following[1],
            )
        )

    return sorted(
        results,
        key=lambda item: (
            CONFIDENCE_ORDER[item.confidence],
            -len(item.call_callers),
            -len(item.jump_callers),
            item.size,
            item.address,
        ),
    )


def json_row(item: Discovery) -> dict[str, object]:
    row = asdict(item)
    row["address"] = f"0x{item.address:08X}"
    row["call_callers"] = [f"0x{address:08X}" for address in item.call_callers]
    row["jump_callers"] = [f"0x{address:08X}" for address in item.jump_callers]
    for key in ("previous_map_address", "next_map_address"):
        value = row[key]
        row[key] = None if value is None else f"0x{value:08X}"
    row["reason"] = item.reason
    return row


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Find credible Ghidra function starts absent from the committed map."
    )
    parser.add_argument("--limit", type=int, default=20, help="maximum rows (0 = all)")
    parser.add_argument(
        "--min-confidence",
        choices=("high", "medium", "low"),
        default="medium",
        help="weakest evidence to include",
    )
    parser.add_argument("--all", action="store_true", help="include low-confidence starts")
    parser.add_argument("--json", action="store_true", help="print structured JSON")
    args = parser.parse_args()
    if args.limit < 0:
        parser.error("--limit must be zero or greater")

    try:
        functions = read_ghidra_functions()
        transfers = scan_transfers(functions)
    except DependencyUnavailable as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    minimum = "low" if args.all else args.min_confidence
    try:
        exclusions = tuple(read_exclusions())
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    results = discover(
        parse_map(),
        functions,
        transfers,
        minimum,
        exclusions,
        import_thunk_addresses(functions),
    )
    shown = results if args.limit == 0 else results[: args.limit]

    if args.json:
        print(json.dumps([json_row(item) for item in shown], indent=2))
        return 0
    if not shown:
        print(f"No unmapped starts met the {minimum}-confidence threshold.")
        return 0

    print("Unmapped retail function candidates (verification required)")
    print("Ghidra names are local hints. Do not copy them into the map without evidence.\n")
    for item in shown:
        hint = f"  hint={item.ghidra_name}" if item.ghidra_name else ""
        print(
            f"0x{item.address:08X}  {item.confidence:<6}  {item.size:5} bytes  "
            f"{item.reason}{hint}"
        )
        previous = (
            "-"
            if item.previous_map_address is None
            else f"0x{item.previous_map_address:08X} {item.previous_map_name}"
        )
        following = (
            "-"
            if item.next_map_address is None
            else f"0x{item.next_map_address:08X} {item.next_map_name}"
        )
        print(f"  map gap: {previous} -> {following}")
    if len(shown) < len(results):
        print(f"\n{len(results) - len(shown)} more candidate(s). Raise --limit to show them.")
    print("\nInspect one with: tools/decomp evidence --unmapped 0xADDRESS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
