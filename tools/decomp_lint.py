#!/usr/bin/env python3
"""Check reconstructed source for decompiler residue and implausible source forms.

The machine-code comparison proves behavior. This linter checks the part that
the comparison cannot measure: whether the reconstructed source states a
coherent data model and resembles source that Traveller's Tales could maintain.

Run ``tools/decomp lint`` for the repository or add ``--staged`` before a
commit. New errors fail. Reviewed legacy errors stay visible until their source
is repaired, and a stale baseline entry fails so quality debt cannot disappear
from the report without being removed from the baseline too.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import os
import re
import subprocess
import sys
from dataclasses import asdict, dataclass, replace
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "src"
BASELINE_PATH = ROOT / ".notes" / "lint-baseline.tsv"
# A whole-tree scan takes seconds, and one finish and the next start scan the same src
# in up to ten processes (the working tree, the index and the next HEAD hold the same
# text). Such scans are saved under build/, keyed on the text of each unit and on the
# source of this module, so a changed unit or rule scans again.
CACHE_DIR = ROOT / "build" / "decomp-cache" / "lint"
CACHE_KEEP = 16

SOURCE_SUFFIXES = (".c", ".cpp", ".h", ".hpp")
ANNOTATION_RE = re.compile(
    r"//\s*(FUNCTION|STUB|LIBRARY|GLOBAL):\s*TOY2\s+(0x[0-9A-Fa-f]+)([^\n]*)"
)
ALLOW_RE = re.compile(
    r"//\s*decomp-lint:\s*allow\[([a-z0-9-]+)\]\s+reason:\s*(.{12,})\s*$",
    re.IGNORECASE,
)

TYPE_WORD = r"(?:[A-Za-z_]\w*(?:::\w+)*(?:\s+const)?|unsigned\s+\w+|signed\s+\w+)"
BYTE_TYPE = r"(?:char|signed\s+char|unsigned\s+char|u?int8_t|BYTE|std::byte)"
DECOMPILER_NAME_RE = re.compile(
    r"\b(?:"
    r"[iu](?:Stack)?Var\d+|[psu][A-Za-z]*Var\d+|local_[0-9A-Fa-f]+|"
    r"param_?\d+|arg_?\d+|field[0-9A-Fa-f]{1,4}|"
    r"DAT_[0-9A-Fa-f]+|FUN_[0-9A-Fa-f]+|LAB_[0-9A-Fa-f]+"
    r")\b"
)
PLACEHOLDER_FIELD_RE = re.compile(
    r"\b(?:field[0-9A-Fa-f]{1,4}|unk[0-9A-Fa-f]{2,}|reserved[0-9A-Fa-f]*)\b"
)
UNKNOWN_SYMBOL_RE = re.compile(r"\b(?:g_unk[A-Za-z0-9_]*|UnkFunc\d*|unknown[A-Za-z0-9_]*)\b")
ARITHMETIC_NAME_RE = re.compile(
    r"\b(?:g_|s_)?[A-Za-z0-9_]*?(?:Copy[0-9]?|Times[0-9]+[A-Za-z0-9]*|"
    r"Minus[0-9]+|Plus[0-9]+|Tmp|Temp)\b"
)

# A directive may accept only these rules. A cast that hides a wrong declared type
# (magic-pointer, signature-concealment) stays debt until the declaration is fixed,
# so a comment can never make its function terminal.
SUPPRESSIBLE_RULES = {"anonymous-buffer-view", "typed-byte-roundtrip"}
# Readability advice: bc prints a new occurrence, but it never fails validate and
# is not source debt, so naming a value in one function never blocks a campaign on
# the literals of another, and un-naming a value never counts as removed debt.
ADVISORY_RULES = {"unnamed-constant", "repeated-macro-body"}
# Values too common to need a name; the unnamed-constant rule skips them.
PLAIN_VALUES = {0, 1, -1, 2}
MACRO_REPEAT_LINES = 8
# Longer operators first, so "==" is not read as "=".
OPERATORS = ("==", "!=", "<=", ">=", "<<", ">>", "&&", "||", "+=", "-=", "*=", "/=", "&=",
             "|=", "^=", "=", "<", ">", "+", "-", "*", "/", "%", "&", "|", "^")
NONZERO_INTEGER = r"(?:0[xX]0*[1-9A-Fa-f][0-9A-Fa-f]*|[1-9][0-9]*)[uUlL]*\b"
INTEGER_VALUE = r"-?[ \t]*(?:0[xX][0-9A-Fa-f]+|[0-9]+)"

RULE_HELP = {
    "raw-layout-access": (
        "A literal byte displacement states a structure offset instead of a field. "
        "Declare the layout and use its field name."
    ),
    "typed-byte-roundtrip": (
        "A typed pointer is converted to bytes, advanced, and converted to the same "
        "type. Use typed indexing, a row stride in elements, or a declared layout."
    ),
    "anonymous-buffer-view": (
        "An inline cast dereferences an untyped buffer. Convert the API or file boundary "
        "once into a role-named typed local and use that local."
    ),
    "implicit-record-layout": (
        "Several constant offsets reinterpret one scalar buffer as a record. Declare the "
        "record header or element type and access named fields."
    ),
    "decompiler-identifier": (
        "A completed function contains an analysis placeholder. Recover the value's role "
        "from callers and uses, or leave the function as a STUB."
    ),
    "placeholder-field-use": (
        "A completed function accesses an unresolved or reserved member. Name the member "
        "from its role and pin the recovered layout."
    ),
    "placeholder-field": (
        "A recovered layout still has an unresolved member. Keep it visible as debt until "
        "enough uses establish the member's role."
    ),
    "magic-pointer": (
        "A nonzero integer is cast to a pointer. Correct the declared type; until then the "
        "finding stays as debt of the function."
    ),
    "unnamed-constant": (
        "A bare literal stands where this file writes a constant, enum or #define of the same "
        "value (same operand and operator, array, call argument or switch). Use the name; "
        "naming does not change the generated code."
    ),
    "repeated-macro-body": (
        "Two macros in one file have the same body of 8 or more lines. Keep one macro and "
        "give it a parameter; the preprocessed code stays identical."
    ),
    "unfinished-function": (
        "A FUNCTION annotation has an empty or default-return body. Mark unfinished work "
        "as STUB unless comparison proves that retail has the same null body."
    ),
    "unknown-symbol": (
        "A completed function still uses a working unknown name. Recover a modest role-based "
        "name when the evidence permits it."
    ),
    "opaque-state-slot": (
        "A completed function accesses a numbered state slot. Declare a role-based "
        "field or accessor for the supported state value."
    ),
    "address-named-symbol": (
        "A symbol name contains a retail address. Replace the address with the "
        "value's supported role."
    ),
    "unexplained-helper": (
        "A helper name does not state its source-level role. Name the operation "
        "from its callers and side effects."
    ),
    "original-name-vocabulary": (
        "The retail binary states a different name for this engine object. Use "
        "the original vocabulary or document why it does not identify this value."
    ),
    "arithmetic-name": "The identifier states how a value was computed instead of what reads it.",
    "unnamed-bitmask": "A raw mask is applied to flags or state. Prefer an established named flag.",
    "unstructured-control-flow": (
        "Several gotos remain in one completed function. Recover structured control flow, "
        "or retain only a clear cleanup or error path."
    ),
    "unpinned-layout": (
        "A structure contains recovered padding or unresolved members but has no size assertion. "
        "Pin its size and the offsets used by completed functions."
    ),
    "signature-name-drift": (
        "A declaration and definition use different role names for the same parameters. "
        "Keep the public reconstruction vocabulary consistent."
    ),
    "signature-concealment": (
        "A cast at a project-function call hides disagreement between the caller's data "
        "model and the declared interface. Correct the source type instead."
    ),
    "surrogate-layout": (
        "A local View type with reserved storage or void pointers can hide an unresolved "
        "shared layout. Recover the owning type or keep the function as a STUB."
    ),
    "surrogate-layout-use": (
        "A completed function casts project data to a local View type. Confirm the shared "
        "layout and use its owning type."
    ),
    "repeated-private-type": (
        "Two or more source files define the same named type and field layout. "
        "Move the type to a shared owning header."
    ),
}


@dataclass(frozen=True)
class Owner:
    kind: str = "shared"
    address: str = ""
    matched: bool = False

    @property
    def key(self) -> str:
        return self.address.lower() if self.address else self.kind


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    rule: str
    severity: str
    text: str
    detail: str
    column: int = 1
    owner_kind: str = "shared"
    owner_address: str = ""
    subject: str = ""
    fingerprint: str = ""
    legacy: bool = False
    suppressed: bool = False

    @property
    def advisory(self) -> bool:
        return self.rule in ADVISORY_RULES

    @property
    def baseline_key(self) -> tuple[str, str, str, str]:
        owner = self.owner_address.lower() if self.owner_address else self.relative_path
        return owner, self.rule, self.subject, self.fingerprint

    @property
    def relative_path(self) -> str:
        try:
            return self.path.resolve().relative_to(ROOT).as_posix()
        except ValueError:
            return self.path.as_posix()

    def render(self) -> str:
        state = "legacy " if self.legacy else ""
        state = "accepted " if self.suppressed else state
        location = f"{self.relative_path}:{self.line}:{self.column}"
        owner = f" ({self.owner_address})" if self.owner_address else ""
        return (
            f"{location}: {state}{self.severity}: [{self.rule}]{owner} {self.detail}\n"
            f"    {self.text.strip()}"
        )

    def to_json(self) -> dict[str, object]:
        data = asdict(self)
        data["path"] = self.relative_path
        data["baseline_key"] = list(self.baseline_key)
        return data


@dataclass(frozen=True)
class SourceUnit:
    path: Path
    text: str


@dataclass(frozen=True)
class BaselineEntry:
    owner: str
    rule: str
    subject: str
    fingerprint: str
    path: str

    @property
    def key(self) -> tuple[str, str, str, str]:
        return self.owner, self.rule, self.subject, self.fingerprint


def _macro_definition_lines(text: str) -> set[int]:
    """Return the 1-based line numbers inside #define bodies, continuations included."""

    lines: set[int] = set()
    continued = False
    for number, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        if continued or stripped.startswith("#define"):
            lines.add(number)
            continued = stripped.endswith("\\")
        else:
            continued = False
    return lines


def _mask_source(text: str) -> str:
    """Replace comments, strings, and inactive #if 0 text while preserving positions."""

    chars = list(text)
    index = 0
    state = "code"
    quote = ""
    line_start = True
    active = True
    preprocessor_stack: list[tuple[bool, bool]] = []

    while index < len(chars):
        char = chars[index]
        following = chars[index + 1] if index + 1 < len(chars) else ""

        if state == "line-comment":
            if char == "\n":
                state = "code"
                line_start = True
            else:
                chars[index] = " "
            index += 1
            continue
        if state == "block-comment":
            if char == "*" and following == "/":
                chars[index] = chars[index + 1] = " "
                index += 2
                state = "code"
            else:
                if char != "\n":
                    chars[index] = " "
                else:
                    line_start = True
                index += 1
            continue
        if state == "string":
            if char == "\\":
                chars[index] = " "
                if index + 1 < len(chars) and chars[index + 1] != "\n":
                    chars[index + 1] = " "
                index += 2
            elif char == quote:
                chars[index] = " "
                index += 1
                state = "code"
            else:
                if char != "\n":
                    chars[index] = " "
                index += 1
            continue

        if line_start:
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
            raw_line = text[index:end]
            directive = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", raw_line)
            if directive:
                word, expression = directive.groups()
                condition = expression.strip() not in ("0", "(0)")
                if word in ("if", "ifdef", "ifndef"):
                    preprocessor_stack.append((active, condition))
                    active = active and condition
                elif word == "elif" and preprocessor_stack:
                    parent, taken = preprocessor_stack[-1]
                    active = parent and not taken and condition
                    preprocessor_stack[-1] = (parent, taken or condition)
                elif word == "else" and preprocessor_stack:
                    parent, taken = preprocessor_stack[-1]
                    active = parent and not taken
                    preprocessor_stack[-1] = (parent, True)
                elif word == "endif" and preprocessor_stack:
                    parent, _ = preprocessor_stack.pop()
                    active = parent
                for pos in range(index, end):
                    chars[pos] = " "
                index = end
                continue
            line_start = False

        if not active:
            if char != "\n":
                chars[index] = " "
            else:
                line_start = True
            index += 1
            continue
        if char == "/" and following == "/":
            chars[index] = chars[index + 1] = " "
            index += 2
            state = "line-comment"
            continue
        if char == "/" and following == "*":
            chars[index] = chars[index + 1] = " "
            index += 2
            state = "block-comment"
            continue
        if char in ('"', "'"):
            quote = char
            chars[index] = " "
            index += 1
            state = "string"
            continue
        if char == "\n":
            line_start = True
        index += 1
    return "".join(chars)


def _line_column(text: str, offset: int) -> tuple[int, int]:
    line = text.count("\n", 0, offset) + 1
    previous = text.rfind("\n", 0, offset)
    return line, offset - previous


def _line_text(text: str, line: int) -> str:
    lines = text.splitlines()
    return lines[line - 1] if 0 < line <= len(lines) else ""


def _normalized(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip())


def _fingerprint(value: str) -> str:
    return hashlib.sha1(_normalized(value).encode("utf-8")).hexdigest()[:12]


def _owners_by_line(text: str) -> list[Owner]:
    lines = text.splitlines()
    owners: list[Owner] = []
    current = Owner()
    for raw in lines:
        found = ANNOTATION_RE.search(raw)
        if found:
            kind, address, tail = found.groups()
            current = Owner(kind.lower(), address.lower(), "[MATCHED]" in tail)
        owners.append(current)
    return owners


def _owner_at(owners: list[Owner], line: int) -> Owner:
    return owners[line - 1] if 0 < line <= len(owners) else Owner()


def _allowed_rules(text: str) -> dict[int, set[str]]:
    allowed: dict[int, set[str]] = {}
    for line, raw in enumerate(text.splitlines(), 1):
        found = ALLOW_RE.search(raw)
        if found and found.group(1) in SUPPRESSIBLE_RULES:
            allowed.setdefault(line + 1, set()).add(found.group(1))
    return allowed


def _add_finding(
    findings: list[Finding], path: Path, text: str, owners: list[Owner], allowed: dict[int, set[str]],
    *, offset: int, rule: str, severity: str, detail: str, subject: str, excerpt: str,
) -> None:
    line, column = _line_column(text, offset)
    owner = _owner_at(owners, line)
    if owner.kind == "library":
        return
    suppressed = rule in allowed.get(line, set())
    findings.append(
        Finding(
            path=path,
            line=line,
            column=column,
            rule=rule,
            severity=severity,
            text=_line_text(text, line),
            detail=detail,
            owner_kind=owner.kind,
            owner_address=owner.address,
            subject=subject,
            fingerprint=_fingerprint(excerpt),
            suppressed=suppressed,
        )
    )


def _balanced_body(masked: str, opening: int) -> tuple[int, int] | None:
    depth = 0
    for index in range(opening, len(masked)):
        if masked[index] == "{":
            depth += 1
        elif masked[index] == "}":
            depth -= 1
            if depth == 0:
                return opening + 1, index
    return None


def check_text(path: Path, text: str) -> list[Finding]:
    findings: list[Finding] = []
    masked = _mask_source(text)
    owners = _owners_by_line(text)
    allowed = _allowed_rules(text)

    parameter_offsets: set[int] = set()

    surrogate_view_use = re.compile(
        r"reinterpret_cast\s*<\s*(?P<name>[A-Za-z_]\w*View)\s*\*\s*>"
    )
    for match in surrogate_view_use.finditer(masked):
        line, _ = _line_column(text, match.start())
        if _owner_at(owners, line).kind != "function":
            continue
        _add_finding(
            findings,
            path,
            text,
            owners,
            allowed,
            offset=match.start(),
            rule="surrogate-layout-use",
            severity="warning",
            detail=f"completed code casts project data to local view {match.group('name')!r}",
            subject=match.group("name"),
            excerpt=match.group(0),
        )

    opaque_state_slot = re.compile(
        r"\b[A-Za-z_]\w*(?:(?:->|\.)[A-Za-z_]\w*)*(?:->|\.)data\s*"
        r"\[\s*(?:0x[0-9A-Fa-f]+|[0-9]+)\s*\]"
    )
    for match in opaque_state_slot.finditer(masked):
        line, _ = _line_column(text, match.start())
        owner = _owner_at(owners, line)
        if owner.kind != "function":
            continue
        _add_finding(
            findings,
            path,
            text,
            owners,
            allowed,
            offset=match.start(),
            rule="opaque-state-slot",
            severity="warning",
            detail="numbered data slot hides a state field's role",
            subject=match.group(0),
            excerpt=match.group(0),
        )

    address_name = re.compile(r"\b(?:g_|s_)(?:unk)?[0-9A-Fa-f]{6,8}\b")
    for match in address_name.finditer(masked):
        line, _ = _line_column(text, match.start())
        if _owner_at(owners, line).kind != "function":
            continue
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="address-named-symbol", severity="warning",
            detail=f"symbol {match.group(0)!r} uses an address as its name",
            subject=match.group(0), excerpt=match.group(0),
        )

    unexplained_helper = re.compile(r"\b[A-Za-z_]\w*Helper\s*(?=\()")
    for match in unexplained_helper.finditer(masked):
        line, _ = _line_column(text, match.start())
        if _owner_at(owners, line).kind != "function":
            continue
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="unexplained-helper", severity="warning",
            detail=f"helper {match.group(0).strip()!r} does not identify its operation",
            subject=match.group(0).strip(), excerpt=match.group(0),
        )

    original_aliases = {
        "g_d3dAppI": "d3dappi",
        "g_drawBuffer": "drawb",
        "g_transparentDrawBuffer": "drawtranb",
    }
    for alias, original in original_aliases.items():
        for match in re.finditer(rf"\b{re.escape(alias)}\b", masked):
            line, _ = _line_column(text, match.start())
            if _owner_at(owners, line).kind != "function":
                continue
            _add_finding(
                findings, path, text, owners, allowed, offset=match.start(),
                rule="original-name-vocabulary", severity="warning",
                detail=f"retail text names {alias!r} as {original!r}",
                subject=alias, excerpt=match.group(0),
            )
    parameter_name = re.compile(
        rf"\b{TYPE_WORD}[\s*&]+(?P<name>param_?\d+|arg_?\d+|field[0-9A-Fa-f]{{1,4}}|"
        rf"[iu](?:Stack)?Var\d+|[psu][A-Za-z]*Var\d+)\b\s*(?=[,)])"
    )
    for match in parameter_name.finditer(masked):
        parameter_offsets.add(match.start("name"))
        line, _ = _line_column(text, match.start("name"))
        owner = _owner_at(owners, line)
        severity = "warning" if owner.kind == "stub" else "error"
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start("name"),
            rule="placeholder-parameter", severity=severity,
            detail=f"parameter {match.group('name')!r} has no role-based name",
            subject=match.group("name"), excerpt=match.group("name"),
        )

    # Literal offsets expressed through byte-pointer casts, including multiline forms.
    raw_offset = re.compile(
        rf"(?:\(\s*{TYPE_WORD}\s*\*+\s*\)|(?:reinterpret|static)_cast\s*<\s*{TYPE_WORD}\s*\*+\s*>)"
        rf"\s*\([^;{{}}]*?(?:\(\s*{BYTE_TYPE}\s*\*\s*\)|(?:reinterpret|static)_cast\s*<\s*{BYTE_TYPE}\s*\*\s*>)"
        rf"\s*[A-Za-z_]\w*(?:->\w+|\.\w+)*\s*[+-]\s*(?:0x[0-9A-Fa-f]+|[1-9][0-9]*)[^;{{}}]*?\)",
        re.DOTALL,
    )
    for match in raw_offset.finditer(masked):
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="raw-layout-access", severity="error",
            detail="literal byte displacement hides a structure field; declare the layout and check .notes/original-names.md",
            subject="literal-offset", excerpt=match.group(0),
        )

    # A pointer returns to the same concrete type after byte arithmetic.
    byte_roundtrip = re.compile(
        rf"\(\s*(?P<outer>{TYPE_WORD})\s*\*\s*\)\s*\(\s*\(\s*{BYTE_TYPE}\s*\*\s*\)"
        rf"\s*(?P<base>[A-Za-z_]\w*(?:->\w+|\.\w+)*)\s*[+-][^;{{}}]+\)",
        re.DOTALL,
    )
    for match in byte_roundtrip.finditer(masked):
        base = match.group("base")
        declaration = re.search(
            rf"\b(?P<type>{TYPE_WORD})\s*\*\s*{re.escape(base.split('.')[-1].split('->')[-1])}\b",
            masked[: match.start()],
        )
        if declaration and _normalized(declaration.group("type")) == _normalized(match.group("outer")):
            _add_finding(
                findings, path, text, owners, allowed, offset=match.start(),
                rule="typed-byte-roundtrip", severity="error",
                detail="typed pointer is converted to bytes and back; use typed stride arithmetic or a row abstraction",
                subject=base, excerpt=match.group(0),
            )

    # Direct access through an inline reinterpretation has no named source-level view.
    anonymous_view = re.compile(
        rf"(?:\*\s*|\breturn\s+)(?:\(\s*{TYPE_WORD}\s*\*\s*\)|reinterpret_cast\s*<\s*{TYPE_WORD}\s*\*\s*>)"
        rf"\s*\([^;{{}}]+\)(?:\s*->)?",
        re.DOTALL,
    )
    for match in anonymous_view.finditer(masked):
        if re.search(r"\(\s*(?:0|NULL|nullptr)\s*\)", match.group(0)):
            continue
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="anonymous-buffer-view", severity="error",
            detail="inline buffer cast has no role-named typed view",
            subject="inline-view", excerpt=match.group(0),
        )

    magic_pointer_patterns = (
        re.compile(rf"\(\s*(?:const\s+)?{TYPE_WORD}\s*\*+\s*\)\s*(?:\(\s*{NONZERO_INTEGER}\s*\)|{NONZERO_INTEGER})"),
        re.compile(rf"reinterpret_cast\s*<\s*(?:const\s+)?{TYPE_WORD}\s*\*+\s*>\s*\(\s*{NONZERO_INTEGER}\s*\)"),
    )
    for pattern in magic_pointer_patterns:
        for match in pattern.finditer(masked):
            _add_finding(
                findings, path, text, owners, allowed, offset=match.start(),
                rule="magic-pointer", severity="error",
                detail="nonzero magic integer is cast to a pointer; correct the type or name the handle",
                subject="integer-pointer", excerpt=match.group(0),
            )

    # Identifier rules are token based and only become blocking in completed functions.
    for match in DECOMPILER_NAME_RE.finditer(masked):
        if match.start() in parameter_offsets:
            continue
        line, _ = _line_column(text, match.start())
        owner = _owner_at(owners, line)
        severity = "error" if owner.kind == "function" else "warning"
        rule = "decompiler-identifier" if owner.kind == "function" else "placeholder-field"
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(), rule=rule,
            severity=severity,
            detail=(
                f"{match.group(0)!r} is an analysis placeholder; recover a role-based name"
                if severity == "error"
                else f"{match.group(0)!r} remains unresolved while this declaration or STUB is incomplete"
            ),
            subject=match.group(0), excerpt=match.group(0),
        )

    member_use = re.compile(r"(?:->|\.)\s*(field[0-9A-Fa-f]{1,4}|unk[0-9A-Fa-f]{2,}|reserved[0-9A-Fa-f]*)\b")
    for match in member_use.finditer(masked):
        line, _ = _line_column(text, match.start())
        if _owner_at(owners, line).kind != "function":
            continue
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="placeholder-field-use", severity="error",
            detail=f"completed code accesses unresolved member {match.group(1)!r}",
            subject=match.group(1), excerpt=match.group(0),
        )

    record_view = re.compile(
        rf"(?:\(\s*{TYPE_WORD}\s*\*\s*\)|reinterpret_cast\s*<\s*{TYPE_WORD}\s*\*\s*>)"
        rf"\s*\(\s*(?P<base>[A-Za-z_]\w*)\s*(?P<offset>[+-]\s*(?:0x[0-9A-Fa-f]+|[1-9][0-9]*))\s*\)"
    )
    record_offsets: dict[tuple[str, str], list[re.Match[str]]] = {}
    for match in record_view.finditer(masked):
        line, _ = _line_column(text, match.start())
        owner = _owner_at(owners, line)
        if owner.kind == "function":
            record_offsets.setdefault((owner.key, match.group("base")), []).append(match)
    for (_, base), matches in record_offsets.items():
        offsets = {re.sub(r"\s+", "", item.group("offset")) for item in matches}
        if len(offsets) < 2:
            continue
        first = matches[0]
        _add_finding(
            findings, path, text, owners, allowed, offset=first.start(),
            rule="implicit-record-layout", severity="error",
            detail=f"{base!r} is read through {len(offsets)} constant record offsets; declare its layout",
            subject=base, excerpt=" ".join(sorted(offsets)),
        )

    # Report working unknown names as advice. One finding per symbol and owner is enough.
    seen_unknown: set[tuple[str, str]] = set()
    for match in UNKNOWN_SYMBOL_RE.finditer(masked):
        line, _ = _line_column(text, match.start())
        owner = _owner_at(owners, line)
        if owner.kind != "function":
            continue
        key = (owner.key, match.group(0))
        if key in seen_unknown:
            continue
        seen_unknown.add(key)
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="unknown-symbol", severity="warning",
            detail=f"completed function still uses working name {match.group(0)!r}",
            subject=match.group(0), excerpt=match.group(0),
        )

    for match in ARITHMETIC_NAME_RE.finditer(masked):
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="arithmetic-name", severity="warning",
            detail=f"{match.group(0)!r} names arithmetic instead of the value's role",
            subject=match.group(0), excerpt=match.group(0),
        )

    # Only names that already state a flags/state role activate the mask heuristic.
    mask_use = re.compile(
        r"\b(?P<name>[A-Za-z_]\w*(?:->\w+|\.\w+)*(?:Flags|flags|State|state))\b\s*"
        r"(?P<op>[&|^]=?|==|!=)\s*(?P<value>0x[0-9A-Fa-f]+|[2-9]|[1-9][0-9]+)"
    )
    for match in mask_use.finditer(masked):
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="unnamed-bitmask", severity="warning",
            detail=f"raw mask {match.group('value')} is applied to {match.group('name')!r}",
            subject=f"{match.group('name')}:{match.group('value')}", excerpt=match.group(0),
        )

    # FUNCTION annotations with placeholder bodies must remain STUBs.
    for annotation in ANNOTATION_RE.finditer(text):
        kind, address, tail = annotation.groups()
        if kind != "FUNCTION" or "[MATCHED]" in tail:
            continue
        opening = masked.find("{", annotation.end())
        next_annotation = ANNOTATION_RE.search(text, annotation.end())
        if opening < 0 or (next_annotation and opening > next_annotation.start()):
            continue
        body_range = _balanced_body(masked, opening)
        if not body_range:
            continue
        start, end = body_range
        body = _normalized(masked[start:end])
        if body and not re.fullmatch(r"return\s+(?:0|-1|FALSE|NULL|nullptr)\s*;", body):
            continue
        _add_finding(
            findings, path, text, owners, allowed, offset=opening,
            rule="unfinished-function", severity="error",
            detail="FUNCTION has a placeholder body; use STUB until retail behavior is implemented",
            subject=address.lower(), excerpt=body or "{}",
        )

    # More than two gotos is advisory. Cleanup/error functions remain possible.
    # A goto inside a #define body belongs to the macro's users, not to the
    # function that happens to precede the definition, so it is not counted.
    macro_lines = _macro_definition_lines(text)
    goto_by_owner: dict[str, list[re.Match[str]]] = {}
    for match in re.finditer(r"\bgoto\s+([A-Za-z_]\w*)\s*;", masked):
        line, _ = _line_column(text, match.start())
        if line in macro_lines:
            continue
        owner = _owner_at(owners, line)
        if owner.kind == "function":
            goto_by_owner.setdefault(owner.key, []).append(match)
    for matches in goto_by_owner.values():
        targets = {match.group(1).lower() for match in matches}
        has_one_error_exit = len(targets) == 1 and next(iter(targets)).startswith(
            ("cleanup", "fail", "error")
        )
        if len(matches) > 2 and not has_one_error_exit:
            first = matches[0]
            _add_finding(
                findings, path, text, owners, allowed, offset=first.start(),
                rule="unstructured-control-flow", severity="warning",
                detail=f"completed function contains {len(matches)} gotos; recover structured flow or one clear cleanup path",
                subject="goto-count", excerpt=" ".join(item.group(0) for item in matches),
            )

    # Layouts with unresolved storage should at least pin their total size.
    struct_re = re.compile(r"\bstruct\s+(\w+)\s*\{")
    structs: list[tuple[re.Match[str], tuple[int, int]]] = []
    for match in struct_re.finditer(masked):
        body_range = _balanced_body(masked, masked.find("{", match.start()))
        if not body_range:
            continue
        structs.append((match, body_range))

    for match, body_range in structs:
        start, end = body_range
        body = masked[start:end]
        name = match.group(1)
        if name.endswith("View") and (
            PLACEHOLDER_FIELD_RE.search(body) or re.search(r"\bvoid\s*\*", body)
        ):
            _add_finding(
                findings,
                path,
                text,
                owners,
                allowed,
                offset=match.start(),
                rule="surrogate-layout",
                severity="warning",
                detail=f"local view {name!r} contains unresolved storage or an untyped pointer",
                subject=name,
                excerpt=name,
            )
        if not PLACEHOLDER_FIELD_RE.search(body):
            continue
        ancestors = [
            parent.group(1)
            for parent, (parent_start, parent_end) in structs
            if parent_start < match.start() < parent_end
        ]
        asserted_names = [name]
        if ancestors:
            asserted_names.append("::".join([*ancestors, name]))
        asserted_name_pattern = "|".join(re.escape(asserted_name) for asserted_name in asserted_names)
        if re.search(rf"STATIC_ASSERT\s*\(\s*sizeof\s*\(\s*(?<![:\w])(?:{asserted_name_pattern})\s*\)", masked):
            continue
        _add_finding(
            findings, path, text, owners, allowed, offset=match.start(),
            rule="unpinned-layout", severity="warning",
            detail=f"layout {name!r} contains unresolved storage but has no size assertion",
            subject=name, excerpt=name,
        )

    findings.extend(check_unnamed_constants(path, text, masked, owners, allowed))
    findings.extend(check_repeated_macros(path, text, masked))

    # Remove exact duplicates caused by overlapping lexical patterns.
    unique: dict[tuple[str, str, str], Finding] = {}
    for finding in findings:
        owner = finding.owner_address or finding.relative_path
        _keep_first_open(unique, (owner, finding.rule, finding.subject), finding)
    return sorted(unique.values(), key=lambda item: (item.line, item.column, item.rule))


def _keep_first_open(unique: dict, key: tuple, finding: Finding) -> None:
    """Keep the first finding of a key, but an unsuppressed one over a suppressed
    one, so one directive never hides a later finding of the same rule."""
    if key not in unique or (unique[key].suppressed and not finding.suppressed):
        unique[key] = finding


def _integer_value(text: str) -> int:
    digits = re.sub(r"[\s()uUlL]", "", text)
    sign = -1 if digits.startswith("-") else 1
    digits = digits.lstrip("-")
    if digits[:2].lower() == "0x":
        return sign * int(digits, 16)
    if len(digits) > 1 and digits.startswith("0") and set(digits) <= set("01234567"):
        return sign * int(digits, 8)
    return sign * int(digits, 10)


def _function_bodies(text: str, masked: str) -> list[tuple[int, int]]:
    """Return the (start, end) offsets of every annotated FUNCTION and STUB body."""
    bodies: list[tuple[int, int]] = []
    for annotation in ANNOTATION_RE.finditer(text):
        if annotation.group(1) not in ("FUNCTION", "STUB"):
            continue
        opening = masked.find("{", annotation.end())
        following = ANNOTATION_RE.search(text, annotation.end())
        if opening < 0 or (following and opening > following.start()):
            continue
        body = _balanced_body(masked, opening)
        if body:
            bodies.append(body)
    return bodies


def _named_constants(masked: str) -> list[tuple[str, int, int, int]]:
    """Return (name, value, start, end) of each integer const, enum member and
    object-like #define with a literal value; start and end locate the value."""
    found: list[tuple[str, int, int, int]] = []
    patterns = [
        re.compile(rf"(?m)^[ \t]*#[ \t]*define[ \t]+(?P<name>[A-Za-z_]\w*)[ \t]+\(?[ \t]*"
                   rf"(?P<value>{INTEGER_VALUE})[uUlL]*[ \t]*\)?[ \t]*$"),
        re.compile(rf"\bconst(?:expr)?\s+[\w:\s]*?\b(?P<name>[A-Za-z_]\w*)\s*=\s*\(?\s*"
                   rf"(?P<value>{INTEGER_VALUE})[uUlL]*\s*\)?\s*;"),
    ]
    for pattern in patterns:
        found.extend((match.group("name"), _integer_value(match.group("value")),
                      match.start("value"), match.end("value"))
                     for match in pattern.finditer(masked))
    member = re.compile(rf"(?P<name>[A-Za-z_]\w*)\s*=\s*\(?\s*(?P<value>{INTEGER_VALUE})"
                        rf"[uUlL]*\s*\)?\s*(?=[,}}]|$)")
    for enum in re.finditer(r"\benum\b[^{};()]*\{", masked):
        body = _balanced_body(masked, enum.end() - 1)
        if body:
            found.extend((match.group("name"), _integer_value(match.group("value")),
                          body[0] + match.start("value"), body[0] + match.end("value"))
                         for match in member.finditer(masked[body[0] : body[1]]))
    return found


def _signed_literal(masked: str, start: int, value: int) -> int:
    """Negate a literal that follows a unary minus."""
    before = masked[:start].rstrip()
    if not before.endswith("-"):
        return value
    prior = before[:-1].rstrip()
    unary = not prior or prior[-1] in "=([,?:{;<>!&|+-*/%^~" or re.search(r"\b(?:return|case)$", prior)
    return -value if unary else value


def _slot(masked: str, start: int, floor: int) -> tuple[str, ...] | None:
    """Name the place a value stands in: the operand and operator before it, the
    array it indexes, the call argument it fills, or the switch of its case label."""
    before = masked[max(floor, start - 400) : start].rstrip()
    if re.search(r"\bcase$", before):
        switches = re.findall(r"\bswitch\s*\(([^;{}]*)\)\s*\{", masked[floor:start])
        return ("case", *re.findall(r"\w+", switches[-1])[-1:]) if switches else None
    for operator in OPERATORS:
        if before.endswith(operator):
            left = before[: -len(operator)].rstrip()
            operand = re.search(r"(\w+)\s*(?:\[[^\[\]]*\])?\s*\)?$", left)
            return (operand.group(1), operator) if operand else None
    if before.endswith("["):
        array = re.search(r"(\w+)\s*\[$", before)
        return (array.group(1), "[]") if array else None
    if not before.endswith(("(", ",")):
        return None
    depth, index, position = 0, 0, len(before) - 1
    while position >= 0:
        char = before[position]
        if char in ")]":
            depth += 1
        elif char in "([" and depth:
            depth -= 1
        elif char in "([":
            break
        elif char == "," and not depth:
            index += 1
        position -= 1
    callee = re.search(r"(\w+)\s*$", before[:position]) if position >= 0 and before[position] == "(" else None
    return (callee.group(1), f"arg{index}") if callee else None


def check_unnamed_constants(
    path: Path, text: str, masked: str, owners: list[Owner], allowed: dict[int, set[str]]
) -> list[Finding]:
    """Report a bare literal that stands where this file writes a constant's name.

    A value matches a name only in the same place: after the same operand and
    operator, as an index of the same array, as the same argument of the same
    call, or as a case of the same switch. A bare value alone would match every
    small number an enum happens to cover. A constant defined in a function body
    names the value only in that function, so a name added near its use never
    reports literals in untouched functions."""
    bodies = _function_bodies(text, masked)
    names: dict[tuple[int, int] | None, dict[str, int]] = {}
    definitions: set[int] = set()
    for name, value, start, end in _named_constants(masked):
        scope = next((body for body in bodies if body[0] <= start < body[1]), None)
        names.setdefault(scope, {})[name] = value
        definitions.update(range(start, end))
    if not names:
        return []
    # Where each value is written by name: the file-wide names in every body,
    # a body's own names only in that body.
    places: dict[tuple[int, int] | None, dict[tuple[int, tuple[str, ...]], str]] = {}
    spelled = re.compile(r"\b(?:" + "|".join(sorted({n for group in names.values() for n in group})) + r")\b")
    for body in bodies:
        for match in spelled.finditer(masked, body[0], body[1]):
            scope = body if match.group(0) in names.get(body, {}) else None
            value = names.get(scope, {}).get(match.group(0))
            slot = None if value is None else _slot(masked, match.start(), body[0])
            if slot:
                places.setdefault(scope, {}).setdefault((value, slot), match.group(0))
    named_values = {value for group in places.values() for value, _ in group}
    macro_lines = _macro_definition_lines(text)
    line_starts = [0] + [match.end() for match in re.finditer("\n", masked)]
    literal = re.compile(r"(?<![\w.])(?:0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*(?![\w.])")
    findings: list[Finding] = []
    for body in bodies:
        uses: dict[int, list[tuple[re.Match[str], str]]] = {}
        for match in literal.finditer(masked, body[0], body[1]):
            if match.start() in definitions:
                continue
            value = _signed_literal(masked, match.start(), _integer_value(match.group(0)))
            if value in PLAIN_VALUES or value not in named_values:
                continue
            if bisect.bisect_right(line_starts, match.start()) in macro_lines:
                continue
            start = masked.rindex("-", body[0], match.start()) if value < 0 else match.start()
            slot = _slot(masked, start, body[0])
            name = places.get(body, {}).get((value, slot)) or places.get(None, {}).get((value, slot))
            if slot and name:
                uses.setdefault(value, []).append((match, name))
        for value, matches in uses.items():
            first, name = matches[0]
            _add_finding(
                findings, path, text, owners, allowed, offset=first.start(),
                rule="unnamed-constant", severity="warning",
                detail=f"literal {first.group(0)} ({len(matches)} use(s)) stands where this file writes {name}",
                subject=str(value), excerpt=str(value),
            )
    return findings


def check_repeated_macros(path: Path, text: str, masked: str) -> list[Finding]:
    """Report each #define whose body repeats an earlier one of 8 or more lines."""
    lines, masked_lines = text.splitlines(), masked.splitlines()
    header = re.compile(r"\s*#\s*define\s+(?P<name>[A-Za-z_]\w*)(?:\([^)]*\))?")
    first: dict[str, tuple[str, int]] = {}
    findings: list[Finding] = []
    index = 0
    while index < len(masked_lines):
        found = header.match(masked_lines[index])
        start = index
        while lines[index].rstrip().endswith("\\") and index + 1 < len(lines):
            index += 1
        index += 1
        if not found or index - start < MACRO_REPEAT_LINES:
            continue
        code = "\n".join([masked_lines[start][found.end() :], *masked_lines[start + 1 : index]])
        strings = re.findall(r'"(?:\\.|[^"\\\n])*"', "\n".join(lines[start:index]))
        key = _normalized(code.replace("\\", " ")) + "\0" + "\0".join(strings)
        name = found.group("name")
        if key not in first:
            first[key] = (name, start + 1)
            continue
        original, line = first[key]
        findings.append(Finding(
            path, start + 1, "repeated-macro-body", "warning", lines[start],
            f"macro {name!r} repeats the {index - start}-line body of {original!r} (line {line}); "
            "keep one macro with a parameter",
            subject=name, fingerprint=_fingerprint(f"{original}={name}"),
        ))
    return findings


def check_file(path: Path) -> list[Finding]:
    return check_text(path, path.read_text(encoding="utf-8", errors="ignore"))


def _signature_records(unit: SourceUnit) -> list[tuple[str, tuple[str, ...], int, Owner]]:
    masked = _mask_source(unit.text)
    owners = _owners_by_line(unit.text)
    records: list[tuple[str, tuple[str, ...], int, Owner]] = []
    # This intentionally accepts only ordinary project declarations, not function pointers.
    signature = re.compile(
        r"(?m)^\s*(?:inline\s+|static\s+|virtual\s+)?[A-Za-z_]\w*(?:::\w+)*(?:\s*[&*])?\s+"
        r"(?P<name>[A-Za-z_]\w*(?:::\w+)*)\s*\((?P<params>[^(){};]*)\)\s*(?P<tail>[;{])"
    )
    for match in signature.finditer(masked):
        params: list[str] = []
        for raw in match.group("params").split(","):
            raw = raw.strip()
            if not raw or raw == "void":
                continue
            found = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^]]*\])?\s*(?:=.*)?$", raw)
            params.append(found.group(1) if found else "")
        line, _ = _line_column(unit.text, match.start())
        records.append((match.group("name"), tuple(params), line, _owner_at(owners, line)))
    return records


def _typed_signature_records(
    unit: SourceUnit,
) -> list[tuple[str, tuple[str, ...], int, Owner]]:
    masked = _mask_source(unit.text)
    owners = _owners_by_line(unit.text)
    records: list[tuple[str, tuple[str, ...], int, Owner]] = []
    signature = re.compile(
        r"(?m)^\s*(?:inline\s+|static\s+|virtual\s+)?[A-Za-z_]\w*(?:::\w+)*(?:\s*[&*])?\s+"
        r"(?P<name>[A-Za-z_]\w*(?:::\w+)*)\s*\((?P<params>[^(){};]*)\)\s*(?:;|{)"
    )
    for match in signature.finditer(masked):
        types: list[str] = []
        for raw in match.group("params").split(","):
            raw = re.sub(r"\s*=.*$", "", raw.strip())
            if not raw or raw == "void":
                continue
            found = re.match(r"(?P<type>.+?[\s*&])(?:[A-Za-z_]\w*)(?:\s*\[[^]]*\])?$", raw)
            types.append(_normalized(found.group("type")) if found else "")
        line, _ = _line_column(unit.text, match.start())
        records.append((match.group("name"), tuple(types), line, _owner_at(owners, line)))
    return records


def _matching_paren(text: str, opening: int) -> int | None:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return None


def _split_arguments(text: str) -> list[str]:
    arguments: list[str] = []
    start = 0
    depth = 0
    for index, char in enumerate(text):
        if char in "(<[{":
            depth += 1
        elif char in ")>]}":
            depth = max(depth - 1, 0)
        elif char == "," and depth == 0:
            arguments.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        arguments.append(tail)
    return arguments


def check_signature_concealment(units: list[SourceUnit]) -> list[Finding]:
    signatures: dict[tuple[str, int], set[tuple[str, ...]]] = {}
    for unit in units:
        for name, types, _, _ in _typed_signature_records(unit):
            signatures.setdefault((name.rsplit("::", 1)[-1], len(types)), set()).add(types)

    findings: list[Finding] = []
    cast_argument = re.compile(
        rf"^\s*(?:\(\s*(?P<cstyle>{TYPE_WORD}\s*\*+)\s*\)|"
        rf"reinterpret_cast\s*<\s*(?P<cpp>{TYPE_WORD}\s*\*+)\s*>)"
    )
    for unit in units:
        masked = _mask_source(unit.text)
        owners = _owners_by_line(unit.text)
        for call in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", masked):
            name = call.group(1)
            line, _ = _line_column(unit.text, call.start())
            owner = _owner_at(owners, line)
            if owner.kind != "function":
                continue
            closing = _matching_paren(masked, masked.find("(", call.start()))
            if closing is None:
                continue
            following = masked[closing + 1 :].lstrip()[:1]
            if following == "{":
                continue
            opening = masked.find("(", call.start())
            arguments = _split_arguments(masked[opening + 1 : closing])
            variants = signatures.get((name, len(arguments)), set())
            if len(variants) != 1:
                continue
            expected = next(iter(variants))
            for index, (argument, expected_type) in enumerate(zip(arguments, expected)):
                cast = cast_argument.match(argument)
                if not cast or "*" not in expected_type:
                    continue
                cast_type = _normalized(cast.group("cstyle") or cast.group("cpp") or "")
                if "LPVOID" in cast_type or re.fullmatch(r"(?:const )?void\s*\*+", cast_type):
                    # DirectX and COM expose typed output buffers as void**.
                    # The cast is part of the SDK boundary, not a project ABI patch.
                    continue
                subject = f"{name}:arg{index + 1}"
                findings.append(
                    Finding(
                        unit.path, line, "signature-concealment", "error",
                        _line_text(unit.text, line),
                        f"cast at argument {index + 1} of project function {name!r} hides the caller's source type",
                        owner_kind=owner.kind, owner_address=owner.address, subject=subject,
                        fingerprint=_fingerprint(argument),
                    )
                )
    unique: dict[tuple[str, str], Finding] = {}
    for finding in findings:
        _keep_first_open(unique, (finding.owner_address, finding.subject), finding)
    return list(unique.values())


def check_signature_drift(units: list[SourceUnit]) -> list[Finding]:
    by_name: dict[tuple[str, str, int], list[tuple[SourceUnit, tuple[str, ...], int, Owner]]] = {}
    for unit in units:
        for name, params, line, owner in _signature_records(unit):
            key = (unit.path.stem, name, len(params))
            by_name.setdefault(key, []).append((unit, params, line, owner))
    findings: list[Finding] = []
    for (_, name, _), records in by_name.items():
        # Overloads and repeated callback declarations need type resolution.
        # Restrict this vocabulary check to one header/definition pair.
        if len(records) != 2 or records[0][0].path.suffix == records[1][0].path.suffix:
            continue
        named_sets = {params for _, params, _, _ in records if params and all(params)}
        if len(named_sets) <= 1:
            continue
        definition = next((record for record in records if record[3].kind == "function"), records[0])
        unit, params, line, owner = definition
        other = next(other_params for _, other_params, _, _ in records if other_params != params)
        excerpt = f"{name}({', '.join(params)}) != ({', '.join(other)})"
        findings.append(
            Finding(
                unit.path, line, "signature-name-drift", "warning",
                _line_text(unit.text, line),
                f"parameter roles {params!r} differ from another declaration {other!r}",
                owner_kind=owner.kind, owner_address=owner.address, subject=name,
                fingerprint=_fingerprint(excerpt),
            )
        )
    return findings


def _namespace_ranges(masked: str) -> list[tuple[int, int, str]]:
    ranges: list[tuple[int, int, str]] = []
    declaration = re.compile(r"\bnamespace\s+([A-Za-z_]\w*(?:::\w+)*)\s*\{")
    for match in declaration.finditer(masked):
        body = _balanced_body(masked, match.end() - 1)
        if body is not None:
            ranges.append((body[0], body[1], match.group(1)))
    return ranges


def _private_type_records(unit: SourceUnit) -> list[tuple[str, str, int, str, str]]:
    if unit.path.suffix not in (".c", ".cpp"):
        return []
    if any(part.lower() in {"external", "generated", "build"} for part in unit.path.parts):
        return []

    masked = _mask_source(unit.text)
    namespaces = _namespace_ranges(masked)
    records: list[tuple[str, str, int, str, str]] = []
    declaration = re.compile(r"\b(struct|union)\s+([A-Za-z_]\w*)\s*\{")
    for match in declaration.finditer(masked):
        body_range = _balanced_body(masked, match.end() - 1)
        if body_range is None:
            continue
        body = masked[body_range[0] : body_range[1]]
        if not body.strip() or "{" in body or "(" in body:
            continue
        fields = [_normalized(field) for field in body.split(";") if field.strip()]
        if not fields or any(re.search(r"(?<!:):(?!:)", field) for field in fields):
            continue
        signature = ";".join(re.sub(r"\s*([,*&\[\]])\s*", r"\1", field) for field in fields)
        containing = [item for item in namespaces if item[0] <= match.start() < item[1]]
        containing.sort(key=lambda item: (item[0], -item[1]))
        namespace = "::".join(item[2] for item in containing)
        root_namespace = namespace.split("::", 1)[0]
        line, _ = _line_column(unit.text, match.start())
        records.append((match.group(1), match.group(2), line, root_namespace, signature))
    return records


def check_repeated_private_types(units: list[SourceUnit]) -> list[Finding]:
    groups: dict[tuple[str, str, str, str], list[tuple[SourceUnit, int]]] = {}
    for unit in units:
        for kind, name, line, root_namespace, signature in _private_type_records(unit):
            groups.setdefault((root_namespace, kind, name, signature), []).append((unit, line))

    findings: list[Finding] = []
    for (root_namespace, kind, name, signature), records in groups.items():
        paths = {unit.path.resolve() for unit, _ in records}
        if len(paths) < 2:
            continue
        subject = f"{root_namespace}::{name}" if root_namespace else name
        fingerprint = _fingerprint(f"{kind} {subject} {{{signature}}}")
        for unit, line in records:
            findings.append(
                Finding(
                    unit.path,
                    line,
                    "repeated-private-type",
                    "error",
                    _line_text(unit.text, line),
                    f"type {subject!r} repeats the same field layout in {len(paths)} source files. Move it to a shared owning header",
                    subject=subject,
                    fingerprint=fingerprint,
                )
            )
    return findings


def target_units(
    staged: bool, explicit: list[str], *, revision: str | None = None
) -> list[SourceUnit]:
    if explicit:
        paths = [Path(item).resolve() for item in explicit]
        return [SourceUnit(path, path.read_text(encoding="utf-8", errors="ignore")) for path in paths]
    if staged or revision is not None:
        result = subprocess.run(
            ["git", "ls-tree", "-r", "--name-only", revision, "--", "src"]
            if revision is not None
            else ["git", "ls-files", "src"],
            capture_output=True, text=True, check=False, cwd=ROOT,
        )
        units: list[SourceUnit] = []
        for name in result.stdout.splitlines():
            path = ROOT / name
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            shown = subprocess.run(
                ["git", "show", f"{revision}:{name}" if revision else f":{name}"],
                capture_output=True, check=False, cwd=ROOT,
            )
            if shown.returncode == 0:
                units.append(SourceUnit(path, shown.stdout.decode("utf-8", errors="ignore")))
        return units
    return [
        SourceUnit(path, path.read_text(encoding="utf-8", errors="ignore"))
        for path in sorted(SOURCE_ROOT.rglob("*")) if path.suffix in SOURCE_SUFFIXES
    ]


def _scan_cache(units: list[SourceUnit]) -> Path | None:
    """Return the cache file of a whole-tree scan of src, or None for any other scan."""
    if not units or not CACHE_DIR.parent.parent.is_dir():
        return None
    digest = hashlib.sha256(Path(__file__).read_bytes())
    for unit in units:
        if not unit.path.is_relative_to(SOURCE_ROOT):
            return None
        digest.update(unit.path.relative_to(SOURCE_ROOT).as_posix().encode("utf-8") + b"\0")
        digest.update(hashlib.sha256(unit.text.encode("utf-8")).digest())
    return CACHE_DIR / f"{digest.hexdigest()}.json"


def _save_scan(cache: Path, findings: list[Finding]) -> None:
    """Save a scan atomically and keep the newest CACHE_KEEP files; a failure costs only the reuse."""
    try:
        rows = [asdict(item) | {"path": item.path.relative_to(SOURCE_ROOT).as_posix()} for item in findings]
        cache.parent.mkdir(parents=True, exist_ok=True)
        temporary = cache.with_name(f"{cache.stem}.{os.getpid()}.tmp")
        temporary.write_text(json.dumps(rows), encoding="utf-8")
        os.replace(temporary, cache)
        for old in sorted(cache.parent.glob("*.json"), key=lambda path: path.stat().st_mtime)[:-CACHE_KEEP]:
            old.unlink(missing_ok=True)
    except (OSError, ValueError):
        pass


def scan_units(units: list[SourceUnit], *, cross_file: bool = True) -> list[Finding]:
    cache = _scan_cache(units) if cross_file else None
    try:
        if cache is not None and cache.is_file():
            rows = json.loads(cache.read_text(encoding="utf-8"))
            os.utime(cache)
            return [Finding(**(row | {"path": SOURCE_ROOT / row["path"]})) for row in rows]
    except (OSError, ValueError, TypeError, KeyError):
        pass
    findings = [finding for unit in units for finding in check_text(unit.path, unit.text)]
    if cross_file:
        findings.extend(check_signature_drift(units))
        findings.extend(check_signature_concealment(units))
        findings.extend(check_repeated_private_types(units))
    findings = sorted(findings, key=lambda item: (item.relative_path, item.line, item.column, item.rule))
    if cache is not None:
        _save_scan(cache, findings)
    return findings


def read_baseline(
    path: Path = BASELINE_PATH,
    *,
    staged: bool = False,
    revision: str | None = None,
) -> list[BaselineEntry]:
    if (staged or revision is not None) and path.resolve() == BASELINE_PATH.resolve():
        shown = subprocess.run(
            [
                "git",
                "show",
                f"{revision}:.notes/lint-baseline.tsv"
                if revision
                else ":.notes/lint-baseline.tsv",
            ],
            capture_output=True, check=False, cwd=ROOT,
        )
        if shown.returncode != 0:
            return []
        rows = shown.stdout.decode("utf-8", errors="ignore").splitlines()
    else:
        if not path.exists():
            return []
        rows = path.read_text(encoding="utf-8").splitlines()
    entries: list[BaselineEntry] = []
    for row in csv.reader(rows, delimiter="\t"):
        if not row or row[0].startswith("#") or len(row) < 5:
            continue
        entries.append(BaselineEntry(*row[:5]))
    return entries


def apply_baseline(
    findings: list[Finding], entries: list[BaselineEntry]
) -> tuple[list[Finding], list[BaselineEntry]]:
    baseline = {entry.key: entry for entry in entries}
    seen: set[tuple[str, str, str, str]] = set()
    classified: list[Finding] = []
    for finding in findings:
        legacy = finding.baseline_key in baseline
        # An accepted (suppressed) finding is not debt, so its baseline row is stale.
        if legacy and not finding.suppressed:
            seen.add(finding.baseline_key)
        classified.append(replace(finding, legacy=legacy))
    stale = [entry for entry in entries if entry.key not in seen]
    return classified, stale


def prune_baseline(path: Path, stale: list[BaselineEntry]) -> int:
    """Remove the stale rows from the baseline file; never add a row."""
    keys = {entry.key for entry in stale}
    kept = [
        line for line in path.read_text(encoding="utf-8").splitlines()
        if line.startswith("#") or tuple(line.split("\t")[:4]) not in keys
    ]
    path.write_text("\n".join(kept) + "\n", encoding="utf-8")
    return len(keys)


def print_baseline(findings: list[Finding]) -> None:
    print("# owner\trule\tsubject\tfingerprint\tpath")
    for finding in findings:
        if finding.suppressed:
            continue
        owner, rule, subject, fingerprint = finding.baseline_key
        print("\t".join((owner, rule, subject, fingerprint, finding.relative_path)))


def _show_finding(finding: Finding, show: str) -> bool:
    if show == "legacy":
        return finding.legacy
    if show == "new":
        return not finding.legacy
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Check reconstructed source plausibility.")
    parser.add_argument("files", nargs="*", help="specific files (default: all of src/)")
    parser.add_argument("--staged", action="store_true", help="check source from the git index")
    parser.add_argument("--warnings-as-errors", action="store_true", help="fail on new advice too")
    parser.add_argument("--quiet", action="store_true", help="print only the summary")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    parser.add_argument("--show", choices=("legacy", "new", "all"), default="new")
    parser.add_argument("--explain", metavar="RULE", help="explain one rule and exit")
    parser.add_argument(
        "--print-baseline", action="store_true",
        help="print reviewed-baseline rows; this never edits the repository",
    )
    parser.add_argument(
        "--prune-baseline", action="store_true",
        help="remove baseline rows whose debt no longer exists (campaigns finish runs it)",
    )
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH, help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.explain:
        detail = RULE_HELP.get(args.explain)
        if detail is None:
            print(f"unknown rule: {args.explain}", file=sys.stderr)
            return 2
        print(f"{args.explain}: {detail}")
        return 0

    units = target_units(args.staged, args.files)
    findings = scan_units(units, cross_file=not args.files)
    entries = read_baseline(args.baseline, staged=args.staged)
    findings, stale = apply_baseline(findings, entries)

    if args.print_baseline:
        print_baseline(findings)
        return 0
    if args.prune_baseline:
        if args.files or args.staged:
            print("lint: --prune-baseline scans the whole working tree", file=sys.stderr)
            return 2
        print(f"lint: pruned {prune_baseline(args.baseline, stale) if stale else 0} stale baseline row(s)")
        return 0

    visible = [item for item in findings if _show_finding(item, args.show)]
    new_errors = [item for item in findings if item.severity == "error" and not item.legacy and not item.suppressed]
    legacy_errors = [item for item in findings if item.severity == "error" and item.legacy and not item.suppressed]
    warnings = [item for item in findings if item.severity == "warning" and not item.suppressed]
    new_warnings = [item for item in warnings if not item.legacy]
    new_blocking_warnings = [item for item in new_warnings if not item.advisory]
    suppressed = [item for item in findings if item.suppressed]
    visible_new_errors = [item for item in visible if item.severity == "error" and not item.legacy and not item.suppressed]
    visible_legacy_errors = [item for item in visible if item.severity == "error" and item.legacy and not item.suppressed]
    visible_warnings = [item for item in visible if item.severity == "warning" and not item.suppressed]
    visible_suppressed = [item for item in visible if item.suppressed]

    # A file subset cannot prove that baseline rows elsewhere are stale.
    stale_is_gate = not args.files

    if args.format == "json":
        json.dump(
            {
                "findings": [item.to_json() for item in visible],
                "stale_baseline": [asdict(item) for item in stale] if stale_is_gate else [],
                "summary": {
                    "new_errors": len(new_errors), "legacy_errors": len(legacy_errors),
                    "warnings": len(warnings), "new_warnings": len(new_warnings),
                    "suppressed": len(suppressed), "stale_baseline": len(stale) if stale_is_gate else 0,
                },
                "displayed_summary": {
                    "new_errors": len(visible_new_errors),
                    "legacy_errors": len(visible_legacy_errors),
                    "warnings": len(visible_warnings),
                    "suppressed": len(visible_suppressed),
                },
            },
            sys.stdout, indent=2,
        )
        print()
    else:
        if not args.quiet:
            for finding in visible:
                print(finding.render())
            for entry in stale if stale_is_gate else []:
                print(
                    f"{entry.path}: error: [stale-baseline] {entry.rule} debt for "
                    f"{entry.owner} no longer exists; remove this baseline row"
                )
            if visible or (stale and stale_is_gate):
                print()
        print(
            "lint: "
            f"{len(visible_new_errors)} new error(s), {len(visible_legacy_errors)} legacy error(s), "
            f"{len(visible_warnings)} warning(s), {len(visible_suppressed)} accepted, "
            f"{len(stale) if stale_is_gate else 0} stale baseline row(s)"
        )
        if new_errors:
            print(
                "\nA machine-code match with a new error is a matched transliteration, "
                "not a reconstruction. Fix the source model or keep the function a STUB."
            )
        if args.warnings_as_errors and new_blocking_warnings:
            print(f"lint: failed: {len(new_blocking_warnings)} new warning(s) count as errors here")

    return 1 if (
        new_errors or (stale_is_gate and stale) or (args.warnings_as_errors and new_blocking_warnings)
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
