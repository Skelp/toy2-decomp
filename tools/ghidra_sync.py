#!/usr/bin/env python3
"""Synchronize exact Toy Story 2 reconstruction metadata into Ghidra."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).parent))

from ghidra_sync.ghidra_client import ensure_bridge_running, get_all_functions
from ghidra_sync.manifest import ROOT, SyncManifest, build_manifest
from ghidra_sync.map_check import run_check as run_map_check


MANIFEST_PATH = ROOT / "build" / "ghidra-sync-manifest.json"


def _eligible(manifest: SyncManifest) -> list[Any]:
    return [record for record in manifest.functions if record.reason is None]


def _ghidra_by_address() -> dict[str, Any]:
    return {function.address.lower(): function for function in get_all_functions()}


def _record_status(record: Any, current: dict[str, Any]) -> tuple[str, str]:
    if record.reason is not None:
        return "SKIP", record.reason
    ghidra_function = current.get(record.address.lower())
    if ghidra_function is None:
        return "CREATE", "create a function at this exact-matched entry point"
    expected_name = record.name.rsplit("::", 1)[-1]
    if ghidra_function.name != expected_name:
        return "UPDATE", f"rename {ghidra_function.name} -> {record.name}"
    return "READY", "exact PDB signature/types and source maps are eligible"


def _render(manifest: SyncManifest, *, json_output: bool = False) -> tuple[str, bool]:
    if not ensure_bridge_running():
        raise RuntimeError("could not start the Ghidra CLI bridge")
    current = _ghidra_by_address()
    rows = []
    drift = False
    for record in manifest.functions:
        status, detail = _record_status(record, current)
        drift |= status in {"CREATE", "UPDATE", "CONFLICT"}
        rows.append(
            {
                "address": record.address,
                "name": record.name,
                "status": status,
                "detail": detail,
                "match": record.matching,
                "effective": record.effective,
                "signature_available": record.signature_available,
                "source_ranges": len(record.source_ranges),
            }
        )
    if json_output:
        return json.dumps({"version": manifest.version, "functions": rows}, indent=2), drift

    lines = [
        "",
        "GHIDRA EXACT-SYNC DIFF",
        f"{'Address':<12} {'Status':<10} {'Name':<42} Detail",
        "-" * 100,
    ]
    for row in rows:
        lines.append(
            f"{row['address']:<12} {row['status']:<10} {row['name']:<42} {row['detail']}"
        )
    counts = {
        status: sum(row["status"] == status for row in rows)
        for status in ("READY", "CREATE", "UPDATE", "CONFLICT", "SKIP")
    }
    lines.append("-" * 100)
    lines.append("  ".join(f"{key}: {value}" for key, value in counts.items()))
    return "\n".join(lines), drift


def _config_value(name: str) -> str:
    result = subprocess.run(
        ["ghidra", "config", "get", name], capture_output=True, text=True, check=False
    )
    if result.returncode != 0 or not result.stdout.strip():
        raise RuntimeError(f"could not read Ghidra configuration key {name}")
    return result.stdout.strip()


def _run_headless(manifest: SyncManifest, *, verify_only: bool = False) -> int:
    manifest.write(MANIFEST_PATH)
    install_dir = _config_value("ghidra_install_dir")
    project_dir = _config_value("ghidra_project_dir")
    project = _config_value("default_project")
    program = _config_value("default_program")

    status = subprocess.run(["ghidra", "status"], capture_output=True, text=True, check=False)
    bridge_was_running = status.returncode == 0
    if bridge_was_running:
        stopped = subprocess.run(["ghidra", "stop"], check=False)
        if stopped.returncode != 0:
            raise RuntimeError("could not stop the Ghidra bridge before transactional import")

    environment = os.environ.copy()
    environment["GHIDRA_INSTALL_DIR"] = install_dir
    command = [
        sys.executable,
        str(ROOT / "tools" / "ghidra_sync" / "headless_apply.py"),
        "--manifest",
        str(MANIFEST_PATH),
        "--root",
        str(ROOT),
        "--project-dir",
        project_dir,
        "--project",
        project,
        "--program",
        program,
    ]
    if verify_only:
        command.append("--verify-only")
    try:
        result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
        return result.returncode
    finally:
        if bridge_was_running:
            subprocess.run(["ghidra", "start"], check=False)


def _build(args: argparse.Namespace, *, require_target: bool = False) -> SyncManifest:
    targets = getattr(args, "target", None)
    if require_target and not targets and not getattr(args, "all", False):
        raise RuntimeError("select --target ADDR... or --all")
    type_scope = getattr(args, "type_scope", "exact")
    manifest = build_manifest(targets, type_scope=type_scope)
    manifest.write(MANIFEST_PATH)
    return manifest


def cmd_diff(args: argparse.Namespace) -> int:
    manifest = _build(args)
    output, _ = _render(manifest, json_output=args.json)
    print(output)
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    manifest = _build(args)
    output, _ = _render(manifest, json_output=args.json)
    print(output)
    if not _eligible(manifest):
        print("No eligible exact functions.", file=sys.stderr)
        return 1
    return _run_headless(manifest, verify_only=True)


def cmd_apply(args: argparse.Namespace) -> int:
    manifest = _build(args, require_target=True)
    output, _ = _render(manifest, json_output=args.json)
    print(output)
    rejected = [record for record in manifest.functions if record.reason is not None]
    if args.target and rejected:
        print("Refusing requested non-exact target(s).", file=sys.stderr)
        return 1
    if not _eligible(manifest):
        print("No eligible exact functions.", file=sys.stderr)
        return 1
    if not args.apply:
        print("\nDry run. Add --apply to commit one transactional Ghidra update.")
        return 0
    return _run_headless(manifest)


def _add_selection(parser: argparse.ArgumentParser, *, required: bool = False) -> None:
    group = parser.add_mutually_exclusive_group(required=required)
    group.add_argument("--target", nargs="+", metavar="ADDRESS")
    group.add_argument("--all", action="store_true", help="consider every exact function")


def cmd_check(args: argparse.Namespace) -> int:
    targets = set(args.target) if args.target else None
    return run_map_check(targets)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    check_parser = subparsers.add_parser(
        "check", help="verify functions_map.txt against source and Ghidra"
    )
    _add_selection(check_parser)
    check_parser.set_defaults(handler=cmd_check)

    diff_parser = subparsers.add_parser("diff", help="compare exact source/PDB state with Ghidra")
    _add_selection(diff_parser)
    diff_parser.add_argument("--json", action="store_true")
    diff_parser.set_defaults(handler=cmd_diff)

    apply_parser = subparsers.add_parser("apply", help="apply source/PDB state; dry-run by default")
    _add_selection(apply_parser, required=True)
    apply_parser.add_argument("--apply", action="store_true", help="commit the transaction")
    apply_parser.add_argument("--json", action="store_true")
    apply_parser.add_argument(
        "--type-scope", choices=("exact", "all"), default="exact"
    )
    apply_parser.set_defaults(handler=cmd_apply)

    verify_parser = subparsers.add_parser(
        "verify", help="fail if exact signatures, datatypes, or source maps drift"
    )
    _add_selection(verify_parser)
    verify_parser.add_argument("--json", action="store_true")
    verify_parser.set_defaults(handler=cmd_verify)

    # Backwards-compatible single-address spellings.
    for name in ("pull", "reconcile"):
        alias = subparsers.add_parser(
            name, help="compatibility alias for apply --target"
        )
        alias.add_argument("address")
        alias.add_argument("--apply", action="store_true")
        alias.add_argument("--json", action="store_true")
        alias.set_defaults(handler=cmd_apply, type_scope="exact", all=False)

    args = parser.parse_args()
    if args.command in {"pull", "reconcile"}:
        args.target = [args.address]
    try:
        return args.handler(args)
    except (RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
