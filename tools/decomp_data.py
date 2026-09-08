#!/usr/bin/env python3
"""Show type-aware reconstruction evidence for initialized globals."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "build" / "decomp-current-data-report.json"


def parse_address(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("use an address such as 0x004DF040") from error


def mismatch_count(variable: dict) -> int:
    return sum(not field.get("match", False) for field in variable.get("fields", []))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", nargs="?", type=parse_address)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--field-limit", type=int, default=40)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if args.limit < 0 or args.field_limit < 0:
        parser.error("limits must not be negative")

    payload = json.loads(args.report.read_text(encoding="utf-8"))
    variables = payload.get("variables", {}).get("variables", [])
    if args.address is not None:
        variables = [
            item
            for item in variables
            if int(item.get("original_address", 0)) == args.address
        ]
    variables.sort(
        key=lambda item: (
            -(int(item.get("size", 0)) - float(item.get("matched_bytes", 0))),
            int(item.get("original_address", 0)),
        )
    )
    if args.json:
        print(json.dumps(variables, indent=2, ensure_ascii=False))
        return 0

    imports = payload.get("imports", {})
    relocations = payload.get("relocations", {})
    print(
        "Data evidence: "
        f"{payload.get('variables', {}).get('explained_bytes', 0):,} / "
        f"{payload.get('variables', {}).get('scored_bytes', 0):,} global bytes; "
        f"{imports.get('matched_entries', 0)} / {imports.get('entry_count', 0)} imports; "
        f"{relocations.get('matched_entries', 0)} / "
        f"{relocations.get('entry_count', 0)} relocations."
    )
    print("ADDRESS    NAME                                            SIZE   MATCH  FIELDS")
    print("-" * 84)
    shown = variables if args.limit == 0 else variables[: args.limit]
    for item in shown:
        score = float(item.get("score", 0)) * 100
        print(
            f"0x{int(item.get('original_address', 0)):08X} "
            f"{str(item.get('name', 'Unknown'))[:47]:47} "
            f"{int(item.get('size', 0)):6} {score:6.1f}% "
            f"{mismatch_count(item):6}"
        )
        if args.address is not None:
            mismatches = [
                field
                for field in item.get("fields", [])
                if not field.get("match", False)
            ]
            shown_fields = (
                mismatches if args.field_limit == 0 else mismatches[: args.field_limit]
            )
            for field in shown_fields:
                field_name = field.get("name") or "raw byte"
                print(
                    f"  +0x{int(field.get('offset', 0)):04X} "
                    f"{field_name}: {field.get('original')} -> "
                    f"{field.get('recompiled')}"
                )
            if args.field_limit and len(mismatches) > args.field_limit:
                print(
                    f"  ... {len(mismatches) - args.field_limit} more fields. "
                    "Use --field-limit 0 to show all fields."
                )
    if args.limit and len(variables) > args.limit:
        print(f"... {len(variables) - args.limit} more. Use --limit 0 to show all rows.")
    if args.address is not None and not variables:
        print(f"No initialized matched global starts at 0x{args.address:08X}.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
