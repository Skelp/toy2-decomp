#!/usr/bin/env python3
"""Extract direct retail call dependencies without a Ghidra query."""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

from tools import decomp_binary


class DependencyUnavailable(RuntimeError):
    """The local retail binary or disassembler is not available."""


@dataclass(frozen=True)
class DependencyGraph:
    callees: dict[int, frozenset[int]]
    callers: dict[int, frozenset[int]]
    indirect_calls: dict[int, int]
    indirect_jumps: dict[int, int]


def _capstone():
    try:
        from capstone import CS_ARCH_X86, CS_MODE_32, Cs
        from capstone.x86 import X86_OP_IMM
    except ImportError as error:
        raise DependencyUnavailable(
            "Capstone is unavailable. Run the repository setup command first."
        ) from error
    disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
    disassembler.detail = True
    return disassembler, X86_OP_IMM


def build_call_graph(
    entries: list[tuple[int, str]],
) -> DependencyGraph:
    """Return direct calls and tail jumps between mapped retail functions."""

    if not decomp_binary.available():
        raise DependencyUnavailable(
            "The retail executable is unavailable. Run the repository setup command first."
        )

    disassembler, immediate_operand = _capstone()
    mapped = {address for address, _ in entries}
    callees: dict[int, set[int]] = defaultdict(set)
    indirect_calls: dict[int, int] = defaultdict(int)
    indirect_jumps: dict[int, int] = defaultdict(int)

    text_end = max(
        (
            section.virtual_address + section.raw_size
            for section in decomp_binary.sections()
            if section.name == ".text"
        ),
        default=entries[-1][0] if entries else 0,
    )
    for index, (address, _) in enumerate(entries):
        next_address = entries[index + 1][0] if index + 1 < len(entries) else text_end
        size = next_address - address
        if size <= 0:
            continue
        code = decomp_binary.read_bytes(address, size)
        if code is None:
            continue
        for instruction in disassembler.disasm(code, address):
            mnemonic = instruction.mnemonic.lower()
            if mnemonic not in ("call", "jmp"):
                continue
            operands = instruction.operands
            if not operands or operands[0].type != immediate_operand:
                if mnemonic == "call":
                    indirect_calls[address] += 1
                else:
                    indirect_jumps[address] += 1
                continue
            target = operands[0].imm & 0xFFFFFFFF
            if target in mapped and target != address:
                callees[address].add(target)

    callers: dict[int, set[int]] = defaultdict(set)
    for caller, targets in callees.items():
        for target in targets:
            callers[target].add(caller)

    return DependencyGraph(
        callees={address: frozenset(targets) for address, targets in callees.items()},
        callers={address: frozenset(sources) for address, sources in callers.items()},
        indirect_calls=dict(indirect_calls),
        indirect_jumps=dict(indirect_jumps),
    )


def strongly_connected_components(
    nodes: set[int], edges: dict[int, set[int]]
) -> tuple[dict[int, int], list[frozenset[int]]]:
    """Collapse recursive dependency groups with Tarjan's algorithm."""

    next_index = 0
    stack: list[int] = []
    on_stack: set[int] = set()
    indices: dict[int, int] = {}
    lowlinks: dict[int, int] = {}
    components: list[frozenset[int]] = []

    def visit(node: int) -> None:
        nonlocal next_index
        indices[node] = next_index
        lowlinks[node] = next_index
        next_index += 1
        stack.append(node)
        on_stack.add(node)

        for target in edges.get(node, set()):
            if target not in nodes:
                continue
            if target not in indices:
                visit(target)
                lowlinks[node] = min(lowlinks[node], lowlinks[target])
            elif target in on_stack:
                lowlinks[node] = min(lowlinks[node], indices[target])

        if lowlinks[node] != indices[node]:
            return
        members = []
        while stack:
            member = stack.pop()
            on_stack.remove(member)
            members.append(member)
            if member == node:
                break
        components.append(frozenset(members))

    for node in sorted(nodes):
        if node not in indices:
            visit(node)

    component_by_node = {
        node: component_index
        for component_index, component in enumerate(components)
        for node in component
    }
    return component_by_node, components
