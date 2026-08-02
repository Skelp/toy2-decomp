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
    characteristics: int = 0


@dataclass(frozen=True)
class DataDirectory:
    virtual_address: int
    size: int


@dataclass(frozen=True)
class ImageMetadata:
    """PE fields that describe the image and its raw file layout."""

    file_size: int
    image_base: int
    header_size: int
    sections: tuple[Section, ...]
    pe_offset: int = 0
    optional_offset: int = 0
    directories: tuple[DataDirectory, ...] = ()


@dataclass(frozen=True)
class ResourceEntry:
    path: tuple[str | int, ...]
    data: bytes
    code_page: int


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
    metadata = parse_image_metadata(data)
    return data, metadata.image_base, metadata.sections


def parse_image_metadata(data: bytes) -> ImageMetadata:
    """Parse the raw layout fields from a PE32 image."""

    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("The retail executable does not have a valid DOS header.")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe : pe + 4] != b"PE\0\0":
        raise ValueError("The retail executable does not have a valid PE header.")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if optional_size < 96 or optional + optional_size > len(data):
        raise ValueError("The retail executable has an invalid optional header.")
    if struct.unpack_from("<H", data, optional)[0] != 0x10B:
        raise ValueError("The retail executable is not a PE32 image.")
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    header_size = struct.unpack_from("<I", data, optional + 60)[0]
    directory_count = struct.unpack_from("<I", data, optional + 92)[0]
    directory_count = min(directory_count, 16)
    directory_offset = optional + 96
    if directory_offset + directory_count * 8 > optional + optional_size:
        raise ValueError("The retail executable has an invalid data directory.")
    directories = tuple(
        DataDirectory(
            virtual_address=struct.unpack_from(
                "<I", data, directory_offset + index * 8
            )[0],
            size=struct.unpack_from(
                "<I", data, directory_offset + index * 8 + 4
            )[0],
        )
        for index in range(directory_count)
    )
    section_table = optional + optional_size
    if section_count > 96 or section_table + section_count * 40 > len(data):
        raise ValueError("The retail executable has an invalid section table.")
    sections = []
    for index in range(section_count):
        offset = section_table + index * 40
        sections.append(
            Section(
                name=data[offset : offset + 8].rstrip(b"\0").decode("ascii", "replace"),
                virtual_address=image_base + struct.unpack_from("<I", data, offset + 12)[0],
                virtual_size=struct.unpack_from("<I", data, offset + 8)[0],
                raw_pointer=struct.unpack_from("<I", data, offset + 20)[0],
                raw_size=struct.unpack_from("<I", data, offset + 16)[0],
                characteristics=struct.unpack_from("<I", data, offset + 36)[0],
            )
        )
    return ImageMetadata(
        file_size=len(data),
        image_base=image_base,
        header_size=header_size,
        sections=tuple(sections),
        pe_offset=pe,
        optional_offset=optional,
        directories=directories,
    )


def read_image_metadata(path: Path = EXE_PATH) -> ImageMetadata:
    """Read PE layout metadata from an executable."""

    return parse_image_metadata(path.read_bytes())


def address_to_file_offset(metadata: ImageMetadata, address: int) -> int | None:
    """Translate an absolute virtual address with explicit image metadata."""

    for section in metadata.sections:
        if (
            section.virtual_address
            <= address
            < section.virtual_address + section.raw_size
        ):
            return section.raw_pointer + address - section.virtual_address
    return None


def parse_resources(data: bytes, metadata: ImageMetadata) -> tuple[ResourceEntry, ...]:
    """Parse leaf payloads from a PE resource directory."""

    resource_index = 2
    if len(metadata.directories) <= resource_index:
        return ()
    directory = metadata.directories[resource_index]
    if directory.virtual_address == 0 or directory.size == 0:
        return ()
    base = address_to_file_offset(
        metadata, metadata.image_base + directory.virtual_address
    )
    if base is None or base + directory.size > len(data):
        raise ValueError("The PE resource directory is outside the file.")

    def checked(offset: int, size: int) -> int:
        absolute = base + offset
        if offset < 0 or absolute < base or absolute + size > base + directory.size:
            raise ValueError("The PE resource directory contains an invalid offset.")
        return absolute

    def name(value: int) -> str | int:
        if value & 0x80000000 == 0:
            return value
        absolute = checked(value & 0x7FFFFFFF, 2)
        length = struct.unpack_from("<H", data, absolute)[0]
        end = absolute + 2 + length * 2
        if end > base + directory.size:
            raise ValueError("The PE resource directory contains an invalid name.")
        return data[absolute + 2 : end].decode("utf-16-le", "replace")

    output: list[ResourceEntry] = []
    active: set[int] = set()

    def visit(offset: int, path: tuple[str | int, ...], depth: int) -> None:
        if depth > 8 or offset in active:
            raise ValueError("The PE resource directory contains a cycle.")
        active.add(offset)
        absolute = checked(offset, 16)
        named_count, id_count = struct.unpack_from("<HH", data, absolute + 12)
        count = named_count + id_count
        entries = checked(offset + 16, count * 8)
        for index in range(count):
            name_value, child_value = struct.unpack_from(
                "<II", data, entries + index * 8
            )
            child_path = path + (name(name_value),)
            child_offset = child_value & 0x7FFFFFFF
            if child_value & 0x80000000:
                visit(child_offset, child_path, depth + 1)
                continue
            leaf = checked(child_offset, 16)
            data_rva, size, code_page, _ = struct.unpack_from("<IIII", data, leaf)
            payload_offset = address_to_file_offset(
                metadata, metadata.image_base + data_rva
            )
            if payload_offset is None or payload_offset + size > len(data):
                raise ValueError("A PE resource payload is outside the file.")
            output.append(
                ResourceEntry(
                    path=child_path,
                    data=data[payload_offset : payload_offset + size],
                    code_page=code_page,
                )
            )
        active.remove(offset)

    visit(0, (), 0)
    return tuple(output)


def read_resources(path: Path = EXE_PATH) -> tuple[ResourceEntry, ...]:
    """Read the resource leaf payloads from a PE executable."""

    data = path.read_bytes()
    return parse_resources(data, parse_image_metadata(data))


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
