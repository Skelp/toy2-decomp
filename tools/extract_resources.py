#!/usr/bin/env python3
"""Extract the retail PE resource tree for the game build."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    from tools import decomp_binary
except ModuleNotFoundError:  # Direct script imports use tools/ as sys.path[0].
    import decomp_binary  # type: ignore[no-redef]


def encode_identifier(value: str | int) -> bytes:
    """Encode one numeric or named Win32 resource identifier."""

    if isinstance(value, int):
        if value < 0 or value > 0xFFFF:
            raise ValueError("A numeric resource identifier must fit in 16 bits.")
        return struct.pack("<HH", 0xFFFF, value)
    if "\0" in value or "\r" in value or "\n" in value:
        raise ValueError("A resource name contains an invalid control character.")
    return value.encode("utf-16-le") + b"\0\0"


def encode_record(
    resource_type: str | int,
    resource_name: str | int,
    language: int,
    data: bytes,
) -> bytes:
    """Encode one record in the Win32 binary resource format."""

    if language < 0 or language > 0xFFFF:
        raise ValueError("A resource language must fit in 16 bits.")
    header = bytearray(struct.pack("<II", len(data), 0))
    header.extend(encode_identifier(resource_type))
    header.extend(encode_identifier(resource_name))
    header.extend(b"\0" * (-len(header) % 4))
    header.extend(struct.pack("<IHHII", 0, 0x0030, language, 0, 0))
    struct.pack_into("<I", header, 4, len(header))
    return bytes(header) + data + b"\0" * (-len(data) % 4)


def encode_resource_file(entries: tuple[decomp_binary.ResourceEntry, ...]) -> bytes:
    """Encode all PE resource leaves in a Win32 binary resource file."""

    if not entries:
        raise ValueError("The retail executable does not contain resources.")

    output = bytearray(encode_record(0, 0, 0, b""))
    paths: set[tuple[str | int, ...]] = set()
    for entry in entries:
        if len(entry.path) != 3:
            raise ValueError("A resource leaf does not have a type, name, and language.")
        resource_type, resource_name, language = entry.path
        if not isinstance(language, int) or language < 0 or language > 0xFFFF:
            raise ValueError("A resource language must fit in 16 bits.")
        if entry.path in paths:
            raise ValueError("The retail executable contains a duplicate resource leaf.")
        paths.add(entry.path)
        output.extend(
            encode_record(resource_type, resource_name, language, entry.data)
        )
    return bytes(output)


def write_if_changed(path: Path, data: bytes) -> None:
    """Write a generated file only when its content changes."""

    if path.is_file() and path.read_bytes() == data:
        return
    path.write_bytes(data)


def extract_resources(input_path: Path, output_path: Path) -> None:
    """Extract all PE resource leaves into a binary resource file."""

    if not input_path.is_file():
        raise ValueError(f"The retail executable does not exist: {input_path}")

    try:
        entries = decomp_binary.read_resources(input_path)
    except (OSError, ValueError) as error:
        raise ValueError(
            f"The extractor cannot read resources from {input_path}: {error}"
        ) from error

    resource_file = encode_resource_file(entries)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(output_path, resource_file)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract all resources from a PE executable for a game build."
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        extract_resources(args.input, args.output)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
