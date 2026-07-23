#!/usr/bin/env python3
"""Ghidra sync tool for Toy2 decompilation.

Synchronizes metadata (names, comments) between Ghidra and source code
annotations. Only operates on exact (100% matched) functions to prevent
polluting the Ghidra database with speculative names.

Usage:
    tools/decomp sync diff [--target <addr>...]   Show differences
    tools/decomp sync push <address> [--apply]    Push Ghidra name to source
    tools/decomp sync pull <file> [--apply]       Pull source name to Ghidra
    tools/decomp sync reconcile <address>         Diff + smart push for one address
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Ensure tools/ is on the path for imports
sys.path.insert(0, str(Path(__file__).parent))

from ghidra_sync.ghidra_client import ensure_bridge_running, rename_function
from ghidra_sync.diff_engine import build_diff, format_diff_report
from ghidra_sync.reccmp_reader import load_match_scores


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
        print("Error: Address required. Usage: tools/decomp sync pull <address> [--apply]", file=sys.stderr)
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
        print(f"\n  Match is {entry.match_score:.0%}, not 100%. Cannot pull for safety.", file=sys.stderr)
        return 0

    if entry.ghidra and not entry.ghidra.is_unnamed:
        print(f"\n  Ghidra already has a real name ({entry.ghidra.name}). Nothing to pull.")
        return 0

    # Extract source name
    from decomp_annotations import read_source_annotations
    import re
    if not entry.source:
        print(f"\n  No source annotation found.", file=sys.stderr)
        return 1

    # Source paths from decomp_annotations are relative to src/
    src_file = entry.source.source
    source_path = Path("src") / src_file
    if not source_path.exists():
        print(f"\n  Source file not found: {src_file}", file=sys.stderr)
        return 1

    text = source_path.read_text(encoding="utf-8")
    lines = text.split('\n')
    src_name = None
    for i in range(entry.source.line, min(entry.source.line + 10, len(lines))):
        line = lines[i].strip()
        line = re.sub(r'//.*$', '', line)
        match = re.search(r'(?:[\w:]+::)?(\w+)\s*\(', line)
        if match:
            name = match.group(1)
            if name not in ('if', 'else', 'for', 'while', 'switch', 'return',
                          'void', 'int', 'char', 'float', 'double', 'bool',
                          'uint', 'int32', 'int64', 'uint32', 'uint64',
                          'unsigned', 'signed', 'const', 'static', 'extern',
                          'class', 'struct', 'enum', 'typedef', 'namespace'):
                src_name = name
                break

    if not src_name:
        print(f"\n  Could not extract a function name from source.", file=sys.stderr)
        return 1

    print(f"\n  Source name: {src_name}")
    print(f"  Ghidra name: {entry.ghidra.name if entry.ghidra else '(not found)'}")

    if args.apply:
        gh_name = entry.ghidra.name if entry.ghidra else None
        if gh_name and rename_function(gh_name, src_name):
            print(f"\n  Renamed {gh_name} → {src_name} in Ghidra.")
        else:
            print(f"\n  Error: Failed to rename in Ghidra.", file=sys.stderr)
            return 1
    else:
        print(f"\n  Dry run. Add --apply to rename in Ghidra.")

    return 0


def cmd_reconcile(args: argparse.Namespace) -> int:
    """Run reconcile for a single address: diff + pull to Ghidra if safe."""
    if not args.address:
        print("Error: Address required. Usage: tools/decomp sync reconcile <address>", file=sys.stderr)
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

        # Extract source name
        import re
        src_file = entry.source.source
        source_path = Path("src") / src_file
        if not source_path.exists():
            print(f"  Source file not found: {src_file}", file=sys.stderr)
            return 1

        text = source_path.read_text(encoding="utf-8")
        lines = text.split('\n')
        src_name = None
        for i in range(entry.source.line, min(entry.source.line + 10, len(lines))):
            line = lines[i].strip()
            line = re.sub(r'//.*$', '', line)
            match = re.search(r'(?:[\w:]+::)?(\w+)\s*\(', line)
            if match:
                name = match.group(1)
                if name not in ('if', 'else', 'for', 'while', 'switch', 'return',
                              'void', 'int', 'char', 'float', 'double', 'bool',
                              'uint', 'int32', 'int64', 'uint32', 'uint64',
                              'unsigned', 'signed', 'const', 'static', 'extern',
                              'class', 'struct', 'enum', 'typedef', 'namespace'):
                    src_name = name
                    break

        if src_name:
            print(f"  Source name: {src_name}")
            print(f"  Ghidra name: {entry.ghidra.name}")

            if args.apply:
                gh_name = entry.ghidra.name if entry.ghidra else None
                if gh_name and rename_function(gh_name, src_name):
                    print(f"\n  Renamed {gh_name} → {src_name} in Ghidra.")
                else:
                    print(f"\n  Error: Failed to rename in Ghidra.", file=sys.stderr)
                    return 1
            else:
                print(f"\n  Dry run. Add --apply to rename in Ghidra.")
        else:
            print(f"  Could not extract a function name from source.")
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
    reconcile_parser = subparsers.add_parser("reconcile", help="Diff + pull to Ghidra for one address")
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
