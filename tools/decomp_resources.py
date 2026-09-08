#!/usr/bin/env python3
"""Measure one PE resource leaf and its tracked source inputs."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from collections import Counter
from pathlib import Path

try:
    from tools import decomp_binary
except ModuleNotFoundError:  # Direct script imports use tools/ as sys.path[0].
    import decomp_binary  # type: ignore[no-redef]


ResourceId = tuple[int, int, int]


def parse_resource(value: str) -> ResourceId:
    parts = value.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("use TYPE,ID,LANGUAGE as decimal integers")
    try:
        resource = tuple(int(part, 10) for part in parts)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "use TYPE,ID,LANGUAGE as decimal integers"
        ) from error
    if any(part < 0 or part > 0xFFFFFFFF for part in resource):
        raise argparse.ArgumentTypeError("resource values must fit in 32 bits")
    return resource  # type: ignore[return-value]


def format_resource(resource: ResourceId) -> str:
    return ",".join(str(part) for part in resource)


def resource_rows(original_path: Path, recompiled_path: Path) -> list[dict[str, object]]:
    original = decomp_binary.read_resources(original_path)
    recompiled = decomp_binary.read_resources(recompiled_path)
    payloads = Counter(item.data for item in recompiled)
    identities = Counter((item.path, item.data) for item in recompiled)
    rows: list[dict[str, object]] = []
    for item in original:
        payload_match = payloads[item.data] > 0
        if payload_match:
            payloads[item.data] -= 1
        key = (item.path, item.data)
        identity_match = identities[key] > 0
        if identity_match:
            identities[key] -= 1
        rows.append(
            {
                "path": [str(part) for part in item.path],
                "size": len(item.data),
                "code_page": item.code_page,
                "match": payload_match,
                "identity_match": identity_match,
            }
        )
    return rows


def selected_evidence(
    rows: list[dict[str, object]], resource: ResourceId
) -> dict[str, int]:
    path = [str(part) for part in resource]
    selected = [row for row in rows if row.get("path") == path]
    return {
        "leaf_count": len(selected),
        "scored_bytes": sum(int(row.get("size", 0)) for row in selected),
        "explained_bytes": sum(
            int(row.get("size", 0))
            for row in selected
            if row.get("identity_match") is True
        ),
        "matched_leaves": sum(row.get("identity_match") is True for row in selected),
    }


def _file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resource_source_snapshot(root: Path) -> dict[str, str]:
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            "resources",
        ],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        raise ValueError("cannot list the resource source files")
    snapshot: dict[str, str] = {}
    for raw in result.stdout.split(b"\0"):
        if not raw:
            continue
        relative = raw.decode("utf-8", errors="surrogateescape")
        path = root / relative
        if path.is_file():
            snapshot[relative] = _file_hash(path)
    return snapshot


def staged_resource_source_problems(
    root: Path, baseline: dict[str, object]
) -> list[str]:
    current = resource_source_snapshot(root)
    changed = sorted(
        path
        for path in baseline.keys() | current.keys()
        if baseline.get(path) != current.get(path)
    )
    if not changed:
        return ["a resource campaign must change a tracked resource source file"]

    tracked = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--", "resources"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if tracked.returncode != 0:
        return ["cannot inspect the staged resource source files"]
    tracked_paths = {
        raw.decode("utf-8", errors="surrogateescape")
        for raw in tracked.stdout.split(b"\0")
        if raw
    }
    if not any(path in tracked_paths for path in changed):
        return ["a resource campaign must change a tracked resource source file"]

    problems = []
    for path in changed:
        worktree = root / path
        index = subprocess.run(
            ["git", "show", f":{path}"], cwd=root, check=False, capture_output=True
        )
        if worktree.exists() != (index.returncode == 0):
            problems.append(f"{path}: stage the resource source change used by the build")
            continue
        if (
            worktree.exists()
            and hashlib.sha256(index.stdout).hexdigest() != _file_hash(worktree)
        ):
            problems.append(f"{path}: stage the resource source change used by the build")
    return problems
