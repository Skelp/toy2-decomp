"""Sync datatypes from source to Ghidra.

Handles:
- Primitive typedefs (uint8_t → uchar)
- Struct/class definitions (Vector3F, Plane, etc.)
- Type dependencies (topological sort)
- Caching to avoid redundant Ghidra calls
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .ghidra_client import _run_ghidra


# ---------------------------------------------------------------------------
# Type definitions
# ---------------------------------------------------------------------------

@dataclass
class StructField:
    """A single field in a struct."""
    type_name: str
    field_name: str


@dataclass
class StructDefinition:
    """A struct/class definition extracted from source."""
    name: str
    fields: list[StructField] = field(default_factory=list)
    size: Optional[int] = None  # None = let Ghidra calculate


@dataclass
class TypedefDefinition:
    """A typedef definition."""
    name: str
    base_type: str


TypeDefinition = StructDefinition | TypedefDefinition


# ---------------------------------------------------------------------------
# Known primitive mappings (C type → Ghidra type)
# ---------------------------------------------------------------------------

PRIMITIVE_MAP = {
    "uint8_t": "uchar",
    "int8_t": "char",
    "uint16_t": "ushort",
    "int16_t": "short",
    "uint32_t": "ulong",
    "int32_t": "long",
    "uintptr_t": "pointer",
    "size_t": "ulong",
    "ptrdiff_t": "long",
    "uint64_t": "ulonglong",
    "int64_t": "longlong",
    "float": "float",
    "double": "double",
    "bool": "bool",
    "char": "char",
    "short": "short",
    "int": "long",  # Ghidra uses long for 32-bit int
    "long": "long",
    "void": "void",
}

# Set of types already known to exist in Ghidra (cached)
_known_types: set[str] = set()


def _is_primitive(type_name: str) -> bool:
    """Check if a type name is a known primitive."""
    return type_name in PRIMITIVE_MAP or type_name in _known_types


def _get_base_type(type_name: str) -> Optional[str]:
    """Get the Ghidra base type for a primitive, or None if not a primitive."""
    return PRIMITIVE_MAP.get(type_name)


# ---------------------------------------------------------------------------
# Source parsing
# ---------------------------------------------------------------------------

def parse_struct_from_source(source_text: str, type_name: str) -> Optional[StructDefinition]:
    """Parse a struct/class definition from source text.

    Looks for patterns like:
        struct Vector3F { float x; float y; float z; };
        struct Vector3F { float x; float y; float z; };  (multi-line)
        class Vector3F { ... };

    Args:
        source_text: Full source file content
        type_name: Name of the struct to find

    Returns:
        StructDefinition or None if not found.
    """
    # Pattern: struct/ClassName Name { ... }
    # Handles both single-line and multi-line definitions
    pattern = rf'(?:struct|class)\s+{re.escape(type_name)}\s*{{([^}}]*)}}'
    match = re.search(pattern, source_text, re.DOTALL)
    if not match:
        return None

    body = match.group(1)
    fields = _parse_struct_body(body)

    if fields is None:
        return None

    return StructDefinition(name=type_name, fields=fields)


def _parse_struct_body(body: str) -> Optional[list[StructField]]:
    """Parse struct body into fields.

    Handles:
        float x;
        Vector3F normal;
        int32_t m00;
    """
    fields = []
    # Match lines like: type name; or type name[dimension];
    for line in body.split('\n'):
        line = line.strip()
        if not line or line.startswith('//') or line.startswith('static ') or line.startswith('void ') or line.startswith('int ') or line.startswith('float ') or line.startswith('bool '):
            # Skip method declarations (they have () or {)
            if '(' in line or '{' in line:
                continue
            # Skip empty or comment lines

        # Skip if it looks like a method declaration
        if '(' in line or '->' in line or 'this' in line:
            continue

        # Match: type name; or type name[...];
        match = re.match(r'([\w:*]+)\s+(\w+)(?:\s*\[[^\]]*\])?\s*;', line)
        if match:
            type_str = match.group(1).strip()
            name_str = match.group(2).strip()
            # Skip if type looks like a keyword
            if type_str in ('if', 'else', 'for', 'while', 'switch', 'return', 'catch'):
                continue
            fields.append(StructField(type_name=type_str, field_name=name_str))

    return fields if fields else None


# ---------------------------------------------------------------------------
# Type dependency analysis
# ---------------------------------------------------------------------------

def extract_type_dependencies(sig: str) -> set[str]:
    """Extract all type names referenced in a signature.

    Args:
        sig: Signature string (e.g. "float GetSignedDistanceToPlane(Vector3F*, Plane*)")

    Returns:
        Set of type names (e.g. {"Vector3F", "Plane", "float"})
    """
    import re
    # Match identifiers that look like types (not primitives)
    # This is a heuristic - we'll check against known types later
    params_match = re.search(r'\(([^)]*)\)', sig)
    if not params_match:
        return set()

    params = params_match.group(1)
    types = set()

    for param in params.split(','):
        param = param.strip()
        if not param:
            continue
        # Remove const/volatile
        param = re.sub(r'\bconst\b', '', param).strip()
        param = re.sub(r'\bvolatile\b', '', param).strip()
        # Extract the base type (before any * or &)
        base = re.split(r'[*&]', param)[0].strip()
        if base:
            types.add(base)

    # Also check return type
    return_type = sig.split('(')[0].strip()
    if return_type:
        types.add(return_type)

    return types


def topological_sort_types(
    types: set[str],
    parse_fn: callable
) -> list[str]:
    """Sort types by dependency order.

    Args:
        types: Set of type names to sort
        parse_fn: Function to parse a type definition from source

    Returns:
        List of type names in creation order (dependencies first)
    """
    # Build dependency graph
    deps: dict[str, set[str]] = {}
    for t in types:
        if t in PRIMITIVE_MAP:
            deps[t] = set()
            continue
        defn = parse_fn(t)
        if defn:
            # Extract field type dependencies
            type_deps = set()
            for f in defn.fields:
                base = re.split(r'[*&]', f.type_name)[0].strip()
                if base and base != t and base not in PRIMITIVE_MAP:
                    type_deps.add(base)
            deps[t] = type_deps
        else:
            deps[t] = set()

    # Topological sort (Kahn's algorithm)
    in_degree = {t: 0 for t in deps}
    for t, d in deps.items():
        for dep in d:
            if dep in in_degree:
                in_degree[t] = in_degree.get(t, 0)  # ensure exists
                # dep is a dependency of t, so t depends on dep

    # Recalculate properly
    in_degree = {t: 0 for t in deps}
    for t, d in deps.items():
        for dep in d:
            if dep in deps:
                in_degree[t] += 1

    queue = [t for t, d in in_degree.items() if d == 0]
    result = []

    while queue:
        t = queue.pop(0)
        result.append(t)
        for other, d in deps.items():
            if t in d:
                in_degree[other] -= 1
                if in_degree[other] == 0:
                    queue.append(other)

    # Add any remaining types (circular deps) at the end
    for t in deps:
        if t not in result:
            result.append(t)

    return result


# ---------------------------------------------------------------------------
# Type creation
# ---------------------------------------------------------------------------

def ensure_type_exists(type_name: str, source_text: Optional[str] = None) -> bool:
    """Ensure a type exists in Ghidra, creating it if necessary.

    Handles:
    1. Primitives (uint8_t → uchar)
    2. Structs (Vector3F, Plane, etc.)
    3. Pointers (Vector3F* → just ensure Vector3F exists)

    Args:
        type_name: Type name from source (e.g. "uint8_t", "Vector3F")
        source_text: Source file content (needed for struct parsing)

    Returns:
        True if type exists or was created, False if it's unresolvable.
    """
    # Check cache first
    if type_name in _known_types:
        return True

    # Strip const/volatile
    type_name = re.sub(r'\bconst\b', '', type_name).strip()
    type_name = re.sub(r'\bvolatile\b', '', type_name).strip()

    # Check if already exists in Ghidra
    result = _run_ghidra(["type", "get", type_name], check=False)
    if result.returncode == 0:
        _known_types.add(type_name)
        return True

    # Handle pointers: just ensure the base type exists
    if type_name.endswith('*'):
        base_type = type_name[:-1].strip()
        return ensure_type_exists(base_type, source_text)

    # Handle primitives
    base = _get_base_type(type_name)
    if base:
        result = _run_ghidra(["type", "typedef", type_name, base], check=False)
        if result.returncode == 0:
            _known_types.add(type_name)
            return True
        # If typedef fails, type might already exist
        result = _run_ghidra(["type", "get", type_name], check=False)
        if result.returncode == 0:
            _known_types.add(type_name)
            return True
        return False

    # Handle structs/classes
    if source_text:
        defn = parse_struct_from_source(source_text, type_name)
        if defn:
            return _create_struct_in_ghidra(defn)

    return False


def _create_struct_in_ghidra(defn: StructDefinition) -> bool:
    """Create a struct in Ghidra.

    Args:
        defn: Struct definition to create

    Returns:
        True if successful.
    """
    # First, ensure all field types exist
    for field in defn.fields:
        if not ensure_type_exists(field.type_name):
            return False  # Can't create struct without field types

    # Build C-style struct definition string
    fields_str = "; ".join(f"{f.type_name} {f.field_name}" for f in defn.fields)
    c_def = f"struct {defn.name} {{ {fields_str}; }}"

    # Create the struct using Ghidra's type create command
    result = _run_ghidra(["type", "create", c_def], check=False)
    if result.returncode != 0:
        # Might already exist
        result = _run_ghidra(["type", "get", defn.name], check=False)
        if result.returncode == 0:
            _known_types.add(defn.name)
            return True
        return False

    # Persist
    _run_ghidra(["analyze"], check=False)

    _known_types.add(defn.name)
    return True


def sync_types_from_signature(
    signature: str,
    source_text: str,
) -> bool:
    """Sync all types referenced in a signature.

    Args:
        signature: Function signature (e.g. "float GetSignedDistanceToPlane(Vector3F*, Plane*)")
        source_text: Source file content for struct parsing

    Returns:
        True if all types were synced successfully.
    """
    types = extract_type_dependencies(signature)

    # Filter to non-primitive types that need creation
    types_to_create = {
        t for t in types
        if t not in PRIMITIVE_MAP and not _is_primitive(t)
    }

    if not types_to_create:
        return True

    # Sort by dependency order
    sorted_types = topological_sort_types(types_to_create, lambda t: parse_struct_from_source(source_text, t))

    # Create each type
    for type_name in sorted_types:
        if not ensure_type_exists(type_name, source_text):
            return False

    return True
