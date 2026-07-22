"""Shared parsing and validation for Toy2 decompilation annotations."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


ANNOTATION_RE = re.compile(
    r"//\s*(FUNCTION|STUB|LIBRARY|GLOBAL):\s*TOY2\s+0x([0-9a-fA-F]+)"
)
DIRECTIVE_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


@dataclass(frozen=True)
class Annotation:
    kind: str
    address: str
    source: str
    line: int


def canonical_address(value: str | int) -> str:
    return f"0x{int(value, 0) if isinstance(value, str) else value:x}"


def _condition_is_active(directive: str, expression: str) -> bool:
    if directive in ("ifdef", "ifndef"):
        return True
    expression = re.sub(r"/\*.*?\*/", "", expression).strip()
    expression = re.sub(r"//.*", "", expression).strip()
    return expression not in ("0", "(0)")


def active_source_lines(text: str):
    """Yield active source lines while conservatively handling preprocessor blocks.

    Unknown conditions remain active so annotations are not hidden merely because
    this lightweight parser does not know the compiler's macro environment. Literal
    ``#if 0`` branches (including nested branches and their ``#else`` arms) are
    evaluated exactly.
    """

    active = True
    stack: list[tuple[bool, bool]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        match = DIRECTIVE_RE.match(line)
        if match:
            directive, expression = match.groups()
            if directive in ("if", "ifdef", "ifndef"):
                condition = _condition_is_active(directive, expression)
                stack.append((active, condition))
                active = active and condition
            elif directive == "elif" and stack:
                parent_active, branch_taken = stack[-1]
                condition = _condition_is_active("if", expression)
                active = parent_active and not branch_taken and condition
                stack[-1] = (parent_active, branch_taken or condition)
            elif directive == "else" and stack:
                parent_active, branch_taken = stack[-1]
                active = parent_active and not branch_taken
                stack[-1] = (parent_active, True)
            elif directive == "endif" and stack:
                parent_active, _ = stack.pop()
                active = parent_active
            continue
        if active:
            yield line_number, line


def read_source_annotations(source_root: Path) -> list[Annotation]:
    annotations: list[Annotation] = []
    paths = sorted((*source_root.rglob("*.cpp"), *source_root.rglob("*.h")))
    for path in paths:
        relative = path.relative_to(source_root).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        for line_number, line in active_source_lines(text):
            for kind, address in ANNOTATION_RE.findall(line):
                annotations.append(
                    Annotation(
                        kind=kind.lower(),
                        address=canonical_address(f"0x{address}"),
                        source=relative,
                        line=line_number,
                    )
                )
    return annotations
