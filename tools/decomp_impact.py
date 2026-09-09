#!/usr/bin/env python3
"""Build and validate post-write impact evidence for source campaigns."""

from __future__ import annotations

import sys
from pathlib import Path


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import tempfile
from collections import deque
from pathlib import PurePosixPath
from typing import Mapping, Sequence

from tools import decomp_binary, decomp_dependencies
from tools.decomp_annotations import ANNOTATION_RE, active_source_lines
from tools.decomp_dependencies import (
    DependencyGraph,
    DependencyUnavailable,
    build_call_graph,
    decoder_identity,
)


IMPACT_SCHEMA = 1
IMPACT_KIND = "post-write-impact-pack"
REVIEW_SCHEMA = 1
REVIEW_KIND = "independent-impact-review"
REVIEW_RECEIPT_KIND = "impact-review-receipt"
MAX_DOCUMENT_BYTES = 8 * 1024 * 1024
MAX_REPORT_BYTES = 64 * 1024 * 1024
MAX_EVIDENCE_FILE_BYTES = 512 * 1024
MAX_EVIDENCE_BYTES = 4 * 1024 * 1024
SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx"})
HEADER_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".ipp"})
FINALIZED_SOURCE_SUFFIXES = SOURCE_SUFFIXES | HEADER_SUFFIXES | frozenset(
    {".def", ".rc", ".s", ".asm", ".cmake"}
)
PROMOTION_CLAIMS = {
    "abi-contract": "The source preserves the supported ABI and parameter roles.",
    "control-flow": "The source preserves the supported branches, loops, and state transitions.",
    "calls-and-cleanup": "The source preserves calls, side effects, ownership, and cleanup paths.",
    "memory-and-layout": "The source preserves supported memory accesses, aliases, and layouts.",
    "ordering-and-failure-paths": "The source preserves update order and failure paths.",
    "numeric-semantics": "The source preserves integer widths, signedness, constants, and evaluation order.",
    "affected-function-regressions": "The impact review found no unexplained affected-function regression.",
}
REFUTER_CHECKS = (
    "automatic-promotion-checks",
    "equal-score-diff-changes",
    "typed-data-regressions",
    "completeness-gaps",
)
TYPED_DATA_GROUPS = (
    "variables",
    "sections",
    "vtables",
    "imports",
    "relocations",
    "debug",
)
_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


class ImpactError(ValueError):
    """The impact evidence is invalid or incomplete."""


def _json_hash(value: object) -> str:
    encoded = json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _document_hash(document: Mapping[str, object]) -> str:
    payload = dict(document)
    payload.pop("content_sha256", None)
    return _json_hash(payload)


def _document_text(document: Mapping[str, object]) -> str:
    return json.dumps(
        document,
        indent=2,
        sort_keys=True,
        allow_nan=False,
    ) + "\n"


def _file_hash(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        raise ImpactError(f"Cannot read the impact input: {path}") from error


def _git_blob_oid(content: bytes, width: int) -> str:
    payload = f"blob {len(content)}\0".encode("ascii") + content
    if width == 40:
        return hashlib.sha1(payload).hexdigest()
    if width == 64:
        return hashlib.sha256(payload).hexdigest()
    raise ImpactError("A Git object ID has an invalid width.")


def _atomic_cache_write(path: Path, content: str, collision_error: str) -> None:
    """Write one cache file atomically and preserve content addressing."""

    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        try:
            if not path.is_symlink() and path.read_text(encoding="utf-8") == content:
                return
        except (OSError, UnicodeError):
            pass
        raise ImpactError(collision_error)
    temporary: Path | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
        )
        temporary = Path(temporary_name)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.link(temporary, path)
        except FileExistsError:
            try:
                same = not path.is_symlink() and path.read_text(
                    encoding="utf-8"
                ) == content
            except (OSError, UnicodeError):
                same = False
            if not same:
                raise ImpactError(collision_error)
        temporary.unlink()
        temporary = None
    except OSError as error:
        raise ImpactError(f"Cannot write the impact cache: {path}") from error
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError:
                pass


def _address(value: object) -> int:
    if isinstance(value, bool):
        raise ImpactError("A comparison address is invalid.")
    try:
        parsed = int(str(value), 16)
    except (TypeError, ValueError) as error:
        raise ImpactError("A comparison address is invalid.") from error
    if parsed < 0 or parsed > 0xFFFFFFFF:
        raise ImpactError("A comparison address is outside the 32-bit range.")
    return parsed


def format_address(value: int) -> str:
    return f"0x{value:08X}"


def _clean_id(value: object, label: str, *, max_length: int = 160) -> str:
    if not isinstance(value, str):
        raise ImpactError(f"{label} must be text.")
    clean = value.strip()
    if not clean or clean != value or "\n" in value or "\r" in value:
        raise ImpactError(f"{label} is invalid.")
    if len(clean) > max_length:
        raise ImpactError(f"{label} is too long.")
    return clean


def _cache_component(value: object, label: str) -> str:
    clean = _clean_id(value, label)
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,159}", clean) is None:
        raise ImpactError(f"{label} is not a safe cache name.")
    return clean


def _strict_keys(
    value: Mapping[str, object], expected: set[str], label: str
) -> None:
    actual = set(value)
    if actual != expected:
        extra = sorted(actual - expected)
        missing = sorted(expected - actual)
        detail = []
        if missing:
            detail.append("missing " + ", ".join(missing))
        if extra:
            detail.append("unexpected " + ", ".join(extra))
        raise ImpactError(f"{label} has invalid fields: {', '.join(detail)}.")


def _read_json(
    path: Path,
    label: str,
    *,
    max_bytes: int = MAX_DOCUMENT_BYTES,
) -> dict[str, object]:
    if path.is_symlink():
        raise ImpactError(f"The {label} cannot be a symbolic link.")
    try:
        size = path.stat().st_size
    except OSError as error:
        raise ImpactError(f"The {label} does not exist: {path}") from error
    if size <= 0 or size > max_bytes:
        raise ImpactError(f"The {label} size is invalid.")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ImpactError(f"The {label} is not valid JSON.") from error
    if not isinstance(value, dict):
        raise ImpactError(f"The {label} must contain a JSON object.")
    return value


def _diff_hash(value: object) -> str:
    return _json_hash(value)


def read_report(path: Path) -> dict[int, dict[str, object]]:
    """Read every comparison row without silently dropping malformed rows."""

    payload = _read_json(path, "comparison report", max_bytes=MAX_REPORT_BYTES)
    rows = payload.get("data")
    if not isinstance(rows, list):
        raise ImpactError("The comparison report has no data list.")
    result: dict[int, dict[str, object]] = {}
    for row in rows:
        if not isinstance(row, Mapping):
            raise ImpactError("A comparison row is not an object.")
        if row.get("address") is None or row.get("matching") is None:
            raise ImpactError("A comparison row has no address or score.")
        address = _address(row.get("address"))
        if address in result:
            raise ImpactError(f"The comparison report repeats {format_address(address)}.")
        matching = row.get("matching")
        if isinstance(matching, bool) or not isinstance(matching, (int, float)):
            raise ImpactError(f"The score for {format_address(address)} is invalid.")
        score = float(matching)
        if not math.isfinite(score) or score < 0.0 or score > 1.0:
            raise ImpactError(f"The score for {format_address(address)} is invalid.")
        effective_value = row.get("effective", False)
        if not isinstance(effective_value, bool):
            raise ImpactError(f"The effective state for {format_address(address)} is invalid.")
        result[address] = {
            "matching": score,
            "effective": effective_value,
            "exact": score == 1.0,
            "terminal": effective_value or score == 1.0,
            "stub": row.get("stub") is True,
            "name": str(row.get("name", "")),
            "diff_sha256": _diff_hash(row.get("diff")),
        }
    if not result:
        raise ImpactError("The comparison report has no function rows.")
    return result


def _effective_score(status: Mapping[str, object]) -> float:
    return 1.0 if status.get("effective") is True else float(status["matching"])


def _comparison_classification(
    before: Mapping[str, object] | None,
    after: Mapping[str, object] | None,
) -> str:
    """Classify one report row with the canonical promotion rules."""

    if before is None:
        return "added"
    if after is None:
        return "missing"
    before_score = _effective_score(before)
    after_score = _effective_score(after)
    terminal_lost = (
        before.get("terminal") is True and after.get("terminal") is not True
    )
    implementation_lost = (
        before.get("stub") is not True and after.get("stub") is True
    )
    if terminal_lost or implementation_lost or after_score + 1e-12 < before_score:
        return "regressed"
    if after_score > before_score + 1e-12:
        return "improved"
    if before.get("diff_sha256") != after.get("diff_sha256"):
        return "equal-score-diff-changed"
    if before != after:
        return "equal-score-state-changed"
    return "unchanged"


def compare_reports(
    baseline: Mapping[int, Mapping[str, object]],
    current: Mapping[int, Mapping[str, object]],
    targets: set[int],
) -> tuple[list[dict[str, object]], dict[str, object]]:
    """Return changed rows and a complete all-function regression oracle."""

    changes: list[dict[str, object]] = []
    regressions: list[str] = []
    equal_diff_changes: list[str] = []
    added: list[str] = []
    checked = 0
    for address in sorted(set(baseline) | set(current)):
        before = baseline.get(address)
        after = current.get(address)
        address_text = format_address(address)
        classification = _comparison_classification(before, after)
        regression = classification in {"missing", "regressed"}
        improvement = classification in {"added", "improved"}
        equal_diff = classification == "equal-score-diff-changed"
        if classification == "added":
            added.append(address_text)
        elif classification == "missing":
            regressions.append(address_text)
            checked += 1
        else:
            checked += 1
            if classification == "regressed":
                regressions.append(address_text)
            elif classification == "equal-score-diff-changed":
                equal_diff_changes.append(address_text)
        if classification == "unchanged" and address not in targets:
            continue
        changes.append(
            {
                "address": address_text,
                "target": address in targets,
                "classification": classification,
                "regression": regression,
                "improvement": improvement,
                "equal_score_diff_changed": equal_diff,
                "before": dict(before) if before is not None else None,
                "after": dict(after) if after is not None else None,
            }
        )
    oracle = {
        "complete": checked == len(baseline),
        "baseline_function_count": len(baseline),
        "current_function_count": len(current),
        "checked_function_count": checked,
        "regressions": regressions,
        "equal_score_diff_changes": equal_diff_changes,
        "added_functions": added,
        "passed": checked == len(baseline) and not regressions,
    }
    return changes, oracle


def _source_annotations(text: str) -> set[int]:
    result: set[int] = set()
    for _, line in active_source_lines(text):
        for kind, address, _tail in ANNOTATION_RE.findall(line):
            if kind in {"FUNCTION", "STUB", "LIBRARY"}:
                result.add(int(address, 16))
    return result


def _resolve_include(source: str, include: str, known: set[str]) -> str | None:
    parent = PurePosixPath(source).parent
    candidates = (
        (parent / include).as_posix(),
        (PurePosixPath("src") / include).as_posix(),
        PurePosixPath(include).as_posix(),
    )
    for candidate in candidates:
        normalized = PurePosixPath(candidate).as_posix()
        if normalized in known:
            return normalized
    return None


def _include_users(source_texts: Mapping[str, str]) -> dict[str, set[str]]:
    known = set(source_texts)
    reverse: dict[str, set[str]] = {}
    for source, text in source_texts.items():
        for raw in text.splitlines():
            found = _INCLUDE_RE.match(raw)
            if not found:
                continue
            target = _resolve_include(source, found.group(1), known)
            if target is not None:
                reverse.setdefault(target, set()).add(source)
    return reverse


def _header_translation_units(
    headers: set[str], source_texts: Mapping[str, str]
) -> set[str]:
    reverse = _include_users(source_texts)
    queue = deque(sorted(headers))
    visited = set(headers)
    units: set[str] = set()
    while queue:
        current = queue.popleft()
        for user in sorted(reverse.get(current, set())):
            suffix = PurePosixPath(user).suffix.lower()
            if suffix in SOURCE_SUFFIXES:
                units.add(user)
            elif user not in visited:
                visited.add(user)
                queue.append(user)
    return units


def compute_impact_scope(
    *,
    changed_paths: Sequence[str],
    source_texts: Mapping[str, str],
    baseline_source_texts: Mapping[str, str],
    all_functions: set[int],
    targets: set[int],
    graph: DependencyGraph | None,
    graph_error: str | None = None,
) -> dict[str, object]:
    """Compute a conservative impact scope from staged source and retail calls."""

    changed = sorted({PurePosixPath(path).as_posix() for path in changed_paths})
    translation_units: set[str] = set()
    headers: set[str] = set()
    gaps: list[dict[str, str]] = []
    for path in changed:
        suffix = PurePosixPath(path).suffix.lower()
        if path == "tools/Resources/functions_map.txt" or PurePosixPath(path).name == "CMakeLists.txt":
            gaps.append(
                {
                    "code": "global-build-input-changed",
                    "detail": f"{path} can affect all mapped functions.",
                }
            )
        elif suffix in SOURCE_SUFFIXES:
            translation_units.add(path)
        elif suffix in HEADER_SUFFIXES:
            headers.add(path)
        elif path.startswith("src/"):
            gaps.append(
                {
                    "code": "unknown-source-file-kind",
                    "detail": f"{path} has no supported source classification.",
                }
            )
        else:
            gaps.append(
                {
                    "code": "non-source-input-changed",
                    "detail": f"{path} can affect the generated program.",
                }
            )

    header_units = _header_translation_units(headers, source_texts)
    translation_units.update(header_units)
    for header in sorted(headers):
        if not _header_translation_units({header}, source_texts):
            gaps.append(
                {
                    "code": "header-has-no-known-include-user",
                    "detail": f"No staged translation unit includes {header}.",
                }
            )

    seed_functions = set(targets)
    for path in sorted(translation_units):
        current_text = source_texts.get(path, "")
        baseline_text = baseline_source_texts.get(path, "")
        annotations = _source_annotations(current_text) | _source_annotations(baseline_text)
        if not annotations:
            gaps.append(
                {
                    "code": "translation-unit-has-no-function-annotations",
                    "detail": f"{path} has no function annotation.",
                }
            )
        seed_functions.update(annotations)

    if not changed:
        gaps.append(
            {
                "code": "no-staged-paths",
                "detail": "The staged tree has no changed path.",
            }
        )
    if graph is None:
        gaps.append(
            {
                "code": "retail-call-graph-unavailable",
                "detail": graph_error or "The retail call graph is unavailable.",
            }
        )

    caller_rows: list[dict[str, object]] = []
    impacted = set(seed_functions)
    if graph is not None:
        queue: deque[tuple[int, int]] = deque((value, 0) for value in sorted(seed_functions))
        depths = {value: 0 for value in seed_functions}
        while queue:
            function, depth = queue.popleft()
            for caller in sorted(graph.callers.get(function, frozenset())):
                new_depth = depth + 1
                if caller in depths and depths[caller] <= new_depth:
                    continue
                depths[caller] = new_depth
                impacted.add(caller)
                queue.append((caller, new_depth))
                caller_rows.append(
                    {
                        "address": format_address(caller),
                        "calls": format_address(function),
                        "depth": new_depth,
                    }
                )
        uncertain = sorted(
            set(graph.indirect_calls) | set(graph.indirect_jumps)
        )
        if uncertain:
            gaps.append(
                {
                    "code": "indirect-control-flow-in-impact-scope",
                    "detail": "Mapped impact functions contain indirect control flow.",
                }
            )

    if not seed_functions:
        gaps.append(
            {
                "code": "no-impact-seed",
                "detail": "The changed files and targets provide no impact seed.",
            }
        )
    # Use the complete function universe for the promotion gate. The narrower
    # translation-unit and call-graph results remain review context only.
    impacted = set(all_functions)

    callee_context: list[dict[str, object]] = []
    if graph is not None:
        for caller in sorted(seed_functions):
            callee_context.append(
                {
                    "address": format_address(caller),
                    "direct_callees": [
                        format_address(value)
                        for value in sorted(graph.callees.get(caller, frozenset()))
                    ],
                    "indirect_calls": int(graph.indirect_calls.get(caller, 0)),
                    "indirect_jumps": int(graph.indirect_jumps.get(caller, 0)),
                }
            )

    return {
        "strategy": "all-functions-fallback",
        "changed_paths": changed,
        "changed_translation_units": sorted(translation_units),
        "changed_headers": sorted(headers),
        "header_translation_units": sorted(header_units),
        "seed_functions": [format_address(value) for value in sorted(seed_functions)],
        "caller_closure": sorted(caller_rows, key=lambda row: (int(row["depth"]), row["address"], row["calls"])),
        "callee_context": callee_context,
        "functions": [format_address(value) for value in sorted(impacted)],
        "all_function_count": len(all_functions),
        "coverage_complete": True,
        "context_complete": not gaps,
        "gaps": gaps,
    }


def promotion_checks(
    *,
    mode: str,
    targets: set[int],
    baseline: Mapping[int, Mapping[str, object]],
    current: Mapping[int, Mapping[str, object]],
    scope: Mapping[str, object],
    oracle: Mapping[str, object],
    typed_data_oracle: Mapping[str, object],
) -> list[dict[str, object]]:
    target_failures: list[str] = []
    for address in sorted(targets):
        before = baseline.get(address)
        after = current.get(address)
        address_text = format_address(address)
        if after is None:
            target_failures.append(address_text)
            continue
        acceptable = (
            float(after["matching"]) >= 0.5
            or after.get("effective") is True
            or after.get("exact") is True
        )
        if not acceptable:
            target_failures.append(address_text)
            continue
        if mode == "refinement" and before is not None:
            improved = _effective_score(after) > _effective_score(before) + 1e-12
            promoted = before.get("terminal") is not True and after.get("terminal") is True
            if not improved and not promoted:
                # Source-debt removal is checked by the standard validator.
                pass
    scope_functions = set(scope.get("functions", []))
    missing_targets = sorted(format_address(value) for value in targets if format_address(value) not in scope_functions)
    return [
        {
            "id": "target-output-contract",
            "status": "pass" if not target_failures else "fail",
            "subjects": target_failures,
        },
        {
            "id": "impact-scope-coverage",
            "status": "pass" if scope.get("coverage_complete") is True and not missing_targets else "fail",
            "subjects": missing_targets,
        },
        {
            "id": "all-function-regression-oracle",
            "status": "pass" if oracle.get("passed") is True else "fail",
            "subjects": list(oracle.get("regressions", [])),
        },
        {
            "id": "typed-data-regression-oracle",
            "status": "pass" if typed_data_oracle.get("passed") is True else "fail",
            "subjects": list(typed_data_oracle.get("problems", [])),
        },
    ]


def _typed_data_oracle(
    baseline: Mapping[str, object], current: Mapping[str, object]
) -> dict[str, object]:
    from tools.decomp_verify import typed_data_regression_problems

    problems = typed_data_regression_problems(baseline, current)
    return {
        "complete": not any(
            "missing" in problem or "invalid" in problem
            for problem in problems
        ),
        "checked_groups": list(TYPED_DATA_GROUPS),
        "problems": problems,
        "passed": not problems,
    }


def read_function_map(path: Path) -> list[tuple[int, str]]:
    entries: list[tuple[int, str]] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ImpactError(f"Cannot read the function map: {path}") from error
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(maxsplit=1)
        if not parts:
            continue
        address = _address(parts[0])
        name = parts[1] if len(parts) > 1 else ""
        entries.append((address, name))
    entries.sort()
    if not entries or len({address for address, _ in entries}) != len(entries):
        raise ImpactError("The function map is empty or has duplicate addresses.")
    return entries


def _git_output(root: Path, command: list[str], label: str) -> bytes:
    completed = subprocess.run(command, cwd=root, check=False, capture_output=True)
    if completed.returncode != 0:
        raise ImpactError(f"Cannot read {label} from Git.")
    return completed.stdout


def staged_paths(root: Path) -> list[str]:
    output = _git_output(
        root,
        ["git", "diff", "--cached", "--name-only", "-z"],
        "staged paths",
    )
    return sorted(
        value.decode("utf-8", errors="surrogateescape")
        for value in output.split(b"\0")
        if value
    )


def _repository_index_snapshot(root: Path) -> dict[str, list[str]]:
    output = _git_output(
        root,
        ["git", "ls-files", "--stage", "-z"],
        "repository index",
    )
    snapshot: dict[str, list[str]] = {}
    for raw in output.split(b"\0"):
        if not raw:
            continue
        if b"\t" not in raw:
            raise ImpactError("The repository index has an invalid entry.")
        metadata, encoded_path = raw.split(b"\t", 1)
        try:
            path = encoded_path.decode("utf-8")
            entry = metadata.decode("ascii")
        except UnicodeDecodeError as error:
            raise ImpactError("The repository index has a non-UTF-8 path.") from error
        snapshot.setdefault(path, []).append(entry)
    return {
        path: sorted(entries)
        for path, entries in sorted(snapshot.items())
    }


def _index_blob(
    entries: object,
    label: str,
) -> tuple[str, str] | None:
    if entries is None:
        return None
    if not isinstance(entries, list) or len(entries) != 1:
        raise ImpactError(f"{label} has an invalid index entry.")
    entry = entries[0]
    if not isinstance(entry, str):
        raise ImpactError(f"{label} has an invalid index entry.")
    match = re.fullmatch(r"(100644|100755) ([0-9a-f]{40,64}) 0", entry)
    if match is None:
        raise ImpactError(f"{label} is not a regular staged file.")
    return match.group(1), match.group(2)


def _git_blob(
    root: Path,
    revision: str,
    path: str,
    expected: tuple[str, str] | None,
    label: str,
) -> dict[str, object] | None:
    specification = f":{path}" if revision == ":" else f"{revision}:{path}"
    completed = subprocess.run(
        ["git", "rev-parse", "--verify", specification],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    if expected is None:
        if completed.returncode == 0:
            raise ImpactError(f"{label} exists outside its bound index snapshot.")
        return None
    if completed.returncode != 0:
        raise ImpactError(f"{label} is absent from Git.")
    object_id = completed.stdout.strip().lower()
    mode, expected_id = expected
    if object_id != expected_id:
        raise ImpactError(f"{label} does not match its bound index object.")
    content = _git_output(root, ["git", "show", specification], label)
    if len(content) > MAX_EVIDENCE_FILE_BYTES:
        raise ImpactError(f"{label} is too large for review.")
    try:
        text = content.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ImpactError(f"{label} is not UTF-8 text.") from error
    return {
        "git_mode": mode,
        "git_oid": object_id,
        "sha256": hashlib.sha256(content).hexdigest(),
        "byte_count": len(content),
        "line_count": len(content.splitlines()),
        "text": text,
    }


def _reviewable_changed_path(path: str) -> bool:
    relative = PurePosixPath(path)
    if relative.is_absolute() or ".." in relative.parts or relative.as_posix() != path:
        return False
    if path == "tools/Resources/functions_map.txt":
        return True
    if relative.name == "CMakeLists.txt" and "tools" not in relative.parts:
        return True
    return relative.parts[:1] == ("src",) and (
        relative.suffix.lower() in FINALIZED_SOURCE_SUFFIXES
    )


def _changed_input_evidence(
    root: Path,
    paths: Sequence[str],
    baseline_index: Mapping[str, object],
    current_index: Mapping[str, object],
) -> tuple[list[dict[str, object]], list[dict[str, object]], int]:
    bindings: list[dict[str, object]] = []
    evidence: list[dict[str, object]] = []
    total = 0
    canonical_paths = sorted(set(paths))
    if not canonical_paths or len(canonical_paths) > 256:
        raise ImpactError("The finalized changed-input set is invalid.")
    for path in canonical_paths:
        if not _reviewable_changed_path(path):
            raise ImpactError(f"The finalized path is not reviewable text: {path}")
        before_index = _index_blob(
            baseline_index.get(path), f"The prior {path} input"
        )
        current_index_entry = _index_blob(
            current_index.get(path), f"The staged {path} input"
        )
        before = _git_blob(root, "HEAD", path, before_index, f"The prior {path} input")
        current = _git_blob(root, ":", path, current_index_entry, f"The staged {path} input")
        if before_index == current_index_entry:
            raise ImpactError(f"The finalized path has no staged change: {path}")

        def descriptor(value: Mapping[str, object] | None) -> dict[str, object] | None:
            if value is None:
                return None
            return {key: value[key] for key in (
                "git_mode", "git_oid", "sha256", "byte_count", "line_count"
            )}

        bindings.append(
            {"path": path, "before": descriptor(before), "current": descriptor(current)}
        )
        evidence.append({"path": path, "before": before, "current": current})
        for value in (before, current):
            if value is not None:
                total += int(value["byte_count"])
    return bindings, evidence, total


def _git_source_texts(root: Path, revision: str) -> dict[str, str]:
    if revision == ":":
        output = _git_output(
            root,
            ["git", "ls-files", "-z", "--", "src"],
            "staged source paths",
        )
    else:
        output = _git_output(
            root,
            ["git", "ls-tree", "-r", "--name-only", "-z", revision, "--", "src"],
            f"{revision} source paths",
        )
    paths = [
        value.decode("utf-8", errors="surrogateescape")
        for value in output.split(b"\0")
        if value
        and PurePosixPath(value.decode("utf-8", errors="surrogateescape")).suffix.lower()
        in SOURCE_SUFFIXES | HEADER_SUFFIXES
    ]
    texts: dict[str, str] = {}
    for path in sorted(paths):
        specification = f":{path}" if revision == ":" else f"{revision}:{path}"
        contents = _git_output(root, ["git", "show", specification], path)
        texts[path] = contents.decode("utf-8", errors="ignore")
    return texts


def staged_source_texts(root: Path) -> dict[str, str]:
    return _git_source_texts(root, ":")


def baseline_source_texts(root: Path) -> dict[str, str]:
    return _git_source_texts(root, "HEAD")


def _brief_roles(
    brief_identities: Sequence[Mapping[str, object]],
) -> tuple[str, list[str], list[dict[str, object]]]:
    writer_ids: set[str] = set()
    scout_ids: set[str] = set()
    bindings: list[dict[str, object]] = []
    for identity in brief_identities:
        path_value = identity.get("path")
        saved_hash = identity.get("sha256")
        if not isinstance(path_value, str) or not isinstance(saved_hash, str):
            raise ImpactError("A target brief identity is invalid.")
        path = Path(path_value)
        if _file_hash(path) != saved_hash:
            raise ImpactError("A target brief changed after campaign start.")
        document = _read_json(path, "target brief")
        if document.get("content_sha256") != identity.get("content_sha256"):
            raise ImpactError("A target brief content identity changed.")
        roles = document.get("roles")
        writer = roles.get("writer") if isinstance(roles, Mapping) else None
        writer_id = writer.get("id") if isinstance(writer, Mapping) else None
        writer_ids.add(_clean_id(writer_id, "The campaign writer ID"))
        scouts = roles.get("scouts") if isinstance(roles, Mapping) else None
        if not isinstance(scouts, list):
            raise ImpactError("A target brief has no scout roles.")
        role_scout_ids = {
            _clean_id(
                scout.get("id") if isinstance(scout, Mapping) else None,
                "A scout ID",
            )
            for scout in scouts
        }
        if len(role_scout_ids) != 2:
            raise ImpactError("A target brief needs two distinct scout IDs.")
        scout_ids.update(role_scout_ids)
        bindings.append(
            {
                "target": identity.get("target"),
                "path": str(path.resolve()),
                "sha256": saved_hash,
                "content_sha256": identity.get("content_sha256"),
                "doctor_receipt": (
                    {
                        "path": identity["doctor_receipt"].get("path"),
                        "sha256": identity["doctor_receipt"].get("sha256"),
                    }
                    if isinstance(identity.get("doctor_receipt"), Mapping)
                    else None
                ),
                "scout_reports": [
                    {
                        "path": report.get("path"),
                        "sha256": report.get("sha256"),
                    }
                    for report in identity.get("scout_reports", [])
                    if isinstance(report, Mapping)
                ],
                "dwarf_input": identity.get("dwarf_input"),
            }
        )
    if len(writer_ids) != 1:
        raise ImpactError("The target briefs name different campaign writers.")
    writer_id = next(iter(writer_ids))
    if writer_id in scout_ids:
        raise ImpactError("The campaign writer ID matches a scout ID.")
    return writer_id, sorted(scout_ids), bindings


def _active_briefs(
    brief_identities: Sequence[Mapping[str, object]],
    ordered_targets: Sequence[int],
) -> list[Mapping[str, object]]:
    """Select one brief for each active target in active-target order."""

    selected: list[Mapping[str, object]] = []
    for target in ordered_targets:
        target_text = format_address(target)
        matches = [
            identity
            for identity in brief_identities
            if identity.get("target") == target_text
        ]
        if not matches:
            raise ImpactError(
                f"The active target {target_text} has no bound brief."
            )
        selected.append(matches[-1])
    return selected


def build_impact_pack(
    *,
    state: Mapping[str, object],
    baseline_report: Path,
    current_report: Path,
    root: Path,
    changed_paths: Sequence[str] | None = None,
    source_texts: Mapping[str, str] | None = None,
    baseline_texts: Mapping[str, str] | None = None,
    graph: DependencyGraph | None = None,
    graph_error: str | None = None,
) -> dict[str, object]:
    """Build one deterministic pack from the staged tree and comparison reports."""

    mode = state.get("mode")
    if mode not in {"coverage", "refinement"}:
        raise ImpactError("An impact pack requires coverage or refinement mode.")
    targets_value = state.get("active_addresses", state.get("addresses"))
    if not isinstance(targets_value, list) or not targets_value:
        raise ImpactError("The source campaign has no active target.")
    ordered_targets = [_address(value) for value in targets_value]
    if len(ordered_targets) != len(set(ordered_targets)):
        raise ImpactError("The source campaign repeats an active target.")
    targets = set(ordered_targets)
    baseline = read_report(baseline_report)
    current = read_report(current_report)
    baseline_data_report = Path(str(state.get("baseline_data_report", "")))
    current_data_report = root / "build" / "decomp-current-data-report.json"
    baseline_data = _read_json(
        baseline_data_report,
        "baseline typed-data report",
        max_bytes=MAX_REPORT_BYTES,
    )
    current_data = _read_json(
        current_data_report,
        "current typed-data report",
        max_bytes=MAX_REPORT_BYTES,
    )
    typed_data_oracle = _typed_data_oracle(baseline_data, current_data)
    map_path = root / "tools" / "Resources" / "functions_map.txt"
    entries = read_function_map(map_path)
    if graph is None and graph_error is None:
        try:
            graph = build_call_graph(
                entries, image_path=root / "original" / "toy2.exe"
            )
        except DependencyUnavailable as error:
            graph_error = str(error)
        except Exception as error:  # The pack records a conservative fallback.
            graph_error = f"The retail call graph failed: {error}"
    current_texts = dict(source_texts) if source_texts is not None else staged_source_texts(root)
    old_texts = dict(baseline_texts) if baseline_texts is not None else baseline_source_texts(root)
    paths = list(changed_paths) if changed_paths is not None else staged_paths(root)
    all_functions = set(baseline) | set(current) | {address for address, _ in entries}
    scope = compute_impact_scope(
        changed_paths=paths,
        source_texts=current_texts,
        baseline_source_texts=old_texts,
        all_functions=all_functions,
        targets=targets,
        graph=graph,
        graph_error=graph_error,
    )
    changes, oracle = compare_reports(baseline, current, targets)
    checks = promotion_checks(
        mode=str(mode),
        targets=targets,
        baseline=baseline,
        current=current,
        scope=scope,
        oracle=oracle,
        typed_data_oracle=typed_data_oracle,
    )
    if any(check["status"] != "pass" for check in checks):
        raise ImpactError("The automatic impact promotion checks failed.")
    briefs = state.get("briefs")
    if not isinstance(briefs, list) or not briefs:
        raise ImpactError("The source campaign has no target brief.")
    if any(not isinstance(brief, Mapping) for brief in briefs):
        raise ImpactError("A target brief identity is invalid.")
    active_briefs = _active_briefs(briefs, ordered_targets)
    writer_id, scout_ids, brief_bindings = _brief_roles(active_briefs)
    index_snapshot = state.get("repository_index")
    if not isinstance(index_snapshot, Mapping):
        raise ImpactError("The source campaign has no repository index snapshot.")
    current_index = _repository_index_snapshot(root)
    from tools.decomp_provenance import provenance_path

    current_provenance = provenance_path(current_report)
    if not current_provenance.is_file():
        raise ImpactError("The current report has no provenance sidecar.")
    campaign_id = _cache_component(state.get("campaign_id"), "The campaign ID")
    changed_input_bindings, changed_review_evidence, evidence_bytes = (
        _changed_input_evidence(root, paths, index_snapshot, current_index)
    )
    brief_review_evidence = []
    for brief in brief_bindings:
        brief_path = Path(str(brief["path"]))
        raw = brief_path.read_bytes()
        if len(raw) > MAX_EVIDENCE_FILE_BYTES:
            raise ImpactError(
                f"The target brief is too large for review: {brief['target']}"
            )
        try:
            brief_text = raw.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ImpactError("A target brief is not UTF-8.") from error
        evidence_bytes += len(raw)
        brief_review_evidence.append(
            {
                "target": brief["target"],
                "path": brief["path"],
                "sha256": brief["sha256"],
                "text": brief_text,
            }
        )
    if evidence_bytes > MAX_EVIDENCE_BYTES:
        raise ImpactError("The embedded review evidence is too large.")
    pack: dict[str, object] = {
        "schema": IMPACT_SCHEMA,
        "kind": IMPACT_KIND,
        "campaign_id": campaign_id,
        "mode": mode,
        "lane": state.get("lane"),
        "targets": [format_address(value) for value in ordered_targets],
        "writer_id": writer_id,
        "scout_ids": scout_ids,
        "bindings": {
            "campaign_head": state.get("campaign_head"),
            "baseline_report": {
                "path": str(baseline_report.resolve()),
                "sha256": _file_hash(baseline_report),
                "provenance_sha256": state.get("baseline_report_provenance_sha256"),
            },
            "current_report": {
                "path": str(current_report.resolve()),
                "sha256": _file_hash(current_report),
                "provenance_sha256": _file_hash(current_provenance),
            },
            "baseline_data_report": {
                "path": str(baseline_data_report.resolve()),
                "sha256": _file_hash(baseline_data_report),
                "provenance_sha256": state.get(
                    "baseline_data_report_provenance_sha256"
                ),
            },
            "current_data_report": {
                "path": str(current_data_report.resolve()),
                "sha256": _file_hash(current_data_report),
                "provenance_sha256": _file_hash(
                    provenance_path(current_data_report)
                ),
            },
            "function_map": {
                "path": str(map_path.resolve()),
                "sha256": _file_hash(map_path),
            },
            "retail_image": {
                "path": str((root / "original" / "toy2.exe").resolve()),
                "sha256": _file_hash(root / "original" / "toy2.exe"),
            },
            "campaign_repository_index_sha256": _json_hash(index_snapshot),
            "repository_index_sha256": _json_hash(current_index),
            "changed_inputs": changed_input_bindings,
            "tool": {
                "path": str(Path(__file__).resolve()),
                "sha256": _file_hash(Path(__file__).resolve()),
            },
            "dependencies_tool": {
                "path": str(Path(decomp_dependencies.__file__).resolve()),
                "sha256": _file_hash(Path(decomp_dependencies.__file__).resolve()),
            },
            "binary_tool": {
                "path": str(Path(decomp_binary.__file__).resolve()),
                "sha256": _file_hash(Path(decomp_binary.__file__).resolve()),
            },
            "annotations_tool": {
                "path": str(Path(__file__).with_name("decomp_annotations.py").resolve()),
                "sha256": _file_hash(
                    Path(__file__).with_name("decomp_annotations.py").resolve()
                ),
            },
            "verify_tool": {
                "path": str(Path(__file__).with_name("decomp_verify.py").resolve()),
                "sha256": _file_hash(
                    Path(__file__).with_name("decomp_verify.py").resolve()
                ),
            },
            "decoder": decoder_identity(),
            "briefs": brief_bindings,
        },
        "scope": scope,
        "comparison_changes": changes,
        "regression_oracle": oracle,
        "typed_data_oracle": typed_data_oracle,
        "review_evidence": {
            "changed_inputs": changed_review_evidence,
            "briefs": brief_review_evidence,
            "total_bytes": evidence_bytes,
        },
        "promotion_checks": checks,
        "required_claims": [
            {"id": claim_id, "claim": claim}
            for claim_id, claim in PROMOTION_CLAIMS.items()
        ],
        "required_refuter_checks": list(REFUTER_CHECKS),
    }
    pack["content_sha256"] = _document_hash(pack)
    validate_impact_pack(pack)
    return pack


_SHA256_RE = re.compile(r"[0-9a-f]{64}")
_COMMIT_RE = re.compile(r"[0-9a-f]{40,64}")


def _sha256(value: object, label: str) -> str:
    if not isinstance(value, str) or _SHA256_RE.fullmatch(value) is None:
        raise ImpactError(f"{label} is not a SHA-256 value.")
    return value


def _canonical_path(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ImpactError(f"{label} is invalid.")
    path = Path(value)
    if not path.is_absolute() or str(path.resolve()) != value:
        raise ImpactError(f"{label} is not a canonical absolute path.")
    return value


def _canonical_addresses(
    value: object,
    label: str,
    *,
    allow_empty: bool = True,
    maximum: int = 10000,
) -> list[str]:
    if not isinstance(value, list) or len(value) > maximum:
        raise ImpactError(f"{label} is invalid.")
    addresses: list[str] = []
    for item in value:
        if not isinstance(item, str):
            raise ImpactError(f"{label} is invalid.")
        parsed = _address(item)
        if item != format_address(parsed):
            raise ImpactError(f"{label} has a noncanonical address.")
        addresses.append(item)
    if (not allow_empty and not addresses) or addresses != sorted(set(addresses)):
        raise ImpactError(f"{label} is not a sorted unique list.")
    return addresses


def _ordered_addresses(
    value: object,
    label: str,
    *,
    allow_empty: bool = True,
    maximum: int = 64,
) -> list[str]:
    if not isinstance(value, list) or len(value) > maximum:
        raise ImpactError(f"{label} is invalid.")
    addresses: list[str] = []
    for item in value:
        if not isinstance(item, str) or item != format_address(_address(item)):
            raise ImpactError(f"{label} has a noncanonical address.")
        addresses.append(item)
    if (not allow_empty and not addresses) or len(addresses) != len(set(addresses)):
        raise ImpactError(f"{label} is not a unique ordered list.")
    return addresses


def _string_list(
    value: object, label: str, *, maximum: int = 10000
) -> list[str]:
    if not isinstance(value, list) or len(value) > maximum:
        raise ImpactError(f"{label} is invalid.")
    result = [_clean_id(item, label, max_length=4096) for item in value]
    if result != sorted(set(result)):
        raise ImpactError(f"{label} is not a sorted unique list.")
    return result


def _path_list(value: object, label: str, *, maximum: int = 256) -> list[str]:
    paths = _string_list(value, label, maximum=maximum)
    for item in paths:
        path = PurePosixPath(item)
        if path.is_absolute() or ".." in path.parts or path.as_posix() != item:
            raise ImpactError(f"{label} has a noncanonical repository path.")
    return paths


def _file_binding(
    value: object,
    label: str,
    *,
    provenance: bool = False,
) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise ImpactError(f"{label} is invalid.")
    fields = {"path", "sha256"}
    if provenance:
        fields.add("provenance_sha256")
    _strict_keys(value, fields, label)
    _canonical_path(value.get("path"), f"{label} path")
    _sha256(value.get("sha256"), f"{label} hash")
    if provenance:
        _sha256(value.get("provenance_sha256"), f"{label} provenance hash")
    return dict(value)


def _input_content_binding(
    value: object,
    label: str,
) -> dict[str, object] | None:
    if value is None:
        return None
    if not isinstance(value, Mapping):
        raise ImpactError(f"{label} is invalid.")
    _strict_keys(
        value,
        {"git_mode", "git_oid", "sha256", "byte_count", "line_count"},
        label,
    )
    if value.get("git_mode") not in {"100644", "100755"}:
        raise ImpactError(f"{label} has an invalid Git mode.")
    object_id = value.get("git_oid")
    if not isinstance(object_id, str) or _COMMIT_RE.fullmatch(object_id) is None:
        raise ImpactError(f"{label} has an invalid Git object ID.")
    _sha256(value.get("sha256"), f"{label} content hash")
    for field in ("byte_count", "line_count"):
        count = value.get(field)
        if isinstance(count, bool) or not isinstance(count, int) or count < 0:
            raise ImpactError(f"{label} has an invalid {field}.")
    if int(value["byte_count"]) > MAX_EVIDENCE_FILE_BYTES:
        raise ImpactError(f"{label} is too large.")
    return dict(value)


def _status_snapshot(value: object, label: str) -> dict[str, object] | None:
    if value is None:
        return None
    if not isinstance(value, Mapping):
        raise ImpactError(f"{label} is invalid.")
    _strict_keys(
        value,
        {
            "matching",
            "effective",
            "exact",
            "terminal",
            "stub",
            "name",
            "diff_sha256",
        },
        label,
    )
    matching = value.get("matching")
    if (
        isinstance(matching, bool)
        or not isinstance(matching, (int, float))
        or not math.isfinite(float(matching))
        or not 0.0 <= float(matching) <= 1.0
    ):
        raise ImpactError(f"{label} has an invalid score.")
    for field in ("effective", "exact", "terminal", "stub"):
        if not isinstance(value.get(field), bool):
            raise ImpactError(f"{label} has an invalid {field} state.")
    exact = float(matching) == 1.0
    if value.get("exact") is not exact:
        raise ImpactError(f"{label} has an inconsistent exact state.")
    if value.get("terminal") is not (exact or value.get("effective") is True):
        raise ImpactError(f"{label} has an inconsistent terminal state.")
    if not isinstance(value.get("name"), str) or len(str(value.get("name"))) > 4096:
        raise ImpactError(f"{label} has an invalid name.")
    _sha256(value.get("diff_sha256"), f"{label} diff hash")
    return dict(value)


def _validate_decoder_identity(value: object) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise ImpactError("The impact decoder identity is invalid.")
    _strict_keys(
        value,
        {
            "engine",
            "version",
            "api_version",
            "architecture",
            "mode",
            "detail",
            "module",
            "library",
        },
        "The impact decoder identity",
    )
    if value.get("architecture") != "x86" or value.get("mode") != 32:
        raise ImpactError("The impact decoder architecture is invalid.")
    engine = value.get("engine")
    if engine == "unavailable":
        if (
            value.get("version") is not None
            or value.get("api_version") is not None
            or value.get("detail") is not False
            or value.get("module") != {"path": None, "sha256": None}
            or value.get("library") != {"path": None, "sha256": None}
        ):
            raise ImpactError("The unavailable decoder identity is invalid.")
        return dict(value)
    if engine != "capstone" or value.get("detail") is not True:
        raise ImpactError("The impact decoder engine is invalid.")
    _clean_id(value.get("version"), "The impact decoder version")
    api_version = value.get("api_version")
    if (
        not isinstance(api_version, list)
        or len(api_version) != 3
        or any(isinstance(item, bool) or not isinstance(item, int) for item in api_version)
    ):
        raise ImpactError("The impact decoder API version is invalid.")
    _file_binding(value.get("module"), "The impact decoder module")
    _file_binding(value.get("library"), "The impact decoder library")
    return dict(value)


def _validate_bindings(
    bindings: object, targets: list[str]
) -> None:
    if not isinstance(bindings, Mapping):
        raise ImpactError("The impact bindings are invalid.")
    _strict_keys(
        bindings,
        {
            "campaign_head",
            "baseline_report",
            "current_report",
            "baseline_data_report",
            "current_data_report",
            "function_map",
            "retail_image",
            "campaign_repository_index_sha256",
            "repository_index_sha256",
            "changed_inputs",
            "tool",
            "dependencies_tool",
            "binary_tool",
            "annotations_tool",
            "verify_tool",
            "decoder",
            "briefs",
        },
        "The impact bindings",
    )
    head = bindings.get("campaign_head")
    if not isinstance(head, str) or _COMMIT_RE.fullmatch(head) is None:
        raise ImpactError("The impact campaign HEAD is invalid.")
    _file_binding(bindings.get("baseline_report"), "The baseline report", provenance=True)
    _file_binding(bindings.get("current_report"), "The current report", provenance=True)
    _file_binding(
        bindings.get("baseline_data_report"),
        "The baseline typed-data report",
        provenance=True,
    )
    _file_binding(
        bindings.get("current_data_report"),
        "The current typed-data report",
        provenance=True,
    )
    _file_binding(bindings.get("function_map"), "The function map")
    _file_binding(bindings.get("retail_image"), "The retail image")
    _file_binding(bindings.get("tool"), "The impact tool")
    _file_binding(bindings.get("dependencies_tool"), "The dependency tool")
    _file_binding(bindings.get("binary_tool"), "The binary tool")
    _file_binding(bindings.get("annotations_tool"), "The annotation tool")
    _file_binding(bindings.get("verify_tool"), "The verification tool")
    _validate_decoder_identity(bindings.get("decoder"))
    _sha256(
        bindings.get("campaign_repository_index_sha256"),
        "The campaign repository index hash",
    )
    _sha256(
        bindings.get("repository_index_sha256"),
        "The finalized repository index hash",
    )
    changed_inputs = bindings.get("changed_inputs")
    if not isinstance(changed_inputs, list) or not 1 <= len(changed_inputs) <= 256:
        raise ImpactError("The changed-input bindings are invalid.")
    changed_paths: list[str] = []
    for changed in changed_inputs:
        if not isinstance(changed, Mapping):
            raise ImpactError("A changed-input binding is invalid.")
        _strict_keys(
            changed,
            {"path", "before", "current"},
            "A changed-input binding",
        )
        path = _path_list([changed.get("path")], "A changed-input path")[0]
        if not _reviewable_changed_path(path):
            raise ImpactError("A changed-input path is outside source finalization.")
        changed_paths.append(path)
        before = _input_content_binding(
            changed.get("before"), "The prior changed-input content"
        )
        current = _input_content_binding(
            changed.get("current"), "The staged changed-input content"
        )
        if (before is None and current is None) or before == current:
            raise ImpactError("A changed-input binding has no content change.")
    if changed_paths != sorted(set(changed_paths)):
        raise ImpactError("The changed-input bindings are not canonical.")
    briefs = bindings.get("briefs")
    if not isinstance(briefs, list) or not 1 <= len(briefs) <= 64:
        raise ImpactError("The impact brief bindings are invalid.")
    brief_targets: list[str] = []
    for brief in briefs:
        if not isinstance(brief, Mapping):
            raise ImpactError("An impact brief binding is invalid.")
        _strict_keys(
            brief,
            {
                "target",
                "path",
                "sha256",
                "content_sha256",
                "doctor_receipt",
                "scout_reports",
                "dwarf_input",
            },
            "An impact brief binding",
        )
        target = _canonical_addresses(
            [brief.get("target")], "An impact brief target", allow_empty=False
        )[0]
        brief_targets.append(target)
        _canonical_path(brief.get("path"), "The target brief path")
        _sha256(brief.get("sha256"), "The target brief file hash")
        _sha256(brief.get("content_sha256"), "The target brief content hash")
        _file_binding(brief.get("doctor_receipt"), "The doctor receipt")
        _file_binding(brief.get("dwarf_input"), "The DWARF input")
        reports = brief.get("scout_reports")
        if not isinstance(reports, list) or len(reports) != 2:
            raise ImpactError("A target brief needs two scout report bindings.")
        for report in reports:
            _file_binding(report, "A scout report")
    if brief_targets != targets or len(set(brief_targets)) != len(targets):
        raise ImpactError("The impact brief targets do not match the campaign targets.")


def _validate_review_evidence(
    evidence: object,
    bindings: Mapping[str, object],
) -> None:
    if not isinstance(evidence, Mapping):
        raise ImpactError("The embedded review evidence is invalid.")
    _strict_keys(
        evidence,
        {"changed_inputs", "briefs", "total_bytes"},
        "The embedded review evidence",
    )
    total = 0
    changed_inputs = evidence.get("changed_inputs")
    bound_inputs = bindings.get("changed_inputs")
    if not isinstance(changed_inputs, list) or not isinstance(bound_inputs, list):
        raise ImpactError("The changed-input review evidence is invalid.")
    if len(changed_inputs) != len(bound_inputs):
        raise ImpactError("The changed-input review evidence is incomplete.")
    for row, bound in zip(changed_inputs, bound_inputs, strict=True):
        if not isinstance(row, Mapping) or not isinstance(bound, Mapping):
            raise ImpactError("A changed-input review evidence row is invalid.")
        _strict_keys(
            row,
            {"path", "before", "current"},
            "A changed-input review evidence row",
        )
        if row.get("path") != bound.get("path"):
            raise ImpactError("A changed-input review path conflicts with its binding.")
        for field in ("before", "current"):
            content = row.get(field)
            bound_content = bound.get(field)
            if content is None:
                if bound_content is not None:
                    raise ImpactError("Changed-input review evidence is incomplete.")
                continue
            if not isinstance(content, Mapping):
                raise ImpactError("A changed-input review content row is invalid.")
            _strict_keys(
                content,
                {
                    "git_mode",
                    "git_oid",
                    "sha256",
                    "byte_count",
                    "line_count",
                    "text",
                },
                "A changed-input review content row",
            )
            text = content.get("text")
            if not isinstance(text, str):
                raise ImpactError("A changed-input review text is invalid.")
            raw = text.encode("utf-8")
            total += len(raw)
            descriptor = {
                key: content.get(key)
                for key in (
                    "git_mode",
                    "git_oid",
                    "sha256",
                    "byte_count",
                    "line_count",
                )
            }
            if descriptor != bound_content:
                raise ImpactError(
                    "A changed-input review row conflicts with its binding."
                )
            if (
                hashlib.sha256(raw).hexdigest() != content.get("sha256")
                or len(raw) != content.get("byte_count")
                or len(raw.splitlines()) != content.get("line_count")
            ):
                raise ImpactError("A changed-input review content hash is invalid.")
            if (
                _git_blob_oid(raw, len(str(content.get("git_oid"))))
                != content.get("git_oid")
            ):
                raise ImpactError(
                    "A changed-input review Git object does not match its text."
                )
    briefs = evidence.get("briefs")
    bound_briefs = bindings.get("briefs")
    if not isinstance(briefs, list) or not isinstance(bound_briefs, list):
        raise ImpactError("The brief review evidence is invalid.")
    if len(briefs) != len(bound_briefs):
        raise ImpactError("The brief review evidence is incomplete.")
    for row, bound in zip(briefs, bound_briefs, strict=True):
        if not isinstance(row, Mapping) or not isinstance(bound, Mapping):
            raise ImpactError("A brief review evidence row is invalid.")
        _strict_keys(
            row,
            {"target", "path", "sha256", "text"},
            "A brief review evidence row",
        )
        text = row.get("text")
        if not isinstance(text, str):
            raise ImpactError("A brief review evidence text is invalid.")
        raw = text.encode("utf-8")
        total += len(raw)
        if len(raw) > MAX_EVIDENCE_FILE_BYTES or (
            row.get("target") != bound.get("target")
            or row.get("path") != bound.get("path")
            or row.get("sha256") != bound.get("sha256")
            or hashlib.sha256(raw).hexdigest() != bound.get("sha256")
        ):
            raise ImpactError("A brief review evidence row conflicts with its binding.")
        try:
            document = json.loads(text)
        except json.JSONDecodeError as error:
            raise ImpactError("An embedded target brief is not valid JSON.") from error
        if not isinstance(document, Mapping) or document.get(
            "content_sha256"
        ) != bound.get("content_sha256"):
            raise ImpactError("An embedded target brief has the wrong identity.")
    saved_total = evidence.get("total_bytes")
    if (
        isinstance(saved_total, bool)
        or not isinstance(saved_total, int)
        or saved_total != total
        or total > MAX_EVIDENCE_BYTES
    ):
        raise ImpactError("The embedded review evidence size is invalid.")


def _embedded_review_roles(
    evidence: Mapping[str, object],
) -> tuple[str, list[str]]:
    briefs = evidence.get("briefs")
    if not isinstance(briefs, list):
        raise ImpactError("The embedded brief evidence is invalid.")
    writers: set[str] = set()
    scouts: set[str] = set()
    for row in briefs:
        if not isinstance(row, Mapping) or not isinstance(row.get("text"), str):
            raise ImpactError("An embedded brief evidence row is invalid.")
        try:
            document = json.loads(str(row["text"]))
        except json.JSONDecodeError as error:
            raise ImpactError("An embedded target brief is not valid JSON.") from error
        roles = document.get("roles") if isinstance(document, Mapping) else None
        writer = roles.get("writer") if isinstance(roles, Mapping) else None
        writers.add(
            _clean_id(
                writer.get("id") if isinstance(writer, Mapping) else None,
                "The embedded campaign writer ID",
            )
        )
        scout_rows = roles.get("scouts") if isinstance(roles, Mapping) else None
        if not isinstance(scout_rows, list) or len(scout_rows) != 2:
            raise ImpactError("An embedded target brief needs two scout roles.")
        brief_scouts = {
            _clean_id(
                scout.get("id") if isinstance(scout, Mapping) else None,
                "An embedded scout ID",
            )
            for scout in scout_rows
        }
        if len(brief_scouts) != 2:
            raise ImpactError("An embedded target brief repeats a scout role.")
        scouts.update(brief_scouts)
    if len(writers) != 1:
        raise ImpactError("The embedded target briefs name different writers.")
    return next(iter(writers)), sorted(scouts)


def _validate_scope(scope: object, targets: list[str]) -> None:
    if not isinstance(scope, Mapping):
        raise ImpactError("The impact scope is invalid.")
    _strict_keys(
        scope,
        {
            "strategy",
            "changed_paths",
            "changed_translation_units",
            "changed_headers",
            "header_translation_units",
            "seed_functions",
            "caller_closure",
            "callee_context",
            "functions",
            "all_function_count",
            "coverage_complete",
            "context_complete",
            "gaps",
        },
        "The impact scope",
    )
    strategy = scope.get("strategy")
    if strategy != "all-functions-fallback":
        raise ImpactError("The impact scope strategy is invalid.")
    changed_paths = _path_list(scope.get("changed_paths"), "The changed paths")
    if not changed_paths:
        raise ImpactError("The impact scope has no changed path.")
    translation_units = _path_list(
        scope.get("changed_translation_units"), "The changed translation units"
    )
    headers = _path_list(scope.get("changed_headers"), "The changed headers")
    header_units = _path_list(
        scope.get("header_translation_units"), "The header translation units"
    )
    if not set(header_units).issubset(translation_units):
        raise ImpactError("A header translation unit is outside the impact scope.")
    if not set(headers).issubset(changed_paths):
        raise ImpactError("A changed header is not a changed path.")
    seeds = _canonical_addresses(scope.get("seed_functions"), "The impact seeds")
    functions = _canonical_addresses(
        scope.get("functions"), "The impacted functions", allow_empty=False
    )
    if not set(targets).issubset(functions) or not set(seeds).issubset(functions):
        raise ImpactError("The impact scope omits a target or seed function.")
    all_count = scope.get("all_function_count")
    if isinstance(all_count, bool) or not isinstance(all_count, int) or all_count < len(functions):
        raise ImpactError("The impact all-function count is invalid.")
    if all_count != len(functions):
        raise ImpactError("The all-function fallback is not complete.")
    if scope.get("coverage_complete") is not True:
        raise ImpactError("The impact scope coverage is not complete.")
    callers = scope.get("caller_closure")
    if not isinstance(callers, list) or len(callers) > 10000:
        raise ImpactError("The impact caller closure is invalid.")
    normalized_callers: list[tuple[int, str, str]] = []
    for row in callers:
        if not isinstance(row, Mapping):
            raise ImpactError("An impact caller row is invalid.")
        _strict_keys(row, {"address", "calls", "depth"}, "An impact caller row")
        address = _canonical_addresses([row.get("address")], "An impact caller address", allow_empty=False)[0]
        calls = _canonical_addresses([row.get("calls")], "An impact callee address", allow_empty=False)[0]
        depth = row.get("depth")
        if isinstance(depth, bool) or not isinstance(depth, int) or depth <= 0:
            raise ImpactError("An impact caller depth is invalid.")
        if address not in functions:
            raise ImpactError("An impact caller is outside the impact scope.")
        normalized_callers.append((depth, address, calls))
    if normalized_callers != sorted(set(normalized_callers)):
        raise ImpactError("The impact caller closure is not canonical.")
    callees = scope.get("callee_context")
    if not isinstance(callees, list) or len(callees) > 10000:
        raise ImpactError("The impact callee context is invalid.")
    callee_addresses: list[str] = []
    for row in callees:
        if not isinstance(row, Mapping):
            raise ImpactError("An impact callee row is invalid.")
        _strict_keys(
            row,
            {"address", "direct_callees", "indirect_calls", "indirect_jumps"},
            "An impact callee row",
        )
        address = _canonical_addresses([row.get("address")], "An impact context address", allow_empty=False)[0]
        callee_addresses.append(address)
        _canonical_addresses(row.get("direct_callees"), "The direct callees")
        for field in ("indirect_calls", "indirect_jumps"):
            count = row.get(field)
            if isinstance(count, bool) or not isinstance(count, int) or count < 0:
                raise ImpactError(f"The impact {field} count is invalid.")
    gaps = scope.get("gaps")
    if not isinstance(gaps, list) or len(gaps) > 256:
        raise ImpactError("The impact completeness gaps are invalid.")
    for gap in gaps:
        if not isinstance(gap, Mapping):
            raise ImpactError("An impact completeness gap is invalid.")
        _strict_keys(gap, {"code", "detail"}, "An impact completeness gap")
        _clean_id(gap.get("code"), "The gap code")
        _clean_id(gap.get("detail"), "The gap detail", max_length=4096)
    gap_codes = {
        str(gap.get("code")) for gap in gaps if isinstance(gap, Mapping)
    }
    expected_callees = (
        set()
        if "retail-call-graph-unavailable" in gap_codes
        else set(seeds)
    )
    if (
        callee_addresses != sorted(set(callee_addresses))
        or set(callee_addresses) != expected_callees
    ):
        raise ImpactError("The impact callee context does not match its seeds.")
    if scope.get("context_complete") is not (not gaps):
        raise ImpactError("The impact context completeness state is invalid.")


def _validate_oracle(oracle: object) -> None:
    if not isinstance(oracle, Mapping):
        raise ImpactError("The regression oracle is invalid.")
    _strict_keys(
        oracle,
        {
            "complete",
            "baseline_function_count",
            "current_function_count",
            "checked_function_count",
            "regressions",
            "equal_score_diff_changes",
            "added_functions",
            "passed",
        },
        "The regression oracle",
    )
    counts: dict[str, int] = {}
    for field in (
        "baseline_function_count",
        "current_function_count",
        "checked_function_count",
    ):
        value = oracle.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise ImpactError(f"The oracle {field} is invalid.")
        counts[field] = value
    regressions = _canonical_addresses(oracle.get("regressions"), "The oracle regressions")
    _canonical_addresses(
        oracle.get("equal_score_diff_changes"), "The equal-score diff changes"
    )
    _canonical_addresses(oracle.get("added_functions"), "The added functions")
    complete = oracle.get("complete") is True
    if complete != (counts["checked_function_count"] == counts["baseline_function_count"]):
        raise ImpactError("The regression oracle completeness count is invalid.")
    if oracle.get("passed") is not (complete and not regressions):
        raise ImpactError("The regression oracle pass state is invalid.")
    if oracle.get("passed") is not True:
        raise ImpactError("The regression oracle did not pass.")


def _validate_typed_data_oracle(oracle: object) -> None:
    if not isinstance(oracle, Mapping):
        raise ImpactError("The typed-data regression oracle is invalid.")
    _strict_keys(
        oracle,
        {"complete", "checked_groups", "problems", "passed"},
        "The typed-data regression oracle",
    )
    if (
        oracle.get("complete") is not True
        or oracle.get("checked_groups") != list(TYPED_DATA_GROUPS)
        or oracle.get("problems") != []
        or oracle.get("passed") is not True
    ):
        raise ImpactError("The typed-data regression oracle did not pass.")


def _validate_changes(
    changes: object,
    targets: list[str],
    oracle: Mapping[str, object],
) -> None:
    if not isinstance(changes, list) or len(changes) > 10000:
        raise ImpactError("The comparison change list is invalid.")
    classifications = {
        "added",
        "missing",
        "regressed",
        "improved",
        "equal-score-diff-changed",
        "equal-score-state-changed",
        "unchanged",
    }
    addresses: list[str] = []
    regression_rows: list[str] = []
    equal_rows: list[str] = []
    added_rows: list[str] = []
    target_rows: list[str] = []
    for row in changes:
        if not isinstance(row, Mapping):
            raise ImpactError("A comparison change is invalid.")
        _strict_keys(
            row,
            {
                "address",
                "target",
                "classification",
                "regression",
                "improvement",
                "equal_score_diff_changed",
                "before",
                "after",
            },
            "A comparison change",
        )
        address = _canonical_addresses([row.get("address")], "A comparison change address", allow_empty=False)[0]
        addresses.append(address)
        classification = row.get("classification")
        if classification not in classifications:
            raise ImpactError("A comparison change classification is invalid.")
        for field in ("target", "regression", "improvement", "equal_score_diff_changed"):
            if not isinstance(row.get(field), bool):
                raise ImpactError(f"A comparison change {field} state is invalid.")
        before = _status_snapshot(row.get("before"), "The prior comparison state")
        after = _status_snapshot(row.get("after"), "The current comparison state")
        expected_classification = _comparison_classification(before, after)
        if classification != expected_classification:
            raise ImpactError("A comparison change has an incorrect classification.")
        if classification == "added" and (before is not None or after is None):
            raise ImpactError("An added comparison change has invalid states.")
        if classification == "missing" and (before is None or after is not None):
            raise ImpactError("A missing comparison change has invalid states.")
        expected_flags = {
            "regression": classification in {"missing", "regressed"},
            "improvement": classification in {"added", "improved"},
            "equal_score_diff_changed": classification == "equal-score-diff-changed",
        }
        if any(row.get(field) is not value for field, value in expected_flags.items()):
            raise ImpactError("A comparison change has inconsistent flags.")
        if row.get("target") is True:
            target_rows.append(address)
        if row.get("target") is not (address in targets):
            raise ImpactError("A comparison change has an incorrect target state.")
        if row.get("regression") is True:
            regression_rows.append(address)
        if row.get("equal_score_diff_changed") is True:
            equal_rows.append(address)
        if classification == "added":
            added_rows.append(address)
    if addresses != sorted(set(addresses)):
        raise ImpactError("The comparison changes are not sorted and unique.")
    if len(target_rows) != len(targets) or set(target_rows) != set(targets):
        raise ImpactError("The comparison changes do not cover each target.")
    if regression_rows != oracle.get("regressions"):
        raise ImpactError("The comparison regressions disagree with the oracle.")
    if equal_rows != oracle.get("equal_score_diff_changes"):
        raise ImpactError("The equal-score changes disagree with the oracle.")
    if added_rows != oracle.get("added_functions"):
        raise ImpactError("The added comparison rows disagree with the oracle.")


def validate_impact_pack(
    pack: Mapping[str, object], *, campaign_id: str | None = None
) -> dict[str, object]:
    expected = {
        "schema", "kind", "campaign_id", "mode", "lane", "targets",
        "writer_id", "scout_ids", "bindings", "scope", "comparison_changes",
        "regression_oracle", "typed_data_oracle", "review_evidence",
        "promotion_checks", "required_claims",
        "required_refuter_checks", "content_sha256",
    }
    _strict_keys(pack, expected, "The impact pack")
    if pack.get("schema") != IMPACT_SCHEMA or pack.get("kind") != IMPACT_KIND:
        raise ImpactError("The impact pack schema is invalid.")
    if pack.get("content_sha256") != _document_hash(pack):
        raise ImpactError("The impact pack content hash is invalid.")
    if len(_document_text(pack).encode("utf-8")) > MAX_DOCUMENT_BYTES:
        raise ImpactError("The serialized impact pack is too large.")
    clean_campaign = _clean_id(pack.get("campaign_id"), "The campaign ID")
    if campaign_id is not None and clean_campaign != campaign_id:
        raise ImpactError("The impact pack belongs to another campaign.")
    mode = pack.get("mode")
    lane = pack.get("lane")
    if mode not in {"coverage", "refinement"} or lane not in {"closure", "production", "research"}:
        raise ImpactError("The impact pack mode or lane is invalid.")
    if mode == "coverage" and lane != "research":
        raise ImpactError("Coverage impact evidence requires the research lane.")
    targets = _ordered_addresses(
        pack.get("targets"), "The impact pack targets", allow_empty=False, maximum=64
    )
    writer_id = _clean_id(pack.get("writer_id"), "The campaign writer ID")
    scouts_value = pack.get("scout_ids")
    if not isinstance(scouts_value, list) or len(scouts_value) < 2:
        raise ImpactError("The impact pack scout IDs are invalid.")
    scouts = [_clean_id(value, "A scout ID") for value in scouts_value]
    if scouts != sorted(set(scouts)) or writer_id in scouts:
        raise ImpactError("The impact pack role IDs are invalid.")
    _validate_bindings(pack.get("bindings"), targets)
    bindings = pack.get("bindings")
    assert isinstance(bindings, Mapping)
    review_evidence = pack.get("review_evidence")
    _validate_review_evidence(review_evidence, bindings)
    assert isinstance(review_evidence, Mapping)
    if _embedded_review_roles(review_evidence) != (writer_id, scouts):
        raise ImpactError("The impact roles conflict with embedded target briefs.")
    _validate_scope(pack.get("scope"), targets)
    scope = pack.get("scope")
    assert isinstance(scope, Mapping)
    changed_inputs = bindings.get("changed_inputs")
    assert isinstance(changed_inputs, list)
    if scope.get("changed_paths") != [
        row.get("path") for row in changed_inputs if isinstance(row, Mapping)
    ]:
        raise ImpactError("The impact scope does not match the changed inputs.")
    oracle = pack.get("regression_oracle")
    _validate_oracle(oracle)
    _validate_typed_data_oracle(pack.get("typed_data_oracle"))
    assert isinstance(oracle, Mapping)
    _validate_changes(pack.get("comparison_changes"), targets, oracle)
    checks = pack.get("promotion_checks")
    expected_check_ids = [
        "target-output-contract",
        "impact-scope-coverage",
        "all-function-regression-oracle",
        "typed-data-regression-oracle",
    ]
    if not isinstance(checks, list) or len(checks) != len(expected_check_ids):
        raise ImpactError("The impact pack promotion checks are invalid.")
    for check, check_id in zip(checks, expected_check_ids):
        if not isinstance(check, Mapping):
            raise ImpactError("An impact promotion check is invalid.")
        _strict_keys(check, {"id", "status", "subjects"}, "An impact promotion check")
        if check.get("id") != check_id or check.get("status") != "pass":
            raise ImpactError("An impact promotion check did not pass.")
        subjects = check.get("subjects")
        if not isinstance(subjects, list) or subjects:
            raise ImpactError("A passed impact promotion check has failure subjects.")
    if pack.get("required_claims") != [
        {"id": claim_id, "claim": claim}
        for claim_id, claim in PROMOTION_CLAIMS.items()
    ]:
        raise ImpactError("The impact pack promotion claims are invalid.")
    if pack.get("required_refuter_checks") != list(REFUTER_CHECKS):
        raise ImpactError("The impact pack refuter checks are invalid.")
    _sha256(pack.get("content_sha256"), "The impact pack content hash")
    return dict(pack)


def write_impact_pack(pack: Mapping[str, object], root: Path) -> Path:
    validated = validate_impact_pack(pack)
    campaign_id = _cache_component(validated["campaign_id"], "The campaign ID")
    digest = str(validated["content_sha256"])
    cache_root = (root / "build" / "decomp-cache" / "impact").resolve()
    path = (cache_root / campaign_id / f"{digest}.json").resolve()
    try:
        path.relative_to(cache_root / campaign_id)
    except ValueError as error:
        raise ImpactError("The impact pack path escaped its cache.") from error
    content = _document_text(validated)
    _atomic_cache_write(
        path, content, "The impact cache contains different content."
    )
    return path


def _artifact_rows(
    value: object, label: str, *, immutable: bool = False
) -> list[dict[str, str]]:
    if not isinstance(value, list):
        raise ImpactError(f"The finalization receipt has no {label} list.")
    rows: list[dict[str, str]] = []
    for item in value:
        if not isinstance(item, Mapping):
            raise ImpactError(f"The finalization receipt has an invalid {label}.")
        expected = {"path", "sha256", "source_path"} if immutable else {"path", "sha256"}
        _strict_keys(item, expected, f"A finalization {label}")
        path = item.get("path")
        sha256 = item.get("sha256")
        if not isinstance(path, str):
            raise ImpactError(f"The finalization receipt has an invalid {label}.")
        _canonical_path(path, f"A finalization {label} path")
        _sha256(sha256, f"A finalization {label} hash")
        if immutable and (
            Path(path).name != sha256 or Path(path).parent.name != "sha256"
        ):
            raise ImpactError("An immutable finalization artifact path is invalid.")
        row = {"path": path, "sha256": sha256}
        source_path = item.get("source_path")
        if source_path is not None:
            _canonical_path(source_path, f"A finalization {label} source path")
            row["source_path"] = source_path
        rows.append(row)
    return rows


def _step_artifacts(
    finalize_receipt: Mapping[str, object], name: str
) -> list[dict[str, str]]:
    steps = finalize_receipt.get("step_results")
    step = steps.get(name) if isinstance(steps, Mapping) else None
    if not isinstance(step, Mapping):
        raise ImpactError(
            f"The finalization receipt has no {name.replace('_', '-')} step."
        )
    return _artifact_rows(
        step.get("artifacts"), f"{name.replace('_', '-')} artifact"
    )


def _validate_frozen_report_binding(
    finalize_receipt: Mapping[str, object],
    binding: object,
    step_name: str,
) -> tuple[Path, Path]:
    if not isinstance(binding, Mapping):
        raise ImpactError("An impact report binding is invalid.")
    report_path = _canonical_path(binding.get("path"), "The impact report path")
    from tools.decomp_provenance import provenance_path

    provenance = str(provenance_path(Path(report_path)).resolve())
    descriptors = {
        row["path"]: row for row in _step_artifacts(finalize_receipt, step_name)
    }
    if (
        descriptors.get(report_path, {}).get("sha256") != binding.get("sha256")
        or descriptors.get(provenance, {}).get("sha256")
        != binding.get("provenance_sha256")
    ):
        raise ImpactError("An impact report binding conflicts with finalization.")
    immutable = {
        row["source_path"]: row
        for row in _artifact_rows(
            finalize_receipt.get("immutable_artifacts"),
            "immutable artifact",
            immutable=True,
        )
    }
    frozen_paths: list[Path] = []
    for source_path in (report_path, provenance):
        descriptor = descriptors[source_path]
        frozen = immutable.get(source_path)
        if frozen is None or frozen.get("sha256") != descriptor.get("sha256"):
            raise ImpactError("An impact report has no immutable finalization copy.")
        if _file_hash(Path(frozen["path"])) != frozen["sha256"]:
            raise ImpactError("An immutable impact report changed.")
        frozen_paths.append(Path(frozen["path"]))
    return frozen_paths[0], frozen_paths[1]


def _snapshot_blob_descriptor(entries: object) -> dict[str, str] | None:
    parsed = _index_blob(entries, "A finalization index path")
    if parsed is None:
        return None
    mode, object_id = parsed
    return {"git_mode": mode, "git_oid": object_id}


def _scope_source_texts(
    *,
    root: Path,
    snapshot: Mapping[str, object],
    changed_evidence: Mapping[str, Mapping[str, object]],
    evidence_field: str,
    label: str,
) -> dict[str, str]:
    """Read the exact source tree that the finalization receipt binds."""

    texts: dict[str, str] = {}
    for path, entries in sorted(snapshot.items()):
        suffix = PurePosixPath(path).suffix.lower()
        if not path.startswith("src/") or suffix not in SOURCE_SUFFIXES | HEADER_SUFFIXES:
            continue
        parsed = _index_blob(entries, f"The {label} {path} input")
        if parsed is None:
            continue
        _mode, object_id = parsed
        evidence = changed_evidence.get(path)
        if evidence is not None:
            content = evidence.get(evidence_field)
            if not isinstance(content, Mapping) or not isinstance(
                content.get("text"), str
            ):
                raise ImpactError(
                    f"The {label} source evidence is incomplete for {path}."
                )
            texts[path] = str(content["text"])
            continue
        raw = _git_output(
            root,
            ["git", "cat-file", "blob", object_id],
            f"the bound {label} source {path}",
        )
        if _git_blob_oid(raw, len(object_id)) != object_id:
            raise ImpactError(f"The bound {label} source object changed for {path}.")
        texts[path] = raw.decode("utf-8", errors="ignore")
    return texts


def _canonical_receipt_scope(
    *,
    pack: Mapping[str, object],
    bindings: Mapping[str, object],
    baseline_index: Mapping[str, object],
    current_index: Mapping[str, object],
    changed_paths: list[str],
    all_functions: set[int],
    targets: set[int],
) -> dict[str, object]:
    """Recompute scope from the immutable finalization inputs."""

    function_map = bindings.get("function_map")
    retail_image = bindings.get("retail_image")
    if not isinstance(function_map, Mapping) or not isinstance(
        retail_image, Mapping
    ):
        raise ImpactError("The impact scope inputs are invalid.")
    map_path = Path(str(function_map["path"]))
    if len(map_path.parents) < 3:
        raise ImpactError("The impact function-map path is invalid.")
    root = map_path.parents[2]
    if (
        map_path != root / "tools" / "Resources" / "functions_map.txt"
        or Path(str(retail_image["path"])) != root / "original" / "toy2.exe"
    ):
        raise ImpactError("The impact scope inputs are outside the project root.")
    review_evidence = pack.get("review_evidence")
    rows = (
        review_evidence.get("changed_inputs")
        if isinstance(review_evidence, Mapping)
        else None
    )
    if not isinstance(rows, list):
        raise ImpactError("The impact scope has no changed-input evidence.")
    changed_evidence = {
        str(row["path"]): row
        for row in rows
        if isinstance(row, Mapping) and isinstance(row.get("path"), str)
    }
    if sorted(changed_evidence) != changed_paths:
        raise ImpactError("The impact scope changed-input evidence is incomplete.")
    baseline_texts = _scope_source_texts(
        root=root,
        snapshot=baseline_index,
        changed_evidence=changed_evidence,
        evidence_field="before",
        label="baseline",
    )
    current_texts = _scope_source_texts(
        root=root,
        snapshot=current_index,
        changed_evidence=changed_evidence,
        evidence_field="current",
        label="current",
    )
    entries = read_function_map(map_path)
    graph: DependencyGraph | None = None
    graph_error: str | None = None
    try:
        graph = build_call_graph(
            entries, image_path=Path(str(retail_image["path"]))
        )
    except DependencyUnavailable as error:
        graph_error = str(error)
    except Exception as error:  # The scope must preserve this explicit gap.
        graph_error = f"The retail call graph failed: {error}"
    return compute_impact_scope(
        changed_paths=changed_paths,
        source_texts=current_texts,
        baseline_source_texts=baseline_texts,
        all_functions=all_functions,
        targets=targets,
        graph=graph,
        graph_error=graph_error,
    )


def _receipt_brief_binding(identity: Mapping[str, object]) -> dict[str, object]:
    doctor = identity.get("doctor_receipt")
    reports = identity.get("scout_reports")
    if not isinstance(doctor, Mapping) or not isinstance(reports, list):
        raise ImpactError("A finalization brief identity is invalid.")
    path = _canonical_path(identity.get("path"), "A finalization brief path")
    return {
        "target": identity.get("target"),
        "path": path,
        "sha256": identity.get("sha256"),
        "content_sha256": identity.get("content_sha256"),
        "doctor_receipt": {
            "path": doctor.get("path"),
            "sha256": doctor.get("sha256"),
        },
        "scout_reports": [
            {
                "path": report.get("path"),
                "sha256": report.get("sha256"),
            }
            for report in reports
            if isinstance(report, Mapping)
        ],
        "dwarf_input": identity.get("dwarf_input"),
    }


def _validate_pack_receipt_binding(
    pack: Mapping[str, object], finalize_receipt: Mapping[str, object]
) -> None:
    key = finalize_receipt.get("key_payload")
    bindings = pack.get("bindings")
    if not isinstance(key, Mapping) or not isinstance(bindings, Mapping):
        raise ImpactError("The impact pack has no finalization binding.")
    expected_targets = key.get("active_addresses", key.get("addresses"))
    if (
        pack.get("mode") != finalize_receipt.get("mode")
        or pack.get("mode") != key.get("mode")
        or pack.get("lane") != finalize_receipt.get("lane")
        or pack.get("lane") != key.get("lane")
        or pack.get("targets") != expected_targets
        or bindings.get("campaign_head") != key.get("campaign_head")
    ):
        raise ImpactError("The impact pack conflicts with its finalization scope.")
    receipt_briefs = key.get("briefs")
    if not isinstance(receipt_briefs, list):
        raise ImpactError("The finalization receipt has no brief binding.")
    expected_briefs: list[dict[str, object]] = []
    for target in expected_targets if isinstance(expected_targets, list) else []:
        matches = [
            item
            for item in receipt_briefs
            if isinstance(item, Mapping) and item.get("target") == target
        ]
        if not matches:
            raise ImpactError("An active finalization target has no brief binding.")
        expected_briefs.append(_receipt_brief_binding(matches[-1]))
    if bindings.get("briefs") != expected_briefs:
        raise ImpactError("The impact brief binding conflicts with finalization.")
    baseline_index = key.get("campaign_repository_index_snapshot")
    current_index = key.get("repository_index_snapshot")
    if not isinstance(baseline_index, Mapping) or not isinstance(
        current_index, Mapping
    ):
        raise ImpactError("The finalization receipt has no repository index binding.")
    baseline_index_hash = _json_hash(baseline_index)
    current_index_hash = _json_hash(current_index)
    if (
        key.get("repository_index_sha256") != current_index_hash
        or bindings.get("campaign_repository_index_sha256")
        != baseline_index_hash
        or bindings.get("repository_index_sha256") != current_index_hash
    ):
        raise ImpactError("The impact repository index binding changed.")
    changed_paths = sorted(
        path
        for path in set(baseline_index) | set(current_index)
        if baseline_index.get(path) != current_index.get(path)
    )
    rows = bindings.get("changed_inputs")
    if not isinstance(rows, list) or [
        row.get("path") for row in rows if isinstance(row, Mapping)
    ] != changed_paths:
        raise ImpactError("The impact changed-input set conflicts with finalization.")
    for row in rows:
        assert isinstance(row, Mapping)
        path = str(row["path"])
        for field, snapshot in (
            ("before", baseline_index),
            ("current", current_index),
        ):
            descriptor = row.get(field)
            expected = _snapshot_blob_descriptor(snapshot.get(path))
            actual = (
                {
                    "git_mode": descriptor.get("git_mode"),
                    "git_oid": descriptor.get("git_oid"),
                }
                if isinstance(descriptor, Mapping)
                else None
            )
            if actual != expected:
                raise ImpactError(
                    "A changed-input Git object conflicts with finalization."
                )
    baseline_report = bindings.get("baseline_report")
    baseline_data = bindings.get("baseline_data_report")
    if not isinstance(baseline_report, Mapping) or not isinstance(
        baseline_data, Mapping
    ):
        raise ImpactError("The impact baseline report binding is invalid.")
    if (
        baseline_report.get("sha256") != key.get("baseline_report_sha256")
        or baseline_report.get("provenance_sha256")
        != key.get("baseline_report_provenance_sha256")
        or baseline_data.get("sha256")
        != key.get("baseline_data_report_sha256")
        or baseline_data.get("provenance_sha256")
        != key.get("baseline_data_report_provenance_sha256")
    ):
        raise ImpactError("The impact baseline report binding changed.")
    inputs = key.get("inputs")
    files = inputs.get("files") if isinstance(inputs, Mapping) else None
    if not isinstance(files, Mapping):
        raise ImpactError("The finalization receipt has no tool input binding.")
    runtime_paths = {
        "tool": Path(__file__).resolve(),
        "dependencies_tool": Path(decomp_dependencies.__file__).resolve(),
        "binary_tool": Path(decomp_binary.__file__).resolve(),
        "annotations_tool": Path(__file__).with_name("decomp_annotations.py").resolve(),
        "verify_tool": Path(__file__).with_name("decomp_verify.py").resolve(),
    }
    for field in (
        "function_map",
        "retail_image",
        "tool",
        "dependencies_tool",
        "binary_tool",
        "annotations_tool",
        "verify_tool",
    ):
        identity = bindings.get(field)
        if (
            not isinstance(identity, Mapping)
            or files.get(identity.get("path")) != identity.get("sha256")
        ):
            raise ImpactError(
                f"The impact {field.replace('_', ' ')} binding conflicts with finalization."
            )
        if _file_hash(Path(str(identity["path"]))) != identity.get("sha256"):
            raise ImpactError(
                f"The bound impact {field.replace('_', ' ')} changed."
            )
        expected_path = runtime_paths.get(field)
        if expected_path is not None and identity.get("path") != str(expected_path):
            raise ImpactError(
                f"The impact {field.replace('_', ' ')} path is invalid."
            )
    bound_decoder = bindings.get("decoder")
    if (
        bound_decoder != inputs.get("decoder")
        or bound_decoder != decoder_identity()
    ):
        raise ImpactError("The impact decoder identity changed.")
    baseline_code_path, _ = _validate_frozen_report_binding(
        finalize_receipt, bindings.get("baseline_report"), "source_scan"
    )
    current_code_path, _ = _validate_frozen_report_binding(
        finalize_receipt, bindings.get("current_report"), "code_report"
    )
    baseline_data_path, _ = _validate_frozen_report_binding(
        finalize_receipt, bindings.get("baseline_data_report"), "source_scan"
    )
    current_data_path, _ = _validate_frozen_report_binding(
        finalize_receipt, bindings.get("current_data_report"), "data_report"
    )
    targets = {_address(value) for value in pack.get("targets", [])}
    baseline_code = read_report(baseline_code_path)
    current_code = read_report(current_code_path)
    expected_changes, expected_oracle = compare_reports(
        baseline_code, current_code, targets
    )
    function_map = bindings.get("function_map")
    assert isinstance(function_map, Mapping)
    map_entries = read_function_map(Path(str(function_map["path"])))
    mapped_addresses = {address for address, _name in map_entries}
    all_functions = set(baseline_code) | set(current_code) | mapped_addresses
    scope = pack.get("scope")
    if not isinstance(scope, Mapping):
        raise ImpactError("The impact pack has no scope for promotion checks.")
    expected_scope = _canonical_receipt_scope(
        pack=pack,
        bindings=bindings,
        baseline_index=baseline_index,
        current_index=current_index,
        changed_paths=changed_paths,
        all_functions=all_functions,
        targets=targets,
    )
    if scope != expected_scope:
        raise ImpactError("The impact scope conflicts with immutable reports and the map.")
    baseline_data_document = _read_json(
        baseline_data_path,
        "immutable baseline typed-data report",
        max_bytes=MAX_REPORT_BYTES,
    )
    current_data_document = _read_json(
        current_data_path,
        "immutable current typed-data report",
        max_bytes=MAX_REPORT_BYTES,
    )
    expected_data_oracle = _typed_data_oracle(
        baseline_data_document, current_data_document
    )
    expected_checks = promotion_checks(
        mode=str(pack.get("mode")),
        targets=targets,
        baseline=baseline_code,
        current=current_code,
        scope=scope,
        oracle=expected_oracle,
        typed_data_oracle=expected_data_oracle,
    )
    if (
        pack.get("comparison_changes") != expected_changes
        or pack.get("regression_oracle") != expected_oracle
        or pack.get("typed_data_oracle") != expected_data_oracle
        or pack.get("promotion_checks") != expected_checks
    ):
        raise ImpactError("The impact oracles conflict with immutable reports.")


def impact_artifact(
    finalize_receipt: Mapping[str, object],
) -> tuple[dict[str, object], dict[str, str]] | None:
    """Return the immutable impact artifact from one finalization receipt."""

    key_payload = finalize_receipt.get("key_payload")
    required = (
        isinstance(key_payload, Mapping)
        and key_payload.get("impact_review_required") is True
    )
    if not required:
        return None
    steps = finalize_receipt.get("step_results")
    source_scan = steps.get("source_scan") if isinstance(steps, Mapping) else None
    descriptors = _artifact_rows(
        source_scan.get("artifacts") if isinstance(source_scan, Mapping) else None,
        "source-scan artifact",
    )
    immutable = _artifact_rows(
        finalize_receipt.get("immutable_artifacts"),
        "immutable artifact",
        immutable=True,
    )
    immutable_by_source = {
        row.get("source_path"): row for row in immutable if row.get("source_path")
    }
    candidates: list[tuple[dict[str, object], dict[str, str]]] = []
    for descriptor in descriptors:
        frozen = immutable_by_source.get(str(Path(descriptor["path"]).resolve()))
        if frozen is None or frozen.get("sha256") != descriptor["sha256"]:
            raise ImpactError("A source-scan artifact has no immutable copy.")
        frozen_path = Path(frozen["path"])
        if _file_hash(frozen_path) != frozen["sha256"]:
            raise ImpactError("An immutable source-scan artifact changed.")
        try:
            document = _read_json(frozen_path, "source-scan artifact")
        except ImpactError:
            continue
        if document.get("kind") != IMPACT_KIND:
            continue
        pack = validate_impact_pack(
            document,
            campaign_id=str(finalize_receipt.get("campaign_id", "")),
        )
        _validate_pack_receipt_binding(pack, finalize_receipt)
        candidates.append(
            (
                pack,
                {
                    "path": str(frozen_path.resolve()),
                    "sha256": frozen["sha256"],
                    "content_sha256": str(pack["content_sha256"]),
                },
            )
        )
    if len(candidates) > 1:
        raise ImpactError("The finalization receipt has multiple impact packs.")
    if not candidates:
        raise ImpactError("The finalization receipt has no impact pack.")
    return candidates[0] if candidates else None


def review_required(finalize_receipt: Mapping[str, object]) -> bool:
    return impact_artifact(finalize_receipt) is not None


def _citation(
    value: object,
    label: str,
    allowed_sources: set[str],
    allowed_locators: set[str],
) -> dict[str, str]:
    if not isinstance(value, Mapping):
        raise ImpactError(f"A {label} citation is invalid.")
    _strict_keys(value, {"source", "locator"}, f"A {label} citation")
    source = _clean_id(
        value.get("source"), "The citation source", max_length=4096
    )
    if source not in allowed_sources:
        raise ImpactError("A review citation names an unbound evidence source.")
    locator = _clean_id(
        value.get("locator"), "The citation locator", max_length=4096
    )
    if locator not in allowed_locators:
        raise ImpactError("A review citation has an unbound evidence locator.")
    return {"source": source, "locator": locator}


def _citations(
    value: object,
    label: str,
    allowed_sources: set[str],
    allowed_locators: set[str],
) -> list[dict[str, str]]:
    if not isinstance(value, list) or not 1 <= len(value) <= 8:
        raise ImpactError(f"A {label} needs one through eight citations.")
    citations = [
        _citation(item, label, allowed_sources, allowed_locators)
        for item in value
    ]
    identities = [(row["source"], row["locator"]) for row in citations]
    if len(identities) != len(set(identities)):
        raise ImpactError(f"A {label} repeats an evidence citation.")
    return citations


def _review_evidence_sources(
    finalize_receipt: Mapping[str, object],
    pack: Mapping[str, object],
    artifact: Mapping[str, str],
) -> dict[str, str]:
    immutable = _artifact_rows(
        finalize_receipt.get("immutable_artifacts"),
        "immutable artifact",
        immutable=True,
    )
    sources: dict[str, str] = {}
    bindings = pack.get("bindings")
    if not isinstance(bindings, Mapping):
        raise ImpactError("The impact pack has no review evidence bindings.")
    report_sources = {
        str(bindings[field]["path"]): field
        for field in (
            "baseline_report",
            "current_report",
            "baseline_data_report",
            "current_data_report",
        )
        if isinstance(bindings.get(field), Mapping)
    }
    for row in immutable:
        path = _canonical_path(row["path"], "An immutable evidence path")
        _sha256(row["sha256"], "An immutable evidence hash")
        if _file_hash(Path(path)) != row["sha256"]:
            raise ImpactError("An immutable review evidence file changed.")
        source_path = row.get("source_path")
        if path == str(Path(artifact.get("path", "")).resolve()):
            sources["impact"] = path
        if source_path in report_sources:
            sources[report_sources[source_path]] = path
    artifact_path = _canonical_path(
        artifact.get("path"), "The immutable impact path"
    )
    if sources.get("impact") != artifact_path:
        raise ImpactError("The impact pack is not immutable review evidence.")
    expected = {
        "impact",
        "baseline_report",
        "current_report",
        "baseline_data_report",
        "current_data_report",
    }
    if set(sources) != expected:
        raise ImpactError("The finalization receipt has incomplete review evidence.")
    return sources


def _review_locators(pack: Mapping[str, object]) -> dict[str, list[str]]:
    evidence = pack.get("review_evidence")
    assert isinstance(evidence, Mapping)
    changed_inputs = evidence.get("changed_inputs")
    briefs = evidence.get("briefs")
    assert isinstance(changed_inputs, list) and isinstance(briefs, list)
    return {
        "changed_inputs": ["review_evidence.changed_inputs"],
        "briefs": ["review_evidence.briefs"],
        "affected": [
            "comparison_changes",
            "regression_oracle",
            "typed_data_oracle",
        ],
        "automatic-promotion-checks": ["promotion_checks"],
        "equal-score-diff-changes": [
            "comparison_changes",
            "regression_oracle.equal_score_diff_changes",
        ],
        "typed-data-regressions": ["typed_data_oracle"],
        "completeness-gaps": ["scope.gaps"],
    }


def _claim_locators(pack: Mapping[str, object], claim_id: str) -> list[str]:
    locators = _review_locators(pack)
    if claim_id == "affected-function-regressions":
        return locators["affected"]
    return locators["briefs"] + locators["changed_inputs"]


def _expected_citations(
    artifact: Mapping[str, str], locators: Sequence[str]
) -> list[dict[str, str]]:
    source = str(artifact["path"])
    return [{"source": source, "locator": locator} for locator in locators]


def _report_citations(
    sources: Mapping[str, str], fields: Sequence[str]
) -> list[dict[str, str]]:
    citations: list[dict[str, str]] = []
    seen: set[str] = set()
    for field in fields:
        source = sources[field]
        if source in seen:
            continue
        seen.add(source)
        citations.append({"source": source, "locator": "$"})
    return citations


def _expected_claim_citations(
    pack: Mapping[str, object],
    artifact: Mapping[str, str],
    sources: Mapping[str, str],
    claim_id: str,
) -> list[dict[str, str]]:
    result = _expected_citations(artifact, _claim_locators(pack, claim_id))
    if claim_id == "affected-function-regressions":
        result.extend(
            _report_citations(
                sources,
                (
                    "baseline_report",
                    "current_report",
                    "baseline_data_report",
                    "current_data_report",
                ),
            )
        )
    return result


def _expected_refuter_citations(
    artifact: Mapping[str, str],
    sources: Mapping[str, str],
    locator_groups: Mapping[str, list[str]],
    check_id: str,
) -> list[dict[str, str]]:
    result = _expected_citations(artifact, locator_groups[check_id])
    if check_id == "equal-score-diff-changes":
        result.extend(
            _report_citations(sources, ("baseline_report", "current_report"))
        )
    elif check_id == "typed-data-regressions":
        result.extend(
            _report_citations(
                sources, ("baseline_data_report", "current_data_report")
            )
        )
    return result


def _expected_refuter_subjects(pack: Mapping[str, object]) -> dict[str, list[str]]:
    checks = pack.get("promotion_checks")
    oracle = pack.get("regression_oracle")
    scope = pack.get("scope")
    assert isinstance(checks, list)
    assert isinstance(oracle, Mapping)
    assert isinstance(scope, Mapping)
    gaps = scope.get("gaps")
    gap_codes = sorted(
        {
            str(gap.get("code"))
            for gap in gaps
            if isinstance(gaps, list)
            and isinstance(gap, Mapping)
            and isinstance(gap.get("code"), str)
        }
    )
    return {
        "automatic-promotion-checks": [
            str(check["id"]) for check in checks if isinstance(check, Mapping)
        ],
        "equal-score-diff-changes": list(
            oracle.get("equal_score_diff_changes", [])
        ),
        "typed-data-regressions": list(
            pack.get("typed_data_oracle", {}).get("problems", [])
        ),
        "completeness-gaps": gap_codes,
    }


def validate_review_report(
    report: Mapping[str, object],
    *,
    finalize_receipt: Mapping[str, object],
    pack: Mapping[str, object],
    artifact: Mapping[str, str],
) -> dict[str, object]:
    """Validate one strict independent review against immutable impact evidence."""

    _strict_keys(
        report,
        {
            "schema",
            "kind",
            "campaign_id",
            "finalize_receipt",
            "impact_pack",
            "targets",
            "writer_id",
            "reviewer_id",
            "scout_ids",
            "decision",
            "claims",
            "refuter_checks",
            "content_sha256",
        },
        "The review report",
    )
    if report.get("schema") != REVIEW_SCHEMA or report.get("kind") != REVIEW_KIND:
        raise ImpactError("The review report schema is invalid.")
    if report.get("content_sha256") != _document_hash(report):
        raise ImpactError("The review report content hash is invalid.")
    campaign_id = _clean_id(report.get("campaign_id"), "The campaign ID")
    if campaign_id != pack.get("campaign_id") or campaign_id != finalize_receipt.get(
        "campaign_id"
    ):
        raise ImpactError("The review report belongs to another campaign.")
    finalize_id = report.get("finalize_receipt")
    if not isinstance(finalize_id, Mapping):
        raise ImpactError("The review report has no finalization identity.")
    _strict_keys(
        finalize_id,
        {"receipt_key", "content_sha256"},
        "The review finalization identity",
    )
    if finalize_id != {
        "receipt_key": finalize_receipt.get("receipt_key"),
        "content_sha256": finalize_receipt.get("receipt_sha256"),
    }:
        raise ImpactError("The review report names another finalization receipt.")
    impact_id = report.get("impact_pack")
    if not isinstance(impact_id, Mapping):
        raise ImpactError("The review report has no impact identity.")
    _strict_keys(
        impact_id,
        {"content_sha256", "artifact_sha256"},
        "The review impact identity",
    )
    if impact_id != {
        "content_sha256": pack.get("content_sha256"),
        "artifact_sha256": artifact.get("sha256"),
    }:
        raise ImpactError("The review report names another impact pack.")
    if report.get("targets") != pack.get("targets"):
        raise ImpactError("The review report names another target set.")
    if report.get("writer_id") != pack.get("writer_id"):
        raise ImpactError("The review report names another campaign writer.")
    if report.get("scout_ids") != pack.get("scout_ids"):
        raise ImpactError("The review report names another scout set.")
    reviewer_id = _clean_id(report.get("reviewer_id"), "The reviewer ID")
    if reviewer_id == pack.get("writer_id") or reviewer_id in pack.get(
        "scout_ids", []
    ):
        raise ImpactError("The reviewer ID matches a writer or scout ID.")
    if report.get("decision") != "accepted":
        raise ImpactError("The independent review did not accept promotion.")
    evidence_sources = _review_evidence_sources(
        finalize_receipt, pack, artifact
    )
    allowed_sources = set(evidence_sources.values())
    locator_groups = _review_locators(pack)
    allowed_locators = {
        locator for values in locator_groups.values() for locator in values
    }
    allowed_locators.add("$")

    claims = report.get("claims")
    if not isinstance(claims, list) or len(claims) != len(PROMOTION_CLAIMS):
        raise ImpactError("The review report has an invalid promotion claim set.")
    normalized_claims: list[dict[str, object]] = []
    for item, (claim_id, claim) in zip(claims, PROMOTION_CLAIMS.items()):
        if not isinstance(item, Mapping):
            raise ImpactError("A promotion claim is invalid.")
        _strict_keys(item, {"id", "claim", "status", "evidence"}, "A promotion claim")
        if (
            item.get("id") != claim_id
            or item.get("claim") != claim
            or item.get("status") != "accepted"
        ):
            raise ImpactError(f"The {claim_id} promotion claim is not accepted.")
        citations = _citations(
            item.get("evidence"),
            "promotion claim",
            allowed_sources,
            allowed_locators,
        )
        if citations != _expected_claim_citations(
            pack, artifact, evidence_sources, claim_id
        ):
            raise ImpactError(
                f"The {claim_id} promotion claim has incomplete evidence."
            )
        normalized_claims.append(
            {
                "id": claim_id,
                "claim": claim,
                "status": "accepted",
                "evidence": citations,
            }
        )

    refuters = report.get("refuter_checks")
    if not isinstance(refuters, list) or len(refuters) != len(REFUTER_CHECKS):
        raise ImpactError("The review report has an invalid refuter check set.")
    expected_subjects = _expected_refuter_subjects(pack)
    normalized_refuters: list[dict[str, object]] = []
    for item, check_id in zip(refuters, REFUTER_CHECKS):
        if not isinstance(item, Mapping):
            raise ImpactError("A refuter check is invalid.")
        _strict_keys(item, {"id", "status", "subjects", "evidence"}, "A refuter check")
        subjects = item.get("subjects")
        if (
            item.get("id") != check_id
            or item.get("status") != "passed"
            or subjects != expected_subjects[check_id]
        ):
            raise ImpactError(f"The {check_id} refuter check did not pass.")
        citations = _citations(
            item.get("evidence"),
            "refuter check",
            allowed_sources,
            allowed_locators,
        )
        if citations != _expected_refuter_citations(
            artifact, evidence_sources, locator_groups, check_id
        ):
            raise ImpactError(
                f"The {check_id} refuter check has incomplete evidence."
            )
        normalized_refuters.append(
            {
                "id": check_id,
                "status": "passed",
                "subjects": list(subjects),
                "evidence": citations,
            }
        )
    normalized = dict(report)
    normalized["claims"] = normalized_claims
    normalized["refuter_checks"] = normalized_refuters
    return normalized


def make_review_template(
    finalize_receipt: Mapping[str, object], reviewer_id: str
) -> dict[str, object]:
    """Return the exact review form for one finalization receipt."""

    found = impact_artifact(finalize_receipt)
    if found is None:
        raise ImpactError("The finalization receipt does not require impact review.")
    pack, artifact = found
    clean_reviewer = _clean_id(reviewer_id, "The reviewer ID")
    if clean_reviewer == pack.get("writer_id") or clean_reviewer in pack.get(
        "scout_ids", []
    ):
        raise ImpactError("The reviewer ID matches a writer or scout ID.")
    locator_groups = _review_locators(pack)
    evidence_sources = _review_evidence_sources(
        finalize_receipt, pack, artifact
    )
    report: dict[str, object] = {
        "schema": REVIEW_SCHEMA,
        "kind": REVIEW_KIND,
        "campaign_id": pack["campaign_id"],
        "finalize_receipt": {
            "receipt_key": finalize_receipt.get("receipt_key"),
            "content_sha256": finalize_receipt.get("receipt_sha256"),
        },
        "impact_pack": {
            "content_sha256": pack["content_sha256"],
            "artifact_sha256": artifact["sha256"],
        },
        "targets": pack["targets"],
        "writer_id": pack["writer_id"],
        "reviewer_id": clean_reviewer,
        "scout_ids": pack["scout_ids"],
        "decision": "pending",
        "claims": [
            {
                "id": claim_id,
                "claim": claim,
                "status": "pending",
                "evidence": _expected_claim_citations(
                    pack, artifact, evidence_sources, claim_id
                ),
            }
            for claim_id, claim in PROMOTION_CLAIMS.items()
        ],
        "refuter_checks": [
            {
                "id": check_id,
                "status": "pending",
                "subjects": subjects,
                "evidence": _expected_refuter_citations(
                    artifact, evidence_sources, locator_groups, check_id
                ),
            }
            for check_id, subjects in _expected_refuter_subjects(pack).items()
        ],
    }
    report["content_sha256"] = _document_hash(report)
    return report


def _contains_placeholder(value: object) -> bool:
    if isinstance(value, str):
        return "REPLACE_" in value or "PLACEHOLDER" in value
    if isinstance(value, Mapping):
        return any(_contains_placeholder(item) for item in value.values())
    if isinstance(value, list):
        return any(_contains_placeholder(item) for item in value)
    return False


def create_review_receipt(
    *,
    finalize_receipt: Mapping[str, object],
    review_report_path: Path,
    cache_root: Path,
) -> tuple[dict[str, object], dict[str, object]]:
    """Validate and cache one independent review report."""

    found = impact_artifact(finalize_receipt)
    if found is None:
        raise ImpactError("The finalization receipt does not require impact review.")
    pack, artifact = found
    raw_report = _read_json(review_report_path, "review report")
    if _contains_placeholder(raw_report):
        raise ImpactError("The review report contains a placeholder.")
    report = validate_review_report(
        raw_report,
        finalize_receipt=finalize_receipt,
        pack=pack,
        artifact=artifact,
    )
    receipt: dict[str, object] = {
        "schema": REVIEW_SCHEMA,
        "kind": REVIEW_RECEIPT_KIND,
        "status": "accepted",
        "campaign_id": pack["campaign_id"],
        "finalize_receipt": report["finalize_receipt"],
        "impact_pack": {
            **report["impact_pack"],
            "artifact_path": artifact["path"],
        },
        "review_report": report,
        "reviewer_id": report["reviewer_id"],
        "writer_id": report["writer_id"],
        "scout_ids": report["scout_ids"],
        "accepted_claims": list(PROMOTION_CLAIMS),
    }
    receipt["content_sha256"] = _document_hash(receipt)
    canonical_root = cache_root.resolve()
    path = (
        canonical_root
        / str(pack["campaign_id"])
        / f"{receipt['content_sha256']}.json"
    ).resolve()
    try:
        path.relative_to(canonical_root / str(pack["campaign_id"]))
    except ValueError as error:
        raise ImpactError("The review receipt path escaped its cache.") from error
    content = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    _atomic_cache_write(
        path,
        content,
        "The review receipt cache contains different content.",
    )
    identity: dict[str, object] = {
        "path": str(path.resolve()),
        "file_sha256": _file_hash(path),
        "content_sha256": receipt["content_sha256"],
        "impact_pack_sha256": artifact["sha256"],
        "reviewer_id": receipt["reviewer_id"],
        "accepted_claims": receipt["accepted_claims"],
    }
    return receipt, identity


def validate_review_identity(
    identity: Mapping[str, object],
    *,
    finalize_receipt: Mapping[str, object],
    cache_root: Path,
) -> dict[str, object]:
    """Re-read a cached review receipt and verify its finalization binding."""

    _strict_keys(
        identity,
        {
            "path",
            "file_sha256",
            "content_sha256",
            "impact_pack_sha256",
            "reviewer_id",
            "accepted_claims",
        },
        "The accepted review identity",
    )
    path_value = identity.get("path")
    if not isinstance(path_value, str):
        raise ImpactError("The accepted review path is invalid.")
    _sha256(identity.get("file_sha256"), "The accepted review file hash")
    _sha256(identity.get("content_sha256"), "The accepted review content hash")
    _sha256(identity.get("impact_pack_sha256"), "The accepted impact artifact hash")
    path = Path(path_value)
    campaign_id = str(finalize_receipt.get("campaign_id", ""))
    expected_path = (
        cache_root.resolve()
        / campaign_id
        / f"{identity.get('content_sha256')}.json"
    ).resolve()
    if path.resolve() != expected_path or path_value != str(expected_path):
        raise ImpactError("The accepted review receipt path is not canonical.")
    if _file_hash(path) != identity.get("file_sha256"):
        raise ImpactError("The accepted review receipt file changed.")
    receipt = _read_json(path, "accepted review receipt")
    _strict_keys(
        receipt,
        {
            "schema",
            "kind",
            "status",
            "campaign_id",
            "finalize_receipt",
            "impact_pack",
            "review_report",
            "reviewer_id",
            "writer_id",
            "scout_ids",
            "accepted_claims",
            "content_sha256",
        },
        "The accepted review receipt",
    )
    if (
        receipt.get("schema") != REVIEW_SCHEMA
        or receipt.get("kind") != REVIEW_RECEIPT_KIND
        or receipt.get("status") != "accepted"
        or receipt.get("content_sha256") != _document_hash(receipt)
        or receipt.get("content_sha256") != identity.get("content_sha256")
    ):
        raise ImpactError("The accepted review receipt is invalid.")
    found = impact_artifact(finalize_receipt)
    if found is None:
        raise ImpactError("The accepted review has no bound impact pack.")
    pack, artifact = found
    report = receipt.get("review_report")
    if not isinstance(report, Mapping):
        raise ImpactError("The accepted review receipt has no review report.")
    top_impact = receipt.get("impact_pack")
    if not isinstance(top_impact, Mapping):
        raise ImpactError("The accepted review receipt has no impact identity.")
    _strict_keys(
        top_impact,
        {"content_sha256", "artifact_sha256", "artifact_path"},
        "The accepted review impact identity",
    )
    if (
        receipt.get("finalize_receipt") != report.get("finalize_receipt")
        or {key: top_impact.get(key) for key in ("content_sha256", "artifact_sha256")}
        != report.get("impact_pack")
        or top_impact.get("artifact_path") != artifact.get("path")
        or receipt.get("reviewer_id") != report.get("reviewer_id")
        or receipt.get("writer_id") != report.get("writer_id")
        or receipt.get("scout_ids") != report.get("scout_ids")
    ):
        raise ImpactError("The accepted review metadata conflicts with its report.")
    if (
        receipt.get("campaign_id") != pack.get("campaign_id")
        or identity.get("impact_pack_sha256") != artifact.get("sha256")
        or identity.get("reviewer_id") != receipt.get("reviewer_id")
        or identity.get("accepted_claims") != list(PROMOTION_CLAIMS)
        or receipt.get("accepted_claims") != list(PROMOTION_CLAIMS)
    ):
        raise ImpactError("The accepted review receipt identity changed.")
    validate_review_report(
        report,
        finalize_receipt=finalize_receipt,
        pack=pack,
        artifact=artifact,
    )
    return receipt


def _load_finalize_receipt(path: Path) -> dict[str, object]:
    receipt = _read_json(path, "finalization receipt")
    key_payload = receipt.get("key_payload")
    receipt_key = receipt.get("receipt_key")
    saved_hash = receipt.get("receipt_sha256")
    payload = dict(receipt)
    payload.pop("receipt_sha256", None)
    if (
        receipt.get("schema_version") != 3
        or receipt.get("receipt_version") != 2
        or receipt.get("record_type") != "finalize-receipt"
        or receipt.get("status") != "passed"
        or not isinstance(key_payload, Mapping)
        or _sha256(receipt_key, "The finalization receipt key")
        != _json_hash(key_payload)
        or _sha256(saved_hash, "The finalization receipt hash")
        != _json_hash(payload)
    ):
        raise ImpactError("The finalization receipt is invalid.")
    impact_artifact(receipt)
    return receipt


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Create or verify an independent impact review."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    template = subparsers.add_parser(
        "template", help="Print the pending review form for a finalization receipt."
    )
    template.add_argument("--finalize-receipt", type=Path, required=True)
    template.add_argument("--reviewer-id", required=True)
    template.add_argument("--json", action="store_true")
    verify = subparsers.add_parser(
        "verify-review", help="Verify a completed independent review report."
    )
    verify.add_argument("--finalize-receipt", type=Path, required=True)
    verify.add_argument("--review-report", type=Path, required=True)
    verify.add_argument("--json", action="store_true")
    seal = subparsers.add_parser(
        "seal-review", help="Hash and verify a completed independent review report."
    )
    seal.add_argument("--finalize-receipt", type=Path, required=True)
    seal.add_argument("--review-report", type=Path, required=True)
    seal.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        receipt = _load_finalize_receipt(args.finalize_receipt)
        if args.command == "template":
            output = make_review_template(receipt, args.reviewer_id)
        else:
            found = impact_artifact(receipt)
            assert found is not None
            pack, artifact = found
            raw = _read_json(args.review_report, "review report")
            if _contains_placeholder(raw):
                raise ImpactError("The review report contains a placeholder.")
            if args.command == "seal-review":
                raw["content_sha256"] = _document_hash(raw)
            report = validate_review_report(
                raw,
                finalize_receipt=receipt,
                pack=pack,
                artifact=artifact,
            )
            if args.command == "seal-review":
                output = report
            else:
                output = {
                    "ok": True,
                    "campaign_id": report["campaign_id"],
                    "reviewer_id": report["reviewer_id"],
                    "accepted_claims": [item["id"] for item in report["claims"]],
                    "passed_refuter_checks": [
                        item["id"] for item in report["refuter_checks"]
                    ],
                }
        print(json.dumps(output, indent=2, sort_keys=True))
        return 0
    except ImpactError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
