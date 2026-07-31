#!/usr/bin/env python3
"""Compare decompilation reports and enforce verification-state regressions."""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
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
from tools import decomp_lint  # noqa: E402
from tools.decomp_annotations import read_source_annotations  # noqa: E402

TOOL_ARTIFACTS = ROOT / "tools" / "Resources" / "tool_artifacts.tsv"
AUDIT_LEDGER = ROOT / "tools" / "Resources" / "audit-ledger.tsv"


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


def metadata(report: Path | None = None) -> dict[str, str]:
    head = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=True
    ).stdout.strip()
    values = {"git_head": head}
    for name, path in (
        ("build_rules_sha256", ROOT / "build" / "build.ninja"),
        ("recompiled_sha256", ROOT / "build" / "toy2.exe"),
        ("compiler_driver_sha256", ROOT / ".tooling" / "msvc600-8168" / "VC98" / "Bin" / "CL.EXE"),
        ("compiler_backend_sha256", ROOT / ".tooling" / "msvc600-8168" / "VC98" / "Bin" / "C1XX.DLL"),
        ("cmake_flags_sha256", ROOT / "CMakeLists.txt"),
    ):
        if path.exists():
            values[name] = file_hash(path)
    if report is not None and report.exists():
        values["baseline_report_sha256"] = file_hash(report)
    values["sdk_headers_sha256"] = tree_hash(ROOT / "external" / "include")
    values["vc6_headers_sha256"] = tree_hash(
        ROOT / ".tooling" / "msvc600-8168" / "VC98" / "Include"
    )
    values["source_dependency_sha256"] = tree_hash(ROOT / "src")
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


def write_metadata(path: Path, report: Path | None = None) -> None:
    path.write_text(json.dumps(metadata(report), indent=2) + "\n", encoding="utf-8")


def read_source_debt(source_root: Path) -> dict[int, list[str]]:
    units = decomp_lint.target_units(False, [str(source_root)]) if source_root.is_file() else [
        decomp_lint.SourceUnit(path, path.read_text(encoding="utf-8", errors="ignore"))
        for path in sorted(source_root.rglob("*"))
        if path.suffix in decomp_lint.SOURCE_SUFFIXES
    ]
    findings = decomp_lint.scan_units(units, cross_file=source_root.is_dir())
    findings, _ = decomp_lint.apply_baseline(findings, decomp_lint.read_baseline())
    debt: dict[int, list[str]] = {}
    for finding in findings:
        if finding.owner_address and not finding.suppressed:
            debt.setdefault(int(finding.owner_address, 16), []).append(finding.rule)
    return debt


def validate_metadata(metadata_path: Path, baseline_path: Path) -> list[str]:
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
    for key in (
        "build_rules_sha256",
        "compiler_driver_sha256",
        "compiler_backend_sha256",
        "cmake_flags_sha256",
        "sdk_headers_sha256",
        "vc6_headers_sha256",
        "reccmp_git_head",
    ):
        if saved.get(key) != current.get(key):
            problems.append(f"baseline {key} does not match the current build context")
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


def write_audit_ledger(report: Path, source_root: Path, output: Path) -> None:
    statuses = read_match_statuses(report)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    debt = read_source_debt(source_root)
    legacy = {}
    legacy_path = ROOT / ".notes" / "caps-registry.tsv"
    if legacy_path.exists():
        with legacy_path.open(encoding="utf-8", newline="") as handle:
            for row in csv.reader(handle, delimiter="\t"):
                if row and not row[0].startswith("#"):
                    legacy[int(row[0], 16)] = row
    existing = {}
    if output.exists():
        with output.open(encoding="utf-8", newline="") as handle:
            for row in csv.reader(handle, delimiter="\t"):
                if row and not row[0].startswith("#"):
                    existing[int(row[0], 16)] = row
    rows = []
    for annotation in read_source_annotations(source_root):
        if annotation.kind != "function":
            continue
        address = int(annotation.address, 16)
        status = statuses.get(address)
        tool = status is not None and address in artifacts and is_symbol_only_diff(status)
        verification = verification_status(
            status, tool_artifact=tool, source_clean=address not in debt
        )
        scopes = []
        if address in legacy:
            scopes.append("former-cap")
        if status is not None and status.matching < 0.5:
            scopes.append("sub-50")
        if status is not None and (status.matching == 1.0 or status.effective or tool) and address in debt:
            scopes.append("verified-debt")
        if verification != "provisional" and not scopes:
            continue
        old = legacy.get(address, [])
        previous = existing.get(address, [])
        origin = previous[5] if len(previous) > 5 else (
            old[1] if len(old) > 1 else "initial-audit"
        )
        uncertainty = previous[6] if len(previous) > 6 else (
            old[4] if len(old) > 4 else "binary or source model is not verified"
        )
        trigger = previous[7] if len(previous) > 7 else ""
        if not trigger or trigger == "recheck ABI, layout, control flow, and natural source forms":
            if old:
                trigger = (
                    "revisit if new caller, type, or source-form evidence explains the "
                    f"recorded {old[1]} mismatch"
                )
            else:
                trigger = "recheck ABI, layout, control flow, and natural source forms"
        audit_state = previous[12] if len(previous) > 12 else (
            "audited" if old and uncertainty != "binary or source model is not verified" else "pending"
        )
        rows.append(
            (
                f"0x{address:08X}",
                verification,
                "unmatched" if status is None else status.binary_status,
                "-" if status is None else f"{status.matching * 100:.2f}",
                ",".join(sorted(set(debt.get(address, [])))) or "clean",
                origin,
                uncertainty,
                trigger,
                previous[8] if len(previous) > 8 else (old[2] if len(old) > 2 else "-"),
                previous[9] if len(previous) > 9 else "-",
                previous[10] if len(previous) > 10 else "-",
                previous[11] if len(previous) > 11 else "-",
                audit_state,
                ",".join(scopes) or "-",
                previous[14] if len(previous) > 14 else "-",
            )
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            (
                "# address", "status", "binary", "raw-score", "source-debt",
                "origin", "uncertainty", "revisit-trigger", "tested-scores",
                "before-score", "after-score", "eliminated-source-defect",
                "audit-state", "freeze-scope",
                "tested-forms",
            )
        )
        writer.writerows(sorted(rows))


def audit_status(report: Path, source_root: Path, ledger: Path) -> dict:
    statuses = read_match_statuses(report)
    debt = read_source_debt(source_root)
    legacy_path = ROOT / ".notes" / "caps-registry.tsv"
    legacy = set()
    if legacy_path.exists():
        with legacy_path.open(encoding="utf-8", newline="") as handle:
            for row in csv.reader(handle, delimiter="\t"):
                if row and not row[0].startswith("#"):
                    legacy.add(int(row[0], 16))
    records = {}
    if ledger.exists():
        with ledger.open(encoding="utf-8", newline="") as handle:
            for row in csv.reader(handle, delimiter="\t"):
                if row and not row[0].startswith("#"):
                    records[int(row[0], 16)] = row
    required = {}
    for annotation in read_source_annotations(source_root):
        if annotation.kind != "function":
            continue
        address = int(annotation.address, 16)
        status = statuses.get(address)
        scopes = []
        if address in legacy:
            scopes.append("former-cap")
        if status is not None and status.matching < 0.5:
            scopes.append("sub-50")
        if status is not None and (status.matching == 1.0 or status.effective) and address in debt:
            scopes.append("verified-debt")
        if scopes:
            required[address] = scopes
    pending = {}
    for address, scopes in required.items():
        row = records.get(address, [])
        state = row[12] if len(row) > 12 else "pending"
        origin = row[5] if len(row) > 5 else ""
        uncertainty = row[6] if len(row) > 6 else ""
        trigger = row[7] if len(row) > 7 else ""
        tested_scores = row[8] if len(row) > 8 else ""
        if (
            state != "audited"
            or origin in ("", "initial-audit")
            or not uncertainty
            or uncertainty == "binary or source model is not verified"
            or len(uncertainty.split()) < 8
            or not trigger
            or trigger == "recheck ABI, layout, control flow, and natural source forms"
            or len(trigger.split()) < 8
            or not re.search(r"\b(?:if|when)\b", trigger, re.IGNORECASE)
            or tested_scores in ("", "-")
        ):
            pending[address] = scopes
    return {
        "required": len(required),
        "audited": len(required) - len(pending),
        "pending": len(pending),
        "pending_functions": [
            {"address": f"0x{address:08X}", "scope": scopes}
            for address, scopes in sorted(pending.items())
        ],
    }


def readability_audit_problems(
    ledger: Path, targets: set[int], baseline: dict, current: dict
) -> list[str]:
    records = {}
    if ledger.exists():
        with ledger.open(encoding="utf-8", newline="") as handle:
            for row in csv.reader(handle, delimiter="\t"):
                if row and not row[0].startswith("#"):
                    records[int(row[0], 16)] = row
    problems = []
    for address in targets:
        before = baseline.get(address)
        after = current.get(address)
        if before is None or after is None or after.matching >= before.matching:
            continue
        row = records.get(address)
        if row is None or len(row) < 12:
            problems.append(f"0x{address:08X}: readability regression has no audit-ledger row")
            continue
        expected_before = f"{before.matching * 100:.2f}"
        expected_after = f"{after.matching * 100:.2f}"
        if row[9] != expected_before or row[10] != expected_after or not row[11].strip():
            problems.append(
                f"0x{address:08X}: audit ledger must record before {expected_before}, "
                f"after {expected_after}, and the eliminated source defect"
            )
    return problems


def low_score_review_problems(
    ledger: Path,
    targets: set[int],
    baseline: dict,
    current: dict,
    artifacts: dict[int, str],
    allow_low_score: bool,
    new_targets: set[int] | None = None,
) -> list[str]:
    records = {}
    if ledger.exists():
        with ledger.open(encoding="utf-8", newline="") as handle:
            for row in csv.reader(handle, delimiter="\t"):
                if row and not row[0].startswith("#"):
                    records[int(row[0], 16)] = row

    problems = []
    for address in sorted(targets):
        before = baseline.get(address)
        status = current.get(address)
        if status is None:
            continue
        tool = address in artifacts and is_symbol_only_diff(status)
        if status.matching >= 0.75 or status.matching == 1.0 or status.effective or tool:
            continue
        is_new = (
            address in new_targets
            if new_targets is not None
            else before is None or before.matching == 0.0
        )
        if not is_new:
            continue
        if not allow_low_score:
            problems.append(
                f"0x{address:08X}: new target score {status.matching * 100:.2f}% is below 75%. "
                "Keep the function as STUB or get a maintainer review"
            )
            continue

        row = records.get(address, [])
        origin = row[5].strip() if len(row) > 5 else ""
        uncertainty = row[6].strip() if len(row) > 6 else ""
        trigger = row[7].strip() if len(row) > 7 else ""
        tested_scores = row[8].strip() if len(row) > 8 else ""
        audit_state = row[12].strip() if len(row) > 12 else ""
        tested_forms = row[14].strip() if len(row) > 14 else ""
        if (
            origin != "maintainer-review"
            or audit_state != "audited"
            or len(uncertainty.split()) < 8
            or len(trigger.split()) < 8
            or not re.search(r"\b(?:if|when)\b", trigger, re.IGNORECASE)
            or tested_scores in ("", "-")
            or tested_forms in ("", "-")
        ):
            problems.append(
                f"0x{address:08X}: low-score approval needs an audited ledger row with "
                "origin maintainer-review, measured scores, tested forms, uncertainty, "
                "and a revisit trigger"
            )
    return problems


def new_provisional_ledger_problems(
    ledger: Path,
    targets: set[int],
    baseline: dict,
    current: dict,
    artifacts: dict[int, str],
    require_staged: bool = False,
    new_targets: set[int] | None = None,
) -> list[str]:
    records = {}
    staged_error = False
    if require_staged:
        try:
            relative = ledger.resolve().relative_to(ROOT.resolve()).as_posix()
            staged_diff = subprocess.run(
                ["git", "diff", "--cached", "--quiet", "--", relative],
                cwd=ROOT,
                capture_output=True,
                check=False,
            )
            if staged_diff.returncode != 1:
                raise ValueError("the audit ledger has no staged change")
            content = subprocess.run(
                ["git", "show", f":{relative}"],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=True,
            ).stdout
            rows = csv.reader(content.splitlines(), delimiter="\t")
            for row in rows:
                if row and not row[0].startswith("#"):
                    records[int(row[0], 16)] = row
        except (OSError, ValueError, subprocess.CalledProcessError):
            staged_error = True
    elif ledger.exists():
        with ledger.open(encoding="utf-8", newline="") as handle:
            for row in csv.reader(handle, delimiter="\t"):
                if row and not row[0].startswith("#"):
                    records[int(row[0], 16)] = row

    problems = []
    for address in sorted(targets):
        before = baseline.get(address)
        status = current.get(address)
        is_new = (
            address in new_targets
            if new_targets is not None
            else before is None or before.matching == 0.0
        )
        if status is None or not is_new:
            continue
        tool = address in artifacts and is_symbol_only_diff(status)
        if status.matching == 1.0 or status.effective or tool:
            continue
        if staged_error:
            problems.append(
                f"0x{address:08X}: stage the audit ledger with the new provisional target"
            )
            continue
        row = records.get(address, [])
        score = row[3].strip() if len(row) > 3 else ""
        origin = row[5].strip() if len(row) > 5 else ""
        uncertainty = row[6].strip() if len(row) > 6 else ""
        trigger = row[7].strip() if len(row) > 7 else ""
        tested_scores = row[8].strip() if len(row) > 8 else ""
        audit_state = row[12].strip() if len(row) > 12 else ""
        tested_forms = row[14].strip() if len(row) > 14 else ""
        if (
            score != f"{status.matching * 100:.2f}"
            or origin in ("", "initial-audit")
            or audit_state != "audited"
            or len(uncertainty.split()) < 8
            or len(trigger.split()) < 8
            or not re.search(r"\b(?:if|when)\b", trigger, re.IGNORECASE)
            or tested_scores in ("", "-")
            or tested_forms in ("", "-")
        ):
            problems.append(
                f"0x{address:08X}: new provisional target needs an audited ledger row "
                "with its current score, specific uncertainty, tested forms, measured "
                "scores, and a conditional revisit trigger"
            )
    return problems


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


def head_function_addresses() -> set[int]:
    result = subprocess.run(
        ["git", "grep", "-h", "-E", r"FUNCTION:.*0x[0-9A-Fa-f]{8}", "HEAD", "--", "src"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode not in (0, 1):
        raise RuntimeError("git could not read the baseline source annotations")
    return {
        int(match.group(0), 16)
        for match in re.finditer(r"0x[0-9A-Fa-f]{8}", result.stdout)
    }


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
    audit_ledger: Path = AUDIT_LEDGER,
    allow_low_score: bool = False,
    require_staged_ledger: bool = False,
) -> int:
    baseline = read_match_statuses(baseline_path)
    current = read_match_statuses(current_path)
    artifacts = read_tool_artifacts(TOOL_ARTIFACTS)
    problems: list[str] = []
    if metadata_path is not None:
        problems.extend(validate_metadata(metadata_path, baseline_path))
    source_debt = read_source_debt(source_root)
    new_targets = targets - head_function_addresses() if require_staged_ledger else None
    if check_annotation_tags:
        problems.extend(check_annotations(current_path, source_root))
    if allow_target_regression:
        problems.extend(readability_audit_problems(audit_ledger, targets, baseline, current))
    problems.extend(
        low_score_review_problems(
            audit_ledger,
            targets,
            baseline,
            current,
            artifacts,
            allow_low_score,
            new_targets,
        )
    )
    problems.extend(
        new_provisional_ledger_problems(
            audit_ledger,
            targets,
            baseline,
            current,
            artifacts,
            require_staged_ledger,
            new_targets,
        )
    )

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
        debt = source_debt.get(address, [])
        verified = verification_status(status, tool_artifact=tool, source_clean=not debt)
        print(
            f"0x{address:08X}  {verified:<11} raw {status.matching * 100:6.2f}%"
            + ("  reccmp-effective" if status.effective else "")
        )
        if debt:
            problems.append(
                f"0x{address:08X}: target has source debt: {', '.join(sorted(set(debt)))}"
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
    validate_parser.add_argument("--allow-low-score", action="store_true")
    validate_parser.add_argument("--metadata", type=Path)
    validate_parser.add_argument("--source-root", type=Path, default=ROOT / "src")
    validate_parser.add_argument("--skip-annotation-check", action="store_true")
    validate_parser.add_argument("--audit-ledger", type=Path, default=AUDIT_LEDGER)
    validate_parser.add_argument("--require-staged-ledger", action="store_true")

    annotations_parser = subparsers.add_parser("annotations")
    annotations_parser.add_argument("report", type=Path)
    annotations_parser.add_argument("--source-root", type=Path, default=ROOT / "src")
    annotations_parser.add_argument("--write", action="store_true")

    experiment_parser = subparsers.add_parser("experiment")
    experiment_parser.add_argument("report", type=Path)
    experiment_parser.add_argument("address", type=parse_address)
    experiment_parser.add_argument("output", type=Path)

    ledger_parser = subparsers.add_parser("ledger")
    ledger_parser.add_argument("report", type=Path)
    ledger_parser.add_argument("output", type=Path, nargs="?", default=AUDIT_LEDGER)
    ledger_parser.add_argument("--source-root", type=Path, default=ROOT / "src")

    audit_status_parser = subparsers.add_parser("audit-status")
    audit_status_parser.add_argument("report", type=Path)
    audit_status_parser.add_argument("--source-root", type=Path, default=ROOT / "src")
    audit_status_parser.add_argument("--audit-ledger", type=Path, default=AUDIT_LEDGER)
    audit_status_parser.add_argument("--check", action="store_true")

    session_parser = subparsers.add_parser("session-summary")
    session_parser.add_argument("baseline", type=Path)
    session_parser.add_argument("current", type=Path)
    session_parser.add_argument("targets", nargs="+", type=parse_address)

    args = parser.parse_args()
    if args.command == "metadata":
        write_metadata(args.output, args.report)
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
    if args.command == "ledger":
        write_audit_ledger(args.report, args.source_root, args.output)
        return 0
    if args.command == "audit-status":
        result = audit_status(args.report, args.source_root, args.audit_ledger)
        print(json.dumps(result, indent=2))
        return 1 if args.check and result["pending"] else 0
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
        args.audit_ledger,
        args.allow_low_score,
        args.require_staged_ledger,
    )


if __name__ == "__main__":
    raise SystemExit(main())
