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
- `tools/Resources/reconstruction-blockers.tsv` supplies concise advisory blockers.
- `tools/Resources/tool_artifacts.tsv` supplies the narrow tool-only allowlist.
- `tools/decomp_lint.py` supplies the source-plausibility errors, so a function
  that matches the machine code but still states byte offsets is still offered
  as work. A 100% match is not the finish line.

New-work ranking starts with the retail dependency frontier. It favors a
function that unlocks unfinished callers or contributes to large targets.
The default queue contains source work. Provisional-score review is opt-in.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
MAP_PATH = ROOT / "tools" / "Resources" / "functions_map.txt"
SOURCE_ROOT = ROOT / "src"
REPORT_JSON = ROOT / "build" / "decomp-report-data.json"
FUNCTION_SIZES_JSON = ROOT / "build" / "decomp-function-sizes.json"
TOOL_ARTIFACTS = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"
DEFERRALS = ROOT / "tools" / "Resources" / "reconstruction-blockers.tsv"

sys.path.insert(0, str(ROOT))
from tools.decomp_annotations import read_source_annotations  # noqa: E402
from tools.decomp_status import (  # noqa: E402
    is_symbol_only_diff,
    read_match_statuses,
    read_tool_artifacts,
)
from tools.decomp_dependencies import (  # noqa: E402
    DependencyGraph,
    DependencyUnavailable,
    build_call_graph,
    strongly_connected_components,
)

# A leaf-sized function is small enough that one decompilation shows the whole
# body. The threshold is a heuristic on the gap to the next map address.
LEAF_MAX_SIZE = 200
CLUSTER_MAX_SIZE = 600
LARGE_GOAL_MIN_SIZE = 1000
BLOCKER_KINDS = (
    "semantic",
    "layout",
    "abi",
    "indirect-dispatch",
    "ownership",
    "source-form",
    "compiler-codegen",
    "tooling",
)


@dataclass(frozen=True)
class Deferral:
    blocked_by: tuple[int, ...] = ()
    reason: str = ""
    kind: str = "semantic"

    @property
    def manual(self) -> bool:
        return not self.blocked_by


@dataclass
class Candidate:
    address: int
    name: str
    size: int
    map_size: int = 0
    size_source: str = "map-gap"
    state: str = "NOT_STARTED"
    source: str = ""
    match: float | None = None
    effective: bool = False
    tool_artifact: str = ""
    siblings: int = 0
    namespace: str = ""
    lint_errors: int = 0
    lint_warnings: int = 0
    nearby_provisional_scores: tuple[float, ...] = ()
    deferred_reason: str = ""
    declared_dependencies: tuple[int, ...] = ()
    manual_blocker: bool = False
    blocker_kind: str = ""
    direct_dependencies: tuple[int, ...] = ()
    unresolved_dependencies: tuple[int, ...] = ()
    weak_dependencies: tuple[int, ...] = ()
    quality_prerequisite: bool = False
    direct_unfinished_callers: tuple[int, ...] = ()
    immediate_unlocks: int = 0
    large_goal_reach: int = 0
    indirect_calls: int = 0
    indirect_jumps: int = 0
    dependency_ready: bool = False
    dependency_component: int = -1
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


def read_original_sizes(path: Path = FUNCTION_SIZES_JSON) -> dict[int, int]:
    if not path.exists():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return {}
    sizes: dict[int, int] = {}
    rows = payload.get("data", []) if isinstance(payload, dict) else payload
    for row in rows if isinstance(rows, list) else []:
        if not isinstance(row, dict):
            continue
        try:
            address = int(str(row.get("address", "")), 16)
            size = int(row.get("original_size", row.get("size", 0)))
        except (TypeError, ValueError):
            continue
        # Source-created placeholder functions can have a one-byte Ghidra body.
        # The map gap is a safer estimate until analysis recovers the extent.
        if size > 1:
            sizes[address] = size
    return sizes


def read_deferrals(path: Path = DEFERRALS) -> dict[int, Deferral]:
    if not path.exists():
        return {}
    records: dict[int, Deferral] = {}
    with path.open(encoding="utf-8", newline="") as handle:
        for row in csv.reader(handle, delimiter="\t"):
            if not row or row[0].lstrip().startswith("#"):
                continue
            try:
                address = int(row[0].strip(), 16)
            except (IndexError, ValueError):
                continue
            if len(row) == 2:
                records[address] = Deferral(reason=row[1].strip())
                continue
            blocked_by = []
            for value in row[1].split(",") if len(row) > 1 else []:
                value = value.strip()
                if not value or value == "-":
                    continue
                try:
                    blocked_by.append(int(value, 16))
                except ValueError:
                    continue
            records[address] = Deferral(
                blocked_by=tuple(sorted(set(blocked_by))),
                reason=row[2].strip() if len(row) > 2 else "",
                kind=row[3].strip() if len(row) > 3 and row[3].strip() else "semantic",
            )
    return records


def write_deferral(
    address: int,
    reason: str,
    blocked_by: tuple[int, ...] | Path = (),
    path: Path = DEFERRALS,
    *,
    kind: str = "semantic",
) -> None:
    if isinstance(blocked_by, Path):
        path = blocked_by
        blocked_by = ()
    records = read_deferrals(path)
    records[address] = Deferral(
        blocked_by=tuple(sorted(set(blocked_by))),
        reason=reason.strip(),
        kind=kind,
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            (
                "# address",
                "blocked-by",
                "reason",
                "kind",
            )
        )
        for item_address, deferral in sorted(records.items()):
            dependencies = ",".join(
                f"0x{dependency:08X}" for dependency in deferral.blocked_by
            ) or "-"
            writer.writerow(
                (
                    f"0x{item_address:08X}",
                    dependencies,
                    deferral.reason,
                    deferral.kind,
                )
            )


def clear_deferral(address: int, path: Path = DEFERRALS) -> bool:
    records = read_deferrals(path)
    removed = records.pop(address, None) is not None
    if not removed:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            (
                "# address",
                "blocked-by",
                "reason",
                "kind",
            )
        )
        for item_address, deferral in sorted(records.items()):
            dependencies = ",".join(
                f"0x{dependency:08X}" for dependency in deferral.blocked_by
            ) or "-"
            writer.writerow(
                (
                    f"0x{item_address:08X}",
                    dependencies,
                    deferral.reason,
                    deferral.kind,
                )
            )
    return True


def print_blockers(
    records: dict[int, Deferral],
    names: dict[int, str],
    candidates: dict[int, Candidate],
    address: int | None = None,
    limit: int = 20,
) -> None:
    selected = [
        (target, blocker)
        for target, blocker in sorted(records.items())
        if address is None or target == address
    ]
    if not selected:
        print("No blocker matches the given address.")
        return
    print("TARGET      STATE     BLOCKED BY                         REASON")
    print("-" * 98)
    shown = selected[:limit] if limit else selected
    for target, blocker in shown:
        if blocker.manual:
            state = "manual"
        elif all(
            item in candidates
            and _dependency_resolved(candidates[item])
            for item in blocker.blocked_by
        ):
            state = "resolved"
        else:
            state = "open"
        dependencies = ", ".join(
            f"0x{item:08X} {names.get(item, '(not in map)')}"
            for item in blocker.blocked_by
        ) or "manual"
        metadata = blocker.kind
        print(
            f"0x{target:08X}  {state:<9} {dependencies:<34} "
            f"[{metadata}] {blocker.reason}"
        )
    if limit and len(selected) > limit:
        print(f"... {len(selected) - limit} more (use --limit 0 or --all)")


def namespace_of(name: str) -> str:
    return name.rsplit("::", 1)[0] if "::" in name else ""


def parse_address(value: str) -> int:
    return int(value, 16)


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
    tool_artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    lint_findings = read_lint_findings()
    deferrals = read_deferrals()
    original_sizes = read_original_sizes()

    reconstructed_per_namespace: dict[str, int] = {}
    for address, name in entries:
        if states.get(address, ("", ""))[0] == "FUNCTION":
            key = namespace_of(name)
            reconstructed_per_namespace[key] = reconstructed_per_namespace.get(key, 0) + 1

    candidates: list[Candidate] = []
    for index, (address, name) in enumerate(entries):
        following = entries[index + 1][0] if index + 1 < len(entries) else address
        map_size = max(following - address, 0)
        state, source = states.get(address, ("NOT_STARTED", ""))
        namespace = namespace_of(name)
        nearby_scores: list[float] = []
        for nearby_index in range(max(0, index - 4), min(len(entries), index + 5)):
            nearby_address, nearby_name = entries[nearby_index]
            if (
                not namespace
                or nearby_address == address
                or namespace_of(nearby_name) != namespace
            ):
                continue
            nearby_status = matches.get(nearby_address)
            if (
                states.get(nearby_address, ("", ""))[0] == "FUNCTION"
                and nearby_status is not None
                and nearby_status.matching != 1.0
                and not nearby_status.effective
            ):
                nearby_scores.append(nearby_status.matching)
        deferral = deferrals.get(address, Deferral())
        match_status = matches.get(address)
        size = original_sizes.get(address, map_size)
        candidates.append(
            Candidate(
                address=address,
                name=name,
                size=size,
                map_size=map_size,
                size_source=(
                    "ghidra-snapshot" if address in original_sizes else "map-gap"
                ),
                state=state,
                source=source,
                match=matches[address].matching if address in matches else None,
                effective=matches[address].effective if address in matches else False,
                tool_artifact=(
                    tool_artifacts.get(address, "")
                    if address in matches and is_symbol_only_diff(matches[address])
                    else ""
                ),
                siblings=reconstructed_per_namespace.get(namespace, 0),
                namespace=namespace,
                lint_errors=lint_findings.get(address, (0, 0))[0],
                lint_warnings=lint_findings.get(address, (0, 0))[1],
                nearby_provisional_scores=tuple(nearby_scores),
                deferred_reason=deferral.reason,
                declared_dependencies=deferral.blocked_by,
                manual_blocker=bool(deferral.reason and deferral.manual),
                blocker_kind=deferral.kind if deferral.reason else "",
            )
        )
    return candidates


def _is_resolved(candidate: Candidate) -> bool:
    return candidate.state == "FUNCTION"


def _dependency_resolved(candidate: Candidate) -> bool:
    return _is_resolved(candidate)


def _is_weak(candidate: Candidate) -> bool:
    return False


def add_dependency_evidence(
    candidates: list[Candidate], graph: DependencyGraph
) -> None:
    """Add dependency readiness and reverse unlock impact to each candidate."""

    by_address = {candidate.address: candidate for candidate in candidates}
    unfinished = {
        candidate.address
        for candidate in candidates
        if not _is_resolved(candidate) and not _dependency_resolved(candidate)
    }
    edges: dict[int, set[int]] = {}
    for candidate in candidates:
        candidate.quality_prerequisite = False
        candidate.unresolved_dependencies = ()
        candidate.direct_unfinished_callers = ()
        candidate.immediate_unlocks = 0
        candidate.large_goal_reach = 0
        candidate.dependency_ready = False
        candidate.dependency_component = -1
        targets = set(graph.callees.get(candidate.address, ()))
        targets.update(candidate.declared_dependencies)
        edges[candidate.address] = {
            target for target in targets if target in by_address and target != candidate.address
        }
        candidate.direct_dependencies = tuple(sorted(edges[candidate.address]))
        candidate.weak_dependencies = tuple(
            sorted(target for target in edges[candidate.address] if _is_weak(by_address[target]))
        )
        candidate.indirect_calls = graph.indirect_calls.get(candidate.address, 0)
        candidate.indirect_jumps = graph.indirect_jumps.get(candidate.address, 0)

    quality_prerequisites: set[int] = set()

    work = unfinished | quality_prerequisites
    unfinished_edges = {
        source: {target for target in targets if target in work}
        for source, targets in edges.items()
        if source in work
    }
    reverse_edges: dict[int, set[int]] = defaultdict(set)
    for source, targets in edges.items():
        for target in targets:
            reverse_edges[target].add(source)
    component_by_node, components = strongly_connected_components(
        work, unfinished_edges
    )
    component_edges: dict[int, set[int]] = defaultdict(set)
    reverse_component_edges: dict[int, set[int]] = defaultdict(set)
    for source, targets in unfinished_edges.items():
        source_component = component_by_node[source]
        for target in targets:
            target_component = component_by_node[target]
            if source_component == target_component:
                continue
            component_edges[source_component].add(target_component)
            reverse_component_edges[target_component].add(source_component)
    for candidate in candidates:
        candidate.direct_unfinished_callers = tuple(
            sorted(
                caller
                for caller in reverse_edges.get(candidate.address, ())
                if caller in unfinished
            )
        )
        if candidate.address not in work:
            continue
        component = component_by_node[candidate.address]
        candidate.dependency_component = component
        unresolved_components = component_edges.get(component, set())
        candidate.unresolved_dependencies = tuple(
            sorted(
                target
                for target in unfinished_edges.get(candidate.address, set())
                if component_by_node[target] != component
            )
        )
        candidate.dependency_ready = not unresolved_components

        immediate_callers: set[int] = set()
        for caller_component in reverse_component_edges.get(component, set()):
            if component_edges.get(caller_component, set()) == {component}:
                immediate_callers.update(components[caller_component])
        candidate.immediate_unlocks = len(immediate_callers)

        reached_components = {component}
        queue = deque([component])
        while queue:
            current = queue.popleft()
            for caller_component in reverse_component_edges.get(current, set()):
                if caller_component in reached_components:
                    continue
                reached_components.add(caller_component)
                queue.append(caller_component)
        candidate.large_goal_reach = sum(
            by_address[address].size > LARGE_GOAL_MIN_SIZE
            for reached in reached_components - {component}
            for address in components[reached]
        )

def dependency_frontier_for(
    target: int, candidates: list[Candidate]
) -> set[int]:
    """Return ready unfinished prerequisites for one target."""

    by_address = {candidate.address: candidate for candidate in candidates}
    if target not in by_address or (
        _is_resolved(by_address[target])
        and not by_address[target].quality_prerequisite
    ):
        return set()
    frontier: set[int] = set()
    visited_components: set[int] = set()

    def visit(address: int) -> None:
        candidate = by_address[address]
        component = candidate.dependency_component
        if component in visited_components:
            return
        visited_components.add(component)
        members = [
            item for item in candidates if item.dependency_component == component
        ]
        if all(item.dependency_ready for item in members):
            frontier.update(item.address for item in members)
            return
        for member in members:
            for dependency in member.unresolved_dependencies:
                visit(dependency)

    visit(target)
    return frontier


def print_frontier_diagnostics(candidates: list[Candidate]) -> None:
    unfinished = [item for item in candidates if not _is_resolved(item)]
    advisory_blockers = [item for item in unfinished if item.deferred_reason]
    ready = [item for item in unfinished if item.dependency_ready]

    print("Dependency frontier diagnosis")
    print(f"  unfinished functions             {len(unfinished)}")
    print(f"  dependency-ready unfinished      {len(ready)}")
    print(f"  advisory blockers                {len(advisory_blockers)}")
    kinds: dict[str, int] = defaultdict(int)
    for item in advisory_blockers:
        kinds[item.blocker_kind or "semantic"] += 1
    if kinds:
        print("  advisory blocker kinds")
        for kind, count in sorted(kinds.items(), key=lambda item: (-item[1], item[0])):
            print(f"    {kind:<24} {count}")


def score(candidate: Candidate) -> None:
    """Assign a rank and the evidence that supports it.

    A higher rank is a better next target. The weights follow the rubric
    order, so a `STUB` always outranks an unannotated function of the same
    size. A mechanically verified tool artifact sinks because it does not
    describe a code difference.
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
    elif not candidate.size:
        reasons.append("size unavailable")
    elif candidate.size <= 1000:
        reasons.append("medium body")
    elif candidate.size <= 2000:
        rank -= 25.0
        reasons.append("large body")
    elif candidate.size <= 4000:
        rank -= 75.0
        reasons.append("very large body")
    else:
        rank -= 125.0
        reasons.append("oversized body")
    if candidate.size_source == "ghidra-snapshot" and candidate.map_size != candidate.size:
        reasons.append(
            f"retail body size replaces {candidate.map_size}-byte map gap"
        )

    if candidate.siblings >= 3:
        rank += 20.0
        reasons.append(f"{candidate.siblings} reconstructed siblings")
    elif candidate.siblings >= 1:
        rank += 10.0
        reasons.append(f"{candidate.siblings} reconstructed sibling")
    else:
        reasons.append("no reconstructed sibling")

    weak_nearby_scores = [score for score in candidate.nearby_provisional_scores if score < 0.75]
    if weak_nearby_scores:
        penalty = min(sum((0.75 - score) * 60.0 for score in weak_nearby_scores), 90.0)
        rank -= penalty
        average = sum(weak_nearby_scores) / len(weak_nearby_scores)
        reasons.append(
            f"{len(weak_nearby_scores)} nearby provisional sibling(s) average "
            f"{average * 100:.1f}%"
        )

    if candidate.namespace:
        rank += 3.0
    else:
        reasons.append("no namespace in map")

    if candidate.tool_artifact:
        rank -= 200.0
        reasons.append(f"tool-only artifact: {candidate.tool_artifact}")

    if candidate.deferred_reason:
        if candidate.manual_blocker:
            blocker_state = "manual blocker"
        elif candidate.dependency_ready:
            blocker_state = "resolved prerequisite record"
        else:
            blocker_state = "declared prerequisite"
        reasons.append(f"{blocker_state}: {candidate.deferred_reason}")
        reasons.append(f"advisory blocker kind {candidate.blocker_kind or 'semantic'}")

    if candidate.dependency_component >= 0:
        if candidate.quality_prerequisite:
            reasons.append("quality prerequisite for a large caller")
        if candidate.dependency_ready:
            reasons.append("dependency frontier")
        elif candidate.manual_blocker:
            reasons.append("manual blocker is open")
        else:
            reasons.append(
                f"{len(candidate.unresolved_dependencies)} unresolved direct prerequisite(s)"
            )
        if candidate.immediate_unlocks:
            reasons.append(f"immediately unlocks {candidate.immediate_unlocks} caller(s)")
        if candidate.large_goal_reach:
            reasons.append(
                f"contributes to {candidate.large_goal_reach} large unfinished target(s)"
            )
        if candidate.weak_dependencies:
            reasons.append(f"{len(candidate.weak_dependencies)} weak implemented dependency(ies)")
        if candidate.indirect_calls or candidate.indirect_jumps:
            reasons.append(
                f"{candidate.indirect_calls} indirect call(s), "
                f"{candidate.indirect_jumps} indirect jump(s)"
            )

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
    new_work_only: bool = False,
    include_deferred: bool = False,
    allow_large: bool = False,
) -> list[Candidate]:
    chosen: list[Candidate] = []
    for candidate in candidates:
        if new_work_only:
            if candidate.state == "FUNCTION" and not candidate.quality_prerequisite:
                continue
        if namespace and not candidate.name.startswith(namespace + "::"):
            continue
        if stubs_only and candidate.state != "STUB":
            continue
        if leaves_only and not (
            candidate.state == "NOT_STARTED" and 0 < candidate.size <= LEAF_MAX_SIZE
        ):
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
        if (
            not (stubs_only or leaves_only or near_only or debt_only)
            and candidate.state == "FUNCTION"
        ):
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
    if new_work_only:
        chosen.sort(
            key=lambda item: (
                not item.dependency_ready,
                item.manual_blocker,
                -item.immediate_unlocks,
                -item.large_goal_reach,
                item.state != "STUB",
                len(item.weak_dependencies),
                item.indirect_calls + item.indirect_jumps,
                -item.rank,
                item.size,
                item.address,
            )
        )
    else:
        chosen.sort(key=lambda item: (-item.rank, item.size, item.address))
    return chosen


def print_table(chosen: list[Candidate], limit: int, show_reasons: bool) -> None:
    if not chosen:
        print("No candidate matches the given filters.")
        return

    shown = chosen[:limit] if limit else chosen
    name_width = min(max(len(item.name) for item in shown), 52)

    show_dependencies = any(item.dependency_component >= 0 for item in shown)
    dependency_header = f"  {'DEPS':>7}  {'UNLOCK':>6}" if show_dependencies else ""
    print(
        f"{'ADDRESS':<11}{'NAME':<{name_width + 2}}{'STATE':<13}{'SIZE':>6}  "
        f"{'MATCH':>7}{dependency_header}  TU"
    )
    print("-" * (11 + name_width + 2 + 13 + 6 + 9 + len(dependency_header) + 4))
    for item in shown:
        name = item.name if len(item.name) <= name_width else item.name[: name_width - 1] + "~"
        dependency_text = ""
        if show_dependencies:
            status = "ready" if item.dependency_ready else str(len(item.unresolved_dependencies))
            dependency_text = f"  {status:>7}  {item.immediate_unlocks:>6}"
        print(
            f"{item.address_text:<11}{name:<{name_width + 2}}{item.state:<13}"
            f"{item.size:>6}  {item.match_text:>7}{dependency_text}  {item.source or '-'}"
        )
        if show_reasons:
            print(f"{'':<11}rank {item.rank:.0f}: {'; '.join(item.reasons)}")

    if limit and len(chosen) > limit:
        print(f"... {len(chosen) - limit} more (raise --limit to see them)")


def print_dependency_summary(target: Candidate, names: dict[int, str], limit: int = 12) -> None:
    print(f"Dependency goal: {target.address_text} {target.name}")
    if target.state == "FUNCTION":
        state = "implemented"
    else:
        state = "ready" if target.dependency_ready else "blocked"
    print(f"State: {state}")
    if target.unresolved_dependencies:
        print("Unresolved direct prerequisites:")
        for address in target.unresolved_dependencies[:limit]:
            print(f"  0x{address:08X}  {names.get(address, '(not in map)')}")
        if len(target.unresolved_dependencies) > limit:
            print(f"  ... {len(target.unresolved_dependencies) - limit} more (use --limit 0)")
    if target.weak_dependencies:
        print("Semantically uncertain dependencies:")
        for address in target.weak_dependencies[:limit]:
            print(f"  0x{address:08X}  {names.get(address, '(not in map)')}")
        if len(target.weak_dependencies) > limit:
            print(f"  ... {len(target.weak_dependencies) - limit} more (use --limit 0)")
    if target.quality_prerequisite:
        print("Quality prerequisite: yes")
    if target.manual_blocker:
        print(f"Manual blocker: {target.deferred_reason}")
    print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rank reconstruction candidates from repository evidence.",
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
        "--debt",
        action="store_true",
        help="only functions with source-plausibility errors (see .notes/refactor-debt.md)",
    )
    parser.add_argument(
        "--quality",
        action="store_true",
        help="alias for --debt; find functions with source-quality findings",
    )
    parser.add_argument(
        "--new-work",
        action="store_true",
        help="compatibility alias for the default source-work queue",
    )
    parser.add_argument(
        "--diagnose",
        action="store_true",
        help="summarize why the dependency frontier is empty or blocked",
    )
    parser.add_argument(
        "--for",
        dest="target_address",
        type=parse_address,
        metavar="ADDRESS",
        help="show the dependency frontier for one unfinished target",
    )
    parser.add_argument(
        "--include-deferred",
        action="store_true",
        help="deprecated alias for --include-blocked",
    )
    parser.add_argument(
        "--include-blocked",
        action="store_true",
        help="compatibility option; blockers are always advisory",
    )
    parser.add_argument(
        "--record-deferral",
        type=parse_address,
        metavar="ADDRESS",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--blocked-by",
        type=parse_address,
        action="append",
        default=[],
        metavar="ADDRESS",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--clear-deferral",
        type=parse_address,
        metavar="ADDRESS",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--list-blockers",
        nargs="?",
        const="",
        metavar="ADDRESS",
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--reason", help=argparse.SUPPRESS)
    parser.add_argument("--kind", choices=BLOCKER_KINDS, default="semantic", help=argparse.SUPPRESS)
    parser.add_argument("--max-size", type=int, help="drop candidates larger than this many bytes")
    parser.add_argument(
        "--allow-large",
        action="store_true",
        help="compatibility option; the source queue includes large goals",
    )
    parser.add_argument(
        "--include-capped",
        action="store_true",
        help="deprecated alias: include verified tool-only artifacts",
    )
    parser.add_argument("--limit", type=int, help="rows to print (0 = all; default: 10)")
    parser.add_argument("--all", action="store_true", help="show all rows")
    parser.add_argument("--why", action="store_true", help="print the rank and its evidence")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of a table")
    args = parser.parse_args()
    limit_explicit = args.limit is not None or args.all
    if args.all:
        args.limit = 0
    elif args.limit is None:
        args.limit = 20 if args.list_blockers is not None else 10

    if args.diagnose and args.json:
        parser.error("--diagnose cannot be combined with --json")

    if args.record_deferral is not None:
        if not args.reason or not args.reason.strip():
            parser.error("--record-deferral needs --reason")
        known_addresses = {address for address, _ in parse_map()} if MAP_PATH.exists() else set()
        if args.record_deferral not in known_addresses:
            parser.error("the deferred target is not in functions_map.txt")
        unknown_dependencies = [
            address
            for address in args.blocked_by
            if address not in known_addresses
        ]
        if unknown_dependencies:
            parser.error(
                f"0x{unknown_dependencies[0]:08X} is not in functions_map.txt"
            )
        if args.record_deferral in args.blocked_by:
            parser.error("a target cannot depend on itself")
        write_deferral(
            args.record_deferral,
            args.reason,
            tuple(args.blocked_by),
            kind=args.kind,
        )
        dependencies = "".join(
            f", blocked by 0x{address:08X}" for address in args.blocked_by
        )
        print(
            f"Deferred 0x{args.record_deferral:08X}{dependencies}: "
            f"{args.reason.strip()}"
        )
        return 0

    if args.clear_deferral is not None:
        if clear_deferral(args.clear_deferral):
            print(f"Cleared blockers for 0x{args.clear_deferral:08X}.")
        else:
            print(f"No blockers exist for 0x{args.clear_deferral:08X}.")
        return 0

    if not MAP_PATH.exists():
        print(f"error: {MAP_PATH} not found", file=sys.stderr)
        return 2

    entries = parse_map()
    names = dict(entries)
    if args.list_blockers is not None:
        try:
            blocker_address = int(args.list_blockers, 16) if args.list_blockers else None
        except ValueError:
            parser.error("--list-blockers address must be hexadecimal")
        print_blockers(
            read_deferrals(),
            names,
            {candidate.address: candidate for candidate in build_candidates()},
            blocker_address,
            args.limit,
        )
        return 0

    if args.target_address is not None and args.namespace:
        parser.error("--for cannot be combined with a namespace filter")
    if args.target_address is not None and args.target_address not in names:
        parser.error(f"0x{args.target_address:08X} is not in functions_map.txt")

    dependency_mode = not (args.near or args.debt or args.quality)
    candidates = build_candidates()
    if dependency_mode:
        try:
            graph = build_call_graph(entries)
        except DependencyUnavailable as error:
            print(f"error: {error}", file=sys.stderr)
            return 2
        add_dependency_evidence(candidates, graph)

    if args.diagnose:
        print_frontier_diagnostics(candidates)
        return 0

    include_blocked = args.include_deferred or args.include_blocked
    chosen = select(
        candidates,
        namespace=args.namespace,
        stubs_only=args.stubs,
        leaves_only=args.leaves,
        near_only=args.near,
        max_size=args.max_size,
        exclude_capped=not args.include_capped,
        debt_only=args.debt or args.quality,
        new_work_only=dependency_mode,
        include_deferred=include_blocked,
        allow_large=True,
    )
    if args.target_address is not None:
        frontier = dependency_frontier_for(args.target_address, candidates)
        if include_blocked and not frontier:
            frontier = {args.target_address}
        chosen = [item for item in chosen if item.address in frontier]

    if args.json:
        json.dump(
            [
                {
                    "address": item.address_text,
                    "name": item.name,
                    "state": item.state,
                    "size": item.size,
                    "map_size": item.map_size,
                    "size_source": item.size_source,
                    "match": item.match,
                    "effective": item.effective,
                    "tool_artifact": item.tool_artifact,
                    "source": item.source,
                    "siblings": item.siblings,
                    "lint_errors": item.lint_errors,
                    "lint_warnings": item.lint_warnings,
                    "nearby_provisional_scores": item.nearby_provisional_scores,
                    "deferred_reason": item.deferred_reason,
                    "declared_dependencies": [
                        f"0x{address:08X}" for address in item.declared_dependencies
                    ],
                    "manual_blocker": item.manual_blocker,
                    "blocker_kind": item.blocker_kind,
                    "direct_dependencies": [
                        f"0x{address:08X}" for address in item.direct_dependencies
                    ],
                    "unresolved_dependencies": [
                        f"0x{address:08X}" for address in item.unresolved_dependencies
                    ],
                    "weak_dependencies": [
                        f"0x{address:08X}" for address in item.weak_dependencies
                    ],
                    "quality_prerequisite": item.quality_prerequisite,
                    "direct_unfinished_callers": [
                        f"0x{address:08X}" for address in item.direct_unfinished_callers
                    ],
                    "dependency_ready": item.dependency_ready,
                    "immediate_unlocks": item.immediate_unlocks,
                    "large_goal_reach": item.large_goal_reach,
                    "indirect_calls": item.indirect_calls,
                    "indirect_jumps": item.indirect_jumps,
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

    if args.target_address is not None:
        target = next(item for item in candidates if item.address == args.target_address)
        dependency_limit = (args.limit or sys.maxsize) if limit_explicit else 12
        print_dependency_summary(target, names, dependency_limit)
    print_table(chosen, args.limit, args.why)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
