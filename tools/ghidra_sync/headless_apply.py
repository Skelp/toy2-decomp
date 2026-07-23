#!/usr/bin/env python3
"""Apply an exact-match sync manifest in one headless Ghidra transaction."""

from __future__ import annotations

import argparse
import json
import os
import sys
from contextlib import contextmanager
from pathlib import Path, PurePosixPath
from typing import Any, Iterator


TRANSACTION_NAME = "Toy2 exact source sync"
OPTIONS_NAME = "Toy2 Ghidra Sync"
OPTIONS_KEY = "ownedSourceMapsV2"


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--project-dir", type=Path, required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--program", required=True)
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args()


@contextmanager
def _open_program(project_dir: Path, project_name: str, program_path: str) -> Iterator[Any]:
    from java.lang import Object
    from ghidra.util.task import TaskMonitor
    from reccmp.ghidra.importer.context import open_ghidra_project

    path = PurePosixPath(program_path if program_path.startswith("/") else f"/{program_path}")
    with open_ghidra_project(str(project_dir), project_name, restore_project=False) as project:
        domain_file = project.getProjectData().getFile(str(path))
        if domain_file is None:
            raise RuntimeError(f"Ghidra program not found: {path}")
        consumer = Object()
        program = domain_file.getDomainObject(consumer, True, False, TaskMonitor.DUMMY)
        try:
            yield program
            project.save(program)
        finally:
            program.release(consumer)


def _selected_pdb_functions(compare: Any, selected: set[int]) -> dict[int, Any]:
    from reccmp.ghidra.importer.pdb_extraction import PdbFunctionExtractor

    return {
        item.match_info.orig_addr: item
        for item in PdbFunctionExtractor(compare).get_function_list()
        if item.match_info.orig_addr in selected
    }


def _namespace_path(namespace: Any) -> tuple[str, ...]:
    """Compare namespace meaning rather than transient Java object identity."""
    parts: list[str] = []
    while namespace is not None and not namespace.isGlobal():
        parts.append(str(namespace.getName()))
        namespace = namespace.getParentNamespace()
    return tuple(reversed(parts))


def _make_type_importer(api: Any, extractor: Any) -> Any:
    """Extend reccmp 0.1.6 for unions and callable PDB datatypes."""
    from ghidra.program.model.data import (
        CategoryPath,
        DataTypeConflictHandler,
        FunctionDefinitionDataType,
        ParameterDefinitionImpl,
        UnionDataType,
    )
    from reccmp.cvdump.cvinfo import CVInfoTypeEnum
    from reccmp.ghidra.importer.entity_names import sanitize_name
    from reccmp.ghidra.importer.ghidra_helper import category_path_of
    from reccmp.ghidra.importer.type_importer import PdbTypeImporter

    class Toy2PdbTypeImporter(PdbTypeImporter):
        def __init__(self):
            super().__init__(api, extractor, ignore_types=set())
            self.handled_unions: dict[str, Any] = {}
            self.handled_procedures: dict[Any, Any] = {}

        def import_pdb_type_into_ghidra(self, type_index, slim_for_vbase=False):
            if not type_index.is_scalar():
                definition = self.extraction.compare.types.keys.get(type_index)
                if definition is not None and definition.get("type") == "LF_PROCEDURE":
                    return self._import_procedure(type_index, definition)
            return super().import_pdb_type_into_ghidra(type_index, slim_for_vbase)

        def _import_procedure(self, type_index, definition):
            cached = self.handled_procedures.get(type_index)
            if cached is not None:
                return cached
            name = f"procedure_{type_index:x}"
            function_type = FunctionDefinitionDataType(CategoryPath("/Toy2/functions"), name)
            self.handled_procedures[type_index] = function_type
            function_type.setReturnType(
                self.import_pdb_type_into_ghidra(definition["return_type"])
            )
            arg_list = self.extraction.compare.types.keys.get(definition["arg_list_type"])
            arguments = [] if arg_list is None else list(arg_list.get("args", []))
            variadic = CVInfoTypeEnum.T_NOTYPE in arguments
            parameters = [
                ParameterDefinitionImpl(
                    f"param{index}", self.import_pdb_type_into_ghidra(item), None
                )
                for index, item in enumerate(arguments)
                if item != CVInfoTypeEnum.T_NOTYPE
            ]
            function_type.setArguments(*parameters)
            function_type.setVarArgs(variadic)
            convention = {
                "C Near": "__cdecl",
                "STD Near": "__stdcall",
                "Fast Near": "__fastcall",
                "ThisCall": "__thiscall",
            }.get(definition.get("call_type"))
            if convention is not None:
                function_type.setCallingConvention(convention)
            managed = api.getCurrentProgram().getDataTypeManager().addDataType(
                function_type, DataTypeConflictHandler.KEEP_HANDLER
            )
            self.handled_procedures[type_index] = managed
            return managed

        def _import_union(self, type_pdb):
            sanitized = sanitize_name(type_pdb["name"])
            key = str(sanitized)
            cached = self.handled_unions.get(key)
            if cached is not None:
                return cached
            union = UnionDataType(category_path_of(sanitized.namespace_path), sanitized.base_name)
            union = api.getCurrentProgram().getDataTypeManager().addDataType(
                union, DataTypeConflictHandler.KEEP_HANDLER
            )
            self.handled_unions[key] = union
            while union.getNumComponents() > 0:
                union.delete(0)
            field_list = self.extraction.compare.types.keys.get(type_pdb["field_list_type"])
            if field_list is None:
                raise RuntimeError(f"missing PDB field list for union {key}")
            for member in field_list.get("members", []):
                member_type = self.import_pdb_type_into_ghidra(member.type)
                union.add(member_type, -1, member.name, None)
            if union.getLength() != type_pdb["size"]:
                raise RuntimeError(
                    f"union {key} size mismatch: expected {type_pdb['size']}, got {union.getLength()}"
                )
            return union

    return Toy2PdbTypeImporter()


def _import_exact_types_and_functions(
    program: Any,
    compare: Any,
    records: list[dict[str, Any]],
    *,
    apply_changes: bool = True,
) -> int:
    from ghidra.program.flatapi import FlatProgramAPI
    from reccmp.cvdump.cvinfo import CVInfoTypeEnum
    from reccmp.ghidra.importer.function_importer import PdbFunctionImporter
    from reccmp.ghidra.importer.ghidra_helper import get_class_namespace_and_name
    from reccmp.ghidra.importer.pdb_extraction import PdbFunctionExtractor

    api = FlatProgramAPI(program)
    selected = {int(record["address"], 16) for record in records}
    functions = _selected_pdb_functions(compare, selected)
    missing = selected - functions.keys()
    if missing:
        raise RuntimeError(
            "PDB functions disappeared after manifest creation: "
            + ", ".join(f"0x{address:08x}" for address in sorted(missing))
        )

    extractor = PdbFunctionExtractor(compare)
    type_importer = _make_type_importer(api, extractor)
    changed = 0
    prepared: list[tuple[int, Any, bool, Any]] = []

    # Exact closure includes return/argument/class types (imported by the function
    # importer) and every local/register symbol type recorded for the function.
    for address in sorted(selected):
        pdb_function = functions[address]
        variadic = False
        if pdb_function.signature is not None:
            # reccmp calls MSVC's C Near convention "default".  Make the
            # corresponding x86 convention explicit so Ghidra does not change
            # behavior if its compiler specification defaults are adjusted.
            if pdb_function.signature.call_type == "default":
                pdb_function.signature.call_type = "__cdecl"
            if CVInfoTypeEnum.T_NOTYPE in pdb_function.signature.arglist:
                variadic = True
                pdb_function.signature.arglist = [
                    item
                    for item in pdb_function.signature.arglist
                    if item != CVInfoTypeEnum.T_NOTYPE
                ]
            for symbol in pdb_function.signature.symbols:
                type_importer.import_pdb_type_into_ghidra(symbol.data_type)

        importer = PdbFunctionImporter.build(api, pdb_function, type_importer, [])
        prepared.append((address, pdb_function, variadic, importer))

    # Import functions only after the entire selected type closure exists. A
    # later class import may replace a placeholder namespace created while an
    # earlier function importer was built.
    for address, pdb_function, variadic, importer in prepared:
        # Building an importer can replace a placeholder class namespace while
        # importing the class data type. Resolve the final namespace after all
        # type imports so methods never retain the stale namespace object.
        importer.namespace, importer.name = get_class_namespace_and_name(
            api, pdb_function.match_info.name
        )
        ghidra_address = api.getAddressFactory().getAddress(f"{address:x}")
        ghidra_function = api.getFunctionAt(ghidra_address)
        if ghidra_function is None:
            containing = api.getFunctionContaining(ghidra_address)
            if containing is not None:
                raise RuntimeError(
                    f"0x{address:08x} is inside existing function {containing.getName()}"
                )
            instruction = program.getListing().getInstructionAt(ghidra_address)
            if instruction is None:
                if not apply_changes:
                    changed += 1
                    continue
                api.disassemble(ghidra_address)
                instruction = program.getListing().getInstructionAt(ghidra_address)
                if instruction is None:
                    raise RuntimeError(
                        f"could not disassemble exact function at 0x{address:08x}"
                    )
            if not apply_changes:
                changed += 1
                continue
            ghidra_function = api.createFunction(ghidra_address, "temp")
            if ghidra_function is None:
                raise RuntimeError(f"could not create exact function at 0x{address:08x}")
        # Ghidra thunks inherit the target function's signature and cannot hold
        # an independent source signature. Exact source functions must own the
        # PDB signature even when their machine body is only a tail jump; this
        # changes metadata, not the disassembled bytes.
        if ghidra_function.isThunk():
            ghidra_function.setThunkedFunction(None)
        current_namespace = ghidra_function.getParentNamespace()
        if (
            current_namespace != importer.namespace
            and _namespace_path(current_namespace) == _namespace_path(importer.namespace)
        ):
            # Importing a class can replace Ghidra's namespace object while
            # preserving the same qualified C++ namespace. Canonicalize the
            # object before asking reccmp to compare the remaining signature.
            ghidra_function.setParentNamespace(importer.namespace)
        matches = importer.matches_ghidra_function(ghidra_function)
        if pdb_function.signature is not None and ghidra_function.hasVarArgs() != variadic:
            matches = False
        if not matches:
            changed += 1
            if not apply_changes:
                print(
                    "function drift: "
                    f"0x{address:08x} {pdb_function.match_info.best_name()} "
                    f"namespace={_namespace_path(ghidra_function.getParentNamespace())!r} "
                    f"expected={_namespace_path(importer.namespace)!r} "
                    f"return={ghidra_function.getReturnType()} "
                    f"expected_return={getattr(importer, 'return_type', None)}",
                    file=sys.stderr,
                )
            if apply_changes:
                importer.overwrite_ghidra_function(ghidra_function)
                ghidra_function.setVarArgs(variadic)

    return changed


def _import_all_project_types(program: Any, compare: Any) -> None:
    from ghidra.program.flatapi import FlatProgramAPI
    from reccmp.ghidra.importer.pdb_extraction import PdbFunctionExtractor
    importer = _make_type_importer(FlatProgramAPI(program), PdbFunctionExtractor(compare))
    supported = {"LF_CLASS", "LF_STRUCTURE", "LF_UNION", "LF_ENUM"}
    for type_key, definition in compare.types.keys.items():
        if definition.get("type") in supported and not definition.get("is_forward_ref", False):
            importer.import_pdb_type_into_ghidra(type_key)


def _ensure_fixed_width_aliases(program: Any) -> None:
    from ghidra.program.model.data import (
        CategoryPath,
        CharDataType,
        DataTypeConflictHandler,
        DoubleDataType,
        FloatDataType,
        IntegerDataType,
        LongLongDataType,
        ShortDataType,
        TypedefDataType,
        UnsignedCharDataType,
        UnsignedIntegerDataType,
        UnsignedLongLongDataType,
        UnsignedShortDataType,
    )

    category = CategoryPath("/Toy2/typedefs")
    definitions = {
        "int8_t": CharDataType(),
        "int16_t": ShortDataType(),
        "int32_t": IntegerDataType(),
        "int64_t": LongLongDataType(),
        "uint8_t": UnsignedCharDataType(),
        "uint16_t": UnsignedShortDataType(),
        "uint32_t": UnsignedIntegerDataType(),
        "uint64_t": UnsignedLongLongDataType(),
        "float32_t": FloatDataType(),
        "float64_t": DoubleDataType(),
    }
    manager = program.getDataTypeManager()
    for name, base in definitions.items():
        existing = manager.getDataType(category, name)
        if existing is not None:
            if existing.getLength() != base.getLength():
                raise RuntimeError(f"conflicting datatype {category}/{name}")
            continue
        manager.addDataType(
            TypedefDataType(category, name, base), DataTypeConflictHandler.KEEP_HANDLER
        )


def _owned_maps(program: Any) -> list[dict[str, Any]]:
    raw = program.getOptions(OPTIONS_NAME).getString(OPTIONS_KEY, "[]")
    try:
        value = json.loads(raw)
        return value if isinstance(value, list) else []
    except json.JSONDecodeError:
        return []


def _find_source_file(manager: Any, item: dict[str, Any]) -> Any | None:
    for source_file in manager.getAllSourceFiles():
        if (
            source_file.getPath() == item["path"]
            and source_file.getIdAsString().lower() == item["sha256"].lower()
        ):
            return source_file
    return None


def _partition_owned_maps(
    owned: list[dict[str, Any]], selected: set[str]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    removed = [item for item in owned if item.get("function") in selected]
    retained = [item for item in owned if item.get("function") not in selected]
    return removed, retained


def _remove_owned_source_maps(
    program: Any, selected: set[str]
) -> list[dict[str, Any]]:
    manager = program.getSourceFileManager()
    removed, retained = _partition_owned_maps(_owned_maps(program), selected)
    for item in removed:
        source_file = _find_source_file(manager, item)
        if source_file is None:
            continue
        address = program.getAddressFactory().getAddress(item["address"][2:])
        for entry in list(manager.getSourceMapEntries(address)):
            if (
                entry.getSourceFile() == source_file
                and entry.getLineNumber() == item["line"]
                and entry.getBaseAddress() == address
                and entry.getLength() == item["length"]
            ):
                manager.removeSourceMapEntry(entry)
    return retained


def _apply_source_maps(program: Any, records: list[dict[str, Any]], root: Path) -> int:
    from ghidra.program.database.sourcemap import SourceFile, SourceFileIdType, UserDataPathTransformer

    manager = program.getSourceFileManager()
    selected = {record["address"] for record in records}
    owned = _remove_owned_source_maps(program, selected)
    added = 0
    for record in records:
        for item in record.get("source_ranges", []):
            source_file = _find_source_file(manager, item)
            if source_file is None:
                source_file = SourceFile(
                    item["path"], SourceFileIdType.SHA256, bytes.fromhex(item["sha256"])
                )
                manager.addSourceFile(source_file)
            address = program.getAddressFactory().getAddress(item["address"][2:])
            manager.addSourceMapEntry(source_file, item["line"], address, item["length"])
            owned.append({**item, "function": record["address"]})
            added += 1

    program.getOptions(OPTIONS_NAME).setString(OPTIONS_KEY, json.dumps(owned, sort_keys=True))
    try:
        transformer = UserDataPathTransformer.getPathTransformer(program)
        transformer.addDirectoryTransform("/toy2-decomp", str(root.resolve()))
    except Exception:
        # Source maps remain valid if the per-user path transform is unavailable.
        pass
    return added


def _source_map_drift(program: Any, records: list[dict[str, Any]]) -> int:
    manager = program.getSourceFileManager()
    missing = 0
    for record in records:
        for item in record.get("source_ranges", []):
            source_file = _find_source_file(manager, item)
            if source_file is None:
                missing += 1
                continue
            address = program.getAddressFactory().getAddress(item["address"][2:])
            found = any(
                entry.getSourceFile() == source_file
                and entry.getLineNumber() == item["line"]
                and entry.getBaseAddress() == address
                and entry.getLength() == item["length"]
                for entry in manager.getSourceMapEntries(address)
            )
            if not found:
                missing += 1
    return missing


def main() -> int:
    args = _parse_args()
    args.root = args.root.resolve()
    sys.path.insert(0, str(args.root / "tools"))

    data = json.loads(args.manifest.read_text(encoding="utf-8"))
    records = [item for item in data["functions"] if item.get("reason") is None]
    if not records:
        raise RuntimeError("manifest contains no eligible exact functions")

    from pyghidra import HeadlessPyGhidraLauncher

    HeadlessPyGhidraLauncher().start()

    from reccmp.compare.core import Compare
    from ghidra_sync.manifest import load_target

    target = load_target(args.root)
    compare = Compare.from_target(target)

    with _open_program(args.project_dir, args.project, args.program) as program:
        actual_hash = program.getExecutableSHA256().lower()
        if actual_hash != data["original_sha256"].lower():
            raise RuntimeError(
                f"Ghidra program hash mismatch: expected {data['original_sha256']}, got {actual_hash}"
            )

        transaction = program.startTransaction(TRANSACTION_NAME)
        commit = False
        try:
            _ensure_fixed_width_aliases(program)
            if data.get("type_scope") == "all":
                _import_all_project_types(program, compare)
            changed = _import_exact_types_and_functions(
                program, compare, records, apply_changes=not args.verify_only
            )
            if args.verify_only:
                mappings = _source_map_drift(program, records)
            else:
                mappings = _apply_source_maps(program, records, args.root)
                commit = True
        finally:
            program.endTransaction(transaction, commit)

    if args.verify_only:
        print(json.dumps({"function_drift": changed, "source_map_drift": mappings}))
        return 1 if changed or mappings else 0
    print(json.dumps({"functions_changed": changed, "source_ranges": mappings}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"Ghidra sync failed: {error}", file=sys.stderr)
        raise
