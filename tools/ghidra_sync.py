#!/usr/bin/env python3
"""Ghidra sync tool for Toy2 decompilation.

Synchronizes metadata (names, signatures, calling conventions) between
Ghidra and source code annotations. Only operates on exact (100% matched)
functions to prevent polluting the Ghidra database with speculative names.

Usage:
    tools/decomp sync diff [--target <addr>...]   Show differences
    tools/decomp sync pull <address> [--apply]    Pull source to Ghidra
    tools/decomp sync reconcile <address>         Diff + pull for one address
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Ensure tools/ is on the path for imports
sys.path.insert(0, str(Path(__file__).parent))

from ghidra_sync.ghidra_client import (
    ensure_bridge_running,
    rename_function,
    set_function_signature,
    set_function_calling_convention,
)
from ghidra_sync.diff_engine import build_diff, format_diff_report
from ghidra_sync.reccmp_reader import load_match_scores


def _apply_pull(entry, ghidra_name: str, src_name: str,
                sig: str | None, cc: str | None, dry_run: bool) -> int:
    """Apply the pull: rename + signature + calling convention.

    Returns 0 on success, 1 on failure.
    """
    changes = []

    # 1. Rename
    if ghidra_name != src_name:
        changes.append(f"rename {ghidra_name} → {src_name}")

    # 2. Signature
    if sig and sig != entry.ghidra.signature:
        changes.append(f"set signature → {sig}")

    # 3. Calling convention
    if cc and cc != entry.ghidra.calling_convention:
        changes.append(f"set calling convention → {cc}")

    if not changes:
        print("  No changes needed.")
        return 0

    print(f"\n  Changes ({len(changes)}):")
    for c in changes:
        print(f"    - {c}")

    if dry_run:
        print(f"\n  Dry run. Add --apply to apply changes.")
        return 0

    # Apply changes
    if ghidra_name != src_name:
        if not rename_function(ghidra_name, src_name):
            print(f"\n  Error: Failed to rename {ghidra_name} → {src_name}.", file=sys.stderr)
            return 1
        print(f"\n  Renamed {ghidra_name} → {src_name}.")
        ghidra_name = src_name  # Update for subsequent operations

    if sig and sig != entry.ghidra.signature:
        if not set_function_signature(entry.address, sig):
            print(f"\n  Error: Failed to set signature.", file=sys.stderr)
            return 1
        print(f"  Set signature → {sig}.")

    if cc and cc != entry.ghidra.calling_convention:
        if not set_function_calling_convention(entry.address, cc):
            print(f"\n  Error: Failed to set calling convention.", file=sys.stderr)
            return 1
        print(f"  Set calling convention → {cc}.")

    return 0


def cmd_diff(args: argparse.Namespace) -> int:
    """Run sync diff command."""
    if not ensure_bridge_running():
        print("Error: Could not start Ghidra bridge", file=sys.stderr)
        return 1

    target = getattr(args, "target", None)
    if target:
        entries = build_diff(target_addresses=target)
    else:
        entries = build_diff()

    print(format_diff_report(entries))

    # Show actionable items
    pullable = [e for e in entries if e.status.value == "PULLABLE"]
    if pullable:
        print(f"Found {len(pullable)} address(es) pullable to Ghidra (100% match).")
        print("Run: tools/decomp sync pull <address> --apply")

    return 0


def cmd_pull(args: argparse.Namespace) -> int:
    """Run sync pull command for a single address."""
    if not args.address:
        print("Error: Address required. Usage: tools/decomp sync pull <address> [--apply]",
              file=sys.stderr)
        return 1

    if not ensure_bridge_running():
        print("Error: Could not start Ghidra bridge", file=sys.stderr)
        return 1

    addresses = [args.address]
    entries = build_diff(target_addresses=addresses)

    if not entries:
        print(f"No data found for {args.address}", file=sys.stderr)
        return 1

    entry = entries[0]
    print(f"\nTarget: {entry.address}")
    print(f"  Ghidra: {entry.ghidra.name if entry.ghidra else '(not found)'}")
    print(f"  Source: {entry.source.source}:{entry.source.line if entry.source else '(not found)'}")
    print(f"  Match:  {entry.match_score:.0%}")

    if entry.match_score < 1.0:
        print(f"\n  Match is {entry.match_score:.0%}, not 100%. Cannot pull for safety.",
              file=sys.stderr)
        return 0

    if entry.ghidra and not entry.ghidra.is_unnamed:
        # Check if signature/calling convention need updating
        sig_diff = entry.source.signature and entry.ghidra.signature != entry.source.signature
        cc_diff = entry.source.calling_convention and entry.ghidra.calling_convention != entry.source.calling_convention
        if not sig_diff and not cc_diff:
            print(f"\n  Ghidra already has a real name ({entry.ghidra.name}). Nothing to pull.")
            return 0
        print(f"\n  Ghidra has name {entry.ghidra.name} but signature/calling convention differ.")
        print(f"  Source signature: {entry.source.signature}")
        print(f"  Source calling convention: {entry.source.calling_convention}")
    else:
        print(f"\n  Source name: {entry.source.name or '(unknown)'}")
        print(f"  Source signature: {entry.source.signature or '(none)'}")
        print(f"  Source calling convention: {entry.source.calling_convention or '(none)'}")

    if not entry.source:
        print(f"\n  No source annotation found.", file=sys.stderr)
        return 1

    print(f"\n  Source name: {entry.source.name or '(unknown)'}")
    print(f"  Source signature: {entry.source.signature or '(none)'}")
    print(f"  Source calling convention: {entry.source.calling_convention or '(none)'}")
    print(f"  Ghidra name: {entry.ghidra.name if entry.ghidra else '(not found)'}")
    print(f"  Ghidra signature: {entry.ghidra.signature or '(unknown)'}")

    if args.apply:
        return _apply_pull(
            entry,
            entry.ghidra.name if entry.ghidra else "",
            entry.source.name or "",
            entry.source.signature,
            entry.source.calling_convention,
            dry_run=False,
        )
    else:
        print(f"\n  Dry run. Add --apply to apply changes.")

    return 0

    return 0


def cmd_reconcile(args: argparse.Namespace) -> int:
    """Run reconcile for a single address: diff + pull to Ghidra if safe."""
    if not args.address:
        print("Error: Address required. Usage: tools/decomp sync reconcile <address>",
              file=sys.stderr)
        return 1

    if not ensure_bridge_running():
        print("Error: Could not start Ghidra bridge", file=sys.stderr)
        return 1

    addresses = [args.address]
    entries = build_diff(target_addresses=addresses)

    if not entries:
        print(f"No data found for {args.address}", file=sys.stderr)
        return 1

    entry = entries[0]
    print(f"\nReconcile: {entry.address}")
    print(f"  Status:    {entry.status.value}")
    print(f"  Match:     {entry.match_score:.0%}")
    print(f"  Ghidra:    {entry.ghidra.name if entry.ghidra else '(not found)'}")
    print(f"  Source:    {entry.source.source}:{entry.source.line if entry.source else '(not found)'}")
    print()

    if entry.match_score == 1.0 and entry.ghidra and entry.ghidra.is_unnamed and entry.source:
        print(f"  This address has a 100% match but Ghidra still has FUN_*.")
        print(f"  Safe to pull the source name into Ghidra.")

        print(f"\n  Source name: {entry.source.name or '(unknown)'}")
        print(f"  Source signature: {entry.source.signature or '(none)'}")
        print(f"  Source calling convention: {entry.source.calling_convention or '(none)'}")
        print(f"  Ghidra name: {entry.ghidra.name}")
        print(f"  Ghidra signature: {entry.ghidra.signature or '(unknown)'}")

        if args.apply:
            return _apply_pull(
                entry,
                entry.ghidra.name if entry.ghidra else "",
                entry.source.name or "",
                entry.source.signature,
                entry.source.calling_convention,
                dry_run=False,
            )
        else:
            print(f"\n  Dry run. Add --apply to apply changes.")
    else:
        print(f"  Not safe to pull (match < 100% or Ghidra already has a real name).")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Sync Ghidra metadata with source annotations."
    )
    subparsers = parser.add_subparsers(dest="command", help="Sync command")

    # diff subcommand
    diff_parser = subparsers.add_parser("diff", help="Show differences between Ghidra and source")
    diff_parser.add_argument(
        "--target",
        nargs="+",
        metavar="ADDRESS",
        help="Only check these addresses (hex, e.g. 0x00414720)",
    )

    # pull subcommand
    pull_parser = subparsers.add_parser("pull", help="Pull source name to Ghidra for one address")
    pull_parser.add_argument(
        "address",
        help="Address to pull (hex, e.g. 0x00414720)",
    )
    pull_parser.add_argument(
        "--apply",
        action="store_true",
        help="Actually rename in Ghidra (default: dry run)",
    )

    # reconcile subcommand
    reconcile_parser = subparsers.add_parser(
        "reconcile", help="Diff + pull to Ghidra for one address"
    )
    reconcile_parser.add_argument(
        "address",
        help="Address to reconcile (hex, e.g. 0x00414720)",
    )
    reconcile_parser.add_argument(
        "--apply",
        action="store_true",
        help="Actually rename in Ghidra (default: dry run)",
    )

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    commands = {
        "diff": cmd_diff,
        "pull": cmd_pull,
        "reconcile": cmd_reconcile,
    }

    handler = commands.get(args.command)
    if handler:
        return handler(args)
    else:
        print(f"Unknown command: {args.command}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
