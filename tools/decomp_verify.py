#!/usr/bin/env python3
"""Compare decompilation reports and enforce verification-state regressions."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from hashlib import sha256
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_status import (  # noqa: E402
    is_symbol_only_diff,
    read_match_statuses,
    read_tool_artifacts,
    verification_status,
)

TOOL_ARTIFACTS = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"


def file_hash(path: Path) -> str:
    digest = sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def metadata() -> dict[str, str]:
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=True
    ).stdout.strip()
    values = {"git_head": head}
    for name, path in (
        ("build_rules_sha256", ROOT / "build" / "build.ninja"),
        ("recompiled_sha256", ROOT / "build" / "toy2.exe"),
    ):
        if path.exists():
            values[name] = file_hash(path)
    return values


def write_metadata(path: Path) -> None:
    path.write_text(json.dumps(metadata(), indent=2) + "\n", encoding="utf-8")


def classify(report: Path, address: int) -> int:
    statuses = read_match_statuses(report)
    status = statuses.get(address)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    if status is None:
        print(f"0x{address:08X}: unmatched")
        return 1
    tool = address in artifacts and is_symbol_only_diff(status)
    verified = verification_status(status, tool_artifact=tool)
    print(
        f"0x{address:08X}: {verified}; raw {status.matching * 100:.2f}%"
        + ("; reccmp effective" if status.effective else "")
    )
    if address in artifacts and not tool and verified not in ("exact", "effective"):
        print("error: the tool-artifact row has a non-symbol instruction difference", file=sys.stderr)
        return 1
    return 0


def score(report: Path, addresses: list[int]) -> int:
    statuses = read_match_statuses(report)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    failed = False
    for address in addresses:
        status = statuses.get(address)
        if status is None:
            print(f"0x{address:08X}  no comparison result")
            failed = True
            continue
        tool = address in artifacts and is_symbol_only_diff(status)
        verified = verification_status(status, tool_artifact=tool)
        if status.effective:
            detail = f"effective match, raw {status.matching * 100:.2f}%"
        elif verified == "tool":
            detail = f"tool-only artifact, raw {status.matching * 100:.2f}%"
        elif verified == "exact":
            detail = "exact match"
        else:
            detail = f"{status.matching * 100:.2f}% raw similarity"
        print(f"0x{address:08X}  {detail}")
    return 1 if failed else 0


def validate(
    baseline_path: Path,
    current_path: Path,
    targets: set[int],
    allow_target_regression: bool,
) -> int:
    baseline = read_match_statuses(baseline_path)
    current = read_match_statuses(current_path)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    problems: list[str] = []

    for address, before in baseline.items():
        after = current.get(address)
        if after is None:
            problems.append(f"0x{address:08X}: matched function disappeared")
            continue
        before_tool = address in artifacts and is_symbol_only_diff(before)
        after_tool = address in artifacts and is_symbol_only_diff(after)
        before_verified = verification_status(before, tool_artifact=before_tool)
        after_verified = verification_status(after, tool_artifact=after_tool)
        if before_verified in ("exact", "effective", "tool") and after_verified not in (
            "exact",
            "effective",
            "tool",
        ):
            problems.append(
                f"0x{address:08X}: {before_verified} regressed to {after_verified} "
                f"({before.matching * 100:.2f}% -> {after.matching * 100:.2f}%)"
            )
        elif after.matching + 1e-12 < before.matching and (
            address not in targets or not allow_target_regression
        ):
            scope = "target" if address in targets else "untouched function"
            problems.append(
                f"0x{address:08X}: {scope} score regressed "
                f"({before.matching * 100:.2f}% -> {after.matching * 100:.2f}%)"
            )

    for address in sorted(targets):
        status = current.get(address)
        if status is None:
            problems.append(f"0x{address:08X}: target is not in the current report")
            continue
        tool = address in artifacts and is_symbol_only_diff(status)
        verified = verification_status(status, tool_artifact=tool)
        print(
            f"0x{address:08X}  {verified:<11} raw {status.matching * 100:6.2f}%"
            + ("  reccmp-effective" if status.effective else "")
        )

    for address, artifact in artifacts.items():
        status = current.get(address)
        if status is None:
            problems.append(f"0x{address:08X}: tool artifact {artifact} is unmatched")
        elif status.matching != 1.0 and not status.effective and not is_symbol_only_diff(status):
            problems.append(
                f"0x{address:08X}: tool artifact {artifact} contains a code difference"
            )

    if problems:
        print("\nvalidation failed:", file=sys.stderr)
        for problem in problems:
            print(f"- {problem}", file=sys.stderr)
        return 1
    print("\nverification-state regression check passed")
    return 0


def parse_address(value: str) -> int:
    return int(value, 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    metadata_parser = subparsers.add_parser("metadata")
    metadata_parser.add_argument("output", type=Path)

    classify_parser = subparsers.add_parser("classify")
    classify_parser.add_argument("report", type=Path)
    classify_parser.add_argument("address", type=parse_address)

    score_parser = subparsers.add_parser("score")
    score_parser.add_argument("report", type=Path)
    score_parser.add_argument("addresses", nargs="+", type=parse_address)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("baseline", type=Path)
    validate_parser.add_argument("current", type=Path)
    validate_parser.add_argument("targets", nargs="+", type=parse_address)
    validate_parser.add_argument("--allow-target-regression", action="store_true")

    args = parser.parse_args()
    if args.command == "metadata":
        write_metadata(args.output)
        return 0
    if args.command == "classify":
        return classify(args.report, args.address)
    if args.command == "score":
        return score(args.report, args.addresses)
    return validate(
        args.baseline,
        args.current,
        set(args.targets),
        args.allow_target_regression,
    )


if __name__ == "__main__":
    raise SystemExit(main())
