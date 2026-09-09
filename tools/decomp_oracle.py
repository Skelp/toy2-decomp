#!/usr/bin/env python3
"""Run finite differential checks for allowlisted IA-32 leaf functions."""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import tempfile
from typing import Mapping, Sequence

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import decomp_binary  # noqa: E402


SCHEMA_VERSION = 1
POLICY_KIND = "leaf-oracle-policy"
RECEIPT_KIND = "leaf-differential-oracle"
BACKEND = "python-ia32-subset-v1"
COVERAGE = "finite-allowlisted-vectors"
DEFAULT_POLICY = ROOT / "tools" / "Resources" / "leaf-oracles.json"
DEFAULT_REPORT = ROOT / "build" / "decomp-current-report.json"
DEFAULT_CACHE = ROOT / "build" / "decomp-cache" / "oracle"
MAX_POLICY_BYTES = 1024 * 1024
MAX_REPORT_BYTES = 32 * 1024 * 1024
MAX_RECEIPT_BYTES = 16 * 1024 * 1024
MAX_MAP_BYTES = 16 * 1024 * 1024
MAX_EXECUTABLE_BYTES = 64 * 1024 * 1024
MAX_SYMBOL_BYTES = 256 * 1024 * 1024
MAX_TOOL_BYTES = 8 * 1024 * 1024
MAX_RUNTIME_BYTES = 256 * 1024 * 1024
MAX_SCOPE_ADDRESSES = 4096
REGISTER_NAMES = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
CALLEE_SAVED = ("ebx", "esi", "edi", "ebp")
RETURN_SENTINEL = 0x0BADF00D
INITIAL_REGISTERS = {
    "eax": 0x10203040,
    "ecx": 0x21314151,
    "edx": 0x32425262,
    "ebx": 0x43536373,
    "ebp": 0x54647484,
    "esi": 0x65758595,
    "edi": 0x768696A6,
}
REQUIRED_CASES = (
    ("int-min", -2147483648, 16),
    ("negative-one", -1, 16),
    ("zero", 0, 16),
    ("one", 1, 16),
    ("below-minimum", 15, 16),
    ("minimum", 16, 16),
    ("above-minimum", 17, 32),
    ("below-32", 31, 32),
    ("power-32", 32, 32),
    ("above-32", 33, 64),
    ("below-256", 255, 256),
    ("power-256", 256, 256),
    ("above-256", 257, 512),
    ("below-65536", 65535, 65536),
    ("power-65536", 65536, 65536),
    ("above-65536", 65537, 131072),
    ("above-power-29", 536870913, 1073741824),
    ("power-30", 1073741824, 1073741824),
)
ALLOWLIST = {
    "0x004B0740": {
        "name": "Numerics::RoundUpToPowerOf2",
        "function_map_name": "Nu3D::Math::RoundUpToPowerOf2",
        "retail_size": 20,
        "retail_sha256": (
            "f22477b047d671f6b3313b7f88eb22c3b85a56de71d2eb797a8c2553de588a4c"
        ),
    }
}


class OracleError(ValueError):
    """The oracle input or receipt is invalid."""


class ExecutionError(OracleError):
    """The bounded IA-32 interpreter rejected an execution."""


@dataclass(frozen=True)
class Instruction:
    offset: int
    size: int
    operation: str
    operands: tuple[object, ...]
    raw: bytes
    successors: tuple[int, ...]


def _json_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _json_hash(value: object) -> str:
    return hashlib.sha256(_json_bytes(value)).hexdigest()


def _document_hash(value: Mapping[str, object]) -> str:
    payload = dict(value)
    payload.pop("content_sha256", None)
    return _json_hash(payload)


def _file_hash(path: Path, *, max_size: int = MAX_RUNTIME_BYTES) -> str:
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        digest = hashlib.sha256()
        size = 0
        with os.fdopen(descriptor, "rb") as stream:
            metadata = os.fstat(stream.fileno())
            if (
                not stat.S_ISREG(metadata.st_mode)
                or metadata.st_size <= 0
                or metadata.st_size > max_size
            ):
                raise OSError
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                size += len(chunk)
                if size > max_size:
                    raise OSError
                digest.update(chunk)
        return digest.hexdigest()
    except OSError as error:
        raise OracleError(f"Cannot read the required file: {path}") from error


def _read_bounded_bytes(path: Path, label: str, *, limit: int) -> bytes:
    """Read one regular file after a descriptor-level size check."""

    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        with os.fdopen(descriptor, "rb") as stream:
            metadata = os.fstat(stream.fileno())
            raw = stream.read(limit + 1)
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_size <= 0
            or metadata.st_size > limit
            or len(raw) != metadata.st_size
        ):
            raise OSError
        return raw
    except OSError as error:
        raise OracleError(f"Cannot read the bounded {label}: {path}") from error


def _reject_lexical_symlinks(path: Path, label: str) -> None:
    """Reject a symbolic link in an existing lexical path."""

    absolute = path.absolute()
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        if current.exists() and current.is_symlink():
            raise OracleError(f"{label} cannot contain a symbolic link.")


def _production_path(root: Path, path: Path, relative: str, label: str) -> Path:
    """Return one exact repository path without following a symbolic link."""

    root = root.absolute()
    expected = root / relative
    if path.absolute() != expected:
        raise OracleError(f"{label} is not the canonical production path.")
    _reject_lexical_symlinks(expected, label)
    if expected.resolve() != expected:
        raise OracleError(f"{label} is not the canonical production path.")
    return expected


def _strict_keys(value: Mapping[str, object], expected: set[str], label: str) -> None:
    if set(value) != expected:
        missing = sorted(expected - set(value))
        extra = sorted(set(value) - expected)
        detail: list[str] = []
        if missing:
            detail.append("missing " + ", ".join(missing))
        if extra:
            detail.append("unexpected " + ", ".join(extra))
        raise OracleError(f"{label} has invalid fields: {', '.join(detail)}")


def _read_json(path: Path, label: str, *, limit: int) -> dict[str, object]:
    if path.is_symlink():
        raise OracleError(f"The {label} cannot be a symbolic link.")
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        with os.fdopen(descriptor, "rb") as stream:
            metadata = os.fstat(stream.fileno())
            raw = stream.read(limit + 1)
        if not stat.S_ISREG(metadata.st_mode) or len(raw) <= 0 or len(raw) > limit:
            raise OracleError(f"The {label} size is invalid.")
        text = raw.decode("utf-8")

        def unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
            result: dict[str, object] = {}
            for key, value in pairs:
                if key in result:
                    raise OracleError(f"The {label} contains a duplicate field.")
                result[key] = value
            return result

        def invalid_constant(_value: str) -> object:
            raise OracleError(f"The {label} contains a nonfinite number.")

        value = json.loads(
            text,
            object_pairs_hook=unique_object,
            parse_constant=invalid_constant,
        )
    except OracleError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise OracleError(f"The {label} is not valid JSON.") from error
    if not isinstance(value, dict):
        raise OracleError(f"The {label} must contain a JSON object.")
    return value


def _format_address(value: int) -> str:
    return f"0x{value:08X}"


def _address(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, str):
        raise OracleError(f"{label} is invalid.")
    try:
        result = int(value, 16)
    except ValueError as error:
        raise OracleError(f"{label} is invalid.") from error
    if result < 0 or result > 0xFFFFFFFF or value != _format_address(result):
        raise OracleError(f"{label} is not a canonical 32-bit address.")
    return result


def _sha256(value: object, label: str) -> str:
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise OracleError(f"{label} is not a SHA-256 value.")
    return value


def _u32(value: int) -> int:
    return value & 0xFFFFFFFF


def _s32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def _parse_hex32(value: object, label: str) -> int:
    return _address(value, label)


def load_policy(path: Path = DEFAULT_POLICY) -> dict[str, object]:
    """Load and validate the exact leaf-oracle allowlist."""

    policy = _read_json(path, "leaf-oracle policy", limit=MAX_POLICY_BYTES)
    _strict_keys(policy, {"schema_version", "kind", "targets"}, "The policy")
    if policy.get("schema_version") != SCHEMA_VERSION or policy.get("kind") != POLICY_KIND:
        raise OracleError("The leaf-oracle policy header is invalid.")
    targets = policy.get("targets")
    if not isinstance(targets, list) or len(targets) != len(ALLOWLIST):
        raise OracleError("The leaf-oracle target allowlist is invalid.")
    seen: set[str] = set()
    for target in targets:
        if not isinstance(target, dict):
            raise OracleError("A leaf-oracle target is invalid.")
        _validate_policy_target(target)
        address = str(target["address"])
        if address in seen:
            raise OracleError("The leaf-oracle policy repeats a target.")
        seen.add(address)
    if seen != set(ALLOWLIST):
        raise OracleError("The leaf-oracle policy has an unsupported target.")
    return policy


def _validate_policy_target(target: Mapping[str, object]) -> None:
    _strict_keys(
        target,
        {
            "address",
            "name",
            "function_map_name",
            "backend",
            "abi",
            "retail",
            "limits",
            "cases",
        },
        "The policy target",
    )
    address = _format_address(_address(target.get("address"), "The policy target address"))
    allowed = ALLOWLIST.get(address)
    if allowed is None:
        raise OracleError("The policy target is not allowlisted.")
    if (
        target.get("name") != allowed["name"]
        or target.get("function_map_name") != allowed["function_map_name"]
        or target.get("backend") != BACKEND
    ):
        raise OracleError("The policy target identity is invalid.")

    abi = target.get("abi")
    if not isinstance(abi, dict):
        raise OracleError("The policy ABI is invalid.")
    _strict_keys(
        abi,
        {
            "calling_convention",
            "parameters",
            "return",
            "callee_saved",
            "stack_delta",
            "memory_effects",
        },
        "The policy ABI",
    )
    expected_abi = {
        "calling_convention": "cdecl",
        "parameters": [
            {"name": "number", "type": "int32", "location": "stack+4"}
        ],
        "return": {"type": "int32", "location": "eax"},
        "callee_saved": list(CALLEE_SAVED),
        "stack_delta": 4,
        "memory_effects": "none",
    }
    if abi != expected_abi:
        raise OracleError("The policy ABI is not supported.")

    retail = target.get("retail")
    if not isinstance(retail, dict):
        raise OracleError("The policy retail identity is invalid.")
    _strict_keys(retail, {"address", "size", "bytes", "sha256"}, "The retail identity")
    retail_address = _format_address(
        _address(retail.get("address"), "The retail function address")
    )
    size = retail.get("size")
    code_hex = retail.get("bytes")
    saved_hash = _sha256(retail.get("sha256"), "The retail function hash")
    if (
        retail_address != address
        or type(size) is not int
        or size != allowed["retail_size"]
        or not isinstance(code_hex, str)
        or re.fullmatch(r"[0-9a-f]+", code_hex) is None
        or len(code_hex) != size * 2
    ):
        raise OracleError("The policy retail identity is invalid.")
    code = bytes.fromhex(code_hex)
    if hashlib.sha256(code).hexdigest() != saved_hash or saved_hash != allowed["retail_sha256"]:
        raise OracleError("The policy retail code hash is invalid.")

    limits = target.get("limits")
    if not isinstance(limits, dict):
        raise OracleError("The policy execution limits are invalid.")
    _strict_keys(
        limits,
        {
            "max_instructions",
            "max_branches",
            "stack_base",
            "stack_size",
            "entry_esp",
            "require_full_code_coverage",
        },
        "The execution limits",
    )
    if (
        limits.get("max_instructions") != 128
        or limits.get("max_branches") != 64
        or limits.get("stack_size") != 512
        or limits.get("require_full_code_coverage") is not True
    ):
        raise OracleError("The policy execution limits are not supported.")
    stack_base = _parse_hex32(limits.get("stack_base"), "The stack base")
    entry_esp = _parse_hex32(limits.get("entry_esp"), "The entry stack pointer")
    if stack_base != 0x0012FE00 or entry_esp != 0x0012FF00:
        raise OracleError("The policy stack addresses are not supported.")

    cases = target.get("cases")
    if not isinstance(cases, list):
        raise OracleError("The policy vectors are invalid.")
    actual: list[tuple[str, int, int]] = []
    for case in cases:
        if not isinstance(case, dict):
            raise OracleError("A policy vector is invalid.")
        _strict_keys(case, {"id", "input", "expected"}, "The policy vector")
        case_id = case.get("id")
        input_value = case.get("input")
        expected = case.get("expected")
        if (
            not isinstance(case_id, str)
            or re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", case_id) is None
            or type(input_value) is not int
            or type(expected) is not int
            or not (-0x80000000 <= input_value <= 0x40000000)
            or not (-0x80000000 <= expected <= 0x7FFFFFFF)
        ):
            raise OracleError("A policy vector is invalid.")
        actual.append((case_id, input_value, expected))
    if tuple(actual) != REQUIRED_CASES:
        raise OracleError("The policy vectors do not match the allowlisted corpus.")


def _need(code: bytes, offset: int, size: int) -> bytes:
    end = offset + size
    if offset < 0 or end > len(code):
        raise ExecutionError("An instruction extends outside the function body.")
    return code[offset:end]


def decode_instruction(code: bytes, offset: int) -> Instruction:
    """Decode one instruction from the small allowlisted IA-32 subset."""

    opcode = _need(code, offset, 1)[0]
    if opcode == 0x8B:
        raw = _need(code, offset, 4)
        modrm, sib, displacement = raw[1], raw[2], raw[3]
        mod = modrm >> 6
        destination = (modrm >> 3) & 7
        rm = modrm & 7
        if mod != 1 or rm != 4 or sib != 0x24 or displacement != 4:
            raise ExecutionError("MOV uses a memory form outside the allowlist.")
        return Instruction(
            offset,
            4,
            "mov_stack",
            (REGISTER_NAMES[destination], 4),
            raw,
            (offset + 4,),
        )
    if 0xB8 <= opcode <= 0xBF:
        raw = _need(code, offset, 5)
        immediate = struct.unpack_from("<I", raw, 1)[0]
        return Instruction(
            offset,
            5,
            "mov_imm",
            (REGISTER_NAMES[opcode - 0xB8], immediate),
            raw,
            (offset + 5,),
        )
    if opcode == 0x3B:
        raw = _need(code, offset, 2)
        modrm = raw[1]
        if modrm >> 6 != 3:
            raise ExecutionError("CMP uses an operand outside the allowlist.")
        left = REGISTER_NAMES[(modrm >> 3) & 7]
        right = REGISTER_NAMES[modrm & 7]
        return Instruction(offset, 2, "cmp", (left, right), raw, (offset + 2,))
    if opcode in (0x7E, 0x7C):
        raw = _need(code, offset, 2)
        displacement = struct.unpack("b", raw[1:2])[0]
        fallthrough = offset + 2
        target = fallthrough + displacement
        operation = "jle" if opcode == 0x7E else "jl"
        return Instruction(
            offset,
            2,
            operation,
            (target,),
            raw,
            (fallthrough, target),
        )
    if opcode == 0xD1:
        raw = _need(code, offset, 2)
        modrm = raw[1]
        if modrm >> 6 != 3 or ((modrm >> 3) & 7) != 4:
            raise ExecutionError("SHL uses an operand outside the allowlist.")
        register = REGISTER_NAMES[modrm & 7]
        return Instruction(offset, 2, "shl1", (register,), raw, (offset + 2,))
    if opcode == 0xC3:
        return Instruction(offset, 1, "ret", (), bytes((opcode,)), ())
    if opcode in (0xE8, 0x9A, 0xFF):
        raise ExecutionError("The function contains a call or indirect control transfer.")
    raise ExecutionError(f"Unsupported IA-32 opcode 0x{opcode:02X} at offset {offset}.")


def decode_body(code: bytes, limits: Mapping[str, object]) -> dict[int, Instruction]:
    """Decode all reachable bytes and validate each control-flow edge."""

    max_instructions = int(limits["max_instructions"])
    queue: deque[int] = deque((0,))
    instructions: dict[int, Instruction] = {}
    occupied: dict[int, int] = {}
    branch_count = 0
    while queue:
        offset = queue.popleft()
        if offset in instructions:
            continue
        if offset < 0 or offset >= len(code):
            raise ExecutionError("A branch leaves the function body.")
        owner = occupied.get(offset)
        if owner is not None and owner != offset:
            raise ExecutionError("A branch enters the middle of an instruction.")
        instruction = decode_instruction(code, offset)
        if len(instructions) >= max_instructions:
            raise ExecutionError("The static instruction limit was exceeded.")
        for byte_offset in range(offset, offset + instruction.size):
            previous = occupied.get(byte_offset)
            if previous is not None and previous != offset:
                raise ExecutionError("Two decoded instructions overlap.")
            occupied[byte_offset] = offset
        instructions[offset] = instruction
        if instruction.operation in ("jle", "jl"):
            branch_count += 1
            if branch_count > int(limits["max_branches"]):
                raise ExecutionError("The static branch limit was exceeded.")
        queue.extend(instruction.successors)
    for instruction in instructions.values():
        for successor in instruction.successors:
            if successor not in instructions:
                raise ExecutionError("A control-flow edge has no decoded instruction.")
    if not any(instruction.operation == "ret" for instruction in instructions.values()):
        raise ExecutionError("The function has no reachable RET instruction.")
    if limits.get("require_full_code_coverage") is True and set(occupied) != set(range(len(code))):
        raise ExecutionError("The function body has an unreachable or undecoded byte.")
    return instructions


def validate_definite_initialization(instructions: Mapping[int, Instruction]) -> None:
    """Reject a reachable register or flag read before an allowlisted write."""

    states: dict[int, tuple[frozenset[str], bool]] = {0: (frozenset({"esp"}), False)}
    queue: deque[int] = deque((0,))
    while queue:
        offset = queue.popleft()
        defined, flags_defined = states[offset]
        instruction = instructions[offset]
        next_defined = set(defined)
        next_flags = flags_defined
        operation = instruction.operation
        if operation == "mov_stack":
            destination, _displacement = instruction.operands
            if "esp" not in defined:
                raise ExecutionError("MOV reads ESP before it is defined.")
            if str(destination) in {*CALLEE_SAVED, "esp"}:
                raise ExecutionError("MOV writes a protected ABI register.")
            next_defined.add(str(destination))
        elif operation == "mov_imm":
            destination, _immediate = instruction.operands
            if str(destination) in {*CALLEE_SAVED, "esp"}:
                raise ExecutionError("MOV writes a protected ABI register.")
            next_defined.add(str(destination))
        elif operation == "cmp":
            left, right = (str(value) for value in instruction.operands)
            if "esp" in (left, right):
                raise ExecutionError("CMP reads the protected stack pointer.")
            if left not in defined or right not in defined:
                raise ExecutionError("CMP reads a register before it is defined.")
            next_flags = True
        elif operation in ("jle", "jl"):
            if not flags_defined:
                raise ExecutionError("A branch reads flags before they are defined.")
        elif operation == "shl1":
            register = str(instruction.operands[0])
            if register not in defined:
                raise ExecutionError("SHL reads a register before it is defined.")
            if register in {*CALLEE_SAVED, "esp"}:
                raise ExecutionError("SHL writes a protected ABI register.")
            next_defined.add(register)
            next_flags = True
        elif operation == "ret":
            if "esp" not in defined or "eax" not in defined:
                raise ExecutionError("RET can run before ESP or EAX is defined.")
        outgoing = (frozenset(next_defined), next_flags)
        for successor in instruction.successors:
            previous = states.get(successor)
            if previous is None:
                states[successor] = outgoing
                queue.append(successor)
                continue
            merged = (previous[0] & outgoing[0], previous[1] and outgoing[1])
            if merged != previous:
                states[successor] = merged
                queue.append(successor)


def _parity(value: int) -> bool:
    return (value & 0xFF).bit_count() % 2 == 0


def _cmp_flags(left: int, right: int) -> dict[str, bool | None]:
    result = _u32(left - right)
    return {
        "cf": left < right,
        "pf": _parity(result),
        "af": bool((left ^ right ^ result) & 0x10),
        "zf": result == 0,
        "sf": bool(result & 0x80000000),
        "of": bool((left ^ right) & (left ^ result) & 0x80000000),
    }


def _shl1_flags(value: int, result: int) -> dict[str, bool | None]:
    carry = bool(value & 0x80000000)
    return {
        "cf": carry,
        "pf": _parity(result),
        "af": None,
        "zf": result == 0,
        "sf": bool(result & 0x80000000),
        "of": bool(result & 0x80000000) ^ carry,
    }


def _read_u32(memory: Mapping[int, int], address: int, base: int, size: int) -> int:
    address = _u32(address)
    if address < base or address + 4 > base + size:
        raise ExecutionError("A memory read is outside the bounded stack.")
    values: list[int] = []
    for current in range(address, address + 4):
        value = memory.get(current)
        if value is None:
            raise ExecutionError("A memory read uses an uninitialized stack byte.")
        values.append(value)
    return int.from_bytes(bytes(values), "little")


def _write_initial_u32(memory: dict[int, int], address: int, value: int) -> None:
    for index, byte in enumerate(_u32(value).to_bytes(4, "little")):
        memory[address + index] = byte


def execute(code: bytes, policy: Mapping[str, object], input_value: int) -> dict[str, object]:
    """Execute one vector with deterministic IA-32 entry state."""

    limits = policy["limits"]
    assert isinstance(limits, Mapping)
    instructions = decode_body(code, limits)
    validate_definite_initialization(instructions)
    stack_base = _parse_hex32(limits["stack_base"], "The stack base")
    entry_esp = _parse_hex32(limits["entry_esp"], "The entry stack pointer")
    stack_size = int(limits["stack_size"])
    registers = dict(INITIAL_REGISTERS)
    registers["esp"] = entry_esp
    memory: dict[int, int] = {}
    _write_initial_u32(memory, entry_esp, RETURN_SENTINEL)
    _write_initial_u32(memory, entry_esp + 4, input_value)
    original_memory = dict(memory)
    flags: dict[str, bool | None] = {
        "cf": None,
        "pf": None,
        "af": None,
        "zf": None,
        "sf": None,
        "of": None,
    }
    offset = 0
    steps = 0
    branches = 0
    trace: list[dict[str, object]] = []
    instruction_offsets: set[int] = set()
    conditional_edges: set[str] = set()
    returned_to: int | None = None
    while returned_to is None:
        if steps >= int(limits["max_instructions"]):
            raise ExecutionError("The runtime instruction limit was exceeded.")
        instruction = instructions.get(offset)
        if instruction is None:
            raise ExecutionError("Execution reached an invalid instruction boundary.")
        steps += 1
        instruction_offsets.add(offset)
        trace_row: dict[str, object] = {
            "offset": offset,
            "bytes": instruction.raw.hex(),
            "operation": instruction.operation,
        }
        operation = instruction.operation
        if operation == "mov_stack":
            register, displacement = instruction.operands
            address = _u32(registers["esp"] + int(displacement))
            registers[str(register)] = _read_u32(memory, address, stack_base, stack_size)
            offset += instruction.size
        elif operation == "mov_imm":
            register, immediate = instruction.operands
            registers[str(register)] = int(immediate)
            offset += instruction.size
        elif operation == "cmp":
            left, right = instruction.operands
            flags = _cmp_flags(registers[str(left)], registers[str(right)])
            offset += instruction.size
        elif operation in ("jle", "jl"):
            if flags["sf"] is None or flags["of"] is None or (
                operation == "jle" and flags["zf"] is None
            ):
                raise ExecutionError("A branch reads an uninitialized flag.")
            branches += 1
            if branches > int(limits["max_branches"]):
                raise ExecutionError("The runtime branch limit was exceeded.")
            less = bool(flags["sf"]) != bool(flags["of"])
            taken = less or (operation == "jle" and bool(flags["zf"]))
            trace_row["taken"] = taken
            conditional_edges.add(
                f"{instruction.offset}:{'taken' if taken else 'not-taken'}"
            )
            offset = int(instruction.operands[0]) if taken else offset + instruction.size
        elif operation == "shl1":
            register = str(instruction.operands[0])
            value = registers[register]
            result = _u32(value << 1)
            registers[register] = result
            flags = _shl1_flags(value, result)
            offset += instruction.size
        elif operation == "ret":
            returned_to = _read_u32(memory, registers["esp"], stack_base, stack_size)
            registers["esp"] = _u32(registers["esp"] + 4)
        else:  # pragma: no cover - all instructions come from this decoder.
            raise ExecutionError("The interpreter reached an unsupported operation.")
        trace.append(trace_row)

    if returned_to != RETURN_SENTINEL:
        raise ExecutionError("RET did not read the expected return address.")
    if registers["esp"] != _u32(entry_esp + int(policy["abi"]["stack_delta"])):
        raise ExecutionError("The function returned with an invalid stack pointer.")
    changed_callee = [
        name for name in CALLEE_SAVED if registers[name] != INITIAL_REGISTERS[name]
    ]
    if changed_callee:
        raise ExecutionError("The function changed a callee-saved register.")
    writes = [
        _format_address(address)
        for address in sorted(set(memory) | set(original_memory))
        if memory.get(address) != original_memory.get(address)
    ]
    if writes:
        raise ExecutionError("The function changed memory outside its ABI contract.")
    return {
        "return": _s32(registers["eax"]),
        "stack_delta": _u32(registers["esp"] - entry_esp),
        "returned_to": _format_address(returned_to),
        "callee_saved_preserved": True,
        "memory_writes": writes,
        "instructions": steps,
        "branches": branches,
        "instruction_offsets": sorted(instruction_offsets),
        "conditional_edges": sorted(conditional_edges),
        "trace_sha256": _json_hash(trace),
    }


def _binary_code(path: Path, address: int, size: int) -> bytes:
    try:
        image = _read_bounded_bytes(
            path, "PE32 image", limit=MAX_EXECUTABLE_BYTES
        )
        metadata = decomp_binary.parse_image_metadata(image)
    except (OracleError, ValueError) as error:
        raise OracleError(f"Cannot read the PE32 image: {path}") from error
    offset = decomp_binary.address_to_file_offset(metadata, address)
    if offset is None or offset + size > len(image):
        raise OracleError("The function body is outside its PE32 image.")
    section = next(
        (
            item
            for item in metadata.sections
            if item.virtual_address <= address
            and address + size <= item.virtual_address + item.raw_size
        ),
        None,
    )
    if section is None or section.name != ".text":
        raise OracleError("The function body is outside the executable code section.")
    if (
        address + size > section.virtual_address + section.virtual_size
        or section.characteristics & 0x20000000 == 0
    ):
        raise OracleError("The function body is outside an executable virtual section.")
    return image[offset : offset + size]


def _descriptor(
    root: Path, path: Path, *, max_size: int = MAX_RUNTIME_BYTES
) -> dict[str, object]:
    resolved_root = root.absolute()
    absolute = path.absolute()
    _reject_lexical_symlinks(absolute, "A required oracle input")
    resolved = absolute.resolve()
    if resolved != absolute:
        raise OracleError(f"A required oracle input is not canonical: {path}")
    try:
        relative = resolved.relative_to(resolved_root).as_posix()
    except ValueError as error:
        raise OracleError(f"A required file is outside the repository: {path}") from error
    return {
        "path": relative,
        "sha256": _file_hash(resolved, max_size=max_size),
        "size": resolved.stat().st_size,
    }


def _git_head(root: Path) -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    value = completed.stdout.strip().lower()
    if completed.returncode != 0 or re.fullmatch(r"[0-9a-f]{40,64}", value) is None:
        raise OracleError("Cannot read the repository HEAD.")
    return value


def _repository_identity(root: Path) -> dict[str, object]:
    from tools.decomp_campaigns import (
        _snapshot_hash,
        source_index_snapshot,
        source_worktree_snapshot,
    )

    return {
        "head": _git_head(root),
        "source_worktree_sha256": _snapshot_hash(source_worktree_snapshot(root)),
        "source_index_sha256": _snapshot_hash(source_index_snapshot(root)),
    }


def _function_map_name(path: Path, address: int) -> str:
    matches: list[str] = []
    try:
        lines = _read_bounded_bytes(
            path, "function map", limit=MAX_MAP_BYTES
        ).decode("utf-8").splitlines()
    except (OracleError, UnicodeError) as error:
        raise OracleError("Cannot read the function map.") from error
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(maxsplit=1)
        try:
            found = int(parts[0], 16)
        except (IndexError, ValueError) as error:
            raise OracleError("The function map contains an invalid address.") from error
        if found == address:
            if len(parts) != 2 or not parts[1].strip():
                raise OracleError("The allowlisted function has no map name.")
            matches.append(parts[1].strip())
    if len(matches) != 1:
        raise OracleError("The function map does not identify one allowlisted target.")
    return matches[0]


def _comparison_row(path: Path, address: int) -> dict[str, object]:
    report = _read_json(path, "comparison report", limit=MAX_REPORT_BYTES)
    rows = report.get("data")
    if not isinstance(rows, list):
        raise OracleError("The comparison report has no function rows.")
    matches: list[dict[str, object]] = []
    for row in rows:
        if not isinstance(row, dict):
            raise OracleError("The comparison report contains an invalid row.")
        value = row.get("address")
        try:
            parsed = int(str(value), 16)
        except (TypeError, ValueError) as error:
            raise OracleError("The comparison report contains an invalid address.") from error
        if parsed == address:
            matches.append(row)
    if len(matches) != 1:
        raise OracleError("The comparison report does not identify one allowlisted target.")
    return matches[0]


def _report_binding(root: Path, report: Path) -> tuple[dict[str, object], dict[str, object]]:
    from tools.decomp_provenance import provenance_path, validate_report

    sidecar = provenance_path(report)
    _file_hash(report, max_size=MAX_REPORT_BYTES)
    _file_hash(sidecar, max_size=MAX_POLICY_BYTES)
    try:
        validate_report(report, root=root)
    except ValueError as error:
        raise OracleError(f"The comparison report provenance is invalid: {error}") from error
    return (
        _descriptor(root, report, max_size=MAX_REPORT_BYTES),
        _descriptor(root, sidecar, max_size=MAX_POLICY_BYTES),
    )


def _target_for_address(policy: Mapping[str, object], address: str) -> dict[str, object]:
    targets = policy["targets"]
    assert isinstance(targets, list)
    matches = [
        item
        for item in targets
        if isinstance(item, dict) and item.get("address") == address
    ]
    if len(matches) != 1:
        raise OracleError("The policy does not identify one requested target.")
    return matches[0]


def _normalize_scope(scope: Sequence[str | int]) -> list[str]:
    if len(scope) > MAX_SCOPE_ADDRESSES:
        raise OracleError("The oracle scope has too many addresses.")
    result: list[str] = []
    for value in scope:
        if isinstance(value, bool):
            raise OracleError("The oracle scope contains an invalid address.")
        try:
            address = int(str(value), 0) if not isinstance(value, int) else value
        except (TypeError, ValueError) as error:
            raise OracleError("The oracle scope contains an invalid address.") from error
        if address < 0 or address > 0xFFFFFFFF:
            raise OracleError("The oracle scope contains an invalid address.")
        result.append(_format_address(address))
    if result != sorted(set(result)):
        raise OracleError("The oracle scope must contain sorted unique addresses.")
    return result


def _capture_inputs(
    root: Path,
    policy_path: Path,
    report_path: Path,
    scope: Sequence[str | int],
) -> tuple[dict[str, object], dict[str, object], list[str]]:
    root = root.absolute()
    policy_path = _production_path(
        root,
        policy_path,
        "tools/Resources/leaf-oracles.json",
        "The leaf-oracle policy",
    )
    report_path = _production_path(
        root,
        report_path,
        "build/decomp-current-report.json",
        "The comparison report",
    )
    policy = load_policy(policy_path)
    normalized_scope = _normalize_scope(scope)
    report_descriptor, provenance_descriptor = _report_binding(root, report_path)
    function_map = root / "tools" / "Resources" / "functions_map.txt"
    retail_image = root / "original" / "toy2.exe"
    current_image = root / "build" / "toy2.exe"
    current_symbols = root / "build" / "toy2.pdb"
    for path, relative, label in (
        (function_map, "tools/Resources/functions_map.txt", "The function map"),
        (retail_image, "original/toy2.exe", "The retail executable"),
        (current_image, "build/toy2.exe", "The current executable"),
        (current_symbols, "build/toy2.pdb", "The current symbols"),
        (Path(__file__), "tools/decomp_oracle.py", "The oracle tool"),
        (root / "tools/decomp_binary.py", "tools/decomp_binary.py", "The PE32 reader"),
        (root / "tools/decomp_campaigns.py", "tools/decomp_campaigns.py", "The snapshot tool"),
        (root / "tools/decomp_provenance.py", "tools/decomp_provenance.py", "The provenance tool"),
    ):
        _production_path(root, path, relative, label)
    from tools import decomp_campaigns, decomp_provenance

    for module, expected in (
        (decomp_binary, root / "tools/decomp_binary.py"),
        (decomp_campaigns, root / "tools/decomp_campaigns.py"),
        (decomp_provenance, root / "tools/decomp_provenance.py"),
    ):
        module_path = Path(str(module.__file__)).resolve() if module.__file__ else None
        if module_path != expected:
            raise OracleError("An imported oracle support module has an invalid path.")
    selected = [address for address in sorted(ALLOWLIST) if address in normalized_scope]
    inputs: dict[str, object] = {
        "scope": normalized_scope,
        "selected_targets": selected,
        "repository": _repository_identity(root),
        "policy": _descriptor(root, policy_path, max_size=MAX_POLICY_BYTES),
        "tool": {
            **_descriptor(root, Path(__file__), max_size=MAX_TOOL_BYTES),
            "backend": BACKEND,
        },
        "decoder": {
            "engine": BACKEND,
            "architecture": "ia32",
            "mode": 32,
            "dependency_free": True,
            "binary_reader": _descriptor(
                root,
                root / "tools" / "decomp_binary.py",
                max_size=MAX_TOOL_BYTES,
            ),
        },
        "support_tools": {
            "snapshot": _descriptor(
                root,
                root / "tools" / "decomp_campaigns.py",
                max_size=MAX_TOOL_BYTES,
            ),
            "provenance": _descriptor(
                root,
                root / "tools" / "decomp_provenance.py",
                max_size=MAX_TOOL_BYTES,
            ),
        },
        "python_runtime": {
            "implementation": sys.implementation.name,
            "version": list(sys.version_info[:3]),
            "executable": {
                "path": str(Path(sys.executable).resolve()),
                "sha256": _file_hash(
                    Path(sys.executable).resolve(), max_size=MAX_RUNTIME_BYTES
                ),
            },
        },
        "function_map": _descriptor(root, function_map, max_size=MAX_MAP_BYTES),
        "comparison_report": report_descriptor,
        "comparison_provenance": provenance_descriptor,
        "retail_image": _descriptor(
            root, retail_image, max_size=MAX_EXECUTABLE_BYTES
        ),
        "current_image": _descriptor(
            root, current_image, max_size=MAX_EXECUTABLE_BYTES
        ),
        "current_symbols": _descriptor(
            root, current_symbols, max_size=MAX_SYMBOL_BYTES
        ),
    }
    return policy, inputs, selected


def _evaluate_target(
    target: Mapping[str, object],
    *,
    map_name: str,
    current_address: int,
    retail_code: bytes,
    current_code: bytes,
) -> dict[str, object]:
    """Evaluate one target from exact code bytes and policy data."""

    address_text = str(target["address"])
    retail = target["retail"]
    assert isinstance(retail, Mapping)
    size = int(retail["size"])
    retail_hash = hashlib.sha256(retail_code).hexdigest()
    current_hash = hashlib.sha256(current_code).hexdigest()
    if retail_code.hex() != retail["bytes"] or retail_hash != retail["sha256"]:
        raise OracleError("The retail function body changed from its allowlist.")

    retail_decoded = decode_body(retail_code, target["limits"])
    validate_definite_initialization(retail_decoded)
    current_static_failure: str | None = None
    try:
        current_decoded = decode_body(current_code, target["limits"])
        validate_definite_initialization(current_decoded)
    except ExecutionError as error:
        current_decoded = {}
        current_static_failure = str(error)

    def required_coverage(decoded: Mapping[int, Instruction]) -> tuple[list[int], list[str]]:
        offsets = sorted(decoded)
        edges = sorted(
            f"{instruction.offset}:{outcome}"
            for instruction in decoded.values()
            if instruction.operation in ("jle", "jl")
            for outcome in ("not-taken", "taken")
        )
        return offsets, edges

    retail_required_offsets, retail_required_edges = required_coverage(retail_decoded)
    current_required_offsets, current_required_edges = required_coverage(current_decoded)
    retail_seen_offsets: set[int] = set()
    current_seen_offsets: set[int] = set()
    retail_seen_edges: set[str] = set()
    current_seen_edges: set[str] = set()

    target_result: dict[str, object] = {
        "address": address_text,
        "name": target["name"],
        "function_map_name": map_name,
        "backend": target["backend"],
        "abi": target["abi"],
        "limits": target["limits"],
        "retail_code": {
            "address": address_text,
            "size": size,
            "bytes": retail_code.hex(),
            "sha256": retail_hash,
        },
        "current_code": {
            "address": _format_address(current_address),
            "size": size,
            "bytes": current_code.hex(),
            "sha256": current_hash,
        },
        "dynamic_coverage": None,
        "cases": [],
        "status": "passed",
        "failure": None,
    }
    if current_static_failure is not None:
        target_result["status"] = "failed"
        target_result["failure"] = current_static_failure
    cases = target["cases"]
    assert isinstance(cases, list)
    for case in cases:
        assert isinstance(case, Mapping)
        row_result: dict[str, object] = {
            "id": case["id"],
            "input": case["input"],
            "expected": case["expected"],
            "retail": None,
            "current": None,
            "passed": False,
            "failure": None,
        }
        try:
            retail_result = execute(retail_code, target, int(case["input"]))
            current_result = execute(current_code, target, int(case["input"]))
            row_result["retail"] = retail_result
            row_result["current"] = current_result
            retail_seen_offsets.update(retail_result["instruction_offsets"])
            current_seen_offsets.update(current_result["instruction_offsets"])
            retail_seen_edges.update(retail_result["conditional_edges"])
            current_seen_edges.update(current_result["conditional_edges"])
            expected = int(case["expected"])
            comparable_fields = (
                "return",
                "stack_delta",
                "returned_to",
                "callee_saved_preserved",
                "memory_writes",
            )
            passed = (
                retail_result["return"] == expected
                and current_result["return"] == expected
                and all(
                    retail_result[field] == current_result[field]
                    for field in comparable_fields
                )
            )
            row_result["passed"] = passed
            if not passed:
                target_result["status"] = "failed"
                if target_result["failure"] is None:
                    target_result["failure"] = (
                        f"Vector {case['id']} did not match its expected ABI result."
                    )
        except ExecutionError as error:
            target_result["status"] = "failed"
            if target_result["failure"] is None:
                target_result["failure"] = str(error)
            row_result["failure"] = str(error)
        cast_cases = target_result["cases"]
        assert isinstance(cast_cases, list)
        cast_cases.append(row_result)
    coverage = {
        "retail": {
            "required_instruction_offsets": retail_required_offsets,
            "executed_instruction_offsets": sorted(retail_seen_offsets),
            "required_conditional_edges": retail_required_edges,
            "executed_conditional_edges": sorted(retail_seen_edges),
            "complete": retail_seen_offsets == set(retail_required_offsets)
            and retail_seen_edges == set(retail_required_edges),
        },
        "current": {
            "required_instruction_offsets": current_required_offsets,
            "executed_instruction_offsets": sorted(current_seen_offsets),
            "required_conditional_edges": current_required_edges,
            "executed_conditional_edges": sorted(current_seen_edges),
            "complete": current_static_failure is None
            and current_seen_offsets == set(current_required_offsets)
            and current_seen_edges == set(current_required_edges),
        },
    }
    target_result["dynamic_coverage"] = coverage
    if not coverage["retail"]["complete"] or not coverage["current"]["complete"]:
        target_result["status"] = "failed"
        if target_result["failure"] is None:
            target_result["failure"] = (
                "The committed vector corpus does not cover each dynamic "
                "instruction and conditional edge."
            )
    return target_result


def _run_target(
    root: Path,
    report_path: Path,
    target: Mapping[str, object],
) -> dict[str, object]:
    address_text = str(target["address"])
    address = _address(address_text, "The target address")
    allowed = ALLOWLIST[address_text]
    map_name = _function_map_name(
        root / "tools" / "Resources" / "functions_map.txt", address
    )
    if map_name != target["function_map_name"] or map_name != allowed["function_map_name"]:
        raise OracleError("The allowlisted function map name changed.")
    row = _comparison_row(report_path, address)
    current_value = row.get("recomp")
    if not isinstance(current_value, str):
        raise OracleError("The comparison row has no current function address.")
    try:
        current_address = int(current_value, 16)
    except ValueError as error:
        raise OracleError("The current function address is invalid.") from error
    if current_address < 0 or current_address > 0xFFFFFFFF:
        raise OracleError("The current function address is invalid.")
    row_type = row.get("type")
    if (
        row.get("name") != target["name"]
        or type(row_type) is not int
        or row_type != 1
    ):
        raise OracleError("The comparison row target identity changed.")
    retail = target["retail"]
    assert isinstance(retail, Mapping)
    size = int(retail["size"])
    retail_code = _binary_code(root / "original" / "toy2.exe", address, size)
    current_code = _binary_code(root / "build" / "toy2.exe", current_address, size)
    return _evaluate_target(
        target,
        map_name=map_name,
        current_address=current_address,
        retail_code=retail_code,
        current_code=current_code,
    )


def build_receipt(
    scope: Sequence[str | int],
    *,
    root: Path = ROOT,
    policy_path: Path | None = None,
    report_path: Path | None = None,
) -> dict[str, object]:
    """Run each allowlisted target in a bounded scope."""

    root = root.resolve()
    effective_policy = policy_path or root / "tools" / "Resources" / "leaf-oracles.json"
    effective_report = report_path or root / "build" / "decomp-current-report.json"
    policy, inputs, selected = _capture_inputs(
        root,
        effective_policy,
        effective_report,
        scope,
    )
    results: list[dict[str, object]] = []
    status = "not-applicable" if not selected else "passed"
    for address in selected:
        target = _target_for_address(policy, address)
        result = _run_target(root, effective_report.resolve(), target)
        results.append(result)
        if result.get("status") != "passed":
            status = "failed"
    receipt: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": RECEIPT_KIND,
        "status": status,
        "semantic_equivalence_claim": False,
        "native_execution": False,
        "coverage": COVERAGE,
        "inputs": inputs,
        "targets": results,
    }
    receipt["content_sha256"] = _document_hash(receipt)
    return receipt


def _safe_cache_root(root: Path, cache_root: Path) -> Path:
    root = root.absolute()
    expected = root / "build" / "decomp-cache" / "oracle"
    if cache_root.absolute() != expected:
        raise OracleError("The oracle cache is not build/decomp-cache/oracle.")
    _reject_lexical_symlinks(expected, "The oracle cache")
    if expected.resolve() != expected:
        raise OracleError("The oracle cache is not canonical.")
    return expected


def _cache_entry_matches(path: Path, content: str) -> bool:
    """Compare one cache entry without following links or unbounded reads."""

    expected = content.encode("utf-8")
    if not expected or len(expected) > MAX_RECEIPT_BYTES:
        return False
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        with os.fdopen(descriptor, "rb") as stream:
            metadata = os.fstat(stream.fileno())
            if (
                not stat.S_ISREG(metadata.st_mode)
                or metadata.st_size != len(expected)
            ):
                return False
            actual = stream.read(len(expected) + 1)
        return actual == expected
    except OSError:
        return False


def _atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise OracleError("The oracle cache target cannot be a symbolic link.")
    if path.exists():
        if _cache_entry_matches(path, content):
            return
        raise OracleError("The oracle cache contains different content.")
    temporary: Path | None = None
    try:
        descriptor, name = tempfile.mkstemp(
            dir=path.parent, prefix=f".{path.name}.", suffix=".tmp"
        )
        temporary = Path(name)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.link(temporary, path)
        except FileExistsError:
            if not _cache_entry_matches(path, content):
                raise OracleError("The oracle cache contains different content.")
        temporary.unlink()
        temporary = None
    except OracleError:
        raise
    except (OSError, UnicodeError) as error:
        raise OracleError("Cannot write the oracle receipt cache.") from error
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError:
                pass


def write_receipt(
    receipt: Mapping[str, object],
    *,
    root: Path = ROOT,
    cache_root: Path | None = None,
) -> Path:
    """Write one content-addressed oracle receipt without replacing content."""

    _validate_receipt_shape(receipt)
    _validate_receipt_semantics(receipt)
    digest = _sha256(receipt.get("content_sha256"), "The oracle receipt hash")
    resolved_cache = _safe_cache_root(
        root, cache_root or root / "build" / "decomp-cache" / "oracle"
    )
    path = resolved_cache / f"{digest}.json"
    content = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if len(content.encode("utf-8")) > MAX_RECEIPT_BYTES:
        raise OracleError("The serialized oracle receipt is too large.")
    _atomic_write(path, content)
    return path.resolve()


def run_oracle(
    scope: Sequence[str | int],
    *,
    root: Path = ROOT,
    policy_path: Path | None = None,
    report_path: Path | None = None,
    cache_root: Path | None = None,
) -> tuple[dict[str, object], Path]:
    root = root.resolve()
    receipt = build_receipt(
        scope,
        root=root,
        policy_path=policy_path or root / "tools" / "Resources" / "leaf-oracles.json",
        report_path=report_path or root / "build" / "decomp-current-report.json",
    )
    path = write_receipt(
        receipt,
        root=root,
        cache_root=cache_root or root / "build" / "decomp-cache" / "oracle",
    )
    return receipt, path


def _validate_receipt_shape(receipt: Mapping[str, object]) -> None:
    _strict_keys(
        receipt,
        {
            "schema_version",
            "kind",
            "status",
            "semantic_equivalence_claim",
            "native_execution",
            "coverage",
            "inputs",
            "targets",
            "content_sha256",
        },
        "The oracle receipt",
    )
    if (
        receipt.get("schema_version") != SCHEMA_VERSION
        or receipt.get("kind") != RECEIPT_KIND
        or receipt.get("status") not in {"passed", "failed", "not-applicable"}
        or receipt.get("semantic_equivalence_claim") is not False
        or receipt.get("native_execution") is not False
        or receipt.get("coverage") != COVERAGE
        or not isinstance(receipt.get("inputs"), dict)
        or not isinstance(receipt.get("targets"), list)
        or receipt.get("content_sha256") != _document_hash(receipt)
    ):
        raise OracleError("The oracle receipt is invalid.")


def _validate_descriptor(
    value: object,
    label: str,
    *,
    extra: set[str] | None = None,
    expected_path: str | None = None,
    max_size: int = 1024 * 1024 * 1024,
) -> Mapping[str, object]:
    if not isinstance(value, Mapping):
        raise OracleError(f"{label} is invalid.")
    expected = {"path", "sha256", "size"} | (extra or set())
    _strict_keys(value, expected, label)
    path = value.get("path")
    size = value.get("size")
    if (
        not isinstance(path, str)
        or not path
        or Path(path).is_absolute()
        or ".." in Path(path).parts
        or type(size) is not int
        or size <= 0
        or size > max_size
        or (expected_path is not None and path != expected_path)
    ):
        raise OracleError(f"{label} is invalid.")
    _sha256(value.get("sha256"), f"{label} hash")
    return value


def _code_from_descriptor(value: object, label: str) -> tuple[int, bytes]:
    if not isinstance(value, Mapping):
        raise OracleError(f"{label} is invalid.")
    _strict_keys(value, {"address", "size", "bytes", "sha256"}, label)
    address = _address(value.get("address"), f"{label} address")
    size = value.get("size")
    code_hex = value.get("bytes")
    saved_hash = _sha256(value.get("sha256"), f"{label} hash")
    if (
        type(size) is not int
        or size <= 0
        or not isinstance(code_hex, str)
        or re.fullmatch(r"[0-9a-f]+", code_hex) is None
        or len(code_hex) != size * 2
    ):
        raise OracleError(f"{label} is invalid.")
    code = bytes.fromhex(code_hex)
    if hashlib.sha256(code).hexdigest() != saved_hash:
        raise OracleError(f"{label} hash changed.")
    return address, code


def _validate_receipt_semantics(receipt: Mapping[str, object]) -> None:
    inputs = receipt.get("inputs")
    targets = receipt.get("targets")
    assert isinstance(inputs, Mapping)
    assert isinstance(targets, list)
    _strict_keys(
        inputs,
        {
            "scope",
            "selected_targets",
            "repository",
            "policy",
            "tool",
            "decoder",
            "support_tools",
            "python_runtime",
            "function_map",
            "comparison_report",
            "comparison_provenance",
            "retail_image",
            "current_image",
            "current_symbols",
        },
        "The oracle receipt inputs",
    )
    scope_value = inputs.get("scope")
    selected_value = inputs.get("selected_targets")
    if not isinstance(scope_value, list) or not isinstance(selected_value, list):
        raise OracleError("The oracle receipt target selection is invalid.")
    scope = _normalize_scope(scope_value)
    selected = [address for address in sorted(ALLOWLIST) if address in scope]
    if selected_value != selected:
        raise OracleError("The oracle receipt target selection changed.")

    repository = inputs.get("repository")
    if not isinstance(repository, Mapping):
        raise OracleError("The oracle repository identity is invalid.")
    _strict_keys(
        repository,
        {"head", "source_worktree_sha256", "source_index_sha256"},
        "The oracle repository identity",
    )
    if (
        not isinstance(repository.get("head"), str)
        or re.fullmatch(r"[0-9a-f]{40,64}", str(repository["head"])) is None
    ):
        raise OracleError("The oracle repository HEAD is invalid.")
    _sha256(repository.get("source_worktree_sha256"), "The source worktree hash")
    _sha256(repository.get("source_index_sha256"), "The source index hash")
    descriptor_paths = {
        "policy": ("tools/Resources/leaf-oracles.json", MAX_POLICY_BYTES),
        "function_map": ("tools/Resources/functions_map.txt", MAX_MAP_BYTES),
        "comparison_report": ("build/decomp-current-report.json", MAX_REPORT_BYTES),
        "comparison_provenance": (
            "build/decomp-current-report.json.provenance.json",
            MAX_POLICY_BYTES,
        ),
        "retail_image": ("original/toy2.exe", MAX_EXECUTABLE_BYTES),
        "current_image": ("build/toy2.exe", MAX_EXECUTABLE_BYTES),
        "current_symbols": ("build/toy2.pdb", MAX_SYMBOL_BYTES),
    }
    for name, (expected_path, max_size) in descriptor_paths.items():
        _validate_descriptor(
            inputs.get(name),
            f"The {name.replace('_', ' ')}",
            expected_path=expected_path,
            max_size=max_size,
        )
    tool = _validate_descriptor(
        inputs.get("tool"),
        "The oracle tool",
        extra={"backend"},
        expected_path="tools/decomp_oracle.py",
        max_size=MAX_TOOL_BYTES,
    )
    if tool.get("backend") != BACKEND:
        raise OracleError("The oracle tool backend is invalid.")
    decoder = inputs.get("decoder")
    if not isinstance(decoder, Mapping):
        raise OracleError("The oracle decoder identity is invalid.")
    _strict_keys(
        decoder,
        {"engine", "architecture", "mode", "dependency_free", "binary_reader"},
        "The oracle decoder identity",
    )
    if (
        decoder.get("engine") != BACKEND
        or decoder.get("architecture") != "ia32"
        or decoder.get("mode") != 32
        or decoder.get("dependency_free") is not True
    ):
        raise OracleError("The oracle decoder identity is invalid.")
    _validate_descriptor(
        decoder.get("binary_reader"),
        "The oracle binary reader",
        expected_path="tools/decomp_binary.py",
        max_size=MAX_TOOL_BYTES,
    )
    support_tools = inputs.get("support_tools")
    if not isinstance(support_tools, Mapping):
        raise OracleError("The oracle support-tool identity is invalid.")
    _strict_keys(
        support_tools,
        {"snapshot", "provenance"},
        "The oracle support-tool identity",
    )
    _validate_descriptor(
        support_tools.get("snapshot"),
        "The oracle snapshot tool",
        expected_path="tools/decomp_campaigns.py",
        max_size=MAX_TOOL_BYTES,
    )
    _validate_descriptor(
        support_tools.get("provenance"),
        "The oracle provenance tool",
        expected_path="tools/decomp_provenance.py",
        max_size=MAX_TOOL_BYTES,
    )
    runtime = inputs.get("python_runtime")
    if not isinstance(runtime, Mapping):
        raise OracleError("The Python runtime identity is invalid.")
    _strict_keys(
        runtime,
        {"implementation", "version", "executable"},
        "The Python runtime identity",
    )
    version = runtime.get("version")
    executable = runtime.get("executable")
    if (
        runtime.get("implementation") != "cpython"
        or not isinstance(version, list)
        or len(version) != 3
        or not all(type(item) is int and item >= 0 for item in version)
        or not isinstance(executable, Mapping)
    ):
        raise OracleError("The Python runtime identity is invalid.")
    _strict_keys(executable, {"path", "sha256"}, "The Python executable identity")
    if not isinstance(executable.get("path"), str) or not Path(
        str(executable["path"])
    ).is_absolute():
        raise OracleError("The Python executable identity is invalid.")
    _sha256(executable.get("sha256"), "The Python executable hash")

    if len(targets) != len(selected):
        raise OracleError("The oracle receipt target count changed.")
    expected_status = "not-applicable" if not selected else "passed"
    for address, target_result in zip(selected, targets, strict=True):
        if not isinstance(target_result, Mapping):
            raise OracleError("An oracle target result is invalid.")
        _strict_keys(
            target_result,
            {
                "address",
                "name",
                "function_map_name",
                "backend",
                "abi",
                "limits",
                "retail_code",
                "current_code",
                "dynamic_coverage",
                "cases",
                "status",
                "failure",
            },
            "The oracle target result",
        )
        if target_result.get("address") != address:
            raise OracleError("The oracle target order changed.")
        cases = target_result.get("cases")
        if not isinstance(cases, list):
            raise OracleError("The oracle target cases are invalid.")
        policy_cases: list[dict[str, object]] = []
        for case in cases:
            if not isinstance(case, Mapping):
                raise OracleError("An oracle case result is invalid.")
            _strict_keys(
                case,
                {"id", "input", "expected", "retail", "current", "passed", "failure"},
                "The oracle case result",
            )
            policy_cases.append(
                {
                    "id": case.get("id"),
                    "input": case.get("input"),
                    "expected": case.get("expected"),
                }
            )
        retail_address, retail_code = _code_from_descriptor(
            target_result.get("retail_code"), "The retail code"
        )
        current_address, current_code = _code_from_descriptor(
            target_result.get("current_code"), "The current code"
        )
        if retail_address != int(address, 16):
            raise OracleError("The retail code address changed.")
        policy_target: dict[str, object] = {
            "address": address,
            "name": target_result.get("name"),
            "function_map_name": target_result.get("function_map_name"),
            "backend": target_result.get("backend"),
            "abi": target_result.get("abi"),
            "retail": target_result.get("retail_code"),
            "limits": target_result.get("limits"),
            "cases": policy_cases,
        }
        _validate_policy_target(policy_target)
        expected_target = _evaluate_target(
            policy_target,
            map_name=str(target_result.get("function_map_name")),
            current_address=current_address,
            retail_code=retail_code,
            current_code=current_code,
        )
        if dict(target_result) != expected_target:
            raise OracleError("An oracle target result does not match its code and vectors.")
        if expected_target["status"] != "passed":
            expected_status = "failed"
    if receipt.get("status") != expected_status:
        raise OracleError("The oracle receipt status is invalid.")


def validate_receipt(
    path: Path,
    *,
    root: Path = ROOT,
    current: bool = False,
    require_pass: bool = False,
    expected_scope: Sequence[str | int] | None = None,
    cache_root: Path | None = None,
) -> dict[str, object]:
    """Validate one receipt and optionally rerun it with current inputs."""

    root = root.resolve()
    expected_cache = _safe_cache_root(
        root, cache_root or root / "build" / "decomp-cache" / "oracle"
    )
    lexical_path = path.absolute()
    _reject_lexical_symlinks(lexical_path, "The oracle receipt path")
    if lexical_path.parent != expected_cache or lexical_path.resolve() != lexical_path:
        raise OracleError("The oracle receipt path is not canonical.")
    receipt = _read_json(lexical_path, "oracle receipt", limit=MAX_RECEIPT_BYTES)
    validate_document(
        receipt,
        require_pass=require_pass,
        expected_scope=expected_scope,
    )
    digest = str(receipt["content_sha256"])
    expected_path = expected_cache / f"{digest}.json"
    if lexical_path != expected_path:
        raise OracleError("The oracle receipt path is not canonical.")
    inputs = receipt["inputs"]
    assert isinstance(inputs, dict)
    scope = inputs["scope"]
    assert isinstance(scope, list)
    if current or require_pass:
        rebuilt = build_receipt(scope, root=root)
        if rebuilt != receipt:
            raise OracleError("The oracle receipt does not match current inputs and outcomes.")
    return receipt


def validate_document(
    receipt: Mapping[str, object],
    *,
    require_pass: bool = False,
    expected_scope: Sequence[str | int] | None = None,
) -> dict[str, object]:
    """Validate an embedded or immutable receipt without its cache path."""

    _validate_receipt_shape(receipt)
    _validate_receipt_semantics(receipt)
    inputs = receipt["inputs"]
    assert isinstance(inputs, dict)
    scope = inputs.get("scope")
    if not isinstance(scope, list):
        raise OracleError("The oracle receipt has no bounded scope.")
    normalized = _normalize_scope(scope)
    if normalized != scope:
        raise OracleError("The oracle receipt scope is invalid.")
    if expected_scope is not None and normalized != _normalize_scope(expected_scope):
        raise OracleError("The oracle receipt scope changed.")
    if require_pass and receipt.get("status") not in {"passed", "not-applicable"}:
        raise OracleError("The oracle gate did not pass.")
    return dict(receipt)


def read_document(path: Path) -> dict[str, object]:
    """Read and deeply validate an oracle document from an immutable store."""

    receipt = _read_json(path, "oracle receipt", limit=MAX_RECEIPT_BYTES)
    return validate_document(receipt)


def replay_identity(receipt: Mapping[str, object]) -> dict[str, object]:
    """Return the policy, code, corpus, and outcomes used for delivery replay."""

    validated = validate_document(receipt, require_pass=True)
    inputs = validated["inputs"]
    targets = validated["targets"]
    assert isinstance(inputs, Mapping)
    assert isinstance(targets, list)
    replay_targets: list[dict[str, object]] = []
    for target in targets:
        assert isinstance(target, Mapping)
        current_code = target["current_code"]
        assert isinstance(current_code, Mapping)
        replay_targets.append(
            {
                "address": target["address"],
                "name": target["name"],
                "function_map_name": target["function_map_name"],
                "backend": target["backend"],
                "abi": target["abi"],
                "limits": target["limits"],
                "retail_code": target["retail_code"],
                "current_code": {
                    "size": current_code["size"],
                    "bytes": current_code["bytes"],
                    "sha256": current_code["sha256"],
                },
                "dynamic_coverage": target["dynamic_coverage"],
                "cases": target["cases"],
                "status": target["status"],
                "failure": target["failure"],
            }
        )
    return {
        "coverage": validated["coverage"],
        "semantic_equivalence_claim": validated["semantic_equivalence_claim"],
        "native_execution": validated["native_execution"],
        "status": validated["status"],
        "scope": inputs["scope"],
        "selected_targets": inputs["selected_targets"],
        "policy_sha256": inputs["policy"]["sha256"],
        "targets": replay_targets,
    }


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run finite differential checks for allowlisted IA-32 leaf functions."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    listing = subparsers.add_parser("list", help="List allowlisted leaf functions.")
    listing.add_argument("--json", action="store_true")
    run = subparsers.add_parser("run", help="Run one allowlisted leaf function.")
    run.add_argument("--target", required=True)
    run.add_argument("--json", action="store_true")
    verify = subparsers.add_parser("verify", help="Verify one oracle receipt.")
    verify.add_argument("--receipt", type=Path, required=True)
    verify.add_argument("--current", action="store_true")
    verify.add_argument("--require-pass", action="store_true")
    verify.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "list":
            policy = load_policy()
            targets = policy["targets"]
            assert isinstance(targets, list)
            output: object = [
                {
                    "address": target["address"],
                    "name": target["name"],
                    "backend": target["backend"],
                    "vectors": len(target["cases"]),
                }
                for target in targets
                if isinstance(target, dict)
            ]
        elif args.command == "run":
            target = _format_address(int(args.target, 0))
            if target not in ALLOWLIST:
                raise OracleError("The requested target is not allowlisted.")
            receipt, path = run_oracle([target])
            output = {
                "status": receipt["status"],
                "target": target,
                "receipt": str(path),
                "content_sha256": receipt["content_sha256"],
            }
        else:
            receipt = validate_receipt(
                args.receipt,
                current=args.current,
                require_pass=args.require_pass,
            )
            inputs = receipt["inputs"]
            assert isinstance(inputs, dict)
            output = {
                "ok": True,
                "status": receipt["status"],
                "scope": inputs["scope"],
                "targets": inputs["selected_targets"],
                "content_sha256": receipt["content_sha256"],
            }
        if getattr(args, "json", False):
            print(json.dumps(output, indent=2, sort_keys=True))
        elif isinstance(output, list):
            for item in output:
                print(f"{item['address']} {item['name']} ({item['vectors']} vectors)")
        else:
            assert isinstance(output, dict)
            print(json.dumps(output, sort_keys=True))
        if args.command == "run" and isinstance(output, dict) and output.get("status") == "failed":
            return 1
        return 0
    except (OracleError, ValueError) as error:
        print(f"oracle failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
