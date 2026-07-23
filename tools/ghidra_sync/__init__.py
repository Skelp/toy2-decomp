"""Ghidra sync tool for Toy2 decompilation.

Synchronizes metadata (names, comments) between Ghidra and source code
annotations, preventing drift and keeping both sides consistent.

Usage:
    tools/decomp sync diff [--dry-run]
    tools/decomp sync push <address> [--dry-run] [--apply]
    tools/decomp sync pull <file> [--dry-run] [--apply]
    tools/decomp sync reconcile <address> [--dry-run] [--apply]
"""
