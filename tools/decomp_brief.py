#!/usr/bin/env python3
"""Build a fixed evidence brief for one reconstruction target."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Mapping

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_campaigns import LANE_MODES  # noqa: E402
from tools.decomp_mismatch import classify_report_diff, empty_taxonomy  # noqa: E402
from tools.decomp_doctor import (  # noqa: E402
    CACHE_RELATIVE as DOCTOR_CACHE_RELATIVE,
    RECEIPT_DIRECTORY_RELATIVE,
    data_artifact_descriptors,
    ghidra_environment,
    load_ghidra_artifact,
    mirror_ghidra_bridge_markers,
    prune_ghidra_logs,
    source_artifact_descriptors,
    validate_doctor_receipt,
)

CACHE_RELATIVE = Path("build/decomp-cache/briefs")
MAP_RELATIVE = Path("tools/Resources/functions_map.txt")
REPORT_RELATIVE = Path("build/decomp-current-report.json")
SIZE_RELATIVE = Path("build/decomp-function-sizes.json")
DATA_REPORT_RELATIVE = Path("build/decomp-current-data-report.json")
LEDGER_RELATIVE = Path("tools/Resources/campaign-ledger.jsonl")
BLOCKERS_RELATIVE = Path("tools/Resources/reconstruction-blockers.tsv")
ARTIFACTS_RELATIVE = Path("tools/Resources/tool_artifacts.tsv")
DWARF_PATH = Path(
    "/run/media/skelp/1TB/venvs/Open-Travellers/OpenCrashWOC/code/src/"
    "dump_alphaNGCport_DWARF.txt"
)
SOURCE_LANES = {"closure", "production", "research"}
LANES = tuple(sorted(SOURCE_LANES | {"data", "resource"}))
TOOL_INPUTS = (
    Path("tools/decomp_brief.py"),
    Path("tools/decomp_doctor.py"),
    Path("tools/decomp_diff.py"),
    Path("tools/decomp_evidence.py"),
    Path("tools/decomp_candidates.py"),
    Path("tools/decomp_campaigns.py"),
    Path("tools/decomp_provenance.py"),
    Path("tools/decomp_lint.py"),
    Path("tools/decomp_status.py"),
    Path("tools/decomp_mismatch.py"),
)
SCOUT_REPORT_SCHEMA = 2
SCOUT_REPORT_MAX_BYTES = 64 * 1024
SCOUT_FINDING_MAX_COUNT = 32
SCOUT_FINDING_MAX_CHARS = 1000
SCOUT_FINDINGS_MAX_BYTES = 8 * 1024
SCOUT_ID_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}")
SCOUT_AUDITS = (
    "retail-abi-control-flow-evidence",
    "callers-types-layout-translation-unit-analogue",
)
SCOUT_AUDIT_CATEGORIES = {
    "retail-abi-control-flow-evidence": {
        "abi",
        "control-flow",
        "retail-evidence",
    },
    "callers-types-layout-translation-unit-analogue": {
        "callers",
        "types-layout",
        "translation-unit-analogue",
    },
}
SCOUT_FINDING_KEYS = {"category", "claim", "evidence"}
SCOUT_CITATION_KEYS = {"source", "locator"}
SCOUT_REPORT_KEYS = {
    "schema",
    "scout_id",
    "audit",
    "lane",
    "target",
    "access",
    "owns_mutations",
    "doctor_receipt",
    "findings",
}


class BriefError(RuntimeError):
    """Report that a brief cannot be built or trusted."""


@dataclass(frozen=True)
class BriefResult:
    brief: dict[str, object]
    path: Path
    cache_hit: bool
    elapsed_seconds: float


Runner = Callable[..., subprocess.CompletedProcess]
EvidenceBuilder = Callable[[str, str, Path, Runner], dict[str, object]]


def _default_runner(command: list[str], **kwargs: object) -> subprocess.CompletedProcess:
    return subprocess.run(command, **kwargs)


def _sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _combined_hash(root: Path, paths: Iterable[Path]) -> str:
    digest = hashlib.sha256()
    for relative in sorted(paths, key=str):
        digest.update(relative.as_posix().encode())
        value = _sha256(root / relative)
        digest.update((value or "MISSING").encode())
    return digest.hexdigest()


def _validate_report_provenance(path: Path, root: Path) -> None:
    from tools.decomp_provenance import validate_report

    try:
        validate_report(path, root=root)
    except (OSError, ValueError) as error:
        raise BriefError(f"comparison report provenance is invalid: {error}") from error


def _run(
    runner: Runner,
    command: list[str],
    *,
    root: Path,
    environment: dict[str, str] | None = None,
    timeout: float = 20.0,
) -> subprocess.CompletedProcess:
    return runner(
        command,
        cwd=root,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
        timeout=timeout,
    )


def _stdout(result: subprocess.CompletedProcess) -> str:
    return result.stdout.decode(errors="replace") if isinstance(result.stdout, bytes) else (result.stdout or "")


def _head(root: Path, runner: Runner) -> str:
    try:
        result = _run(runner, ["git", "rev-parse", "HEAD"], root=root)
    except (OSError, subprocess.SubprocessError) as error:
        raise BriefError(f"cannot read HEAD: {error}") from error
    value = _stdout(result).strip()
    if result.returncode != 0 or not re.fullmatch(r"[0-9a-fA-F]{40,64}", value):
        raise BriefError("cannot read a full HEAD hash")
    return value.lower()


def normalize_target(lane: str, target: str | int) -> str:
    if lane == "resource":
        value = str(target).strip()
        fields = value.split(",")
        if len(fields) != 3:
            raise BriefError("a resource target must be TYPE,ID,LANGUAGE")
        try:
            return ",".join(str(int(field, 0)) for field in fields)
        except ValueError as error:
            raise BriefError("resource fields must be integers") from error
    try:
        address = int(target, 0) if isinstance(target, str) else int(target)
    except (TypeError, ValueError) as error:
        raise BriefError("use a target address such as 0x00401000") from error
    if not 0 <= address <= 0xFFFFFFFF:
        raise BriefError("the target address must fit in 32 bits")
    return f"0x{address:08X}"


def _doctor_descriptor(
    root: Path,
    lane: str,
    target: str,
    path: Path,
    runner: Runner,
) -> dict[str, object]:
    resolved = path if path.is_absolute() else root / path
    resolved = resolved.resolve()
    allowed_modes = LANE_MODES.get(lane)
    if not allowed_modes:
        raise BriefError(f"unsupported lane {lane!r}")
    expected_mode = next(iter(allowed_modes)) if len(allowed_modes) == 1 else None
    try:
        receipt = validate_doctor_receipt(
            resolved,
            root=root,
            runner=runner,
            expected_mode=expected_mode,
            expected_lane=lane,
        )
    except ValueError as error:
        raise BriefError(f"the doctor receipt is invalid: {error}") from error
    mode = receipt.get("mode")
    if mode not in allowed_modes:
        choices = " or ".join(sorted(allowed_modes))
        raise BriefError(f"the {lane} lane needs a {choices} doctor receipt")
    if lane == "resource" and receipt.get("resource") != target:
        raise BriefError("the doctor receipt does not include this resource target")
    if lane != "resource" and target not in receipt.get("addresses", []):
        raise BriefError("the doctor receipt does not include this address target")
    receipt_hash = _sha256(resolved)
    if receipt_hash is None:
        raise BriefError("the doctor receipt is missing")
    receipt_id = receipt.get("receipt_id")
    saved_path = receipt.get("receipt_path")
    if isinstance(receipt_id, str) and isinstance(saved_path, str):
        expected_path = RECEIPT_DIRECTORY_RELATIVE / f"{receipt_id}.json"
        if saved_path != expected_path.as_posix():
            raise BriefError("the doctor receipt immutable path is invalid")
        immutable = (root / expected_path).resolve()
        try:
            immutable.relative_to((root / DOCTOR_CACHE_RELATIVE).resolve())
        except ValueError as error:
            raise BriefError("the immutable doctor receipt escaped the build cache") from error
        immutable_hash = _sha256(immutable)
        if immutable_hash is None or immutable_hash != receipt_hash:
            raise BriefError("the immutable doctor receipt does not match")
        resolved = immutable
        receipt_hash = immutable_hash
    descriptor: dict[str, object] = {
        "path": str(resolved),
        "sha256": receipt_hash,
        "receipt_id": receipt.get("receipt_id"),
        "mode": mode,
        "lane": receipt.get("lane"),
    }
    if lane in SOURCE_LANES:
        try:
            artifacts = source_artifact_descriptors(receipt, target)
            for kind, artifact in artifacts.items():
                load_ghidra_artifact(root, target, kind, artifact)
        except ValueError as error:
            raise BriefError(f"the doctor receipt artifact is invalid: {error}") from error
        descriptor["source_artifacts"] = artifacts
    elif lane == "data":
        try:
            artifacts = data_artifact_descriptors(receipt, target)
            for kind, artifact in artifacts.items():
                load_ghidra_artifact(root, target, kind, artifact)
        except ValueError as error:
            raise BriefError(f"the doctor receipt artifact is invalid: {error}") from error
        descriptor["data_artifacts"] = artifacts
    return descriptor


def _scout_report_descriptor(
    root: Path,
    lane: str,
    target: str,
    path: Path,
    doctor_receipt: Mapping[str, object],
) -> dict[str, object]:
    resolved = (path if path.is_absolute() else root / path).resolve()
    try:
        raw = resolved.read_bytes()
    except OSError as error:
        raise BriefError(f"cannot read the scout report {resolved}: {error}") from error
    if not raw or len(raw) > SCOUT_REPORT_MAX_BYTES:
        raise BriefError("a scout report must contain 1 through 65536 bytes")
    try:
        report = json.loads(raw.decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BriefError(f"the scout report is not valid JSON: {resolved}") from error
    if not isinstance(report, dict) or set(report) != SCOUT_REPORT_KEYS:
        raise BriefError("the scout report does not match schema 2")
    if report.get("schema") != SCOUT_REPORT_SCHEMA:
        raise BriefError("the scout report schema is not supported")
    scout_id = report.get("scout_id")
    if not isinstance(scout_id, str) or SCOUT_ID_PATTERN.fullmatch(scout_id) is None:
        raise BriefError("the scout report has an invalid scout ID")
    if report.get("audit") not in SCOUT_AUDITS:
        raise BriefError("the scout report has an invalid audit")
    if report.get("lane") != lane:
        raise BriefError("the scout report lane does not match the brief")
    if report.get("target") != target:
        raise BriefError("the scout report target does not match the brief")
    if report.get("access") != "read-only":
        raise BriefError("the scout report access must be read-only")
    if report.get("owns_mutations") is not False:
        raise BriefError("the scout report must set owns_mutations to false")
    expected_doctor = {
        "receipt_id": doctor_receipt.get("receipt_id"),
        "sha256": doctor_receipt.get("sha256"),
    }
    if (
        not isinstance(expected_doctor["receipt_id"], str)
        or re.fullmatch(r"[0-9a-f]{64}", expected_doctor["receipt_id"]) is None
        or not isinstance(expected_doctor["sha256"], str)
        or re.fullmatch(r"[0-9a-f]{64}", expected_doctor["sha256"]) is None
    ):
        raise BriefError("the canonical doctor receipt has no valid identity")
    if report.get("doctor_receipt") != expected_doctor:
        raise BriefError("the scout report doctor receipt does not match the brief")
    findings = report.get("findings")
    required_categories = SCOUT_AUDIT_CATEGORIES[str(report["audit"])]
    if (
        not isinstance(findings, list)
        or not len(required_categories) <= len(findings) <= SCOUT_FINDING_MAX_COUNT
        or any(
            not isinstance(finding, dict)
            or set(finding) != SCOUT_FINDING_KEYS
            or finding.get("category") not in required_categories
            or not isinstance(finding.get("claim"), str)
            or not str(finding.get("claim")).strip()
            or len(str(finding.get("claim"))) > SCOUT_FINDING_MAX_CHARS
            or not isinstance(finding.get("evidence"), list)
            or not finding.get("evidence")
            or len(finding.get("evidence", [])) > 8
            or any(
                not isinstance(citation, dict)
                or set(citation) != SCOUT_CITATION_KEYS
                or not isinstance(citation.get("source"), str)
                or not citation.get("source", "").strip()
                or len(citation.get("source", "")) > 200
                or not isinstance(citation.get("locator"), str)
                or not citation.get("locator", "").strip()
                or len(citation.get("locator", "")) > 500
                for citation in finding.get("evidence", [])
            )
            for finding in findings
        )
        or {str(finding["category"]) for finding in findings}
        != required_categories
    ):
        raise BriefError(
            "the scout report needs one cited finding for each audit category"
        )
    findings_size = len(
        json.dumps(findings, ensure_ascii=False, separators=(",", ":")).encode(
            "utf-8"
        )
    )
    if findings_size > SCOUT_FINDINGS_MAX_BYTES:
        raise BriefError("the scout report findings exceed 8192 bytes")
    return {
        "path": str(resolved),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "content": report,
    }


def _scout_report_descriptors(
    root: Path,
    lane: str,
    target: str,
    paths: Iterable[Path],
    doctor_receipt: Mapping[str, object] | None,
) -> list[dict[str, object]]:
    report_paths = tuple(paths)
    if len(report_paths) != 2:
        raise BriefError("provide exactly two scout reports")
    if doctor_receipt is None:
        raise BriefError("scout reports need a doctor receipt")
    resolved_paths = [
        (path if path.is_absolute() else root / path).resolve()
        for path in report_paths
    ]
    if len(set(resolved_paths)) != 2:
        raise BriefError("use two different scout report files")
    descriptors = [
        _scout_report_descriptor(root, lane, target, path, doctor_receipt)
        for path in report_paths
    ]
    contents = [descriptor.get("content") for descriptor in descriptors]
    if any(not isinstance(content, dict) for content in contents):
        raise BriefError("the scout report content is invalid")
    scout_ids = [str(content.get("scout_id")) for content in contents]
    if len(set(scout_ids)) != 2:
        raise BriefError("use two different scout IDs")
    audits = {content.get("audit") for content in contents}
    if audits != set(SCOUT_AUDITS):
        raise BriefError("use one scout report for each required audit")
    return descriptors


def _production_mismatch_descriptor(
    root: Path, target: str
) -> dict[str, object]:
    """Bind a production brief to one current verbose comparison diff."""

    from tools.decomp_provenance import provenance_path, validate_diff

    address = int(target, 0)
    path = (root / "build/decomp-diffs" / f"{target}.txt").resolve()
    try:
        receipt = validate_diff(path, address, root=root)
    except (OSError, ValueError) as error:
        raise BriefError(
            f"production brief needs a current `tools/decomp bc {target}` result: {error}"
        ) from error
    sidecar = provenance_path(path)
    descriptor = {
        "path": str(path),
        "sha256": _sha256(path),
        "provenance_path": str(sidecar.resolve()),
        "provenance_sha256": _sha256(sidecar),
        "receipt": receipt,
    }
    if descriptor["sha256"] is None or descriptor["provenance_sha256"] is None:
        raise BriefError("the production mismatch evidence is incomplete")
    return descriptor


def _input_descriptor(
    root: Path,
    lane: str,
    target: str,
    runner: Runner,
    doctor_receipt_path: Path | None = None,
    scout_report_paths: Iterable[Path] | None = None,
) -> dict[str, object]:
    _validate_report_provenance(root / REPORT_RELATIVE, root)
    if lane == "data":
        _validate_report_provenance(root / DATA_REPORT_RELATIVE, root)
    reports = {"function": _sha256(root / REPORT_RELATIVE), "sizes": _sha256(root / SIZE_RELATIVE)}
    if lane == "data":
        reports["data"] = _sha256(root / DATA_REPORT_RELATIVE)
    if lane == "resource":
        reports["retail_executable"] = _sha256(root / "original/toy2.exe")
        reports["build_executable"] = _sha256(root / "build/toy2.exe")
    doctor_descriptor = (
        _doctor_descriptor(root, lane, target, doctor_receipt_path, runner)
        if doctor_receipt_path is not None
        else None
    )
    scout_descriptors = (
        _scout_report_descriptors(
            root,
            lane,
            target,
            scout_report_paths,
            doctor_descriptor,
        )
        if scout_report_paths is not None
        else None
    )
    descriptor: dict[str, object] = {
        "head": _head(root, runner),
        "lane": lane,
        "target": target,
        "target_hash": hashlib.sha256(target.encode()).hexdigest(),
        "subsystem": (
            _source_subsystem(root, int(target, 0))
            if lane in SOURCE_LANES
            else None
        ),
        "report_hashes": reports,
        "map_hash": _sha256(root / MAP_RELATIVE),
        "tool_hash": _combined_hash(root, TOOL_INPUTS),
        "history_hash": _sha256(root / LEDGER_RELATIVE),
        "blocker_hash": _sha256(root / BLOCKERS_RELATIVE),
        "artifact_hash": _sha256(root / ARTIFACTS_RELATIVE),
        "dwarf_input": {
            "path": str(DWARF_PATH),
            "sha256": _sha256(DWARF_PATH),
        },
        "doctor_receipt": doctor_descriptor,
        "scout_reports": scout_descriptors,
        "production_mismatch": (
            _production_mismatch_descriptor(root, target)
            if lane == "production"
            else None
        ),
    }
    required = {
        "map": descriptor["map_hash"],
        "function report": reports["function"],
        "function sizes": reports["sizes"],
    }
    if lane == "data":
        required["data report"] = reports.get("data")
    if lane == "resource":
        required["retail executable"] = reports.get("retail_executable")
        required["build executable"] = reports.get("build_executable")
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise BriefError(f"brief inputs are missing: {', '.join(missing)}")
    return descriptor


def _cache_key(descriptor: dict[str, object]) -> str:
    encoded = json.dumps(descriptor, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def _cache_path(root: Path, lane: str, target: str, key: str) -> Path:
    token = re.sub(r"[^A-Za-z0-9]+", "-", target).strip("-").lower()
    base = (root / CACHE_RELATIVE).resolve()
    path = (base / f"{lane}-{token}-{key}.json").resolve()
    try:
        path.relative_to(base)
    except ValueError as error:
        raise BriefError("brief cache path escaped the build cache") from error
    return path


def _content_hash(document: dict[str, object]) -> str:
    content = dict(document)
    content.pop("content_sha256", None)
    encoded = json.dumps(content, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def scout_roles(
    scout_reports: Iterable[Mapping[str, object]] | None = None,
) -> dict[str, object]:
    scouts: list[dict[str, object]] = []
    for descriptor in scout_reports or ():
        content = descriptor.get("content")
        if not isinstance(content, Mapping):
            continue
        scouts.append(
            {
                "id": content.get("scout_id"),
                "audit": content.get("audit"),
                "access": "read-only",
                "owns_mutations": False,
                "report_path": descriptor.get("path"),
                "report_sha256": descriptor.get("sha256"),
            }
        )
    return {
        "scouts": scouts,
        "writer": {
            "id": "source-writer",
            "access": "writer",
            "owns_mutations": True,
        },
    }


def _validate_cached(document: object, descriptor: dict[str, object], key: str) -> dict[str, object]:
    if not isinstance(document, dict):
        raise BriefError("cached brief is not a JSON object")
    if document.get("schema") != 1 or document.get("cache_key") != key:
        raise BriefError("cached brief identity does not match its file name")
    if document.get("inputs") != descriptor:
        raise BriefError("cached brief input hashes do not match")
    saved_hash = document.get("content_sha256")
    if not isinstance(saved_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", saved_hash):
        raise BriefError("cached brief has no valid content hash")
    if saved_hash != _content_hash(document):
        raise BriefError("cached brief content hash does not match")
    return document


def _load_cache(path: Path, descriptor: dict[str, object], key: str) -> dict[str, object] | None:
    if not path.exists():
        return None
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BriefError(f"cached brief is not readable: {error}") from error
    return _validate_cached(document, descriptor, key)


def _store_immutable(path: Path, document: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
    try:
        with path.open("x", encoding="utf-8") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        existing = _load_cache(path, document["inputs"], str(document["cache_key"]))
        if existing != document:
            raise BriefError("another process wrote different brief content for the same key")


def build_brief(
    lane: str,
    target: str | int,
    *,
    root: Path = ROOT,
    runner: Runner = _default_runner,
    evidence_builder: EvidenceBuilder | None = None,
    clock: Callable[[], float] = time.monotonic,
    doctor_receipt_path: Path | None = None,
    scout_report_paths: Iterable[Path] | None = None,
) -> BriefResult:
    if lane not in LANES:
        raise BriefError(f"unsupported lane {lane!r}")
    normalized = normalize_target(lane, target)
    root = root.resolve()
    report_paths = (
        tuple(scout_report_paths) if scout_report_paths is not None else None
    )
    started = clock()
    descriptor = _input_descriptor(
        root,
        lane,
        normalized,
        runner,
        doctor_receipt_path,
        report_paths,
    )
    key = _cache_key(descriptor)
    path = _cache_path(root, lane, normalized, key)
    cached = _load_cache(path, descriptor, key)
    if cached is not None:
        return BriefResult(cached, path, True, max(0.0, clock() - started))

    if evidence_builder is not None:
        evidence = evidence_builder(lane, normalized, root, runner)
    else:
        doctor_descriptor = descriptor.get("doctor_receipt")
        artifact_key = (
            "source_artifacts"
            if lane in SOURCE_LANES
            else "data_artifacts" if lane == "data" else None
        )
        doctor_artifacts = (
            doctor_descriptor.get(artifact_key)
            if isinstance(doctor_descriptor, dict) and artifact_key is not None
            else None
        )
        evidence = gather_evidence(
            lane,
            normalized,
            root,
            runner,
            doctor_artifacts=doctor_artifacts if isinstance(doctor_artifacts, dict) else None,
        )
    if lane == "research":
        evidence = dict(evidence)
        route = research_route_evidence(root, normalized)
        evidence["research_route"] = route
        if route.get("eligible") is not True:
            evidence["readiness"] = False
            rejection = evidence.get("rejection_reasons")
            reasons = list(rejection) if isinstance(rejection, list) else []
            reason = (
                "research needs an active blocker, a target cooldown, or an open subsystem circuit"
            )
            if reason not in reasons:
                reasons.append(reason)
            evidence["rejection_reasons"] = reasons
    if lane in SOURCE_LANES:
        gate = evidence.get("source_evidence") if isinstance(evidence, dict) else None
        if not isinstance(gate, dict) or not gate.get("disassembly_nonempty") or not gate.get("decompilation_nonempty"):
            raise BriefError("source briefs need nonempty disassembly and decompilation")
    after = _input_descriptor(
        root,
        lane,
        normalized,
        runner,
        doctor_receipt_path,
        report_paths,
    )
    if after != descriptor:
        raise BriefError("brief inputs changed while evidence was collected")
    scout_reports = descriptor.get("scout_reports")
    bound_reports = scout_reports if isinstance(scout_reports, list) else []
    scout_findings = []
    for report_descriptor in bound_reports:
        content = report_descriptor.get("content")
        if not isinstance(content, dict):
            continue
        scout_findings.append(
            {
                "scout_id": content.get("scout_id"),
                "audit": content.get("audit"),
                "report_sha256": report_descriptor.get("sha256"),
                "findings": content.get("findings"),
            }
        )
    document: dict[str, object] = {
        "schema": 1,
        "cache_key": key,
        "inputs": descriptor,
        "lane": lane,
        "target": normalized,
        "subsystem": descriptor["subsystem"],
        "roles": scout_roles(bound_reports),
        "scout_findings": scout_findings,
        "evidence": evidence,
    }
    document["content_sha256"] = _content_hash(document)
    _store_immutable(path, document)
    return BriefResult(document, path, False, max(0.0, clock() - started))


def validate_brief(
    path: Path,
    lane: str,
    target: str | int,
    *,
    root: Path = ROOT,
    runner: Runner = _default_runner,
    doctor_receipt_path: Path | None = None,
    scout_report_paths: Iterable[Path] | None = None,
    require_scout_reports: bool = True,
) -> dict[str, object]:
    """Validate one immutable brief against the current repository inputs."""

    if lane not in LANES:
        raise BriefError(f"unsupported lane {lane!r}")
    root = root.resolve()
    normalized = normalize_target(lane, target)
    report_paths = (
        tuple(scout_report_paths) if scout_report_paths is not None else None
    )
    if report_paths is None and path.is_file():
        try:
            saved_document = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise BriefError(f"cannot read the target brief: {error}") from error
        saved_inputs = (
            saved_document.get("inputs")
            if isinstance(saved_document, dict)
            else None
        )
        saved_reports = (
            saved_inputs.get("scout_reports")
            if isinstance(saved_inputs, dict)
            else None
        )
        if isinstance(saved_reports, list) and saved_reports:
            saved_paths = [
                report.get("path") if isinstance(report, dict) else None
                for report in saved_reports
            ]
            if not all(isinstance(value, str) for value in saved_paths):
                raise BriefError("the target brief has invalid scout report paths")
            report_paths = tuple(Path(str(value)) for value in saved_paths)
    if require_scout_reports and report_paths is None:
        raise BriefError("the target brief needs two bound scout reports")
    descriptor = _input_descriptor(
        root,
        lane,
        normalized,
        runner,
        doctor_receipt_path,
        report_paths,
    )
    key = _cache_key(descriptor)
    expected_path = _cache_path(root, lane, normalized, key)
    if path.resolve() != expected_path:
        raise BriefError("the brief path does not match the current input identity")
    document = _load_cache(expected_path, descriptor, key)
    if document is None:
        raise BriefError("the immutable brief does not exist")
    if document.get("subsystem") != descriptor.get("subsystem"):
        raise BriefError("the target brief subsystem identity changed")
    evidence = document.get("evidence")
    if lane == "research":
        route = research_route_evidence(root, normalized)
        if not isinstance(evidence, dict) or evidence.get("research_route") != route:
            raise BriefError("the research route evidence changed")
        if route.get("eligible") is not True:
            raise BriefError(
                "research needs an active blocker, a target cooldown, or an open subsystem circuit"
            )
    if not isinstance(evidence, dict) or evidence.get("readiness") is not True:
        raise BriefError("the target brief is not ready")
    roles = document.get("roles")
    scouts = roles.get("scouts") if isinstance(roles, dict) else None
    bound_reports = descriptor.get("scout_reports")
    if require_scout_reports or bound_reports is not None:
        if (
            not isinstance(bound_reports, list)
            or len(bound_reports) != 2
            or not isinstance(scouts, list)
            or len(scouts) != 2
            or any(
                not isinstance(scout, dict)
                or scout.get("access") != "read-only"
                or scout.get("owns_mutations") is not False
                for scout in scouts
            )
        ):
            raise BriefError("the target brief needs two bound read-only scouts")
        expected_findings = []
        for report in bound_reports:
            content = report.get("content")
            if not isinstance(content, dict):
                raise BriefError("the target brief has an invalid scout report")
            expected_findings.append(
                {
                    "scout_id": content.get("scout_id"),
                    "audit": content.get("audit"),
                    "report_sha256": report.get("sha256"),
                    "findings": content.get("findings"),
                }
            )
        if document.get("scout_findings") != expected_findings:
            raise BriefError("the target brief scout findings do not match the reports")
    return {
        "path": str(expected_path),
        "sha256": _sha256(expected_path),
        "cache_key": key,
        "lane": lane,
        "target": normalized,
        "subsystem": descriptor["subsystem"],
        "head": descriptor["head"],
        "content_sha256": document["content_sha256"],
        "dwarf_input": descriptor["dwarf_input"],
        "doctor_receipt": descriptor["doctor_receipt"],
        "scout_reports": descriptor["scout_reports"],
        "research_route": (
            evidence.get("research_route") if lane == "research" else None
        ),
    }


def _read_json(path: Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise BriefError(f"cannot read {path}: {error}") from error


def _parse_map(root: Path) -> list[tuple[int, str]]:
    entries: list[tuple[int, str]] = []
    for line in (root / MAP_RELATIVE).read_text(encoding="utf-8", errors="ignore").splitlines():
        fields = line.split(maxsplit=1)
        if not fields or fields[0].startswith("#"):
            continue
        try:
            entries.append((int(fields[0], 0), fields[1] if len(fields) > 1 else ""))
        except ValueError:
            continue
    return sorted(entries)


def _annotations(root: Path) -> dict[int, dict[str, object]]:
    pattern = re.compile(r"//\s*(FUNCTION|STUB):\s*TOY2\s+(0x[0-9A-Fa-f]+)([^\n]*)")
    values: dict[int, dict[str, object]] = {}
    for path in sorted((root / "src").rglob("*")):
        if path.suffix.lower() not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            values[int(match.group(2), 0)] = {
                "state": match.group(1),
                "source": path.relative_to(root).as_posix(),
                "line": line,
                "annotation_suffix": match.group(3).strip(),
            }
    return values


def _source_subsystem(root: Path, address: int) -> str:
    name = dict(_parse_map(root)).get(address, "")
    namespace = name.rsplit("::", 1)[0] if "::" in name else ""
    annotation = _annotations(root).get(address)
    source = str(annotation.get("source", "")) if annotation else ""
    return namespace or (Path(source).stem if source else "") or "(global)"


def _json_command(
    runner: Runner,
    command: list[str],
    *,
    root: Path,
    environment: dict[str, str],
    timeout: float = 30.0,
) -> object | None:
    try:
        result = _run(runner, command, root=root, environment=environment, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    try:
        return json.loads(_stdout(result))
    except json.JSONDecodeError:
        return None


def _walk(value: object) -> Iterable[object]:
    yield value
    if isinstance(value, dict):
        for child in value.values():
            yield from _walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk(child)


def _instruction_rows(payload: object) -> list[object]:
    candidates: list[list[object]] = []
    for item in _walk(payload):
        if not isinstance(item, dict):
            continue
        for key, value in item.items():
            if key.lower() in {"instructions", "disassembly"} and isinstance(value, list):
                candidates.append(value)
    if isinstance(payload, list) and payload:
        candidates.append(payload)
    return max(candidates, key=len, default=[])


def _code(payload: object) -> str:
    for item in _walk(payload):
        if not isinstance(item, dict):
            continue
        for key, value in item.items():
            if key.lower() in {"code", "decompilation", "c"} and isinstance(value, str) and value.strip():
                return value.strip()
    return ""


def _bounded_records(payload: object, limit: int = 40) -> list[dict[str, object]]:
    rows = payload if isinstance(payload, list) else []
    if not rows:
        candidates = [item for item in _walk(payload) if isinstance(item, list)]
        rows = max(candidates, key=len, default=[])
    allowed = {
        "address",
        "entry",
        "from",
        "to",
        "function",
        "name",
        "ref_type",
        "source",
        "storage",
        "type",
    }
    result: list[dict[str, object]] = []
    for row in rows[:limit]:
        if isinstance(row, dict):
            record = {
                str(key): value
                for key, value in row.items()
                if str(key).lower() in allowed and isinstance(value, (str, int, float, bool, type(None)))
            }
            result.append(record or {"summary": json.dumps(row, sort_keys=True)[:500]})
        else:
            result.append({"summary": str(row)[:500]})
    return result


def _first_scalar(payload: object, names: set[str]) -> object | None:
    for item in _walk(payload):
        if not isinstance(item, dict):
            continue
        for key, value in item.items():
            if key.lower() in names and isinstance(value, (str, int, float, bool)):
                return value
    return None


def _named_list(payload: object, names: set[str]) -> list[dict[str, object]]:
    for item in _walk(payload):
        if not isinstance(item, dict):
            continue
        for key, value in item.items():
            if key.lower() in names and isinstance(value, list):
                return _bounded_records(value, 30)
    return []


def _row_text(row: object) -> str:
    if isinstance(row, str):
        return row
    if isinstance(row, (list, tuple)):
        return " ".join(str(item) for item in row)
    if isinstance(row, dict):
        for keys in (("mnemonic", "operands"), ("address", "instruction"), ("addr", "text")):
            values = [row.get(key) for key in keys if row.get(key) is not None]
            if values:
                return " ".join(str(value) for value in values)
        return json.dumps(row, sort_keys=True)
    return str(row)


def _function_report(root: Path, address: int) -> dict[str, object] | None:
    payload = _read_json(root / REPORT_RELATIVE)
    rows = payload.get("data", []) if isinstance(payload, dict) else []
    for row in rows if isinstance(rows, list) else []:
        if not isinstance(row, dict):
            continue
        try:
            if int(str(row.get("address", "")), 16) == address:
                return row
        except ValueError:
            continue
    return None


def _changed_rows(group: dict[str, object], side: str) -> list[object]:
    value = group.get(side, [])
    return value if isinstance(value, list) else []


def _mismatch_class(diff: object, matching: float, effective: bool) -> str:
    if matching == 1.0:
        return "exact"
    if effective:
        return "effective"
    text = json.dumps(diff).lower()
    if re.search(r"\bcall\b", text):
        return "call-shape"
    if re.search(r"\b(?:j[a-z]{1,3}|cmp|test)\b", text):
        return "control-flow"
    if "[esp" in text or "[ebp" in text:
        return "stack-layout"
    if "(offset)" in text or "(data)" in text or "<offset" in text:
        return "data-reference"
    return "instruction-shape"


def _mismatch_summary(row: dict[str, object] | None) -> dict[str, object]:
    if row is None:
        return {
            "available": False,
            "class": "missing-report-entry",
            "taxonomy": empty_taxonomy(),
            "clusters": [],
        }
    try:
        matching = float(row.get("matching", 0.0))
    except (TypeError, ValueError):
        matching = 0.0
    diff = row.get("diff")
    clusters: list[dict[str, object]] = []
    if isinstance(diff, list):
        for hunk in diff[:6]:
            if not isinstance(hunk, (list, tuple)) or len(hunk) != 2:
                continue
            header, groups = hunk
            original = recompiled = 0
            examples: list[str] = []
            for group in groups if isinstance(groups, list) else []:
                if not isinstance(group, dict):
                    continue
                original += len(_changed_rows(group, "orig"))
                recompiled += len(_changed_rows(group, "recomp"))
                for side in ("orig", "recomp"):
                    examples.extend(_row_text(item)[:180] for item in _changed_rows(group, side)[:2])
            clusters.append(
                {
                    "header": str(header),
                    "original_changed_rows": original,
                    "recompiled_changed_rows": recompiled,
                    "examples": examples[:4],
                }
            )
    return {
        "available": True,
        "matching": matching,
        "effective": bool(row.get("effective")),
        "class": _mismatch_class(diff, matching, bool(row.get("effective"))),
        "taxonomy": classify_report_diff(diff),
        "clusters": clusters,
    }


def _campaign_history(
    root: Path,
    address_text: str,
    lane: str | None = None,
    mode: str | None = None,
) -> dict[str, object]:
    path = root / LEDGER_RELATIVE
    ledger_records: list[dict[str, object]] = []
    records: list[dict[str, object]] = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(row, dict):
                continue
            ledger_records.append(row)
            if row.get("record_type", "campaign") not in {
                "campaign",
                "abort",
            }:
                continue
            addresses = row.get("addresses", []) if isinstance(row, dict) else []
            normalized = []
            for value in addresses if isinstance(addresses, list) else []:
                try:
                    normalized.append(f"0x{int(str(value), 16):08X}")
                except ValueError:
                    continue
            if address_text in normalized:
                records.append(row)
    def severe_result(row: dict[str, object]) -> bool:
        if row.get("result") == "no-source":
            return True
        expected = float(row.get("expected_retained_bytes", 0.0) or 0.0)
        retained = sum(
            float(row.get(field, 0.0) or 0.0)
            for field in (
                "effective_bytes",
                "initialized_bytes",
                "resource_explained_bytes",
            )
        )
        return expected > 0 and retained / expected < 0.10

    zero_yield = sum(1 for row in records if severe_result(row))
    latest = records[-1] if records else {}
    cooldown = bool(records and severe_result(latest))
    retry_evidence: dict[str, object] | None = None
    attempts_count = len(records)
    minutes = sum(float(row.get("minutes", 0.0) or 0.0) for row in records)
    retained = sum(float(row.get("effective_bytes", 0.0) or 0.0) for row in records)
    if lane in {"closure", "production", "research"} and mode in {
        "coverage",
        "refinement",
    }:
        from tools.decomp_candidates import (
            campaign_attempts,
            retry_evidence_for_target,
        )

        address = int(address_text, 0)
        attempts = [
            attempt
            for attempt in campaign_attempts(ledger_records)
            if attempt.address == address
            and attempt.mode == mode
            and attempt.lane == lane
        ]
        attempts_count = len(attempts)
        minutes = sum(attempt.minutes for attempt in attempts)
        retained = sum(attempt.retained_bytes for attempt in attempts)
        zero_yield = sum(attempt.retained_bytes <= 0.0 for attempt in attempts)
        latest_attempt = attempts[-1] if attempts else None
        latest_failed = bool(
            latest_attempt is not None
            and (
                latest_attempt.result == "no-source"
                or (
                    latest_attempt.realization_ratio is not None
                    and latest_attempt.realization_ratio < 0.10
                )
            )
        )
        retry_evidence = (
            retry_evidence_for_target(
                ledger_records,
                address,
                mode,
                lane,
            )
            if latest_failed
            else None
        )
        cooldown = latest_failed and retry_evidence is None
    return {
        "attempts": attempts_count,
        "zero_yield_attempts": zero_yield,
        "minutes": minutes,
        "effective_bytes": retained,
        "cooldown": cooldown,
        "retry_eligible": retry_evidence is not None,
        "retry_evidence_id": (
            str(retry_evidence.get("evidence_id", ""))
            if retry_evidence is not None
            else ""
        ),
        "recent": [
            {key: row.get(key) for key in ("timestamp", "mode", "lane", "result", "minutes", "note")}
            for row in records[-5:]
        ],
    }


def _blocker(root: Path, address: int) -> dict[str, object] | None:
    path = root / BLOCKERS_RELATIVE
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        fields = line.split("\t")
        try:
            if int(fields[0], 16) != address:
                continue
        except (ValueError, IndexError):
            continue
        return {
            "blocked_by": fields[1].split(",") if len(fields) > 1 and fields[1] != "-" else [],
            "reason": fields[2] if len(fields) > 2 else "",
            "kind": fields[3] if len(fields) > 3 else "semantic",
        }
    return None


def _ledger_records(root: Path) -> list[dict[str, object]]:
    path = root / LEDGER_RELATIVE
    if not path.is_file():
        return []
    records: list[dict[str, object]] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(record, dict):
            records.append(record)
    return records


def _specific_blocker_route(
    blocker: dict[str, object] | None,
) -> dict[str, object] | None:
    if not isinstance(blocker, dict):
        return None
    dependencies: list[str] = []
    values = blocker.get("blocked_by")
    if isinstance(values, list):
        for value in values:
            try:
                address = int(str(value), 0)
            except (TypeError, ValueError):
                continue
            if 0 <= address <= 0xFFFFFFFF:
                dependencies.append(f"0x{address:08X}")
    reason = str(blocker.get("reason", "")).strip()
    if not dependencies and not reason:
        return None
    return {
        "kind": "blocker",
        "blocker_kind": str(blocker.get("kind", "semantic")).strip()
        or "semantic",
        "blocked_by": sorted(set(dependencies)),
        "reason": reason,
    }


def _current_selector_base_lane(root: Path, address: int) -> str:
    from tools.decomp_candidates import current_base_lane

    try:
        return current_base_lane(address, root=root)
    except (OSError, RuntimeError, ValueError) as error:
        raise BriefError(
            f"cannot determine the current selector base lane: {error}"
        ) from error


def research_route_evidence(
    root: Path,
    target: str | int,
    *,
    base_lane_resolver: Callable[[Path, int], str] = _current_selector_base_lane,
) -> dict[str, object]:
    """Return the current evidence that routes one target to research."""

    root = root.resolve()
    normalized = normalize_target("research", target)
    address = int(normalized, 0)
    annotation = _annotations(root).get(address)
    state = str(annotation.get("state", "UNSTARTED")) if annotation else "UNSTARTED"
    mode = "refinement" if state == "FUNCTION" else "coverage"
    subsystem = _source_subsystem(root, address)
    records = _ledger_records(root)
    routes: list[dict[str, object]] = []
    selector_base_lane: str | None = None

    blocker_route = _specific_blocker_route(_blocker(root, address))
    if blocker_route is not None:
        routes.append(blocker_route)

    from tools.decomp_candidates import (
        campaign_attempts,
        retry_evidence_for_target,
        source_circuit_open,
    )

    source_attempts = [
        attempt
        for attempt in campaign_attempts(records)
        if attempt.address == address
        and attempt.mode == mode
        and attempt.lane in {"closure", "production"}
        and attempt.calibration_eligible
    ]
    circuit_open = state == "FUNCTION" and source_circuit_open(records, subsystem)
    if state == "FUNCTION" and (source_attempts or circuit_open):
        selector_base_lane = base_lane_resolver(root, address)

    attempts = [
        attempt
        for attempt in source_attempts
        if attempt.lane == selector_base_lane
    ]
    latest = attempts[-1] if attempts else None
    retry_eligible = False
    if selector_base_lane in {"closure", "production"} and latest is not None:
        severe = latest.result == "no-source" or (
            latest.realization_ratio is not None
            and latest.realization_ratio < 0.10
        )
        retry_eligible = bool(
            severe
            and retry_evidence_for_target(
                records,
                address,
                mode,
                latest.lane,
            )
            is not None
        )
        if severe and not retry_eligible:
            routes.append(
                {
                    "kind": "cooldown",
                    "campaign_id": latest.campaign_id,
                    "lane": latest.lane,
                    "mode": latest.mode,
                    "result": latest.result,
                }
            )

    if (
        selector_base_lane in {"closure", "production"}
        and circuit_open
        and not retry_eligible
    ):
        routes.append({"kind": "circuit", "subsystem": subsystem})

    return {
        "eligible": bool(routes),
        "target": normalized,
        "state": state,
        "mode": mode,
        "subsystem": subsystem,
        "selector_base_lane": selector_base_lane,
        "history_sha256": _sha256(root / LEDGER_RELATIVE),
        "blocker_sha256": _sha256(root / BLOCKERS_RELATIVE),
        "routes": routes,
    }


def _tool_artifact(root: Path, address: int) -> str | None:
    path = root / ARTIFACTS_RELATIVE
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        fields = line.split("\t", 1)
        try:
            if int(fields[0], 16) == address:
                return fields[1].strip() if len(fields) > 1 else "tracked"
        except (ValueError, IndexError):
            continue
    return None


def _dwarf_evidence(name: str, root: Path, runner: Runner) -> dict[str, object]:
    if not DWARF_PATH.is_file():
        return {"available": False, "matches": []}
    terms = [term for term in re.split(r"::|[^A-Za-z0-9_]", name) if len(term) >= 4]
    term = terms[-1] if terms else name
    executable = shutil.which("rg")
    if not executable or not term:
        return {"available": True, "query": term, "matches": []}
    try:
        result = _run(
            runner,
            [executable, "-n", "-F", "-i", "-m", "8", term, str(DWARF_PATH)],
            root=root,
            timeout=12.0,
        )
    except (OSError, subprocess.SubprocessError):
        return {"available": True, "query": term, "matches": []}
    return {
        "available": True,
        "query": term,
        "matches": [line[:500] for line in _stdout(result).splitlines()[:8]],
    }


def _layout_clues(text: str) -> list[str]:
    patterns = (
        r"(?:->|\.)[A-Za-z_]\w*",
        r"(?:\+|-)\s*0x[0-9A-Fa-f]+",
        r"\b(?:int|uint|float|double|char)(?:8|16|32|64)?_t\b",
    )
    values: list[str] = []
    for pattern in patterns:
        values.extend(re.findall(pattern, text))
    return list(dict.fromkeys(values))[:40]


def _control_flow(rows: list[object], start: int, size: int | None) -> tuple[dict[str, object], list[str]]:
    texts = [_row_text(row) for row in rows]
    calls = [text for text in texts if re.search(r"\bcall\b", text, re.IGNORECASE)]
    branches = [text for text in texts if re.search(r"\bj(?:mp|[a-z]{1,3})\b", text, re.IGNORECASE)]
    returns = [text for text in texts if re.search(r"\bret\b", text, re.IGNORECASE)]
    back_edges = 0
    for row, text in zip(rows, texts):
        if re.search(r"\bj(?:mp|[a-z]{1,3})\b", text, re.IGNORECASE) is None:
            continue
        addresses = re.findall(r"0x([0-9A-Fa-f]{5,8})", text)
        current: int | None = None
        if isinstance(row, dict):
            value = row.get("address", row.get("addr"))
            try:
                current = int(str(value), 0)
            except (TypeError, ValueError):
                current = None
        if current is None and len(addresses) > 1:
            current = int(addresses[0], 16)
        target = int(addresses[-1], 16) if addresses else None
        in_function = target is not None and target >= start and (
            size is None or target < start + size
        )
        if current is not None and in_function and target < current:
            back_edges += 1
    control = {
        "instruction_count": len(rows),
        "branch_count": len(branches),
        "return_count": len(returns),
        "back_edge_count": back_edges,
        "retail_size": size,
        "epilogue": texts[-8:],
    }
    return control, calls


def _address_references(text: str, map_entries: list[tuple[int, str]]) -> list[dict[str, object]]:
    names = dict(map_entries)
    values: list[dict[str, object]] = []
    for match in re.finditer(r"0x([0-9A-Fa-f]{5,8})", text):
        address = int(match.group(1), 16)
        if address in names:
            values.append({"address": f"0x{address:08X}", "name": names[address]})
    unique = {(item["address"], item["name"]): item for item in values}
    return list(unique.values())[:30]


def _function_size(root: Path, address: int) -> int | None:
    payload = _read_json(root / SIZE_RELATIVE)
    rows = payload.get("data", []) if isinstance(payload, dict) else payload
    for row in rows if isinstance(rows, list) else []:
        if not isinstance(row, dict):
            continue
        try:
            if int(str(row.get("address", "")), 16) == address:
                return int(row.get("original_size", row.get("size", 0))) or None
        except (TypeError, ValueError):
            continue
    return None


def _tu_evidence(
    root: Path,
    address: int,
    annotation: dict[str, object] | None,
    annotations: dict[int, dict[str, object]],
    map_entries: list[tuple[int, str]],
) -> dict[str, object]:
    source = str(annotation.get("source", "")) if annotation else ""
    same_tu = [
        {"address": f"0x{item_address:08X}", "name": dict(map_entries).get(item_address, "")}
        for item_address, item in annotations.items()
        if source and item.get("source") == source and item_address != address
    ]
    nearby = sorted(map_entries, key=lambda item: abs(item[0] - address))
    return {
        "source": source or None,
        "line": annotation.get("line") if annotation else None,
        "source_hash": _sha256(root / source) if source else None,
        "same_translation_unit": same_tu[:12],
        "nearby_map_functions": [
            {"address": f"0x{item_address:08X}", "name": name}
            for item_address, name in nearby
            if item_address != address
        ][:6],
    }


def _global_names(root: Path) -> dict[int, str]:
    annotation = re.compile(r"//\s*GLOBAL:\s*TOY2\s+(0x[0-9A-Fa-f]+)")
    identifier = re.compile(r"\b([A-Za-z_]\w*)\s*(?:\[|=|;)")
    names: dict[int, str] = {}
    for path in sorted((root / "src").rglob("*")):
        if path.suffix.lower() not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        for index, line in enumerate(lines):
            match = annotation.search(line)
            if not match:
                continue
            for following in lines[index + 1 : index + 4]:
                found = identifier.search(following)
                if found:
                    names[int(match.group(1), 0)] = found.group(1)
                    break
    return names


def _source_readiness(
    lane: str,
    state: str,
    mismatch: dict[str, object],
    history: dict[str, object],
    blocker: dict[str, object] | None,
    artifact: str | None,
    source_debt: bool,
    evidence_ok: bool,
    retail_size: int | None = None,
    unresolved_bytes: float | None = None,
    weak_dependency_count: int = 0,
    actionable_mismatch: bool = False,
    research_route: Mapping[str, object] | None = None,
) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    matching = float(mismatch.get("matching", 0.0) or 0.0)
    if not evidence_ok:
        reasons.append("Ghidra did not return both source evidence forms")
    if mismatch.get("available") is not True:
        reasons.append("the target is absent from the function comparison report")
    if blocker and lane != "research":
        reasons.append("a reconstruction blocker is active")
    if history.get("cooldown") and lane != "research":
        reasons.append("the latest attempt places this target on cooldown")
    if lane == "production" and state != "FUNCTION":
        reasons.append("production refinement needs an implemented function")
    if lane == "production" and not 0.5 <= matching <= 0.9:
        reasons.append("production refinement needs a score from 50 through 90 percent")
    if lane == "production" and (
        retail_size is None or not 300 <= retail_size <= 3000
    ):
        reasons.append("production refinement needs a retail body from 300 through 3000 bytes")
    if lane == "production" and (
        unresolved_bytes is None or unresolved_bytes < 100
    ):
        reasons.append("production refinement needs at least 100 unresolved bytes")
    if lane == "production" and weak_dependency_count > 2:
        reasons.append("production refinement permits at most two weak dependencies")
    if lane == "production" and not actionable_mismatch:
        reasons.append("production refinement needs a current actionable mismatch")
    if lane == "closure" and state != "FUNCTION":
        reasons.append("closure needs an implemented function")
    if (
        lane == "closure"
        and matching <= 0.99
        and not artifact
        and not (
            source_debt
            and (matching == 1.0 or bool(mismatch.get("effective")))
        )
    ):
        reasons.append(
            "closure needs above 99 percent similarity, a tool artifact, or source debt"
        )
    if lane == "research" and mismatch.get("class") in {"exact", "effective"}:
        reasons.append("research needs a nonterminal mismatch")
    if lane == "research" and (
        not isinstance(research_route, Mapping)
        or research_route.get("eligible") is not True
    ):
        reasons.append(
            "research needs an active blocker, a target cooldown, or an open subsystem circuit"
        )
    return not reasons, reasons


def _source_debt(root: Path, address: int) -> list[dict[str, object]]:
    from tools import decomp_lint

    units = [
        decomp_lint.SourceUnit(
            path, path.read_text(encoding="utf-8", errors="ignore")
        )
        for path in sorted((root / "src").rglob("*"))
        if path.is_file() and path.suffix in decomp_lint.SOURCE_SUFFIXES
    ]
    findings = decomp_lint.scan_units(units)
    findings, _ = decomp_lint.apply_baseline(
        findings,
        decomp_lint.read_baseline(root / ".notes/lint-baseline.tsv"),
    )
    target = f"0x{address:08x}"
    return [
        {
            "rule": finding.rule,
            "severity": finding.severity,
            "legacy": finding.legacy,
            "suppressed": finding.suppressed,
        }
        for finding in findings
        if finding.owner_address.lower() == target and not finding.suppressed
    ]


def _source_evidence(
    lane: str,
    target: str,
    root: Path,
    runner: Runner,
    doctor_artifacts: Mapping[str, object] | None = None,
) -> dict[str, object]:
    address = int(target, 0)
    map_entries = _parse_map(root)
    map_names = dict(map_entries)
    annotations = _annotations(root)
    annotation = annotations.get(address)
    if doctor_artifacts is not None:
        try:
            disassembly = load_ghidra_artifact(
                root,
                target,
                "disassembly",
                doctor_artifacts.get("disassembly"),
            )
            decompilation = load_ghidra_artifact(
                root,
                target,
                "decompilation",
                doctor_artifacts.get("decompilation"),
            )
            function = load_ghidra_artifact(
                root,
                target,
                "function",
                doctor_artifacts.get("function"),
            )
            callers = load_ghidra_artifact(
                root,
                target,
                "xrefs",
                doctor_artifacts.get("xrefs"),
            )
        except ValueError as error:
            raise BriefError(f"the doctor receipt artifact is invalid: {error}") from error
    else:
        disassembly = decompilation = function = callers = None
    executable = shutil.which("ghidra")
    environment, _ = ghidra_environment(root)
    if executable and doctor_artifacts is None:
        for name in ("XDG_CACHE_HOME", "XDG_DATA_HOME", "GHIDRA_CLI_LOG_DIR"):
            Path(environment[name]).mkdir(parents=True, exist_ok=True)
        mirror_ghidra_bridge_markers(root)
        prune_ghidra_logs(root)
        try:
            function = _json_command(
                runner,
                [executable, "--json", "function", "get", target],
                root=root,
                environment=environment,
            )
            callers = _json_command(
                runner,
                [executable, "--json", "x-ref", "to", target],
                root=root,
                environment=environment,
            )
            disassembly = _json_command(
                runner,
                [executable, "--json", "function", "disasm", target],
                root=root,
                environment=environment,
            )
            decompilation = _json_command(
                runner,
                [executable, "--json", "decompile", target, "--with-params", "--with-vars"],
                root=root,
                environment=environment,
            )
        finally:
            prune_ghidra_logs(root)
    elif doctor_artifacts is None:
        prune_ghidra_logs(root)
    rows = _instruction_rows(disassembly)
    code = _code(decompilation)
    evidence_ok = bool(rows and code)
    size = _function_size(root, address)
    control, call_rows = _control_flow(rows, address, size)
    callees = _address_references("\n".join(call_rows), map_entries)
    combined = "\n".join([*(_row_text(row) for row in rows), code])
    mismatch = _mismatch_summary(_function_report(root, address))
    blocker = _blocker(root, address)
    artifact = _tool_artifact(root, address)
    source_debt = _source_debt(root, address) if lane == "closure" else []
    state = str(annotation.get("state", "UNSTARTED")) if annotation else "UNSTARTED"
    source_mode = "refinement" if state == "FUNCTION" else "coverage"
    history = _campaign_history(root, target, lane, source_mode)
    research_route = (
        research_route_evidence(root, target) if lane == "research" else None
    )
    weak_dependencies = []
    for callee in callees:
        try:
            callee_address = int(str(callee["address"]), 0)
        except (KeyError, TypeError, ValueError):
            continue
        callee_annotation = annotations.get(callee_address)
        if not callee_annotation or callee_annotation.get("state") != "FUNCTION":
            continue
        callee_mismatch = _mismatch_summary(_function_report(root, callee_address))
        if (
            float(callee_mismatch.get("matching", 0.0) or 0.0) < 1.0
            and not callee_mismatch.get("effective")
        ):
            weak_dependencies.append(str(callee["address"]))
    actionable_mismatch = False
    if lane == "production":
        from tools.decomp_candidates import (
            mismatch_artifact_is_current,
            read_actionable_mismatches,
        )

        saved = read_actionable_mismatches(
            root / "build/decomp-diffs",
            root=root,
            require_provenance=True,
        ).get(address)
        actionable_mismatch = bool(
            saved is not None
            and mismatch_artifact_is_current(
                saved, float(mismatch.get("matching", 0.0) or 0.0)
            )
        )
    unresolved_bytes = (
        size * max(0.0, 1.0 - float(mismatch.get("matching", 0.0) or 0.0))
        if size is not None
        else None
    )
    ready, rejection = _source_readiness(
        lane,
        state,
        mismatch,
        history,
        blocker,
        artifact,
        bool(source_debt),
        evidence_ok,
        size,
        unresolved_bytes,
        len(weak_dependencies),
        actionable_mismatch,
        research_route,
    )
    signature = code.splitlines()[0].strip()[:500] if code else None
    global_names = _global_names(root)
    global_addresses = list(
        dict.fromkeys(
            int(value, 16)
            for value in re.findall(r"0x(00[5-9A-Fa-f][0-9A-Fa-f]{5})", combined)
        )
    )[:40]
    return {
        "identity": {
            "name": map_names.get(address, ""),
            "state": state,
            "annotation": annotation,
            "ghidra_function": function,
        },
        "abi": {
            "signature": signature,
            "calling_convention": _first_scalar(
                [function, decompilation], {"calling_convention", "callingconvention"}
            ),
            "return_type": _first_scalar(
                [function, decompilation], {"return_type", "returntype"}
            ),
            "parameters": _named_list(decompilation, {"parameters", "params"}),
            "variables": _named_list(decompilation, {"variables", "vars", "locals"}),
            "epilogue": control["epilogue"],
        },
        "control_flow": control,
        "callers": _bounded_records(callers),
        "callees": callees,
        "globals": [
            {"address": f"0x{value:08X}", "name": global_names.get(value)}
            for value in global_addresses
        ],
        "layout": {"clues": _layout_clues(code)},
        "dwarf": _dwarf_evidence(map_names.get(address, ""), root, runner),
        "mismatch": mismatch,
        "campaign": history,
        "translation_unit": _tu_evidence(root, address, annotation, annotations, map_entries),
        "blocker": blocker,
        "tool_artifact": artifact,
        "source_debt": source_debt,
        "production_gates": {
            "retail_size": size,
            "unresolved_bytes": unresolved_bytes,
            "weak_dependencies": weak_dependencies,
            "actionable_mismatch": actionable_mismatch,
        },
        "research_route": research_route,
        "source_evidence": {
            "disassembly_nonempty": bool(rows),
            "decompilation_nonempty": bool(code),
            "instruction_count": len(rows),
            "decompilation_characters": len(code),
            "origin": "doctor-receipt" if doctor_artifacts is not None else "live-query",
        },
        "readiness": ready,
        "rejection_reasons": rejection,
    }


def _find_address_rows(value: object, address: int) -> list[dict[str, object]]:
    matches: list[dict[str, object]] = []
    for item in _walk(value):
        if not isinstance(item, dict):
            continue
        for key in ("address", "original_address", "orig_addr"):
            if key not in item:
                continue
            try:
                if int(str(item[key]), 0) == address:
                    matches.append(item)
                    break
            except (TypeError, ValueError):
                continue
    return matches


def _typed_data_rows(payload: object, address: int) -> list[dict[str, object]]:
    if not isinstance(payload, dict):
        return []
    variables = payload.get("variables")
    if not isinstance(variables, dict):
        return []
    rows = variables.get("variables")
    if not isinstance(rows, list):
        return []
    matches = _find_address_rows(rows, address)
    typed: list[dict[str, object]] = []
    for row in matches:
        try:
            size = int(row.get("size", 0))
        except (TypeError, ValueError):
            continue
        if size > 0 and row.get("raw_only") is False and not row.get("error"):
            typed.append(row)
    return typed


def _data_callers(target: str, root: Path, runner: Runner) -> list[dict[str, object]]:
    executable = shutil.which("ghidra")
    if not executable:
        return []
    environment, _ = ghidra_environment(root)
    for name in ("XDG_CACHE_HOME", "XDG_DATA_HOME", "GHIDRA_CLI_LOG_DIR"):
        Path(environment[name]).mkdir(parents=True, exist_ok=True)
    mirror_ghidra_bridge_markers(root)
    prune_ghidra_logs(root)
    payload = _json_command(
        runner,
        [executable, "--json", "x-ref", "to", target],
        root=root,
        environment=environment,
    )
    prune_ghidra_logs(root)
    return _bounded_records(payload)


def _data_evidence(
    target: str,
    root: Path,
    runner: Runner = _default_runner,
    doctor_artifacts: Mapping[str, object] | None = None,
) -> dict[str, object]:
    address = int(target, 0)
    payload = _read_json(root / DATA_REPORT_RELATIVE)
    rows = _typed_data_rows(payload, address)
    if rows and doctor_artifacts is not None:
        try:
            xrefs = load_ghidra_artifact(
                root,
                target,
                "xrefs",
                doctor_artifacts.get("xrefs"),
            )
        except ValueError as error:
            raise BriefError(f"the doctor receipt artifact is invalid: {error}") from error
        callers = _bounded_records(xrefs)
    else:
        callers = _data_callers(target, root, runner) if rows else []
    name = str(rows[0].get("name", "")) if rows else ""
    dwarf = (
        _dwarf_evidence(name, root, runner)
        if name
        else {"available": DWARF_PATH.is_file(), "matches": []}
    )
    dwarf_matches = dwarf.get("matches", []) if isinstance(dwarf, dict) else []
    rejection: list[str] = []
    if not rows:
        rejection.append("the typed data report does not score retail bytes at this address")
    if rows and not callers and not dwarf_matches:
        rejection.append("data work needs caller or DWARF evidence")
    return {
        "abi": None,
        "control_flow": None,
        "callers": callers,
        "callees": [],
        "globals": [{key: value for key, value in row.items() if key != "diff"} for row in rows[:6]],
        "layout": {"report_rows": rows[:6]},
        "dwarf": dwarf,
        "mismatch": {"available": bool(rows), "class": "typed-data", "clusters": []},
        "campaign": _campaign_history(root, target),
        "translation_unit": {"source": None, "same_translation_unit": []},
        "source_evidence": {"disassembly_nonempty": False, "decompilation_nonempty": False},
        "readiness": not rejection,
        "rejection_reasons": rejection,
    }


def _resource_evidence(target: str, root: Path) -> dict[str, object]:
    from tools import decomp_resources

    resource = decomp_resources.parse_resource(target)
    source_files = sorted(
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in {".rc", ".ico", ".bmp", ".cur"}
        and "build/" not in path.relative_to(root).as_posix()
    )
    rows: list[dict[str, object]] = []
    score_error: str | None = None
    try:
        rows = decomp_resources.resource_rows(
            root / "original/toy2.exe", root / "build/toy2.exe"
        )
    except (OSError, ValueError) as error:
        score_error = str(error)
    selected = decomp_resources.selected_evidence(rows, resource)
    leaf_count = int(selected["leaf_count"])
    scored_bytes = int(selected["scored_bytes"])
    explained_bytes = int(selected["explained_bytes"])
    score = explained_bytes / scored_bytes if scored_bytes else 0.0
    rejection: list[str] = []
    if score_error:
        rejection.append("the selected resource leaf cannot be scored")
    elif leaf_count == 0:
        rejection.append("the selected resource leaf is not present in the retail executable")
    elif scored_bytes <= 0:
        rejection.append("the selected resource leaf has no improvable retail bytes")
    elif explained_bytes >= scored_bytes:
        rejection.append("the selected resource leaf is already exact")
    return {
        "abi": None,
        "control_flow": None,
        "callers": [],
        "callees": [],
        "globals": [],
        "layout": {"resource_tuple": target, "selected_evidence": selected},
        "dwarf": {"available": False, "matches": []},
        "mismatch": {
            "available": score_error is None and leaf_count > 0,
            "matching": score,
            "effective": False,
            "class": "exact" if leaf_count > 0 and scored_bytes > 0 and score == 1.0 else "resource-leaf",
            "clusters": [],
            "score_error": score_error,
        },
        "campaign": {"attempts": 0, "cooldown": False, "recent": []},
        "translation_unit": {"source_files": source_files[:30]},
        "source_evidence": {"disassembly_nonempty": False, "decompilation_nonempty": False},
        "readiness": not rejection,
        "rejection_reasons": rejection,
    }


def gather_evidence(
    lane: str,
    target: str,
    root: Path,
    runner: Runner,
    *,
    doctor_artifacts: Mapping[str, object] | None = None,
) -> dict[str, object]:
    if lane in SOURCE_LANES:
        return _source_evidence(
            lane,
            target,
            root,
            runner,
            doctor_artifacts=doctor_artifacts,
        )
    if lane == "data":
        return _data_evidence(
            target,
            root,
            runner,
            doctor_artifacts=doctor_artifacts,
        )
    return _resource_evidence(target, root)


class _CliParseError(ValueError):
    pass


class _CliParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise _CliParseError(message)


def _parse_cli_target(value: str) -> str:
    text = value.strip()
    if "," in text:
        fields = text.split(",")
        try:
            numbers = [int(field, 0) for field in fields]
        except ValueError as error:
            raise argparse.ArgumentTypeError(
                "use an address or TYPE,ID,LANGUAGE integers"
            ) from error
        if len(numbers) != 3 or any(
            number < 0 or number > 0xFFFFFFFF for number in numbers
        ):
            raise argparse.ArgumentTypeError(
                "use an address or TYPE,ID,LANGUAGE integers"
            )
        return ",".join(str(number) for number in numbers)
    try:
        address = int(text, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "use a target address such as 0x00401000"
        ) from error
    if address < 0 or address > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("the target address must fit in 32 bits")
    return f"0x{address:08X}"


def _parser() -> argparse.ArgumentParser:
    parser = _CliParser(description=__doc__)
    parser.add_argument("--lane", choices=LANES, required=True)
    parser.add_argument("--target", type=_parse_cli_target, required=True)
    parser.add_argument(
        "--doctor-receipt",
        type=Path,
        required=True,
        help="Ready doctor receipt for this target",
    )
    parser.add_argument(
        "--scout-report",
        type=Path,
        action="append",
        required=True,
        help="Read-only scout report for this target. Use this option two times.",
    )
    parser.add_argument("--json", action="store_true", help="Print the full brief as JSON")
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    return parser


def _print_parse_error(
    parser: argparse.ArgumentParser, message: str, *, as_json: bool
) -> None:
    if as_json:
        print(json.dumps({"ok": False, "error": message}, sort_keys=True))
        return
    parser.print_usage(sys.stderr)
    print(f"{parser.prog}: error: {message}", file=sys.stderr)


def main() -> int:
    parser = _parser()
    wants_json = "--json" in sys.argv[1:]
    try:
        arguments = parser.parse_args()
    except _CliParseError as error:
        _print_parse_error(parser, str(error), as_json=wants_json)
        return 2
    if len(arguments.scout_report) != 2:
        error = "provide exactly two --scout-report options"
        if arguments.json:
            print(json.dumps({"ok": False, "error": error}, sort_keys=True))
        else:
            print(f"brief failed: {error}", file=sys.stderr)
        return 1
    try:
        result = build_brief(
            arguments.lane,
            arguments.target,
            root=arguments.root,
            doctor_receipt_path=arguments.doctor_receipt,
            scout_report_paths=arguments.scout_report,
        )
    except BriefError as error:
        if arguments.json:
            print(json.dumps({"ok": False, "error": str(error)}, sort_keys=True))
        else:
            print(f"brief failed: {error}", file=sys.stderr)
        return 1
    if arguments.json:
        print(
            json.dumps(
                {
                    "ok": True,
                    "cache": "warm" if result.cache_hit else "cold",
                    "elapsed_seconds": result.elapsed_seconds,
                    "path": str(result.path.relative_to(arguments.root.resolve())),
                    "brief": result.brief,
                },
                indent=2,
                sort_keys=True,
            )
        )
    else:
        state = "warm" if result.cache_hit else "cold"
        evidence = result.brief.get("evidence", {})
        print(f"brief {state}: {arguments.lane} {result.brief['target']}")
        print(f"ready: {evidence.get('readiness', False)}")
        for reason in evidence.get("rejection_reasons", []):
            print(f"  reject: {reason}")
        print(f"cache: {result.path.relative_to(arguments.root.resolve())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
