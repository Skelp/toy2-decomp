#!/usr/bin/env python3
"""Export byte-weighted comparisons for initialized project globals."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import NamedTuple

from reccmp.compare import Compare
from reccmp.compare.db import ReccmpMatch
from reccmp.compare.variables import (
    BssState,
    ComparedOffset,
    DataBlock,
    VariableComparator,
    pointer_display,
)
from reccmp.cvdump.cvinfo import CvdumpTypeKey
from reccmp.cvdump.types import CvdumpIntegrityError, CvdumpKeyError
from reccmp.project.detect import GhidraConfig, RecCmpTarget, ReportConfig
from reccmp.types import ImageId


class UnionStorage(NamedTuple):
    """Describe one union storage range in a variable."""

    offset: int
    size: int
    name: str


def union_storage_ranges(types, type_key, base: int = 0, name: str = ""):
    """Find outer union storage ranges without selecting a union member."""

    item = types.get(type_key)
    if item.is_array():
        assert item.array_type is not None
        assert item.array_length is not None
        assert item.array_element_size is not None
        return [
            storage
            for index in range(item.array_length)
            for storage in union_storage_ranges(
                types,
                item.array_type,
                base + index * item.array_element_size,
                f"{name}[{index}]",
            )
        ]

    if not item.is_struct():
        return []

    raw_type = types.from_key(item.key)
    if raw_type.get("type") == "LF_UNION":
        assert item.size is not None
        return [UnionStorage(base, item.size, f"{name} (union)".strip())]

    assert item.members is not None
    ranges = []
    for member in item.members:
        member_name = f"{name}.{member.name}" if name else member.name
        ranges.extend(
            union_storage_ranges(
                types, member.type, base + member.offset, member_name
            )
        )
    return ranges


def union_storage_comparison(
    comparator: VariableComparator,
    variable,
    original: DataBlock,
    recompiled: DataBlock,
    storage: UnionStorage,
) -> ComparedOffset:
    """Compare one union range with relocation evidence for active pointers."""

    start = storage.offset
    end = start + storage.size
    original_relocations = {
        offset
        for offset in range(storage.size)
        if variable.orig_addr + start + offset in comparator.orig_bin.relocations
    }
    recompiled_relocations = {
        offset
        for offset in range(storage.size)
        if variable.recomp_addr + start + offset in comparator.recomp_bin.relocations
    }

    original_bytes = original.data[start:end]
    recompiled_bytes = recompiled.data[start:end]
    match = True

    pointer_values = []
    ignored = set()
    pointer_offsets = original_relocations | recompiled_relocations
    if pointer_offsets:
        for offset in sorted(pointer_offsets):
            if offset + 4 > storage.size:
                match = False
                break
            ignored.update(range(offset, offset + 4))
            original_pointer = int.from_bytes(
                original_bytes[offset : offset + 4], "little"
            )
            recompiled_pointer = int.from_bytes(
                recompiled_bytes[offset : offset + 4], "little"
            )
            pointer_match = comparator.is_pointer_match(
                original_pointer, recompiled_pointer
            ) or comparator.is_pointer_match_to_offset(
                original_pointer, recompiled_pointer
            )
            match = match and pointer_match
            pointer_values.append(
                (
                    offset,
                    pointer_display(
                        comparator.db,
                        comparator.types,
                        ImageId.ORIG,
                        original_pointer,
                    ),
                    pointer_display(
                        comparator.db,
                        comparator.types,
                        ImageId.RECOMP,
                        recompiled_pointer,
                    ),
                )
            )

    match = match and all(
        original_bytes[index] == recompiled_bytes[index]
        for index in range(storage.size)
        if index not in ignored
    )

    bss_conflict = (
        original.bss == BssState.NO and recompiled.bss == BssState.YES
    ) or (recompiled.bss == BssState.NO and original.bss == BssState.YES)
    match = match and not bss_conflict

    def display(block, raw_bytes, relocations, pointers, image_index):
        if block.bss == BssState.YES:
            return "(uninitialized)"
        relocation_text = ", ".join(f"+0x{offset:X}" for offset in sorted(relocations))
        pointer_text = ", ".join(
            f"+0x{offset:X}: {values[image_index]}"
            for offset, *values in pointers
        )
        parts = [f"Raw union bytes {raw_bytes.hex()}"]
        if relocation_text:
            parts.append(f"relocations {relocation_text}")
        if pointer_text:
            parts.append(pointer_text)
        return ". ".join(parts)

    return ComparedOffset(
        offset=storage.offset,
        name=storage.name,
        match=match,
        values=(
            display(
                original,
                original_bytes,
                original_relocations,
                pointer_values,
                0,
            ),
            display(
                recompiled,
                recompiled_bytes,
                recompiled_relocations,
                pointer_values,
                1,
            ),
        ),
    )


def variable_size(engine: Compare, variable) -> int:
    """Get the precise PDB type size, or use the matched symbol size."""

    type_key = variable.get("data_type")
    if type_key:
        try:
            size = engine.types.get(CvdumpTypeKey(type_key)).size
            if size is not None:
                return size
        except (CvdumpIntegrityError, CvdumpKeyError, KeyError, ValueError):
            pass
    size = variable.any_size()
    return int(size or 0)


def is_physically_stored(engine: Compare, address: int, size: int) -> bool:
    """Return true when the complete retail variable has bytes in the file."""

    return any(
        section.virtual_address <= address
        and address + size <= section.virtual_address + section.size_of_raw_data
        for section in engine.orig_bin.sections
    )


def scalar_sizes(engine: Compare, variable, item, size: int) -> list[int]:
    """Return the byte width for each result from the variable comparator."""

    type_key = variable.get("data_type")
    if type_key and not item.raw_only:
        try:
            return [
                scalar.size
                for scalar in engine.types.get_scalars_gapless(CvdumpTypeKey(type_key))
            ]
        except (CvdumpIntegrityError, CvdumpKeyError, KeyError, ValueError):
            pass
    return [1] * size


def comparison_fields(engine, comparator, variable, item, size):
    """Return comparison fields with one result for each union range."""

    widths = scalar_sizes(engine, variable, item, size)
    ordinary = list(zip(item.compared, widths))
    type_key = variable.get("data_type")
    if not type_key or item.raw_only:
        return ordinary

    try:
        ranges = union_storage_ranges(engine.types, CvdumpTypeKey(type_key))
    except (CvdumpIntegrityError, CvdumpKeyError, KeyError, ValueError):
        return ordinary
    if not ranges:
        return ordinary

    original = DataBlock.read(variable.orig_addr, size, engine.orig_bin)
    recompiled = DataBlock.read(variable.recomp_addr, size, engine.recomp_bin)
    output = [
        (compared, width)
        for compared, width in ordinary
        if not any(
            storage.offset <= compared.offset < storage.offset + storage.size
            for storage in ranges
        )
    ]
    output.extend(
        (
            union_storage_comparison(
                comparator, variable, original, recompiled, storage
            ),
            storage.size,
        )
        for storage in ranges
    )
    return sorted(output, key=lambda field: field[0].offset)


def export_variables(engine: Compare) -> dict:
    """Compare all matched globals that occupy physical retail bytes."""

    comparator = VariableComparator(
        # reccmp exposes no public database accessor for its data comparator.
        db=engine._db,  # pylint: disable=protected-access
        types=engine.types,
        orig_bin=engine.orig_bin,
        recomp_bin=engine.recomp_bin,
    )
    variables = []
    unscored_variables = []
    for variable in engine.get_variables():
        size = variable_size(engine, variable)
        if size <= 0:
            unscored_variables.append(
                {
                    "original_address": variable.orig_addr,
                    "name": variable.name,
                    "size": size,
                    "reason": "unknown_size",
                }
            )
            continue
        if not is_physically_stored(engine, variable.orig_addr, size):
            reason = (
                "bss_only"
                if engine.orig_bin.addr_is_uninitialized(variable.orig_addr)
                else "no_physical_storage"
            )
            unscored_variables.append(
                {
                    "original_address": variable.orig_addr,
                    "name": variable.name,
                    "size": size,
                    "reason": reason,
                }
            )
            continue

        item = comparator.compare_variable(variable)
        fields = []
        matched_bytes = 0
        for compared, width in comparison_fields(
            engine, comparator, variable, item, size
        ):
            width = min(width, max(0, size - compared.offset))
            if width <= 0:
                continue
            if compared.match:
                matched_bytes += width
            fields.append(
                {
                    "offset": compared.offset,
                    "size": width,
                    "name": compared.name or "",
                    "match": compared.match,
                    "original": compared.values[0],
                    "recompiled": compared.values[1],
                }
            )

        variables.append(
            {
                "original_address": variable.orig_addr,
                "recompiled_address": variable.recomp_addr,
                "name": variable.name,
                "size": size,
                "matched_bytes": matched_bytes,
                "score": matched_bytes / size,
                "result": (
                    "error"
                    if item.error
                    else "match"
                    if matched_bytes == size
                    else "warn"
                    if item.raw_only
                    else "diff"
                ),
                "raw_only": item.raw_only,
                "error": item.error,
                "fields": fields,
            }
        )

    return {
        "format": 1,
        "variables": variables,
        "unscored_variables": unscored_variables,
        "variable_count": len(variables),
        "scored_bytes": sum(item["size"] for item in variables),
        "explained_bytes": sum(item["matched_bytes"] for item in variables),
    }


def export_sections(engine: Compare, payload: dict) -> dict:
    """Summarize semantic evidence for each scored data section."""

    variables = payload["variables"]["variables"]
    vtables = payload["vtables"]["tables"]
    rows = []
    for section in engine.orig_bin.sections:
        if section.name not in (".data", ".rdata", ".idata", ".reloc"):
            continue
        size = section.size_of_raw_data
        evidence_bytes = 0
        explained_bytes = 0.0
        if section.name in (".data", ".rdata"):
            for item in variables + vtables:
                address = int(item.get("original_address", 0))
                item_size = int(item.get("size", 0))
                if (
                    section.virtual_address <= address
                    and address + item_size <= section.virtual_address + size
                ):
                    evidence_bytes += item_size
                    explained_bytes += float(item.get("matched_bytes", 0))
        elif section.name == ".idata":
            evidence_bytes = size
            explained_bytes = size * float(payload["imports"].get("score", 0))
        else:
            evidence_bytes = size
            explained_bytes = size * float(payload["relocations"].get("score", 0))
        rows.append(
            {
                "name": section.name,
                "size": size,
                "evidence_bytes": evidence_bytes,
                "explained_bytes": explained_bytes,
                "score": explained_bytes / size if size else 1.0,
            }
        )
    return {
        "sections": rows,
        "scored_bytes": sum(item["size"] for item in rows),
        "explained_bytes": sum(item["explained_bytes"] for item in rows),
    }


def export_vtables(engine: Compare) -> dict:
    """Export byte-weighted virtual-table comparison results."""

    matches = {item.orig_addr: item for item in engine.get_vtables()}
    tables = []
    for result in engine.compare_vtables():
        match = matches.get(result.orig_addr)
        if match is None:
            continue
        size = match.any_size(ImageId.ORIG)
        if size <= 0 or not is_physically_stored(engine, result.orig_addr, size):
            continue
        score = result.effective_ratio
        tables.append(
            {
                "original_address": result.orig_addr,
                "recompiled_address": result.recomp_addr,
                "name": result.name,
                "size": size,
                "matched_bytes": size * score,
                "score": score,
            }
        )
    return {
        "tables": tables,
        "table_count": len(tables),
        "scored_bytes": sum(item["size"] for item in tables),
        "explained_bytes": sum(item["matched_bytes"] for item in tables),
    }


def export_imports(engine: Compare) -> dict:
    """Compare imported modules and symbols without using table addresses."""

    def key(item):
        return (item.module.lower(), item.name, item.ordinal)

    original = list(engine.orig_bin.imports)
    recompiled_keys = {key(item) for item in engine.recomp_bin.imports}
    entries = [
        {
            "module": item.module,
            "name": item.name,
            "ordinal": item.ordinal,
            "match": key(item) in recompiled_keys,
        }
        for item in original
    ]
    return {
        "entries": entries,
        "entry_count": len(entries),
        "matched_entries": sum(item["match"] for item in entries),
        "score": sum(item["match"] for item in entries) / len(entries)
        if entries
        else 1.0,
    }


def export_relocations(engine: Compare) -> dict:
    """Map retail relocation sites through matched functions and globals."""

    original = sorted(engine.orig_bin.relocations)
    recompiled = engine.recomp_bin.relocations
    matched = 0
    mapped = 0
    for address in original:
        entity = engine._db.get(  # pylint: disable=protected-access
            ImageId.ORIG, address, exact=False
        )
        if not isinstance(entity, ReccmpMatch):
            continue
        mapped += 1
        recompiled_address = entity.recomp_addr + address - entity.orig_addr
        if recompiled_address in recompiled:
            matched += 1
    return {
        "entry_count": len(original),
        "mapped_entries": mapped,
        "matched_entries": matched,
        "score": matched / len(original) if original else 1.0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--recompiled", type=Path, required=True)
    parser.add_argument("--pdb", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    target = RecCmpTarget(
        target_id="TOY2",
        filename=args.original.name,
        sha256="",
        encoding=None,
        source_paths=(args.source_root.resolve(),),
        ghidra_config=GhidraConfig(),
        report_config=ReportConfig(),
        original_path=args.original.resolve(),
        recompiled_path=args.recompiled.resolve(),
        recompiled_pdb=args.pdb.resolve(),
    )
    engine = Compare.from_target(target)
    payload = {
        "format": 1,
        "variables": export_variables(engine),
        "vtables": export_vtables(engine),
        "imports": export_imports(engine),
        "relocations": export_relocations(engine),
        "debug": {
            "original_pdb": engine.orig_bin.pdb_filename,
            "recompiled_pdb": engine.recomp_bin.pdb_filename,
        },
    }
    payload["sections"] = export_sections(engine, payload)
    args.output.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(
        f"Data evidence: {payload['variables']['variable_count']:,} variables, "
        f"{payload['variables']['explained_bytes']:,} / "
        f"{payload['variables']['scored_bytes']:,} bytes explained."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
