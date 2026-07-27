#!/usr/bin/env python3
"""Check reconstructed source for decompiler residue.

Machine-code similarity is measured every build. Source plausibility was not
measured at all, so a function could reach an exact match while still reading
like transliterated decompiler output. This tool supplies the missing signal.

An `error` means the source states an offset where it should state a name. Fix
it or leave the function a `STUB` for a session that can. A `warning` means the
name describes the arithmetic that produced a value rather than the value's
role.

Run `tools/decomp lint`, or `tools/decomp lint --staged` for the files in the
index. It exits non-zero when any error is found.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "src"

TYPE = r"(?:u?int(?:8|16|32|64)_t|unsigned\s+\w+|signed\s+\w+|short|long|char|void|float|double|WORD|DWORD|BYTE|LPVOID|LPWORD|HRESULT|BOOL)"
PLACEHOLDER = r"(?:field[0-9A-Fa-f]{1,3}|param[0-9]+|arg[0-9]+|unk[0-9A-Fa-f]{2,}|[iu]Var[0-9]+|[pu][A-Za-z]?Var[0-9]+)"

# A name whose suffix states the arithmetic instead of the role. Anchored at the
# end so a legitimate word such as `Template` does not match.
ARITHMETIC_SUFFIX = re.compile(
    r"\b(?:g_|s_)?[A-Za-z0-9_]*?(Copy[0-9]?|Times[0-9]+[A-Za-z0-9]*|Minus[0-9]+|Plus[0-9]+|Tmp|Temp)\b"
)


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    rule: str
    severity: str
    text: str
    detail: str

    def render(self) -> str:
        try:
            shown = self.path.relative_to(ROOT)
        except ValueError:
            shown = self.path
        location = f"{shown}:{self.line}"
        return (
            f"{location}: {self.severity}: [{self.rule}] {self.detail}\n"
            f"    {self.text.strip()}"
        )


def strip_comment(line: str) -> str:
    index = line.find("//")
    return line[:index] if index >= 0 else line


def check_file(path: Path) -> list[Finding]:
    findings: list[Finding] = []
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()

    # A declared struct body may legitimately hold a placeholder field name
    # while a layout is still being recovered, so those lines are only warned
    # about. Track the brace depth at which the current struct opened. The
    # opening brace is often on the line after the `struct` keyword, so arm a
    # pending flag first and record the depth when the brace actually arrives.
    struct_depth: int | None = None
    pending_struct = False
    depth = 0

    for number, raw in enumerate(lines, 1):
        code = strip_comment(raw)
        if not code.strip():
            continue

        # A forward declaration (`struct Foo;`) opens no body.
        if re.search(r"\b(struct|union|class)\b", code) and not re.search(r"\b(struct|union|class)\b[^{]*;\s*$", code):
            pending_struct = True

        opens = code.count("{")
        closes = code.count("}")

        in_struct = struct_depth is not None

        if pending_struct and opens:
            if struct_depth is None:
                struct_depth = depth
                in_struct = True
            pending_struct = False

        depth += opens - closes
        if struct_depth is not None and depth <= struct_depth:
            struct_depth = None

        # A byte-offset cast whose displacement is a HEX LITERAL. That is a
        # structure field spelled as an offset. A cast whose displacement is a
        # runtime value (`row * pitch`) is ordinary surface or buffer walking
        # and is correct, so it must not be reported.
        for match in re.finditer(
            rf"\(\s*{TYPE}\s*\*+\s*\)\s*\(\s*\(\s*(?:char|uint8_t|BYTE)\s*\*\s*\)([^;]*)",
            code,
        ):
            tail = match.group(1)
            if not re.search(r"\+\s*0x[0-9A-Fa-f]{2,}", tail):
                continue
            findings.append(
                Finding(
                    path, number, "raw-offset-cast", "error", raw,
                    "byte-offset cast with a literal displacement; declare the structure "
                    "and name the field (check .notes/original-names.md first)",
                )
            )
            break

        # A placeholder used as a function parameter name. A struct field may
        # stay a placeholder while a layout is recovered; a parameter may not,
        # because the caller already proves the argument's role.
        if "(" in code and not in_struct:
            for match in re.finditer(rf"\b{TYPE}[\s*&]+({PLACEHOLDER})\b\s*(?=[,)])", code):
                findings.append(
                    Finding(
                        path, number, "placeholder-parameter", "error", raw,
                        f"parameter named {match.group(1)!r} states an offset or an index, "
                        "not a role; name it from the caller's use",
                    )
                )

        if in_struct:
            for match in re.finditer(rf"\b{TYPE}[\s*&]+({PLACEHOLDER})\b\s*(?:\[[^\]]*\])?\s*;", code):
                findings.append(
                    Finding(
                        path, number, "placeholder-field", "warning", raw,
                        f"field named {match.group(1)!r}; check "
                        ".notes/original-names.md for the developers' own name",
                    )
                )

        # A magic integer stored through a pointer type. Either the type is
        # wrong or the constant is a handle, mask, or sentinel.
        if re.search(rf"\(\s*{TYPE}\s*\*+\s*\)\s*0x[0-9A-Fa-f]{{2,}}", code):
            findings.append(
                Finding(
                    path, number, "magic-pointer", "error", raw,
                    "magic integer cast to a pointer; correct the type or name the constant",
                )
            )

        # A declaration whose name only restates the arithmetic.
        for match in ARITHMETIC_SUFFIX.finditer(code):
            name = re.search(r"\b[A-Za-z_][A-Za-z0-9_]*" + re.escape(match.group(1)) + r"\b", code)
            if not name:
                continue
            findings.append(
                Finding(
                    path, number, "arithmetic-name", "warning", raw,
                    f"{name.group(0)!r} names the arithmetic, not the role; say what reads it",
                )
            )
            break

    return findings


def target_files(staged: bool, explicit: list[str]) -> list[Path]:
    if explicit:
        return [Path(item).resolve() for item in explicit]
    if staged:
        result = subprocess.run(
            ["git", "diff", "--cached", "--name-only", "--diff-filter=ACM"],
            capture_output=True, text=True, check=False, cwd=ROOT,
        )
        paths = []
        for name in result.stdout.split():
            candidate = ROOT / name
            if candidate.suffix in (".c", ".cpp", ".h", ".hpp") and candidate.exists():
                paths.append(candidate)
        return paths
    return sorted(
        path
        for path in SOURCE_ROOT.rglob("*")
        if path.suffix in (".c", ".cpp", ".h", ".hpp")
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Check source for decompiler residue.")
    parser.add_argument("files", nargs="*", help="specific files (default: all of src/)")
    parser.add_argument("--staged", action="store_true", help="check the files in the git index")
    parser.add_argument(
        "--warnings-as-errors", action="store_true", help="fail on warnings too"
    )
    parser.add_argument("--quiet", action="store_true", help="print only the summary")
    args = parser.parse_args()

    findings: list[Finding] = []
    for path in target_files(args.staged, args.files):
        findings.extend(check_file(path))

    errors = [item for item in findings if item.severity == "error"]
    warnings = [item for item in findings if item.severity == "warning"]

    if not args.quiet:
        for finding in sorted(findings, key=lambda item: (str(item.path), item.line)):
            print(finding.render())
        if findings:
            print()

    print(f"lint: {len(errors)} error(s), {len(warnings)} warning(s)")
    if errors:
        print(
            "\nAn exact machine-code match with these findings is a matched\n"
            "transliteration, not a reconstruction. Declare the type, or leave the\n"
            "function a STUB. Check .notes/original-names.md: the retail binary\n"
            "names many of these structures itself."
        )
    return 1 if errors or (args.warnings_as_errors and warnings) else 0


if __name__ == "__main__":
    raise SystemExit(main())
