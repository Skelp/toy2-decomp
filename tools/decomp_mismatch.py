#!/usr/bin/env python3
"""Classify direct instruction signals in reccmp mismatch data."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Iterable, Mapping, Sequence


SCHEMA_VERSION = 1
ROUTE_ORDER = (
    "abi",
    "call",
    "control-flow",
    "integer",
    "memory",
    "stack",
    "floating-point",
    "side-effect",
    "codegen",
    "instruction",
)
DEFAULT_EVIDENCE_LIMIT = 4
MAX_EVIDENCE_LIMIT = 8
MAX_INSTRUCTION_CHARS = 180

_REGISTER_RE = re.compile(
    r"\b(?:e(?:ax|bx|cx|dx|si|di|sp|bp)|(?:ax|bx|cx|dx|si|di|sp|bp)|"
    r"[abcd][lh]|st\([0-7]\))\b",
    re.IGNORECASE,
)
_STACK_RE = re.compile(r"\b(?:esp|ebp|sp|bp)\b", re.IGNORECASE)
_IMMEDIATE_RE = re.compile(r"(?<![A-Za-z0-9_])[-+]?(?:0x[0-9a-f]+|\d+)(?![A-Za-z0-9_])", re.IGNORECASE)
_MEMORY_RE = re.compile(r"\[[^\]]+\]|\((?:data|offset|unk)\)|<offset\d*>", re.IGNORECASE)
_PREFIXES = {"lock", "rep", "repe", "repz", "repne", "repnz"}
_ABI_OPCODES = {"enter", "leave", "ret", "retn", "retf", "iret", "iretd"}
_STACK_OPCODES = {"push", "pop", "pushad", "popad", "pushfd", "popfd"}
_CONDITION_OPCODES = {"cmp", "test"}
_INTEGER_OPCODES = {
    "adc",
    "add",
    "and",
    "bsf",
    "bsr",
    "bt",
    "btc",
    "btr",
    "bts",
    "cbw",
    "cdq",
    "cwd",
    "cwde",
    "dec",
    "div",
    "idiv",
    "imul",
    "inc",
    "mul",
    "neg",
    "not",
    "or",
    "rol",
    "ror",
    "sal",
    "sar",
    "sbb",
    "shl",
    "shr",
    "sub",
    "xor",
}
_MEMORY_WRITE_OPCODES = {
    "adc",
    "add",
    "and",
    "btc",
    "btr",
    "bts",
    "cmpxchg",
    "dec",
    "fst",
    "fstp",
    "inc",
    "mov",
    "neg",
    "not",
    "or",
    "pop",
    "sbb",
    "sub",
    "xadd",
    "xchg",
    "xor",
}
_DESCRIPTIONS = {
    "abi": "A changed return or frame-boundary instruction is present.",
    "call": "A changed call instruction is present.",
    "control-flow": "A changed branch, loop, or condition instruction is present.",
    "integer": "A changed integer operation or immediate operand is present.",
    "memory": "A changed non-stack memory-form operand is present.",
    "stack": "A changed stack instruction or stack-relative operand is present.",
    "floating-point": "A changed x87 instruction is present.",
    "side-effect": "A changed instruction writes to memory.",
    "codegen": "A changed instruction differs only in register operands.",
    "instruction": "An instruction change has no more specific direct signal.",
}


@dataclass(frozen=True)
class _Row:
    locator: str
    address: str
    instruction: str


@dataclass(frozen=True)
class _Change:
    change: str
    original: _Row | None = None
    recompiled: _Row | None = None


def empty_taxonomy() -> dict[str, object]:
    """Return an empty schema-version-1 taxonomy."""

    return {
        "schema_version": SCHEMA_VERSION,
        "primary_route": None,
        "signals": [],
        "row_changes": {"replace": 0, "insert": 0, "delete": 0},
    }


def classify_report_diff(
    diff: object, *, evidence_limit: int = DEFAULT_EVIDENCE_LIMIT
) -> dict[str, object]:
    """Classify changed rows from a comparison-report diff."""

    return _classify(_report_changes(diff), evidence_limit=evidence_limit)


def classify_text_diff(
    lines: Sequence[str], *, evidence_limit: int = DEFAULT_EVIDENCE_LIMIT
) -> dict[str, object]:
    """Classify changed rows from a saved verbose text diff."""

    changes: list[_Change] = []
    for line_index, line in enumerate(lines, start=1):
        parsed = _text_change(line, line_index)
        if parsed is not None:
            changes.append(parsed)
    return _classify(changes, evidence_limit=evidence_limit)


def legacy_candidate_classifications(lines: Sequence[str]) -> tuple[str, ...]:
    """Return the legacy candidate labels without a behavior change."""

    text = "\n".join(lines).lower()
    classes: list[str] = []
    if any(
        token in text
        for token in (" jmp ", " je ", " jne ", " jg ", " jl ", " call ", " ret ")
    ):
        classes.append("control-flow")
    if "[" in text or "(data)" in text or "(offset)" in text:
        classes.append("data-access")
    if " esp" in text or " ebp" in text:
        classes.append("stack-frame")
    if any(token in text for token in (" fld", " fst", " fmul", " fadd", " fsub")):
        classes.append("floating-point")
    return tuple(classes or ("instruction",))


def _bounded_limit(value: int) -> int:
    try:
        requested = int(value)
    except (TypeError, ValueError):
        requested = DEFAULT_EVIDENCE_LIMIT
    return max(1, min(requested, MAX_EVIDENCE_LIMIT))


def _classify(
    changes: Iterable[_Change], *, evidence_limit: int
) -> dict[str, object]:
    rows = list(changes)
    if not rows:
        return empty_taxonomy()
    limit = _bounded_limit(evidence_limit)
    matches: dict[str, list[_Change]] = {route: [] for route in ROUTE_ORDER}
    change_counts = {"replace": 0, "insert": 0, "delete": 0}
    for change in rows:
        change_counts[change.change] += 1
        routes = _routes(change)
        for route in routes or {"instruction"}:
            matches[route].append(change)
    signals: list[dict[str, object]] = []
    for route in ROUTE_ORDER:
        matched = matches[route]
        if not matched:
            continue
        evidence = [_evidence(change) for change in matched[:limit]]
        signals.append(
            {
                "route": route,
                "description": _DESCRIPTIONS[route],
                "changed_rows": len(matched),
                "evidence": evidence,
                "evidence_truncated": len(matched) > len(evidence),
            }
        )
    return {
        "schema_version": SCHEMA_VERSION,
        "primary_route": signals[0]["route"],
        "signals": signals,
        "row_changes": change_counts,
    }


def _report_changes(diff: object) -> list[_Change]:
    changes: list[_Change] = []
    if not isinstance(diff, list):
        return changes
    for hunk_index, hunk in enumerate(diff):
        if not isinstance(hunk, (list, tuple)) or len(hunk) != 2:
            continue
        groups = hunk[1]
        if not isinstance(groups, list):
            continue
        for group_index, group in enumerate(groups):
            if not isinstance(group, Mapping):
                continue
            original = _report_rows(group.get("orig"), hunk_index, group_index, "orig")
            recompiled = _report_rows(
                group.get("recomp"), hunk_index, group_index, "recomp"
            )
            paired = min(len(original), len(recompiled))
            changes.extend(
                _Change("replace", original[index], recompiled[index])
                for index in range(paired)
            )
            changes.extend(_Change("delete", row, None) for row in original[paired:])
            changes.extend(_Change("insert", None, row) for row in recompiled[paired:])
    return changes


def _report_rows(
    value: object, hunk_index: int, group_index: int, side: str
) -> list[_Row]:
    if not isinstance(value, list):
        return []
    rows: list[_Row] = []
    for row_index, value_row in enumerate(value):
        if not isinstance(value_row, (list, tuple)) or len(value_row) < 2:
            continue
        rows.append(
            _Row(
                locator=f"/diff/{hunk_index}/1/{group_index}/{side}/{row_index}",
                address=str(value_row[0])[:32],
                instruction=_instruction_text(str(value_row[1])),
            )
        )
    return rows


def _text_change(line: str, line_index: int) -> _Change | None:
    stripped = line.lstrip()
    if stripped.startswith(("---", "+++", "@@")):
        return None
    address = ""
    marker = ""
    instruction = ""
    if ":" in line:
        prefix, payload = line.split(":", 1)
        payload = payload.lstrip()
        if payload.startswith(("-", "+")):
            marker = payload[0]
            instruction = payload[1:]
            address_match = re.search(r"0x[0-9a-f]+", prefix, re.IGNORECASE)
            address = address_match.group(0) if address_match else ""
    elif stripped.startswith(("-", "+")):
        marker = stripped[0]
        instruction = stripped[1:]
    if not marker:
        return None
    row = _Row(
        locator=f"/lines/{line_index}",
        address=address,
        instruction=_instruction_text(instruction),
    )
    return _Change("delete", row, None) if marker == "-" else _Change("insert", None, row)


def _instruction_text(value: str) -> str:
    return value.split("\t", 1)[0].strip()[:MAX_INSTRUCTION_CHARS]


def _instruction_parts(value: str) -> tuple[str, str]:
    parts = value.split()
    if not parts:
        return "", ""
    opcode_index = 0
    while opcode_index < len(parts) - 1 and parts[opcode_index].lower() in _PREFIXES:
        opcode_index += 1
    opcode = parts[opcode_index].lower()
    operands = " ".join(parts[opcode_index + 1 :])
    return opcode, operands


def _routes(change: _Change) -> set[str]:
    values = [
        row.instruction
        for row in (change.original, change.recompiled)
        if row is not None and row.instruction
    ]
    parts = [_instruction_parts(value) for value in values]
    opcodes = {opcode for opcode, _ in parts if opcode}
    routes: set[str] = set()
    if opcodes & _ABI_OPCODES:
        routes.add("abi")
    if any(opcode.startswith("call") for opcode in opcodes):
        routes.add("call")
    if any(
        opcode == "jmp"
        or opcode.startswith("j")
        or opcode.startswith("loop")
        or opcode in _CONDITION_OPCODES
        or opcode.startswith("set")
        or opcode.startswith("cmov")
        for opcode in opcodes
    ):
        routes.add("control-flow")
    if any(opcode.startswith("f") for opcode in opcodes):
        routes.add("floating-point")
    control_transfer = any(
        opcode in _ABI_OPCODES
        or opcode.startswith("call")
        or opcode == "jmp"
        or opcode.startswith("j")
        or opcode.startswith("loop")
        for opcode in opcodes
    )
    if (opcodes & _INTEGER_OPCODES) or (
        _changed_immediate(values) and not control_transfer
    ):
        routes.add("integer")
    stack_form = bool(opcodes & _STACK_OPCODES) or any(_STACK_RE.search(value) for value in values)
    if stack_form and not (opcodes & _ABI_OPCODES):
        routes.add("stack")
    if any(_MEMORY_RE.search(value) for value in values) and not stack_form:
        routes.add("memory")
    if any(_writes_memory(opcode, operands) for opcode, operands in parts):
        routes.add("side-effect")
    if not routes and _register_only_change(change):
        routes.add("codegen")
    return routes


def _changed_immediate(values: Sequence[str]) -> bool:
    immediate_sets = [tuple(_IMMEDIATE_RE.findall(value)) for value in values]
    return bool(any(immediate_sets) and len(set(immediate_sets)) > 1)


def _writes_memory(opcode: str, operands: str) -> bool:
    if opcode.startswith(("stos", "movs")):
        return True
    destination = operands.split(",", 1)[0]
    return opcode in _MEMORY_WRITE_OPCODES and bool(_MEMORY_RE.search(destination))


def _register_only_change(change: _Change) -> bool:
    if change.original is None or change.recompiled is None:
        return False
    original = change.original.instruction
    recompiled = change.recompiled.instruction
    if original == recompiled:
        return False
    original_opcode, _ = _instruction_parts(original)
    recompiled_opcode, _ = _instruction_parts(recompiled)
    if original_opcode != recompiled_opcode:
        return False
    if _REGISTER_RE.findall(original) == _REGISTER_RE.findall(recompiled):
        return False
    return _REGISTER_RE.sub("<reg>", original.lower()) == _REGISTER_RE.sub(
        "<reg>", recompiled.lower()
    )


def _evidence(change: _Change) -> dict[str, object]:
    result: dict[str, object] = {"change": change.change}
    if change.original is not None:
        result["original"] = _row_evidence(change.original)
    if change.recompiled is not None:
        result["recompiled"] = _row_evidence(change.recompiled)
    return result


def _row_evidence(row: _Row) -> dict[str, str]:
    result = {"locator": row.locator, "instruction": row.instruction}
    if row.address:
        result["address"] = row.address
    return result
