"""Ghidra CLI wrapper for sync operations."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Optional

from .models import GhidraFunction


def _run_ghidra(args: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    """Run a ghidra CLI command and return the result."""
    cmd = ["ghidra"] + args
    result = subprocess.run(cmd, capture_output=True, text=True, check=check)
    return result


def get_function(address: str) -> Optional[GhidraFunction]:
    """Get a single function from Ghidra by address.

    Args:
        address: Address string, e.g. "0x00414720" or "00414720"

    Returns:
        GhidraFunction or None if not found.
    """
    # Normalize address format
    addr = address.strip()
    if addr.startswith("0x"):
        addr = addr[2:]
    addr = addr.zfill(8)

    result = _run_ghidra(["function", "get", addr, "--json", "--pretty"], check=False)
    if result.returncode != 0:
        return None

    try:
        data = json.loads(result.stdout)
        if not data:
            return None
        item = data[0]
        return GhidraFunction(
            address=f"0x{item['address']}",
            name=item["name"],
            calling_convention=item.get("calling_convention", "unknown"),
            comment=item.get("comment"),
            signature=item.get("signature", ""),
            size=item.get("size", 0),
        )
    except (json.JSONDecodeError, KeyError, IndexError):
        return None


def get_all_functions() -> list[GhidraFunction]:
    """Get all functions from Ghidra.

    Returns:
        List of GhidraFunction objects.
    """
    result = _run_ghidra(["function", "list", "--limit", "0", "--json", "--pretty"], check=False)
    if result.returncode != 0:
        print(f"Error listing functions: {result.stderr}", file=sys.stderr)
        return []

    try:
        data = json.loads(result.stdout)
        return [
            GhidraFunction(
                address=f"0x{item['address']}",
                name=item["name"],
                calling_convention=item.get("calling_convention", "unknown"),
                comment=item.get("comment"),
                signature=item.get("signature", ""),
                size=item.get("size", 0),
            )
            for item in data
        ]
    except (json.JSONDecodeError, KeyError):
        return []


def get_callees(address: str) -> list[dict]:
    """Get callee graph for a function.

    Args:
        address: Address string, e.g. "0x00414720"

    Returns:
        List of callee dicts with address, name, call_site, depth.
    """
    addr = address.strip()
    if addr.startswith("0x"):
        addr = addr[2:]
    addr = addr.zfill(8)

    result = _run_ghidra(["graph", "callees", addr, "--json", "--pretty"], check=False)
    if result.returncode != 0:
        return []

    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return []


def get_comment(address: str) -> Optional[str]:
    """Get the function comment from Ghidra.

    Args:
        address: Address string

    Returns:
        Comment string or None.
    """
    addr = address.strip()
    if addr.startswith("0x"):
        addr = addr[2:]
    addr = addr.zfill(8)

    result = _run_ghidra(["comment", "get", addr, "--json", "--pretty"], check=False)
    if result.returncode != 0:
        return None

    try:
        data = json.loads(result.stdout)
        if data and isinstance(data, list) and len(data) > 0:
            return data[0].get("comment") or data[0].get("text")
        return None
    except (json.JSONDecodeError, KeyError, IndexError):
        return None


def rename_function(old_name: str, new_name: str) -> bool:
    """Rename a function in Ghidra.

    Args:
        old_name: Current name (e.g. "FUN_00414720")
        new_name: New name (e.g. "EnterLevel")

    Returns:
        True if rename succeeded.
    """
    result = _run_ghidra(["symbol", "rename", old_name, new_name], check=False)
    if result.returncode != 0:
        return False

    # Persist the change to the Ghidra project database
    _run_ghidra(["analyze"], check=False)
    return True


def _ensure_type_exists(type_name: str) -> bool:
    """Ensure a type exists in Ghidra, creating it if necessary.

    Handles simple typedefs (uint8_t -> uchar) and basic types.
    For complex types (structs, classes), returns False and lets the
    caller handle it.

    Args:
        type_name: Type name from source (e.g. "uint8_t", "MyClass")

    Returns:
        True if type exists or was created, False if it's a complex type.
    """
    # Check if type already exists
    result = _run_ghidra(["type", "get", type_name], check=False)
    if result.returncode == 0:
        return True

    # Map simple C types to Ghidra base types
    simple_map = {
        "uint8_t": "uchar",
        "int8_t": "char",
        "uint16_t": "ushort",
        "int16_t": "short",
        "uint32_t": "ulong",
        "int32_t": "long",
        "uintptr_t": "pointer",
        "size_t": "ulong",
        "ptrdiff_t": "long",
    }

    if type_name in simple_map:
        base = simple_map[type_name]
        result = _run_ghidra(["type", "typedef", type_name, base], check=False)
        return result.returncode == 0

    # For complex types (structs, classes), we can't auto-create them
    # Return False so the caller can handle it
    return False


def set_function_signature(address: str, signature: str) -> bool:
    """Set the function signature in Ghidra.

    Args:
        address: Address string (e.g. "004a1bb0" or "FUN_004a1bb0")
        signature: C-style signature string (e.g. "void SetTint(uint8_t, uint8_t, uint8_t, uint8_t)")

    Returns:
        True if signature was set.
    """
    # Get the current function name to use as target
    ghidra_func = get_function(address)
    target = ghidra_func.name if ghidra_func else address

    # Try to apply the signature
    result = _run_ghidra([
        "function", "set-signature",
        "--signature", signature,
        target
    ], check=False)

    if result.returncode == 0:
        # Persist the change
        _run_ghidra(["analyze"], check=False)
        return True

    # If it failed, try to create missing types and retry
    output = (result.stdout + " " + result.stderr).lower()
    if "can't resolve datatype" in output or "cannot resolve" in output:
        # Extract the missing type from the error
        import re
        match = re.search(r"(?:can't|cannot) resolve datatype: (\S+)", result.stderr)
        if not match:
            match = re.search(r"(?:can't|cannot) resolve (?:datatype|type): (\S+)", result.stderr)
        if match:
            missing_type = match.group(1).rstrip('.,;')
            if _ensure_type_exists(missing_type):
                # Retry with the type now available
                result = _run_ghidra([
                    "function", "set-signature",
                    "--signature", signature,
                    target
                ], check=False)
                if result.returncode == 0:
                    _run_ghidra(["analyze"], check=False)
                    return True

    return False


def set_function_calling_convention(address: str, convention: str) -> bool:
    """Set the function calling convention in Ghidra.

    Args:
        address: Address string (e.g. "004a1bb0" or "FUN_004a1bb0")
        convention: Calling convention (e.g. "__stdcall")

    Returns:
        True if calling convention was set.
    """
    result = _run_ghidra([
        "function", "set-calling-convention",
        convention,
        address
    ], check=False)
    if result.returncode != 0:
        return False

    # Persist the change
    _run_ghidra(["analyze"], check=False)
    return True


def set_function_comment(address: str, comment: str) -> bool:
    """Set a function comment in Ghidra.

    Args:
        address: Address string
        comment: Comment text

    Returns:
        True if comment was set.
    """
    addr = address.strip()
    if addr.startswith("0x"):
        addr = addr[2:]
    addr = addr.zfill(8)

    result = _run_ghidra([
        "comment", "set", addr,
        "--text", comment,
        "--type", "USER"
    ], check=False)
    return result.returncode == 0


def ensure_bridge_running() -> bool:
    """Check if the Ghidra bridge is running, start it if not.

    Returns:
        True if bridge is available.
    """
    result = _run_ghidra(["status"], check=False)
    if result.returncode == 0:
        return True

    result = _run_ghidra(["start"], check=False)
    if result.returncode != 0:
        print(f"Warning: Could not start Ghidra bridge: {result.stderr}", file=sys.stderr)
        return False

    # Wait for bridge to be ready
    import time
    for _ in range(10):
        time.sleep(1)
        result = _run_ghidra(["status"], check=False)
        if result.returncode == 0:
            return True

    print("Warning: Ghidra bridge may not be ready", file=sys.stderr)
    return False
