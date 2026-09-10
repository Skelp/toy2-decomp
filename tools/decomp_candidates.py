#!/usr/bin/env python3
"""Ranked reconstruction candidate list for Toy Story 2.

Every input is a committed repository file or a build artifact. The list needs
no Ghidra call and no reccmp run:

- `tools/Resources/functions_map.txt` supplies the authoritative address set,
  the maintainer name, and (through the next address) an approximate size.
- The `// FUNCTION:` and `// STUB:` annotations in `src/` supply the state and
  the owning translation unit.
- `build/decomp-current-report.json` supplies the current match percent when a
  comparison is available.
- `tools/Resources/reconstruction-blockers.tsv` supplies concise advisory blockers.
- `tools/Resources/tool_artifacts.tsv` supplies the narrow tool-only allowlist.
- `tools/decomp_lint.py` supplies the source-plausibility errors, so a function
  that matches the machine code but still states byte offsets is still offered
  as work. A 100% match is not the finish line.

New-work ranking starts with the retail dependency frontier. It favors a
function that unlocks unfinished callers or contributes to large targets.
The default queue contains coverage work. Refinement work is opt-in.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import re
import sys
from collections import defaultdict, deque
from dataclasses import asdict, dataclass, field, fields
from datetime import datetime, timezone
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
MAP_PATH = ROOT / "tools" / "Resources" / "functions_map.txt"
SOURCE_ROOT = ROOT / "src"
REPORT_JSON = ROOT / "build" / "decomp-current-report.json"
FUNCTION_SIZES_JSON = ROOT / "build" / "decomp-function-sizes.json"
TOOL_ARTIFACTS = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"
DEFERRALS = ROOT / "tools" / "Resources" / "reconstruction-blockers.tsv"
LEDGER_PATH = ROOT / "tools" / "Resources" / "campaign-ledger.jsonl"
DIFF_ROOT = ROOT / "build" / "decomp-diffs"
CANDIDATE_CACHE_ROOT = ROOT / "build" / "decomp-cache" / "candidates"
CANDIDATE_CACHE_VERSION = 1
PREDICTION_HANDOFF_SCHEMA_VERSION = 2
PREDICTION_VERSION = "cohort-v1"

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
from tools.decomp_campaigns import address_stats, read_records  # noqa: E402
from tools.decomp_mismatch import (  # noqa: E402
    classify_text_diff,
    empty_taxonomy,
    legacy_candidate_classifications,
)

# A leaf-sized function is small enough that one decompilation shows the whole
# body. The threshold is a heuristic on the gap to the next map address.
LEAF_MAX_SIZE = 200
CLUSTER_MAX_SIZE = 600
LARGE_GOAL_MIN_SIZE = 1000
INDEPENDENT_REFINEMENT_MIN_BYTES = 100
MAP_DEFECT_SCORE_CEILING = 0.6
ZERO_YIELD_PENALTY_FLOOR = 0.05
LANES = ("closure", "production", "research")
PRODUCTION_MATCH_MIN = 0.50
PRODUCTION_MATCH_MAX = 0.90
PRODUCTION_SIZE_MIN = 300
PRODUCTION_SIZE_MAX = 3000
PRODUCTION_UNRESOLVED_MIN = 100
PRODUCTION_WEAK_DEPENDENCY_MAX = 2
CLOSURE_MATCH_MIN = 0.99
COHORT_MIN_SAMPLES = 8
LOWER_QUANTILE = 0.10
UNDER_YIELD_RATIO = 0.25
UNDER_YIELD_PENALTY_FLOOR = 0.05
NO_SOURCE_PENALTY = 0.10
HISTORY_PENALTY_FLOOR = 0.01
CIRCUIT_BREAKER_FAILURES = 3
CIRCUIT_BREAKER_WINDOW = 5
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


@dataclass(frozen=True)
class CampaignAttempt:
    """One measured target result from the tracked campaign ledger."""

    address: int
    campaign_id: str
    mode: str
    result: str
    timestamp: str
    minutes: float
    retained_bytes: float
    expected_retained_bytes: float | None
    lane: str = ""
    subsystem: str = ""
    attempt_bucket: str = ""
    size_bucket: str = ""
    score_bucket: str = ""
    ruled_out_models: tuple[str, ...] = ()
    calibration_eligible: bool = True

    @property
    def no_source(self) -> bool:
        return self.result == "no-source" or self.retained_bytes <= 0.0

    @property
    def realization_ratio(self) -> float | None:
        if not self.expected_retained_bytes or self.expected_retained_bytes <= 0.0:
            return None
        return self.retained_bytes / self.expected_retained_bytes

    @property
    def under_yield(self) -> bool:
        ratio = self.realization_ratio
        return self.result == "source" and ratio is not None and ratio < UNDER_YIELD_RATIO


@dataclass(frozen=True)
class CohortEstimate:
    """A sparse-safe estimate from comparable campaign attempts."""

    key: str
    sample_size: int
    success_probability: float
    lower_retained_bytes: float
    median_retained_bytes: float
    median_minutes: float


@dataclass(frozen=True)
class CalibrationPoint:
    """One walk-forward check of a cohort lower bound."""

    campaign_id: str
    address: int
    lane: str
    cohort_key: str
    sample_size: int
    predicted_lower_bytes: float
    actual_retained_bytes: float
    covered: bool


@dataclass(frozen=True)
class MismatchArtifact:
    """A saved comparison with classified mismatch windows."""

    path: str
    windows: int
    classifications: tuple[str, ...]
    matching: float | None = None
    provenance_bound: bool = False


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
    actionable_mismatch: bool = False
    mismatch_artifact: str = ""
    mismatch_windows: int = 0
    mismatch_classifications: tuple[str, ...] = ()
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
    prior_attempts: int = 0
    prior_zero_yield_attempts: int = 0
    penalty_attempts: int | None = None
    prior_minutes: float = 0.0
    prior_effective_bytes: float = 0.0
    prior_initialized_bytes: int = 0
    expected_retained_bytes: float | None = 0.0
    expected_minutes: float | None = 0.0
    expected_bytes_per_minute: float | None = 0.0
    lane: str = ""
    lane_eligible: bool = False
    closure_eligible: bool = False
    production_eligible: bool = False
    research_eligible: bool = False
    lane_reason: str = ""
    success_probability: float | None = None
    cohort_key: str = ""
    cohort_sample_size: int = 0
    lower_retained_bytes: float | None = None
    median_retained_bytes: float | None = None
    median_minutes: float | None = None
    no_source_attempts: int = 0
    under_yield_attempts: int = 0
    history_penalty: float = 1.0
    latest_campaign_id: str = ""
    retry_eligible: bool = False
    retry_evidence_id: str = ""
    fresh_evidence: bool = False
    circuit_breaker_open: bool = False
    research_value: float = 0.0

    @property
    def source_debt(self) -> bool:
        return bool(self.lint_errors or self.lint_warnings)

    @property
    def has_actionable_mismatch(self) -> bool:
        return bool(
            self.actionable_mismatch
            and self.mismatch_artifact
            and self.mismatch_windows > 0
            and self.mismatch_classifications
        )

    @property
    def binary_terminal(self) -> bool:
        return self.match == 1.0 or self.effective

    @property
    def terminal(self) -> bool:
        return (
            self.state == "FUNCTION"
            and self.binary_terminal
            and not self.source_debt
        )

    @property
    def unresolved_bytes(self) -> float:
        if self.state != "FUNCTION" or self.match is None:
            return float(self.size)
        effective_match = 1.0 if self.binary_terminal else self.match
        return self.size * max(0.0, 1.0 - effective_match)

    @property
    def score_ceiling(self) -> float | None:
        if (
            self.state not in ("STUB", "NOT_STARTED")
            or self.size_source != "ghidra-snapshot"
            or self.map_size <= 0
        ):
            return None
        return min(self.size / self.map_size, 1.0)

    @property
    def map_defect(self) -> bool:
        ceiling = self.score_ceiling
        return ceiling is not None and ceiling < MAP_DEFECT_SCORE_CEILING

    @property
    def work_target(self) -> bool:
        return not self.map_defect

    @property
    def active_penalty_attempts(self) -> int:
        if self.penalty_attempts is None:
            return self.prior_zero_yield_attempts
        return self.penalty_attempts

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
    # Candidate debt belongs to one annotated function. Repository-wide
    # findings stay in the full lint validation and do not affect this queue.
    findings = decomp_lint.scan_units(units, cross_file=False)
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


def _mismatch_change_kind(line: str) -> str:
    stripped = line.lstrip()
    if stripped.startswith(("---", "+++")):
        return ""
    if stripped.startswith("-") or " : -" in line:
        return "remove"
    if stripped.startswith("+") or " : +" in line:
        return "add"
    return ""


def _classify_mismatch_window(lines: list[str]) -> tuple[str, ...]:
    return legacy_candidate_classifications(lines)


def read_actionable_mismatches(
    path: Path = DIFF_ROOT,
    *,
    root: Path = ROOT,
    require_provenance: bool = False,
    comparison_identity: dict[str, object] | None = None,
    report_receipt: dict[str, object] | None = None,
) -> dict[int, MismatchArtifact]:
    """Read saved diffs that contain at least one two-sided mismatch window."""

    artifacts: dict[int, MismatchArtifact] = {}
    if not path.is_dir():
        return artifacts
    for artifact_path in sorted(path.glob("0x*.txt")):
        try:
            address = int(artifact_path.stem, 16)
            if artifact_path.stat().st_size > 10_000_000:
                continue
            lines = artifact_path.read_text(
                encoding="utf-8", errors="ignore"
            ).splitlines()
        except (OSError, ValueError):
            continue
        windows: list[list[str]] = []
        current: list[str] = []
        for line in lines:
            if line.startswith("@@"):
                if current:
                    windows.append(current)
                current = [line]
            elif current:
                current.append(line)
        if current:
            windows.append(current)
        actionable = [
            window
            for window in windows
            if {"add", "remove"}.issubset(
                {_mismatch_change_kind(line) for line in window}
            )
        ]
        if not actionable:
            continue
        classes = tuple(
            sorted(
                {
                    kind
                    for window in actionable
                    for kind in _classify_mismatch_window(window)
                }
            )
        )
        score_matches = re.findall(
            r"(\d+(?:\.\d+)?)%\s+(?:similar|(?:effective\s+)?match)",
            "\n".join(lines),
            re.IGNORECASE,
        )
        matching = float(score_matches[-1]) / 100.0 if score_matches else None
        provenance_bound = False
        if require_provenance:
            try:
                from tools.decomp_provenance import validate_diff

                validate_diff(
                    artifact_path,
                    address,
                    current_match=matching,
                    root=root,
                    current=comparison_identity,
                    report_receipt=report_receipt,
                )
                provenance_bound = True
            except (OSError, ValueError):
                continue
        try:
            saved_path = artifact_path.relative_to(ROOT).as_posix()
        except ValueError:
            saved_path = artifact_path.as_posix()
        artifacts[address] = MismatchArtifact(
            path=saved_path,
            windows=len(actionable),
            classifications=classes,
            matching=matching,
            provenance_bound=provenance_bound,
        )
    return artifacts


def mismatch_artifact_is_current(
    artifact: MismatchArtifact, current_match: float
) -> bool:
    """Return true when a saved diff describes the current comparison score."""

    return artifact.provenance_bound and artifact.matching is not None and math.isclose(
        artifact.matching,
        current_match,
        rel_tol=0.0,
        abs_tol=0.000051,
    )


def mismatch_taxonomy(candidate: Candidate, *, root: Path = ROOT) -> dict[str, object]:
    """Return additive route data for a candidate saved diff."""

    if not candidate.has_actionable_mismatch:
        return empty_taxonomy()
    path = Path(candidate.mismatch_artifact)
    if not path.is_absolute():
        path = root / path
    try:
        if path.stat().st_size > 10_000_000:
            return empty_taxonomy()
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except OSError:
        return empty_taxonomy()
    return classify_text_diff(lines)


def build_candidates() -> list[Candidate]:
    from tools.decomp_provenance import current_identity, validate_report

    comparison_identity = current_identity(ROOT)
    report_receipt = validate_report(
        REPORT_JSON, root=ROOT, current=comparison_identity
    )
    entries = parse_map()
    states = read_annotation_states()
    matches = read_match_statuses(REPORT_JSON)
    tool_artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    lint_findings = read_lint_findings()
    deferrals = read_deferrals()
    original_sizes = read_original_sizes()
    attempts = address_stats(read_records())
    mismatch_artifacts = read_actionable_mismatches(
        root=ROOT,
        require_provenance=True,
        comparison_identity=comparison_identity,
        report_receipt=report_receipt,
    )

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
        history = attempts.get(address)
        mismatch_artifact = mismatch_artifacts.get(address)
        if (
            mismatch_artifact is not None
            and match_status is not None
            and not mismatch_artifact_is_current(
                mismatch_artifact, match_status.matching
            )
        ):
            mismatch_artifact = None
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
                actionable_mismatch=mismatch_artifact is not None,
                mismatch_artifact=(
                    mismatch_artifact.path if mismatch_artifact is not None else ""
                ),
                mismatch_windows=(
                    mismatch_artifact.windows if mismatch_artifact is not None else 0
                ),
                mismatch_classifications=(
                    mismatch_artifact.classifications
                    if mismatch_artifact is not None
                    else ()
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
                prior_attempts=history.attempts if history else 0,
                prior_zero_yield_attempts=(history.zero_yield_attempts if history else 0),
                penalty_attempts=history.penalty_attempts if history else 0,
                prior_minutes=history.minutes if history else 0.0,
                prior_effective_bytes=history.effective_bytes if history else 0.0,
                prior_initialized_bytes=history.initialized_bytes if history else 0,
            )
        )
    return candidates


_CACHED_TUPLE_FIELDS = {
    "mismatch_classifications",
    "nearby_provisional_scores",
    "declared_dependencies",
    "direct_dependencies",
    "unresolved_dependencies",
    "weak_dependencies",
    "direct_unfinished_callers",
}


def _file_digest(path: Path) -> str:
    if not path.is_file():
        return "missing"
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
    except OSError:
        return "unreadable"
    return digest.hexdigest()


def _semantic_report_digest(path: Path) -> str:
    """Hash comparison rows without volatile report serialization metadata."""

    statuses = read_match_statuses(path)
    payload = {
        f"0x{address:08X}": {
            "matching": status.matching,
            "effective": status.effective,
            "name": status.name,
            "diff": status.diff,
        }
        for address, status in sorted(statuses.items())
    }
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _tree_digest(path: Path, suffixes: tuple[str, ...] | None = None) -> str:
    digest = hashlib.sha256()
    if not path.is_dir():
        return "missing"
    try:
        paths = sorted(item for item in path.rglob("*") if item.is_file())
        for item in paths:
            if suffixes is not None and item.suffix not in suffixes:
                continue
            relative = item.relative_to(path).as_posix().encode("utf-8")
            digest.update(relative)
            digest.update(b"\0")
            digest.update(_file_digest(item).encode("ascii"))
            digest.update(b"\0")
    except OSError:
        return "unreadable"
    return digest.hexdigest()


def _head_identity() -> str:
    git_path = ROOT / ".git"
    if git_path.is_file():
        try:
            value = git_path.read_text(encoding="utf-8").strip()
        except OSError:
            return "unreadable"
        if not value.startswith("gitdir:"):
            return "invalid"
        git_path = (ROOT / value.removeprefix("gitdir:").strip()).resolve()
    head_path = git_path / "HEAD"
    try:
        head = head_path.read_text(encoding="utf-8").strip()
    except OSError:
        return "unreadable"
    if not head.startswith("ref: "):
        return head
    reference = head.removeprefix("ref: ")
    reference_path = git_path / reference
    try:
        return reference_path.read_text(encoding="utf-8").strip()
    except OSError:
        pass
    try:
        for line in (git_path / "packed-refs").read_text(
            encoding="utf-8"
        ).splitlines():
            if line.endswith(f" {reference}"):
                return line.split(" ", 1)[0]
    except OSError:
        return "unreadable"
    return "missing"


def candidate_cache_fingerprint(*, dependency_mode: bool) -> dict[str, object]:
    """Return all identities that can change a cached candidate record."""

    tool_paths = (
        Path(__file__),
        ROOT / "tools" / "decomp_annotations.py",
        ROOT / "tools" / "decomp_status.py",
        ROOT / "tools" / "decomp_dependencies.py",
        ROOT / "tools" / "decomp_binary.py",
        ROOT / "tools" / "decomp_campaigns.py",
        ROOT / "tools" / "decomp_diff.py",
        ROOT / "tools" / "decomp_lint.py",
        ROOT / "tools" / "decomp_provenance.py",
    )
    support_paths = (
        FUNCTION_SIZES_JSON,
        TOOL_ARTIFACTS,
        DEFERRALS,
        ROOT / ".notes" / "lint-baseline.tsv",
        ROOT / "original" / "toy2.exe",
    )
    return {
        "cache_version": CANDIDATE_CACHE_VERSION,
        "dependency_mode": dependency_mode,
        "head": _head_identity(),
        "report_sha256": _file_digest(REPORT_JSON),
        "report_semantic_sha256": _semantic_report_digest(REPORT_JSON),
        "map_sha256": _file_digest(MAP_PATH),
        "ledger_sha256": _file_digest(LEDGER_PATH),
        "source_sha256": _tree_digest(
            SOURCE_ROOT,
            (
                ".c",
                ".cc",
                ".cpp",
                ".cxx",
                ".def",
                ".h",
                ".hh",
                ".hpp",
                ".hxx",
                ".inc",
                ".inl",
                ".ipp",
                ".rc",
                ".s",
                ".asm",
            ),
        ),
        "mismatch_sha256": _tree_digest(DIFF_ROOT, (".txt", ".json")),
        "report_provenance_sha256": _file_digest(
            REPORT_JSON.with_name(f"{REPORT_JSON.name}.provenance.json")
        ),
        "comparison_identity": _comparison_identity(),
        "tool_hashes": {
            path.relative_to(ROOT).as_posix(): _file_digest(path)
            for path in tool_paths
        },
        "support_hashes": {
            path.relative_to(ROOT).as_posix(): _file_digest(path)
            for path in support_paths
        },
    }


def candidate_selection_fingerprint(*, dependency_mode: bool) -> dict[str, object]:
    """Return selector inputs that stay stable during post-selection `bc` work."""

    fingerprint = dict(candidate_cache_fingerprint(dependency_mode=dependency_mode))
    fingerprint.pop("report_sha256", None)
    fingerprint.pop("mismatch_sha256", None)
    fingerprint.pop("report_provenance_sha256", None)
    return fingerprint


def _comparison_identity() -> dict[str, object]:
    try:
        from tools.decomp_provenance import current_identity

        return current_identity(ROOT)
    except (OSError, ValueError) as error:
        return {"error": str(error)}


def _cached_mismatches_are_current(candidates: list[Candidate]) -> bool:
    if not any(candidate.actionable_mismatch for candidate in candidates):
        return True
    try:
        from tools.decomp_provenance import (
            current_identity,
            validate_diff,
            validate_report,
        )

        comparison_identity = current_identity(ROOT)
        report_receipt = validate_report(
            REPORT_JSON, root=ROOT, current=comparison_identity
        )

        for candidate in candidates:
            if not candidate.actionable_mismatch:
                continue
            path = Path(candidate.mismatch_artifact)
            if not path.is_absolute():
                path = ROOT / path
            validate_diff(
                path,
                candidate.address,
                current_match=candidate.match,
                root=ROOT,
                current=comparison_identity,
                report_receipt=report_receipt,
            )
    except (OSError, ValueError):
        return False
    return True


def candidate_cache_key(fingerprint: dict[str, object]) -> str:
    encoded = json.dumps(
        fingerprint, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _safe_cache_fingerprint(value: object) -> bool:
    if isinstance(value, dict):
        return all(_safe_cache_fingerprint(item) for item in value.values())
    if isinstance(value, list):
        return all(_safe_cache_fingerprint(item) for item in value)
    return value not in ("unreadable", "invalid")


def _candidate_from_cache(value: object) -> Candidate | None:
    if not isinstance(value, dict):
        return None
    names = {item.name for item in fields(Candidate)}
    if set(value) != names:
        return None
    data = dict(value)
    if (
        not isinstance(data.get("address"), int)
        or isinstance(data.get("address"), bool)
        or not 0 <= int(data["address"]) <= 0xFFFFFFFF
        or not isinstance(data.get("name"), str)
        or not data["name"]
        or not isinstance(data.get("size"), int)
        or int(data["size"]) < 0
        or not isinstance(data.get("reasons"), list)
        or not all(isinstance(reason, str) for reason in data["reasons"])
    ):
        return None
    for field_name in _CACHED_TUPLE_FIELDS:
        value_list = data.get(field_name)
        if not isinstance(value_list, (list, tuple)):
            return None
        data[field_name] = tuple(value_list)
    for field_name, field_value in data.items():
        if isinstance(field_value, float) and not math.isfinite(field_value):
            return None
    if data.get("lane") or data.get("reasons") or data.get("rank") != 0.0:
        return None
    try:
        candidate = Candidate(**data)
    except (TypeError, ValueError):
        return None
    if candidate.actionable_mismatch != candidate.has_actionable_mismatch:
        return None
    return candidate


def _read_candidate_cache(
    path: Path, fingerprint: dict[str, object], cache_key: str
) -> list[Candidate] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeError):
        return None
    if (
        not isinstance(value, dict)
        or value.get("cache_version") != CANDIDATE_CACHE_VERSION
        or value.get("key") != cache_key
        or value.get("fingerprint") != fingerprint
        or not isinstance(value.get("candidates"), list)
    ):
        return None
    candidates = [_candidate_from_cache(item) for item in value["candidates"]]
    if any(item is None for item in candidates):
        return None
    valid = [item for item in candidates if item is not None]
    addresses = [item.address for item in valid]
    if len(addresses) != len(set(addresses)):
        return None
    return valid


def _write_candidate_cache(
    path: Path,
    fingerprint: dict[str, object],
    cache_key: str,
    candidates: list[Candidate],
) -> None:
    value = {
        "cache_version": CANDIDATE_CACHE_VERSION,
        "key": cache_key,
        "fingerprint": fingerprint,
        "candidates": [asdict(candidate) for candidate in candidates],
    }
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    )
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
        temporary.write_text(encoded + "\n", encoding="utf-8")
        os.replace(temporary, path)
    except OSError:
        return


def load_or_build_candidates(
    entries: list[tuple[int, str]],
    *,
    dependency_mode: bool,
    cache_root: Path = CANDIDATE_CACHE_ROOT,
) -> tuple[list[Candidate], bool]:
    """Load validated parsed inputs, or build and cache a fresh copy."""

    fingerprint = candidate_cache_fingerprint(dependency_mode=dependency_mode)
    if not _safe_cache_fingerprint(fingerprint):
        candidates = build_candidates()
        if dependency_mode:
            graph = build_call_graph(entries)
            add_dependency_evidence(candidates, graph)
        return candidates, False
    cache_key = candidate_cache_key(fingerprint)
    cache_path = cache_root / f"{cache_key}.json"
    cached = _read_candidate_cache(cache_path, fingerprint, cache_key)
    if cached is not None and _cached_mismatches_are_current(cached):
        return cached, True

    candidates = build_candidates()
    if dependency_mode:
        graph = build_call_graph(entries)
        add_dependency_evidence(candidates, graph)
    _write_candidate_cache(cache_path, fingerprint, cache_key, candidates)
    return candidates, False


def _is_resolved(candidate: Candidate) -> bool:
    return candidate.state == "FUNCTION"


def _dependency_resolved(candidate: Candidate) -> bool:
    return _is_resolved(candidate)


def _is_weak(candidate: Candidate) -> bool:
    return candidate.state == "FUNCTION" and not candidate.terminal


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

    quality_prerequisites = {
        dependency
        for address in unfinished
        for dependency in edges.get(address, ())
        if _is_weak(by_address[dependency])
    }
    for address in quality_prerequisites:
        by_address[address].quality_prerequisite = True

    work = unfinished | quality_prerequisites
    unfinished_edges = {
        source: {
            target
            for target in targets
            if target in work
            and not (source in unfinished and target in quality_prerequisites)
        }
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

    if candidate.size:
        opportunity = candidate.unresolved_bytes
        rank += min(opportunity / 16.0, 160.0)
        reasons.append(f"{opportunity:.0f} unresolved retail byte(s)")

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
    if candidate.score_ceiling is not None:
        reasons.append(f"score ceiling {candidate.score_ceiling * 100:.1f}%")
    if candidate.map_defect:
        rank -= 1000.0
        reasons.append("map defect: score ceiling is below 60%")

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
    if candidate.has_actionable_mismatch:
        if candidate.mismatch_windows:
            classes = ", ".join(candidate.mismatch_classifications) or "instruction"
            reasons.append(
                f"saved mismatch has {candidate.mismatch_windows} window(s): {classes}"
            )
        else:
            reasons.append("an actionable saved mismatch is available")

    if candidate.deferred_reason:
        if candidate.manual_blocker:
            blocker_state = "manual blocker"
        elif candidate.dependency_ready:
            blocker_state = "resolved prerequisite record"
        else:
            blocker_state = "declared prerequisite"
        reasons.append(f"{blocker_state}: {candidate.deferred_reason}")
        reasons.append(f"advisory blocker kind {candidate.blocker_kind or 'semantic'}")

    if candidate.active_penalty_attempts:
        penalty = min(candidate.active_penalty_attempts * 220.0, 500.0)
        rank -= penalty
        reasons.append(
            f"{candidate.active_penalty_attempts} prior zero-yield campaign(s) "
            f"used {candidate.prior_minutes:.1f} minute(s)"
        )
    elif candidate.prior_attempts:
        retained = candidate.prior_effective_bytes + candidate.prior_initialized_bytes
        reasons.append(
            f"{candidate.prior_attempts} prior campaign(s) retained {retained:.1f} byte(s)"
        )
    if candidate.active_penalty_attempts != candidate.prior_zero_yield_attempts:
        reasons.append(
            f"new evidence reset {candidate.prior_zero_yield_attempts} historical "
            "zero-yield campaign(s)"
        )

    if candidate.dependency_component >= 0:
        if candidate.quality_prerequisite:
            rank += 180.0
            reasons.append("provisional prerequisite for unfinished caller")
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


def estimate_yield(candidate: Candidate, queue: str | None) -> None:
    """Estimate retained bytes per minute from current and local evidence."""

    if queue == "refinement":
        expected_minutes = min(20.0, max(5.0, 4.0 + candidate.size / 250.0))
        if candidate.match is not None and candidate.match >= 0.5:
            confidence = 0.35
        else:
            current_match = candidate.match or 0.0
            confidence = 0.15 * max(0.1, current_match / 0.5)
    else:
        expected_minutes = min(60.0, max(6.0, 5.0 + candidate.size / 180.0))
        if candidate.size <= 600:
            confidence = 0.42
        elif candidate.size <= 1500:
            confidence = 0.28
        elif candidate.size <= 3000:
            confidence = 0.16
        else:
            confidence = 0.025
        nearby = candidate.nearby_provisional_scores
        if nearby:
            average = sum(nearby) / len(nearby)
            confidence *= max(0.4, min(1.5, average / 0.5))
        elif candidate.siblings == 0:
            confidence *= 0.7

    if not candidate.source:
        confidence *= 0.8
    if candidate.dependency_component >= 0 and not candidate.dependency_ready:
        confidence *= 0.65
    if candidate.manual_blocker:
        confidence *= 0.1
    if candidate.indirect_calls or candidate.indirect_jumps:
        confidence *= 0.75
    if candidate.quality_prerequisite:
        confidence *= 1.2
    retry_penalty = max(
        0.15 ** candidate.active_penalty_attempts,
        ZERO_YIELD_PENALTY_FLOOR,
    )
    confidence *= retry_penalty

    if candidate.map_defect:
        confidence = 0.0

    candidate.expected_retained_bytes = candidate.unresolved_bytes * confidence
    candidate.expected_minutes = expected_minutes
    candidate.expected_bytes_per_minute = (
        candidate.expected_retained_bytes / expected_minutes if expected_minutes else 0.0
    )


def _production_forecast_yield_rate(candidate: Candidate) -> float:
    expected_bytes = candidate.expected_retained_bytes
    expected_minutes = candidate.expected_minutes
    if (
        isinstance(expected_bytes, bool)
        or not isinstance(expected_bytes, (int, float))
        or not math.isfinite(float(expected_bytes))
        or expected_bytes <= 0.0
        or isinstance(expected_minutes, bool)
        or not isinstance(expected_minutes, (int, float))
        or not math.isfinite(float(expected_minutes))
        or expected_minutes <= 0.0
    ):
        return 0.0
    return float(expected_bytes) / float(expected_minutes)


def _record_time(record: dict[str, object]) -> str:
    for field_name in ("ended_at", "timestamp", "started_at"):
        value = record.get(field_name)
        if isinstance(value, str) and value:
            return value
    return ""


def _time_value(value: str) -> float:
    try:
        parsed = datetime.fromisoformat(value)
    except (TypeError, ValueError, OverflowError):
        return float("-inf")
    if parsed.tzinfo is None:
        return float("-inf")
    return parsed.timestamp()


def _record_addresses(record: dict[str, object]) -> list[int]:
    values = record.get("addresses", [])
    if not isinstance(values, list):
        return []
    addresses: list[int] = []
    for value in values:
        try:
            addresses.append(int(str(value), 16))
        except (TypeError, ValueError):
            continue
    return addresses


def _target_retained_bytes(
    record: dict[str, object], address: int, target_count: int
) -> float:
    target_deltas = record.get("target_deltas")
    if isinstance(target_deltas, dict):
        target = target_deltas.get(f"0x{address:08X}")
        if isinstance(target, dict):
            return max(
                sum(
                    float(target.get(field_name, 0.0) or 0.0)
                    for field_name in (
                        "effective_bytes",
                        "initialized_bytes",
                        "resource_explained_bytes",
                    )
                ),
                0.0,
            )
    retained = sum(
        float(record.get(field_name, 0.0) or 0.0)
        for field_name in (
            "effective_bytes",
            "initialized_bytes",
            "resource_explained_bytes",
        )
    )
    return max(retained / max(target_count, 1), 0.0)


def _prediction_features(record: dict[str, object]) -> dict[str, object]:
    prediction = record.get("prediction")
    if not isinstance(prediction, dict):
        return {}
    features = prediction.get("features")
    return features if isinstance(features, dict) else {}


def _feature_value(record: dict[str, object], *names: str) -> object | None:
    features = _prediction_features(record)
    for name in names:
        if name in features:
            return features[name]
        if name in record:
            return record[name]
    return None


def _prediction_context_for_address(
    record: dict[str, object], address: int
) -> tuple[dict[str, object], int, bool]:
    """Get the newest target-specific prediction, or the legacy campaign data."""

    events = record.get("prediction_events")
    if isinstance(events, list):
        for event in reversed(events):
            if not isinstance(event, dict):
                continue
            event_addresses = _record_addresses(event)
            prediction = event.get("prediction")
            expected = (
                prediction.get("expected_retained_bytes")
                if isinstance(prediction, dict)
                else None
            )
            if (
                address in event_addresses
                and isinstance(expected, (int, float))
                and not isinstance(expected, bool)
            ):
                target = f"0x{address:08X}"
                features = prediction.get("features")
                per_target = (
                    features.get("per_target")
                    if isinstance(features, dict)
                    else None
                )
                target_prediction = (
                    per_target.get(target)
                    if isinstance(per_target, dict)
                    else None
                )
                if isinstance(target_prediction, dict):
                    median = target_prediction.get("median_retained_bytes")
                    lower = target_prediction.get("lower_retained_bytes")
                    if (
                        isinstance(median, (int, float))
                        and not isinstance(median, bool)
                        and isinstance(lower, (int, float))
                        and not isinstance(lower, bool)
                    ):
                        selected = dict(prediction)
                        selected["expected_retained_bytes"] = float(median)
                        selected["lower_bound_retained_bytes"] = float(lower)
                        minutes = target_prediction.get("median_minutes")
                        if isinstance(minutes, (int, float)) and not isinstance(
                            minutes, bool
                        ):
                            selected["expected_minutes"] = float(minutes)
                        return {"prediction": selected}, 1, True
                return {"prediction": prediction}, max(len(event_addresses), 1), True
    return record, max(len(_record_addresses(record)), 1), False


def _attempt_bucket_from_count(count: int) -> str:
    if count <= 0:
        return "0"
    if count == 1:
        return "1"
    return "2+"


def _normalize_attempt_bucket(value: object) -> str:
    if isinstance(value, (int, float)) and float(value).is_integer():
        return _attempt_bucket_from_count(int(value))
    if not isinstance(value, str):
        return ""
    clean = value.strip().lower()
    aliases = {
        "first": "0",
        "new": "0",
        "none": "0",
        "second": "1",
        "retry": "1",
        "third": "2+",
        "repeat": "2+",
        "multiple": "2+",
    }
    return aliases.get(clean, clean)


def _record_lane(record: dict[str, object], mode: str) -> str:
    lane = record.get("lane")
    if (
        isinstance(record.get("schema_version"), int)
        and int(record["schema_version"]) >= 3
        and isinstance(lane, str)
        and lane.strip()
    ):
        return lane.strip().lower()
    if mode == "coverage":
        return "research"
    return "production" if mode == "refinement" else mode


def _subsystem_key(value: str) -> str:
    return "".join(character for character in value.lower() if character.isalnum())


def _candidate_subsystem(candidate: Candidate) -> str:
    source_name = Path(candidate.source).stem if candidate.source else ""
    return candidate.namespace or namespace_of(candidate.name) or source_name or "(global)"


def _size_bucket_value(size: float | int | None) -> str:
    if size is None:
        return "unknown-size"
    if size < PRODUCTION_SIZE_MIN:
        return "small"
    if size <= 1000:
        return "medium"
    if size <= PRODUCTION_SIZE_MAX:
        return "large"
    return "oversized"


def _score_bucket_value(score: float | int | None) -> str:
    if score is None:
        return "unscored"
    value = float(score)
    if value > 1.0 and value <= 100.0:
        value /= 100.0
    if value < PRODUCTION_MATCH_MIN:
        return "below-50"
    if value <= 0.75:
        return "50-75"
    if value <= PRODUCTION_MATCH_MAX:
        return "75-90"
    return "above-90"


def _record_size_bucket(record: dict[str, object]) -> str:
    value = _feature_value(record, "size_bucket")
    if isinstance(value, str) and value.strip():
        return value.strip().lower()
    size = _feature_value(record, "retail_size", "size")
    return _size_bucket_value(size if isinstance(size, (int, float)) else None)


def _record_score_bucket(record: dict[str, object]) -> str:
    value = _feature_value(record, "score_bucket", "match_bucket")
    if isinstance(value, str) and value.strip():
        return value.strip().lower()
    score_value = _feature_value(record, "match", "score", "matching")
    return _score_bucket_value(
        score_value if isinstance(score_value, (int, float)) else None
    )


def _campaign_target_minutes(
    record: dict[str, object],
    addresses: list[int],
    active_addresses: list[int],
    total_minutes: float,
) -> dict[int, float]:
    default = {
        address: (
            total_minutes / len(active_addresses)
            if address in active_addresses
            else total_minutes / len(addresses)
        )
        for address in addresses
    }
    retired = set(addresses) - set(active_addresses)
    events = record.get("target_events")
    if not retired or not isinstance(events, list) or not events:
        return default
    started = _time_value(str(record.get("started_at", "")))
    ended = _time_value(_record_time(record))
    if not math.isfinite(started) or not math.isfinite(ended) or ended <= started:
        return default
    parsed_events: list[tuple[float, int, int]] = []
    added_addresses: set[int] = set()
    for event in events:
        if not isinstance(event, dict) or event.get("replaces") is None:
            return default
        added = _time_value(str(event.get("added_at", "")))
        event_addresses = _record_addresses(
            {"addresses": [event.get("address")]}
        )
        replaced_addresses = _record_addresses(
            {"addresses": [event.get("replaces")]}
        )
        if (
            not math.isfinite(added)
            or not started <= added <= ended
            or len(event_addresses) != 1
            or len(replaced_addresses) != 1
        ):
            return default
        address = event_addresses[0]
        replaced = replaced_addresses[0]
        if address not in addresses or replaced not in addresses:
            return default
        parsed_events.append((added, address, replaced))
        added_addresses.add(address)
    opened = {
        address: started for address in addresses if address not in added_addresses
    }
    durations = {address: 0.0 for address in addresses}
    previous = started
    for added, address, replaced in parsed_events:
        if added < previous or replaced not in opened or address in opened:
            return default
        durations[replaced] += added - opened.pop(replaced)
        opened[address] = added
        previous = added
    if set(opened) != set(active_addresses):
        return default
    for address, opened_at in opened.items():
        durations[address] += ended - opened_at
    duration_total = sum(durations.values())
    if duration_total <= 0.0:
        return default
    scale = total_minutes / (duration_total / 60.0)
    return {
        address: max(duration / 60.0 * scale, 0.0)
        for address, duration in durations.items()
    }


def campaign_attempts(records: list[dict[str, object]]) -> list[CampaignAttempt]:
    """Convert campaign ledger rows to per-target measured attempts."""

    attempts: list[CampaignAttempt] = []
    prior_counts: dict[tuple[str, int], int] = defaultdict(int)
    ordered_records = sorted(
        enumerate(records),
        key=lambda item: (_time_value(_record_time(item[1])), item[0]),
    )
    for record_index, record in ordered_records:
        if record.get("record_type", "campaign") != "campaign":
            continue
        mode = str(record.get("mode", ""))
        if mode not in ("coverage", "refinement"):
            continue
        result = str(record.get("result", ""))
        if result not in ("source", "no-source"):
            continue
        addresses = _record_addresses(record)
        if not addresses:
            continue
        active_value = record.get("active_addresses")
        if active_value is None:
            active_addresses = list(addresses)
        elif isinstance(active_value, list):
            active_addresses = _record_addresses({"addresses": active_value})
            if (
                not active_addresses
                or len(active_addresses) != len(set(active_addresses))
                or set(active_addresses) - set(addresses)
            ):
                continue
        else:
            continue
        total_minutes = float(record.get("minutes", 0.0) or 0.0)
        if total_minutes <= 0.0:
            continue
        active_set = set(active_addresses)
        target_minutes = _campaign_target_minutes(
            record, addresses, active_addresses, total_minutes
        )
        models_value = record.get("ruled_out_models", [])
        models = (
            tuple(str(value) for value in models_value if str(value).strip())
            if isinstance(models_value, list)
            else ()
        )
        first_score_model = str(record.get("first_score_model", "")).strip()
        if first_score_model and first_score_model not in models:
            models = (*models, first_score_model)
        campaign_id = str(record.get("campaign_id", "")).strip()
        if not campaign_id:
            campaign_id = f"legacy:{record_index}"
        lane = _record_lane(record, mode)
        subsystem = str(record.get("subsystem", "")).strip()
        for address in addresses:
            calibration_eligible = address in active_set
            prediction_context, prediction_target_count, prediction_event = (
                _prediction_context_for_address(record, address)
            )
            prediction = prediction_context.get("prediction")
            expected_value = (
                prediction.get("expected_retained_bytes")
                if prediction_event and isinstance(prediction, dict)
                else record.get("expected_retained_bytes")
            )
            expected = (
                float(expected_value) / prediction_target_count
                if isinstance(expected_value, (int, float))
                and not isinstance(expected_value, bool)
                and math.isfinite(float(expected_value))
                and expected_value > 0.0
                else None
            )
            explicit_attempt_bucket = _normalize_attempt_bucket(
                _feature_value(
                    prediction_context,
                    "attempt_bucket",
                    "prior_attempt_bucket",
                    "prior_attempts",
                )
            )
            size_bucket = _record_size_bucket(prediction_context)
            score_bucket = _record_score_bucket(prediction_context)
            attempt_bucket = explicit_attempt_bucket or _attempt_bucket_from_count(
                prior_counts[(lane, address)]
            )
            attempts.append(
                CampaignAttempt(
                    address=address,
                    campaign_id=campaign_id,
                    mode=mode,
                    lane=lane,
                    subsystem=subsystem,
                    attempt_bucket=attempt_bucket,
                    size_bucket=size_bucket,
                    score_bucket=score_bucket,
                    result=result if calibration_eligible else "no-source",
                    timestamp=_record_time(record),
                    minutes=target_minutes[address],
                    retained_bytes=_target_retained_bytes(
                        record, address, len(addresses)
                    ),
                    expected_retained_bytes=expected,
                    ruled_out_models=models,
                    calibration_eligible=calibration_eligible,
                )
            )
            prior_counts[(lane, address)] += 1
    return attempts


def _percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def _size_bucket(candidate: Candidate) -> str:
    return _size_bucket_value(candidate.size)


def _match_bucket(candidate: Candidate) -> str:
    return _score_bucket_value(candidate.match)


def _lane_rule(candidate: Candidate) -> tuple[str, str]:
    """Classify one candidate without changing it."""

    if candidate.map_defect:
        return "research", "the function boundary needs more evidence"
    if candidate.state == "FUNCTION" and (
        candidate.tool_artifact
        or (candidate.source_debt and candidate.binary_terminal)
        or (
            not candidate.terminal
            and candidate.match is not None
            and candidate.match > CLOSURE_MATCH_MIN
        )
    ):
        return "closure", "the target needs a terminal or source-debt result"

    production_checks = (
        candidate.state == "FUNCTION",
        not candidate.binary_terminal,
        candidate.match is not None,
        candidate.match is not None
        and PRODUCTION_MATCH_MIN <= candidate.match <= PRODUCTION_MATCH_MAX,
        PRODUCTION_SIZE_MIN <= candidate.size <= PRODUCTION_SIZE_MAX,
        candidate.unresolved_bytes >= PRODUCTION_UNRESOLVED_MIN,
        len(candidate.weak_dependencies) <= PRODUCTION_WEAK_DEPENDENCY_MAX,
        not candidate.deferred_reason,
        not candidate.tool_artifact,
        not candidate.source_debt,
        not candidate.map_defect,
    )
    if all(production_checks):
        return "production", "the target passes all production gates"

    if candidate.state != "FUNCTION":
        reason = "the target needs a coverage source model"
    elif candidate.match is None:
        reason = "the saved comparison has no mismatch score"
    elif candidate.match < PRODUCTION_MATCH_MIN:
        reason = "the match score is below the production range"
    elif candidate.size < PRODUCTION_SIZE_MIN or candidate.size > PRODUCTION_SIZE_MAX:
        reason = "the body size is outside the production range"
    elif candidate.unresolved_bytes < PRODUCTION_UNRESOLVED_MIN:
        reason = "the unresolved range is below the production minimum"
    elif len(candidate.weak_dependencies) > PRODUCTION_WEAK_DEPENDENCY_MAX:
        reason = "the target has too many weak dependencies"
    elif candidate.deferred_reason:
        reason = "the target has a recorded blocker"
    else:
        reason = "the target needs more source evidence"
    return "research", reason


def _base_lane(candidate: Candidate) -> tuple[str, str]:
    candidate.closure_eligible = False
    candidate.production_eligible = False
    candidate.research_eligible = False
    lane, reason = _lane_rule(candidate)
    if lane == "research" and not (
        candidate.map_defect
        or candidate.deferred_reason
        or candidate.declared_dependencies
        or candidate.manual_blocker
    ):
        return "inactive", "no specific evidence blocker is recorded"
    candidate.closure_eligible = lane == "closure"
    candidate.production_eligible = lane == "production"
    candidate.research_eligible = lane == "research"
    return lane, reason


def current_base_lane(address: int, *, root: Path = ROOT) -> str:
    """Return the current selector base lane for one canonical target."""

    if root.resolve() != ROOT.resolve():
        raise ValueError(
            "candidate base-lane validation needs the canonical repository root"
        )
    entries = parse_map()
    values, _ = load_or_build_candidates(entries, dependency_mode=True)
    candidate = next((item for item in values if item.address == address), None)
    if candidate is None:
        raise ValueError(f"0x{address:08X} is not a current candidate")
    lane, _ = _base_lane(candidate)
    return lane


def _cohort_keys(
    candidate: Candidate,
    mode: str,
    lane: str,
    *,
    subsystem: str | None = None,
    size_bucket: str | None = None,
    score_bucket: str | None = None,
    attempt_bucket: str | None = None,
) -> list[tuple[str, ...]]:
    subsystem_value = (
        subsystem or _candidate_subsystem(candidate) or "(global)"
    ).strip().lower()
    size_value = size_bucket or _size_bucket(candidate)
    score_value = score_bucket or _match_bucket(candidate)
    attempt_value = attempt_bucket or _attempt_bucket_from_count(
        candidate.prior_attempts
    )
    proposed = [
        (
            mode,
            lane,
            subsystem_value,
            size_value,
            score_value,
            attempt_value,
        ),
        (mode, lane, subsystem_value, size_value, score_value),
        (mode, lane, subsystem_value),
        (mode, lane, size_value, score_value, attempt_value),
        (mode, lane, size_value, score_value),
        (mode, lane, size_value),
        (mode, lane),
    ]
    keys: list[tuple[str, ...]] = []
    for key in proposed:
        if key not in keys:
            keys.append(key)
    return keys


def _cohort_estimate(
    candidate: Candidate,
    mode: str,
    lane: str,
    grouped: dict[tuple[str, ...], list[CampaignAttempt]],
    *,
    subsystem: str | None = None,
    size_bucket: str | None = None,
    score_bucket: str | None = None,
    attempt_bucket: str | None = None,
) -> CohortEstimate | None:
    selected_key: tuple[str, ...] | None = None
    selected: list[CampaignAttempt] = []
    keys = _cohort_keys(
        candidate,
        mode,
        lane,
        subsystem=subsystem,
        size_bucket=size_bucket,
        score_bucket=score_bucket,
        attempt_bucket=attempt_bucket,
    )
    for key in keys:
        values = grouped.get(key, [])
        if len(values) >= COHORT_MIN_SAMPLES:
            selected_key = key
            selected = values
            break
    if not selected:
        for key in reversed(keys):
            values = grouped.get(key, [])
            if values:
                selected_key = key
                selected = values
                break
    if not selected or selected_key is None:
        return None

    if lane == "closure":
        successes = [item for item in selected if item.result == "source"]
    else:
        successes = [
            item
            for item in selected
            if item.result == "source" and item.retained_bytes > 0.0
        ]
    probability = (len(successes) + 1.0) / (len(selected) + 2.0)
    rates = [
        max(item.retained_bytes, 0.0) / item.minutes
        for item in selected
        if item.minutes > 0.0
    ]
    positive_rates = [
        item.retained_bytes / item.minutes
        for item in successes
        if item.retained_bytes > 0.0 and item.minutes > 0.0
    ]
    median_minutes = _percentile([item.minutes for item in selected], 0.5)
    if len(selected) < COHORT_MIN_SAMPLES:
        lower_bytes = 0.0
        median_bytes = (
            probability * _percentile(positive_rates, 0.5) * median_minutes
        )
    else:
        lower_bytes = _percentile(rates, LOWER_QUANTILE) * median_minutes
        median_bytes = _percentile(rates, 0.5) * median_minutes
    return CohortEstimate(
        key="/".join(selected_key),
        sample_size=len(selected),
        success_probability=probability,
        lower_retained_bytes=min(lower_bytes, candidate.unresolved_bytes),
        median_retained_bytes=min(median_bytes, candidate.unresolved_bytes),
        median_minutes=median_minutes,
    )


def _valid_retry_evidence(
    latest_failure: CampaignAttempt,
    records: list[dict[str, object]],
) -> dict[str, object] | None:
    if latest_failure.campaign_id.startswith("legacy:"):
        return None
    failure_time = _time_value(latest_failure.timestamp)
    valid: list[dict[str, object]] = []
    for record in records:
        if record.get("record_type") != "evidence":
            continue
        if record.get("failed_campaign_id") != latest_failure.campaign_id:
            continue
        if latest_failure.address not in _record_addresses(record):
            continue
        failed_model = str(record.get("failed_model", "")).strip()
        if not failed_model or failed_model not in latest_failure.ruled_out_models:
            continue
        changed_assumption = str(record.get("changed_assumption", "")).strip()
        changed_source = str(record.get("changed_source", "")).strip()
        if not changed_assumption and not changed_source:
            continue
        if _time_value(_record_time(record)) <= failure_time:
            continue
        valid.append(record)
    if not valid:
        return None
    return max(valid, key=lambda item: _time_value(_record_time(item)))


def _history_penalty(attempts: list[CampaignAttempt]) -> float:
    penalty = 1.0
    for attempt in attempts:
        if attempt.result == "no-source":
            penalty *= NO_SOURCE_PENALTY
            continue
        if not attempt.under_yield:
            continue
        ratio = attempt.realization_ratio or 0.0
        penalty *= max(
            UNDER_YIELD_PENALTY_FLOOR,
            min(1.0, ratio / UNDER_YIELD_RATIO),
        )
    return max(penalty, HISTORY_PENALTY_FLOOR)


def _research_value(candidate: Candidate) -> float:
    value = 0.0
    value += candidate.immediate_unlocks * 100.0
    value += candidate.large_goal_reach * 30.0
    value += len(candidate.direct_unfinished_callers) * 20.0
    value += 80.0 if candidate.quality_prerequisite else 0.0
    value += 30.0 if candidate.deferred_reason else 0.0
    value += min(len(candidate.weak_dependencies), 5) * 10.0
    value += min(candidate.indirect_calls + candidate.indirect_jumps, 5) * 8.0
    value += min(candidate.no_source_attempts, 3) * 5.0
    value += 40.0 if candidate.retry_eligible else 0.0
    return value


def _add_cohort_attempt(
    grouped: dict[tuple[str, ...], list[CampaignAttempt]],
    candidate: Candidate,
    attempt: CampaignAttempt,
    lane: str,
) -> None:
    for key in _cohort_keys(
        candidate,
        attempt.mode,
        lane,
        subsystem=attempt.subsystem or _candidate_subsystem(candidate),
        size_bucket=(
            attempt.size_bucket
            if attempt.size_bucket != "unknown-size"
            else None
        ),
        score_bucket=(
            attempt.score_bucket
            if attempt.score_bucket != "unscored"
            else None
        ),
        attempt_bucket=attempt.attempt_bucket,
    ):
        grouped[key].append(attempt)


def replay_cohort_calibration(
    candidates: list[Candidate],
    records: list[dict[str, object]],
    *,
    warmup: int = COHORT_MIN_SAMPLES,
) -> list[CalibrationPoint]:
    """Replay ledger results and check forecasts against later outcomes.

    This function does not change the candidates or ledger rows. Each forecast
    uses only attempts that ended before the result under test.
    """

    if warmup < 1:
        raise ValueError("warmup must be at least 1")
    by_address = {candidate.address: candidate for candidate in candidates}
    grouped: dict[tuple[str, ...], list[CampaignAttempt]] = defaultdict(list)
    direct: dict[int, list[CampaignAttempt]] = defaultdict(list)
    points: list[CalibrationPoint] = []

    for attempt in campaign_attempts(records):
        candidate = by_address.get(attempt.address)
        if candidate is None:
            continue
        lane = attempt.lane
        if not attempt.calibration_eligible:
            direct[attempt.address].append(attempt)
            continue
        if lane in ("closure", "production"):
            estimate = _cohort_estimate(
                candidate,
                attempt.mode,
                lane,
                grouped,
                subsystem=attempt.subsystem or _candidate_subsystem(candidate),
                size_bucket=(
                    attempt.size_bucket
                    if attempt.size_bucket != "unknown-size"
                    else None
                ),
                score_bucket=(
                    attempt.score_bucket
                    if attempt.score_bucket != "unscored"
                    else None
                ),
                attempt_bucket=attempt.attempt_bucket,
            )
            if estimate is not None and estimate.sample_size >= warmup:
                prior_attempts = [
                    item
                    for item in direct.get(candidate.address, [])
                    if item.mode == attempt.mode and item.lane == lane
                ]
                lower_bytes = estimate.lower_retained_bytes * _history_penalty(
                    prior_attempts
                )
                direct_rates = [
                    item.retained_bytes / item.minutes
                    for item in prior_attempts
                    if item.retained_bytes > 0.0 and item.minutes > 0.0
                ]
                if direct_rates:
                    lower_bytes = min(
                        lower_bytes,
                        _percentile(direct_rates, LOWER_QUANTILE)
                        * estimate.median_minutes,
                    )
                lower_bytes = min(
                    max(lower_bytes, 0.0), candidate.unresolved_bytes
                )
                actual = max(attempt.retained_bytes, 0.0)
                points.append(
                    CalibrationPoint(
                        campaign_id=attempt.campaign_id,
                        address=attempt.address,
                        lane=lane,
                        cohort_key=estimate.key,
                        sample_size=estimate.sample_size,
                        predicted_lower_bytes=lower_bytes,
                        actual_retained_bytes=actual,
                        covered=actual + 1e-9 >= lower_bytes,
                    )
                )
        direct[attempt.address].append(attempt)
        _add_cohort_attempt(grouped, candidate, attempt, lane)
    return points


def _circuit_campaign_failed(items: list[CampaignAttempt]) -> bool:
    if any(item.result == "no-source" for item in items):
        return True
    forecasted = [
        item
        for item in items
        if item.expected_retained_bytes is not None
        and item.expected_retained_bytes > 0.0
    ]
    expected = sum(float(item.expected_retained_bytes) for item in forecasted)
    actual = sum(max(item.retained_bytes, 0.0) for item in forecasted)
    return expected > 0.0 and actual / expected < 0.10


def source_circuit_open(
    records: list[dict[str, object]], subsystem: str
) -> bool:
    """Return whether recent measured failures stop source production work."""

    subsystem_key = _subsystem_key(subsystem)
    if not subsystem_key:
        return False
    grouped: dict[str, list[CampaignAttempt]] = {}
    for attempt in campaign_attempts(records):
        if (
            attempt.calibration_eligible
            and _subsystem_key(attempt.subsystem) == subsystem_key
        ):
            grouped.setdefault(attempt.campaign_id, []).append(attempt)
    recent = list(grouped.values())[-CIRCUIT_BREAKER_WINDOW:]
    return (
        len(recent) >= CIRCUIT_BREAKER_FAILURES
        and sum(_circuit_campaign_failed(items) for items in recent)
        >= CIRCUIT_BREAKER_FAILURES
    )


def retry_evidence_for_target(
    records: list[dict[str, object]],
    address: int,
    mode: str,
    lane: str,
) -> dict[str, object] | None:
    """Return unused evidence that supports a retry after the latest failure."""

    attempts = [
        attempt
        for attempt in campaign_attempts(records)
        if attempt.address == address
        and attempt.mode == mode
        and attempt.lane == lane
    ]
    if not attempts:
        return None
    latest = attempts[-1]
    failed = latest.result == "no-source" or (
        latest.realization_ratio is not None
        and latest.realization_ratio < 0.10
    )
    return _valid_retry_evidence(latest, records) if failed else None


def apply_lane_model(
    candidates: list[Candidate],
    records: list[dict[str, object]],
    queue: str | None,
) -> None:
    """Assign exclusive lanes and measured estimates to candidates."""

    by_address = {candidate.address: candidate for candidate in candidates}
    for candidate in candidates:
        candidate.lane, candidate.lane_reason = _base_lane(candidate)

    attempts = campaign_attempts(records)
    direct: dict[int, list[CampaignAttempt]] = defaultdict(list)
    grouped: dict[tuple[str, ...], list[CampaignAttempt]] = defaultdict(list)
    circuit_groups: dict[
        str, dict[str, list[CampaignAttempt]]
    ] = defaultdict(dict)
    for attempt in attempts:
        candidate = by_address.get(attempt.address)
        if candidate is not None:
            direct[attempt.address].append(attempt)
            if attempt.calibration_eligible:
                _add_cohort_attempt(grouped, candidate, attempt, attempt.lane)
        subsystem = attempt.subsystem or (
            _candidate_subsystem(candidate) if candidate is not None else ""
        )
        if subsystem and attempt.calibration_eligible:
            grouped_campaigns = circuit_groups[_subsystem_key(subsystem)]
            grouped_campaigns.setdefault(attempt.campaign_id, []).append(attempt)

    for candidate in candidates:
        mode = "coverage" if candidate.state != "FUNCTION" else "refinement"
        candidate_attempts = [
            item
            for item in direct.get(candidate.address, [])
            if item.mode == mode and item.lane == candidate.lane
        ]
        candidate.prior_attempts = len(candidate_attempts)
        candidate.prior_minutes = sum(item.minutes for item in candidate_attempts)
        candidate.prior_effective_bytes = sum(
            item.retained_bytes for item in candidate_attempts
        )
        candidate.no_source_attempts = sum(
            item.result == "no-source" for item in candidate_attempts
        )
        candidate.prior_zero_yield_attempts = sum(
            item.retained_bytes <= 0.0 for item in candidate_attempts
        )
        candidate.under_yield_attempts = sum(
            item.under_yield for item in candidate_attempts
        )
        candidate.penalty_attempts = (
            candidate.no_source_attempts + candidate.under_yield_attempts
        )
        candidate.history_penalty = _history_penalty(candidate_attempts)
        candidate.latest_campaign_id = (
            candidate_attempts[-1].campaign_id if candidate_attempts else ""
        )

        latest_failure = next(
            (
                item
                for item in reversed(candidate_attempts)
                if item.result == "no-source"
                or (
                    item.realization_ratio is not None
                    and item.realization_ratio < 0.10
                )
            ),
            None,
        )
        latest_attempt = candidate_attempts[-1] if candidate_attempts else None
        retry_evidence = (
            _valid_retry_evidence(latest_failure, records)
            if latest_failure is not None and latest_attempt is latest_failure
            else None
        )
        candidate.retry_eligible = retry_evidence is not None
        candidate.fresh_evidence = candidate.retry_eligible
        candidate.retry_evidence_id = (
            str(retry_evidence.get("evidence_id", ""))
            if retry_evidence is not None
            else ""
        )
        score(candidate)
        candidate.reasons = [
            reason
            for reason in candidate.reasons
            if not reason.startswith("new evidence reset ")
            and " prior zero-yield campaign(s) " not in reason
            and " prior campaign(s) retained " not in reason
        ]

        group_campaigns = list(
            circuit_groups.get(
                _subsystem_key(_candidate_subsystem(candidate)), {}
            ).values()
        )
        recent = group_campaigns[-CIRCUIT_BREAKER_WINDOW:]
        candidate.circuit_breaker_open = (
            len(recent) >= CIRCUIT_BREAKER_FAILURES
            and sum(_circuit_campaign_failed(items) for items in recent)
            >= CIRCUIT_BREAKER_FAILURES
        )

        if (
            candidate.lane in ("closure", "production")
            and latest_attempt is not None
            and (
                latest_attempt.result == "no-source"
                or (
                    latest_attempt.realization_ratio is not None
                    and latest_attempt.realization_ratio < 0.10
                )
            )
            and not candidate.retry_eligible
        ):
            candidate.closure_eligible = False
            candidate.production_eligible = False
            candidate.research_eligible = True
            candidate.lane = "research"
            candidate.lane_reason = "the latest failure has no valid retry evidence"
        elif (
            candidate.lane in ("closure", "production")
            and candidate.circuit_breaker_open
            and not candidate.retry_eligible
        ):
            candidate.closure_eligible = False
            candidate.production_eligible = False
            candidate.research_eligible = True
            candidate.lane = "research"
            candidate.lane_reason = "the source cohort circuit breaker is open"

        candidate.lane_eligible = candidate.lane != "inactive"
        if candidate.lane == "inactive":
            candidate.success_probability = None
            candidate.cohort_key = ""
            candidate.cohort_sample_size = 0
            candidate.lower_retained_bytes = None
            candidate.median_retained_bytes = None
            candidate.median_minutes = None
            candidate.expected_retained_bytes = None
            candidate.expected_minutes = None
            candidate.expected_bytes_per_minute = None
            candidate.reasons.append(candidate.lane_reason)
            continue
        if candidate.lane == "research":
            candidate.success_probability = None
            candidate.cohort_key = ""
            candidate.cohort_sample_size = 0
            candidate.lower_retained_bytes = None
            candidate.median_retained_bytes = None
            candidate.median_minutes = None
            candidate.expected_retained_bytes = None
            candidate.expected_minutes = None
            candidate.expected_bytes_per_minute = None
            candidate.research_value = _research_value(candidate)
            candidate.reasons.append(candidate.lane_reason)
            if candidate.circuit_breaker_open:
                candidate.reasons.append("source cohort circuit breaker is open")
            continue

        estimate = _cohort_estimate(candidate, mode, candidate.lane, grouped)
        if estimate is None:
            candidate.closure_eligible = False
            candidate.production_eligible = False
            candidate.research_eligible = False
            candidate.lane_eligible = False
            candidate.lane = "inactive"
            candidate.lane_reason = "no completed campaign cohort can support a forecast"
            candidate.success_probability = None
            candidate.cohort_key = ""
            candidate.cohort_sample_size = 0
            candidate.lower_retained_bytes = None
            candidate.median_retained_bytes = None
            candidate.median_minutes = None
            candidate.expected_retained_bytes = None
            candidate.expected_minutes = None
            candidate.expected_bytes_per_minute = None
            candidate.research_value = 0.0
            candidate.reasons.append(candidate.lane_reason)
            continue

        lower_bytes = estimate.lower_retained_bytes * candidate.history_penalty
        median_bytes = estimate.median_retained_bytes * candidate.history_penalty
        positive_rates = [
            item.retained_bytes / item.minutes
            for item in candidate_attempts
            if item.retained_bytes > 0.0 and item.minutes > 0.0
        ]
        if positive_rates:
            direct_lower = (
                _percentile(positive_rates, LOWER_QUANTILE)
                * estimate.median_minutes
            )
            direct_median = _percentile(positive_rates, 0.5) * estimate.median_minutes
            lower_bytes = min(lower_bytes, direct_lower)
            median_bytes = min(median_bytes, direct_median)

        candidate.success_probability = estimate.success_probability
        candidate.cohort_key = estimate.key
        candidate.cohort_sample_size = estimate.sample_size
        candidate.lower_retained_bytes = min(lower_bytes, candidate.unresolved_bytes)
        candidate.median_retained_bytes = min(median_bytes, candidate.unresolved_bytes)
        candidate.median_minutes = estimate.median_minutes
        candidate.expected_retained_bytes = candidate.median_retained_bytes
        candidate.expected_minutes = candidate.median_minutes
        candidate.expected_bytes_per_minute = (
            candidate.expected_retained_bytes / candidate.expected_minutes
            if candidate.expected_minutes
            else 0.0
        )
        candidate.research_value = 0.0
        candidate.reasons.append(candidate.lane_reason)
        candidate.reasons.append(
            f"cohort {candidate.cohort_key} has {candidate.cohort_sample_size} sample(s)"
        )
        if candidate.no_source_attempts:
            candidate.reasons.append(
                f"{candidate.no_source_attempts} no-source result(s) remain in the estimate"
            )
        if candidate.under_yield_attempts:
            candidate.reasons.append(
                f"{candidate.under_yield_attempts} result(s) retained less than 25% of forecast"
            )
        if candidate.retry_eligible:
            candidate.reasons.append("linked evidence permits one retry")


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
    queue: str | None = None,
    yield_order: bool = False,
    independent_refinement: bool = False,
    lane: str | None = None,
    records: list[dict[str, object]] | None = None,
) -> list[Candidate]:
    if lane is not None and lane not in LANES:
        raise ValueError(f"unknown candidate lane: {lane}")
    chosen: list[Candidate] = []
    for candidate in candidates:
        if lane is not None and candidate.terminal and not candidate.tool_artifact:
            continue
        if queue == "coverage" and candidate.state not in ("STUB", "NOT_STARTED"):
            continue
        if queue == "refinement" and (
            candidate.state != "FUNCTION"
            or (candidate.terminal and not candidate.tool_artifact)
        ):
            continue
        if independent_refinement and not (
            candidate.state == "FUNCTION"
            and not candidate.binary_terminal
            and candidate.match is not None
            and not candidate.tool_artifact
            and not candidate.quality_prerequisite
            and candidate.unresolved_bytes >= INDEPENDENT_REFINEMENT_MIN_BYTES
        ):
            continue
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
        if exclude_capped and candidate.tool_artifact and queue != "refinement":
            continue
        if debt_only and not (candidate.lint_errors or candidate.lint_warnings):
            continue
        if (
            queue is None
            and lane is None
            and not (stubs_only or leaves_only or near_only or debt_only)
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
        if independent_refinement:
            candidate.reasons.append("saved comparison available")
            candidate.reasons.append("outside the dependency frontier")
    if lane is not None:
        apply_lane_model(chosen, records or [], queue)
        chosen = [candidate for candidate in chosen if candidate.lane == lane]
    else:
        for candidate in chosen:
            estimate_yield(candidate, queue)

    if lane == "production":
        chosen.sort(
            key=lambda item: (
                item.map_defect,
                _production_forecast_yield_rate(item) <= 0.0,
                -_production_forecast_yield_rate(item),
                item.active_penalty_attempts,
                -item.rank,
                item.address,
            )
        )
    elif lane == "closure":
        chosen.sort(
            key=lambda item: (
                item.prior_attempts > 0,
                not bool(item.tool_artifact),
                not item.source_debt,
                -float(item.success_probability or 0.0),
                float(item.median_minutes or sys.maxsize),
                -float(item.match or 0.0),
                item.address,
            )
        )
    elif lane == "research":
        chosen.sort(
            key=lambda item: (
                -item.research_value,
                not item.circuit_breaker_open,
                item.address,
            )
        )
    elif yield_order:
        chosen.sort(
            key=lambda item: (
                item.map_defect,
                -float(item.expected_bytes_per_minute or 0.0),
                item.active_penalty_attempts,
                -item.rank,
                item.address,
            )
        )
    elif queue == "refinement":
        chosen.sort(
            key=lambda item: (
                item.map_defect,
                not item.quality_prerequisite,
                -item.unresolved_bytes,
                not item.source_debt,
                -item.rank,
                item.address,
            )
        )
    elif new_work_only:
        chosen.sort(
            key=lambda item: (
                item.map_defect,
                not item.dependency_ready,
                item.manual_blocker,
                -item.immediate_unlocks,
                -item.large_goal_reach,
                item.state != "STUB",
                len(item.weak_dependencies),
                item.indirect_calls + item.indirect_jumps,
                -item.unresolved_bytes,
                -item.rank,
                item.size,
                item.address,
            )
        )
    else:
        chosen.sort(
            key=lambda item: (item.map_defect, -item.rank, item.size, item.address)
        )
    return chosen


def print_table(
    chosen: list[Candidate],
    limit: int,
    show_reasons: bool,
    show_yield: bool = False,
    lane: str | None = None,
) -> None:
    if not chosen:
        print("No candidate matches the given filters.")
        return

    shown = chosen[:limit] if limit else chosen
    name_width = min(max(len(item.name) for item in shown), 52)

    show_dependencies = any(item.dependency_component >= 0 for item in shown)
    dependency_header = f"  {'DEPS':>7}  {'UNLOCK':>6}" if show_dependencies else ""
    lane_header = (
        f"  {'LANE':<10}{'ELIG':>5}  {'P SRC':>6}  {'N':>3}  "
        f"{'LOW B':>8}  {'MED B':>8}  {'MED MIN':>7}  {'VALUE':>7}"
        if lane is not None
        else ""
    )
    yield_header = (
        f"  {'EST B':>8}  {'EST MIN':>7}  {'EST B/M':>8}"
        if show_yield and lane is None
        else ""
    )
    print(
        f"{'ADDRESS':<11}{'NAME':<{name_width + 2}}{'STATE':<13}{'SIZE':>6}  "
        f"{'MATCH':>7}{dependency_header}{lane_header}{yield_header}  TU"
    )
    print(
        "-"
        * (
            11
            + name_width
            + 2
            + 13
            + 6
            + 9
            + len(dependency_header)
            + len(lane_header)
            + len(yield_header)
            + 4
        )
    )
    for item in shown:
        name = item.name if len(item.name) <= name_width else item.name[: name_width - 1] + "~"
        state = "MAP_REPAIR" if item.map_defect else item.state
        dependency_text = ""
        if show_dependencies:
            status = "ready" if item.dependency_ready else str(len(item.unresolved_dependencies))
            dependency_text = f"  {status:>7}  {item.immediate_unlocks:>6}"
        if lane is not None:
            probability = (
                "-"
                if item.success_probability is None
                else f"{item.success_probability * 100:.0f}%"
            )
            lower = (
                "-"
                if item.lower_retained_bytes is None
                else f"{item.lower_retained_bytes:.1f}"
            )
            median = (
                "-"
                if item.median_retained_bytes is None
                else f"{item.median_retained_bytes:.1f}"
            )
            minutes = (
                "-" if item.median_minutes is None else f"{item.median_minutes:.1f}"
            )
            value = f"{item.research_value:.1f}" if item.lane == "research" else "-"
            lane_text = (
                f"  {item.lane:<10}{'yes' if item.lane_eligible else 'no':>5}  "
                f"{probability:>6}  {item.cohort_sample_size:>3}  {lower:>8}  "
                f"{median:>8}  {minutes:>7}  {value:>7}"
            )
        else:
            lane_text = ""
        yield_text = (
            f"  {float(item.expected_retained_bytes or 0.0):>8.1f}  "
            f"{float(item.expected_minutes or 0.0):>7.1f}  "
            f"{float(item.expected_bytes_per_minute or 0.0):>8.1f}"
            if show_yield and lane is None
            else ""
        )
        print(
            f"{item.address_text:<11}{name:<{name_width + 2}}{state:<13}"
            f"{item.size:>6}  {item.match_text:>7}{dependency_text}{lane_text}"
            f"{yield_text}  {item.source or '-'}"
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


def prediction_feature_handoff(candidate: Candidate) -> dict[str, object]:
    """Return one campaign-ready forecast for a selected source candidate."""

    if candidate.lane not in ("closure", "production"):
        raise ValueError(
            "prediction export needs a closure or production candidate"
        )
    values = (
        candidate.success_probability,
        candidate.lower_retained_bytes,
        candidate.median_retained_bytes,
        candidate.median_minutes,
    )
    if any(value is None or not math.isfinite(float(value)) for value in values):
        raise ValueError("the selected candidate has no complete prediction")
    success_probability = float(candidate.success_probability)
    lower_retained_bytes = float(candidate.lower_retained_bytes)
    median_retained_bytes = float(candidate.median_retained_bytes)
    median_minutes = float(candidate.median_minutes)
    if not 0.0 <= success_probability <= 1.0:
        raise ValueError("the prediction success probability is invalid")
    if candidate.cohort_sample_size < 0:
        raise ValueError("the prediction cohort sample count is invalid")
    if lower_retained_bytes < 0.0 or median_retained_bytes < 0.0:
        raise ValueError("the prediction byte estimate is invalid")
    if lower_retained_bytes > median_retained_bytes:
        raise ValueError("the prediction lower bound exceeds the median")
    if median_minutes <= 0.0:
        raise ValueError("the prediction time estimate is invalid")

    mode = "coverage" if candidate.state != "FUNCTION" else "refinement"
    fingerprint = candidate_selection_fingerprint(dependency_mode=True)
    return {
        "schema_version": PREDICTION_HANDOFF_SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "selection_fingerprint": fingerprint,
        "selection_fingerprint_sha256": candidate_cache_key(fingerprint),
        "prediction_version": PREDICTION_VERSION,
        "lane": candidate.lane,
        "mode": mode,
        "address": candidate.address_text,
        "name": candidate.name,
        "subsystem": _candidate_subsystem(candidate),
        "retail_size": candidate.size,
        "size_bucket": _size_bucket(candidate),
        "match": candidate.match,
        "score_bucket": _match_bucket(candidate),
        "attempt_bucket": _attempt_bucket_from_count(candidate.prior_attempts),
        "prior_attempts": candidate.prior_attempts,
        "unresolved_bytes": candidate.unresolved_bytes,
        "cohort_key": candidate.cohort_key,
        "success_probability": success_probability,
        "cohort_sample_size": candidate.cohort_sample_size,
        "lower_retained_bytes": lower_retained_bytes,
        "median_retained_bytes": median_retained_bytes,
        "median_minutes": median_minutes,
        "history_penalty": candidate.history_penalty,
        "retry_evidence_id": candidate.retry_evidence_id,
        "expected_retained_bytes": median_retained_bytes,
        "expected_minutes": median_minutes,
        "prediction_lower_bound_bytes": lower_retained_bytes,
    }


def write_prediction_feature_handoff(
    path: Path, candidate: Candidate
) -> dict[str, object]:
    """Write one forecast atomically and return its JSON object."""

    payload = prediction_feature_handoff(candidate)
    encoded = json.dumps(
        payload,
        indent=2,
        sort_keys=True,
        ensure_ascii=True,
        allow_nan=False,
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(encoded + "\n", encoding="utf-8")
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    return payload


def current_prediction_feature_handoff(
    address: int, lane: str
) -> dict[str, object]:
    """Recompute the forecast for one exact currently selectable target."""

    if lane not in ("closure", "production"):
        raise ValueError("prediction validation needs a closure or production lane")
    entries = parse_map()
    candidates, _ = load_or_build_candidates(entries, dependency_mode=True)
    chosen = select(
        candidates,
        namespace=None,
        stubs_only=False,
        leaves_only=False,
        near_only=False,
        max_size=None,
        exclude_capped=True,
        debt_only=False,
        new_work_only=False,
        include_deferred=False,
        allow_large=True,
        queue="refinement",
        yield_order=False,
        independent_refinement=False,
        lane=lane,
        records=read_records(),
    )
    selected = next(
        (
            candidate
            for candidate in chosen
            if candidate.address == address
        ),
        None,
    )
    if selected is None:
        raise ValueError(
            f"0x{address:08X} is no longer selectable in the {lane} lane"
        )
    return prediction_feature_handoff(selected)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rank reconstruction candidates from repository evidence.",
    )
    parser.add_argument("namespace", nargs="?", help="restrict to one map namespace, e.g. Nu3D")
    parser.add_argument("--stubs", action="store_true", help="only functions marked STUB in src/")
    parser.add_argument("--leaves", action="store_true", help="only small unannotated functions")
    parser.add_argument(
        "--coverage",
        action="store_true",
        help="only STUB and unstarted functions",
    )
    parser.add_argument(
        "--refine",
        action="store_true",
        help="rank all provisional, tool-only, and source-debt functions",
    )
    parser.add_argument(
        "--refine-independent",
        action="store_true",
        help=(
            "rank provisional functions outside the dependency frontier with a "
            "saved comparison and at least 100 unresolved bytes"
        ),
    )
    parser.add_argument(
        "--lane",
        choices=LANES,
        help="select the closure, production, or research queue",
    )
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
        help=(
            "select this exact closure or production target, or show its "
            "research dependency frontier"
        ),
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
    parser.add_argument(
        "--yield",
        dest="yield_order",
        action="store_true",
        help="rank by estimated retained bytes per minute",
    )
    parser.add_argument("--json", action="store_true", help="emit JSON instead of a table")
    parser.add_argument(
        "--prediction-features-out",
        type=Path,
        metavar="PATH",
        help="write one selected closure or production forecast as JSON",
    )
    args = parser.parse_args()
    queue_modes = sum((args.coverage, args.refine, args.refine_independent))
    if queue_modes > 1:
        parser.error(
            "--coverage, --refine, and --refine-independent cannot be combined"
        )
    if args.refine_independent and (args.near or args.debt or args.quality):
        parser.error(
            "--refine-independent cannot be combined with --near, --debt, or --quality"
        )
    if args.refine_independent and args.target_address is not None:
        parser.error("--refine-independent cannot be combined with --for")
    if args.lane and args.refine_independent:
        parser.error("--lane cannot be combined with --refine-independent")
    if args.lane in ("closure", "production") and args.coverage:
        parser.error("the closure and production lanes contain refinement work")
    limit_explicit = args.limit is not None or args.all
    if args.all:
        args.limit = 0
    elif args.limit is None:
        args.limit = 20 if args.list_blockers is not None else 10

    if args.diagnose and args.json:
        parser.error("--diagnose cannot be combined with --json")
    if args.prediction_features_out is not None:
        if args.lane not in ("closure", "production"):
            parser.error(
                "--prediction-features-out requires --lane closure or --lane production"
            )
        if (
            args.diagnose
            or args.record_deferral is not None
            or args.clear_deferral is not None
            or args.list_blockers is not None
        ):
            parser.error("--prediction-features-out requires candidate selection")

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

    if args.coverage:
        queue: str | None = "coverage"
    elif args.refine or args.refine_independent:
        queue = "refinement"
    elif args.lane in ("closure", "production"):
        queue = "refinement"
    elif args.lane == "research":
        queue = None
    else:
        queue = "coverage"
    dependency_mode = (
        args.lane is not None
        or not (args.near or args.debt or args.quality)
        or args.refine
    )
    try:
        candidates, _ = load_or_build_candidates(
            entries, dependency_mode=dependency_mode
        )
    except (DependencyUnavailable, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

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
        new_work_only=(
            dependency_mode and queue == "coverage" and args.lane is None
        ),
        include_deferred=include_blocked,
        allow_large=True,
        queue=(
            queue
            if args.lane is not None or not (args.near or args.debt or args.quality)
            else None
        ),
        yield_order=args.yield_order,
        independent_refinement=args.refine_independent,
        lane=args.lane,
        records=read_records() if args.lane is not None else None,
    )
    if args.target_address is not None:
        if args.lane in ("closure", "production"):
            chosen = [
                item for item in chosen if item.address == args.target_address
            ]
        else:
            frontier = dependency_frontier_for(args.target_address, candidates)
            if include_blocked and not frontier:
                frontier = {args.target_address}
            chosen = [item for item in chosen if item.address in frontier]

    visible = chosen[: args.limit] if args.limit else chosen
    if args.prediction_features_out is not None:
        if args.target_address is not None:
            exact_prediction_target = next(
                (
                    item
                    for item in chosen
                    if item.address == args.target_address
                ),
                None,
            )
            visible = (
                [exact_prediction_target]
                if exact_prediction_target is not None
                else []
            )
        if len(visible) != 1:
            parser.error(
                "--prediction-features-out needs the exact requested target or "
                "exactly one selected candidate"
            )
        try:
            write_prediction_feature_handoff(args.prediction_features_out, visible[0])
        except (OSError, ValueError) as error:
            parser.error(str(error))

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
                    "score_ceiling": item.score_ceiling,
                    "map_defect": item.map_defect,
                    "work_target": item.work_target,
                    "match": item.match,
                    "effective": item.effective,
                    "tool_artifact": item.tool_artifact,
                    "actionable_mismatch": item.has_actionable_mismatch,
                    "mismatch_artifact": item.mismatch_artifact,
                    "mismatch_windows": item.mismatch_windows,
                    "mismatch_classifications": item.mismatch_classifications,
                    "mismatch_taxonomy": mismatch_taxonomy(item),
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
                    "unresolved_bytes": item.unresolved_bytes,
                    "prior_attempts": item.prior_attempts,
                    "prior_zero_yield_attempts": item.prior_zero_yield_attempts,
                    "penalty_attempts": item.active_penalty_attempts,
                    "prior_minutes": item.prior_minutes,
                    "expected_retained_bytes": item.expected_retained_bytes,
                    "expected_minutes": item.expected_minutes,
                    "expected_bytes_per_minute": item.expected_bytes_per_minute,
                    "lane": item.lane,
                    "lane_eligible": item.lane_eligible,
                    "closure_eligible": item.closure_eligible,
                    "production_eligible": item.production_eligible,
                    "research_eligible": item.research_eligible,
                    "lane_reason": item.lane_reason,
                    "success_probability": item.success_probability,
                    "sample_size": item.cohort_sample_size,
                    "cohort_key": item.cohort_key,
                    "cohort_sample_size": item.cohort_sample_size,
                    "lower_retained_bytes": item.lower_retained_bytes,
                    "median_retained_bytes": item.median_retained_bytes,
                    "median_minutes": item.median_minutes,
                    "no_source_attempts": item.no_source_attempts,
                    "under_yield_attempts": item.under_yield_attempts,
                    "history_penalty": item.history_penalty,
                    "latest_campaign_id": item.latest_campaign_id,
                    "retry_eligible": item.retry_eligible,
                    "retry_evidence_id": item.retry_evidence_id,
                    "fresh_evidence": item.fresh_evidence,
                    "circuit_breaker_open": item.circuit_breaker_open,
                    "research_value": item.research_value,
                    "terminal": item.terminal,
                    "reasons": item.reasons,
                }
                for item in visible
            ],
            sys.stdout,
            indent=2,
        )
        print()
        return 0

    if not REPORT_JSON.exists():
        print("note: build/decomp-current-report.json is absent, so no match percent is shown.")
        print("      Run `tools/decomp report` to populate it.")

    if args.target_address is not None:
        target = next(item for item in candidates if item.address == args.target_address)
        dependency_limit = (args.limit or sys.maxsize) if limit_explicit else 12
        print_dependency_summary(target, names, dependency_limit)
    print_table(chosen, args.limit, args.why, args.yield_order, args.lane)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
