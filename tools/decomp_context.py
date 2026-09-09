#!/usr/bin/env python3
"""Build and validate a bounded, doctor-backed function context pack."""

from __future__ import annotations

import sys
from pathlib import Path


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import argparse
import hashlib
import json
import math
import re
from dataclasses import dataclass
from typing import Callable, Final, Mapping

from tools.decomp_annotations import read_source_annotations
from tools.decomp_dependencies import (
    DependencyGraph,
    DependencyUnavailable,
    build_call_graph,
)
from tools.decomp_doctor import load_ghidra_artifact


CONTEXT_PACK_SCHEMA: Final = 1
PINNED_CAPSTONE_VERSION: Final = "5.0.9"
DEFAULT_CALL_DEPTH: Final = 1
MAX_CALL_NODES: Final = 12
MAX_ACCEPTED_SOURCES: Final = 3
MAX_INSTRUCTIONS: Final = 4096
MAX_BASIC_BLOCKS: Final = 1024
MAX_EDGES: Final = 2048
MAX_MEMORY_OPERANDS: Final = 4096
MAX_UNKNOWN_REASONS: Final = 64
MAX_TEXT_CHARS: Final = 500
MAX_SOURCE_EXCERPT_CHARS: Final = 2000
MAX_CONTEXT_BYTES: Final = 131_072
MAX_BRIEF_BYTES: Final = 8 * 1024 * 1024

_ADDRESS_RE = re.compile(r"(?:0x)?([0-9a-fA-F]{1,8})$")
_CONDITIONAL_JUMPS = frozenset(
    {
        "ja", "jae", "jb", "jbe", "jc", "jcxz", "jecxz", "je", "jg",
        "jge", "jl", "jle", "jna", "jnae", "jnb", "jnbe", "jnc", "jne",
        "jng", "jnge", "jnl", "jnle", "jno", "jnp", "jns", "jnz", "jo",
        "jp", "jpe", "jpo", "js", "jz", "loop", "loope", "loopne",
        "loopnz", "loopz",
    }
)
_UNCONDITIONAL_JUMPS = frozenset({"jmp", "ljmp"})
_RETURNS = frozenset({"ret", "retf", "iret", "iretd"})


class ContextError(RuntimeError):
    """Report that a context pack cannot be built or trusted."""


@dataclass(frozen=True)
class DecodedMemory:
    operand: int
    access: str
    segment: str | None = None
    base: str | None = None
    index: str | None = None
    scale: int = 1
    displacement: int = 0
    size: int = 0


@dataclass(frozen=True)
class DecodedInstruction:
    address: int
    size: int
    mnemonic: str
    operands: str
    direct_target: int | None = None
    indirect: bool = False
    memory: tuple[DecodedMemory, ...] = ()


Decoder = Callable[[bytes, int], DecodedInstruction]


def _capstone_decoder() -> Decoder:
    """Create one x86-32 detail decoder or fail with a stable reason."""

    try:
        from capstone import (  # type: ignore[import-not-found]
            CS_AC_READ,
            CS_AC_WRITE,
            CS_ARCH_X86,
            CS_MODE_32,
            Cs,
        )
        from capstone.x86 import (  # type: ignore[import-not-found]
            X86_OP_IMM,
            X86_OP_MEM,
        )
    except ImportError as error:
        raise ContextError("capstone-unavailable") from error

    disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
    disassembler.detail = True

    def decode(code: bytes, address: int) -> DecodedInstruction:
        values = list(disassembler.disasm(code, address, count=2))
        if len(values) != 1 or values[0].address != address:
            raise ContextError("instruction-decode-failed")
        instruction = values[0]
        if instruction.size != len(code):
            raise ContextError("instruction-byte-count-mismatch")
        mnemonic = instruction.mnemonic.lower()
        is_transfer = (
            mnemonic in _CONDITIONAL_JUMPS
            or mnemonic in _UNCONDITIONAL_JUMPS
            or mnemonic.startswith("call")
        )
        direct_target = None
        indirect = False
        if is_transfer:
            if instruction.operands and instruction.operands[0].type == X86_OP_IMM:
                direct_target = int(instruction.operands[0].imm) & 0xFFFFFFFF
            else:
                indirect = True
        memory: list[DecodedMemory] = []
        for index, operand in enumerate(instruction.operands):
            if operand.type != X86_OP_MEM:
                continue
            access_bits = int(getattr(operand, "access", 0))
            access = (
                "read-write"
                if access_bits & CS_AC_READ and access_bits & CS_AC_WRITE
                else "read"
                if access_bits & CS_AC_READ
                else "write"
                if access_bits & CS_AC_WRITE
                else "unknown"
            )
            memory.append(
                DecodedMemory(
                    operand=index,
                    access=access,
                    segment=(
                        instruction.reg_name(operand.mem.segment).lower()
                        if operand.mem.segment else None
                    ),
                    base=(
                        instruction.reg_name(operand.mem.base).lower()
                        if operand.mem.base else None
                    ),
                    index=(
                        instruction.reg_name(operand.mem.index).lower()
                        if operand.mem.index else None
                    ),
                    scale=int(operand.mem.scale),
                    displacement=int(operand.mem.disp),
                    size=int(operand.size),
                )
            )
        return DecodedInstruction(
            address=address,
            size=instruction.size,
            mnemonic=mnemonic,
            operands=instruction.op_str.lower(),
            direct_target=direct_target,
            indirect=indirect,
            memory=tuple(memory),
        )

    return decode


def _capstone_identity() -> dict[str, object]:
    try:
        import capstone  # type: ignore[import-not-found]
    except ImportError:
        return {
            "engine": "artifact-row-fallback",
            "version": None,
            "pinned_version": PINNED_CAPSTONE_VERSION,
            "architecture": "x86",
            "mode": 32,
            "detail": False,
        }
    return {
        "engine": "capstone",
        "version": str(capstone.__version__),
        "pinned_version": PINNED_CAPSTONE_VERSION,
        "architecture": "x86",
        "mode": 32,
        "detail": True,
    }


def _address(value: object) -> int:
    if isinstance(value, bool):
        raise ContextError("an instruction address is invalid")
    if isinstance(value, int):
        address = value
    elif isinstance(value, str) and (match := _ADDRESS_RE.fullmatch(value.strip())):
        address = int(match.group(1), 16)
    else:
        raise ContextError("an instruction address is invalid")
    if not 0 <= address <= 0xFFFFFFFF:
        raise ContextError("an instruction address does not fit in 32 bits")
    return address


def _artifact_decoder(rows: list[object]) -> Decoder:
    by_address = {
        _address(row.get("address", row.get("addr"))): row
        for row in rows
        if isinstance(row, Mapping)
    }

    def decode(code: bytes, address: int) -> DecodedInstruction:
        row = by_address[address]
        mnemonic = str(row.get("mnemonic", "")).strip().lower()[:32]
        raw_operands = row.get("operands", [])
        if isinstance(raw_operands, list):
            operands = ", ".join(str(value) for value in raw_operands)
            first = str(raw_operands[0]).strip() if raw_operands else ""
        else:
            operands = str(raw_operands)
            first = operands.split(",", 1)[0].strip()
        direct_target = None
        indirect = False
        if (
            mnemonic in _CONDITIONAL_JUMPS
            or mnemonic in _UNCONDITIONAL_JUMPS
            or mnemonic.startswith("call")
        ):
            match = _ADDRESS_RE.fullmatch(first)
            if match:
                direct_target = int(match.group(1), 16)
            else:
                indirect = True
        return DecodedInstruction(
            address=address,
            size=len(code),
            mnemonic=mnemonic,
            operands=operands[:MAX_TEXT_CHARS],
            direct_target=direct_target,
            indirect=indirect,
        )

    return decode


def _artifact_citation(
    reference: str,
    descriptor: Mapping[str, object],
    locator: str,
) -> dict[str, str]:
    path = descriptor.get("path")
    digest = descriptor.get("sha256")
    if not isinstance(path, str) or Path(path).is_absolute():
        raise ContextError("the disassembly artifact path is not repository-relative")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ContextError("the disassembly artifact hash is invalid")
    return {
        "artifact_ref": reference,
        "json_locator": locator,
    }


def _citation(descriptor: Mapping[str, object], address: int) -> dict[str, str]:
    return _artifact_citation(
        "disassembly",
        descriptor,
        f"$.payload[?(@.address=='{address:08x}')]",
    )


def normalize_disassembly(
    rows: object,
    *,
    decoder: Decoder | None = None,
) -> list[dict[str, object]]:
    """Decode, normalize, and sort bounded retail instruction rows."""

    active_decoder = decoder or _capstone_decoder()
    if not isinstance(rows, list):
        raise ContextError("the disassembly artifact payload is not a list")
    parsed: list[tuple[int, bytes, Mapping[str, object]]] = []
    for row in rows:
        if not isinstance(row, Mapping):
            raise ContextError("a disassembly row is not an object")
        address = _address(row.get("address", row.get("addr")))
        encoded = row.get("bytes")
        if not isinstance(encoded, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2})+", encoded):
            raise ContextError(f"instruction 0x{address:08X} has invalid bytes")
        parsed.append((address, bytes.fromhex(encoded), row))
    parsed.sort(key=lambda item: (item[0], item[1]))
    if len(parsed) > MAX_INSTRUCTIONS:
        parsed = parsed[:MAX_INSTRUCTIONS]

    result: list[dict[str, object]] = []
    seen: set[int] = set()
    for address, code, row in parsed:
        if address in seen:
            raise ContextError(f"instruction 0x{address:08X} is duplicated")
        seen.add(address)
        instruction = active_decoder(code, address)
        if instruction.address != address or instruction.size != len(code):
            raise ContextError(f"instruction 0x{address:08X} does not match its bytes")
        retail_mnemonic = str(row.get("mnemonic", "")).strip().lower()
        if retail_mnemonic and retail_mnemonic != instruction.mnemonic.lower():
            raise ContextError(f"instruction 0x{address:08X} does not match its artifact row")
        result.append(
            {
                "address": f"0x{address:08X}",
                "bytes": code.hex(),
                "size": instruction.size,
                "mnemonic": instruction.mnemonic.lower()[:32],
                "operands": instruction.operands.lower()[:MAX_TEXT_CHARS],
                "direct_target": (
                    f"0x{instruction.direct_target & 0xFFFFFFFF:08X}"
                    if instruction.direct_target is not None
                    else None
                ),
                "indirect": instruction.indirect,
                "memory": [
                    {
                        "operand": memory.operand,
                        "access": memory.access,
                        "segment": memory.segment,
                        "base": memory.base,
                        "index": memory.index,
                        "scale": memory.scale,
                        "displacement": memory.displacement,
                        "size": memory.size,
                        "class": (
                            "stack"
                            if memory.base in {"esp", "ebp"}
                            else "absolute"
                            if memory.base is None and memory.index is None
                            else "base-index"
                        ),
                    }
                    for memory in instruction.memory[:MAX_MEMORY_OPERANDS]
                ],
            }
        )
    return result


def normalize_artifact(
    root: Path,
    target: str | int,
    size: int,
    descriptor: Mapping[str, object],
    *,
    decoder: Decoder | None = None,
) -> dict[str, object]:
    """Load one bound artifact and report conservative completeness."""

    target_address = _address(target)
    target_text = f"0x{target_address:08X}"
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
        raise ContextError("the retail function size is invalid")
    try:
        payload = load_ghidra_artifact(
            root.resolve(), target_text, "disassembly", descriptor
        )
    except ValueError as error:
        raise ContextError(f"the disassembly artifact is invalid: {error}") from error
    if not isinstance(payload, list):
        raise ContextError("the disassembly artifact payload is not a list")

    reasons: set[str] = set()
    used_capstone = False
    decoder_identity = {
        "engine": "injected",
        "version": None,
        "pinned_version": PINNED_CAPSTONE_VERSION,
        "architecture": "x86",
        "mode": 32,
        "detail": True,
    }
    active_decoder = decoder
    if active_decoder is None:
        decoder_identity = _capstone_identity()
        try:
            active_decoder = _capstone_decoder()
            used_capstone = True
            if decoder_identity.get("version") != PINNED_CAPSTONE_VERSION:
                reasons.add("capstone-version-mismatch")
        except ContextError as error:
            if str(error) != "capstone-unavailable":
                raise
            reasons.add("capstone-unavailable")
            reasons.add("memory-metadata-unavailable")
            active_decoder = _artifact_decoder(payload)
    instructions = normalize_disassembly(payload, decoder=active_decoder)
    if len(payload) > MAX_INSTRUCTIONS:
        reasons.add("instruction-limit")

    memory_rows: list[dict[str, object]] = []
    previous_end: int | None = None
    for row in instructions:
        address = _address(row["address"])
        citation = _citation(descriptor, address)
        row["evidence"] = citation
        if previous_end is not None and address != previous_end:
            kind = "overlap" if address < previous_end else "gap"
            reasons.add(f"instruction-{kind}:{previous_end:08X}-{address:08X}")
        previous_end = address + int(row["size"])
        if row.get("indirect") is True:
            reasons.add(f"indirect-control-transfer:{address:08X}")
        for memory in row.get("memory", []):
            if not isinstance(memory, dict):
                continue
            item = dict(memory)
            item["instruction"] = f"0x{address:08X}"
            item["evidence"] = citation
            memory_rows.append(item)
            if item.get("access") == "unknown":
                reasons.add(
                    f"unknown-memory-access:{address:08X}:{item.get('operand', 0)}"
                )

    if not instructions:
        reasons.add("no-decodable-instructions")
    else:
        first = _address(instructions[0]["address"])
        actual_end = _address(instructions[-1]["address"]) + int(
            instructions[-1]["size"]
        )
        if first != target_address:
            reasons.add(f"function-start-mismatch:{first:08X}")
        if actual_end != target_address + size:
            reasons.add(f"function-size-mismatch:{actual_end - target_address}:{size}")
        present = {_address(row["address"]) for row in instructions}
        for row in instructions:
            mnemonic = str(row["mnemonic"])
            direct_value = row.get("direct_target")
            if (
                mnemonic in _CONDITIONAL_JUMPS | _UNCONDITIONAL_JUMPS
                and isinstance(direct_value, str)
            ):
                direct = _address(direct_value)
                if target_address <= direct < target_address + size and direct not in present:
                    reasons.add(f"branch-target-gap:{direct:08X}")
    if len(memory_rows) > MAX_MEMORY_OPERANDS:
        memory_rows = memory_rows[:MAX_MEMORY_OPERANDS]
        reasons.add("memory-operand-limit")
    full_reasons = sorted(reasons)
    bounded_reasons = full_reasons[:MAX_UNKNOWN_REASONS]
    if len(full_reasons) > MAX_UNKNOWN_REASONS:
        bounded_reasons[-1] = f"reason-limit:{len(full_reasons)}"
    return {
        "instructions": instructions,
        "memory_operands": memory_rows,
        "decoder": decoder_identity,
        "completeness": {
            "complete": not full_reasons,
            "capstone": used_capstone,
            "instruction_bytes": not any(
                reason.startswith((
                    "instruction-gap",
                    "instruction-overlap",
                    "function-",
                    "branch-target-gap",
                    "instruction-limit",
                    "no-decodable-instructions",
                ))
                for reason in full_reasons
            ),
            "control_flow": not any(
                reason.startswith((
                    "capstone-",
                    "indirect-control-transfer",
                    "instruction-gap",
                    "instruction-overlap",
                    "instruction-limit",
                    "branch-target-gap",
                    "no-decodable-instructions",
                ))
                for reason in full_reasons
            ),
            "memory_access": not any(
                reason.startswith((
                    "capstone-",
                    "function-",
                    "instruction-gap",
                    "instruction-overlap",
                    "instruction-limit",
                    "branch-target-gap",
                    "unknown-memory-access",
                    "memory-metadata-unavailable",
                    "memory-operand-limit",
                    "no-decodable-instructions",
                ))
                for reason in full_reasons
            ),
            "unknown_reasons": bounded_reasons,
        },
    }


def build_control_flow(
    instructions: list[dict[str, object]],
    *,
    target: str | int,
    size: int,
) -> dict[str, object]:
    """Build a conservative CFG from direct transfers only."""

    start = _address(target)
    end = start + size
    ordered = sorted(instructions, key=lambda row: _address(row["address"]))
    by_address = {_address(row["address"]): row for row in ordered}
    addresses = sorted(by_address)
    following = {
        address: addresses[index + 1] if index + 1 < len(addresses) else None
        for index, address in enumerate(addresses)
    }
    leaders = {start} if start in by_address else set(addresses[:1])
    instruction_edges: list[tuple[int, int | None, str]] = []
    calls: list[dict[str, object]] = []
    returns: list[dict[str, object]] = []
    exits: list[dict[str, object]] = []

    for address in addresses:
        row = by_address[address]
        mnemonic = str(row.get("mnemonic", "")).lower()
        next_value = following[address]
        target_value = row.get("direct_target")
        direct = _address(target_value) if isinstance(target_value, str) else None
        if direct is not None and start <= direct < end and direct in by_address:
            leaders.add(direct)
        if mnemonic.startswith("call"):
            call: dict[str, object] = {
                "address": f"0x{address:08X}",
                "target": f"0x{direct:08X}" if direct is not None else None,
                "direct": direct is not None,
            }
            if isinstance(row.get("evidence"), Mapping):
                call["evidence"] = dict(row["evidence"])
            calls.append(call)
        elif mnemonic in _RETURNS:
            returned: dict[str, object] = {"address": f"0x{address:08X}"}
            if isinstance(row.get("evidence"), Mapping):
                returned["evidence"] = dict(row["evidence"])
            returns.append(returned)
            if next_value is not None:
                leaders.add(next_value)
        elif mnemonic in _UNCONDITIONAL_JUMPS:
            instruction_edges.append((address, direct, "jump" if direct is not None else "unknown"))
            if direct is not None and not start <= direct < end:
                exit_row: dict[str, object] = {
                    "address": f"0x{address:08X}",
                    "target": f"0x{direct:08X}",
                    "kind": "external-tail-jump",
                }
                if isinstance(row.get("evidence"), Mapping):
                    exit_row["evidence"] = dict(row["evidence"])
                exits.append(exit_row)
            if next_value is not None:
                leaders.add(next_value)
        elif mnemonic in _CONDITIONAL_JUMPS:
            instruction_edges.append((address, direct, "branch" if direct is not None else "unknown"))
            if next_value is not None:
                leaders.add(next_value)
                instruction_edges.append((address, next_value, "fallthrough"))

    sorted_leaders = sorted(value for value in leaders if value in by_address)
    block_limit_hit = len(sorted_leaders) > MAX_BASIC_BLOCKS
    block_for: dict[int, int] = {}
    blocks: list[dict[str, object]] = []
    for index, leader in enumerate(sorted_leaders[:MAX_BASIC_BLOCKS]):
        next_leader = sorted_leaders[index + 1] if index + 1 < len(sorted_leaders) else end
        members = [value for value in addresses if leader <= value < next_leader]
        if not members:
            continue
        for value in members:
            block_for[value] = leader
        last = members[-1]
        block: dict[str, object] = {
            "start": f"0x{leader:08X}",
            "end": f"0x{last + int(by_address[last]['size']):08X}",
            "instructions": [f"0x{value:08X}" for value in members],
        }
        if isinstance(by_address[leader].get("evidence"), Mapping):
            block["evidence"] = dict(by_address[leader]["evidence"])
        blocks.append(block)

    edge_keys: set[tuple[int, int | None, str]] = set()
    edge_sources: dict[tuple[int, int | None, str], int] = {}
    for source_instruction, target_instruction, kind in instruction_edges:
        source = block_for.get(source_instruction, source_instruction)
        destination = (
            block_for.get(target_instruction, target_instruction)
            if target_instruction is not None else None
        )
        key = (source, destination, kind)
        edge_keys.add(key)
        edge_sources[key] = source_instruction
    for block in blocks:
        members = block["instructions"]
        last = _address(members[-1])
        mnemonic = str(by_address[last]["mnemonic"])
        next_value = following[last]
        block_start = _address(block["start"])
        if (
            next_value is not None
            and mnemonic not in _RETURNS | _UNCONDITIONAL_JUMPS | _CONDITIONAL_JUMPS
            and block_for.get(next_value) != block_start
        ):
            key = (block_start, block_for.get(next_value, next_value), "fallthrough")
            edge_keys.add(key)
            edge_sources[key] = last
        elif next_value is None and mnemonic not in _RETURNS | _UNCONDITIONAL_JUMPS:
            exit_row: dict[str, object] = {
                "address": f"0x{last:08X}",
                "target": None,
                "kind": "fall-off",
            }
            if isinstance(by_address[last].get("evidence"), Mapping):
                exit_row["evidence"] = dict(by_address[last]["evidence"])
            exits.append(exit_row)
    visible_nodes = {_address(block["start"]) for block in blocks}
    visible_edge_keys = {
        key for key in edge_keys if key[0] in visible_nodes
    }
    ordered_edges = sorted(
        visible_edge_keys,
        key=lambda row: (row[0], -1 if row[1] is None else row[1], row[2]),
    )
    edge_limit_hit = len(ordered_edges) > MAX_EDGES
    edges = []
    for source, destination, kind in ordered_edges[:MAX_EDGES]:
        edge: dict[str, object] = {
            "from": f"0x{source:08X}",
            "to": f"0x{destination:08X}" if destination is not None else None,
            "kind": kind,
        }
        evidence_row = by_address.get(edge_sources[(source, destination, kind)])
        if evidence_row is not None and isinstance(evidence_row.get("evidence"), Mapping):
            edge["evidence"] = dict(evidence_row["evidence"])
        edges.append(edge)

    nodes = visible_nodes
    predecessors: dict[int, set[int]] = {node: set() for node in nodes}
    for edge in edges:
        source = _address(edge["from"])
        destination_value = edge.get("to")
        if isinstance(destination_value, str):
            destination = _address(destination_value)
            if source in nodes and destination in nodes:
                predecessors[destination].add(source)
    entry = min(nodes) if nodes else None
    successors: dict[int, set[int]] = {node: set() for node in nodes}
    for destination, incoming in predecessors.items():
        for source in incoming:
            successors[source].add(destination)
    reachable: set[int] = set()
    pending = [entry] if entry is not None else []
    while pending:
        node = pending.pop()
        if node in reachable:
            continue
        reachable.add(node)
        pending.extend(sorted(successors[node] - reachable, reverse=True))
    dominators = {
        node: ({node} if node == entry else set(reachable)) for node in reachable
    }
    changed = True
    while changed:
        changed = False
        for node in sorted(reachable):
            if node == entry:
                continue
            incoming = predecessors[node] & reachable
            common = set(reachable)
            if incoming:
                for predecessor in incoming:
                    common &= dominators[predecessor]
            else:
                common.clear()
            value = {node} | common
            if value != dominators[node]:
                dominators[node] = value
                changed = True
    dominator_rows = []
    for node in sorted(dominators):
        row: dict[str, object] = {
            "block": f"0x{node:08X}",
            "dominators": [f"0x{value:08X}" for value in sorted(dominators[node])],
        }
        if isinstance(by_address[node].get("evidence"), Mapping):
            row["evidence"] = dict(by_address[node]["evidence"])
        dominator_rows.append(row)
    backedges = [
        dict(edge)
        for edge in edges
        if isinstance(edge.get("to"), str)
        and _address(edge["to"]) in dominators.get(_address(edge["from"]), set())
    ]
    return {
        "basic_blocks": blocks,
        "edges": edges,
        "calls": sorted(calls, key=lambda row: (str(row["address"]), str(row["target"]))),
        "returns": sorted(returns, key=lambda row: str(row["address"])),
        "exits": sorted(exits, key=lambda row: (str(row["address"]), str(row["kind"]))),
        "loop_backedges": backedges,
        "dominators": dominator_rows,
        "unreachable_blocks": [
            f"0x{node:08X}" for node in sorted(nodes - reachable)
        ],
        "truncated": {
            "basic_blocks": block_limit_hit,
            "edges": edge_limit_hit,
        },
    }


def _sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_json(path: Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None


def _map_entries(root: Path) -> list[tuple[int, str]]:
    path = root / "tools/Resources/functions_map.txt"
    entries: list[tuple[int, str]] = []
    if not path.is_file():
        return entries
    seen: set[int] = set()
    try:
        lines = path.read_text(encoding="utf-8", errors="strict").splitlines()
    except (OSError, UnicodeError):
        return []
    for line in lines:
        fields = line.split(maxsplit=1)
        if not fields or fields[0].startswith("#"):
            continue
        try:
            address = _address(fields[0])
        except ContextError:
            return []
        if address in seen or len(fields) != 2 or not fields[1].strip():
            return []
        seen.add(address)
        entries.append((address, fields[1].strip()[:MAX_TEXT_CHARS]))
    return sorted(entries)


def _report_by_address(root: Path) -> dict[int, Mapping[str, object]]:
    document = _read_json(root / "build/decomp-current-report.json")
    rows = document.get("data", []) if isinstance(document, Mapping) else document
    result: dict[int, Mapping[str, object]] = {}
    for row in rows if isinstance(rows, list) else []:
        if not isinstance(row, Mapping):
            return {}
        try:
            address = _address(row.get("address"))
            matching = float(row.get("matching"))
        except (ContextError, TypeError, ValueError):
            return {}
        if address in result or not math.isfinite(matching) or not 0.0 <= matching <= 1.0:
            return {}
        result[address] = row
    return result


def _annotations_by_address(root: Path) -> dict[int, object]:
    result: dict[int, object] = {}
    for annotation in read_source_annotations(root / "src"):
        try:
            address = _address(annotation.address)
        except ContextError:
            return {}
        if address in result:
            return {}
        result[address] = annotation
    return result


def build_call_neighbors(
    root: Path,
    target: str | int,
    *,
    graph: DependencyGraph | None = None,
    entries: list[tuple[int, str]] | None = None,
    limit: int = MAX_CALL_NODES,
) -> list[dict[str, object]]:
    """Return deterministic depth-one direct-call neighbors."""

    if not 0 <= limit <= MAX_CALL_NODES:
        raise ContextError(f"the call-neighbor limit must be from 0 through {MAX_CALL_NODES}")
    target_address = _address(target)
    map_entries = entries if entries is not None else _map_entries(root)
    active_graph = graph
    if active_graph is None:
        try:
            active_graph = build_call_graph(map_entries)
        except DependencyUnavailable as error:
            raise ContextError(str(error)) from error
    names = dict(map_entries)
    annotations = _annotations_by_address(root)
    reports = _report_by_address(root)
    candidates = [
        (address, "caller")
        for address in active_graph.callers.get(target_address, frozenset())
        if address != target_address
    ] + [
        (address, "callee")
        for address in active_graph.callees.get(target_address, frozenset())
        if address != target_address
    ]
    result: list[dict[str, object]] = []
    for address, direction in sorted(set(candidates), key=lambda item: (item[0], item[1]))[:limit]:
        annotation = annotations.get(address)
        report = reports.get(address)
        matching: float | None = None
        status: str | None = None
        if report is not None:
            try:
                matching = float(report.get("matching"))
            except (TypeError, ValueError):
                matching = None
            status = (
                "exact"
                if matching == 1.0
                else "effective"
                if report.get("effective") is True
                else "provisional"
                if matching is not None
                else None
            )
        result.append(
            {
                "address": f"0x{address:08X}",
                "direction": direction,
                "name": names.get(address) or None,
                "source": (
                    f"src/{annotation.source}" if annotation is not None else None
                ),
                "state": annotation.kind.upper() if annotation is not None else None,
                "score": matching,
                "status": status,
                "evidence_tier": "retail-direct-call",
            }
        )
    return result


def bind_call_neighbor_evidence(
    neighbors: list[dict[str, object]],
    instructions: list[dict[str, object]],
    xrefs: object,
    *,
    target: str | int,
    xref_artifact: Mapping[str, object],
    entries: list[tuple[int, str]],
    function_sizes: Mapping[int, int],
) -> tuple[list[dict[str, object]], list[str]]:
    """Keep only neighbors supported by the target's doctor artifacts."""

    target_address = _address(target)
    outgoing: dict[int, dict[str, object]] = {}
    for row in instructions:
        if str(row.get("mnemonic", "")).startswith("call"):
            target = row.get("direct_target")
            if isinstance(target, str) and isinstance(row.get("evidence"), Mapping):
                outgoing.setdefault(_address(target), row)
    incoming: dict[int, tuple[int, dict[str, str]]] = {}
    ordered_entries = sorted(entries)
    names = dict(ordered_entries)
    next_address = {
        address: ordered_entries[index + 1][0]
        if index + 1 < len(ordered_entries)
        else None
        for index, (address, _name) in enumerate(ordered_entries)
    }
    for row in xrefs if isinstance(xrefs, list) else []:
        if not isinstance(row, Mapping) or "CALL" not in str(row.get("ref_type", "")).upper():
            continue
        try:
            call_site = _address(row.get("from"))
            xref_target = _address(row.get("to"))
        except ContextError:
            continue
        if xref_target != target_address:
            continue
        bounded_owners = [
            address
            for address, _name in ordered_entries
            if isinstance(function_sizes.get(address), int)
            and function_sizes[address] > 0
            and address <= call_site < address + function_sizes[address]
        ]
        from_function = row.get("from_function")
        named_owners = []
        if isinstance(from_function, str) and from_function.strip():
            artifact_name = from_function.strip()
            named_owners = [
                address
                for address, name in ordered_entries
                if name == artifact_name
                or name.rsplit("::", 1)[-1] == artifact_name.rsplit("::", 1)[-1]
            ]
        caller: int | None = None
        if len(bounded_owners) == 1:
            caller = bounded_owners[0]
            if named_owners and named_owners != [caller]:
                continue
        elif len(named_owners) == 1:
            candidate = named_owners[0]
            upper = next_address[candidate]
            if candidate <= call_site and upper is not None and call_site < upper:
                caller = candidate
        if caller is None or caller not in names:
            continue
        citation = _artifact_citation(
            "xrefs",
            xref_artifact,
            f"$.payload[?(@.from=='{call_site:08x}' && @.to=='{xref_target:08x}')]",
        )
        previous = incoming.get(caller)
        if previous is None or call_site < previous[0]:
            incoming[caller] = (call_site, citation)

    result: list[dict[str, object]] = []
    unknown: list[str] = []
    for neighbor in neighbors:
        address = _address(neighbor["address"])
        item = dict(neighbor)
        if item.get("direction") == "callee":
            instruction = outgoing.get(address)
            if instruction is None:
                unknown.append(f"unproven-call-neighbor:{address:08X}:callee")
                continue
            item["call_site"] = instruction["address"]
            item["evidence"] = dict(instruction["evidence"])
        else:
            caller = incoming.get(address)
            if caller is None:
                unknown.append(f"unproven-call-neighbor:{address:08X}:caller")
                continue
            item["call_site"] = f"0x{caller[0]:08X}"
            item["evidence"] = caller[1]
        result.append(item)
    return result, sorted(unknown)


def _ledger_rows(root: Path) -> list[dict[str, object]]:
    path = root / "tools/Resources/campaign-ledger.jsonl"
    rows: list[dict[str, object]] = []
    if not path.is_file():
        return rows
    try:
        lines = path.read_text(encoding="utf-8", errors="strict").splitlines()
    except (OSError, UnicodeError):
        return []
    for line in lines:
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            return []
        if not isinstance(row, dict):
            return []
        if row.get("schema_version") == 3:
            campaign_id = row.get("campaign_id")
            if (
                row.get("record_type", "campaign") not in {"campaign", "delivery"}
                or not isinstance(campaign_id, str)
                or not re.fullmatch(
                    r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", campaign_id
                )
            ):
                return []
        rows.append(row)
    return rows


def _snapshot_hash(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def _receipt_path(root: Path, value: object, directory: str) -> Path | None:
    if not isinstance(value, str) or not value:
        return None
    path = Path(value)
    candidate = path if path.is_absolute() else root / path
    if candidate.is_symlink():
        return None
    resolved = candidate.resolve()
    base = (root / "build/decomp-cache" / directory).resolve()
    try:
        resolved.relative_to(base)
    except ValueError:
        return None
    if not resolved.is_file():
        return None
    return resolved


def _validate_campaign_receipts(
    root: Path,
    campaign: Mapping[str, object],
    finalize: Mapping[str, object],
    delivery_path_value: object,
    delivery_hash: str,
    commit: str,
) -> tuple[dict[str, object], dict[str, object]] | None:
    raw_addresses = campaign.get("active_addresses")
    if not isinstance(raw_addresses, list):
        raw_addresses = campaign.get("addresses")
    try:
        active_addresses = [f"0x{_address(value):08X}" for value in raw_addresses]
    except (ContextError, TypeError):
        return None
    finalize_path = _receipt_path(root, finalize.get("path"), "finalize")
    saved_file_hash = finalize.get("file_sha256")
    saved_content_hash = finalize.get("content_sha256")
    receipt_key = finalize.get("receipt_key")
    if (
        finalize_path is None
        or not isinstance(saved_file_hash, str)
        or _sha256(finalize_path) != saved_file_hash
        or not isinstance(saved_content_hash, str)
        or not isinstance(receipt_key, str)
        or not re.fullmatch(r"[0-9a-f]{64}", receipt_key)
        or finalize_path.name != f"{receipt_key}.json"
    ):
        return None
    finalize_document = _read_json(finalize_path)
    if not isinstance(finalize_document, dict):
        return None
    unsigned_finalize = dict(finalize_document)
    unsigned_finalize.pop("receipt_sha256", None)
    key_payload = finalize_document.get("key_payload")
    source_snapshot = (
        key_payload.get("source_worktree_snapshot")
        if isinstance(key_payload, Mapping)
        else None
    )
    source_snapshot_hash = (
        key_payload.get("source_worktree_sha256")
        if isinstance(key_payload, Mapping)
        else None
    )
    repository_snapshot = (
        key_payload.get("repository_worktree_snapshot")
        if isinstance(key_payload, Mapping)
        else None
    )
    repository_snapshot_hash = (
        key_payload.get("repository_worktree_sha256")
        if isinstance(key_payload, Mapping)
        else None
    )
    if (
        finalize_document.get("schema_version") != 3
        or finalize_document.get("receipt_version") != 2
        or finalize_document.get("record_type") != "finalize-receipt"
        or finalize_document.get("status") != "passed"
        or finalize_document.get("receipt_sha256") != saved_content_hash
        or _snapshot_hash(unsigned_finalize) != saved_content_hash
        or finalize_document.get("receipt_key") != receipt_key
        or finalize_document.get("campaign_id") != campaign.get("campaign_id")
        or finalize_document.get("result") != "source"
        or finalize_document.get("mode") != campaign.get("mode")
        or finalize_document.get("lane") != campaign.get("lane")
        or not isinstance(key_payload, Mapping)
        or _snapshot_hash(key_payload) != receipt_key
        or key_payload.get("receipt_version") != 2
        or key_payload.get("campaign_id") != campaign.get("campaign_id")
        or key_payload.get("result") != "source"
        or key_payload.get("mode") != campaign.get("mode")
        or key_payload.get("lane") != campaign.get("lane")
        or key_payload.get("active_addresses", key_payload.get("addresses"))
        != active_addresses
        or not isinstance(source_snapshot, Mapping)
        or not isinstance(source_snapshot_hash, str)
        or not re.fullmatch(r"[0-9a-f]{64}", source_snapshot_hash)
        or _snapshot_hash(source_snapshot) != source_snapshot_hash
        or not isinstance(repository_snapshot, Mapping)
        or not isinstance(repository_snapshot_hash, str)
        or not re.fullmatch(r"[0-9a-f]{64}", repository_snapshot_hash)
        or _snapshot_hash(repository_snapshot) != repository_snapshot_hash
    ):
        return None

    delivery_path = _receipt_path(root, delivery_path_value, "delivery")
    if delivery_path is None or delivery_path.stem != delivery_hash:
        return None
    expected_delivery_parent = (
        root
        / "build/decomp-cache/delivery"
        / str(campaign.get("campaign_id"))
    ).resolve()
    if delivery_path.parent != expected_delivery_parent:
        return None
    delivery_document = _read_json(delivery_path)
    if not isinstance(delivery_document, dict):
        return None
    unsigned_delivery = dict(delivery_document)
    unsigned_delivery.pop("content_sha256", None)
    if (
        delivery_document.get("content_sha256") != delivery_hash
        or _snapshot_hash(unsigned_delivery) != delivery_hash
        or delivery_document.get("schema_version") != 3
        or delivery_document.get("receipt_version") != 2
        or delivery_document.get("record_type") != "delivery-receipt"
        or delivery_document.get("status") != "passed"
        or delivery_document.get("campaign_id") != campaign.get("campaign_id")
        or delivery_document.get("campaign_record_sha256") != _snapshot_hash(campaign)
        or delivery_document.get("source_commit") != commit
    ):
        return None
    return finalize_document, delivery_document


def _accepted_campaigns(
    root: Path, source_paths: Mapping[int, str]
) -> dict[int, dict[str, object]]:
    """Index source campaigns with one complete, unrejected delivery chain."""

    rows = _ledger_rows(root)
    by_campaign: dict[str, list[tuple[int, dict[str, object]]]] = {}
    for index, row in enumerate(rows):
        campaign_id = row.get("campaign_id")
        if isinstance(campaign_id, str) and campaign_id:
            by_campaign.setdefault(campaign_id, []).append((index, row))
    accepted: dict[int, dict[str, object]] = {}
    required = ("staged", "accepted", "integrated", "committed", "pushed")
    for campaign_id, records in sorted(by_campaign.items()):
        campaigns = [
            (index, row)
            for index, row in records
            if row.get("record_type", "campaign") == "campaign"
            and row.get("schema_version") == 3
            and row.get("result") == "source"
            and row.get("mode") in {"coverage", "refinement"}
        ]
        if len(campaigns) != 1:
            continue
        campaign_index, campaign = campaigns[0]
        raw_addresses = campaign.get("active_addresses")
        if not isinstance(raw_addresses, list):
            raw_addresses = campaign.get("addresses")
        try:
            active_addresses = [_address(value) for value in raw_addresses]
        except (ContextError, TypeError):
            continue
        if not active_addresses or len(active_addresses) != len(set(active_addresses)):
            continue
        deliveries = [
            (index, row)
            for index, row in records
            if row.get("record_type") == "delivery"
        ]
        if [row.get("status") for _, row in deliveries] != list(required):
            continue
        phase_by_status = {
            "staged": "staged",
            "accepted": "accepted",
            "integrated": "integrated",
            "committed": "commit",
            "pushed": "push",
        }
        delivery_ids = [row.get("delivery_id") for _, row in deliveries]
        try:
            delivery_addresses_match = all(
                isinstance(row.get("addresses"), list)
                and [_address(value) for value in row["addresses"]]
                == active_addresses
                for _, row in deliveries
            )
        except ContextError:
            delivery_addresses_match = False
        if (
            any(index <= campaign_index for index, _ in deliveries)
            or len(delivery_ids) != len(set(delivery_ids))
            or any(
                not isinstance(delivery_id, str)
                or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", delivery_id)
                for delivery_id in delivery_ids
            )
            or any(
                row.get("schema_version") != 3
                or row.get("phase") != phase_by_status[str(row.get("status"))]
                or row.get("mode") != campaign.get("mode")
                or row.get("lane") != campaign.get("lane")
                for _, row in deliveries
            )
            or not delivery_addresses_match
        ):
            continue
        chain = [row for _, row in deliveries]
        finalize = campaign.get("finalize_receipt")
        finalize_hash = (
            finalize.get("content_sha256") if isinstance(finalize, Mapping) else None
        )
        if (
            not isinstance(finalize_hash, str)
            or not re.fullmatch(r"[0-9a-f]{64}", finalize_hash)
            or any(row.get("receipt_sha256") != finalize_hash for row in chain[:2])
        ):
            continue
        delivery_hash = chain[2].get("receipt_sha256")
        delivery_path = chain[2].get("delivery_receipt_path")
        commit = chain[2].get("commit")
        if (
            not isinstance(delivery_hash, str)
            or not re.fullmatch(r"[0-9a-f]{64}", delivery_hash)
            or not isinstance(delivery_path, str)
            or not delivery_path
            or not isinstance(commit, str)
            or not re.fullmatch(r"[0-9a-f]{40,64}", commit)
            or any(row.get("receipt_sha256") != delivery_hash for row in chain[2:])
            or any(row.get("delivery_receipt_path") != delivery_path for row in chain[2:])
            or any(row.get("commit") != commit for row in chain[2:])
        ):
            continue
        validated_receipts = _validate_campaign_receipts(
            root,
            campaign,
            finalize,
            delivery_path,
            delivery_hash,
            commit,
        )
        if validated_receipts is None:
            continue
        finalize_document, delivery_document = validated_receipts
        key_payload = finalize_document["key_payload"]
        source_snapshot = key_payload["source_worktree_snapshot"]
        source_snapshot_hash = key_payload["source_worktree_sha256"]
        repository_snapshot = key_payload["repository_worktree_snapshot"]
        repository_snapshot_hash = key_payload["repository_worktree_sha256"]
        finalize_path = _receipt_path(root, finalize.get("path"), "finalize")
        delivery_path_resolved = _receipt_path(root, delivery_path, "delivery")
        if finalize_path is None or delivery_path_resolved is None:
            continue
        for address in active_addresses:
            source_path = source_paths.get(address)
            accepted_source_hash = (
                source_snapshot.get(source_path)
                if isinstance(source_path, str)
                else None
            )
            if (
                not isinstance(accepted_source_hash, str)
                or not re.fullmatch(r"[0-9a-f]{64}", accepted_source_hash)
                or repository_snapshot.get(source_path) != accepted_source_hash
            ):
                continue
            record = {
                "campaign_id": campaign_id,
                "campaign_record_index": campaign_index,
                "finalize_receipt": {
                    "path": finalize_path.relative_to(root).as_posix(),
                    "file_sha256": finalize.get("file_sha256"),
                    "content_sha256": finalize.get("content_sha256"),
                    "receipt_key": finalize.get("receipt_key"),
                },
                "delivery_receipt": {
                    "path": delivery_path_resolved.relative_to(root).as_posix(),
                    "sha256": delivery_hash,
                },
                "commit": commit,
                "ledger_locators": {
                    "campaign": f"line:{campaign_index + 1}",
                    "delivery": [f"line:{index + 1}" for index, _ in deliveries],
                },
                "delivery_ids": delivery_ids,
                "validated_receipts": {
                    "finalize_schema": finalize_document.get("schema_version"),
                    "delivery_schema": delivery_document.get("schema_version"),
                    "source_worktree_sha256": source_snapshot_hash,
                    "repository_worktree_sha256": repository_snapshot_hash,
                },
                "accepted_source_snapshot": {
                    "path": source_path,
                    "sha256": accepted_source_hash,
                    "json_locator": (
                        "$.key_payload.source_worktree_snapshot"
                        f"[{json.dumps(source_path)}]"
                    ),
                },
            }
            previous = accepted.get(address)
            if previous is None or campaign_index > int(previous["campaign_record_index"]):
                accepted[address] = record
    return accepted


def _function_sizes(root: Path) -> dict[int, int]:
    document = _read_json(root / "build/decomp-function-sizes.json")
    rows = document.get("data", []) if isinstance(document, Mapping) else document
    result: dict[int, int] = {}
    for row in rows if isinstance(rows, list) else []:
        if not isinstance(row, Mapping):
            continue
        try:
            address = _address(row.get("address"))
            size = int(row.get("original_size", row.get("size", 0)))
        except (ContextError, TypeError, ValueError):
            continue
        if size > 0:
            result[address] = size
    return result


def _tokens(value: str) -> tuple[str, ...]:
    separated = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", value)
    return tuple(
        sorted(
            {
                token.lower()
                for token in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", separated)
                if len(token) >= 2
            }
        )[:32]
    )


def _signature_tokens(root: Path, source: str, line: int) -> tuple[str, ...]:
    path = root / source
    if not path.is_file() or line <= 0:
        return ()
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    text = " ".join(lines[line : line + 8])
    return _tokens(text.split("{", 1)[0][:1000])


def _source_excerpt(root: Path, source: str, line: int) -> tuple[str, bool, str | None]:
    candidate = root / source
    if candidate.is_symlink():
        return "", False, "source-symlink"
    path = candidate.resolve()
    try:
        path.relative_to((root / "src").resolve())
    except ValueError:
        return "", False, "source-outside-root"
    if not path.is_file() or line <= 0:
        return "", False, "source-unavailable"
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    selected: list[str] = []
    depth = 0
    saw_body = False
    for source_line in lines[line : line + 80]:
        selected.append(source_line.rstrip())
        depth += source_line.count("{") - source_line.count("}")
        saw_body = saw_body or "{" in source_line
        if saw_body and depth <= 0:
            return "\n".join(selected)[:MAX_SOURCE_EXCERPT_CHARS], True, None
        if len("\n".join(selected)) >= MAX_SOURCE_EXCERPT_CHARS:
            return (
                "\n".join(selected)[:MAX_SOURCE_EXCERPT_CHARS],
                False,
                "source-excerpt-character-limit",
            )
    return (
        "\n".join(selected)[:MAX_SOURCE_EXCERPT_CHARS],
        False,
        "source-excerpt-line-limit" if selected else "source-excerpt-empty",
    )


def _size_bucket(size: int | None) -> str:
    if size is None or size <= 0:
        return "unknown"
    if size < 32:
        return "lt-32"
    if size < 128:
        return "32-127"
    if size < 512:
        return "128-511"
    if size < 2048:
        return "512-2047"
    return "ge-2048"


def build_accepted_sources(
    root: Path,
    target: str | int,
    *,
    graph: DependencyGraph | None = None,
    entries: list[tuple[int, str]] | None = None,
    debt_by_address: Mapping[int, object] | None = None,
) -> list[dict[str, object]]:
    """Retrieve at most three current terminal functions with accepted provenance."""

    root = root.resolve()
    target_address = _address(target)
    map_entries = entries if entries is not None else _map_entries(root)
    names = dict(map_entries)
    reports = _report_by_address(root)
    sizes = _function_sizes(root)
    annotations = _annotations_by_address(root)
    accepted = _accepted_campaigns(
        root,
        {
            address: f"src/{annotation.source}"
            for address, annotation in annotations.items()
        },
    )
    if debt_by_address is None:
        from tools.decomp_verify import read_source_debt

        debt_by_address = read_source_debt(root / "src")
    active_graph = graph
    if active_graph is None:
        try:
            active_graph = build_call_graph(map_entries)
        except DependencyUnavailable:
            active_graph = DependencyGraph({}, {}, {}, {})

    target_annotation = annotations.get(target_address)
    target_source = (
        f"src/{target_annotation.source}" if target_annotation is not None else None
    )
    target_name = names.get(target_address, "")
    target_namespace = target_name.rsplit("::", 1)[0] if "::" in target_name else ""
    target_name_tokens = set(_tokens(target_name))
    target_signature = set(
        _signature_tokens(
            root,
            target_source or "",
            target_annotation.line if target_annotation is not None else 0,
        )
    )
    target_bucket = _size_bucket(sizes.get(target_address))
    target_degrees = (
        len(active_graph.callers.get(target_address, frozenset())),
        len(active_graph.callees.get(target_address, frozenset())),
    )
    map_hash = _sha256(root / "tools/Resources/functions_map.txt")
    report_hash = _sha256(root / "build/decomp-current-report.json")
    ledger_hash = _sha256(root / "tools/Resources/campaign-ledger.jsonl")

    ranked: list[tuple[tuple[int, ...], int, dict[str, object]]] = []
    for address, annotation in sorted(annotations.items()):
        if address == target_address or annotation.kind != "function":
            continue
        report = reports.get(address)
        campaign = accepted.get(address)
        if report is None or campaign is None or address in debt_by_address:
            continue
        try:
            score = float(report.get("matching"))
        except (TypeError, ValueError):
            continue
        if not math.isfinite(score) or not 0.0 <= score <= 1.0:
            continue
        status = (
            "exact"
            if score == 1.0
            else "effective"
            if report.get("effective") is True
            else None
        )
        expected_tag = "matched" if status == "exact" else "effective"
        if status is None or annotation.tag != expected_tag:
            continue
        source = f"src/{annotation.source}"
        source_hash = _sha256(root / source)
        accepted_snapshot = campaign.get("accepted_source_snapshot")
        if (
            not isinstance(accepted_snapshot, Mapping)
            or accepted_snapshot.get("path") != source
            or accepted_snapshot.get("sha256") != source_hash
        ):
            continue
        excerpt, excerpt_complete, excerpt_reason = _source_excerpt(
            root, source, annotation.line
        )
        if source_hash is None or not excerpt:
            continue
        name = names.get(address, "")
        namespace = name.rsplit("::", 1)[0] if "::" in name else ""
        signature = set(_signature_tokens(root, source, annotation.line))
        name_overlap = len(target_name_tokens & set(_tokens(name)))
        signature_overlap = len(target_signature & signature)
        same_tu = int(bool(target_source) and source == target_source)
        same_namespace = int(bool(target_namespace) and namespace == target_namespace)
        same_bucket = int(
            target_bucket != "unknown" and _size_bucket(sizes.get(address)) == target_bucket
        )
        degrees = (
            len(active_graph.callers.get(address, frozenset())),
            len(active_graph.callees.get(address, frozenset())),
        )
        degree_distance = abs(target_degrees[0] - degrees[0]) + abs(
            target_degrees[1] - degrees[1]
        )
        rank = (
            same_tu,
            same_namespace,
            signature_overlap,
            name_overlap,
            same_bucket,
            -degree_distance,
        )
        explanation = (
            f"same_tu={same_tu}; same_namespace={same_namespace}; "
            f"signature_overlap={signature_overlap}; name_overlap={name_overlap}; "
            f"same_size_bucket={same_bucket}; call_degree_distance={degree_distance}"
        )
        ranked.append(
            (
                rank,
                address,
                {
                    "address": f"0x{address:08X}",
                    "name": name or None,
                    "source": source,
                    "line": annotation.line,
                    "state": "FUNCTION",
                    "score": score,
                    "status": status,
                    "size": sizes.get(address),
                    "source_excerpt": excerpt,
                    "source_excerpt_complete": excerpt_complete,
                    "source_excerpt_unknown_reason": excerpt_reason,
                    "rank_features": {
                        "same_translation_unit": bool(same_tu),
                        "same_namespace": bool(same_namespace),
                        "signature_token_overlap": signature_overlap,
                        "name_token_overlap": name_overlap,
                        "same_size_bucket": bool(same_bucket),
                        "call_degree_distance": degree_distance,
                    },
                    "rank_explanation": explanation,
                    "evidence_tier": "accepted-source",
                    "provenance": {
                        "ledger": {
                            "path": "tools/Resources/campaign-ledger.jsonl",
                            "sha256": ledger_hash,
                            "locator": f"line:{int(campaign['campaign_record_index']) + 1}",
                        },
                        "report": {
                            "path": "build/decomp-current-report.json",
                            "sha256": report_hash,
                            "json_locator": f"$.data[?(@.address=='0x{address:08X}')]",
                        },
                        "function_map": {
                            "path": "tools/Resources/functions_map.txt",
                            "sha256": map_hash,
                            "locator": f"address:0x{address:08X}",
                        },
                        "source": {
                            "path": source,
                            "sha256": source_hash,
                            "line": annotation.line,
                            "accepted_snapshot": dict(accepted_snapshot),
                        },
                        "campaign_id": campaign["campaign_id"],
                        "finalize_receipt": {
                            key: campaign["finalize_receipt"].get(key)
                            for key in (
                                "path",
                                "file_sha256",
                                "content_sha256",
                                "receipt_key",
                            )
                        },
                        "delivery_receipt": campaign["delivery_receipt"],
                        "commit": campaign["commit"],
                        "ledger_locators": campaign["ledger_locators"],
                        "delivery_ids": campaign["delivery_ids"],
                        "validated_receipts": campaign["validated_receipts"],
                    },
                },
            )
        )
    ranked.sort(key=lambda item: (tuple(-value for value in item[0]), item[1]))
    return [row for _rank, _address_value, row in ranked[:MAX_ACCEPTED_SOURCES]]


def _bounded_value(value: object, *, depth: int = 0) -> object:
    if depth >= 4:
        return str(value)[:MAX_TEXT_CHARS]
    if value is None or isinstance(value, (bool, int, float)):
        return value
    if isinstance(value, str):
        return value[:MAX_TEXT_CHARS]
    if isinstance(value, Mapping):
        return {
            str(key)[:80]: _bounded_value(child, depth=depth + 1)
            for key, child in sorted(value.items(), key=lambda item: str(item[0]))[:64]
        }
    if isinstance(value, (list, tuple)):
        return [_bounded_value(child, depth=depth + 1) for child in value[:64]]
    return str(value)[:MAX_TEXT_CHARS]


def context_input_descriptor(root: Path) -> dict[str, object]:
    """Return the cache identity for every context-pack producer input."""

    root = root.resolve()
    tools = (
        "tools/__init__.py",
        "tools/decomp_context.py",
        "tools/decomp_annotations.py",
        "tools/decomp_dependencies.py",
        "tools/decomp_binary.py",
        "tools/decomp_verify.py",
        "tools/decomp_lint.py",
        "tools/decomp_status.py",
        "tools/decomp_campaigns.py",
    )
    return {
        "decoder": _capstone_identity(),
        "retail_image": {
            "path": "original/toy2.exe",
            "sha256": _sha256(root / "original/toy2.exe"),
        },
        "repository": {
            "function_map_sha256": _sha256(root / "tools/Resources/functions_map.txt"),
            "function_sizes_sha256": _sha256(root / "build/decomp-function-sizes.json"),
            "report_sha256": _sha256(root / "build/decomp-current-report.json"),
            "ledger_sha256": _sha256(root / "tools/Resources/campaign-ledger.jsonl"),
            "lint_baseline_sha256": _sha256(root / ".notes/lint-baseline.tsv"),
        },
        "tools": {path: _sha256(root / path) for path in tools},
    }


def _doctor_binding(
    root: Path, doctor: Mapping[str, object]
) -> tuple[dict[str, object], Mapping[str, object]]:
    path_value = doctor.get("path")
    digest = doctor.get("sha256")
    receipt_id = doctor.get("receipt_id")
    artifacts = doctor.get("source_artifacts")
    if (
        not isinstance(path_value, str)
        or not isinstance(digest, str)
        or not re.fullmatch(r"[0-9a-f]{64}", digest)
        or not isinstance(receipt_id, str)
        or not re.fullmatch(r"[0-9a-f]{64}", receipt_id)
        or not isinstance(artifacts, Mapping)
    ):
        raise ContextError("the doctor receipt descriptor is incomplete")
    path = Path(path_value)
    candidate = path if path.is_absolute() else root / path
    if candidate.is_symlink():
        raise ContextError("the doctor receipt path is a symbolic link")
    resolved = candidate.resolve()
    try:
        relative = resolved.relative_to(root)
    except ValueError as error:
        raise ContextError("the doctor receipt path is outside the repository") from error
    if not resolved.is_file() or _sha256(resolved) != digest:
        raise ContextError("the immutable doctor receipt changed")
    return (
        {
            "path": relative.as_posix(),
            "sha256": digest,
            "receipt_id": receipt_id,
        },
        artifacts,
    )


def _encoded_size(value: object) -> int:
    return len(
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode(
            "utf-8"
        )
    )


def context_pack_hash(pack: Mapping[str, object]) -> str:
    content = dict(pack)
    content.pop("content_sha256", None)
    return _snapshot_hash(content)


def build_context_pack(
    root: Path,
    target: str | int,
    *,
    doctor: Mapping[str, object],
    evidence: Mapping[str, object],
    size: int | None = None,
    decoder: Decoder | None = None,
    graph: DependencyGraph | None = None,
    debt_by_address: Mapping[int, object] | None = None,
) -> dict[str, object]:
    """Build one bounded pack from immutable doctor and repository evidence."""

    root = root.resolve()
    target_address = _address(target)
    target_text = f"0x{target_address:08X}"
    known_size = _function_sizes(root).get(target_address)
    if size is None:
        size = known_size
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
        raise ContextError("the target has no valid retail size")
    if known_size is not None and size != known_size:
        raise ContextError("the target size does not match the function-size input")
    doctor_binding, artifacts = _doctor_binding(root, doctor)
    artifact_payloads: dict[str, object] = {}
    for kind, descriptor in sorted(artifacts.items()):
        try:
            artifact_payloads[str(kind)] = load_ghidra_artifact(
                root, target_text, str(kind), descriptor
            )
        except ValueError as error:
            raise ContextError(f"the {kind} artifact is invalid: {error}") from error
    disassembly = artifacts.get("disassembly")
    if not isinstance(disassembly, Mapping):
        raise ContextError("the doctor receipt has no disassembly artifact")
    normalized = normalize_artifact(
        root, target_text, size, disassembly, decoder=decoder
    )
    instructions = [dict(row) for row in normalized["instructions"]]
    for row in instructions:
        row.pop("memory", None)
    memory = [dict(row) for row in normalized["memory_operands"]]
    entries = _map_entries(root)
    active_graph = graph
    reasons = set(normalized["completeness"]["unknown_reasons"])
    dependency_complete = True
    if not entries:
        dependency_complete = False
        reasons.add("function-map-unavailable")
    if active_graph is None:
        try:
            active_graph = build_call_graph(entries)
        except DependencyUnavailable:
            active_graph = DependencyGraph({}, {}, {}, {})
            dependency_complete = False
            reasons.add("dependency-graph-unavailable")
    if active_graph.indirect_calls.get(target_address, 0):
        dependency_complete = False
        reasons.add(f"indirect-call-count:{active_graph.indirect_calls[target_address]}")
    if active_graph.indirect_jumps.get(target_address, 0):
        dependency_complete = False
        reasons.add(f"indirect-jump-count:{active_graph.indirect_jumps[target_address]}")
    neighbor_total = len(active_graph.callers.get(target_address, frozenset())) + len(
        active_graph.callees.get(target_address, frozenset())
    )
    if neighbor_total > MAX_CALL_NODES:
        dependency_complete = False
        reasons.add(f"call-neighbor-limit:{neighbor_total}")
    neighbors = build_call_neighbors(
        root, target_address, graph=active_graph, entries=entries
    )
    xref_artifact = artifacts.get("xrefs")
    if isinstance(xref_artifact, Mapping):
        neighbors, neighbor_reasons = bind_call_neighbor_evidence(
            neighbors,
            list(normalized["instructions"]),
            artifact_payloads.get("xrefs"),
            target=target_address,
            xref_artifact=xref_artifact,
            entries=entries,
            function_sizes=_function_sizes(root),
        )
        if neighbor_reasons:
            dependency_complete = False
            reasons.update(neighbor_reasons)
    elif neighbors:
        neighbors = []
        dependency_complete = False
        reasons.add("xrefs-artifact-unavailable")
    accepted_sources = build_accepted_sources(
        root,
        target_address,
        graph=active_graph,
        entries=entries,
        debt_by_address=debt_by_address,
    )
    base_completeness = dict(normalized["completeness"])

    def assemble(selected: list[dict[str, object]]) -> dict[str, object]:
        selected_addresses = {_address(row["address"]) for row in selected}
        selected_memory = [
            row for row in memory if _address(row["instruction"]) in selected_addresses
        ]
        control_flow = build_control_flow(selected, target=target_text, size=size)
        current_reasons = set(reasons)
        truncation = control_flow["truncated"]
        if truncation["basic_blocks"]:
            current_reasons.add("basic-block-limit")
        if truncation["edges"]:
            current_reasons.add("edge-limit")
        for block in control_flow["unreachable_blocks"]:
            current_reasons.add(f"unreachable-block:{str(block)[2:]}")
        for exit_row in control_flow["exits"]:
            if exit_row.get("kind") == "fall-off":
                current_reasons.add(f"fall-off-exit:{str(exit_row['address'])[2:]}")
            elif exit_row.get("kind") == "external-tail-jump":
                current_reasons.add(
                    f"external-tail-jump:{str(exit_row['address'])[2:]}"
                )
        completeness = dict(base_completeness)
        completeness["dependencies"] = dependency_complete
        displayed_reasons = sorted(current_reasons)[:MAX_UNKNOWN_REASONS]
        if len(current_reasons) > MAX_UNKNOWN_REASONS:
            displayed_reasons[-1] = f"reason-limit:{len(current_reasons)}"
        completeness["unknown_reasons"] = displayed_reasons
        completeness["complete"] = not current_reasons
        if (
            truncation["basic_blocks"]
            or truncation["edges"]
            or control_flow["unreachable_blocks"]
            or control_flow["exits"]
        ):
            completeness["control_flow"] = False
        pack: dict[str, object] = {
            "schema": CONTEXT_PACK_SCHEMA,
            "target": target_text,
            "abi": _bounded_value(evidence.get("abi")),
            "size": size,
            "instructions": selected,
            "control_flow": control_flow,
            "memory_operands": selected_memory,
            "call_neighbors": neighbors,
            "accepted_sources": accepted_sources,
            "completeness": completeness,
            "bindings": {
                "doctor_receipt": doctor_binding,
                "artifacts": {
                    str(kind): dict(descriptor)
                    for kind, descriptor in sorted(artifacts.items())
                    if isinstance(descriptor, Mapping)
                },
                "evidence_sources": {
                    str(kind): {
                        "path": descriptor.get("path"),
                        "sha256": descriptor.get("sha256"),
                    }
                    for kind, descriptor in sorted(artifacts.items())
                    if isinstance(descriptor, Mapping)
                },
                "context_inputs": context_input_descriptor(root),
                "decoder": normalized["decoder"],
            },
            "limits": {
                "instructions": MAX_INSTRUCTIONS,
                "basic_blocks": MAX_BASIC_BLOCKS,
                "edges": MAX_EDGES,
                "memory_operands": MAX_MEMORY_OPERANDS,
                "call_neighbors": MAX_CALL_NODES,
                "accepted_sources": MAX_ACCEPTED_SOURCES,
                "text_chars": MAX_TEXT_CHARS,
                "source_excerpt_chars": MAX_SOURCE_EXCERPT_CHARS,
                "bytes": MAX_CONTEXT_BYTES,
            },
            "content_sha256": "0" * 64,
        }
        return pack

    pack = assemble(instructions)
    while _encoded_size(pack) > MAX_CONTEXT_BYTES and len(instructions) > 1:
        keep = max(1, len(instructions) - max(1, len(instructions) // 8))
        instructions = instructions[:keep]
        reasons.add("context-byte-limit")
        base_completeness["complete"] = False
        base_completeness["instruction_bytes"] = False
        base_completeness["control_flow"] = False
        pack = assemble(instructions)
    if _encoded_size(pack) > MAX_CONTEXT_BYTES:
        raise ContextError("the context pack cannot fit its hard byte limit")
    pack["content_sha256"] = context_pack_hash(pack)
    validate_context_pack(pack, target=target_text, size=size)
    return pack


def validate_context_pack(
    pack: object,
    *,
    target: str | int,
    size: int,
) -> dict[str, object]:
    """Validate the bounded context schema and its content hash."""

    if not isinstance(pack, dict) or pack.get("schema") != CONTEXT_PACK_SCHEMA:
        raise ContextError("the context pack schema is invalid")
    target_text = f"0x{_address(target):08X}"
    if pack.get("target") != target_text:
        raise ContextError("the context pack target does not match")
    if pack.get("size") != size or not isinstance(size, int) or size <= 0:
        raise ContextError("the context pack size does not match")
    saved_hash = pack.get("content_sha256")
    if (
        not isinstance(saved_hash, str)
        or not re.fullmatch(r"[0-9a-f]{64}", saved_hash)
        or saved_hash != context_pack_hash(pack)
    ):
        raise ContextError("the context pack content hash does not match")
    if _encoded_size(pack) > MAX_CONTEXT_BYTES:
        raise ContextError("the context pack exceeds its byte limit")
    instructions = pack.get("instructions")
    memory = pack.get("memory_operands")
    neighbors = pack.get("call_neighbors")
    examples = pack.get("accepted_sources")
    graph = pack.get("control_flow")
    completeness = pack.get("completeness")
    bindings = pack.get("bindings")
    if (
        not isinstance(instructions, list)
        or len(instructions) > MAX_INSTRUCTIONS
        or not isinstance(memory, list)
        or len(memory) > MAX_MEMORY_OPERANDS
        or not isinstance(neighbors, list)
        or len(neighbors) > MAX_CALL_NODES
        or not isinstance(examples, list)
        or len(examples) > MAX_ACCEPTED_SOURCES
        or not isinstance(graph, dict)
        or not isinstance(completeness, dict)
        or not isinstance(bindings, dict)
    ):
        raise ContextError("the context pack has an invalid bounded section")
    addresses = [_address(row.get("address")) for row in instructions if isinstance(row, Mapping)]
    if len(addresses) != len(instructions) or addresses != sorted(set(addresses)):
        raise ContextError("the normalized instructions are not unique and sorted")
    blocks = graph.get("basic_blocks")
    edges = graph.get("edges")
    if (
        not isinstance(blocks, list)
        or len(blocks) > MAX_BASIC_BLOCKS
        or not isinstance(edges, list)
        or len(edges) > MAX_EDGES
    ):
        raise ContextError("the context control-flow graph exceeds its limits")
    reasons = completeness.get("unknown_reasons")
    if not isinstance(reasons, list) or len(reasons) > MAX_UNKNOWN_REASONS:
        raise ContextError("the context completeness reasons are invalid")
    if completeness.get("complete") is True and reasons:
        raise ContextError("a complete context pack has unknown reasons")
    decoder_identity = bindings.get("decoder")
    if (
        not isinstance(decoder_identity, Mapping)
        or decoder_identity.get("architecture") != "x86"
        or decoder_identity.get("mode") != 32
        or decoder_identity.get("pinned_version") != PINNED_CAPSTONE_VERSION
    ):
        raise ContextError("the context decoder identity is invalid")
    return pack


def load_context_from_brief(
    path: Path,
    *,
    root: Path,
    validate_repository: bool = True,
) -> dict[str, object]:
    """Read and validate a context pack without changing repository state."""

    root = root.resolve()
    candidate = path if path.is_absolute() else root / path
    if candidate.is_symlink():
        raise ContextError("the brief path is a symbolic link")
    resolved = candidate.resolve()
    cache = (root / "build/decomp-cache/briefs").resolve()
    try:
        resolved.relative_to(cache)
    except ValueError as error:
        raise ContextError("the brief is outside the immutable brief cache") from error
    if not resolved.is_file():
        raise ContextError("the brief does not exist")
    if resolved.stat().st_size > MAX_BRIEF_BYTES:
        raise ContextError("the brief exceeds its byte limit")
    document = _read_json(resolved)
    if not isinstance(document, dict) or document.get("schema") != 1:
        raise ContextError("the brief schema is invalid")
    saved_brief_hash = document.get("content_sha256")
    unsigned_brief = dict(document)
    unsigned_brief.pop("content_sha256", None)
    if (
        not isinstance(saved_brief_hash, str)
        or saved_brief_hash != _snapshot_hash(unsigned_brief)
    ):
        raise ContextError("the brief content hash does not match")
    target = document.get("target")
    lane = document.get("lane")
    if not isinstance(target, str) or lane not in {"closure", "production", "research"}:
        raise ContextError("the brief is not a function brief")
    current_size = _function_sizes(root).get(_address(target))
    if current_size is None:
        raise ContextError("the brief target has no current retail size")
    evidence = document.get("evidence")
    pack = evidence.get("context_pack") if isinstance(evidence, Mapping) else None
    validate_context_pack(pack, target=target, size=current_size)
    if validate_repository:
        inputs = document.get("inputs")
        doctor = inputs.get("doctor_receipt") if isinstance(inputs, Mapping) else None
        doctor_path = doctor.get("path") if isinstance(doctor, Mapping) else None
        if not isinstance(doctor_path, str):
            raise ContextError("the function brief has no bound doctor receipt")
        try:
            from tools.decomp_brief import BriefError, validate_brief

            validate_brief(
                resolved,
                lane,
                target,
                root=root,
                doctor_receipt_path=Path(doctor_path),
            )
        except BriefError as error:
            raise ContextError(f"the immutable brief is invalid: {error}") from error
    return pack


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--brief", type=Path, required=True)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1], help=argparse.SUPPRESS)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        pack = load_context_from_brief(arguments.brief, root=arguments.root)
    except ContextError as error:
        if arguments.json:
            print(json.dumps({"ok": False, "error": str(error)}, sort_keys=True))
        else:
            print(f"context failed: {error}", file=sys.stderr)
        return 1
    if arguments.json:
        print(json.dumps({"ok": True, "context_pack": pack}, indent=2, sort_keys=True))
    else:
        graph = pack["control_flow"]
        completeness = pack["completeness"]
        print(f"context: {pack['target']} ({pack['size']} bytes)")
        print(
            "instructions: "
            f"{len(pack['instructions'])}; blocks: {len(graph['basic_blocks'])}; "
            f"edges: {len(graph['edges'])}"
        )
        print(
            f"neighbors: {len(pack['call_neighbors'])}; "
            f"accepted sources: {len(pack['accepted_sources'])}"
        )
        print(f"complete: {str(bool(completeness.get('complete'))).lower()}")
        for reason in completeness.get("unknown_reasons", []):
            print(f"  unknown: {reason}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
