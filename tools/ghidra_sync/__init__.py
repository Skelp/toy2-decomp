"""Ghidra sync package for Toy2 decompilation.

Synchronizes metadata (names, signatures, calling conventions) between
Ghidra and source code annotations. Only operates on exact (100% matched)
functions to prevent polluting the Ghidra database with speculative names.
"""

from .ghidra_client import (
    ensure_bridge_running,
    get_function,
    rename_function,
    set_function_signature,
    set_function_calling_convention,
)

__all__ = [
    "ensure_bridge_running",
    "get_function",
    "rename_function",
    "set_function_signature",
    "set_function_calling_convention",
]
