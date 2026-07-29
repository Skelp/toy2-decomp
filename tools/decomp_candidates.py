#!/usr/bin/env python3
"""Ranked reconstruction candidate list for Toy Story 2.

Every input is a committed repository file or a build artifact. The list needs
no Ghidra call and no reccmp run:

- `tools/Resources/functions_map.txt` supplies the authoritative address set,
  the maintainer name, and (through the next address) an approximate size.
- The `// FUNCTION:` and `// STUB:` annotations in `src/` supply the state and
  the owning translation unit.
- `build/decomp-report-data.json` supplies the current match percent when a
  comparison is available.
- `.notes/caps-registry.tsv` supplies legacy mismatch claims for the audit queue.
- `tools/Resources/tool_artifacts.tsv` supplies the narrow tool-only allowlist.
- `tools/decomp_lint.py` supplies the source-plausibility errors, so a function
  that matches the machine code but still states byte offsets is still offered
  as work. A 100% match is not the finish line.

The ranking follows the candidate rubric in `AGENTS.md`: a marked `STUB`
first, then a small unannotated function in a namespace that already has
reconstructed siblings, then a larger unannotated function, then an
implemented function that still needs verification.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
MAP_PATH = ROOT / "tools" / "Resources" / "functions_map.txt"
SOURCE_ROOT = ROOT / "src"
REPORT_JSON = ROOT / "build" / "decomp-report-data.json"
CAPS_REGISTRY = ROOT / ".notes" / "caps-registry.tsv"
TOOL_ARTIFACTS = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"
AUDIT_FREEZE = ROOT / "tools" / "Resources" / "audit-freeze.txt"
AUDIT_LEDGER = ROOT / "tools" / "Resources" / "audit-ledger.tsv"

sys.path.insert(0, str(ROOT))
from tools.decomp_annotations import read_source_annotations  # noqa: E402
from tools.decomp_status import (  # noqa: E402
    is_symbol_only_diff,
    read_match_statuses,
    read_tool_artifacts,
)

# A leaf-sized function is small enough that one decompilation shows the whole
# body. The threshold is a heuristic on the gap to the next map address.
LEAF_MAX_SIZE = 200
CLUSTER_MAX_SIZE = 600


@dataclass
class Candidate:
    address: int
    name: str
    size: int
    state: str = "NOT_STARTED"
    source: str = ""
    match: float | None = None
    effective: bool = False
    tool_artifact: str = ""
    cap: str = ""
    siblings: int = 0
    namespace: str = ""
    lint_errors: int = 0
    lint_warnings: int = 0
    audit_state: str = ""
    audit_scope: str = ""
    reasons: list[str] = field(default_factory=list)
    rank: float = 0.0

    @property
    def address_text(self) -> str:
        return f"0x{self.address:08X}"

    @property
    def match_text(self) -> str:
        if self.match is None:
            return "-"
        suffix = "*" if self.effective else ""
        return f"{self.match * 100:.1f}%{suffix}"


def parse_map(path: Path = MAP_PATH) -> list[tuple[int, str]]:
    entries: list[tuple[int, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        try:
            address = int(parts[0], 16)
        except ValueError:
            continue
        entries.append((address, parts[1].strip() if len(parts) > 1 else ""))
    entries.sort()
    return entries


def read_annotation_states() -> dict[int, tuple[str, str]]:
    states: dict[int, tuple[str, str]] = {}
    for annotation in read_source_annotations(SOURCE_ROOT):
        if annotation.kind not in ("function", "stub"):
            continue
        state = "FUNCTION" if annotation.kind == "function" else "STUB"
        states[int(annotation.address, 16)] = (state, annotation.source)
    return states


def read_match_percentages(path: Path = REPORT_JSON) -> dict[int, float]:
    return {
        address: status.matching
        for address, status in read_match_statuses(path).items()
    }


def read_caps(path: Path = CAPS_REGISTRY) -> dict[int, str]:
    if not path.exists():
        return {}
    caps: dict[int, str] = {}
    with path.open(encoding="utf-8", newline="") as handle:
        for row in csv.reader(handle, delimiter="\t"):
            if not row or row[0].lstrip().startswith("#"):
                continue
            try:
                address = int(row[0].strip(), 16)
            except ValueError:
                continue
            caps[address] = row[1].strip() if len(row) > 1 else "capped"
    return caps


def read_audit_ledger(path: Path = AUDIT_LEDGER) -> dict[int, tuple[str, str]]:
    if not path.exists():
        return {}
    records = {}
    with path.open(encoding="utf-8", newline="") as handle:
        for row in csv.reader(handle, delimiter="\t"):
            if not row or row[0].lstrip().startswith("#"):
                continue
            records[int(row[0].strip(), 16)] = (
                row[12].strip() if len(row) > 12 else "pending",
                row[13].strip() if len(row) > 13 else "-",
            )
    return records


def namespace_of(name: str) -> str:
    return name.rsplit("::", 1)[0] if "::" in name else ""


def read_lint_findings() -> dict[int, tuple[int, int]]:
    """Count blocking and advisory plausibility findings per retail address."""

    try:
        from tools import decomp_lint
    except ImportError:
        return {}

    units = decomp_lint.target_units(False, [])
    findings = decomp_lint.scan_units(units)
    findings, _ = decomp_lint.apply_baseline(findings, decomp_lint.read_baseline())
    counts: dict[int, tuple[int, int]] = {}
    for finding in findings:
        if not finding.owner_address or finding.suppressed:
            continue
        address = int(finding.owner_address, 16)
        errors, warnings = counts.get(address, (0, 0))
        if finding.severity == "error":
            errors += 1
        else:
            warnings += 1
        counts[address] = errors, warnings
    return counts


def read_lint_errors() -> dict[int, int]:
    """Compatibility view used by older tooling tests."""

    return {address: errors for address, (errors, _) in read_lint_findings().items()}


def build_candidates() -> list[Candidate]:
    entries = parse_map()
    states = read_annotation_states()
    matches = read_match_statuses(REPORT_JSON)
    caps = read_caps()
    tool_artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    lint_findings = read_lint_findings()
    audit_ledger = read_audit_ledger()

    reconstructed_per_namespace: dict[str, int] = {}
    for address, name in entries:
        if states.get(address, ("", ""))[0] == "FUNCTION":
            key = namespace_of(name)
            reconstructed_per_namespace[key] = reconstructed_per_namespace.get(key, 0) + 1

    candidates: list[Candidate] = []
    for index, (address, name) in enumerate(entries):
        following = entries[index + 1][0] if index + 1 < len(entries) else address
        state, source = states.get(address, ("NOT_STARTED", ""))
        namespace = namespace_of(name)
        candidates.append(
            Candidate(
                address=address,
                name=name,
                size=max(following - address, 0),
                state=state,
                source=source,
                match=matches[address].matching if address in matches else None,
                effective=matches[address].effective if address in matches else False,
                tool_artifact=(
                    tool_artifacts.get(address, "")
                    if address in matches and is_symbol_only_diff(matches[address])
                    else ""
                ),
                cap=caps.get(address, ""),
                siblings=reconstructed_per_namespace.get(namespace, 0),
                namespace=namespace,
                lint_errors=lint_findings.get(address, (0, 0))[0],
                lint_warnings=lint_findings.get(address, (0, 0))[1],
                audit_state=audit_ledger.get(address, ("", ""))[0],
                audit_scope=audit_ledger.get(address, ("", ""))[1],
            )
        )
    return candidates


def score(candidate: Candidate) -> None:
    """Assign a rank and the evidence that supports it.

    A higher rank is a better next target. The weights follow the rubric
    order, so a `STUB` always outranks an unannotated function of the same
    size. A legacy CAP claim raises the audit priority. A mechanically verified
    tool artifact sinks because it does not describe a code difference.
    """

    rank = 0.0
    reasons: list[str] = []

    if candidate.state == "STUB":
        if candidate.match == 1.0 or candidate.effective:
            # An empty stub body that already matches means retail is also
            # trivially empty. There is nothing left to reconstruct.
            rank -= 150.0
            reasons.append("STUB already matches, nothing to recover")
        else:
            rank += 100.0
            reasons.append("marked STUB")
    elif candidate.state == "NOT_STARTED":
        rank += 60.0
        reasons.append("unannotated")
    else:
        rank += 10.0
        if candidate.match is not None and candidate.match != 1.0 and not candidate.effective:
            reasons.append("implemented but provisional")

    # A function that matches the machine code but still states byte offsets is
    # unfinished work, whatever its percentage says. Rank it as real work.
    if candidate.lint_errors:
        rank += 45.0 + min(candidate.lint_errors, 10)
        reasons.append(f"{candidate.lint_errors} lint error(s): states offsets, not names")
    if candidate.lint_warnings:
        rank += min(candidate.lint_warnings, 10) * 0.5
        reasons.append(f"{candidate.lint_warnings} plausibility warning(s)")

    if candidate.size and candidate.size <= LEAF_MAX_SIZE:
        rank += 30.0
        reasons.append("leaf sized")
    elif candidate.size and candidate.size <= CLUSTER_MAX_SIZE:
        rank += 12.0
        reasons.append("small body")
    elif candidate.size > 2000:
        rank -= 25.0
        reasons.append("large body")

    if candidate.siblings >= 3:
        rank += 20.0
        reasons.append(f"{candidate.siblings} reconstructed siblings")
    elif candidate.siblings >= 1:
        rank += 10.0
        reasons.append(f"{candidate.siblings} reconstructed sibling")
    else:
        reasons.append("no reconstructed sibling")

    if candidate.namespace:
        rank += 3.0
    else:
        reasons.append("no namespace in map")

    if candidate.cap:
        rank += 35.0
        reasons.append(f"legacy CAP claim needs audit: {candidate.cap}")

    if candidate.audit_state == "pending" and candidate.audit_scope != "-":
        rank += 80.0
        reasons.append(f"pending freeze audit: {candidate.audit_scope}")

    if candidate.tool_artifact:
        rank -= 200.0
        reasons.append(f"tool-only artifact: {candidate.tool_artifact}")

    candidate.rank = rank
    candidate.reasons = reasons


def select(
    candidates: list[Candidate],
    *,
    namespace: str | None,
    stubs_only: bool,
    leaves_only: bool,
    near_only: bool,
    max_size: int | None,
    exclude_capped: bool,
    debt_only: bool = False,
) -> list[Candidate]:
    chosen: list[Candidate] = []
    for candidate in candidates:
        if namespace and not candidate.name.startswith(namespace + "::"):
            continue
        if stubs_only and candidate.state != "STUB":
            continue
        if leaves_only and not (candidate.state == "NOT_STARTED" and 0 < candidate.size <= LEAF_MAX_SIZE):
            continue
        if near_only and not (
            candidate.state == "FUNCTION"
            and candidate.match is not None
            and candidate.match != 1.0
            and not candidate.effective
            and not candidate.tool_artifact
        ):
            continue
        if max_size is not None and candidate.size > max_size:
            continue
        if exclude_capped and candidate.tool_artifact:
            continue
        if debt_only and not (candidate.lint_errors or candidate.lint_warnings):
            continue
        if not (stubs_only or leaves_only or near_only or debt_only) and candidate.state == "FUNCTION":
            # A fully matched function is finished work only when its source also
            # reads like source. Lint errors keep it in the list.
            if (
                candidate.match is None
                or candidate.match == 1.0
                or candidate.effective
                or candidate.tool_artifact
            ) and not (
                candidate.lint_errors or candidate.lint_warnings
            ):
                continue
        chosen.append(candidate)

    for candidate in chosen:
        score(candidate)
    chosen.sort(key=lambda item: (-item.rank, item.size, item.address))
    return chosen


def print_table(chosen: list[Candidate], limit: int, show_reasons: bool) -> None:
    if not chosen:
        print("No candidate matches the given filters.")
        return

    shown = chosen[:limit] if limit else chosen
    name_width = min(max(len(item.name) for item in shown), 52)

    print(f"{'ADDRESS':<11}{'NAME':<{name_width + 2}}{'STATE':<13}{'SIZE':>6}  {'MATCH':>7}  TU")
    print("-" * (11 + name_width + 2 + 13 + 6 + 9 + 4))
    for item in shown:
        name = item.name if len(item.name) <= name_width else item.name[: name_width - 1] + "~"
        print(
            f"{item.address_text:<11}{name:<{name_width + 2}}{item.state:<13}"
            f"{item.size:>6}  {item.match_text:>7}  {item.source or '-'}"
        )
        if show_reasons:
            print(f"{'':<11}rank {item.rank:.0f}: {'; '.join(item.reasons)}")

    if limit and len(chosen) > limit:
        print(f"... {len(chosen) - limit} more (raise --limit to see them)")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rank reconstruction candidates from committed inputs only.",
    )
    parser.add_argument("namespace", nargs="?", help="restrict to one map namespace, e.g. Nu3D")
    parser.add_argument("--stubs", action="store_true", help="only functions marked STUB in src/")
    parser.add_argument("--leaves", action="store_true", help="only small unannotated functions")
    parser.add_argument(
        "--near",
        action="store_true",
        help="only provisional implemented functions; effective matches are excluded",
    )
    parser.add_argument(
        "--audit",
        action="store_true",
        help="alias for --near; rank provisional functions for audit",
    )
    parser.add_argument(
        "--legacy-caps",
        action="store_true",
        help="with --audit, keep only old CAP claims that need a new audit",
    )
    parser.add_argument(
        "--score-below",
        type=float,
        metavar="PERCENT",
        help="keep only functions below this raw similarity percentage",
    )
    parser.add_argument(
        "--debt",
        action="store_true",
        help="only functions with source-plausibility errors (see .notes/refactor-debt.md)",
    )
    parser.add_argument(
        "--quality",
        action="store_true",
        help="alias for --debt; audit functions with source-quality findings",
    )
    parser.add_argument(
        "--new-work",
        action="store_true",
        help="show STUB and unannotated work during the audit freeze",
    )
    parser.add_argument("--max-size", type=int, help="drop candidates larger than this many bytes")
    parser.add_argument(
        "--include-capped",
        action="store_true",
        help="deprecated alias: include verified tool-only artifacts",
    )
    parser.add_argument("--limit", type=int, default=20, help="rows to print (0 = all)")
    parser.add_argument("--why", action="store_true", help="print the rank and its evidence")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of a table")
    args = parser.parse_args()

    if not MAP_PATH.exists():
        print(f"error: {MAP_PATH} not found", file=sys.stderr)
        return 2

    audit_default = (
        AUDIT_FREEZE.exists()
        and not args.new_work
        and not (args.stubs or args.leaves or args.near or args.audit or args.debt or args.quality)
    )
    freeze_queue = AUDIT_FREEZE.exists() and not args.new_work and (
        audit_default
        or (
            args.audit
            and not args.legacy_caps
            and args.score_below is None
            and not args.debt
            and not args.quality
            and not args.near
        )
    )
    chosen = select(
        build_candidates(),
        namespace=args.namespace,
        stubs_only=args.stubs,
        leaves_only=args.leaves,
        near_only=args.near or (args.audit and not (args.debt or args.quality)),
        max_size=args.max_size,
        exclude_capped=not args.include_capped,
        debt_only=args.debt or args.quality,
    )
    if args.legacy_caps:
        chosen = [item for item in chosen if item.cap]
    if args.score_below is not None:
        chosen = [
            item for item in chosen
            if item.match is not None and item.match * 100 < args.score_below
        ]
    if freeze_queue:
        chosen = [
            item for item in chosen
            if item.audit_state == "pending" and item.audit_scope != "-"
        ]

    if args.json:
        json.dump(
            [
                {
                    "address": item.address_text,
                    "name": item.name,
                    "state": item.state,
                    "size": item.size,
                    "match": item.match,
                    "effective": item.effective,
                    "tool_artifact": item.tool_artifact,
                    "source": item.source,
                    "cap": item.cap,
                    "siblings": item.siblings,
                    "lint_errors": item.lint_errors,
                    "lint_warnings": item.lint_warnings,
                    "audit_state": item.audit_state,
                    "audit_scope": item.audit_scope,
                    "rank": item.rank,
                    "reasons": item.reasons,
                }
                for item in (chosen[: args.limit] if args.limit else chosen)
            ],
            sys.stdout,
            indent=2,
        )
        print()
        return 0

    if not REPORT_JSON.exists():
        print("note: build/decomp-report-data.json is absent, so no match percent is shown.")
        print("      Run `tools/decomp report` to populate it.")

    print_table(chosen, args.limit, args.why)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
