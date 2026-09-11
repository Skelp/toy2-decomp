#!/usr/bin/env python3
"""Compare decompilation reports and enforce verification-state regressions."""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import statistics
import subprocess
import sys
from hashlib import sha256
from pathlib import Path
from typing import NamedTuple

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_status import (  # noqa: E402
    MatchStatus,
    is_symbol_only_diff,
    read_match_statuses,
    read_tool_artifacts,
    verification_status,
)
from tools import decomp_lint  # noqa: E402
from tools.decomp_annotations import read_source_annotations  # noqa: E402
from tools.decomp_resources import (  # noqa: E402
    ResourceId,
    parse_resource,
    resource_rows,
    resource_source_snapshot,
    selected_evidence,
    staged_resource_source_problems,
)

TOOL_ARTIFACTS = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"
FUNCTIONS_MAP = ROOT / "tools" / "Resources" / "functions_map.txt"
FUNCTION_SIZES = ROOT / "build" / "decomp-function-sizes.json"
# A coverage body this large is accepted below 50 percent when it reaches this
# share of its score ceiling: the STUB becomes a FUNCTION and refinement
# continues later instead of discarding thousands of retail bytes of work.
PROVISIONAL_COVERAGE_MIN_SIZE = 2048
PROVISIONAL_COVERAGE_MIN_SCORE = 0.25
# A header edit moves a function the writer never touched when its effective
# score changes by this many percentage points (the display resolution).
HEADER_SIDE_EFFECT_MIN_POINTS = 0.01


def provisional_coverage_ok(
    address: int, matching: float, functions_map: Path, function_sizes: Path
) -> tuple[bool, str]:
    """Return (accepted, reason) for a coverage target that scores below 50 percent."""

    import decomp_utils  # noqa: PLC0415  (decomp_utils imports this module)
    from tools.decomp_diff import read_score_ceiling  # noqa: PLC0415

    try:
        size = decomp_utils.read_mapped_sizes(functions_map, function_sizes).get(address, 0)
    except (OSError, ValueError, KeyError, TypeError):
        size = 0
    ceiling = read_score_ceiling(address, functions_map, function_sizes) or 1.0
    relative = matching / ceiling if ceiling else matching
    accepted = size >= PROVISIONAL_COVERAGE_MIN_SIZE and relative >= PROVISIONAL_COVERAGE_MIN_SCORE
    return accepted, f"{size} retail bytes, {relative * 100:.1f}% of the score ceiling"


class LintDebtChange(NamedTuple):
    removed_errors: int = 0
    new_errors: int = 0
    stale_rows: int = 0


def file_hash(path: Path) -> str:
    digest = sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_hash(root: Path) -> str:
    digest = sha256()
    if not root.exists():
        return "missing"
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(bytes.fromhex(file_hash(path)))
    return digest.hexdigest()


def ninja_logical_lines(path: Path) -> list[str]:
    """Read Ninja statements and join continued lines."""

    statements = []
    pending = ""
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.rstrip()
        if line.endswith("$"):
            pending += line[:-1] + " "
            continue
        statements.append(pending + line)
        pending = ""
    if pending:
        statements.append(pending.rstrip())
    return statements


def ninja_blocks(path: Path) -> list[list[str]]:
    """Read rule and build blocks from a generated Ninja file."""

    blocks = []
    block = []
    for line in ninja_logical_lines(path):
        if line.startswith(("rule ", "build ")):
            if block:
                blocks.append(block)
            block = [line]
        elif block and line[:1].isspace():
            if line.strip():
                block.append(line)
        elif block and line.strip():
            blocks.append(block)
            block = []
    if block:
        blocks.append(block)
    return blocks


def normalized_build_context(build_root: Path) -> str:
    """Hash the generated compile and link graph."""

    build_file = build_root / "build.ninja"
    rules_file = build_root / "CMakeFiles" / "rules.ninja"
    if not build_file.exists() or not rules_file.exists():
        return "missing"

    rule_blocks = {
        block[0].split(None, 1)[1]: block
        for block in ninja_blocks(rules_file)
        if block[0].startswith("rule ")
        and re.search(r"(?:COMPILER|LINKER)", block[0])
    }
    referenced_variables = {}
    normalized = []
    for name, block in sorted(rule_blocks.items()):
        rule_lines = [block[0]]
        variables = set()
        for line in block[1:]:
            statement = line.strip()
            if statement.startswith("description ="):
                continue
            rule_lines.append(statement)
            variables.update(
                first or second
                for first, second in re.findall(
                    r"\$(?:\{([A-Za-z_][A-Za-z0-9_]*)\}|([A-Za-z_][A-Za-z0-9_]*))",
                    statement,
                )
            )
        referenced_variables[name] = variables
        normalized.extend(line.strip() for line in rule_lines)

    for block in ninja_blocks(build_file):
        match = re.match(r"build\s+.+?:\s+(\S+)", block[0])
        if match is None or match.group(1) not in rule_blocks:
            continue
        normalized.append(block[0].strip())
        variables = referenced_variables[match.group(1)]
        for line in block[1:]:
            statement = line.strip()
            key = statement.partition("=")[0].strip()
            if key in variables:
                normalized.append(statement)

    return sha256(("\n".join(normalized) + "\n").encode("utf-8")).hexdigest()


def source_state(source_root: Path) -> dict[str, object]:
    annotations = read_source_annotations(source_root)
    return {
        "implemented_addresses": sorted(
            int(item.address, 16) for item in annotations if item.kind == "function"
        ),
        "stub_addresses": sorted(
            int(item.address, 16) for item in annotations if item.kind == "stub"
        ),
        "source_debt": {
            str(address): sorted(set(rules))
            for address, rules in read_source_debt(source_root).items()
        },
    }


def build_inputs(root: Path) -> dict[str, str]:
    """Hash the build rules and output, the compiler and the generated build graph."""
    values = {}
    for name, path in (
        ("build_rules_sha256", root / "build" / "build.ninja"),
        ("recompiled_sha256", root / "build" / "toy2.exe"),
        ("compiler_driver_sha256", root / ".tooling" / "msvc600-8168" / "VC98" / "Bin" / "CL.EXE"),
        ("compiler_backend_sha256", root / ".tooling" / "msvc600-8168" / "VC98" / "Bin" / "C1XX.DLL"),
    ):
        if path.exists():
            values[name] = file_hash(path)
    values["build_context_sha256"] = normalized_build_context(root / "build")
    return values


def metadata(
    report: Path | None = None, data_report: Path | None = None
) -> dict[str, object]:
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=True
    ).stdout.strip()
    values = {"git_head": head, **build_inputs(ROOT)}
    if report is not None and report.exists():
        values["baseline_report_sha256"] = file_hash(report)
    if data_report is not None and data_report.exists():
        values["baseline_data_report_sha256"] = file_hash(data_report)
    values["sdk_headers_sha256"] = tree_hash(ROOT / "external" / "include")
    values["vc6_headers_sha256"] = tree_hash(
        ROOT / ".tooling" / "msvc600-8168" / "VC98" / "Include"
    )
    values["source_dependency_sha256"] = tree_hash(ROOT / "src")
    values["resource_sources"] = resource_source_snapshot(ROOT)
    original = ROOT / "original" / "toy2.exe"
    recompiled = ROOT / "build" / "toy2.exe"
    if original.exists() and recompiled.exists():
        values["resources"] = resource_rows(original, recompiled)
    values.update(source_state(ROOT / "src"))
    reccmp_head = subprocess.run(
        ["git", "-C", "external/submodules/reccmp", "rev-parse", "HEAD"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    ).stdout.strip()
    if reccmp_head:
        values["reccmp_git_head"] = reccmp_head
    return values


def write_metadata(
    path: Path, report: Path | None = None, data_report: Path | None = None
) -> None:
    path.write_text(
        json.dumps(metadata(report, data_report), indent=2) + "\n",
        encoding="utf-8",
    )


# The report stamp. validate fingerprints the tree that its current reports describe,
# so that campaigns finish (the record path) and the next baseline can reuse them in
# place of a new comparison. Any changed input or report disables the reuse.
STAMP_ADVANCE_EXEMPT = ("git_head", "index_tree")


def _git_output(root: Path, *args: str) -> str | None:
    result = subprocess.run(["git", *args], cwd=root, text=True, capture_output=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def report_fingerprint(root: Path = ROOT) -> dict[str, object]:
    """Return what the comparison and data reports depend on: HEAD, the index, src, the
    function map, the build outputs, the toolchain, the reccmp configuration and the code
    that writes the reports."""
    values: dict[str, object] = {"git_head": _git_output(root, "rev-parse", "HEAD"),
                                 "index_tree": _git_output(root, "write-tree")}
    values.update(build_inputs(root))
    for name, path in (("pdb_sha256", root / "build" / "toy2.pdb"),
                       ("original_sha256", root / "original" / "toy2.exe"),
                       ("functions_map_sha256", root / "tools" / "Resources" / "functions_map.txt"),
                       ("data_report_tool_sha256", root / "tools" / "generate-decomp-data-report.py"),
                       ("reccmp_project_sha256", root / "reccmp-project.yml"),
                       ("reccmp_user_sha256", root / "reccmp-user.yml"),
                       ("reccmp_build_sha256", root / "reccmp-build.yml"),
                       ("reccmp_detected_sha256", root / "build" / "reccmp-build.yml")):
        values[name] = file_hash(path) if path.exists() else "missing"
    values["sdk_headers_sha256"] = tree_hash(root / "external" / "include")
    values["vc6_headers_sha256"] = tree_hash(root / ".tooling" / "msvc600-8168" / "VC98" / "Include")
    values["source_dependency_sha256"] = tree_hash(root / "src")
    # The venv can load reccmp from another checkout, and a git head misses uncommitted edits.
    values["reccmp_source_sha256"] = package_hash("reccmp")
    return values


def package_hash(name: str) -> str:
    """Hash the Python files of an importable package, wherever the venv loads it from."""
    try:
        spec = importlib.util.find_spec(name)
    except (ImportError, ValueError):
        spec = None
    locations = list(spec.submodule_search_locations or []) if spec else []
    if not locations:
        return "missing"
    digest, package = sha256(), Path(locations[0])
    for path in sorted(package.rglob("*.py")):
        digest.update(path.relative_to(package).as_posix().encode("utf-8"))
        digest.update(bytes.fromhex(file_hash(path)))
    return digest.hexdigest()


def write_report_stamp(path: Path, reports: list[Path], root: Path = ROOT) -> None:
    stamp = {"fingerprint": report_fingerprint(root),
             "reports": {str(report): file_hash(root / report) for report in reports}}
    (root / path).write_text(json.dumps(stamp, indent=1) + "\n", encoding="utf-8")


def report_stamp_problem(path: Path, root: Path = ROOT, exempt: tuple[str, ...] = (),
                         required: tuple[Path, ...] = ()) -> str:
    """Return why the stamped reports may not describe the current tree, or "" when every
    input and every report is unchanged and each required report is stamped."""
    try:
        stamp = json.loads((root / path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        stamp = None
    saved = stamp.get("fingerprint") if isinstance(stamp, dict) else None
    reports = stamp.get("reports") if isinstance(stamp, dict) else None
    if not isinstance(saved, dict) or not isinstance(reports, dict) or not reports:
        return "no valid report stamp"
    missing = [str(report) for report in required if str(report) not in reports]
    if missing:
        return ", ".join(missing) + " not stamped"
    current = report_fingerprint(root)
    if not current["git_head"] or not current["index_tree"]:
        return "git cannot read HEAD or the index"
    changed = sorted(key for key in set(saved) | set(current)
                     if key not in exempt and saved.get(key) != current.get(key))
    if changed:
        return ", ".join(changed) + " changed"
    for name, digest in reports.items():
        report = root / name
        if not report.is_file() or file_hash(report) != digest:
            return f"{name} changed"
    return ""


def advance_report_stamp(path: Path, root: Path = ROOT) -> str:
    """After a commit of the stamped tree, move the stamp to the new HEAD and index when
    nothing else changed and src and the map equal HEAD; else remove the stamp."""
    problem = report_stamp_problem(path, root, STAMP_ADVANCE_EXEMPT)
    status = _git_output(root, "status", "--porcelain", "--", "src", "tools/Resources/functions_map.txt")
    if not problem and status != "":
        problem = "src or the function map differs from HEAD"
    if problem:
        (root / path).unlink(missing_ok=True)
        return problem
    stamp = json.loads((root / path).read_text(encoding="utf-8"))
    stamp["fingerprint"] = report_fingerprint(root)
    (root / path).write_text(json.dumps(stamp, indent=1) + "\n", encoding="utf-8")
    return ""


def read_source_debt(source_root: Path, *, staged: bool = False) -> dict[int, list[str]]:
    if staged:
        units = decomp_lint.target_units(True, [])
    elif source_root.is_file():
        units = decomp_lint.target_units(False, [str(source_root)])
    else:
        units = [
            decomp_lint.SourceUnit(
                path, path.read_text(encoding="utf-8", errors="ignore")
            )
            for path in sorted(source_root.rglob("*"))
            if path.suffix in decomp_lint.SOURCE_SUFFIXES
        ]
    findings = decomp_lint.scan_units(units, cross_file=source_root.is_dir())
    findings, _ = decomp_lint.apply_baseline(
        findings, decomp_lint.read_baseline(staged=staged)
    )
    debt: dict[int, list[str]] = {}
    for finding in findings:
        if finding.owner_address and not finding.suppressed:
            debt.setdefault(int(finding.owner_address, 16), []).append(finding.rule)
    return debt


def classify_lint_debt_change(
    head_findings: list[decomp_lint.Finding],
    head_entries: list[decomp_lint.BaselineEntry],
    staged_findings: list[decomp_lint.Finding],
    staged_entries: list[decomp_lint.BaselineEntry],
) -> LintDebtChange:
    head_findings, _ = decomp_lint.apply_baseline(head_findings, head_entries)
    classified, stale = decomp_lint.apply_baseline(staged_findings, staged_entries)

    staged_finding_keys = {finding.baseline_key for finding in staged_findings}
    staged_entry_keys = {entry.key for entry in staged_entries}
    removed = {
        finding.baseline_key
        for finding in head_findings
        if finding.severity == "error"
        and finding.legacy
        and not finding.suppressed
        and finding.baseline_key not in staged_finding_keys
        and finding.baseline_key not in staged_entry_keys
    }
    new_errors = {
        finding.baseline_key
        for finding in classified
        if finding.severity == "error"
        and not finding.legacy
        and not finding.suppressed
    }
    return LintDebtChange(len(removed), len(new_errors), len(stale))


def read_lint_debt_change() -> LintDebtChange:
    return classify_lint_debt_change(
        decomp_lint.scan_units(
            decomp_lint.target_units(False, [], revision="HEAD")
        ),
        decomp_lint.read_baseline(revision="HEAD"),
        decomp_lint.scan_units(decomp_lint.target_units(True, [])),
        decomp_lint.read_baseline(staged=True),
    )


def validate_metadata(
    metadata_path: Path,
    baseline_path: Path,
    baseline_data_path: Path | None = None,
) -> list[str]:
    if not metadata_path.exists():
        return ["baseline metadata is missing; run tools/decomp baseline"]
    try:
        saved = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return ["baseline metadata is invalid; run tools/decomp baseline"]
    current = metadata()
    problems = []
    expected_report = saved.get("baseline_report_sha256")
    if not expected_report or expected_report != file_hash(baseline_path):
        problems.append("baseline report does not match its saved metadata")
    if baseline_data_path is not None:
        expected_data_report = saved.get("baseline_data_report_sha256")
        if (
            not baseline_data_path.exists()
            or not expected_data_report
            or expected_data_report != file_hash(baseline_data_path)
        ):
            problems.append("baseline data report does not match its saved metadata")
    baseline_head = saved.get("git_head")
    if not baseline_head:
        problems.append("baseline git_head does not match the current build context")
    else:
        ancestor = subprocess.run(
            ["git", "merge-base", "--is-ancestor", baseline_head, current["git_head"]],
            cwd=ROOT,
            check=False,
            capture_output=True,
        )
        if ancestor.returncode != 0:
            problems.append("baseline git_head is not an ancestor of the current build context")
    saved_build_context = saved.get("build_context_sha256")
    if saved_build_context is not None:
        if saved_build_context != current.get("build_context_sha256"):
            problems.append(
                "baseline build_context_sha256 does not match the current build context"
            )
    elif saved.get("build_rules_sha256") != current.get("build_rules_sha256"):
        problems.append(
            "baseline build_rules_sha256 does not match the current build context"
        )
    for key in (
        "compiler_driver_sha256",
        "compiler_backend_sha256",
        "sdk_headers_sha256",
        "vc6_headers_sha256",
        "reccmp_git_head",
    ):
        if saved.get(key) != current.get(key):
            problems.append(f"baseline {key} does not match the current build context")
    return problems


def read_baseline_state(metadata_path: Path | None) -> dict[str, object]:
    if metadata_path is None or not metadata_path.exists():
        return {}
    try:
        payload = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return payload if isinstance(payload, dict) else {}


def effective_score(status) -> float:
    return 1.0 if status.exact or status.effective else status.matching


def is_terminal(status, debt: list[str] | None = None) -> bool:
    return bool(status and (status.exact or status.effective) and not debt)


def read_data_report(path: Path | None) -> dict[str, object]:
    """Read a typed-data report, or return an empty report."""

    if path is None or not path.exists():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return payload if isinstance(payload, dict) else {}


def data_variables(payload: dict[str, object]) -> dict[int, dict]:
    group = payload.get("variables", {})
    rows = group.get("variables", []) if isinstance(group, dict) else []
    return {
        int(item["original_address"]): item
        for item in rows
        if isinstance(item, dict) and item.get("original_address") is not None
    }


def unscored_data_variables(payload: dict[str, object]) -> dict[int, dict]:
    group = payload.get("variables", {})
    rows = group.get("unscored_variables", []) if isinstance(group, dict) else []
    return {
        int(item["original_address"]): item
        for item in rows
        if isinstance(item, dict) and item.get("original_address") is not None
    }


def data_target_problem(address: int, payload: dict[str, object]) -> str:
    unscored = unscored_data_variables(payload).get(address)
    if unscored is None:
        return (
            f"0x{address:08X}: data target is unknown. "
            "Use tools/decomp data --limit 10."
        )
    reason = unscored.get("reason")
    if reason == "bss_only":
        return (
            f"0x{address:08X}: data target is BSS-only. "
            "Select an initialized global."
        )
    if reason == "unknown_size":
        return f"0x{address:08X}: data target has no scored type size"
    return f"0x{address:08X}: data target has no scored retail bytes"


def data_sections(payload: dict[str, object]) -> dict[str, dict]:
    group = payload.get("sections", {})
    rows = group.get("sections", []) if isinstance(group, dict) else []
    return {
        str(item["name"]): item
        for item in rows
        if isinstance(item, dict) and item.get("name")
    }


def _regressed(after: dict, before: dict, key: str) -> bool:
    return float(after.get(key, 0)) + 1e-12 < float(before.get(key, 0))


def typed_data_regression_problems(
    baseline: dict[str, object], current: dict[str, object]
) -> list[str]:
    """Return every typed-data variable, section, and evidence-group regression."""

    if not baseline or not current:
        return ["a typed-data report is missing or invalid"]
    problems: list[str] = []
    after_variables = data_variables(current)
    for address, before in data_variables(baseline).items():
        after = after_variables.get(address)
        if after is None:
            problems.append(f"0x{address:08X}: data target disappeared")
        elif _regressed(after, before, "matched_bytes"):
            problems.append(f"0x{address:08X}: data bytes regressed")
        elif _regressed(after, before, "score"):
            problems.append(f"0x{address:08X}: data score regressed")
    after_sections = data_sections(current)
    for name, before in data_sections(baseline).items():
        after = after_sections.get(name)
        if after is None:
            problems.append(f"{name}: scored section disappeared")
        elif _regressed(after, before, "explained_bytes"):
            problems.append(f"{name}: explained section bytes regressed")
        elif _regressed(after, before, "score"):
            problems.append(f"{name}: section score regressed")
    for group_name in ("vtables", "imports", "relocations"):
        before = baseline.get(group_name, {})
        after = current.get(group_name, {})
        key = "explained_bytes" if group_name == "vtables" else "matched_entries"
        if isinstance(before, dict) and isinstance(after, dict) and _regressed(
            after, before, key
        ):
            problems.append(f"{group_name}: data evidence regressed")
    return problems


def validate_resource_campaign(
    baseline_rows: list[dict[str, object]],
    current_rows: list[dict[str, object]],
    resource: ResourceId,
    baseline_data: dict[str, object],
    current_data: dict[str, object],
) -> list[str]:
    """Validate one exact resource leaf and all unrelated scored evidence."""

    problems: list[str] = []
    before_variables = data_variables(baseline_data)
    after_variables = data_variables(current_data)
    for address, old_variable in before_variables.items():
        new_variable = after_variables.get(address)
        if new_variable is None:
            problems.append(f"0x{address:08X}: data target disappeared")
            continue
        if float(new_variable.get("matched_bytes", 0)) + 1e-12 < float(
            old_variable.get("matched_bytes", 0)
        ):
            problems.append(f"0x{address:08X}: data bytes regressed")
        elif float(new_variable.get("score", 0)) + 1e-12 < float(
            old_variable.get("score", 0)
        ):
            problems.append(f"0x{address:08X}: data score regressed")
    before = selected_evidence(baseline_rows, resource)
    after = selected_evidence(current_rows, resource)
    label = ",".join(str(part) for part in resource)
    if not before["leaf_count"]:
        problems.append(f"retail resource {label} does not exist")
    elif after["explained_bytes"] <= before["explained_bytes"]:
        problems.append(f"resource {label} did not improve explained bytes")

    paths = {
        tuple(str(part) for part in row.get("path", []))
        for row in baseline_rows
        if isinstance(row, dict)
    }
    target_path = tuple(str(part) for part in resource)
    for path in sorted(paths):
        if path == target_path:
            continue
        old_explained = sum(
            int(row.get("size", 0))
            for row in baseline_rows
            if tuple(str(part) for part in row.get("path", [])) == path
            and row.get("identity_match") is True
        )
        new_explained = sum(
            int(row.get("size", 0))
            for row in current_rows
            if tuple(str(part) for part in row.get("path", [])) == path
            and row.get("identity_match") is True
        )
        if new_explained < old_explained:
            problems.append(f"resource {','.join(path)}: unrelated resource bytes regressed")

    before_sections = data_sections(baseline_data)
    after_sections = data_sections(current_data)
    for name, old in before_sections.items():
        new = after_sections.get(name)
        if new is None:
            problems.append(f"{name}: scored section disappeared")
            continue
        if float(new.get("explained_bytes", 0)) + 1e-12 < float(
            old.get("explained_bytes", 0)
        ):
            problems.append(f"{name}: explained section bytes regressed")
        elif float(new.get("score", 0)) + 1e-12 < float(old.get("score", 0)):
            problems.append(f"{name}: section score regressed")
    for group_name in ("vtables", "imports", "relocations"):
        old = baseline_data.get(group_name, {})
        new = current_data.get(group_name, {})
        if not isinstance(old, dict) or not isinstance(new, dict):
            continue
        key = "explained_bytes" if group_name == "vtables" else "matched_entries"
        if float(new.get(key, 0)) + 1e-12 < float(old.get(key, 0)):
            problems.append(f"{group_name}: data evidence regressed")
    return problems


def validate_data_campaign(
    baseline_payload: dict[str, object],
    current_payload: dict[str, object],
    targets: set[int],
    accounting_correction: str | None,
) -> list[str]:
    """Validate selected initialized globals and aggregate data evidence."""

    problems: list[str] = []
    if not baseline_payload:
        return ["baseline data report is missing. Run tools/decomp baseline."]
    if not current_payload:
        return ["current data report is missing"]
    if len(targets) > 3:
        problems.append("a data campaign can have at most three targets")

    baseline = data_variables(baseline_payload)
    current = data_variables(current_payload)
    selected_before_matched = 0.0
    selected_after_matched = 0.0
    selected_before_size = 0
    selected_after_size = 0
    for address in sorted(targets):
        before = baseline.get(address)
        after = current.get(address)
        if before is None and after is None:
            if accounting_correction is not None:
                problems.append(
                    f"0x{address:08X}: data target is absent from both reports"
                )
            else:
                problems.append(data_target_problem(address, baseline_payload))
            continue
        if before is None:
            if accounting_correction is not None:
                after_matched = float(after.get("matched_bytes", 0))
                after_size = int(after.get("size", 0))
                selected_after_matched += after_matched
                selected_after_size += after_size
                print(
                    f"0x{address:08X}  data added {after_matched:g}/{after_size} bytes"
                )
                continue
            problems.append(data_target_problem(address, baseline_payload))
            continue
        if after is None:
            if accounting_correction is not None:
                before_matched = float(before.get("matched_bytes", 0))
                before_size = int(before.get("size", 0))
                selected_before_matched += before_matched
                selected_before_size += before_size
                print(
                    f"0x{address:08X}  data removed "
                    f"{before_matched:g}/{before_size} bytes"
                )
                continue
            problems.append(
                f"0x{address:08X}: data target is not scored in the current report"
            )
            continue
        before_matched = float(before.get("matched_bytes", 0))
        after_matched = float(after.get("matched_bytes", 0))
        before_size = int(before.get("size", 0))
        after_size = int(after.get("size", 0))
        before_score = float(before.get("score", 0))
        after_score = float(after.get("score", 0))
        print(
            f"0x{address:08X}  data {before_matched:g}/{before_size} -> "
            f"{after_matched:g}/{after_size} bytes "
            f"({before_score * 100:.2f}% -> {after_score * 100:.2f}%)"
        )
        selected_before_matched += before_matched
        selected_after_matched += after_matched
        selected_before_size += before_size
        selected_after_size += after_size
        if accounting_correction is None:
            if after_matched <= before_matched:
                problems.append(
                    f"0x{address:08X}: data target did not improve explained bytes"
                )
            if after_score + 1e-12 < before_score:
                problems.append(f"0x{address:08X}: data target score regressed")

    if accounting_correction is not None and not problems:
        print(f"Accounting correction: {accounting_correction}")
        if selected_after_matched <= selected_before_matched:
            problems.append("selected data targets did not improve explained bytes")
        before_score = (
            selected_before_matched / selected_before_size
            if selected_before_size
            else 0.0
        )
        after_score = (
            selected_after_matched / selected_after_size
            if selected_after_size
            else 0.0
        )
        if after_score + 1e-12 < before_score:
            problems.append("selected data target score regressed")

    for address, before in baseline.items():
        if address in targets:
            continue
        after = current.get(address)
        if after is None:
            problems.append(f"0x{address:08X}: unrelated data target disappeared")
            continue
        if float(after.get("matched_bytes", 0)) + 1e-12 < float(
            before.get("matched_bytes", 0)
        ):
            problems.append(f"0x{address:08X}: unrelated data bytes regressed")
        elif float(after.get("score", 0)) + 1e-12 < float(
            before.get("score", 0)
        ):
            problems.append(f"0x{address:08X}: unrelated data score regressed")

    before_group = baseline_payload.get("variables", {})
    after_group = current_payload.get("variables", {})
    if not isinstance(before_group, dict) or not isinstance(after_group, dict):
        return problems + ["typed-data totals are missing"]
    before_explained = float(before_group.get("explained_bytes", 0))
    after_explained = float(after_group.get("explained_bytes", 0))
    if after_explained <= before_explained:
        problems.append("initialized-data explained bytes did not improve")

    before_sections = data_sections(baseline_payload)
    after_sections = data_sections(current_payload)
    for name, before in before_sections.items():
        after = after_sections.get(name)
        if after is None:
            problems.append(f"{name}: scored data section disappeared")
            continue
        if float(after.get("explained_bytes", 0)) + 1e-12 < float(
            before.get("explained_bytes", 0)
        ):
            problems.append(f"{name}: explained section bytes regressed")
        elif float(after.get("score", 0)) + 1e-12 < float(
            before.get("score", 0)
        ):
            problems.append(f"{name}: data section score regressed")

    for group_name in ("vtables", "imports", "relocations"):
        before = baseline_payload.get(group_name, {})
        after = current_payload.get(group_name, {})
        if not isinstance(before, dict) or not isinstance(after, dict):
            continue
        if group_name == "vtables":
            before_value = float(before.get("explained_bytes", 0))
            after_value = float(after.get("explained_bytes", 0))
        else:
            before_value = float(before.get("matched_entries", 0))
            after_value = float(after.get("matched_entries", 0))
        if after_value + 1e-12 < before_value:
            problems.append(f"{group_name}: data evidence regressed")
    return problems


VERIFICATION_TAG = {
    "exact": "matched",
    "effective": "effective",
    "tool": "tool",
    "provisional": "provisional",
}


def expected_annotation_tags(report: Path, source_root: Path) -> dict[int, str]:
    statuses = read_match_statuses(report)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    debt = read_source_debt(source_root)
    expected = {}
    for annotation in read_source_annotations(source_root):
        if annotation.kind != "function":
            continue
        address = int(annotation.address, 16)
        status = statuses.get(address)
        tool = status is not None and address in artifacts and is_symbol_only_diff(status)
        verification = verification_status(
            status, tool_artifact=tool, source_clean=address not in debt
        )
        expected[address] = VERIFICATION_TAG.get(verification, "provisional")
    return expected


def check_annotations(report: Path, source_root: Path) -> list[str]:
    expected = expected_annotation_tags(report, source_root)
    problems = []
    for annotation in read_source_annotations(source_root):
        if annotation.kind == "function":
            address = int(annotation.address, 16)
            wanted = expected[address]
            if annotation.tag != wanted:
                actual = annotation.tag or "untagged"
                problems.append(
                    f"0x{address:08X}: annotation is {actual}; fresh verification requires {wanted}"
                )
        elif annotation.kind == "stub" and annotation.tag:
            problems.append(
                f"0x{int(annotation.address, 16):08X}: STUB must not have verification tag {annotation.tag}"
            )
    return problems


def write_annotation_tags(report: Path, source_root: Path) -> int:
    expected = expected_annotation_tags(report, source_root)
    changed = 0
    tag_re = re.compile(r"\s+\[(?:MATCHED|EFFECTIVE|TOOL|PROVISIONAL)\]")
    annotations_by_source: dict[str, list] = {}
    for annotation in read_source_annotations(source_root):
        if annotation.kind == "function":
            annotations_by_source.setdefault(annotation.source, []).append(annotation)
    for source, annotations in annotations_by_source.items():
        path = source_root / source
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines(keepends=True)
        file_changed = False
        for annotation in annotations:
            index = annotation.line - 1
            line = tag_re.sub("", lines[index].rstrip("\r\n"))
            ending = "\r\n" if lines[index].endswith("\r\n") else "\n" if lines[index].endswith("\n") else ""
            tag = expected[int(annotation.address, 16)].upper()
            updated = f"{line} [{tag}]{ending}"
            if updated != lines[index]:
                lines[index] = updated
                changed += 1
                file_changed = True
        if file_changed:
            path.write_text("".join(lines), encoding="utf-8")
    return changed


REGISTER_RE = re.compile(r"\b(?:e?[abcd]x|e?[sd]i|e?[sb]p|[abcd][lh])\b", re.I)
CONTROL_OPCODE_RE = re.compile(r"^(?:j\w+|call|ret|loop\w*)$", re.I)


def experiment_record(report: Path, address: int) -> dict[str, object]:
    status = read_match_statuses(report).get(address)
    if status is None:
        raise ValueError(f"0x{address:08X} is not in {report}")
    normalized = []
    opcode_changes = 0
    register_changes = 0
    stack_changes = 0
    control_changes = 0
    row_count_changes = 0
    for _, groups in status.diff or []:
        for group in groups:
            original = group.get("orig", [])
            recompiled = group.get("recomp", [])
            if len(original) != len(recompiled):
                row_count_changes += 1
            for original_row, recompiled_row in zip(original, recompiled):
                original_text = original_row[1].split("\t", 1)[0].strip()
                recompiled_text = recompiled_row[1].split("\t", 1)[0].strip()
                original_opcode, _ = _instruction_parts_for_record(original_text)
                recompiled_opcode, _ = _instruction_parts_for_record(recompiled_text)
                if original_opcode != recompiled_opcode:
                    opcode_changes += 1
                if REGISTER_RE.findall(original_text) != REGISTER_RE.findall(recompiled_text):
                    register_changes += 1
                if any(word in original_text.lower() or word in recompiled_text.lower() for word in ("ebp", "esp", "push", "pop")):
                    if original_text != recompiled_text:
                        stack_changes += 1
                if CONTROL_OPCODE_RE.match(original_opcode) or CONTROL_OPCODE_RE.match(recompiled_opcode):
                    if original_text != recompiled_text:
                        control_changes += 1
                normalized.append({"original": original_text, "recompiled": recompiled_text})
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    tool = address in artifacts and is_symbol_only_diff(status)
    return {
        "address": f"0x{address:08X}",
        "raw_score": status.matching,
        "reccmp_effective": status.effective,
        "binary_status": verification_status(status, tool_artifact=tool),
        "normalized_diff": normalized,
        "structural_changes": {
            "row_count_groups": row_count_changes,
            "opcode_rows": opcode_changes,
            "register_rows": register_changes,
            "stack_or_frame_rows": stack_changes,
            "control_flow_rows": control_changes,
        },
    }


def _instruction_parts_for_record(text: str) -> tuple[str, str]:
    parts = text.split(None, 1)
    return (parts[0].lower() if parts else "", parts[1] if len(parts) > 1 else "")


def write_experiment_record(report: Path, address: int, output: Path) -> None:
    output.write_text(
        json.dumps(experiment_record(report, address), indent=2) + "\n",
        encoding="utf-8",
    )


def session_summary(baseline_path: Path, current_path: Path, targets: list[int]) -> int:
    baseline = read_match_statuses(baseline_path)
    current = read_match_statuses(current_path)
    try:
        baseline_payload = json.loads(baseline_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        baseline_payload = {"data": []}
    try:
        current_payload = json.loads(current_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        current_payload = {"data": []}
    baseline_stubs = {
        int(str(item["address"]), 16)
        for item in baseline_payload.get("data", [])
        if item.get("stub") and item.get("address") is not None
    }
    selected = [current[address] for address in targets if address in current]
    missing = [address for address in targets if address not in current]
    if missing:
        for address in missing:
            print(f"error: 0x{address:08X} is not in the current report", file=sys.stderr)
        return 1
    scores = [status.matching * 100 for status in selected]
    new_count = sum(
        address not in baseline or address in baseline_stubs for address in targets
    )
    exact_count = sum(status.matching == 1.0 for status in selected)
    before_scores = [
        1.0 if item.get("effective") else float(item.get("matching", 0.0))
        for item in baseline_payload.get("data", [])
        if not item.get("stub") and item.get("matching") is not None
    ]
    after_scores = [
        1.0 if item.get("effective") else float(item.get("matching", 0.0))
        for item in current_payload.get("data", [])
        if not item.get("stub") and item.get("matching") is not None
    ]
    before_accuracy = statistics.fmean(before_scores) if before_scores else 0.0
    after_accuracy = statistics.fmean(after_scores) if after_scores else 0.0
    print(f"Session targets: {len(selected)} ({new_count} new, {exact_count} exact)")
    if scores:
        print(
            f"Target similarity: mean {statistics.fmean(scores):.2f}%, "
            f"median {statistics.median(scores):.2f}%, minimum {min(scores):.2f}%"
        )
    print(
        f"Global effective accuracy: {before_accuracy * 100:.2f}% -> "
        f"{after_accuracy * 100:.2f}% ({(after_accuracy - before_accuracy) * 100:+.2f} points)"
    )
    return 0


def classify(report: Path, address: int, source_root: Path = ROOT / "src") -> int:
    statuses = read_match_statuses(report)
    status = statuses.get(address)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    if status is None:
        print(f"0x{address:08X}: unmatched")
        return 1
    tool = address in artifacts and is_symbol_only_diff(status)
    debt = read_source_debt(source_root).get(address, [])
    verified = verification_status(status, tool_artifact=tool, source_clean=not debt)
    print(
        f"0x{address:08X}: {verified}; raw {status.matching * 100:.2f}%"
        + ("; reccmp effective" if status.effective else "")
        + (f"; source debt: {', '.join(sorted(set(debt)))}" if debt else "")
    )
    if address in artifacts and not tool and verified not in ("exact", "effective"):
        print("error: the tool-artifact row has a non-symbol instruction difference", file=sys.stderr)
        return 1
    return 0


def score(report: Path, addresses: list[int], source_root: Path = ROOT / "src") -> int:
    statuses = read_match_statuses(report)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    failed = False
    source_debt = read_source_debt(source_root)
    for address in addresses:
        status = statuses.get(address)
        if status is None:
            print(f"0x{address:08X}  no comparison result")
            failed = True
            continue
        tool = address in artifacts and is_symbol_only_diff(status)
        debt = source_debt.get(address, [])
        verified = verification_status(status, tool_artifact=tool, source_clean=not debt)
        if verified == "provisional" and debt:
            detail = f"provisional, source debt: {', '.join(sorted(set(debt)))}"
        elif status.effective:
            detail = f"100% effective (raw {status.matching * 100:.2f}%)"
        elif verified == "tool":
            detail = f"tool-only artifact, raw {status.matching * 100:.2f}%"
        elif verified == "exact":
            detail = "exact match"
        else:
            detail = f"{status.matching * 100:.2f}% raw similarity"
        print(f"0x{address:08X}  {detail}")
    return 1 if failed else 0


def dirty_headers(root: Path = ROOT) -> list[str]:
    """Return the headers under src that differ from HEAD, as git names them."""
    command = ["git", "diff", "--name-only", "HEAD", "--", "src"]
    try:
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip().endswith(".h")]


def parse_ninja_deps(text: str) -> dict[str, list[str]]:
    """Map each object in ``ninja -t deps`` output to its recorded dependency paths."""
    deps: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in text.splitlines():
        if line[:1].isspace():
            if current is not None and line.strip():
                current.append(line.strip().replace("\\", "/"))
        elif "#deps" in line:
            current = deps.setdefault(line.split(":", 1)[0].strip(), [])
        else:
            current = None
    return deps


def ninja_deps(build_root: Path = ROOT / "build") -> dict[str, list[str]] | None:
    """Return the recorded ninja dependencies, or None when ninja is unavailable."""
    command = ["ninja", "-C", str(build_root), "-t", "deps"]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return parse_ninja_deps(result.stdout)


def translation_units_including(header: str, deps: dict[str, list[str]] | None) -> str:
    """Count the objects whose recorded dependencies include the header, or '?'."""
    if deps is None:
        return "?"
    suffix = "/" + header
    return str(
        sum(
            1
            for paths in deps.values()
            if any(path == header or path.endswith(suffix) for path in paths)
        )
    )


def header_side_effects(
    baseline: dict[int, MatchStatus], current: dict[int, MatchStatus], excluded: set[int]
) -> list[tuple[int, float, float]]:
    """Functions outside ``excluded`` whose effective score moved between the reports."""
    moved = []
    for address in sorted(baseline):
        after = current.get(address)
        if after is None or address in excluded:
            continue
        before_score, after_score = effective_score(baseline[address]), effective_score(after)
        if abs(after_score - before_score) * 100 >= HEADER_SIDE_EFFECT_MIN_POINTS - 1e-9:
            moved.append((address, before_score, after_score))
    return moved


def report_header_side_effects(
    baseline_path: Path,
    current_path: Path,
    excluded: set[int],
    root: Path = ROOT,
    build_root: Path = ROOT / "build",
) -> int:
    """Print the untouched functions a dirty header moved; information only."""
    moved = header_side_effects(
        read_match_statuses(baseline_path), read_match_statuses(current_path), excluded
    )
    if not moved:
        return 0
    for address, before, after in moved:
        print(f"header side effect: 0x{address:08X} {before * 100:.2f}% -> {after * 100:.2f}%")
    deps = ninja_deps(build_root)
    headers = ", ".join(
        f"{header} ({translation_units_including(header, deps)} translation units)"
        for header in dirty_headers(root)
    )
    print(
        f"header side effect: {len(moved)} untouched function(s) moved; "
        f"dirty headers: {headers or 'none'}"
    )
    return 0


def validate(
    baseline_path: Path,
    current_path: Path,
    targets: set[int],
    allow_target_regression: bool,
    metadata_path: Path | None = None,
    source_root: Path = ROOT / "src",
    check_annotation_tags: bool = True,
    mode: str = "coverage",
    meta_resolution: bool = False,
    baseline_data_path: Path | None = None,
    current_data_path: Path | None = None,
    accounting_correction: str | None = None,
    staged: bool = False,
    resource: ResourceId | None = None,
    functions_map: Path = FUNCTIONS_MAP,
    function_sizes: Path = FUNCTION_SIZES,
) -> int:
    baseline = read_match_statuses(baseline_path)
    current = read_match_statuses(current_path)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    problems: list[str] = []
    if metadata_path is not None:
        problems.extend(
            validate_metadata(
                metadata_path,
                baseline_path,
                baseline_data_path if mode in ("data", "resource") else None,
            )
        )
    source_debt = read_source_debt(source_root, staged=staged)
    lint_change = (
        read_lint_debt_change()
        if staged and mode == "refinement"
        else LintDebtChange()
    )
    current_state = source_state(source_root)
    baseline_state = read_baseline_state(metadata_path)
    baseline_implemented = {
        int(address) for address in baseline_state.get("implemented_addresses", [])
    }
    has_baseline_state = "implemented_addresses" in baseline_state
    current_implemented = {
        int(address) for address in current_state.get("implemented_addresses", [])
    }
    baseline_debt = {
        int(address): list(rules)
        for address, rules in baseline_state.get("source_debt", {}).items()
    }
    if allow_target_regression and not meta_resolution:
        problems.append(
            "--allow-target-regression requires explicit meta-resolution work"
        )
    if accounting_correction is not None and not meta_resolution:
        problems.append("--accounting-correction requires --meta-resolution")
    if accounting_correction is not None and mode != "data":
        problems.append("--accounting-correction requires --mode data")
    if mode in ("coverage", "refinement", "data"):
        if not targets:
            problems.append(f"a {mode} campaign needs at least one target address")
        if resource is not None:
            problems.append("--resource requires --mode resource")
    elif mode == "resource":
        if targets:
            problems.append("a resource campaign cannot have target addresses")
        if resource is None:
            problems.append("a resource campaign needs exactly one --resource")
    if lint_change.new_errors:
        problems.append(
            f"staged source has {lint_change.new_errors} new source-debt error(s)"
        )
    if lint_change.stale_rows:
        problems.append(
            f"staged lint baseline has {lint_change.stale_rows} stale row(s)"
        )
    if check_annotation_tags:
        problems.extend(check_annotations(current_path, source_root))
    for address, before in baseline.items():
        after = current.get(address)
        if after is None:
            problems.append(f"0x{address:08X}: matched function disappeared")
            continue
        before_tool = address in artifacts and is_symbol_only_diff(before)
        after_tool = address in artifacts and is_symbol_only_diff(after)
        before_verified = verification_status(
            before, tool_artifact=before_tool, source_clean=address not in baseline_debt
        )
        after_verified = verification_status(
            after, tool_artifact=after_tool, source_clean=address not in source_debt
        )
        if is_terminal(before, baseline_debt.get(address)) and not is_terminal(
            after, source_debt.get(address)
        ):
            problems.append(
                f"0x{address:08X}: {before_verified} regressed to {after_verified} "
                f"({before.matching * 100:.2f}% -> {after.matching * 100:.2f}%)"
            )
        elif effective_score(after) + 1e-12 < effective_score(before) and (
            address not in targets or not allow_target_regression
        ):
            scope = "target" if address in targets else "untouched function"
            problems.append(
                f"0x{address:08X}: {scope} score regressed "
                f"({before.matching * 100:.2f}% -> {after.matching * 100:.2f}%)"
            )

    function_targets = sorted(targets) if mode in ("coverage", "refinement") else []
    for address in function_targets:
        status = current.get(address)
        if status is None:
            problems.append(f"0x{address:08X}: target is not in the current report")
            continue
        tool = address in artifacts and is_symbol_only_diff(status)
        debt = source_debt.get(address, [])
        verified = verification_status(status, tool_artifact=tool, source_clean=not debt)
        print(
            f"0x{address:08X}  {verified:<11} raw {status.matching * 100:6.2f}%"
            + ("  reccmp-effective" if status.effective else "")
        )
        before = baseline.get(address)
        final_acceptable = (
            status.matching >= 0.5
            or status.exact
            or status.effective
            or (
                mode == "refinement"
                and before is not None
                and before.matching < 0.5
                and status.matching > before.matching + 1e-9
            )
        )
        if not final_acceptable and mode == "coverage":
            # A complete body for a large function is worth keeping below 50
            # percent: it converts a STUB and its refinement continues later.
            accepted, why = provisional_coverage_ok(
                address, status.matching, functions_map, function_sizes
            )
            if accepted:
                print(f"0x{address:08X}  accepted as PROVISIONAL coverage ({why})")
                final_acceptable = True
        if not final_acceptable:
            problems.append(
                f"0x{address:08X}: target finishes below 50% similarity"
            )
        if debt:
            problems.append(
                f"0x{address:08X}: target has source debt: "
                f"{', '.join(sorted(set(debt)))}"
            )

        before_rules = baseline_debt.get(address, [])
        promoted = bool(
            before
            and not is_terminal(before, before_rules)
            and is_terminal(status, debt)
        )
        debt_removed = bool(
            before
            and before_rules
            and not debt
            and (before.exact or before.effective)
            and (status.exact or status.effective)
        )
        improved = bool(
            before
            and status.matching > before.matching + 1e-12
            and final_acceptable
        )

        if mode == "coverage" and not meta_resolution:
            if has_baseline_state and address in baseline_implemented:
                problems.append(
                    f"0x{address:08X}: coverage target was already implemented at baseline"
                )
            if has_baseline_state and address not in current_implemented:
                problems.append(
                    f"0x{address:08X}: coverage target is not a FUNCTION"
                )
        elif mode == "refinement" and not meta_resolution:
            if has_baseline_state and address not in baseline_implemented:
                problems.append(
                    f"0x{address:08X}: refinement target was not implemented at baseline"
                )
            lint_debt_removed = bool(
                lint_change.removed_errors
                and not lint_change.new_errors
                and not lint_change.stale_rows
            )
            if not (improved or promoted or debt_removed or lint_debt_removed):
                problems.append(
                    f"0x{address:08X}: refinement did not improve similarity, "
                    "reach terminal status, or remove tracked source debt"
                )

    if mode == "coverage" and has_baseline_state:
        if len(current_implemented) <= len(baseline_implemented):
            problems.append("coverage did not increase the implemented-function count")

    if mode in ("coverage", "refinement") and (
        baseline_data_path is not None or current_data_path is not None
    ):
        for warning in typed_data_regression_problems(
            read_data_report(baseline_data_path),
            read_data_report(current_data_path),
        ):
            print(f"warning: {warning}", file=sys.stderr)

    if mode in ("data", "resource"):
        if has_baseline_state:
            for address in sorted(baseline_implemented - current_implemented):
                problems.append(
                    f"0x{address:08X}: implemented function disappeared"
                )
        for address, rules in source_debt.items():
            added_rules = set(rules) - set(baseline_debt.get(address, []))
            if added_rules:
                problems.append(
                    f"0x{address:08X}: source debt increased: "
                    f"{', '.join(sorted(added_rules))}"
                )
    if mode == "data":
        baseline_source_hash = baseline_state.get("source_dependency_sha256")
        if accounting_correction is None:
            if baseline_source_hash is None:
                problems.append(
                    "baseline metadata has no source state. Run tools/decomp baseline."
                )
            elif baseline_source_hash == tree_hash(source_root):
                problems.append("data campaign did not change the source tree")
        problems.extend(
            validate_data_campaign(
                read_data_report(baseline_data_path),
                read_data_report(current_data_path),
                targets,
                accounting_correction,
            )
        )

    if mode == "resource" and resource is not None:
        baseline_rows = baseline_state.get("resources", [])
        if not isinstance(baseline_rows, list):
            problems.append("baseline metadata has no resource evidence")
            baseline_rows = []
        original = ROOT / "original" / "toy2.exe"
        recompiled = ROOT / "build" / "toy2.exe"
        if not original.exists() or not recompiled.exists():
            problems.append("the resource executable input is missing")
            current_rows: list[dict[str, object]] = []
        else:
            current_rows = resource_rows(original, recompiled)
        problems.extend(
            validate_resource_campaign(
                baseline_rows,
                current_rows,
                resource,
                read_data_report(baseline_data_path),
                read_data_report(current_data_path),
            )
        )
        if staged:
            baseline_sources = baseline_state.get("resource_sources")
            if not isinstance(baseline_sources, dict):
                problems.append("baseline metadata has no resource source state")
            else:
                problems.extend(staged_resource_source_problems(ROOT, baseline_sources))

    newly_implemented = (
        current_implemented - baseline_implemented if has_baseline_state else set()
    )
    for address in sorted(newly_implemented - targets):
        status = current.get(address)
        if status is None or not (
            status.matching >= 0.5 or status.exact or status.effective
        ):
            problems.append(
                f"0x{address:08X}: newly integrated function finishes below 50% similarity"
            )
        if source_debt.get(address):
            problems.append(
                f"0x{address:08X}: newly integrated function has source debt: "
                f"{', '.join(sorted(set(source_debt[address])))}"
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


SCOREBOARD_HEADER = "address\tsize\tmatching\texact\teffective\tdebt"


def reccmp_short_head() -> str:
    """Return the short reccmp submodule revision, or "unknown"."""

    submodule = ROOT / "external" / "submodules" / "reccmp"
    if not (submodule / ".git").exists():
        return "unknown"
    result = subprocess.run(
        ["git", "-C", str(submodule), "rev-parse", "--short", "HEAD"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    head = result.stdout.strip()
    return head if result.returncode == 0 and head else "unknown"


def scoreboard_rows(
    report: Path, functions_map: Path, function_sizes: Path, source_root: Path
) -> list[tuple[int, int, float, bool, bool, int]]:
    """Score every mapped function as (address, size, matching, exact, effective, debt).

    A STUB or a function absent from the report scores 0. The size is the Ghidra
    snapshot size when it is at least one byte, else the gap to the next map address.
    """

    import decomp_utils  # noqa: PLC0415  (decomp_utils imports this module)

    statuses = read_match_statuses(report)
    state = source_state(source_root)
    stubs = set(state["stub_addresses"])
    debt = {int(address): len(rules) for address, rules in state["source_debt"].items()}
    sizes = decomp_utils.read_mapped_sizes(functions_map, function_sizes)
    rows = []
    for address_text in decomp_utils.parse_functions_map(functions_map):
        address = int(address_text, 16)
        status = None if address in stubs else statuses.get(address)
        exact = bool(status and status.exact)
        effective = bool(status and status.effective)
        matching = 1.0 if exact or effective else (status.matching if status else 0.0)
        rows.append(
            (address, sizes.get(address, 0), matching, exact, effective, debt.get(address, 0))
        )
    return rows


def write_scoreboard(
    report: Path,
    output: Path,
    functions_map: Path,
    function_sizes: Path,
    source_root: Path,
    generated_by: str,
) -> None:
    """Write the shared per-function scoreboard TSV."""

    lines = [f"# reccmp_head={reccmp_short_head()} generated_by={generated_by}", SCOREBOARD_HEADER]
    for address, size, matching, exact, effective, debt in scoreboard_rows(
        report, functions_map, function_sizes, source_root
    ):
        lines.append(
            f"0x{address:08X}\t{size}\t{matching:.6f}\t{int(exact)}\t{int(effective)}\t{debt}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_address(value: str) -> int:
    return int(value, 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    metadata_parser = subparsers.add_parser("metadata")
    metadata_parser.add_argument("output", type=Path)
    metadata_parser.add_argument("--report", type=Path)
    metadata_parser.add_argument("--data-report", type=Path)

    classify_parser = subparsers.add_parser("classify")
    classify_parser.add_argument("report", type=Path)
    classify_parser.add_argument("address", type=parse_address)
    classify_parser.add_argument("--source-root", type=Path, default=ROOT / "src")

    score_parser = subparsers.add_parser("score")
    score_parser.add_argument("report", type=Path, nargs="?")
    score_parser.add_argument("addresses", nargs="*", type=parse_address)
    score_parser.add_argument("--source-root", type=Path, default=ROOT / "src")
    score_parser.add_argument(
        "--changed", type=Path, help="current report to compare against --baseline"
    )
    score_parser.add_argument("--baseline", type=Path, help="report to compare --changed with")
    score_parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        type=parse_address,
        help="campaign target to leave out of the --changed comparison",
    )

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("baseline", type=Path)
    validate_parser.add_argument("current", type=Path)
    validate_parser.add_argument("targets", nargs="*", type=parse_address)
    validate_parser.add_argument("--allow-target-regression", action="store_true")
    validate_parser.add_argument(
        "--mode", choices=("coverage", "refinement", "data", "resource"), default="coverage"
    )
    validate_parser.add_argument("--resource", type=parse_resource)
    validate_parser.add_argument("--meta-resolution", action="store_true")
    validate_parser.add_argument("--baseline-data", type=Path)
    validate_parser.add_argument("--current-data", type=Path)
    validate_parser.add_argument("--accounting-correction")
    validate_parser.add_argument("--metadata", type=Path)
    validate_parser.add_argument("--source-root", type=Path, default=ROOT / "src")
    validate_parser.add_argument("--skip-annotation-check", action="store_true")
    validate_parser.add_argument("--staged", action="store_true")

    annotations_parser = subparsers.add_parser("annotations")
    annotations_parser.add_argument("report", type=Path)
    annotations_parser.add_argument("--source-root", type=Path, default=ROOT / "src")
    annotations_parser.add_argument("--write", action="store_true")

    experiment_parser = subparsers.add_parser("experiment")
    experiment_parser.add_argument("report", type=Path)
    experiment_parser.add_argument("address", type=parse_address)
    experiment_parser.add_argument("output", type=Path)

    session_parser = subparsers.add_parser("session-summary")
    session_parser.add_argument("baseline", type=Path)
    session_parser.add_argument("current", type=Path)
    session_parser.add_argument("targets", nargs="+", type=parse_address)

    scoreboard_parser = subparsers.add_parser("scoreboard")
    scoreboard_parser.add_argument("report", type=Path)
    scoreboard_parser.add_argument("output", type=Path)
    scoreboard_parser.add_argument(
        "--functions-map",
        type=Path,
        default=ROOT / "tools" / "Resources" / "functions_map.txt",
    )
    scoreboard_parser.add_argument(
        "--function-sizes",
        type=Path,
        default=ROOT / "build" / "decomp-function-sizes.json",
    )
    scoreboard_parser.add_argument("--source-root", type=Path, default=ROOT / "src")
    scoreboard_parser.add_argument("--generated-by", default="report")

    stamp_parser = subparsers.add_parser("stamp", help="write or check the report stamp")
    stamp_parser.add_argument("path", type=Path)
    stamp_parser.add_argument("--report", type=Path, action="append", default=[])
    stamp_action = stamp_parser.add_mutually_exclusive_group()
    stamp_action.add_argument("--check", action="store_true",
                              help="exit 1 and say why when the stamped reports (each --report) may be stale")
    stamp_action.add_argument("--advance", action="store_true",
                              help="move the stamp to a new commit of the same tree")
    stamp_parser.add_argument("--ignore-index", action="store_true",
                              help="with --check: a changed index does not change the reports")

    args = parser.parse_args()
    if args.command == "metadata":
        write_metadata(args.output, args.report, args.data_report)
        return 0
    if args.command == "stamp":
        if args.check or args.advance:
            problem = advance_report_stamp(args.path) if args.advance else report_stamp_problem(
                args.path, exempt=("index_tree",) if args.ignore_index else (), required=tuple(args.report))
            if problem:
                print(f"stamp: {problem}")
            return 1 if problem else 0
        if not args.report:
            stamp_parser.error("stamp needs --report FILE, --check or --advance")
        write_report_stamp(args.path, args.report)
        return 0
    if args.command == "classify":
        return classify(args.report, args.address, args.source_root)
    if args.command == "score":
        if args.changed is not None or args.baseline is not None:
            if args.changed is None or args.baseline is None:
                score_parser.error("--changed and --baseline go together")
            return report_header_side_effects(args.baseline, args.changed, set(args.exclude))
        if args.report is None or not args.addresses:
            score_parser.error("score needs a report and at least one address")
        return score(args.report, args.addresses, args.source_root)
    if args.command == "annotations":
        if args.write:
            changed = write_annotation_tags(args.report, args.source_root)
            print(f"updated {changed} FUNCTION annotation tag(s)")
        problems = check_annotations(args.report, args.source_root)
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1 if problems else 0
    if args.command == "experiment":
        write_experiment_record(args.report, args.address, args.output)
        return 0
    if args.command == "session-summary":
        return session_summary(args.baseline, args.current, args.targets)
    if args.command == "scoreboard":
        write_scoreboard(
            args.report,
            args.output,
            args.functions_map,
            args.function_sizes,
            args.source_root,
            args.generated_by,
        )
        return 0
    return validate(
        args.baseline,
        args.current,
        set(args.targets),
        args.allow_target_regression,
        args.metadata,
        args.source_root,
        not args.skip_annotation_check,
        args.mode,
        args.meta_resolution,
        args.baseline_data,
        args.current_data,
        args.accounting_correction,
        args.staged,
        args.resource,
    )


if __name__ == "__main__":
    raise SystemExit(main())
