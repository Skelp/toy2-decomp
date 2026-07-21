#!/usr/bin/env python3
"""Build the self-contained Toy Story 2 decompilation dashboard."""

from __future__ import annotations

import argparse
import json
import re
from datetime import datetime
from pathlib import Path


ANNOTATION_RE = re.compile(
    r"//\s*(FUNCTION|STUB|LIBRARY):\s*TOY2\s+0x([0-9a-fA-F]+)"
)
SUMMARY_RE = {
    "implemented": re.compile(r"Implemented:\s+[\d.]+%\s+\((\d+)\s*/\s*(\d+)\)"),
    "accuracy": re.compile(r"Accuracy:\s+([\d.]+)%"),
    "progress": re.compile(r"Progress:\s+([\d.]+)%"),
}


def canonical_address(value: str | int) -> str:
    return f"0x{int(value, 0) if isinstance(value, str) else value:x}"


def read_annotations(source_root: Path) -> dict[str, dict[str, str]]:
    annotations: dict[str, dict[str, str]] = {}
    for path in sorted((*source_root.rglob("*.cpp"), *source_root.rglob("*.h"))):
        text = path.read_text(encoding="utf-8", errors="ignore")
        relative = path.relative_to(source_root).as_posix()
        for kind, address in ANNOTATION_RE.findall(text):
            annotations.setdefault(
                canonical_address(f"0x{address}"),
                {"kind": kind.lower(), "source": relative},
            )
    return annotations


def read_function_names(path: Path | None) -> dict[str, str]:
    if path is None or not path.exists():
        return {}
    names: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = re.match(r"\s*(?:0x)?([0-9a-fA-F]{6,8})(?:\s+(.+?))?\s*$", line)
        if match:
            names[canonical_address(f"0x{match.group(1)}")] = (
                match.group(2) or "Unknown function"
            )
    return names


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
) -> dict:
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
        entity["source"] = source or "[linked-runtime]/unknown"
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
            "annotation": annotation["kind"],
            "stub": is_stub,
            "status": "stub" if is_stub else "unmatched",
            "diff": None,
        }

    entities = sorted(entities_by_address.values(), key=lambda item: int(item["address"], 16))
    known_nonstubs = sum(not item.get("stub") for item in entities)
    missing_expected = max(0, int(summary["total"]) - known_nonstubs)
    for index in range(missing_expected):
        entities.append(
            {
                "address": f"unknown-{index + 1}",
                "name": "Unidentified expected function",
                "matching": 0,
                "source": "[unmapped]/expected",
                "annotation": "unmatched",
                "stub": False,
                "status": "unmatched",
                "diff": None,
            }
        )

    comparable = [item for item in entities if not item.get("stub")]
    summary.update(
        {
            "exact": sum(item["status"] == "exact" for item in comparable),
            "effective": sum(item["status"] == "effective" for item in comparable),
            "partial": sum(item["status"] == "partial" for item in comparable),
            "zero": sum(item["status"] == "zero" for item in comparable),
            "unmatched": sum(item["status"] == "unmatched" for item in comparable),
            "stubs": sum(bool(item.get("stub")) for item in entities),
            "report_entities": len(report.get("data", [])),
        }
    )

    return {
        "project": "Toy Story 2",
        "binary": report.get("file", "toy2.exe"),
        "generated": report.get("timestamp", 0),
        "generated_iso": datetime.fromtimestamp(report.get("timestamp", 0)).astimezone().isoformat(),
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
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    report = json.loads(args.input.read_text(encoding="utf-8"))
    annotations = read_annotations(args.source_root)
    names = read_function_names(args.functions_map)
    summary = parse_summary(args.summary, report.get("data", []))
    payload = enrich_report(report, annotations, names, summary)
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
