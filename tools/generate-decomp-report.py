#!/usr/bin/env python3
"""Build the self-contained Toy Story 2 decompilation dashboard."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from decomp_annotations import canonical_address, read_source_annotations
import decomp_lint

SUMMARY_RE = {
    "implemented": re.compile(r"Implemented:\s+[\d.]+%\s+\((\d+)\s*/\s*(\d+)\)"),
    "accuracy": re.compile(r"Accuracy:\s+([\d.]+)%"),
    "progress": re.compile(r"Progress:\s+([\d.]+)%"),
}


def read_annotations(source_root: Path) -> dict[str, dict[str, str]]:
    annotations: dict[str, dict[str, str]] = {}
    for annotation in read_source_annotations(source_root):
        if annotation.kind == "global":
            continue
        annotations.setdefault(
            annotation.address,
            {
                "kind": annotation.kind,
                "source": annotation.source,
                "line": annotation.line,
            },
        )
    return annotations


def read_function_map(path: Path | None) -> tuple[dict[str, str], dict[str, int]]:
    if path is None or not path.exists():
        return {}, {}
    names: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = re.match(r"\s*(?:0x)?([0-9a-fA-F]{6,8})(?:\s+(.+?))?\s*$", line)
        if match:
            names[canonical_address(f"0x{match.group(1)}")] = (
                match.group(2) or "Unknown function"
            )
    ordered = sorted(int(address, 16) for address in names)
    spans = {
        canonical_address(hex(address)): next_address - address
        for address, next_address in zip(ordered, ordered[1:])
    }
    return names, spans


def read_function_sizes(path: Path | None) -> dict[str, int]:
    """Read original function extents exported by the Ghidra function table."""

    if path is None or not path.exists():
        return {}
    sizes = {}
    for item in json.loads(path.read_text(encoding="utf-8-sig")):
        address = item.get("address") or item.get("entry_point")
        size = item.get("size")
        if address and isinstance(size, int) and size > 0:
            sizes[canonical_address(f"0x{address}")] = size
    return sizes


def read_lint_quality(source_root: Path) -> tuple[dict[str, list[dict]], dict[str, int]]:
    units = [
        decomp_lint.SourceUnit(path, path.read_text(encoding="utf-8", errors="ignore"))
        for path in sorted(source_root.rglob("*"))
        if path.suffix in decomp_lint.SOURCE_SUFFIXES
    ]
    findings = decomp_lint.scan_units(units)
    findings, stale = decomp_lint.apply_baseline(findings, decomp_lint.read_baseline())
    by_address: dict[str, list[dict]] = {}
    for finding in findings:
        if finding.owner_address and not finding.suppressed:
            by_address.setdefault(finding.owner_address, []).append(finding.to_json())
    summary = {
        "quality_errors": sum(item.severity == "error" and not item.suppressed for item in findings),
        "quality_warnings": sum(item.severity == "warning" and not item.suppressed for item in findings),
        "quality_new_errors": sum(
            item.severity == "error" and not item.legacy and not item.suppressed for item in findings
        ),
        "quality_stale_baseline": len(stale),
    }
    return by_address, summary


def parse_summary(path: Path | None, entities: list[dict]) -> dict[str, float | int]:
    text = path.read_text(encoding="utf-8", errors="ignore") if path else ""
    implemented_match = SUMMARY_RE["implemented"].search(text)
    accuracy_match = SUMMARY_RE["accuracy"].search(text)
    progress_match = SUMMARY_RE["progress"].search(text)

    comparable = [item for item in entities if not item.get("stub")]
    effective_score = sum(
        1.0 if item.get("effective") else float(item.get("matching", 0))
        for item in comparable
    )
    implemented = len(comparable)
    total = implemented
    if implemented_match:
        implemented, total = map(int, implemented_match.groups())

    accuracy = effective_score / implemented * 100 if implemented else 0.0
    progress = effective_score / total * 100 if total else 0.0
    if accuracy_match:
        accuracy = float(accuracy_match.group(1))
    if progress_match:
        progress = float(progress_match.group(1))

    return {
        "implemented": implemented,
        "total": total,
        "accuracy": accuracy,
        "progress": progress,
        "effective_score": effective_score,
    }


def infer_source_from_diff(entity: dict, basename_index: dict[str, list[str]]) -> str | None:
    for _, groups in entity.get("diff") or []:
        for group in groups:
            for side in ("both", "orig", "recomp"):
                for row in group.get(side, []):
                    if len(row) < 2:
                        continue
                    match = re.search(r"\t([^\t()]+\.(?:cpp|c|h)):\d+\)?$", row[1])
                    if match:
                        candidates = basename_index.get(Path(match.group(1)).name, [])
                        if len(candidates) == 1:
                            return candidates[0]
    return None


def enrich_report(
    report: dict,
    annotations: dict[str, dict[str, str]],
    names: dict[str, str],
    summary: dict[str, float | int],
    quality: dict[str, list[dict]] | None = None,
    function_sizes: dict[str, int] | None = None,
) -> dict:
    quality = quality or {}
    function_sizes = function_sizes or {}
    entities_by_address: dict[str, dict] = {}
    known_sources = sorted({item["source"] for item in annotations.values()})
    basename_index: dict[str, list[str]] = {}
    for source in known_sources:
        basename_index.setdefault(Path(source).name, []).append(source)

    for raw_entity in report.get("data", []):
        entity = dict(raw_entity)
        address = canonical_address(entity["address"])
        entity["address"] = address
        annotation = annotations.get(address)
        source = annotation["source"] if annotation else None
        if source is None:
            source = infer_source_from_diff(entity, basename_index)
        is_project = source is not None or address in names
        entity["source"] = source or ("[unmapped]/project" if is_project else "[linked-runtime]/unknown")
        entity["category"] = "project" if is_project else "runtime"
        entity["annotation"] = annotation["kind"] if annotation else "report-only"
        entity["status"] = (
            "stub"
            if entity.get("stub")
            else "effective"
            if entity.get("effective")
            else "exact"
            if entity.get("matching") == 1
            else "partial"
            if entity.get("matching", 0) > 0
            else "zero"
        )
        entity["quality"] = quality.get(address, [])
        entity["quality_errors"] = sum(item["severity"] == "error" for item in entity["quality"])
        entity["quality_warnings"] = sum(item["severity"] == "warning" for item in entity["quality"])
        entity["original_size"] = function_sizes.get(address)
        entities_by_address[address] = entity

    # Make the treemap a project view, not merely a list of successful pairs.
    # reccmp's JSON contains only entities it paired, so add annotated misses.
    for address, annotation in annotations.items():
        if address in entities_by_address:
            continue
        is_stub = annotation["kind"] == "stub"
        entities_by_address[address] = {
            "address": address,
            "name": names.get(address, "Unmatched annotated function"),
            "matching": 0,
            "source": annotation["source"],
            "category": "project",
            "annotation": annotation["kind"],
            "stub": is_stub,
            "status": "stub" if is_stub else "unmatched",
            "diff": None,
            "quality": quality.get(address, []),
            "quality_errors": sum(item["severity"] == "error" for item in quality.get(address, [])),
            "quality_warnings": sum(item["severity"] == "warning" for item in quality.get(address, [])),
            "original_size": function_sizes.get(address),
        }

    entities = sorted(entities_by_address.values(), key=lambda item: int(item["address"], 16))
    for address, name in names.items():
        if address in entities_by_address:
            continue
        entities.append(
            {
                "address": address,
                "name": name,
                "matching": 0,
                "source": "[unmapped]/project",
                "category": "project",
                "annotation": "unmatched",
                "stub": False,
                "status": "unmatched",
                "diff": None,
                "quality": quality.get(address, []),
                "quality_errors": sum(item["severity"] == "error" for item in quality.get(address, [])),
                "quality_warnings": sum(item["severity"] == "warning" for item in quality.get(address, [])),
                "original_size": function_sizes.get(address),
            }
        )

    entities.sort(
        key=lambda item: int(item["address"], 16)
        if str(item["address"]).startswith("0x")
        else 0xFFFFFFFF
    )

    comparable = [item for item in entities if not item.get("stub")]
    mapped_addresses = set(names)
    mapped_annotations = {
        address: annotation
        for address, annotation in annotations.items()
        if address in mapped_addresses and annotation["kind"] in ("function", "stub")
    }
    project_compared = [
        item
        for item in entities
        if item.get("category") == "project"
        and item.get("address") in mapped_addresses
        and not item.get("stub")
        and item["status"] != "unmatched"
    ]
    runtime_compared = [
        item
        for item in entities
        if item.get("category") == "runtime"
        and not item.get("stub")
        and item["status"] != "unmatched"
    ]
    project_effective_score = sum(
        1.0 if item.get("effective") else float(item.get("matching", 0))
        for item in project_compared
    )
    runtime_effective_score = sum(
        1.0 if item.get("effective") else float(item.get("matching", 0))
        for item in runtime_compared
    )
    project_entities = [
        item
        for item in entities
        if item.get("category") == "project" and item.get("address") in mapped_addresses
    ]
    project_original_bytes = sum(
        int(item.get("original_size") or 0) for item in project_entities
    )
    project_matched_bytes = sum(
        int(item.get("original_size") or 0) * float(item.get("matching", 0))
        for item in project_entities
        if not item.get("stub") and item["status"] != "unmatched"
    )
    project_effective_bytes = sum(
        int(item.get("original_size") or 0)
        * (1.0 if item.get("effective") else float(item.get("matching", 0)))
        for item in project_entities
        if not item.get("stub") and item["status"] != "unmatched"
    )
    summary.update(
        {
            "exact": sum(item["status"] == "exact" for item in comparable),
            "effective": sum(item["status"] == "effective" for item in comparable),
            "partial": sum(item["status"] == "partial" for item in comparable),
            "zero": sum(item["status"] == "zero" for item in comparable),
            "unmatched": sum(item["status"] == "unmatched" for item in comparable),
            "stubs": sum(bool(item.get("stub")) for item in entities),
            "report_entities": len(report.get("data", [])),
            "visualized_nonstubs": sum(not item.get("stub") for item in entities),
            "project_total": len(mapped_addresses),
            "project_implemented": sum(
                item["kind"] == "function" for item in mapped_annotations.values()
            ),
            "project_started": len(mapped_annotations),
            "project_compared": len(project_compared),
            "project_effective_score": project_effective_score,
            "project_accuracy": (
                project_effective_score / len(project_compared) * 100
                if project_compared
                else 0.0
            ),
            "project_progress": (
                project_effective_score / len(mapped_addresses) * 100
                if mapped_addresses
                else 0.0
            ),
            "project_original_bytes": project_original_bytes,
            "project_matched_bytes": project_matched_bytes,
            "project_effective_bytes": project_effective_bytes,
            "project_byte_progress": (
                project_matched_bytes / project_original_bytes * 100
                if project_original_bytes
                else 0.0
            ),
            "project_effective_byte_progress": (
                project_effective_bytes / project_original_bytes * 100
                if project_original_bytes
                else 0.0
            ),
            "quality_gate_passed": summary.get("quality_new_errors", 0) == 0
            and summary.get("quality_stale_baseline", 0) == 0,
            "runtime_compared": len(runtime_compared),
            "runtime_effective_score": runtime_effective_score,
            "runtime_accuracy": (
                runtime_effective_score / len(runtime_compared) * 100
                if runtime_compared
                else 0.0
            ),
        }
    )

    return {
        "project": "Toy Story 2",
        "binary": report.get("file", "toy2.exe"),
        "generated": report.get("timestamp", 0),
        "generated_iso": datetime.fromtimestamp(
            report.get("timestamp", 0), timezone.utc
        ).astimezone().isoformat(),
        "metrics": summary,
        "entities": entities,
    }


def safe_json_for_script(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).replace(
        "</", "<\\/"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Full reccmp JSON report")
    parser.add_argument("--summary", type=Path, help="Captured reccmp text summary")
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--functions-map", type=Path)
    parser.add_argument("--function-sizes", type=Path, help="Ghidra function-list JSON for the original executable")
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    report = json.loads(args.input.read_text(encoding="utf-8"))
    annotations = read_annotations(args.source_root)
    names, function_sizes = read_function_map(args.functions_map)
    # The original function table provides analyzed extents. It also prevents a
    # map span from absorbing a deliberately excluded linked-library region.
    function_sizes.update(read_function_sizes(args.function_sizes))
    summary = parse_summary(args.summary, report.get("data", []))
    quality, quality_summary = read_lint_quality(args.source_root)
    summary.update(quality_summary)
    payload = enrich_report(report, annotations, names, summary, quality, function_sizes)
    template = args.template.read_text(encoding="utf-8")
    marker = "__DECOMP_REPORT_DATA__"
    if template.count(marker) != 1:
        raise ValueError(f"Expected exactly one {marker} marker in {args.template}")
    output = template.replace(marker, safe_json_for_script(payload))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")
    print(f"Wrote {args.output} ({len(output.encode('utf-8')):,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
