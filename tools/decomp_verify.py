#!/usr/bin/env python3
"""Compare decompilation reports and enforce verification-state regressions."""

from __future__ import annotations

import argparse
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
    is_symbol_only_diff,
    read_match_statuses,
    read_tool_artifacts,
    verification_status,
)
from tools import decomp_lint  # noqa: E402
from tools.decomp_annotations import read_source_annotations  # noqa: E402

TOOL_ARTIFACTS = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"


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


def metadata(
    report: Path | None = None, data_report: Path | None = None
) -> dict[str, object]:
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=True
    ).stdout.strip()
    values = {"git_head": head}
    for name, path in (
        ("build_rules_sha256", ROOT / "build" / "build.ninja"),
        ("recompiled_sha256", ROOT / "build" / "toy2.exe"),
        ("compiler_driver_sha256", ROOT / ".tooling" / "msvc600-8168" / "VC98" / "Bin" / "CL.EXE"),
        ("compiler_backend_sha256", ROOT / ".tooling" / "msvc600-8168" / "VC98" / "Bin" / "C1XX.DLL"),
    ):
        if path.exists():
            values[name] = file_hash(path)
    values["build_context_sha256"] = normalized_build_context(ROOT / "build")
    if report is not None and report.exists():
        values["baseline_report_sha256"] = file_hash(report)
    if data_report is not None and data_report.exists():
        values["baseline_data_report_sha256"] = file_hash(data_report)
    values["sdk_headers_sha256"] = tree_hash(ROOT / "external" / "include")
    values["vc6_headers_sha256"] = tree_hash(
        ROOT / ".tooling" / "msvc600-8168" / "VC98" / "Include"
    )
    values["source_dependency_sha256"] = tree_hash(ROOT / "src")
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
                baseline_data_path if mode == "data" else None,
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

    function_targets = sorted(targets) if mode != "data" else []
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
        final_acceptable = status.matching >= 0.5 or status.exact or status.effective
        if not final_acceptable:
            problems.append(
                f"0x{address:08X}: target finishes below 50% similarity"
            )
        if debt:
            problems.append(
                f"0x{address:08X}: target has source debt: "
                f"{', '.join(sorted(set(debt)))}"
            )

        before = baseline.get(address)
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

    if mode == "data":
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
    score_parser.add_argument("report", type=Path)
    score_parser.add_argument("addresses", nargs="+", type=parse_address)
    score_parser.add_argument("--source-root", type=Path, default=ROOT / "src")

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("baseline", type=Path)
    validate_parser.add_argument("current", type=Path)
    validate_parser.add_argument("targets", nargs="+", type=parse_address)
    validate_parser.add_argument("--allow-target-regression", action="store_true")
    validate_parser.add_argument(
        "--mode", choices=("coverage", "refinement", "data"), default="coverage"
    )
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

    args = parser.parse_args()
    if args.command == "metadata":
        write_metadata(args.output, args.report, args.data_report)
        return 0
    if args.command == "classify":
        return classify(args.report, args.address, args.source_root)
    if args.command == "score":
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
    )


if __name__ == "__main__":
    raise SystemExit(main())
