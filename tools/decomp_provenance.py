#!/usr/bin/env python3
"""Bind comparison artifacts to the source and build that produced them."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Mapping

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_campaigns import (  # noqa: E402
    _snapshot_hash,
    file_hash,
    resource_source_snapshot,
    source_index_snapshot,
    source_worktree_snapshot,
)

SCHEMA_VERSION = 1


def provenance_path(artifact: Path) -> Path:
    """Return the sidecar path for one comparison artifact."""

    return artifact.with_name(f"{artifact.name}.provenance.json")


def _head(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    value = result.stdout.strip().lower()
    if result.returncode != 0 or re.fullmatch(r"[0-9a-f]{40,64}", value) is None:
        raise ValueError("cannot read the comparison HEAD")
    return value


def _directory_identity(path: Path) -> dict[str, object]:
    """Return a content identity for a Git-backed runtime directory."""

    if not path.is_dir():
        return {"path": str(path.resolve()), "missing": True}
    head = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    status = subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain=v2", "-z"],
        check=False,
        capture_output=True,
    )
    worktree = subprocess.run(
        ["git", "-C", str(path), "diff", "--binary", "HEAD", "--"],
        check=False,
        capture_output=True,
    )
    untracked = subprocess.run(
        ["git", "-C", str(path), "ls-files", "-z", "--others", "--exclude-standard"],
        check=False,
        capture_output=True,
    )
    content = hashlib.sha256()
    content.update(worktree.stdout)
    if untracked.returncode == 0:
        for encoded in sorted(value for value in untracked.stdout.split(b"\0") if value):
            relative = Path(encoded.decode("utf-8", errors="surrogateescape"))
            candidate = path / relative
            content.update(encoded)
            if candidate.is_file() and not candidate.is_symlink():
                with candidate.open("rb") as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        content.update(chunk)
            elif candidate.is_symlink():
                content.update(str(candidate.readlink()).encode("utf-8"))
    return {
        "path": str(path.resolve()),
        "head": head.stdout.strip().lower() if head.returncode == 0 else None,
        "status_sha256": (
            hashlib.sha256(status.stdout).hexdigest()
            if status.returncode == 0
            else None
        ),
        "worktree_sha256": (
            content.hexdigest()
            if worktree.returncode == 0 and untracked.returncode == 0
            else None
        ),
    }


def _package_tree_hash(path: Path | None) -> str | None:
    if path is None or not path.is_dir():
        return None
    digest = hashlib.sha256()
    for candidate in sorted(path.rglob("*")):
        if (
            not candidate.is_file()
            or "__pycache__" in candidate.parts
            or candidate.suffix in {".pyc", ".pyo"}
        ):
            continue
        relative = candidate.relative_to(path).as_posix()
        digest.update(relative.encode("utf-8"))
        with candidate.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def _reccmp_runtime_identity(root: Path) -> dict[str, object]:
    spec = importlib.util.find_spec("reccmp")
    origin = Path(spec.origin).resolve() if spec and spec.origin else None
    executables: dict[str, dict[str, object]] = {}
    script_directory = (
        root / ".tooling" / "venv" / ("Scripts" if os.name == "nt" else "bin")
    )
    for name in ("reccmp-project", "reccmp-reccmp"):
        local_names = (f"{name}.exe", name) if os.name == "nt" else (name,)
        local = next(
            (
                script_directory / candidate
                for candidate in local_names
                if (script_directory / candidate).is_file()
            ),
            None,
        )
        found = str(local) if local is not None else shutil.which(name)
        resolved = Path(found).resolve() if found is not None else None
        executables[name] = {
            "path": str(resolved) if resolved is not None else None,
            "sha256": file_hash(resolved) if resolved is not None else None,
        }
    return {
        "module_origin": str(origin) if origin is not None else None,
        "module_sha256": file_hash(origin) if origin is not None else None,
        "package_sha256": _package_tree_hash(origin.parent if origin else None),
        "checkout": _directory_identity(root / "external/submodules/reccmp"),
        "executables": executables,
    }


def validation_tool_identity(
    root: Path = ROOT, *, require_reccmp_user: bool = True
) -> dict[str, object]:
    """Return the validator and comparator identity outside product source."""

    root = root.resolve()
    relatives = (
        "reccmp-project.yml",
        "reccmp-user.yml",
        "tools/__init__.py",
        "tools/decomp",
        "tools/decomp.ps1",
        "tools/decomp_campaigns.py",
        "tools/decomp_diff.py",
        "tools/decomp_lint.py",
        "tools/decomp_oracle.py",
        "tools/decomp_status.py",
        "tools/decomp_verify.py",
        "tools/decomp_provenance.py",
        "tools/generate-decomp-data-report.py",
        "tools/decomp_resources.py",
        "tools/ghidra_sync.py",
        "tools/Resources/leaf-oracles.json",
    )
    return {
        "files": {
            relative: (
                file_hash(root / relative) if (root / relative).is_file() else None
            )
            for relative in relatives
        },
        "reccmp_user": (
            reccmp_user_identity(root) if require_reccmp_user else None
        ),
        "reccmp_runtime": _reccmp_runtime_identity(root),
    }


def reccmp_user_identity(root: Path = ROOT) -> dict[str, object]:
    """Validate and identify the retail path that reccmp uses for TOY2."""

    root = root.resolve()
    config = root / "reccmp-user.yml"
    project_config = root / "reccmp-project.yml"
    if not config.is_file():
        raise ValueError("reccmp-user.yml is missing")
    if not project_config.is_file():
        raise ValueError("reccmp-project.yml is missing")
    try:
        from reccmp.project.config import ProjectFile, UserFile

        document = UserFile.from_file(config)
        project = ProjectFile.from_file(project_config)
    except Exception as error:
        raise ValueError(f"the reccmp project configuration is invalid: {error}") from error
    target = document.targets.get("TOY2")
    if target is None:
        raise ValueError("reccmp-user.yml has no TOY2 target")
    project_target = project.targets.get("TOY2")
    if project_target is None:
        raise ValueError("reccmp-project.yml has no TOY2 target")
    configured = Path(target.path)
    if configured.is_absolute():
        raise ValueError("the TOY2 retail path in reccmp-user.yml must be relative")
    resolved = (config.parent / configured).resolve()
    canonical = (root / "original/toy2.exe").resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValueError("the TOY2 retail path escapes the repository") from error
    if resolved != canonical:
        raise ValueError(
            "the TOY2 retail path in reccmp-user.yml must be original/toy2.exe"
        )
    if not canonical.is_file():
        raise ValueError("the configured TOY2 retail executable is missing")
    expected_hash = str(project_target.hash.sha256).lower()
    if re.fullmatch(r"[0-9a-f]{64}", expected_hash) is None:
        raise ValueError("reccmp-project.yml has an invalid TOY2 retail SHA-256")
    retail_hash = file_hash(canonical)
    if retail_hash != expected_hash:
        raise ValueError(
            "the configured TOY2 retail executable does not match reccmp-project.yml"
        )
    return {
        "config_path": "reccmp-user.yml",
        "config_sha256": file_hash(config),
        "project_path": "reccmp-project.yml",
        "project_sha256": file_hash(project_config),
        "retail_path": "original/toy2.exe",
        "retail_sha256": retail_hash,
        "retail_expected_sha256": expected_hash,
    }


def current_identity(root: Path = ROOT) -> dict[str, object]:
    """Return the repository and binary identity used by a comparison."""

    root = root.resolve()
    user_identity = reccmp_user_identity(root)
    files = {
        name: file_hash(root / relative) if (root / relative).is_file() else None
        for name, relative in {
            "retail_executable": "original/toy2.exe",
            "recompiled_executable": "build/toy2.exe",
            "recompiled_symbols": "build/toy2.pdb",
            "reccmp_build": "build/reccmp-build.yml",
            "reccmp_project": "reccmp-project.yml",
            "reccmp_user": "reccmp-user.yml",
            "functions_map": "tools/Resources/functions_map.txt",
            "function_sizes": "build/decomp-function-sizes.json",
            "data_report_generator": "tools/generate-decomp-data-report.py",
            "resource_tool": "tools/decomp_resources.py",
        }.items()
    }
    return {
        "root": str(root),
        "head": _head(root),
        "source_worktree_sha256": _snapshot_hash(source_worktree_snapshot(root)),
        "source_index_sha256": _snapshot_hash(source_index_snapshot(root)),
        "resource_sources_sha256": _snapshot_hash(resource_source_snapshot(root)),
        "validation_tools": validation_tool_identity(root),
        "reccmp_user": user_identity,
        "files": files,
    }


def _receipt_id(document: Mapping[str, object]) -> str:
    value = dict(document)
    value.pop("receipt_id", None)
    encoded = json.dumps(
        value,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _write_sidecar(path: Path, document: dict[str, object]) -> None:
    document["receipt_id"] = _receipt_id(document)
    encoded = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode(
        "utf-8"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    finally:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass


def _read_sidecar(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read comparison provenance: {error}") from error
    if not isinstance(document, dict):
        raise ValueError("comparison provenance must be a JSON object")
    if document.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("comparison provenance schema is invalid")
    if document.get("receipt_id") != _receipt_id(document):
        raise ValueError("comparison provenance identity is invalid")
    return document


def _artifact_state(path: Path) -> dict[str, object]:
    resolved = path.resolve()
    if not resolved.is_file():
        return {"path": str(resolved), "exists": False}
    stat = resolved.stat()
    return {
        "path": str(resolved),
        "exists": True,
        "sha256": file_hash(resolved),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "inode": getattr(stat, "st_ino", None),
    }


def capture_input(
    path: Path, output: Path, *, root: Path = ROOT
) -> dict[str, object]:
    """Save the identity that must stay unchanged during report generation."""

    document: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": "comparison-input",
        "created_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "input_identity": current_identity(root),
        "output_before": _artifact_state(output),
    }
    _write_sidecar(path, document)
    return document


def _captured_input(path: Path) -> tuple[dict[str, object], dict[str, object]]:
    document = _read_sidecar(path)
    if document.get("kind") != "comparison-input":
        raise ValueError("the comparison input receipt kind is invalid")
    identity = document.get("input_identity")
    output = document.get("output_before")
    if not isinstance(identity, dict) or not isinstance(output, dict):
        raise ValueError("the comparison input receipt has no identity")
    return dict(identity), dict(output)


def seal_report(
    report: Path,
    *,
    root: Path = ROOT,
    expected_identity: Mapping[str, object] | None = None,
    expected_artifact: Mapping[str, object] | None = None,
    input_receipt: Path | None = None,
) -> dict[str, object]:
    """Write current source and build provenance for one JSON report."""

    report = report.resolve()
    report_hash = file_hash(report)
    if report_hash is None:
        raise ValueError(f"the comparison report does not exist: {report}")
    try:
        value = json.loads(report.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"the comparison report is invalid: {error}") from error
    if not value:
        raise ValueError("the comparison report is empty")
    if expected_identity is not None and input_receipt is not None:
        raise ValueError("use one expected comparison identity")
    if expected_identity is None and input_receipt is None:
        raise ValueError(
            "report sealing needs an input receipt captured before generation"
        )
    if input_receipt is not None:
        before, artifact_before = _captured_input(input_receipt)
    else:
        before = dict(expected_identity or {})
        artifact_before = dict(expected_artifact or {})
    if artifact_before.get("path") != str(report):
        raise ValueError("the comparison input receipt names another output")
    after = current_identity(root)
    if before != after:
        raise ValueError("comparison inputs changed while the report was generated")
    if artifact_before == _artifact_state(report):
        raise ValueError("the report generator did not replace or update its output")
    document: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": "comparison-report",
        "created_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "artifact": {"path": str(report), "sha256": report_hash},
        "input_identity": after,
    }
    sidecar = provenance_path(report)
    _write_sidecar(sidecar, document)
    return document


def validate_report_artifact(report: Path) -> dict[str, object]:
    """Validate a sealed report without comparing it with current inputs."""

    report = report.resolve()
    document = _read_sidecar(provenance_path(report))
    if document.get("kind") != "comparison-report":
        raise ValueError("the report provenance kind is invalid")
    if document.get("artifact") != {
        "path": str(report),
        "sha256": file_hash(report),
    }:
        raise ValueError("the comparison report changed after it was sealed")
    if not isinstance(document.get("input_identity"), dict):
        raise ValueError("the comparison report has no input identity")
    return document


def validate_report(
    report: Path,
    *,
    root: Path = ROOT,
    current: Mapping[str, object] | None = None,
) -> dict[str, object]:
    """Reject a report that does not describe the current source and build."""

    document = validate_report_artifact(report)
    identity = dict(current) if current is not None else current_identity(root)
    if document.get("input_identity") != identity:
        raise ValueError("the comparison report source or build identity is stale")
    return document


def seal_diff(
    diff: Path,
    address: str,
    report: Path,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Bind one verbose diff to a current canonical comparison row."""

    try:
        normalized = f"0x{int(address, 0):08X}"
    except (TypeError, ValueError) as error:
        raise ValueError("the mismatch address is invalid") from error
    diff = diff.resolve()
    report = report.resolve()
    diff_hash = file_hash(diff)
    if diff_hash is None:
        raise ValueError(f"the saved mismatch does not exist: {diff}")
    identity = current_identity(root)
    report_receipt = validate_report(report, root=root, current=identity)
    document: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": "function-diff",
        "created_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "address": normalized,
        "artifact": {"path": str(diff), "sha256": diff_hash},
        "report": {
            "path": str(report),
            "sha256": file_hash(report),
            "provenance_sha256": file_hash(provenance_path(report)),
            "receipt_id": report_receipt.get("receipt_id"),
        },
        "input_identity": identity,
    }
    _write_sidecar(provenance_path(diff), document)
    return document


def validate_diff(
    diff: Path,
    address: int,
    *,
    current_match: float | None = None,
    root: Path = ROOT,
    current: Mapping[str, object] | None = None,
    report_receipt: Mapping[str, object] | None = None,
) -> dict[str, object]:
    """Reject a mismatch diff without current, report-bound provenance."""

    diff = diff.resolve()
    document = _read_sidecar(provenance_path(diff))
    normalized = f"0x{address:08X}"
    if document.get("kind") != "function-diff" or document.get("address") != normalized:
        raise ValueError("the mismatch provenance target is invalid")
    if document.get("artifact") != {
        "path": str(diff),
        "sha256": file_hash(diff),
    }:
        raise ValueError("the saved mismatch changed after it was sealed")
    identity = dict(current) if current is not None else current_identity(root)
    if document.get("input_identity") != identity:
        raise ValueError("the saved mismatch source or build identity is stale")
    report_descriptor = document.get("report")
    if not isinstance(report_descriptor, dict):
        raise ValueError("the saved mismatch has no report identity")
    report_path = report_descriptor.get("path")
    if not isinstance(report_path, str):
        raise ValueError("the saved mismatch report path is invalid")
    report = Path(report_path)
    validated_report = (
        dict(report_receipt)
        if report_receipt is not None
        else validate_report(report, root=root, current=identity)
    )
    if validated_report.get("artifact") != {
        "path": str(report.resolve()),
        "sha256": file_hash(report),
    } or validated_report.get("input_identity") != identity:
        raise ValueError("the saved mismatch report receipt is invalid")
    expected_report = {
        "path": str(report.resolve()),
        "sha256": file_hash(report),
        "provenance_sha256": file_hash(provenance_path(report)),
        "receipt_id": validated_report.get("receipt_id"),
    }
    if report_descriptor != expected_report:
        raise ValueError("the saved mismatch report identity changed")

    from tools.decomp_diff import read_similarity
    from tools.decomp_status import read_match_statuses

    diff_score = read_similarity(diff.read_text(encoding="utf-8", errors="replace"))
    report_status = read_match_statuses(report).get(address)
    if diff_score is None or report_status is None:
        raise ValueError("the saved mismatch has no matching comparison row")
    if not math.isclose(diff_score, report_status.matching, abs_tol=0.000051):
        raise ValueError("the saved mismatch score disagrees with its report")
    if current_match is not None and not math.isclose(
        report_status.matching, current_match, abs_tol=0.000051
    ):
        raise ValueError("the saved mismatch score is stale")
    return document


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    capture = subparsers.add_parser("capture-input")
    capture.add_argument("path", type=Path)
    capture.add_argument("--output", required=True, type=Path)
    report = subparsers.add_parser("seal-report")
    report.add_argument("report", type=Path)
    report.add_argument("--input-receipt", required=True, type=Path)
    check = subparsers.add_parser("validate-report")
    check.add_argument("report", type=Path)
    diff = subparsers.add_parser("seal-diff")
    diff.add_argument("diff", type=Path)
    diff.add_argument("--address", required=True)
    diff.add_argument("--report", required=True, type=Path)
    return parser


def main() -> int:
    arguments = _parser().parse_args()
    try:
        if arguments.command == "capture-input":
            document = capture_input(arguments.path, arguments.output)
        elif arguments.command == "seal-report":
            document = seal_report(
                arguments.report,
                input_receipt=arguments.input_receipt,
            )
        elif arguments.command == "validate-report":
            document = validate_report(arguments.report)
        else:
            document = seal_diff(
                arguments.diff,
                arguments.address,
                arguments.report,
            )
    except ValueError as error:
        print(f"provenance failed: {error}", file=sys.stderr)
        return 2
    print(json.dumps(document, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
