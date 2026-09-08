#!/usr/bin/env python3
"""Build the self-contained Toy Story 2 decompilation dashboard."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from decomp_annotations import canonical_address, read_source_annotations
import decomp_binary
import decomp_lint
from decomp_status import MatchStatus, is_symbol_only_diff, read_tool_artifacts, verification_status

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
        # Ghidra uses a one-byte extent when it knows only a function start.
        # Keep the function-map span in that case.
        if address and isinstance(size, int) and size > 1:
            sizes[canonical_address(f"0x{address}")] = size
    return sizes


def read_data_evidence(path: Path | None) -> dict:
    """Read the optional type-aware global-data comparison report."""

    if path is None or not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def image_end(metadata: decomp_binary.ImageMetadata) -> int:
    """Return the first byte after the final raw PE region."""

    return max(
        [metadata.header_size]
        + [section.raw_pointer + section.raw_size for section in metadata.sections]
    )


def exact_byte_count(original: bytes, recompiled: bytes) -> int:
    """Count equal bytes at equivalent offsets in two structural regions."""

    return sum(left == right for left, right in zip(original, recompiled))


def score_debug_overlay(
    original: bytes, recompiled: bytes
) -> tuple[float, dict]:
    """Score the NB10 fields and PDB basename in a file overlay."""

    detail = {"format": "unknown", "explained_bytes": 0.0}
    if not original:
        return 0.0, detail
    if not (original.startswith(b"NB10") and recompiled.startswith(b"NB10")):
        explained = exact_byte_count(original, recompiled)
        detail.update({"format": "raw", "explained_bytes": explained})
        return float(explained), detail

    header_size = min(16, len(original))
    explained = exact_byte_count(original[:header_size], recompiled[:header_size])
    original_path = original[16:].split(b"\0", 1)[0]
    recompiled_path = recompiled[16:].split(b"\0", 1)[0]
    original_name = original_path.replace(b"/", b"\\").rsplit(b"\\", 1)[-1]
    recompiled_name = recompiled_path.replace(b"/", b"\\").rsplit(b"\\", 1)[-1]
    if original_name.lower() == recompiled_name.lower():
        explained += min(len(original_name) + 1, max(0, len(original) - 16))
    detail.update(
        {
            "format": "NB10",
            "original_pdb": original_path.decode("ascii", "replace"),
            "recompiled_pdb": recompiled_path.decode("ascii", "replace"),
            "basename_match": original_name.lower() == recompiled_name.lower(),
            "explained_bytes": explained,
        }
    )
    return float(min(explained, len(original))), detail


def score_resources(
    original_data: bytes,
    original_metadata: decomp_binary.ImageMetadata,
    recompiled_data: bytes,
    recompiled_metadata: decomp_binary.ImageMetadata,
) -> tuple[float, list[dict]]:
    """Score resource payloads by identity, independent of resource IDs."""

    original = decomp_binary.parse_resources(original_data, original_metadata)
    recompiled = decomp_binary.parse_resources(recompiled_data, recompiled_metadata)
    recompiled_payloads = {item.data for item in recompiled}
    recompiled_identities = Counter((item.path, item.data) for item in recompiled)
    rows = []
    explained = 0
    for item in original:
        match = item.data in recompiled_payloads
        identity_key = (item.path, item.data)
        identity_match = recompiled_identities[identity_key] > 0
        if identity_match:
            recompiled_identities[identity_key] -= 1
        if match:
            explained += len(item.data)
        rows.append(
            {
                "path": [str(part) for part in item.path],
                "size": len(item.data),
                "code_page": item.code_page,
                "match": match,
                "identity_match": identity_match,
            }
        )
    return float(explained), rows


def score_intervals(
    section_start: int, section_size: int, intervals: list[tuple[int, int, float]]
) -> tuple[int, float]:
    """Return the covered and explained bytes from non-overlapping intervals."""

    section_end = section_start + section_size
    cursor = section_start
    covered = 0
    explained = 0.0
    for start, end, score in sorted(intervals):
        start = max(section_start, start, cursor)
        end = min(section_end, end)
        if start >= end:
            continue
        size = end - start
        covered += size
        explained += size * max(0.0, min(1.0, score))
        cursor = end
    return covered, explained


def build_binary_layout(
    metadata: decomp_binary.ImageMetadata,
    entities: list[dict],
    original_data: bytes | None = None,
    recompiled_metadata: decomp_binary.ImageMetadata | None = None,
    recompiled_data: bytes | None = None,
    data_evidence: dict | None = None,
) -> dict:
    """Build a complete raw-file score with one status for every byte."""

    file_size = metadata.file_size
    if file_size <= 0 or not 0 < metadata.header_size <= file_size:
        raise ValueError("The retail executable has an invalid raw header size.")

    raw_sections = sorted(
        (section for section in metadata.sections if section.raw_size),
        key=lambda section: (section.raw_pointer, section.name),
    )
    cursor = metadata.header_size
    segments = [
        {
            "key": "headers",
            "name": "PE headers",
            "kind": "headers",
            "offset": 0,
            "size": metadata.header_size,
            "measurement": "raw-score",
            "scored_bytes": metadata.header_size,
            "explained_bytes": 0.0,
            "unexplained_scored_bytes": metadata.header_size,
            "unscored_bytes": 0,
        }
    ]
    section_rows: list[dict] = []
    gap_index = 0
    for index, section in enumerate(raw_sections):
        start = section.raw_pointer
        end = start + section.raw_size
        if start < cursor:
            raise ValueError("The retail executable has overlapping raw regions.")
        if start > file_size or end > file_size:
            raise ValueError("A PE section extends past the retail file.")
        if start > cursor:
            gap_index += 1
            segments.append(
                {
                    "key": f"gap-{gap_index}",
                    "name": "File padding",
                    "kind": "gap",
                    "offset": cursor,
                    "size": start - cursor,
                    "measurement": "raw-score",
                    "scored_bytes": start - cursor,
                    "explained_bytes": 0.0,
                    "unexplained_scored_bytes": start - cursor,
                    "unscored_bytes": 0,
                }
            )
        group = (
            "code"
            if section.name == ".text" or section.characteristics & 0x20000000
            else "data"
            if section.name == ".data"
            else "other"
        )
        row = {
            "key": f"section-{index}",
            "name": section.name or f"Section {index + 1}",
            "kind": "section",
            "group": group,
            "offset": start,
            "size": section.raw_size,
            "virtual_address": section.virtual_address,
            "virtual_size": section.virtual_size,
            "characteristics": section.characteristics,
            "measurement": "code-score" if group == "code" else "semantic-score",
            "scored_bytes": section.raw_size,
            "explained_bytes": 0.0,
            "unexplained_scored_bytes": section.raw_size,
            "unscored_bytes": 0,
        }
        section_rows.append(row)
        segments.append(row.copy())
        cursor = end

    if cursor < file_size:
        segments.append(
            {
                "key": "overlay",
                "name": "File overlay",
                "kind": "overlay",
                "offset": cursor,
                "size": file_size - cursor,
                "measurement": "semantic-score",
                "scored_bytes": file_size - cursor,
                "explained_bytes": 0.0,
                "unexplained_scored_bytes": file_size - cursor,
                "unscored_bytes": 0,
            }
        )

    code_entities = [
        entity
        for entity in entities
        if int(entity.get("original_size") or 0) > 0
    ]
    evidence = data_evidence or {}
    variables = evidence.get("variables", {}).get("variables", [])
    vtables = evidence.get("vtables", {}).get("tables", [])
    data_rows = variables + vtables
    resource_rows: list[dict] = []
    for section in section_rows:
        section_start = section["virtual_address"]
        intervals: list[tuple[int, int, float]] = []
        if section["group"] == "code":
            for entity in code_entities:
                try:
                    start = int(str(entity["address"]), 16)
                except (KeyError, TypeError, ValueError):
                    continue
                size = int(entity["original_size"])
                score = (
                    1.0
                    if entity.get("effective")
                    else max(0.0, min(1.0, float(entity.get("matching", 0))))
                )
                if entity.get("stub") or entity.get("status") == "unmatched":
                    score = 0.0
                intervals.append((start, start + size, score))
            covered, explained = score_intervals(
                section_start, section["size"], intervals
            )
            section["evidence_bytes"] = covered
            section["explained_bytes"] = explained
        elif section["name"] in (".rdata", ".data"):
            for item in data_rows:
                start = int(item.get("original_address", 0))
                size = int(item.get("size", 0))
                if size > 0:
                    intervals.append((start, start + size, float(item.get("score", 0))))
            covered, explained = score_intervals(
                section_start, section["size"], intervals
            )
            section["evidence_bytes"] = covered
            section["explained_bytes"] = explained
        elif section["name"] == ".idata":
            section["evidence_bytes"] = section["size"]
            section["explained_bytes"] = section["size"] * float(
                evidence.get("imports", {}).get("score", 0)
            )
        elif section["name"] == ".reloc":
            section["evidence_bytes"] = section["size"]
            section["explained_bytes"] = section["size"] * float(
                evidence.get("relocations", {}).get("score", 0)
            )
        elif (
            section["name"] == ".rsrc"
            and original_data is not None
            and recompiled_data is not None
            and recompiled_metadata is not None
        ):
            explained, resource_rows = score_resources(
                original_data, metadata, recompiled_data, recompiled_metadata
            )
            section["evidence_bytes"] = sum(item["size"] for item in resource_rows)
            section["explained_bytes"] = min(section["size"], explained)
        elif original_data is not None and recompiled_data is not None:
            original = original_data[
                section["offset"] : section["offset"] + section["size"]
            ]
            peer = next(
                (
                    item
                    for item in recompiled_metadata.sections
                    if item.name == section["name"]
                ),
                None,
            ) if recompiled_metadata is not None else None
            recompiled = (
                recompiled_data[peer.raw_pointer : peer.raw_pointer + peer.raw_size]
                if peer is not None
                else b""
            )
            section["evidence_bytes"] = section["size"]
            section["explained_bytes"] = exact_byte_count(original, recompiled)

        section["unexplained_scored_bytes"] = (
            section["size"] - section["explained_bytes"]
        )
        for segment in segments:
            if segment["key"] == section["key"]:
                segment.update(section)
                break

    debug_detail: dict = {}
    if original_data is not None and recompiled_data is not None and recompiled_metadata:
        headers = segments[0]
        headers["explained_bytes"] = exact_byte_count(
            original_data[: metadata.header_size],
            recompiled_data[: recompiled_metadata.header_size],
        )
        headers["unexplained_scored_bytes"] = (
            headers["size"] - headers["explained_bytes"]
        )
        original_overlay = original_data[image_end(metadata) :]
        recompiled_overlay = recompiled_data[image_end(recompiled_metadata) :]
        for segment in segments:
            if segment["kind"] == "overlay":
                explained, debug_detail = score_debug_overlay(
                    original_overlay, recompiled_overlay
                )
                segment["explained_bytes"] = explained
                segment["unexplained_scored_bytes"] = segment["size"] - explained
            elif segment["kind"] == "gap":
                peer = recompiled_data[
                    segment["offset"] : segment["offset"] + segment["size"]
                ]
                original = original_data[
                    segment["offset"] : segment["offset"] + segment["size"]
                ]
                segment["explained_bytes"] = exact_byte_count(original, peer)
                segment["unexplained_scored_bytes"] = (
                    segment["size"] - segment["explained_bytes"]
                )

    scored_code_bytes = sum(
        row["scored_bytes"] for row in section_rows if row["group"] == "code"
    )
    explained_code_bytes = sum(
        row["explained_bytes"] for row in section_rows if row["group"] == "code"
    )
    unexplained_scored_code_bytes = scored_code_bytes - explained_code_bytes
    raw_total = sum(segment["size"] for segment in segments)
    if raw_total != file_size:
        raise ValueError("The PE raw regions do not cover the retail file once.")

    explained_file_bytes = sum(segment["explained_bytes"] for segment in segments)
    return {
        "available": True,
        "error": None,
        "file_size": file_size,
        "header_size": metadata.header_size,
        "section_count": len(metadata.sections),
        "segments": segments,
        "sections": section_rows,
        "scored_code_bytes": scored_code_bytes,
        "explained_code_bytes": explained_code_bytes,
        "unexplained_scored_code_bytes": unexplained_scored_code_bytes,
        "scored_file_bytes": file_size,
        "explained_file_bytes": explained_file_bytes,
        "unexplained_file_bytes": file_size - explained_file_bytes,
        "unscored_file_bytes": 0,
        "explained_file_percent": (
            explained_file_bytes / file_size * 100 if file_size else 0.0
        ),
        "scored_code_effective_percent": (
            explained_code_bytes / scored_code_bytes * 100
            if scored_code_bytes
            else 0.0
        ),
        "data_evidence": evidence,
        "resources": resource_rows,
        "debug_overlay": debug_detail,
    }


def read_binary_layout(
    path: Path | None,
    entities: list[dict],
    recompiled_path: Path | None = None,
    data_evidence: dict | None = None,
) -> dict:
    """Read the retail layout without stopping report generation on an error."""

    if path is None:
        return {
            "available": False,
            "error": "The report command did not specify a retail executable.",
        }
    try:
        original_data = path.read_bytes()
        metadata = decomp_binary.parse_image_metadata(original_data)
        recompiled_data = recompiled_path.read_bytes() if recompiled_path else None
        recompiled_metadata = (
            decomp_binary.parse_image_metadata(recompiled_data)
            if recompiled_data is not None
            else None
        )
        return build_binary_layout(
            metadata,
            entities,
            original_data,
            recompiled_metadata,
            recompiled_data,
            data_evidence,
        )
    except FileNotFoundError:
        return {
            "available": False,
            "error": "The retail executable is not available.",
        }
    except (OSError, ValueError) as error:
        return {
            "available": False,
            "error": f"Cannot read the retail executable layout: {error}",
        }


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
            address = canonical_address(finding.owner_address)
            by_address.setdefault(address, []).append(finding.to_json())
    summary = {
        "quality_errors": sum(item.severity == "error" and not item.suppressed for item in findings),
        "quality_warnings": sum(item.severity == "warning" and not item.suppressed for item in findings),
        "quality_new_warnings": sum(
            item.severity == "warning" and not item.legacy and not item.suppressed
            for item in findings
        ),
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
    tool_artifacts: dict[int, str] | None = None,
) -> dict:
    quality = quality or {}
    function_sizes = function_sizes or {}
    tool_artifacts = tool_artifacts or {}
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
        entity["binary_status"] = (
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
        match_status = MatchStatus(
            matching=float(entity.get("matching", 0)),
            effective=bool(entity.get("effective")),
            name=str(entity.get("name", "")),
            diff=entity.get("diff"),
        )
        tool_artifact = int(address, 16) in tool_artifacts and is_symbol_only_diff(match_status)
        entity["tool_artifact"] = tool_artifacts.get(int(address, 16)) if tool_artifact else None
        entity["verification"] = verification_status(
            match_status,
            tool_artifact=tool_artifact,
            source_clean=not entity["quality_errors"] and not entity["quality_warnings"],
        )
        entity["status"] = entity["binary_status"]
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
            "binary_status": "stub" if is_stub else "unmatched",
            "verification": "stub" if is_stub else "unmatched",
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
                "binary_status": "unmatched",
                "verification": "unmatched",
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
    verified_project = [
        item for item in project_entities
        if item.get("verification") in ("exact", "effective", "tool")
    ]
    verified_bytes = sum(int(item.get("original_size") or 0) for item in verified_project)
    terminal_project = [
        item for item in project_entities
        if item.get("verification") in ("exact", "effective")
    ]
    terminal_bytes = sum(int(item.get("original_size") or 0) for item in terminal_project)
    coverage_gap_bytes = sum(
        int(item.get("original_size") or 0)
        for item in project_entities
        if item.get("annotation") != "function"
    )
    refinement_gap_bytes = sum(
        int(item.get("original_size") or 0)
        * max(
            0.0,
            1.0
            - (
                1.0
                if item.get("binary_status") in ("exact", "effective")
                else float(item.get("matching", 0))
            ),
        )
        for item in project_entities
        if item.get("annotation") == "function"
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
            "change_gate_passed": summary.get("quality_new_errors", 0) == 0
            and summary.get("quality_new_warnings", 0) == 0
            and summary.get("quality_stale_baseline", 0) == 0,
            "verified_functions": len(verified_project),
            "verified_function_progress": (
                len(verified_project) / len(mapped_addresses) * 100 if mapped_addresses else 0.0
            ),
            "verified_bytes": verified_bytes,
            "verified_byte_progress": (
                verified_bytes / project_original_bytes * 100 if project_original_bytes else 0.0
            ),
            "terminal_functions": len(terminal_project),
            "terminal_function_progress": (
                len(terminal_project) / len(mapped_addresses) * 100
                if mapped_addresses
                else 0.0
            ),
            "terminal_bytes": terminal_bytes,
            "terminal_byte_progress": (
                terminal_bytes / project_original_bytes * 100
                if project_original_bytes
                else 0.0
            ),
            "coverage_gap_bytes": coverage_gap_bytes,
            "refinement_gap_bytes": refinement_gap_bytes,
            "provisional_functions": sum(
                item.get("verification") == "provisional" and not item.get("stub")
                for item in project_entities
            ),
            "tool_artifacts": sum(item.get("verification") == "tool" for item in project_entities),
            "source_debt_functions": sum(
                bool(item.get("quality_errors") or item.get("quality_warnings"))
                for item in project_entities
            ),
            "binary_exact_functions": sum(
                item.get("binary_status") == "exact" for item in project_entities
            ),
            "binary_effective_functions": sum(
                item.get("binary_status") == "effective" for item in project_entities
            ),
            "binary_partial_functions": sum(
                item.get("binary_status") in ("partial", "zero")
                for item in project_entities
            ),
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
    parser.add_argument(
        "--retail-exe",
        type=Path,
        help="Retail PE executable used for the raw-file layout",
    )
    parser.add_argument(
        "--recompiled-exe",
        type=Path,
        help="Recompiled PE executable used for non-code comparison",
    )
    parser.add_argument(
        "--data-report",
        type=Path,
        help="Type-aware global-data comparison JSON",
    )
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
    tool_artifacts = read_tool_artifacts(
        Path(__file__).resolve().parents[1] / "tools" / "Resources" / "tool_artifacts.tsv"
    )
    payload = enrich_report(
        report, annotations, names, summary, quality, function_sizes, tool_artifacts
    )
    payload["binary_layout"] = read_binary_layout(
        args.retail_exe,
        payload["entities"],
        recompiled_path=args.recompiled_exe,
        data_evidence=read_data_evidence(args.data_report),
    )
    template = args.template.read_text(encoding="utf-8")
    marker = "__DECOMP_REPORT_DATA__"
    if template.count(marker) != 1:
        raise ValueError(f"Expected exactly one {marker} marker in {args.template}")
    output = template.replace(marker, safe_json_for_script(payload))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")
    layout = payload["binary_layout"]
    if layout.get("available"):
        print(
            "Whole-file evidence: "
            f"{layout['explained_file_bytes']:.3f} / {layout['file_size']} bytes "
            f"({layout['explained_file_percent']:.2f}%)."
        )
        section_text = ", ".join(
            f"{section['name']}={section['explained_bytes'] / section['size'] * 100:.2f}%"
            for section in layout["sections"]
            if section["size"]
        )
        print(f"Section evidence: {section_text}.")
    print(f"Wrote {args.output} ({len(output.encode('utf-8')):,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
