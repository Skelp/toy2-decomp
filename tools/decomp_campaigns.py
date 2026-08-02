#!/usr/bin/env python3
"""Record local campaign results and summarize reconstruction throughput."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LEDGER = ROOT / ".git" / "decomp-campaigns.jsonl"


@dataclass(frozen=True)
class AddressStats:
    attempts: int = 0
    zero_yield_attempts: int = 0
    minutes: float = 0.0
    effective_bytes: float = 0.0
    initialized_bytes: int = 0


def parse_address(value: str) -> str:
    address = int(value, 16)
    return f"0x{address:08X}"


def read_records(path: Path = DEFAULT_LEDGER) -> list[dict[str, object]]:
    if not path.exists():
        return []
    records: list[dict[str, object]] = []
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip():
            continue
        try:
            record = json.loads(raw)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid campaign record at line {line_number}: {error}") from error
        if isinstance(record, dict):
            records.append(record)
    return records


def address_stats(records: list[dict[str, object]]) -> dict[int, AddressStats]:
    totals: dict[int, dict[str, float]] = defaultdict(
        lambda: {
            "attempts": 0,
            "zero_yield_attempts": 0,
            "minutes": 0.0,
            "effective_bytes": 0.0,
            "initialized_bytes": 0.0,
        }
    )
    for record in records:
        addresses = record.get("addresses", [])
        if not isinstance(addresses, list) or not addresses:
            continue
        effective = float(record.get("effective_bytes", 0.0) or 0.0)
        initialized = int(record.get("initialized_bytes", 0) or 0)
        minutes = float(record.get("minutes", 0.0) or 0.0)
        is_zero = effective <= 0.0 and initialized <= 0
        share = float(len(addresses))
        for value in addresses:
            try:
                address = int(str(value), 16)
            except ValueError:
                continue
            item = totals[address]
            item["attempts"] += 1
            item["zero_yield_attempts"] += int(is_zero)
            item["minutes"] += minutes / share
            item["effective_bytes"] += effective / share
            item["initialized_bytes"] += initialized / share
    return {
        address: AddressStats(
            attempts=int(values["attempts"]),
            zero_yield_attempts=int(values["zero_yield_attempts"]),
            minutes=values["minutes"],
            effective_bytes=values["effective_bytes"],
            initialized_bytes=int(values["initialized_bytes"]),
        )
        for address, values in totals.items()
    }


def append_record(path: Path, record: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        json.dump(record, handle, sort_keys=True)
        handle.write("\n")


def print_summary(records: list[dict[str, object]], limit: int) -> None:
    selected = records[-limit:] if limit else records
    if not selected:
        print("No campaign records exist.")
        return
    effective = sum(float(item.get("effective_bytes", 0.0) or 0.0) for item in selected)
    initialized = sum(int(item.get("initialized_bytes", 0) or 0) for item in selected)
    minutes = sum(float(item.get("minutes", 0.0) or 0.0) for item in selected)
    sources = sum(item.get("result") == "source" for item in selected)
    no_sources = sum(item.get("result") == "no-source" for item in selected)
    print(f"Campaigns: {len(selected)} ({sources} source, {no_sources} no-source)")
    print(f"Elapsed: {minutes:.1f} minutes")
    print(f"Effective code: {effective:+.2f} bytes")
    print(f"Initialized data: {initialized:+d} bytes")
    if minutes > 0:
        print(f"Retained rate: {(effective + initialized) / minutes:.2f} bytes/minute")
    print("Recent results:")
    for item in selected:
        addresses = ",".join(str(value) for value in item.get("addresses", [])) or "-"
        retained = float(item.get("effective_bytes", 0.0) or 0.0) + int(
            item.get("initialized_bytes", 0) or 0
        )
        print(
            f"  {item.get('mode', '-'):<10} {item.get('result', '-'):<9} "
            f"{float(item.get('minutes', 0.0) or 0.0):>5.1f} min  "
            f"{retained:>8.2f} bytes  {addresses}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Record and summarize campaign throughput.")
    parser.add_argument("--file", type=Path, default=DEFAULT_LEDGER, help=argparse.SUPPRESS)
    subparsers = parser.add_subparsers(dest="command", required=True)

    record = subparsers.add_parser("record", help="record one completed campaign")
    record.add_argument("--mode", required=True, choices=("coverage", "refinement", "data", "meta"))
    record.add_argument("--result", required=True, choices=("source", "no-source", "meta-fix"))
    record.add_argument("--address", action="append", default=[], type=parse_address)
    record.add_argument("--minutes", required=True, type=float)
    record.add_argument("--effective-bytes", type=float, default=0.0)
    record.add_argument("--initialized-bytes", type=int, default=0)
    record.add_argument("--commit", default="")
    record.add_argument("--note", default="")

    summary = subparsers.add_parser("summary", help="show recent throughput")
    summary.add_argument("--limit", type=int, default=10)

    args = parser.parse_args()
    try:
        records = read_records(args.file)
    except ValueError as error:
        parser.error(str(error))

    if args.command == "record":
        if args.minutes < 0:
            parser.error("--minutes cannot be negative")
        if args.result != "meta-fix" and not args.address:
            parser.error("a source campaign needs at least one --address")
        item = {
            "timestamp": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "mode": args.mode,
            "result": args.result,
            "addresses": args.address,
            "minutes": args.minutes,
            "effective_bytes": args.effective_bytes,
            "initialized_bytes": args.initialized_bytes,
            "commit": args.commit,
            "note": args.note,
        }
        append_record(args.file, item)
        print(f"Recorded {args.mode} {args.result} campaign.")
        return 0

    print_summary(records, args.limit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
