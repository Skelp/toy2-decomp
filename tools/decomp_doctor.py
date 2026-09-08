#!/usr/bin/env python3
"""Check reconstruction dependencies before a campaign starts.

The doctor does not start or update a campaign. It writes one receipt under
the ignored build cache so campaign tooling can account for preflight time.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Iterable, Mapping

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_campaigns import (  # noqa: E402
    LANE_MODES,
    _snapshot_hash,
    _validate_lane,
    comparison_artifact_hashes,
    file_hash,
    repository_index_snapshot,
    repository_worktree_snapshot,
    resource_source_snapshot,
    source_index_snapshot,
    source_worktree_snapshot,
)

CACHE_RELATIVE = Path("build/decomp-cache")
ACTIVE_STATE_RELATIVE = Path("build/decomp-campaign-state.json")
RECEIPT_RELATIVE = CACHE_RELATIVE / "doctor/latest.json"
RECEIPT_DIRECTORY_RELATIVE = CACHE_RELATIVE / "doctor/receipts"
GHIDRA_ARTIFACT_RELATIVE = CACHE_RELATIVE / "doctor/ghidra"
MAP_RELATIVE = Path("tools/Resources/functions_map.txt")
SOURCE_MODES = {"coverage", "refinement"}
DATA_MODES = {"data"}
MODES = tuple(sorted(SOURCE_MODES | DATA_MODES | {"resource", "meta"}))
EXPECTED_BRANCH = "agent/continuous"
LOG_LIMIT = 10 * 1024 * 1024
LOG_COUNT = 3
GHIDRA_ARTIFACT_LIMIT = 4 * 1024 * 1024
GHIDRA_ARTIFACT_TOTAL_LIMIT = 32 * 1024 * 1024
GHIDRA_ARTIFACT_KINDS = {
    "decompilation",
    "disassembly",
    "function",
    "xrefs",
}
SOURCE_ARTIFACT_KINDS = GHIDRA_ARTIFACT_KINDS
DATA_ARTIFACT_KINDS = {"xrefs"}
_UNSET = object()


@dataclass(frozen=True)
class Check:
    name: str
    ok: bool
    detail: str
    data: dict[str, object] | None = None


Runner = Callable[..., subprocess.CompletedProcess]


def _default_runner(command: list[str], **kwargs: object) -> subprocess.CompletedProcess:
    return subprocess.run(command, **kwargs)


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _timestamp(value: datetime) -> str:
    return value.astimezone(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _artifact_bytes(target: str, kind: str, payload: object) -> bytes:
    document = {
        "schema": 1,
        "target": target,
        "kind": kind,
        "payload": payload,
    }
    try:
        text = json.dumps(
            document,
            allow_nan=False,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        return (text + "\n").encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as error:
        raise ValueError(f"cannot encode the {kind} artifact: {error}") from error


def _artifact_relative_path(target: str, kind: str, digest: str) -> Path:
    if not re.fullmatch(r"0x[0-9A-F]{8}", target):
        raise ValueError("the Ghidra artifact target is not canonical")
    if kind not in GHIDRA_ARTIFACT_KINDS:
        raise ValueError("the Ghidra artifact kind is invalid")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("the Ghidra artifact hash is invalid")
    return GHIDRA_ARTIFACT_RELATIVE / target[2:].lower() / f"{kind}-{digest}.json"


def _store_ghidra_artifact(
    root: Path,
    target: str,
    kind: str,
    payload: object,
    *,
    remaining_bytes: int = GHIDRA_ARTIFACT_TOTAL_LIMIT,
) -> dict[str, object]:
    encoded = _artifact_bytes(target, kind, payload)
    limit = min(GHIDRA_ARTIFACT_LIMIT, max(0, remaining_bytes))
    if len(encoded) > limit:
        raise ValueError(f"the {kind} artifact exceeds its {limit}-byte limit")
    digest = hashlib.sha256(encoded).hexdigest()
    relative = _artifact_relative_path(target, kind, digest)
    base = (root / GHIDRA_ARTIFACT_RELATIVE).resolve()
    cache = (root / CACHE_RELATIVE).resolve()
    try:
        base.relative_to(cache)
    except ValueError as error:
        raise ValueError("the Ghidra artifact directory escaped the build cache") from error
    path = root / relative
    resolved = path.resolve()
    try:
        resolved.relative_to(base)
    except ValueError as error:
        raise ValueError("the Ghidra artifact path escaped the build cache") from error
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise ValueError("the Ghidra artifact path is a symbolic link")
    try:
        with path.open("xb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        if path.read_bytes() != encoded:
            raise ValueError("the content-addressed Ghidra artifact has different content")
    return {
        "path": relative.as_posix(),
        "sha256": digest,
        "bytes": len(encoded),
    }


def load_ghidra_artifact(
    root: Path,
    target: str,
    kind: str,
    descriptor: object,
) -> object:
    """Load one target-bound Ghidra artifact after all identity checks pass."""

    if not isinstance(descriptor, dict):
        raise ValueError(f"the {kind} artifact descriptor is invalid")
    path_value = descriptor.get("path")
    digest = descriptor.get("sha256")
    byte_count = descriptor.get("bytes")
    if (
        not isinstance(path_value, str)
        or not isinstance(digest, str)
        or not isinstance(byte_count, int)
        or isinstance(byte_count, bool)
        or not 0 < byte_count <= GHIDRA_ARTIFACT_LIMIT
    ):
        raise ValueError(f"the {kind} artifact descriptor is incomplete")
    expected_relative = _artifact_relative_path(target, kind, digest)
    relative = Path(path_value)
    if relative.is_absolute() or relative.as_posix() != expected_relative.as_posix():
        raise ValueError(f"the {kind} artifact path does not match its identity")
    base = (root / GHIDRA_ARTIFACT_RELATIVE).resolve()
    cache = (root / CACHE_RELATIVE).resolve()
    try:
        base.relative_to(cache)
    except ValueError as error:
        raise ValueError(f"the {kind} artifact directory escaped the build cache") from error
    path = root / relative
    resolved = path.resolve()
    try:
        resolved.relative_to(base)
    except ValueError as error:
        raise ValueError(f"the {kind} artifact path escaped the build cache") from error
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"the {kind} artifact is missing")
    try:
        encoded = path.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read the {kind} artifact: {error}") from error
    if len(encoded) != byte_count or len(encoded) > GHIDRA_ARTIFACT_LIMIT:
        raise ValueError(f"the {kind} artifact size does not match")
    if hashlib.sha256(encoded).hexdigest() != digest:
        raise ValueError(f"the {kind} artifact hash does not match")
    try:
        document = json.loads(encoded.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"the {kind} artifact is not valid JSON: {error}") from error
    if (
        not isinstance(document, dict)
        or document.get("schema") != 1
        or document.get("target") != target
        or document.get("kind") != kind
        or "payload" not in document
    ):
        raise ValueError(f"the {kind} artifact identity does not match")
    if _artifact_bytes(target, kind, document["payload"]) != encoded:
        raise ValueError(f"the {kind} artifact JSON is not canonical")
    return document["payload"]


def input_hashes(root: Path, _mode: str | None = None) -> dict[str, object]:
    map_path = root / MAP_RELATIVE
    sizes_path = root / "build/decomp-function-sizes.json"
    return {
        "source_worktree_sha256": _snapshot_hash(source_worktree_snapshot(root)),
        "source_index_sha256": _snapshot_hash(source_index_snapshot(root)),
        "repository_worktree_sha256": _snapshot_hash(repository_worktree_snapshot(root)),
        "repository_index_sha256": _snapshot_hash(repository_index_snapshot(root)),
        "resource_sources_sha256": _snapshot_hash(resource_source_snapshot(root)),
        "functions_map_sha256": file_hash(map_path) if map_path.is_file() else None,
        "function_sizes_file_sha256": file_hash(sizes_path) if sizes_path.is_file() else None,
        **comparison_artifact_hashes(root),
    }


def ghidra_environment(root: Path) -> tuple[dict[str, str], dict[str, str]]:
    base = root / CACHE_RELATIVE / "ghidra"
    paths = {
        "cache": str(base / "cache"),
        "state": str(base / "state"),
        "logs": str(base / "cache"),
    }
    environment = os.environ.copy()
    environment.update(
        {
            "XDG_CACHE_HOME": paths["cache"],
            "XDG_DATA_HOME": paths["state"],
            "GHIDRA_CLI_CACHE_DIR": paths["cache"],
            "GHIDRA_CLI_STATE_DIR": paths["state"],
            "GHIDRA_CLI_LOG_DIR": paths["logs"],
            "GHIDRA_CLI_FULL_RESPONSE_LOGGING": "0",
            "RUST_LOG": "warn",
        }
    )
    return environment, paths


def _bridge_process_state(pid_path: Path) -> str:
    try:
        process_id = int(pid_path.read_text(encoding="utf-8").strip())
        os.kill(process_id, 0)
    except PermissionError:
        pass
    except (OSError, ValueError):
        return "dead"
    command_path = Path("/proc") / str(process_id) / "cmdline"
    if not command_path.exists():
        return "unverified"
    try:
        command = command_path.read_bytes().replace(b"\0", b" ").lower()
    except OSError:
        return "unverified"
    return "verified" if b"ghidra" in command else "other"


def mirror_ghidra_bridge_markers(
    root: Path,
    *,
    source_data_home: Path | None = None,
) -> dict[str, object]:
    """Copy live bridge marker pairs into the isolated Ghidra state directory."""

    _, paths = ghidra_environment(root)
    source_home = source_data_home or Path(
        os.environ.get("TOY2_GHIDRA_LIVE_DATA_HOME", str(Path.home() / ".local/share"))
    )
    source = source_home / "ghidra-cli"
    target = Path(paths["state"]) / "ghidra-cli"
    target.mkdir(parents=True, exist_ok=True)

    retained: set[str] = set()
    unverified: set[str] = set()
    for pid_path in target.glob("bridge-*.pid"):
        port_path = pid_path.with_suffix(".port")
        state = _bridge_process_state(pid_path)
        if state in {"verified", "unverified"} and port_path.is_file() and port_path.stat().st_size:
            (retained if state == "verified" else unverified).add(pid_path.stem)
            continue
        pid_path.unlink(missing_ok=True)
        port_path.unlink(missing_ok=True)

    copied: list[str] = []
    if source.resolve() != target.resolve() and source.is_dir():
        for pid_path in source.glob("bridge-*.pid"):
            port_path = pid_path.with_suffix(".port")
            state = _bridge_process_state(pid_path)
            if state not in {"verified", "unverified"} or not port_path.is_file() or not port_path.stat().st_size:
                continue
            shutil.copyfile(pid_path, target / pid_path.name)
            shutil.copyfile(port_path, target / port_path.name)
            if state == "verified":
                copied.append(pid_path.stem)
            else:
                unverified.add(pid_path.stem)
    active = sorted(retained | set(copied))
    return {
        "source": str(source),
        "target": str(target),
        "active": active,
        "unverified": sorted(unverified),
        "copied": sorted(copied),
    }


def prune_ghidra_logs(root: Path) -> dict[str, int]:
    log_root = root / CACHE_RELATIVE / "ghidra"
    if not log_root.exists():
        return {"files": 0, "truncated": 0, "removed": 0}
    logs = sorted(
        (
            path
            for path in log_root.rglob("*")
            if path.is_file() and not path.is_symlink() and ".log" in path.name
        ),
        key=lambda path: path.stat().st_mtime_ns,
        reverse=True,
    )
    truncated = 0
    for path in logs[:LOG_COUNT]:
        if path.stat().st_size <= LOG_LIMIT:
            continue
        with path.open("rb") as stream:
            stream.seek(-LOG_LIMIT, os.SEEK_END)
            tail = stream.read()
        with path.open("wb") as stream:
            stream.write(tail)
        truncated += 1
    removed = 0
    for path in logs[LOG_COUNT:]:
        path.unlink()
        removed += 1
    return {"files": min(len(logs), LOG_COUNT), "truncated": truncated, "removed": removed}


def _run(
    runner: Runner,
    command: list[str],
    *,
    root: Path,
    environment: dict[str, str] | None = None,
    timeout: float = 15.0,
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


def _output(result: subprocess.CompletedProcess) -> str:
    stdout = result.stdout.decode(errors="replace") if isinstance(result.stdout, bytes) else result.stdout
    stderr = result.stderr.decode(errors="replace") if isinstance(result.stderr, bytes) else result.stderr
    return ((stdout or "") + "\n" + (stderr or "")).strip()


def _command_check(
    name: str,
    runner: Runner,
    command: list[str],
    *,
    root: Path,
    environment: dict[str, str] | None = None,
    timeout: float = 15.0,
) -> Check:
    try:
        result = _run(runner, command, root=root, environment=environment, timeout=timeout)
    except (OSError, subprocess.SubprocessError) as error:
        return Check(name, False, str(error))
    output = _output(result)
    detail = output.splitlines()[0] if output else f"exit status {result.returncode}"
    return Check(name, result.returncode == 0, detail[:500])


def _branch_check(root: Path, runner: Runner, expected: str) -> Check:
    result = _run(runner, ["git", "branch", "--show-current"], root=root)
    branch = _output(result).splitlines()[0] if _output(result) else ""
    return Check(
        "branch",
        result.returncode == 0 and branch == expected,
        f"branch is {branch or '<detached>'}; expected {expected}",
        {"actual": branch, "expected": expected},
    )


def _head_check(root: Path, runner: Runner) -> Check:
    result = _run(runner, ["git", "rev-parse", "HEAD"], root=root)
    head = _output(result).splitlines()[0].strip().lower() if _output(result) else ""
    ok = result.returncode == 0 and bool(re.fullmatch(r"[0-9a-f]{40,64}", head))
    return Check(
        "head",
        ok,
        f"HEAD is {head}" if ok else "Git did not return a full HEAD hash",
        {"actual": head},
    )


def _origin_integration_check(
    root: Path, runner: Runner, expected_branch: str
) -> Check:
    remote_ref = f"refs/remotes/origin/{expected_branch}"
    refspec = f"+refs/heads/{expected_branch}:{remote_ref}"
    data: dict[str, object] = {
        "remote_ref": remote_ref,
        "remote": "",
        "head": "",
        "fetched": False,
    }
    fetch = _run(
        runner,
        ["git", "fetch", "--quiet", "--no-tags", "origin", refspec],
        root=root,
        timeout=60.0,
    )
    if fetch.returncode != 0:
        detail = _output(fetch).splitlines()
        return Check(
            "origin-integration",
            False,
            detail[0][:500] if detail else "Git could not fetch the integration branch",
            data,
        )
    data["fetched"] = True
    remote_result = _run(
        runner,
        ["git", "rev-parse", "--verify", f"{remote_ref}^{{commit}}"],
        root=root,
    )
    head_result = _run(runner, ["git", "rev-parse", "HEAD"], root=root)
    remote = _output(remote_result).splitlines()
    head = _output(head_result).splitlines()
    remote_hash = remote[0].strip().lower() if remote else ""
    head_hash = head[0].strip().lower() if head else ""
    data["remote"] = remote_hash
    data["head"] = head_hash
    hashes_ok = (
        remote_result.returncode == 0
        and head_result.returncode == 0
        and re.fullmatch(r"[0-9a-f]{40,64}", remote_hash) is not None
        and re.fullmatch(r"[0-9a-f]{40,64}", head_hash) is not None
    )
    if not hashes_ok:
        return Check(
            "origin-integration",
            False,
            "Git could not resolve the integration branch and HEAD",
            data,
        )
    ancestor = _run(
        runner,
        ["git", "merge-base", "--is-ancestor", remote_hash, head_hash],
        root=root,
    )
    ok = ancestor.returncode == 0
    return Check(
        "origin-integration",
        ok,
        (
            "HEAD contains the fetched integration branch"
            if ok
            else "HEAD does not contain the fetched integration branch"
        ),
        data,
    )


def _allowed_dirty_path(path: str) -> bool:
    return path == "external/submodules/reccmp" or path == ".codex" or path.startswith(".codex/")


def _campaign_product_path(path: str) -> bool:
    relative = Path(path)
    return (
        relative.parts[:1] in (("src",), ("resources",))
        or (relative.name == "CMakeLists.txt" and "tools" not in relative.parts)
    )


def _active_campaign_identity(
    root: Path, mode: str | None, head: str | None
) -> tuple[dict[str, object] | None, str | None]:
    path = root / ACTIVE_STATE_RELATIVE
    if not path.is_file():
        return None, None
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return None, f"cannot read the active campaign: {error}"
    if not isinstance(state, dict):
        return None, "the active campaign state is invalid"
    if state.get("mode") != mode or mode not in SOURCE_MODES | DATA_MODES:
        return None, None
    if state.get("phase") == "finalized" or state.get("finalize_receipt") is not None:
        return None, "the active campaign is already finalized"
    if not state.get("baseline_at"):
        return None, "the active campaign has no baseline"
    if state.get("campaign_head") != head:
        return None, "the active campaign HEAD does not match the doctor HEAD"
    if state.get("source_worktree_root") != str(root.resolve()):
        return None, "the active campaign belongs to another worktree"
    campaign_id = state.get("campaign_id")
    if not isinstance(campaign_id, str) or not campaign_id:
        return None, "the active campaign ID is invalid"
    return {
        "campaign_id": campaign_id,
        "campaign_head": head,
        "mode": mode,
        "path": str(path.resolve()),
        "sha256": _sha256(path),
    }, None


def _dirt_check(
    root: Path,
    runner: Runner,
    *,
    mode: str | None = None,
    head: str | None = None,
) -> Check:
    result = _run(
        runner,
        ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"],
        root=root,
    )
    stdout = result.stdout.decode(errors="replace") if isinstance(result.stdout, bytes) else (result.stdout or "")
    entries = [entry for entry in stdout.split("\0") if entry]
    active_campaign, active_error = _active_campaign_identity(root, mode, head)
    rejected: list[str] = []
    campaign_entries: list[str] = []
    for entry in entries:
        path = entry[3:] if len(entry) >= 4 else entry
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        if _allowed_dirty_path(path):
            continue
        if active_campaign is not None and _campaign_product_path(path):
            campaign_entries.append(entry)
        else:
            rejected.append(entry)
    ok = result.returncode == 0 and active_error is None and not rejected
    detail = "only allowed worktree changes are present" if ok else f"unexpected worktree changes: {', '.join(rejected[:8])}"
    if active_error is not None:
        detail = active_error
    return Check(
        "allowed-dirt",
        ok,
        detail,
        {
            "rejected": rejected,
            "entry_count": len(entries),
            "campaign_entries": campaign_entries,
            "active_campaign": active_campaign,
        },
    )


def _writable_check(root: Path) -> Check:
    build = root / "build"
    cache = root / CACHE_RELATIVE
    tooling = root / ".tooling"
    try:
        build.mkdir(parents=True, exist_ok=True)
        cache.mkdir(parents=True, exist_ok=True)
        (cache / "doctor").mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix="write-probe-", dir=cache, delete=True):
            pass
    except OSError as error:
        return Check("writable-state", False, str(error))
    required = [tooling]
    if not sys.platform.startswith("win"):
        required.append(tooling / "wineprefix")
    missing = [str(path) for path in required if not path.exists()]
    unwritable = [str(path) for path in (build, cache, tooling) if path.exists() and not os.access(path, os.W_OK)]
    ok = not missing and not unwritable
    detail = "build, cache, and tool state are writable" if ok else f"missing={missing}; not writable={unwritable}"
    return Check("writable-state", ok, detail, {"cache": str(cache), "missing": missing, "unwritable": unwritable})


def _retail_expected_hash(project: Path) -> str | None:
    if not project.is_file():
        return None
    match = re.search(r"(?im)^\s*sha256:\s*['\"]?([0-9a-f]{64})", project.read_text(errors="ignore"))
    return match.group(1).lower() if match else None


def _reccmp_inputs_check(root: Path) -> Check:
    paths = [
        root / "original/toy2.exe",
        root / "build/toy2.exe",
        root / "build/toy2.pdb",
        root / "reccmp-project.yml",
        root / "build/reccmp-build.yml",
    ]
    bad = [str(path.relative_to(root)) for path in paths if not path.is_file() or path.stat().st_size == 0]
    expected = _retail_expected_hash(root / "reccmp-project.yml")
    actual = _sha256(root / "original/toy2.exe")
    if expected and actual != expected:
        bad.append("original/toy2.exe has the wrong SHA-256")
    ok = not bad
    return Check(
        "reccmp-inputs",
        ok,
        "reccmp inputs are present and valid" if ok else "; ".join(bad),
        {"retail_sha256": actual, "expected_sha256": expected},
    )


def _find_executable(root: Path, name: str, local: str | None = None) -> str | None:
    found = shutil.which(name)
    if found:
        return found
    if local:
        candidate = root / local
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def _wine_checks(root: Path, runner: Runner) -> list[Check]:
    if sys.platform.startswith("win"):
        runtime = _find_executable(root, "cmd.exe") or _find_executable(root, "cmd")
        return [
            Check("wine-runtime", True, "Wine is not applicable on native Windows"),
            Check(
                "native-build-runtime",
                bool(runtime),
                f"native command runtime is {runtime}" if runtime else "native command runtime is unavailable",
            ),
        ]
    wine = _find_executable(root, "wine")
    if not wine:
        return [Check("wine-runtime", False, "wine is not on PATH")]
    environment = os.environ.copy()
    environment.update({"WINEPREFIX": str(root / ".tooling/wineprefix"), "WINEDEBUG": "-all"})
    version = _command_check("wine-runtime", runner, [wine, "--version"], root=root, environment=environment)
    if not version.ok:
        return [version]
    winepath = _find_executable(root, "winepath")
    if not winepath:
        return [version, Check("wine-prefix", False, "winepath is not on PATH")]
    path_check = _command_check(
        "wine-prefix",
        runner,
        [winepath, "-w", str(root / "build/toy2.pdb")],
        root=root,
        environment=environment,
    )
    return [version, path_check]


def _reccmp_runtime_check(root: Path, runner: Runner) -> Check:
    executable = _find_executable(root, "reccmp-reccmp", ".tooling/venv/bin/reccmp-reccmp")
    if not executable and sys.platform.startswith("win"):
        executable = _find_executable(
            root, "reccmp-reccmp.exe", ".tooling/venv/Scripts/reccmp-reccmp.exe"
        )
    if not executable:
        return Check("reccmp-runtime", False, "reccmp-reccmp is not available")
    return _command_check("reccmp-runtime", runner, [executable, "--version"], root=root)


def _map_check(root: Path) -> Check:
    path = root / MAP_RELATIVE
    if not path.is_file():
        return Check("function-map", False, f"missing {MAP_RELATIVE}")
    addresses: list[int] = []
    invalid: list[int] = []
    for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = line.split(maxsplit=1)
        try:
            address = int(fields[0], 0)
        except (ValueError, IndexError):
            invalid.append(line_number)
            continue
        if len(fields) < 2 or not fields[1].strip():
            invalid.append(line_number)
        addresses.append(address)
    ordered = addresses == sorted(addresses)
    unique = len(addresses) == len(set(addresses))
    ok = bool(addresses) and not invalid and ordered and unique
    detail = f"{len(addresses)} unique ordered function starts" if ok else f"invalid lines={invalid[:8]}, ordered={ordered}, unique={unique}"
    return Check("function-map", ok, detail, {"function_count": len(addresses)})


def _read_json(path: Path) -> object:
    with path.open(encoding="utf-8-sig") as stream:
        return json.load(stream)


def _report_address(value: object) -> int | None:
    try:
        return int(str(value), 0)
    except (TypeError, ValueError):
        try:
            return int(str(value), 16)
        except (TypeError, ValueError):
            return None


def _code_report_addresses(value: object) -> set[int]:
    rows = value.get("data", []) if isinstance(value, dict) else []
    return {
        address
        for row in rows if isinstance(rows, list) and isinstance(row, dict)
        if (address := _report_address(row.get("address"))) is not None
    }


def _data_report_addresses(value: object) -> set[int]:
    variables = value.get("variables", {}) if isinstance(value, dict) else {}
    rows = variables.get("variables", []) if isinstance(variables, dict) else []
    return {
        address
        for row in rows if isinstance(rows, list) and isinstance(row, dict)
        if (address := _report_address(row.get("original_address"))) is not None
    }


def _validate_report_provenance(path: Path, root: Path) -> None:
    from tools.decomp_provenance import validate_report

    validate_report(path, root=root)


def _reports_check(root: Path, mode: str, targets: int | Iterable[int] | None) -> Check:
    addresses = [] if targets is None else [targets] if isinstance(targets, int) else list(targets)
    paths = [root / "build/decomp-current-report.json", root / "build/decomp-function-sizes.json"]
    if mode in DATA_MODES:
        paths.append(root / "build/decomp-current-data-report.json")
    documents: dict[str, object] = {}
    errors: list[str] = []
    def newest(*dependencies: Path) -> int:
        return max(
            (path.stat().st_mtime_ns for path in dependencies if path.is_file()),
            default=0,
        )

    build_executable = root / "build/toy2.exe"
    retail_executable = root / "original/toy2.exe"
    function_map = root / MAP_RELATIVE
    dependency_times = {
        root / "build/decomp-current-report.json": newest(
            build_executable, function_map
        ),
        root / "build/decomp-function-sizes.json": newest(
            retail_executable, function_map
        ),
        root / "build/decomp-current-data-report.json": newest(
            build_executable, function_map
        ),
    }
    expected_hashes = {
        "map": _sha256(root / MAP_RELATIVE),
        "original": _sha256(root / "original/toy2.exe"),
        "recompiled": _sha256(root / "build/toy2.exe"),
        "rebuilt": _sha256(root / "build/toy2.exe"),
        "executable": _sha256(root / "build/toy2.exe"),
    }
    for path in paths:
        try:
            document = _read_json(path)
            if not document:
                raise ValueError("empty JSON document")
            documents[path.name] = document
            if path.name in {
                "decomp-current-report.json",
                "decomp-current-data-report.json",
            }:
                _validate_report_provenance(path, root)
            if path.stat().st_mtime_ns < dependency_times.get(path, 0):
                errors.append(f"{path.relative_to(root)} is older than its executable or map input")
            for item in (value for value in _walk_json(document) if isinstance(value, dict)):
                for key, value in item.items():
                    clean_key = key.lower().replace("-", "_")
                    if "sha256" not in clean_key or not isinstance(value, str):
                        continue
                    source = next((name for name in expected_hashes if name in clean_key), None)
                    if source and expected_hashes[source] and value.lower() != expected_hashes[source]:
                        errors.append(f"{path.relative_to(root)} has a stale {key}")
        except (OSError, ValueError, json.JSONDecodeError) as error:
            errors.append(f"{path.relative_to(root)}: {error}")
    if mode in SOURCE_MODES:
        code_document = documents.get("decomp-current-report.json")
        report_addresses = _code_report_addresses(code_document)
        for target in addresses:
            if target not in report_addresses:
                errors.append(f"target 0x{target:08X} is absent from the code report")
    if mode in DATA_MODES and addresses:
        data_document = documents.get("decomp-current-data-report.json")
        data_addresses = _data_report_addresses(data_document)
        for target in addresses:
            if target not in data_addresses:
                errors.append(f"data target 0x{target:08X} is absent from the typed-data report")
    return Check(
        "reports",
        not errors,
        f"{len(paths)} reports are valid" if not errors else "; ".join(errors),
        {"paths": [str(path.relative_to(root)) for path in paths]},
    )


def _walk_json(value: object) -> Iterable[object]:
    yield value
    if isinstance(value, dict):
        for child in value.values():
            yield from _walk_json(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk_json(child)


def _json_payload(result: subprocess.CompletedProcess) -> object | None:
    output = result.stdout.decode(errors="replace") if isinstance(result.stdout, bytes) else result.stdout
    try:
        return json.loads(output or "")
    except json.JSONDecodeError:
        return None


def _find_lists(value: object, keys: set[str]) -> Iterable[list[object]]:
    if isinstance(value, dict):
        for key, child in value.items():
            if key.lower() in keys and isinstance(child, list):
                yield child
            yield from _find_lists(child, keys)
    elif isinstance(value, list):
        for child in value:
            yield from _find_lists(child, keys)


def _find_strings(value: object, keys: set[str]) -> Iterable[str]:
    if isinstance(value, dict):
        for key, child in value.items():
            if key.lower() in keys and isinstance(child, str):
                yield child
            yield from _find_strings(child, keys)
    elif isinstance(value, list):
        for child in value:
            yield from _find_strings(child, keys)


def _ghidra_checks(
    root: Path,
    runner: Runner,
    mode: str,
    targets: int | Iterable[int] | None,
) -> list[Check]:
    addresses = [] if targets is None else [targets] if isinstance(targets, int) else list(targets)
    executable = _find_executable(root, "ghidra")
    if not executable:
        checks = [
            Check("ghidra-runtime", False, "ghidra is not on PATH"),
            Check("ghidra-config", False, "Ghidra is unavailable"),
            Check("ghidra-bridge-markers", False, "Ghidra is unavailable"),
            Check("ghidra-state", False, "Ghidra is unavailable"),
            Check(
                "ghidra-log-cap",
                True,
                "Ghidra logs are capped",
                prune_ghidra_logs(root),
            ),
        ]
        artifact_kinds = (
            SOURCE_ARTIFACT_KINDS
            if mode in SOURCE_MODES
            else DATA_ARTIFACT_KINDS if mode in DATA_MODES else set()
        )
        for _target in addresses or [None]:
            checks.extend(
                Check(kind, False, "Ghidra is unavailable")
                for kind in sorted(artifact_kinds)
            )
        return checks

    environment, paths = ghidra_environment(root)
    for path in paths.values():
        Path(path).mkdir(parents=True, exist_ok=True)
    marker_stats = mirror_ghidra_bridge_markers(root)
    log_stats = prune_ghidra_logs(root)
    runtime = _command_check(
        "ghidra-runtime", runner, [executable, "--version"], root=root, environment=environment
    )
    config = _command_check(
        "ghidra-config", runner, [executable, "config", "list", "--json"], root=root, environment=environment
    )
    config = Check(config.name, config.ok, config.detail, {"paths": paths, "logging": "warning-only"})
    state = _command_check(
        "ghidra-state", runner, [executable, "--json", "status"], root=root, environment=environment
    )
    state_text = state.detail.lower()
    if state.ok and ("no bridge" in state_text or "not running" in state_text):
        state = Check("ghidra-state", False, state.detail)
    marker_ready = bool(marker_stats.get("active")) or (
        bool(marker_stats.get("unverified")) and state.ok
    )
    marker_check = Check(
        "ghidra-bridge-markers",
        marker_ready,
        (
            f"{len(marker_stats['active'])} live bridge marker pair is available"
            if marker_stats.get("active")
            else "Ghidra status verified an unverified portable bridge marker"
            if marker_stats.get("unverified") and state.ok
            else "no live Ghidra bridge marker pair is available"
        ),
        marker_stats,
    )
    checks = [
        runtime,
        config,
        marker_check,
        state,
        Check("ghidra-log-cap", True, "Ghidra logs are capped", log_stats),
    ]
    if mode in DATA_MODES:
        artifact_bytes = 0
        for target in addresses:
            address = f"0x{target:08X}"
            try:
                result = _run(
                    runner,
                    [executable, "--json", "x-ref", "to", address],
                    root=root,
                    environment=environment,
                    timeout=30.0,
                )
                payload = _json_payload(result)
                ok = result.returncode == 0 and payload is not None
                artifact = _store_ghidra_artifact(
                    root,
                    address,
                    "xrefs",
                    payload,
                    remaining_bytes=GHIDRA_ARTIFACT_TOTAL_LIMIT - artifact_bytes,
                )
                artifact_bytes += int(artifact["bytes"])
                checks.append(
                    Check(
                        "xrefs",
                        ok,
                        "cross-references captured" if ok else "Ghidra returned invalid cross-references",
                        {"target": address, "artifact": artifact},
                    )
                )
            except (OSError, subprocess.SubprocessError, TypeError, ValueError) as error:
                checks.append(Check("xrefs", False, str(error), {"target": address}))
        prune_ghidra_logs(root)
        return checks
    if mode not in SOURCE_MODES:
        prune_ghidra_logs(root)
        return checks
    if not addresses:
        checks.extend(
            [
                Check("disassembly", False, "a source mode needs --target"),
                Check("decompilation", False, "a source mode needs --target"),
            ]
        )
        return checks

    artifact_bytes = 0
    for target in addresses:
        address = f"0x{target:08X}"
        try:
            disassembly = _run(
                runner,
                [executable, "--json", "function", "disasm", address],
                root=root,
                environment=environment,
                timeout=30.0,
            )
            disassembly_payload = _json_payload(disassembly)
            instruction_lists = (
                [disassembly_payload] if isinstance(disassembly_payload, list) else []
            ) + list(_find_lists(disassembly_payload, {"instructions", "disassembly"}))
            instruction_count = max((len(items) for items in instruction_lists), default=0)
            disassembly_ok = disassembly.returncode == 0 and instruction_count > 0
            artifact = _store_ghidra_artifact(
                root,
                address,
                "disassembly",
                disassembly_payload,
                remaining_bytes=GHIDRA_ARTIFACT_TOTAL_LIMIT - artifact_bytes,
            )
            artifact_bytes += int(artifact["bytes"])
            checks.append(
                Check(
                    "disassembly",
                    disassembly_ok,
                    f"{instruction_count} instructions" if disassembly_ok else "Ghidra returned no instructions",
                    {
                        "target": address,
                        "instruction_count": instruction_count,
                        "artifact": artifact,
                    },
                )
            )
        except (OSError, subprocess.SubprocessError, TypeError, ValueError) as error:
            checks.append(Check("disassembly", False, str(error), {"target": address}))

        try:
            decompilation = _run(
                runner,
                [executable, "--json", "decompile", address, "--with-params", "--with-vars"],
                root=root,
                environment=environment,
                timeout=30.0,
            )
            decompilation_payload = _json_payload(decompilation)
            code = next(
                (text for text in _find_strings(decompilation_payload, {"code", "decompilation", "c"}) if text.strip()),
                "",
            )
            decompilation_ok = decompilation.returncode == 0 and bool(code.strip())
            artifact = _store_ghidra_artifact(
                root,
                address,
                "decompilation",
                decompilation_payload,
                remaining_bytes=GHIDRA_ARTIFACT_TOTAL_LIMIT - artifact_bytes,
            )
            artifact_bytes += int(artifact["bytes"])
            checks.append(
                Check(
                    "decompilation",
                    decompilation_ok,
                    f"{len(code)} source characters" if decompilation_ok else "Ghidra returned no decompilation",
                    {
                        "target": address,
                        "source_characters": len(code),
                        "artifact": artifact,
                    },
                )
            )
        except (OSError, subprocess.SubprocessError, TypeError, ValueError) as error:
            checks.append(Check("decompilation", False, str(error), {"target": address}))

        for kind, command in (
            ("function", [executable, "--json", "function", "get", address]),
            ("xrefs", [executable, "--json", "x-ref", "to", address]),
        ):
            try:
                result = _run(
                    runner,
                    command,
                    root=root,
                    environment=environment,
                    timeout=30.0,
                )
                payload = _json_payload(result)
                ok = result.returncode == 0 and payload is not None
                artifact = _store_ghidra_artifact(
                    root,
                    address,
                    kind,
                    payload,
                    remaining_bytes=GHIDRA_ARTIFACT_TOTAL_LIMIT - artifact_bytes,
                )
                artifact_bytes += int(artifact["bytes"])
                checks.append(
                    Check(
                        kind,
                        ok,
                        f"{kind} evidence captured" if ok else f"Ghidra returned invalid {kind} evidence",
                        {"target": address, "artifact": artifact},
                    )
                )
            except (OSError, subprocess.SubprocessError, TypeError, ValueError) as error:
                checks.append(Check(kind, False, str(error), {"target": address}))
    prune_ghidra_logs(root)
    return checks


def _write_receipt(root: Path, receipt: dict[str, object]) -> Path:
    receipt_id = receipt.get("receipt_id")
    if not isinstance(receipt_id, str) or not re.fullmatch(r"[0-9a-f]{64}", receipt_id):
        raise ValueError("the doctor receipt identity is invalid")
    immutable = root / RECEIPT_DIRECTORY_RELATIVE / f"{receipt_id}.json"
    latest = root / RECEIPT_RELATIVE
    cache = (root / CACHE_RELATIVE).resolve()
    try:
        immutable.resolve().relative_to(cache)
        latest.resolve().relative_to(cache)
    except ValueError as error:
        raise ValueError("the doctor receipt path escaped the build cache") from error
    immutable.parent.mkdir(parents=True, exist_ok=True)
    latest.parent.mkdir(parents=True, exist_ok=True)
    if immutable.is_symlink():
        raise ValueError("the immutable doctor receipt path is a symbolic link")
    encoded = (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")
    try:
        with immutable.open("xb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError:
        if immutable.is_symlink() or immutable.read_bytes() != encoded:
            raise ValueError("the immutable doctor receipt has different content")
    descriptor, temporary = tempfile.mkstemp(prefix="latest-", suffix=".json", dir=latest.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, latest)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return immutable


def _receipt_id(receipt: Mapping[str, object]) -> str:
    identity = dict(receipt)
    identity.pop("receipt_id", None)
    identity.pop("receipt_path", None)
    encoded = json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def _required_check_counts(mode: str, address_count: int) -> Counter[str]:
    names = Counter(
        {
            "selection": 1,
            "branch": 1,
            "head": 1,
            "origin-integration": 1,
            "allowed-dirt": 1,
            "writable-state": 1,
            "wine-runtime": 1,
            "reccmp-inputs": 1,
            "reccmp-runtime": 1,
            "ghidra-runtime": 1,
            "ghidra-config": 1,
            "ghidra-bridge-markers": 1,
            "ghidra-state": 1,
            "ghidra-log-cap": 1,
            "function-map": 1,
            "reports": 1,
            "campaign-input-hashes": 1,
        }
    )
    names["native-build-runtime" if sys.platform.startswith("win") else "wine-prefix"] = 1
    if mode in SOURCE_MODES:
        for kind in SOURCE_ARTIFACT_KINDS:
            names[kind] = address_count
    if mode in DATA_MODES:
        for kind in DATA_ARTIFACT_KINDS:
            names[kind] = address_count
    return names


def _normalize_addresses(values: Iterable[str | int]) -> list[str]:
    addresses: list[str] = []
    for value in values:
        try:
            address = int(value, 0) if isinstance(value, str) else int(value)
        except (TypeError, ValueError) as error:
            raise ValueError("the doctor receipt has an invalid address") from error
        if not 0 <= address <= 0xFFFFFFFF:
            raise ValueError("the doctor receipt has an invalid address")
        addresses.append(f"0x{address:08X}")
    return addresses


def _artifact_descriptors(
    receipt: Mapping[str, object],
    target: str | int,
    kinds: Iterable[str],
) -> dict[str, dict[str, object]]:
    """Get the selected Ghidra artifact descriptors for one receipt target."""

    normalized = _normalize_addresses([target])[0]
    checks = receipt.get("checks")
    if not isinstance(checks, list):
        raise ValueError("the doctor receipt checks are invalid")
    descriptors: dict[str, dict[str, object]] = {}
    for kind in sorted(kinds):
        matches = [
            check
            for check in checks
            if isinstance(check, dict)
            and check.get("name") == kind
            and check.get("ok") is True
            and isinstance(check.get("data"), dict)
            and check["data"].get("target") == normalized
        ]
        if len(matches) != 1:
            raise ValueError(f"the doctor receipt has no unique {kind} artifact for {normalized}")
        descriptor = matches[0]["data"].get("artifact")
        if not isinstance(descriptor, dict):
            raise ValueError(f"the doctor receipt has no {kind} artifact for {normalized}")
        descriptors[kind] = dict(descriptor)
    return descriptors


def source_artifact_descriptors(
    receipt: Mapping[str, object],
    target: str | int,
) -> dict[str, dict[str, object]]:
    """Get all source evidence artifact descriptors for one receipt target."""

    return _artifact_descriptors(receipt, target, SOURCE_ARTIFACT_KINDS)


def data_artifact_descriptors(
    receipt: Mapping[str, object],
    target: str | int,
) -> dict[str, dict[str, object]]:
    """Get all data evidence artifact descriptors for one receipt target."""

    return _artifact_descriptors(receipt, target, DATA_ARTIFACT_KINDS)


def _read_receipt_document(receipt_or_path: Mapping[str, object] | Path) -> dict[str, object]:
    if isinstance(receipt_or_path, Path):
        try:
            value = json.loads(receipt_or_path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read the doctor receipt: {error}") from error
    else:
        value = dict(receipt_or_path)
    if not isinstance(value, dict):
        raise ValueError("the doctor receipt must be a JSON object")
    return value


def validate_doctor_receipt(
    receipt_or_path: Mapping[str, object] | Path,
    *,
    root: Path = ROOT,
    runner: Runner = _default_runner,
    expected_branch: str = EXPECTED_BRANCH,
    expected_mode: str | None = None,
    expected_lane: str | None = None,
    expected_addresses: Iterable[str | int] | None = None,
    expected_resource: str | None | object = _UNSET,
    expected_input_hashes: Mapping[str, object] | None = None,
) -> dict[str, object]:
    """Validate a ready doctor receipt against the current repository."""

    receipt = _read_receipt_document(receipt_or_path)
    if (
        receipt.get("schema") != 1
        or receipt.get("status") != "ready"
        or receipt.get("ok") is not True
        or receipt.get("campaign_timing_started") is not False
    ):
        raise ValueError("the doctor receipt is not ready")
    if receipt.get("receipt_id") != _receipt_id(receipt):
        raise ValueError("the doctor receipt identity is invalid")

    resolved_root = root.resolve()
    if receipt.get("root") != str(resolved_root):
        raise ValueError("the doctor receipt root is stale")

    mode = receipt.get("mode")
    if not isinstance(mode, str) or mode not in MODES:
        raise ValueError("the doctor receipt mode is invalid")
    if expected_mode is not None and mode != expected_mode:
        raise ValueError("the doctor receipt mode does not match")
    lane = receipt.get("lane")
    if not isinstance(lane, str) or not lane or "\n" in lane or "\r" in lane:
        raise ValueError("the doctor receipt lane is invalid")
    _validate_lane(mode, lane)
    if expected_lane is not None and lane != expected_lane:
        raise ValueError("the doctor receipt lane does not match")

    raw_addresses = receipt.get("addresses")
    if not isinstance(raw_addresses, list):
        raise ValueError("the doctor receipt addresses are invalid")
    addresses = _normalize_addresses(raw_addresses)
    if raw_addresses != addresses or len(addresses) != len(set(addresses)):
        raise ValueError("the doctor receipt addresses are not canonical")
    resource = receipt.get("resource")
    selection_ok = (
        mode == "resource" and not addresses and isinstance(resource, str) and bool(resource)
    ) or (
        mode in SOURCE_MODES | DATA_MODES and bool(addresses) and resource is None
    ) or (mode == "meta" and not addresses and resource is None)
    if not selection_ok:
        raise ValueError("the doctor receipt selection is invalid")
    if expected_addresses is not None and addresses != _normalize_addresses(expected_addresses):
        raise ValueError("the doctor receipt addresses do not match")
    if expected_resource is not _UNSET and resource != expected_resource:
        raise ValueError("the doctor receipt resource does not match")

    current_inputs = (
        dict(expected_input_hashes)
        if expected_input_hashes is not None
        else input_hashes(resolved_root, mode)
    )
    if receipt.get("input_hashes") != current_inputs:
        raise ValueError("the doctor receipt input hashes are stale")

    branch = receipt.get("branch")
    if not isinstance(branch, dict):
        raise ValueError("the doctor receipt branch identity is invalid")
    if branch.get("expected") != expected_branch:
        raise ValueError("the doctor receipt expected branch is invalid")
    branch_check = _branch_check(resolved_root, runner, expected_branch)
    if not branch_check.ok or branch.get("actual") != branch_check.data.get("actual"):
        raise ValueError("the doctor receipt branch is stale")

    head = receipt.get("head")
    head_check = _head_check(resolved_root, runner)
    if not head_check.ok or head != head_check.data.get("actual"):
        raise ValueError("the doctor receipt HEAD is stale")

    origin = receipt.get("origin_integration")
    if not isinstance(origin, dict):
        raise ValueError("the doctor receipt origin identity is invalid")
    origin_check = _origin_integration_check(
        resolved_root, runner, expected_branch
    )
    if not origin_check.ok or origin_check.data != origin:
        raise ValueError("the doctor receipt origin integration is stale")

    checks = receipt.get("checks")
    if not isinstance(checks, list) or not checks:
        raise ValueError("the doctor receipt checks are invalid")
    if any(
        not isinstance(check, dict)
        or not isinstance(check.get("name"), str)
        or check.get("ok") is not True
        for check in checks
    ):
        raise ValueError("the doctor receipt has a failed check")
    actual_counts = Counter(str(check["name"]) for check in checks)
    if actual_counts != _required_check_counts(mode, len(addresses)):
        raise ValueError("the doctor receipt check set is incomplete")
    by_name = {str(check["name"]): check for check in checks}
    if by_name["branch"].get("data") != branch:
        raise ValueError("the doctor receipt branch check is inconsistent")
    if by_name["head"].get("data") != {"actual": head}:
        raise ValueError("the doctor receipt HEAD check is inconsistent")
    if by_name["origin-integration"].get("data") != origin:
        raise ValueError("the doctor receipt origin check is inconsistent")
    if by_name["selection"].get("data") != {"addresses": addresses, "resource": resource}:
        raise ValueError("the doctor receipt selection check is inconsistent")
    dirt_check = _dirt_check(
        resolved_root, runner, mode=mode, head=str(head)
    )
    if (
        not dirt_check.ok
        or by_name["allowed-dirt"].get("data") != dirt_check.data
    ):
        raise ValueError("the doctor receipt worktree identity is stale")
    if mode in SOURCE_MODES | DATA_MODES:
        artifact_bytes = 0
        for target in addresses:
            descriptors = (
                source_artifact_descriptors(receipt, target)
                if mode in SOURCE_MODES
                else data_artifact_descriptors(receipt, target)
            )
            for kind, descriptor in descriptors.items():
                load_ghidra_artifact(resolved_root, target, kind, descriptor)
                artifact_bytes += int(descriptor["bytes"])
        if artifact_bytes > GHIDRA_ARTIFACT_TOTAL_LIMIT:
            raise ValueError("the doctor receipt artifact total exceeds its limit")
    return receipt


def run_doctor(
    mode: str,
    target: str | int | None = None,
    *,
    targets: Iterable[str | int] | None = None,
    resource: str | None = None,
    lane: str | None = None,
    selection_started_at: datetime | None = None,
    root: Path = ROOT,
    expected_branch: str = EXPECTED_BRANCH,
    runner: Runner = _default_runner,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], datetime] = _utc_now,
) -> dict[str, object]:
    if mode not in MODES:
        raise ValueError(f"unsupported mode {mode!r}")
    clean_lane = _validate_lane(mode, lane)
    raw_targets = list(targets or [])
    if target is not None:
        raw_targets.insert(0, target)
    addresses = [int(value, 0) if isinstance(value, str) else int(value) for value in raw_targets]
    if any(address < 0 or address > 0xFFFFFFFF for address in addresses):
        raise ValueError("target addresses must fit in 32 bits")
    address_texts = [f"0x{address:08X}" for address in addresses]
    resource_text = _normalize_resource(resource) if resource is not None else None
    selection_ok = (
        mode == "resource" and not addresses and resource_text is not None
    ) or (
        mode in SOURCE_MODES | DATA_MODES and bool(addresses) and resource_text is None
    ) or (mode == "meta" and not addresses and resource_text is None)
    if not selection_ok or len(addresses) != len(set(addresses)):
        raise ValueError("targets do not match the selected doctor mode")
    root = root.resolve()
    started_at = now()
    selected_at = selection_started_at or started_at
    if selected_at.tzinfo is None:
        raise ValueError("selection_started_at needs a UTC offset")
    if selected_at > started_at:
        raise ValueError("selection_started_at cannot be after the doctor start")
    started_tick = clock()
    checks: list[Check] = []
    branch_identity: dict[str, object] = {"actual": "", "expected": expected_branch}
    head_identity: str | None = None
    origin_identity: dict[str, object] = {}
    try:
        checks.append(
            Check(
                "selection",
                selection_ok,
                "targets match the selected mode" if selection_ok else "targets do not match the selected mode",
                {"addresses": address_texts, "resource": resource_text},
            )
        )
        branch_check = _branch_check(root, runner, expected_branch)
        checks.append(branch_check)
        if isinstance(branch_check.data, dict):
            branch_identity = dict(branch_check.data)
        head_check = _head_check(root, runner)
        checks.append(head_check)
        if isinstance(head_check.data, dict) and isinstance(head_check.data.get("actual"), str):
            head_identity = str(head_check.data["actual"])
        origin_check = _origin_integration_check(root, runner, expected_branch)
        checks.append(origin_check)
        if isinstance(origin_check.data, dict):
            origin_identity = dict(origin_check.data)
        checks.append(_dirt_check(root, runner, mode=mode, head=head_identity))
        checks.append(_writable_check(root))
        checks.extend(_wine_checks(root, runner))
        checks.append(_reccmp_inputs_check(root))
        checks.append(_reccmp_runtime_check(root, runner))
        checks.extend(_ghidra_checks(root, runner, mode, addresses))
        checks.append(_map_check(root))
        checks.append(_reports_check(root, mode, addresses))
    except (OSError, subprocess.SubprocessError) as error:
        checks.append(Check("doctor-internal", False, str(error)))
    try:
        hashes = input_hashes(root, mode)
        required_hashes = {
            "source_worktree_sha256",
            "source_index_sha256",
            "repository_worktree_sha256",
            "repository_index_sha256",
            "resource_sources_sha256",
            "functions_map_sha256",
            "function_sizes_file_sha256",
            "retail_executable_sha256",
            "recompiled_executable_sha256",
            "recompiled_symbols_sha256",
            "reccmp_build_sha256",
            "reccmp_user_sha256",
            "configured_retail_executable_sha256",
            "current_report_sha256",
            "current_report_provenance_sha256",
            "current_data_report_sha256",
            "current_data_report_provenance_sha256",
        }
        optional_hashes = {
            "current_data_report_sha256",
            "current_data_report_provenance_sha256",
        }
        hashes_ok = set(hashes) == required_hashes and all(
            (value is None and name in optional_hashes)
            or (
                isinstance(value, str)
                and bool(re.fullmatch(r"[0-9a-f]{64}", value))
            )
            for name, value in hashes.items()
        )
        checks.append(
            Check(
                "campaign-input-hashes",
                hashes_ok,
                "campaign input hashes are complete" if hashes_ok else "campaign input hashes are incomplete",
            )
        )
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        hashes = {
            "source_worktree_sha256": None,
            "source_index_sha256": None,
            "repository_worktree_sha256": None,
            "repository_index_sha256": None,
            "resource_sources_sha256": None,
            "functions_map_sha256": None,
            "function_sizes_file_sha256": None,
            **{
                name: None
                for name in (
                    "retail_executable_sha256",
                    "recompiled_executable_sha256",
                    "recompiled_symbols_sha256",
                    "reccmp_build_sha256",
                    "reccmp_user_sha256",
                    "configured_retail_executable_sha256",
                    "current_report_sha256",
                    "current_report_provenance_sha256",
                    "current_data_report_sha256",
                    "current_data_report_provenance_sha256",
                )
            },
        }
        checks.append(Check("campaign-input-hashes", False, str(error)))
    ended_tick = clock()
    ended_at = now()
    receipt: dict[str, object] = {
        "schema": 1,
        "status": "ready" if bool(checks) and all(check.ok for check in checks) else "failed",
        "root": str(root),
        "head": head_identity,
        "branch": branch_identity,
        "origin_integration": origin_identity,
        "lane": clean_lane,
        "selection_started_at": _timestamp(selected_at),
        "doctor_started_at": _timestamp(started_at),
        "doctor_ended_at": _timestamp(ended_at),
        "started_at": _timestamp(started_at),
        "ended_at": _timestamp(ended_at),
        "elapsed_seconds": max(0.0, ended_tick - started_tick),
        "mode": mode,
        "addresses": address_texts,
        "target": address_texts[0] if len(address_texts) == 1 else None,
        "resource": resource_text,
        "input_hashes": hashes,
        "ok": bool(checks) and all(check.ok for check in checks),
        "campaign_timing_started": False,
        "checks": [asdict(check) for check in checks],
    }
    receipt["receipt_id"] = _receipt_id(receipt)
    receipt["receipt_path"] = (
        RECEIPT_DIRECTORY_RELATIVE / f"{receipt['receipt_id']}.json"
    ).as_posix()
    _write_receipt(root, receipt)
    return receipt


def _parse_target(value: str) -> str:
    try:
        address = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "use an address such as 0x00401000"
        ) from error
    if not 0 <= address <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("the target address must fit in 32 bits")
    return f"0x{address:08X}"


def _normalize_resource(value: str) -> str:
    fields = value.split(",")
    if len(fields) != 3:
        raise ValueError("resource must use TYPE,ID,LANGUAGE integers")
    try:
        numbers = [int(field, 0) for field in fields]
    except ValueError as error:
        raise ValueError("resource must use TYPE,ID,LANGUAGE integers") from error
    if any(number < 0 or number > 0xFFFFFFFF for number in numbers):
        raise ValueError("resource values must fit in 32 bits")
    return ",".join(str(number) for number in numbers)


def _parse_resource(value: str) -> str:
    try:
        return _normalize_resource(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


class _CliParseError(ValueError):
    pass


class _CliParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise _CliParseError(message)


def _parser() -> argparse.ArgumentParser:
    parser = _CliParser(description=__doc__)
    parser.add_argument("--mode", choices=MODES, required=True)
    parser.add_argument(
        "--lane", choices=tuple(sorted(LANE_MODES)), required=True,
        help="Campaign queue lane",
    )
    parser.add_argument(
        "--target", action="append", dest="targets", type=_parse_target,
        help="Function or data address; repeat for a bundle",
    )
    parser.add_argument(
        "--resource", type=_parse_resource,
        help="Resource tuple as TYPE,ID,LANGUAGE",
    )
    parser.add_argument(
        "--selection-started-at",
        help="UTC timestamp from before candidate selection; defaults to the doctor start",
    )
    parser.add_argument("--json", action="store_true", help="Print the receipt as JSON")
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
    selection_error = None
    if arguments.mode == "resource":
        if arguments.targets or arguments.resource is None:
            selection_error = "resource mode needs one --resource and no --target"
    elif arguments.mode == "meta":
        if arguments.targets or arguments.resource is not None:
            selection_error = "meta mode does not accept --target or --resource"
    elif not arguments.targets or arguments.resource is not None:
        selection_error = f"{arguments.mode} mode needs at least one --target and no --resource"
    if selection_error is not None:
        _print_parse_error(parser, selection_error, as_json=arguments.json)
        return 2
    selection_started_at = None
    if arguments.selection_started_at:
        try:
            selection_started_at = datetime.fromisoformat(
                arguments.selection_started_at.replace("Z", "+00:00")
            )
        except ValueError as error:
            message = f"invalid selection timestamp: {error}"
            if arguments.json:
                print(json.dumps({"ok": False, "error": message}, sort_keys=True))
            else:
                print(f"doctor failed: {message}", file=sys.stderr)
            return 2
    try:
        receipt = run_doctor(
            arguments.mode,
            targets=arguments.targets,
            resource=arguments.resource,
            lane=arguments.lane,
            selection_started_at=selection_started_at,
            root=arguments.root,
            expected_branch=EXPECTED_BRANCH,
        )
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if arguments.json:
            print(json.dumps({"ok": False, "error": str(error)}, sort_keys=True))
        else:
            print(f"doctor failed: {error}", file=sys.stderr)
        return 2
    if arguments.json:
        print(json.dumps(receipt, indent=2, sort_keys=True))
    else:
        state = "PASS" if receipt["ok"] else "FAIL"
        print(f"doctor {state}: {arguments.mode}")
        for check in receipt["checks"]:
            marker = "ok" if check["ok"] else "FAIL"
            print(f"  {marker:4} {check['name']}: {check['detail']}")
        print(f"receipt: {receipt['receipt_path']}")
    return 0 if receipt["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
