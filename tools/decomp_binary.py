#!/usr/bin/env python3
"""Read-only access to the retail executable's sections and string literals.

The retail build kept its assert and log text. That text is the best naming
evidence in the binary, because it contains expressions the original developers
wrote:

- `drawb->VerticeCount[i]` gives a structure pointer name, a field name, and
  the loop variable.
- `C:\\projects\\nu3d\\objload.c` with a line number gives the original
  translation unit and the function's position inside it.

This module locates those literals and maps a virtual address to the file
offset that holds it. It parses only the PE section table, so it needs no
Ghidra and no build.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE_PATH = ROOT / "original" / "toy2.exe"

# An expression a developer wrote, e.g. `drawb->VerticeCount[i]`. Requires the
# arrow so ordinary prose does not match.
FIELD_EXPRESSION_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\s*->\s*[A-Za-z_][A-Za-z0-9_]*")
# An original translation unit path, e.g. `C:\projects\nu3d\objload.c`.
# Anchor the extension at the end of the path so `.cpp` is not truncated to
# `.c` by a non-greedy prefix.
SOURCE_PATH_RE = re.compile(
    r"[A-Za-z]:\\[\w\\.]*?([\w]+\.(?:cpp|cxx|hpp|c|h))(?![\w])", re.IGNORECASE
)


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_pointer: int
    raw_size: int


@dataclass(frozen=True)
class StringLiteral:
    address: int
    text: str

    @property
    def field_expressions(self) -> list[str]:
        return [match.group(0) for match in FIELD_EXPRESSION_RE.finditer(self.text)]

    @property
    def source_file(self) -> str | None:
        found = SOURCE_PATH_RE.search(self.text)
        return found.group(1) if found else None


@lru_cache(maxsize=1)
def _image() -> tuple[bytes, int, tuple[Section, ...]]:
    data = EXE_PATH.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    sections = []
    for index in range(section_count):
        offset = pe + 24 + optional_size + index * 40
        sections.append(
            Section(
                name=data[offset : offset + 8].rstrip(b"\0").decode("ascii", "replace"),
                virtual_address=image_base + struct.unpack_from("<I", data, offset + 12)[0],
                virtual_size=struct.unpack_from("<I", data, offset + 8)[0],
                raw_pointer=struct.unpack_from("<I", data, offset + 20)[0],
                raw_size=struct.unpack_from("<I", data, offset + 16)[0],
            )
        )
    return data, image_base, tuple(sections)


def available() -> bool:
    return EXE_PATH.exists()


def sections() -> tuple[Section, ...]:
    return _image()[2]


def file_offset(address: int) -> int | None:
    """Translate a virtual address to a file offset, or None when unmapped."""

    for section in sections():
        if section.virtual_address <= address < section.virtual_address + section.raw_size:
            return section.raw_pointer + (address - section.virtual_address)
    return None


def read_bytes(address: int, size: int) -> bytes | None:
    """Read bytes from one mapped section without crossing its raw extent."""

    if size < 0:
        raise ValueError("size must not be negative")
    data = _image()[0]
    for section in sections():
        section_end = section.virtual_address + section.raw_size
        if section.virtual_address <= address <= section_end:
            available_size = section_end - address
            if size > available_size:
                return None
            offset = section.raw_pointer + (address - section.virtual_address)
            return data[offset : offset + size]
    return None


def read_string(address: int, limit: int = 400) -> str | None:
    """Read a NUL-terminated printable string at a virtual address."""

    data = _image()[0]
    offset = file_offset(address)
    if offset is None:
        return None
    end = data.find(b"\0", offset, offset + limit)
    if end < 0:
        return None
    raw = data[offset:end]
    if len(raw) < 4:
        return None
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    # Require mostly printable text so a coincidental byte run is not reported.
    printable = sum(1 for character in text if 0x20 <= ord(character) < 0x7F or character == "\t")
    if printable < len(text):
        return None
    return text


def strings_referenced_by(operand_addresses: list[int]) -> list[StringLiteral]:
    """Return the string literals found at the given candidate addresses."""

    found: dict[int, StringLiteral] = {}
    for address in operand_addresses:
        text = read_string(address)
        if text is not None:
            found[address] = StringLiteral(address=address, text=text)
    return [found[key] for key in sorted(found)]


def iter_all_strings(minimum_length: int = 6):
    """Yield every printable NUL-terminated literal in the data sections."""

    data, _, all_sections = _image()
    for section in all_sections:
        if section.name not in (".rdata", ".data"):
            continue
        block = data[section.raw_pointer : section.raw_pointer + section.raw_size]
        for match in re.finditer(rb"[\x20-\x7e\t]{%d,}" % minimum_length, block):
            yield StringLiteral(
                address=section.virtual_address + match.start(),
                text=match.group(0).decode("ascii"),
            )
