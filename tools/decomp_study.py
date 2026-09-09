#!/usr/bin/env python3
"""Evaluate preregistered option studies and route-policy data gates."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
from fractions import Fraction
import hashlib
import json
from math import comb
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
from typing import Callable, Iterator, Mapping, Sequence

from tools import decomp_replay as replay
from tools.decomp_mismatch import ROUTE_ORDER
from tools.decomp_options import (
    HASH_RE,
    MAX_POPULATION,
    MAX_TEXT,
    NAME_RE,
    OptionError,
    REGISTRY_RELATIVE,
    canonical_json,
    find_preregistration,
    read_registry,
    validate_registry,
    validate_preregistration,
)


SCHEMA_VERSION = 1
ROOT = Path(__file__).resolve().parents[1]
ALPHA = Fraction(1, 20)
MIN_CASES = 20
MIN_CAMPAIGNS = 20
MIN_TARGETS = 20
MIN_NON_TIES = 10
MAX_OBSERVATION_ROWS = MAX_POPULATION * 17
MAX_TRAINING_ROWS = 65_536
MAX_CORPUS_BYTES = 16 * 1024 * 1024
MAX_RECEIPT_BYTES = 4 * 1024 * 1024
MAX_GIT_OUTPUT_BYTES = 64 * 1024 * 1024
MAX_HISTORY_COMMITS = 100_000
MAX_PRIVATE_ENTRIES = 4096
MAX_GIT_EXECUTABLE_BYTES = 64 * 1024 * 1024
PRIVATE_STUDIES_NAME = "studies"
OBSERVATIONS_NAME = "observations"
RECEIPTS_NAME = "receipts"
TRAINING_NAME = "training"
TRAINING_RECEIPTS_NAME = "training-receipts"
CORPUS_FILE_NAME = "corpus.json"
COMMIT_RE = re.compile(r"[0-9a-f]{40}(?:[0-9a-f]{24})?\Z")
GIT_EXECUTABLE = Path("/usr/bin/git")

TRUSTED_INPUTS = (
    ".gitignore",
    "tools/__init__.py",
    "tools/decomp",
    "tools/decomp.ps1",
    "tools/decomp_mismatch.py",
    "tools/decomp_options.py",
    "tools/decomp_quality.py",
    "tools/decomp_replay.py",
    "tools/decomp_study.py",
    REGISTRY_RELATIVE.as_posix(),
)
TRUSTED_INPUT_MODES = {
    name: ("100755" if name == "tools/decomp" else "100644")
    for name in TRUSTED_INPUTS
}

OBSERVATION_KEYS = {"schema_version", "kind", "synthetic", "rows"}
OBSERVATION_ROW_KEYS = {
    "case_commitment",
    "campaign_commitment",
    "target_commitment",
    "arm",
    "status",
    "primary",
    "protected",
    "budget_used",
}
TRAINING_KEYS = {"schema_version", "kind", "synthetic", "rows"}
TRAINING_ROW_KEYS = {
    "row_commitment",
    "campaign_commitment",
    "target_commitment",
    "route",
    "label",
    "split",
    "valid",
}
STORED_OBSERVATION_KEYS = {
    "schema_version",
    "kind",
    "study_id",
    "synthetic",
    "recorded_at",
    "preregistration_commit",
    "registry_sha256",
    "acquisition",
    "rows",
    "content_sha256",
}
STORED_OBSERVATION_ROW_KEYS = OBSERVATION_ROW_KEYS | {"observed_at"}
STORAGE_MANIFEST_KEYS = {"kind", "entries"}
STORAGE_ENTRY_KEYS = {"name", "bytes", "sha256", "content_sha256"}
PREREGISTRATION_BINDING_KEYS = {
    "study_id",
    "introduction_commit",
    "commit_time",
    "registry_blob_sha256",
}
REPOSITORY_BINDING_KEYS = {
    "branch",
    "head",
    "origin_head",
    "index_sha256",
    "head_tree",
    "trusted_worktree_sha256",
    "registry_sha256",
    "tools",
    "python_runtime",
    "git_runtime",
}
STUDY_RECEIPT_KEYS = {
    "schema_version",
    "kind",
    "status",
    "study_id",
    "issued_at",
    "result",
    "preregistration",
    "repository",
    "storage_manifest",
    "evaluation",
    "content_sha256",
}
TRAINING_CORPUS_KEYS = {
    "schema_version",
    "kind",
    "study_id",
    "study_receipt_sha256",
    "synthetic",
    "recorded_at",
    "registry_sha256",
    "acquisition",
    "rows",
    "content_sha256",
}
STORED_TRAINING_ROW_KEYS = TRAINING_ROW_KEYS | {"observed_at"}
TRAINING_RECEIPT_KEYS = {
    "schema_version",
    "kind",
    "status",
    "study_id",
    "issued_at",
    "study_receipt",
    "corpus",
    "repository",
    "storage_manifest",
    "evaluation",
    "content_sha256",
}


class StudyError(ValueError):
    """Report invalid study evidence."""


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _canonical_time(value: datetime) -> str:
    if value.tzinfo is None or value.utcoffset() is None:
        raise StudyError("a study time is not UTC")
    normalized = value.astimezone(timezone.utc).replace(microsecond=0)
    return normalized.isoformat(timespec="seconds")


def _clock_time(clock: Callable[[], datetime], description: str) -> datetime:
    value = clock()
    if value.tzinfo is None or value.utcoffset() is None:
        raise StudyError(f"the {description} is not UTC")
    return value.astimezone(timezone.utc).replace(microsecond=0)


def _parse_time(value: object, description: str) -> datetime:
    if not isinstance(value, str):
        raise StudyError(f"the {description} is invalid")
    try:
        parsed = datetime.fromisoformat(value)
    except ValueError as error:
        raise StudyError(f"the {description} is invalid") from error
    if parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise StudyError(f"the {description} is not UTC")
    if parsed.isoformat(timespec="seconds") != value:
        raise StudyError(f"the {description} is not canonical")
    return parsed


def _json_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise StudyError("a private study document repeats a key")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> object:
    raise StudyError(f"a private study document contains {value}")


def _strict_json(content: bytes, description: str) -> dict[str, object]:
    try:
        value = json.loads(
            content.decode("utf-8"),
            object_pairs_hook=_json_pairs,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise StudyError(f"the {description} is not valid JSON") from error
    if not isinstance(value, dict):
        raise StudyError(f"the {description} must be a JSON object")
    return value


def _content_id(value: Mapping[str, object]) -> str:
    payload = dict(value)
    payload.pop("content_sha256", None)
    return hashlib.sha256(canonical_json(payload)).hexdigest()


def _encoded_document(value: Mapping[str, object]) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _descriptor(content: bytes, document: Mapping[str, object]) -> dict[str, object]:
    content_id = document.get("content_sha256")
    if (
        not isinstance(content_id, str)
        or HASH_RE.fullmatch(content_id) is None
        or _content_id(document) != content_id
    ):
        raise StudyError("a private study content ID is invalid")
    return {
        "name": CORPUS_FILE_NAME,
        "bytes": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
        "content_sha256": content_id,
    }


@contextmanager
def _study_directory(
    root: Path,
    components: Sequence[str] = (),
    *,
    create: bool,
) -> Iterator[int]:
    """Open one owner-only studies directory below the replay root."""

    for component in components:
        if (
            component not in {
                OBSERVATIONS_NAME,
                RECEIPTS_NAME,
                TRAINING_NAME,
                TRAINING_RECEIPTS_NAME,
            }
            and HASH_RE.fullmatch(component) is None
        ):
            raise StudyError("a private study directory name is invalid")
    opened = -1
    try:
        with replay._private_directory(root, create=create) as private_fd:
            opened = replay._open_directory(
                private_fd, PRIVATE_STUDIES_NAME, create=create
            )
            for component in components:
                child = replay._open_directory(opened, component, create=create)
                os.close(opened)
                opened = child
            yield opened
            os.close(opened)
            opened = -1
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    finally:
        if opened >= 0:
            os.close(opened)


def _directory_names(directory_fd: int) -> set[str]:
    names: set[str] = set()
    try:
        with os.scandir(directory_fd) as entries:
            for entry in entries:
                if len(names) >= MAX_PRIVATE_ENTRIES:
                    raise StudyError("a private study directory has too many entries")
                if entry.name in names:
                    raise StudyError("a private study directory repeats an entry")
                names.add(entry.name)
    except OSError as error:
        raise StudyError("cannot enumerate a private study directory") from error
    return names


def _require_matching_temporary(
    directory_fd: int,
    final_name: str,
    content: bytes,
) -> None:
    """Reject an unknown crash residue before replay recovery can remove it."""

    temporary = f".{final_name}.tmp"
    try:
        os.stat(temporary, dir_fd=directory_fd, follow_symlinks=False)
    except FileNotFoundError:
        return
    except OSError as error:
        raise StudyError("cannot inspect a private study temporary file") from error
    try:
        saved, _ = replay._read_private_file_at(
            directory_fd,
            temporary,
            max(len(content), 1),
            "private study temporary file",
            allow_empty=True,
        )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    if saved != content:
        raise StudyError("a private study temporary file has unknown content")


def _publish_corpus_locked(
    root: Path,
    category: str,
    study_id: str,
    content: bytes,
    *,
    preregistration: Mapping[str, object] | None = None,
) -> None:
    if not 0 < len(content) <= MAX_CORPUS_BYTES:
        raise StudyError("a private study corpus is too large")
    document = _strict_json(content, "private study corpus")
    if content != _encoded_document(document):
        raise StudyError("the private study corpus encoding is not canonical")
    _validate_corpus_document(
        category, study_id, document, preregistration=preregistration
    )
    _descriptor(content, document)
    try:
        with _study_directory(
            root, (category, study_id), create=True
        ) as directory_fd:
            names = _directory_names(directory_fd)
            temporary = f".{CORPUS_FILE_NAME}.tmp"
            if names - {CORPUS_FILE_NAME, temporary}:
                raise StudyError("a private study corpus directory has extra entries")
            _require_matching_temporary(directory_fd, CORPUS_FILE_NAME, content)
            replay._write_exclusive_at(
                directory_fd,
                CORPUS_FILE_NAME,
                content,
                reject_unknown_temporary=True,
            )
            if _directory_names(directory_fd) != {CORPUS_FILE_NAME}:
                raise StudyError("a private study corpus directory is incomplete")
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error


def _read_corpus_locked(
    root: Path,
    category: str,
    study_id: str,
    *,
    preregistration: Mapping[str, object] | None = None,
) -> tuple[dict[str, object], bytes, dict[str, object]]:
    try:
        with _study_directory(
            root, (category, study_id), create=False
        ) as directory_fd:
            if _directory_names(directory_fd) != {CORPUS_FILE_NAME}:
                raise StudyError(
                    "a private study corpus must contain exactly one result file"
                )
            content = replay._read_at(
                directory_fd,
                CORPUS_FILE_NAME,
                MAX_CORPUS_BYTES,
                "private study corpus",
            )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    document = _strict_json(content, "private study corpus")
    if content != _encoded_document(document):
        raise StudyError("the private study corpus encoding is not canonical")
    _validate_corpus_document(
        category, study_id, document, preregistration=preregistration
    )
    return document, content, {
        "kind": "private-study-storage-manifest",
        "entries": [_descriptor(content, document)],
    }


def _publish_receipt_locked(
    root: Path,
    category: str,
    receipt: Mapping[str, object],
) -> tuple[dict[str, object], bytes]:
    receipt_id = receipt.get("content_sha256")
    if (
        not isinstance(receipt_id, str)
        or HASH_RE.fullmatch(receipt_id) is None
        or _content_id(receipt) != receipt_id
    ):
        raise StudyError("a study receipt content ID is invalid")
    _validate_receipt_document(category, receipt)
    content = _encoded_document(receipt)
    if not 0 < len(content) <= MAX_RECEIPT_BYTES:
        raise StudyError("a study receipt is too large")
    try:
        with _study_directory(root, (category,), create=True) as directory_fd:
            final_name = f"{receipt_id}.json"
            _require_matching_temporary(directory_fd, final_name, content)
            replay._write_exclusive_at(
                directory_fd,
                final_name,
                content,
                reject_unknown_temporary=True,
            )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return dict(receipt), content


def _read_receipt_locked(
    root: Path,
    category: str,
    receipt_id: str,
) -> tuple[dict[str, object], bytes]:
    _commitment(receipt_id, "study receipt ID")
    try:
        with _study_directory(root, (category,), create=False) as directory_fd:
            content = replay._read_at(
                directory_fd,
                f"{receipt_id}.json",
                MAX_RECEIPT_BYTES,
                "private study receipt",
            )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    document = _strict_json(content, "private study receipt")
    if content != _encoded_document(document):
        raise StudyError("the private study receipt encoding is not canonical")
    if (
        document.get("content_sha256") != receipt_id
        or _content_id(document) != receipt_id
    ):
        raise StudyError("the study receipt content ID is invalid")
    _validate_receipt_document(category, document)
    return document, content


def _git_output(root: Path, arguments: Sequence[str], maximum: int) -> bytes:
    """Run one bounded Git query."""

    try:
        process = subprocess.Popen(
            _git_command(root, arguments),
            cwd=root,
            env=_git_environment(),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        assert process.stdout is not None
        with process.stdout:
            content = process.stdout.read(maximum + 1)
        if len(content) > maximum:
            process.kill()
        returncode = process.wait()
    except OSError as error:
        raise StudyError("a study Git query is unavailable") from error
    if len(content) > maximum:
        raise StudyError("a study Git query returned too much data")
    if returncode != 0:
        raise StudyError("a study Git query failed")
    return content


def _git_environment() -> dict[str, str]:
    return {
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_OPTIONAL_LOCKS": "0",
        "GIT_TERMINAL_PROMPT": "0",
        "HOME": "/",
        "LANG": "C",
        "LC_ALL": "C",
        "PATH": "/usr/bin:/bin",
    }


def _git_command(root: Path, arguments: Sequence[str]) -> list[str]:
    command = [
        str(GIT_EXECUTABLE),
        "--no-pager",
        "--no-replace-objects",
        "--no-optional-locks",
    ]
    if list(arguments) != ["--version"]:
        resolved = root.resolve()
        command.extend(
            [
                f"--git-dir={resolved / '.git'}",
                f"--work-tree={resolved}",
                "-c",
                "core.fsmonitor=false",
                "-c",
                "core.hooksPath=/dev/null",
                "-c",
                "core.attributesFile=/dev/null",
            ]
        )
    command.extend(arguments)
    return command


def _git_returncode(root: Path, arguments: Sequence[str]) -> int:
    try:
        completed = subprocess.run(
            _git_command(root, arguments),
            cwd=root,
            env=_git_environment(),
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as error:
        raise StudyError("a study Git query is unavailable") from error
    return completed.returncode


def _git_text(root: Path, arguments: Sequence[str], maximum: int = 4096) -> str:
    try:
        return _git_output(root, arguments, maximum).decode("utf-8").strip()
    except UnicodeDecodeError as error:
        raise StudyError("a study Git query returned invalid text") from error


def _git_ref(root: Path, name: str) -> str | None:
    try:
        value = _git_text(root, ["rev-parse", "--verify", f"{name}^{{commit}}"])
    except StudyError:
        return None
    value = value.lower()
    return value if COMMIT_RE.fullmatch(value) is not None else None


def _symbolic_branch(root: Path) -> str | None:
    try:
        value = _git_text(root, ["symbolic-ref", "--quiet", "--short", "HEAD"])
    except StudyError:
        return None
    return value if 0 < len(value) <= 128 else None


def _git_blob(
    root: Path,
    revision: str,
    relative: Path,
    maximum: int,
    *,
    modes: frozenset[str] = frozenset({"100644"}),
) -> bytes:
    if (
        COMMIT_RE.fullmatch(revision) is None
        or relative.is_absolute()
        or not relative.parts
        or any(part in {"", ".", ".."} for part in relative.parts)
    ):
        raise StudyError("a required Git evidence blob is invalid")
    tree = _git_output(
        root,
        ["ls-tree", "-z", revision, "--", relative.as_posix()],
        8192,
    )
    if not tree.endswith(b"\0") or tree.count(b"\0") != 1 or b"\t" not in tree:
        raise StudyError("a required Git evidence blob is invalid")
    header, saved_path = tree[:-1].split(b"\t", 1)
    fields = header.split()
    try:
        path_text = saved_path.decode("utf-8")
        mode = fields[0].decode("ascii")
        object_type = fields[1].decode("ascii")
        object_id = fields[2].decode("ascii")
    except (IndexError, UnicodeDecodeError) as error:
        raise StudyError("a required Git evidence blob is invalid") from error
    if (
        len(fields) != 3
        or mode not in modes
        or object_type != "blob"
        or COMMIT_RE.fullmatch(object_id) is None
        or path_text != relative.as_posix()
    ):
        raise StudyError("a required Git evidence blob is invalid")
    size_text = _git_text(root, ["cat-file", "-s", object_id])
    try:
        size = int(size_text)
    except ValueError as error:
        raise StudyError("a required Git evidence blob is invalid") from error
    if not 0 < size <= maximum:
        raise StudyError("a required Git evidence blob is invalid")
    content = _git_output(root, ["cat-file", "blob", object_id], maximum)
    if len(content) != size:
        raise StudyError("a required Git evidence blob is invalid")
    return content


def _git_runtime() -> dict[str, object]:
    directory = -1
    descriptor = -1
    try:
        directory = os.open("/", os.O_RDONLY | os.O_DIRECTORY)
        if _metadata_is_user_writable(os.fstat(directory)):
            raise OSError
        for component in GIT_EXECUTABLE.parts[1:-1]:
            child = os.open(
                component,
                os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                dir_fd=directory,
            )
            metadata = os.fstat(child)
            if _metadata_is_user_writable(metadata):
                os.close(child)
                raise OSError
            os.close(directory)
            directory = child
        descriptor = os.open(
            GIT_EXECUTABLE.name,
            os.O_RDONLY | os.O_NOFOLLOW,
            dir_fd=directory,
        )
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = -1
            before = os.fstat(stream.fileno())
            digest = hashlib.sha256()
            total = 0
            while True:
                content = stream.read(1024 * 1024)
                if not content:
                    break
                total += len(content)
                if total > MAX_GIT_EXECUTABLE_BYTES:
                    raise OSError
                digest.update(content)
            after = os.fstat(stream.fileno())
    except OSError as error:
        raise StudyError("the trusted Git runtime is unavailable") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if directory >= 0:
            os.close(directory)
    identity = (
        before.st_dev,
        before.st_ino,
        before.st_mode,
        before.st_nlink,
        before.st_uid,
        before.st_size,
        before.st_mtime_ns,
        before.st_ctime_ns,
    )
    if (
        identity
        != (
            after.st_dev,
            after.st_ino,
            after.st_mode,
            after.st_nlink,
            after.st_uid,
            after.st_size,
            after.st_mtime_ns,
            after.st_ctime_ns,
        )
        or not stat.S_ISREG(after.st_mode)
        or after.st_nlink != 1
        or _metadata_is_user_writable(after)
        or not after.st_mode & 0o111
        or after.st_size != total
        or total <= 0
    ):
        raise StudyError("the trusted Git runtime is invalid")
    version = _git_output(Path("/"), ["--version"], 256).decode(
        "ascii", errors="strict"
    ).strip()
    if not version.startswith("git version ") or len(version) > 128:
        raise StudyError("the trusted Git runtime version is invalid")
    return {
        "path": GIT_EXECUTABLE.as_posix(),
        "version": version,
        "sha256": digest.hexdigest(),
    }


def _metadata_is_user_writable(metadata: os.stat_result) -> bool:
    effective_uid = os.geteuid() if hasattr(os, "geteuid") else None
    effective_gid = os.getegid() if hasattr(os, "getegid") else None
    groups = set(os.getgroups()) if hasattr(os, "getgroups") else set()
    if effective_gid is not None:
        groups.add(effective_gid)
    if metadata.st_mode & stat.S_IWOTH:
        return True
    if metadata.st_mode & stat.S_IWGRP and metadata.st_gid in groups:
        return True
    return bool(
        effective_uid not in {None, 0}
        and metadata.st_uid == effective_uid
    )


def _private_root_is_untracked(root: Path, head: str) -> bool:
    private = replay.PRIVATE_RELATIVE.as_posix()
    try:
        ignore_content = _git_blob(
            root, head, Path(".gitignore"), replay.MAX_TOOL_BYTES
        )
        current_ignore = replay._regular_bytes(
            root / ".gitignore",
            replay.MAX_TOOL_BYTES,
            "study private-root ignore file",
        )
        indexed = _git_output(root, ["ls-files", "-z", "--", private], 4096)
        committed = _git_output(
            root,
            ["ls-tree", "-r", "-z", "--name-only", head, "--", private],
            4096,
        )
        ignored = _git_output(
            root,
            ["check-ignore", "-v", "--no-index", "--", f"{private}/probe"],
            4096,
        ).decode("utf-8")
    except (StudyError, replay.ReplayError, UnicodeDecodeError):
        return False
    return (
        not indexed
        and not committed
        and current_ignore == ignore_content
        and re.fullmatch(
            r"\.gitignore:[0-9]+:/\.decomp-replay/\t\.decomp-replay/probe\r?\n?",
            ignored,
        )
        is not None
    )


def _git_is_ancestor(root: Path, ancestor: str, descendant: str) -> bool:
    returncode = _git_returncode(
        root, ["merge-base", "--is-ancestor", ancestor, descendant]
    )
    if returncode not in {0, 1}:
        raise StudyError("the preregistration ancestry is invalid")
    return returncode == 0


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.resolve(strict=True).open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise StudyError("the study Python runtime is unavailable") from error
    return digest.hexdigest()


def _tool_binding(root: Path, head: str) -> dict[str, str]:
    """Bind each study dependency to one clean HEAD blob."""

    result: dict[str, str] = {}
    for name in TRUSTED_INPUTS:
        relative = Path(name)
        modes = frozenset({TRUSTED_INPUT_MODES[name]})
        try:
            committed = _git_blob(
                root,
                head,
                relative,
                replay.MAX_TOOL_BYTES,
                modes=modes,
            )
            current = replay._regular_bytes(
                root / relative,
                replay.MAX_TOOL_BYTES,
                "study validation dependency",
            )
        except replay.ReplayError as error:
            raise StudyError(str(error)) from error
        if current != committed:
            raise StudyError("a study validation dependency is not clean at HEAD")
        result[relative.as_posix()] = hashlib.sha256(committed).hexdigest()
    return result


@contextmanager
def _held_git_directory(root: Path) -> Iterator[tuple[int, ...]]:
    root_fd = -1
    git_fd = -1
    try:
        root_fd = replay._open_absolute_directory(root)
        git_fd = os.open(
            ".git",
            os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
            dir_fd=root_fd,
        )
        metadata = os.fstat(git_fd)
        identity = (
            metadata.st_dev,
            metadata.st_ino,
            metadata.st_mode,
            metadata.st_nlink,
            metadata.st_uid,
            metadata.st_gid,
        )
        if not stat.S_ISDIR(metadata.st_mode):
            raise OSError
        yield identity
        after = os.fstat(git_fd)
        named = os.stat(".git", dir_fd=root_fd, follow_symlinks=False)
        after_identity = (
            after.st_dev,
            after.st_ino,
            after.st_mode,
            after.st_nlink,
            after.st_uid,
            after.st_gid,
        )
        named_identity = (
            named.st_dev,
            named.st_ino,
            named.st_mode,
            named.st_nlink,
            named.st_uid,
            named.st_gid,
        )
        if identity != after_identity or identity != named_identity:
            raise StudyError("the study Git directory changed")
    except (OSError, replay.ReplayError) as error:
        raise StudyError("the study Git directory is invalid") from error
    finally:
        if git_fd >= 0:
            os.close(git_fd)
        if root_fd >= 0:
            os.close(root_fd)


def _repository_binding(
    root: Path,
    registry_content: bytes,
) -> dict[str, object]:
    """Require one clean current agent/continuous repository."""

    root = root.resolve()
    with _held_git_directory(root):
        return _repository_binding_held(root, registry_content)


def _repository_binding_held(
    root: Path,
    registry_content: bytes,
) -> dict[str, object]:
    git_runtime = _git_runtime()
    git_directory = root / ".git"
    try:
        git_metadata = git_directory.lstat()
    except OSError as error:
        raise StudyError("the study Git directory is invalid") from error
    if not stat.S_ISDIR(git_metadata.st_mode):
        raise StudyError("the study Git directory is invalid")
    git_identity = (
        git_metadata.st_dev,
        git_metadata.st_ino,
        git_metadata.st_mode,
        git_metadata.st_nlink,
        git_metadata.st_uid,
    )
    if (
        _git_text(root, ["rev-parse", "--show-toplevel"])
        != root.as_posix()
        or _git_text(root, ["rev-parse", "--absolute-git-dir"])
        != git_directory.as_posix()
        or _git_text(
            root, ["rev-parse", "--path-format=absolute", "--git-common-dir"]
        )
        != git_directory.as_posix()
    ):
        raise StudyError("the study repository identity is invalid")
    for forbidden in (git_directory / "shallow", git_directory / "info/grafts"):
        try:
            forbidden.lstat()
        except FileNotFoundError:
            pass
        except OSError as error:
            raise StudyError("the study history metadata is invalid") from error
        else:
            raise StudyError("a study requires complete ungrafted history")
    if _git_text(root, ["rev-parse", "--is-shallow-repository"]) != "false":
        raise StudyError("a study requires complete history")
    if _git_output(root, ["for-each-ref", "--format=%(refname)", "refs/replace"], 4096):
        raise StudyError("a study does not permit replacement refs")
    try:
        head = _git_ref(root, "HEAD")
        origin = _git_ref(root, "refs/remotes/origin/agent/continuous")
        branch = _symbolic_branch(root)
    except StudyError:
        raise
    if (
        head is None
        or origin is None
        or head != origin
        or branch != "agent/continuous"
    ):
        raise StudyError("a study requires the current agent/continuous origin")
    try:
        index_returncode = _git_returncode(
            root,
            [
                "diff",
                "--cached",
                "--quiet",
                "--no-ext-diff",
                "--no-textconv",
                "--ignore-submodules=none",
            ],
        )
    except StudyError:
        raise
    if index_returncode != 0:
        raise StudyError("a study requires a clean index")
    index = _git_output(
        root, ["ls-files", "--stage", "-z"], MAX_GIT_OUTPUT_BYTES
    )
    head_tree = _git_text(root, ["rev-parse", "HEAD^{tree}"])
    if not index or not head_tree:
        raise StudyError("the study index identity is invalid")
    count_text = _git_text(root, ["rev-list", "--count", "HEAD"])
    try:
        history_count = int(count_text)
    except ValueError as error:
        raise StudyError("the study history count is invalid") from error
    if not 0 < history_count <= MAX_HISTORY_COMMITS:
        raise StudyError("the study history is too large or empty")
    private_history = _git_output(
        root,
        [
            "log",
            "--full-history",
            "--format=",
            "--name-only",
            "-z",
            "HEAD",
            "--",
            replay.PRIVATE_RELATIVE.as_posix(),
        ],
        MAX_GIT_OUTPUT_BYTES,
    )
    if private_history:
        raise StudyError("the private study root appears in HEAD history")
    try:
        private_ok = _private_root_is_untracked(root, head)
        registry_at_head = _git_blob(
            root,
            head,
            REGISTRY_RELATIVE,
            replay.MAX_TOOL_BYTES,
            modes=frozenset({"100644"}),
        )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    if not private_ok:
        raise StudyError("the private study root is tracked or not ignored")
    if registry_at_head != registry_content:
        raise StudyError("the option registry is not clean at HEAD")
    tools = _tool_binding(root, head)
    try:
        final_git_metadata = git_directory.lstat()
    except OSError as error:
        raise StudyError("the study Git directory changed") from error
    if git_identity != (
        final_git_metadata.st_dev,
        final_git_metadata.st_ino,
        final_git_metadata.st_mode,
        final_git_metadata.st_nlink,
        final_git_metadata.st_uid,
    ) or _git_runtime() != git_runtime:
        raise StudyError("the study Git runtime changed")
    return {
        "branch": branch,
        "head": head,
        "origin_head": origin,
        "index_sha256": hashlib.sha256(index).hexdigest(),
        "head_tree": head_tree,
        "trusted_worktree_sha256": hashlib.sha256(canonical_json(tools)).hexdigest(),
        "registry_sha256": hashlib.sha256(registry_content).hexdigest(),
        "tools": tools,
        "python_runtime": {
            "implementation": sys.implementation.name,
            "version": list(sys.version_info[:3]),
            "executable_sha256": _file_sha256(Path(sys.executable)),
        },
        "git_runtime": git_runtime,
    }


def _registry_at_commit(root: Path, commit: str) -> tuple[dict[str, object], bytes]:
    try:
        content = _git_blob(
            root,
            commit,
            REGISTRY_RELATIVE,
            replay.MAX_TOOL_BYTES,
            modes=frozenset({"100644"}),
        )
    except replay.ReplayError as error:
        raise StudyError("the preregistration commit has no valid registry") from error
    try:
        return validate_registry(_strict_json(content, "historical option registry")), content
    except OptionError as error:
        raise StudyError(str(error)) from error


def _registry_at_commit_if_present(
    root: Path, commit: str
) -> tuple[dict[str, object], bytes] | None:
    tree = _git_output(
        root,
        ["ls-tree", "-z", commit, "--", REGISTRY_RELATIVE.as_posix()],
        8192,
    )
    if not tree:
        return None
    return _registry_at_commit(root, commit)


def _parent_commits(root: Path, commit: str) -> list[str]:
    values = _git_text(root, ["rev-list", "--parents", "-n", "1", commit]).split()
    if not values or values[0] != commit:
        raise StudyError("the preregistration commit identity is invalid")
    if len(values) > 17:
        raise StudyError("the preregistration commit has too many parents")
    return values[1:]


def _preregistration_binding(
    root: Path,
    preregistration: Mapping[str, object],
    introduction_commit: str,
    observation_times: Sequence[datetime],
    *,
    head: str,
    origin: str,
) -> dict[str, object]:
    """Prove the exact ancestor that introduced one preregistration."""

    if COMMIT_RE.fullmatch(introduction_commit) is None:
        raise StudyError("the preregistration commit is invalid")
    if not _git_is_ancestor(root, introduction_commit, head) or not _git_is_ancestor(
        root, introduction_commit, origin
    ):
        raise StudyError("the preregistration commit is not a current ancestor")
    historical_registry, content = _registry_at_commit(root, introduction_commit)
    study_id = str(preregistration.get("study_id"))
    historical = find_preregistration(historical_registry, study_id)
    if historical != dict(preregistration):
        raise StudyError("the preregistration commit does not contain the exact study")
    for parent in _parent_commits(root, introduction_commit):
        parent_value = _registry_at_commit_if_present(root, parent)
        if parent_value is None:
            continue
        parent_registry, _ = parent_value
        if find_preregistration(parent_registry, study_id) == dict(preregistration):
            raise StudyError("the supplied commit did not introduce the preregistration")
    commit_text = _git_text(
        root, ["show", "-s", "--format=%cI", introduction_commit]
    )
    try:
        commit_time = datetime.fromisoformat(commit_text).astimezone(timezone.utc)
    except ValueError as error:
        raise StudyError("the preregistration commit time is invalid") from error
    commit_time = commit_time.replace(microsecond=0)
    if not observation_times or any(value < commit_time for value in observation_times):
        raise StudyError("each observation must follow the preregistration commit")
    return {
        "study_id": study_id,
        "introduction_commit": introduction_commit,
        "commit_time": _canonical_time(commit_time),
        "registry_blob_sha256": hashlib.sha256(content).hexdigest(),
    }


def _validate_storage_entry(value: object, description: str) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise StudyError(f"the {description} is invalid")
    _strict_keys(value, STORAGE_ENTRY_KEYS, description)
    name = value.get("name")
    byte_count = value.get("bytes")
    if (
        not isinstance(name, str)
        or not name
        or len(name) > MAX_TEXT
        or "/" in name
        or "\\" in name
        or type(byte_count) is not int
        or not 0 < byte_count <= MAX_CORPUS_BYTES
    ):
        raise StudyError(f"the {description} is invalid")
    return {
        "name": name,
        "bytes": byte_count,
        "sha256": _commitment(value.get("sha256"), f"{description} file hash"),
        "content_sha256": _commitment(
            value.get("content_sha256"), f"{description} content ID"
        ),
    }


def _validate_storage_manifest(value: object) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise StudyError("a private study storage manifest is invalid")
    _strict_keys(value, STORAGE_MANIFEST_KEYS, "private study storage manifest")
    entries = value.get("entries")
    if (
        value.get("kind") != "private-study-storage-manifest"
        or not isinstance(entries, list)
        or len(entries) != 1
    ):
        raise StudyError("a private study storage manifest is invalid")
    return {
        "kind": "private-study-storage-manifest",
        "entries": [_validate_storage_entry(entries[0], "storage entry")],
    }


def _validate_repository_binding(value: object) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise StudyError("a study repository binding is invalid")
    _strict_keys(value, REPOSITORY_BINDING_KEYS, "study repository binding")
    tools = value.get("tools")
    runtime = value.get("python_runtime")
    git_runtime = value.get("git_runtime")
    if not isinstance(tools, Mapping) or set(tools) != set(TRUSTED_INPUTS):
        raise StudyError("a study tool binding is invalid")
    normalized_tools = {
        str(name): _commitment(saved, "study tool hash")
        for name, saved in tools.items()
    }
    if not isinstance(runtime, Mapping) or set(runtime) != {
        "implementation",
        "version",
        "executable_sha256",
    }:
        raise StudyError("a study Python runtime binding is invalid")
    version = runtime.get("version")
    if (
        runtime.get("implementation") not in {"cpython", "pypy"}
        or not isinstance(version, list)
        or len(version) != 3
        or any(type(item) is not int or item < 0 for item in version)
    ):
        raise StudyError("a study Python runtime binding is invalid")
    if not isinstance(git_runtime, Mapping) or set(git_runtime) != {
        "path",
        "version",
        "sha256",
    }:
        raise StudyError("a study Git runtime binding is invalid")
    git_version = git_runtime.get("version")
    if (
        git_runtime.get("path") != GIT_EXECUTABLE.as_posix()
        or not isinstance(git_version, str)
        or not git_version.startswith("git version ")
        or len(git_version) > 128
    ):
        raise StudyError("a study Git runtime binding is invalid")
    branch = value.get("branch")
    if branch != "agent/continuous":
        raise StudyError("a study branch binding is invalid")
    result: dict[str, object] = {
        "branch": branch,
        "head": value.get("head"),
        "origin_head": value.get("origin_head"),
        "index_sha256": value.get("index_sha256"),
        "head_tree": value.get("head_tree"),
        "trusted_worktree_sha256": value.get("trusted_worktree_sha256"),
        "registry_sha256": value.get("registry_sha256"),
        "tools": normalized_tools,
        "python_runtime": {
            "implementation": runtime["implementation"],
            "version": version,
            "executable_sha256": _commitment(
                runtime.get("executable_sha256"), "Python runtime hash"
            ),
        },
        "git_runtime": {
            "path": git_runtime["path"],
            "version": git_version,
            "sha256": _commitment(
                git_runtime.get("sha256"), "Git executable hash"
            ),
        },
    }
    for key in (
        "head",
        "origin_head",
        "head_tree",
        "index_sha256",
        "trusted_worktree_sha256",
        "registry_sha256",
    ):
        raw = result[key]
        if key in {"head", "origin_head", "head_tree"}:
            if not isinstance(raw, str) or COMMIT_RE.fullmatch(raw) is None:
                raise StudyError("a study repository object ID is invalid")
        else:
            _commitment(raw, f"study repository {key}")
    return result


def _validate_preregistration_binding(value: object) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise StudyError("a preregistration binding is invalid")
    _strict_keys(value, PREREGISTRATION_BINDING_KEYS, "preregistration binding")
    commit = value.get("introduction_commit")
    if not isinstance(commit, str) or COMMIT_RE.fullmatch(commit) is None:
        raise StudyError("a preregistration commit is invalid")
    _parse_time(value.get("commit_time"), "preregistration commit time")
    return {
        "study_id": _commitment(value.get("study_id"), "preregistration study ID"),
        "introduction_commit": commit,
        "commit_time": value["commit_time"],
        "registry_blob_sha256": _commitment(
            value.get("registry_blob_sha256"), "historical registry hash"
        ),
    }


def _validate_fraction(value: object, description: str) -> None:
    if not isinstance(value, Mapping) or set(value) != {"numerator", "denominator"}:
        raise StudyError(f"the {description} is invalid")
    numerator = value.get("numerator")
    denominator = value.get("denominator")
    if (
        type(numerator) is not int
        or numerator < 0
        or type(denominator) is not int
        or denominator <= 0
        or Fraction(numerator, denominator).denominator != denominator
    ):
        raise StudyError(f"the {description} is invalid")


def _validate_study_evaluation(value: object, study_id: str) -> None:
    keys = {
        "schema_version",
        "kind",
        "status",
        "study_id",
        "real_evidence",
        "aggregate",
        "treatments",
        "winner",
        "failure_codes",
    }
    aggregate_keys = {
        "cases",
        "campaigns",
        "targets",
        "declared_treatments",
        "expected_cells",
        "observed_rows",
        "missing_cells",
        "extra_cells",
        "duplicate_cells",
        "failed_or_invalid_rows",
        "incomplete_budget_rows",
    }
    treatment_keys = {
        "treatment",
        "complete",
        "wins",
        "losses",
        "ties",
        "non_ties",
        "protected_regressions",
        "primary_aggregate",
        "sign_test_p",
        "bonferroni_alpha",
        "passes",
    }
    if not isinstance(value, Mapping):
        raise StudyError("a study evaluation is invalid")
    _strict_keys(value, keys, "study evaluation")
    if (
        value.get("schema_version") != SCHEMA_VERSION
        or value.get("kind") != "option-study-evaluation"
        or value.get("status") not in {"passed", "failed"}
        or value.get("study_id") != study_id
        or type(value.get("real_evidence")) is not bool
    ):
        raise StudyError("a study evaluation identity is invalid")
    aggregate = value.get("aggregate")
    if not isinstance(aggregate, Mapping):
        raise StudyError("a study evaluation aggregate is invalid")
    _strict_keys(aggregate, aggregate_keys, "study evaluation aggregate")
    if any(type(aggregate[key]) is not int or aggregate[key] < 0 for key in aggregate_keys):
        raise StudyError("a study evaluation aggregate is invalid")
    treatments = value.get("treatments")
    if not isinstance(treatments, list) or not treatments or len(treatments) > 16:
        raise StudyError("a study treatment evaluation list is invalid")
    names: set[str] = set()
    passing = 0
    for item in treatments:
        if not isinstance(item, Mapping):
            raise StudyError("a study treatment evaluation is invalid")
        _strict_keys(item, treatment_keys, "study treatment evaluation")
        name = _name(item.get("treatment"), "study treatment evaluation ID")
        if name in names:
            raise StudyError("a study treatment evaluation repeats a treatment")
        names.add(name)
        for key in ("complete", "passes"):
            if type(item.get(key)) is not bool:
                raise StudyError("a study treatment evaluation Boolean is invalid")
        for key in (
            "wins",
            "losses",
            "ties",
            "non_ties",
            "protected_regressions",
        ):
            if type(item.get(key)) is not int or int(item[key]) < 0:
                raise StudyError("a study treatment evaluation count is invalid")
        primary = item.get("primary_aggregate")
        if primary is not None and type(primary) is not int:
            raise StudyError("a study primary aggregate is invalid")
        _validate_fraction(item.get("sign_test_p"), "sign-test probability")
        _validate_fraction(item.get("bonferroni_alpha"), "Bonferroni alpha")
        passing += item.get("passes") is True
    winner = value.get("winner")
    if winner is not None and winner not in names:
        raise StudyError("a study winner is invalid")
    failures = value.get("failure_codes")
    if (
        not isinstance(failures, list)
        or len(failures) > 64
        or any(not isinstance(item, str) or not item for item in failures)
        or len(failures) != len(set(failures))
    ):
        raise StudyError("a study failure list is invalid")
    if (value.get("status") == "passed") != (not failures):
        raise StudyError("a study status disagrees with its failures")
    if value.get("status") == "passed" and (passing != 1 or winner is None):
        raise StudyError("a passing study has no unique treatment")
    if value.get("status") == "passed" and value.get("real_evidence") is not True:
        raise StudyError("a passing study has no verified evidence")


def _validate_training_evaluation(value: object, study_id: str) -> None:
    keys = {
        "schema_version",
        "kind",
        "status",
        "route_policy_only",
        "training_performed",
        "verified_evidence",
        "linked_study_id",
        "aggregate",
        "failure_codes",
    }
    aggregate_keys = {
        "rows",
        "valid_rows",
        "invalid_rows",
        "duplicate_row_commitments",
        "commitment_namespace_overlaps",
        "campaigns",
        "targets",
        "routes",
        "qualifying_routes",
        "route_counts",
        "positive_labels",
        "negative_labels",
        "groups",
        "overlapping_groups",
        "split_counts",
    }
    if not isinstance(value, Mapping):
        raise StudyError("a route training evaluation is invalid")
    _strict_keys(value, keys, "route training evaluation")
    if (
        value.get("schema_version") != SCHEMA_VERSION
        or value.get("kind") != "route-policy-training-gate"
        or value.get("status") not in {"passed", "failed"}
        or value.get("route_policy_only") is not True
        or value.get("training_performed") is not False
        or type(value.get("verified_evidence")) is not bool
        or value.get("linked_study_id") not in {study_id, None}
    ):
        raise StudyError("a route training evaluation identity is invalid")
    aggregate = value.get("aggregate")
    if not isinstance(aggregate, Mapping):
        raise StudyError("a route training aggregate is invalid")
    _strict_keys(aggregate, aggregate_keys, "route training aggregate")
    route_counts = aggregate.get("route_counts")
    split_counts = aggregate.get("split_counts")
    if not isinstance(route_counts, Mapping) or set(route_counts) != set(ROUTE_ORDER):
        raise StudyError("the route training counts are invalid")
    if not isinstance(split_counts, Mapping) or set(split_counts) != {"train", "test"}:
        raise StudyError("the route training split counts are invalid")
    scalar_values = [
        aggregate[key]
        for key in aggregate_keys - {"route_counts", "split_counts"}
    ] + list(route_counts.values()) + list(split_counts.values())
    if any(type(item) is not int or item < 0 for item in scalar_values):
        raise StudyError("a route training count is invalid")
    failures = value.get("failure_codes")
    if (
        not isinstance(failures, list)
        or len(failures) > 64
        or any(not isinstance(item, str) or not item for item in failures)
        or len(failures) != len(set(failures))
    ):
        raise StudyError("a route training failure list is invalid")
    if (value.get("status") == "passed") != (not failures):
        raise StudyError("a route training status disagrees with its failures")
    if (value.get("verified_evidence") is False) != (
        "unverified-acquisition" in failures
    ):
        raise StudyError("a route training evidence status is invalid")


def _validate_corpus_document(
    category: str,
    study_id: str,
    document: Mapping[str, object],
    *,
    preregistration: Mapping[str, object] | None,
) -> None:
    if document.get("study_id") != study_id or _content_id(document) != document.get(
        "content_sha256"
    ):
        raise StudyError("a private study corpus content ID is invalid")
    if category == OBSERVATIONS_NAME:
        if preregistration is None:
            raise StudyError("a stored study corpus has no bound preregistration")
        _strict_keys(document, STORED_OBSERVATION_KEYS, "stored study corpus")
        if (
            document.get("schema_version") != SCHEMA_VERSION
            or document.get("kind") != "option-study-corpus"
            or type(document.get("synthetic")) is not bool
            or document.get("acquisition") != "unverified-external"
            or not isinstance(document.get("registry_sha256"), str)
            or HASH_RE.fullmatch(str(document.get("registry_sha256"))) is None
            or not isinstance(document.get("preregistration_commit"), str)
            or COMMIT_RE.fullmatch(str(document.get("preregistration_commit"))) is None
        ):
            raise StudyError("a stored study corpus identity is invalid")
        recorded = _parse_time(document.get("recorded_at"), "study result time")
        rows = document.get("rows")
        if not isinstance(rows, list):
            raise StudyError("a stored study row list is invalid")
        public_rows = []
        for row in rows:
            if not isinstance(row, Mapping):
                raise StudyError("a stored study row is invalid")
            _strict_keys(row, STORED_OBSERVATION_ROW_KEYS, "stored study row")
            observed = _parse_time(row.get("observed_at"), "study observation time")
            if observed != recorded:
                raise StudyError("a study observation time is inconsistent")
            public_rows.append(
                {key: row[key] for key in OBSERVATION_ROW_KEYS}
            )
        validate_observations(
            {
                "schema_version": SCHEMA_VERSION,
                "kind": "option-study-observations",
                "synthetic": document["synthetic"],
                "rows": public_rows,
            },
            preregistration,
        )
        normalized = sorted(
            public_rows,
            key=_observation_sort_key,
        )
        if public_rows != normalized:
            raise StudyError("stored study rows are not in canonical order")
        return
    if category == TRAINING_NAME:
        _strict_keys(document, TRAINING_CORPUS_KEYS, "stored training corpus")
        if (
            document.get("schema_version") != SCHEMA_VERSION
            or document.get("kind") != "route-policy-training-corpus-envelope"
            or type(document.get("synthetic")) is not bool
            or document.get("acquisition") != "unverified-external"
        ):
            raise StudyError("a stored training corpus identity is invalid")
        _commitment(document.get("study_receipt_sha256"), "study receipt ID")
        _commitment(document.get("registry_sha256"), "training registry hash")
        recorded = _parse_time(document.get("recorded_at"), "training result time")
        rows = document.get("rows")
        if not isinstance(rows, list):
            raise StudyError("a stored training row list is invalid")
        public_rows = []
        for row in rows:
            if not isinstance(row, Mapping):
                raise StudyError("a stored training row is invalid")
            _strict_keys(row, STORED_TRAINING_ROW_KEYS, "stored training row")
            observed = _parse_time(row.get("observed_at"), "training observation time")
            if observed != recorded:
                raise StudyError("a training observation time is inconsistent")
            public_rows.append({key: row[key] for key in TRAINING_ROW_KEYS})
        validate_training_corpus(
            {
                "schema_version": SCHEMA_VERSION,
                "kind": "route-policy-training-corpus",
                "synthetic": document["synthetic"],
                "rows": public_rows,
            }
        )
        normalized = sorted(public_rows, key=_training_sort_key)
        if public_rows != normalized:
            raise StudyError("stored training rows are not in canonical order")
        return
    raise StudyError("a private study corpus category is invalid")


def _validate_receipt_document(category: str, value: Mapping[str, object]) -> None:
    if category == RECEIPTS_NAME:
        _strict_keys(value, STUDY_RECEIPT_KEYS, "study receipt")
        kind = "option-study-receipt"
    elif category == TRAINING_RECEIPTS_NAME:
        _strict_keys(value, TRAINING_RECEIPT_KEYS, "training gate receipt")
        kind = "route-policy-training-gate-receipt"
    else:
        raise StudyError("a private receipt category is invalid")
    study_id = _commitment(value.get("study_id"), "receipt study ID")
    if (
        value.get("schema_version") != SCHEMA_VERSION
        or value.get("kind") != kind
        or value.get("status") not in {"passed", "failed"}
        or _content_id(value) != value.get("content_sha256")
    ):
        raise StudyError("a study receipt identity is invalid")
    _parse_time(value.get("issued_at"), "study receipt time")
    _validate_repository_binding(value.get("repository"))
    manifest = _validate_storage_manifest(value.get("storage_manifest"))
    manifest_entry = manifest["entries"][0]
    if category == RECEIPTS_NAME:
        result = _validate_storage_entry(
            value.get("result"), "study result descriptor"
        )
        if result != manifest_entry:
            raise StudyError("a study receipt result descriptor is invalid")
        prereg = _validate_preregistration_binding(value.get("preregistration"))
        if prereg["study_id"] != study_id:
            raise StudyError("a study receipt preregistration is invalid")
        _validate_study_evaluation(value.get("evaluation"), study_id)
    else:
        linked = _validate_storage_entry(
            value.get("study_receipt"), "study receipt descriptor"
        )
        corpus = _validate_storage_entry(
            value.get("corpus"), "training corpus descriptor"
        )
        if corpus != manifest_entry or linked["name"] != (
            f"{linked['content_sha256']}.json"
        ):
            raise StudyError("a training gate receipt descriptor is invalid")
        _validate_training_evaluation(value.get("evaluation"), study_id)
    evaluation = value.get("evaluation")
    if not isinstance(evaluation, Mapping) or value.get("status") != evaluation.get(
        "status"
    ):
        raise StudyError("a study receipt status is invalid")


def _strict_keys(
    value: Mapping[str, object], keys: set[str], description: str
) -> None:
    if set(value) != keys:
        raise StudyError(f"the {description} keys are invalid")


def _commitment(value: object, description: str) -> str:
    if not isinstance(value, str) or HASH_RE.fullmatch(value) is None:
        raise StudyError(f"the {description} is invalid")
    return value


def _name(value: object, description: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) > MAX_TEXT
        or NAME_RE.fullmatch(value) is None
    ):
        raise StudyError(f"the {description} is invalid")
    return value


def _exact_int(value: object, description: str) -> int:
    if type(value) is not int:
        raise StudyError(f"the {description} is invalid")
    return value


def _fraction(value: Fraction) -> dict[str, int]:
    return {"numerator": value.numerator, "denominator": value.denominator}


def exact_sign_tail(wins: int, non_ties: int) -> Fraction:
    """Return the exact one-sided sign-test tail."""

    if (
        type(wins) is not int
        or type(non_ties) is not int
        or wins < 0
        or non_ties < 0
        or wins > non_ties
    ):
        raise StudyError("the sign-test counts are invalid")
    numerator = sum(comb(non_ties, index) for index in range(wins, non_ties + 1))
    return Fraction(numerator, 2**non_ties)


def _normalize_preregistration(value: object) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise StudyError("the study preregistration is invalid")
    baseline = value.get("baseline")
    options = baseline.get("options") if isinstance(baseline, Mapping) else None
    option_names = set(options) if isinstance(options, Mapping) else set()
    try:
        return validate_preregistration(value, option_names=option_names)
    except OptionError as error:
        raise StudyError(str(error)) from error


def validate_observations(
    document: object,
    preregistration: Mapping[str, object],
) -> list[dict[str, object]]:
    """Validate the exact row schema without selecting rows."""

    if not isinstance(document, Mapping):
        raise StudyError("the study observations must be a JSON object")
    _strict_keys(document, OBSERVATION_KEYS, "study observations")
    if (
        type(document.get("schema_version")) is not int
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("kind") != "option-study-observations"
        or type(document.get("synthetic")) is not bool
    ):
        raise StudyError("the study observation identity is invalid")
    rows = document.get("rows")
    if (
        not isinstance(rows, list)
        or not rows
        or len(rows) > MAX_OBSERVATION_ROWS
    ):
        raise StudyError("the study observation row list is invalid")

    primary = preregistration.get("primary_metric")
    protected_metrics = preregistration.get("protected_metrics")
    budget = preregistration.get("budget")
    if (
        not isinstance(primary, Mapping)
        or not isinstance(protected_metrics, list)
        or not isinstance(budget, Mapping)
    ):
        raise StudyError("the study preregistration is invalid")
    protected_names = {
        str(item["name"])
        for item in protected_metrics
        if isinstance(item, Mapping) and isinstance(item.get("name"), str)
    }
    if len(protected_names) != len(protected_metrics):
        raise StudyError("the protected metric declaration is invalid")
    budget_limit = budget.get("per_case_limit")
    if type(budget_limit) is not int or budget_limit <= 0:
        raise StudyError("the study budget is invalid")

    normalized: list[dict[str, object]] = []
    for value in rows:
        if not isinstance(value, Mapping):
            raise StudyError("a study observation row is invalid")
        _strict_keys(value, OBSERVATION_ROW_KEYS, "study observation row")
        status = value.get("status")
        if status not in {"ok", "missing", "error", "invalid"}:
            raise StudyError("a study observation status is invalid")
        used = _exact_int(value.get("budget_used"), "study budget use")
        if used < 0 or used > budget_limit:
            raise StudyError("a study budget use is out of range")
        raw_primary = value.get("primary")
        raw_protected = value.get("protected")
        if status == "ok":
            primary_value: int | None = _exact_int(
                raw_primary, "study primary value"
            )
            if not isinstance(raw_protected, Mapping):
                raise StudyError("a protected metric result is invalid")
            if set(raw_protected) != protected_names:
                raise StudyError("the protected metric result keys are invalid")
            protected_values: dict[str, int] | None = {
                name: _exact_int(
                    raw_protected[name], f"protected metric {name!r} value"
                )
                for name in sorted(protected_names)
            }
        else:
            if raw_primary is not None or raw_protected is not None:
                raise StudyError("a failed study row contains metric values")
            primary_value = None
            protected_values = None
        normalized.append(
            {
                "case_commitment": _commitment(
                    value.get("case_commitment"), "study case commitment"
                ),
                "campaign_commitment": _commitment(
                    value.get("campaign_commitment"),
                    "study campaign commitment",
                ),
                "target_commitment": _commitment(
                    value.get("target_commitment"), "study target commitment"
                ),
                "arm": _name(value.get("arm"), "study arm"),
                "status": status,
                "primary": primary_value,
                "protected": protected_values,
                "budget_used": used,
            }
        )
    return normalized


def _better(candidate: int, baseline: int, direction: object) -> int:
    if direction == "maximize":
        return (candidate > baseline) - (candidate < baseline)
    if direction == "minimize":
        return (candidate < baseline) - (candidate > baseline)
    raise StudyError("a metric direction is invalid")


def evaluate_study(
    preregistration: object,
    observations: object,
    *,
    verified_evidence: bool = False,
) -> dict[str, object]:
    """Evaluate one complete preregistered Cartesian population."""

    prereg = _normalize_preregistration(preregistration)
    rows = validate_observations(observations, prereg)
    assert isinstance(observations, Mapping)
    population = prereg["population"]
    treatments = prereg["treatments"]
    primary_metric = prereg["primary_metric"]
    protected_metrics = prereg["protected_metrics"]
    budget = prereg["budget"]
    assert isinstance(population, list)
    assert isinstance(treatments, list)
    assert isinstance(primary_metric, Mapping)
    assert isinstance(protected_metrics, list)
    assert isinstance(budget, Mapping)

    population_by_case = {
        str(item["case_commitment"]): item
        for item in population
        if isinstance(item, Mapping)
    }
    treatment_ids = [
        str(item["id"]) for item in treatments if isinstance(item, Mapping)
    ]
    arm_ids = {"off", *treatment_ids}
    expected = {
        (case_id, arm)
        for case_id in population_by_case
        for arm in arm_ids
    }
    cells: dict[tuple[str, str], dict[str, object]] = {}
    duplicates = 0
    extras = 0
    invalid_rows = 0
    budget_failures = 0
    for row in rows:
        key = (str(row["case_commitment"]), str(row["arm"]))
        population_row = population_by_case.get(key[0])
        if (
            key not in expected
            or not isinstance(population_row, Mapping)
            or row["campaign_commitment"]
            != population_row.get("campaign_commitment")
            or row["target_commitment"] != population_row.get("target_commitment")
        ):
            extras += 1
            continue
        if key in cells:
            duplicates += 1
            continue
        cells[key] = row
        if row["status"] != "ok":
            invalid_rows += 1
        if row["budget_used"] != budget["per_case_limit"]:
            budget_failures += 1
    missing = len(expected - set(cells))
    complete = not any(
        (missing, extras, duplicates, invalid_rows, budget_failures)
    )

    cases = len(population_by_case)
    campaigns = len(
        {
            item["campaign_commitment"]
            for item in population
            if isinstance(item, Mapping)
        }
    )
    targets = len(
        {
            item["target_commitment"]
            for item in population
            if isinstance(item, Mapping)
        }
    )
    multiplicity = len(treatment_ids)
    corrected_alpha = ALPHA / multiplicity
    treatment_results: list[dict[str, object]] = []
    primary_totals: dict[str, int] = {}
    minimums_pass = (
        cases >= MIN_CASES
        and campaigns >= MIN_CAMPAIGNS
        and targets >= MIN_TARGETS
    )

    for treatment_id in treatment_ids:
        wins = 0
        losses = 0
        ties = 0
        protected_regressions = 0
        primary_total = 0
        treatment_complete = complete
        for case_id in population_by_case:
            control = cells.get((case_id, "off"))
            treatment = cells.get((case_id, treatment_id))
            if (
                not isinstance(control, Mapping)
                or not isinstance(treatment, Mapping)
                or control.get("status") != "ok"
                or treatment.get("status") != "ok"
                or type(control.get("primary")) is not int
                or type(treatment.get("primary")) is not int
            ):
                treatment_complete = False
                continue
            comparison = _better(
                int(treatment["primary"]),
                int(control["primary"]),
                primary_metric.get("direction"),
            )
            if comparison > 0:
                wins += 1
            elif comparison < 0:
                losses += 1
            else:
                ties += 1
            primary_total += (
                int(treatment["primary"])
                if primary_metric.get("direction") == "maximize"
                else -int(treatment["primary"])
            )
            control_protected = control.get("protected")
            treatment_protected = treatment.get("protected")
            if not isinstance(control_protected, Mapping) or not isinstance(
                treatment_protected, Mapping
            ):
                treatment_complete = False
                continue
            for metric in protected_metrics:
                assert isinstance(metric, Mapping)
                name = str(metric["name"])
                if _better(
                    int(treatment_protected[name]),
                    int(control_protected[name]),
                    metric.get("direction"),
                ) < 0:
                    protected_regressions += 1
        non_ties = wins + losses
        probability = exact_sign_tail(wins, non_ties)
        passes = (
            treatment_complete
            and minimums_pass
            and non_ties >= MIN_NON_TIES
            and losses == 0
            and protected_regressions == 0
            and probability <= corrected_alpha
        )
        if treatment_complete:
            primary_totals[treatment_id] = primary_total
        treatment_results.append(
            {
                "treatment": treatment_id,
                "complete": treatment_complete,
                "wins": wins,
                "losses": losses,
                "ties": ties,
                "non_ties": non_ties,
                "protected_regressions": protected_regressions,
                "primary_aggregate": primary_total if treatment_complete else None,
                "sign_test_p": _fraction(probability),
                "bonferroni_alpha": _fraction(corrected_alpha),
                "passes": passes,
            }
        )

    unique_aggregate_winner: str | None = None
    if len(primary_totals) == multiplicity and primary_totals:
        best = max(primary_totals.values())
        winners = [
            treatment_id
            for treatment_id, value in primary_totals.items()
            if value == best
        ]
        if len(winners) == 1:
            unique_aggregate_winner = winners[0]
    passing = [
        str(item["treatment"])
        for item in treatment_results
        if item["passes"] is True
    ]

    failures: list[str] = []
    if cases < MIN_CASES:
        failures.append("minimum-cases")
    if campaigns < MIN_CAMPAIGNS:
        failures.append("minimum-campaigns")
    if targets < MIN_TARGETS:
        failures.append("minimum-targets")
    if missing:
        failures.append("missing-cells")
    if extras:
        failures.append("extra-cells")
    if duplicates:
        failures.append("duplicate-cells")
    if invalid_rows:
        failures.append("failed-or-invalid-rows")
    if budget_failures:
        failures.append("incomplete-budget")
    real_evidence = (
        verified_evidence is True
        and prereg["synthetic"] is False
        and observations.get("synthetic") is False
    )
    if prereg["synthetic"] is True or observations.get("synthetic") is True:
        failures.append("synthetic-evidence")
    if verified_evidence is not True:
        failures.append("unverified-acquisition")
    if unique_aggregate_winner is None:
        failures.append("no-unique-primary-aggregate")
    if len(passing) != 1:
        failures.append("passing-treatment-count")
    elif unique_aggregate_winner != passing[0]:
        failures.append("passing-treatment-is-not-aggregate-winner")

    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "option-study-evaluation",
        "status": "passed" if not failures else "failed",
        "study_id": prereg["study_id"],
        "real_evidence": real_evidence,
        "aggregate": {
            "cases": cases,
            "campaigns": campaigns,
            "targets": targets,
            "declared_treatments": multiplicity,
            "expected_cells": len(expected),
            "observed_rows": len(rows),
            "missing_cells": missing,
            "extra_cells": extras,
            "duplicate_cells": duplicates,
            "failed_or_invalid_rows": invalid_rows,
            "incomplete_budget_rows": budget_failures,
        },
        "treatments": treatment_results,
        "winner": unique_aggregate_winner if len(passing) == 1 else None,
        "failure_codes": failures,
    }


def validate_training_corpus(document: object) -> list[dict[str, object]]:
    """Validate route-policy rows against the canonical route labels."""

    if not isinstance(document, Mapping):
        raise StudyError("the route training corpus must be a JSON object")
    _strict_keys(document, TRAINING_KEYS, "route training corpus")
    if (
        type(document.get("schema_version")) is not int
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("kind") != "route-policy-training-corpus"
        or type(document.get("synthetic")) is not bool
    ):
        raise StudyError("the route training corpus identity is invalid")
    rows = document.get("rows")
    if (
        not isinstance(rows, list)
        or not rows
        or len(rows) > MAX_TRAINING_ROWS
    ):
        raise StudyError("the route training row list is invalid")
    allowed_routes = frozenset(ROUTE_ORDER)
    normalized: list[dict[str, object]] = []
    for value in rows:
        if not isinstance(value, Mapping):
            raise StudyError("a route training row is invalid")
        _strict_keys(value, TRAINING_ROW_KEYS, "route training row")
        route = _name(value.get("route"), "route training label")
        if route not in allowed_routes:
            raise StudyError("a route training label is not canonical")
        split = value.get("split")
        if split not in {"train", "test"}:
            raise StudyError("a route training split is invalid")
        if type(value.get("label")) is not bool or type(value.get("valid")) is not bool:
            raise StudyError("a route training Boolean value is invalid")
        normalized.append(
            {
                "row_commitment": _commitment(
                    value.get("row_commitment"), "route training row commitment"
                ),
                "campaign_commitment": _commitment(
                    value.get("campaign_commitment"),
                    "route training campaign commitment",
                ),
                "target_commitment": _commitment(
                    value.get("target_commitment"),
                    "route training target commitment",
                ),
                "route": route,
                "label": value["label"],
                "split": split,
                "valid": value["valid"],
            }
        )
    return normalized


def evaluate_training_gate(
    document: object,
    linked_study: object,
    *,
    verified_evidence: bool = False,
) -> dict[str, object]:
    """Evaluate data sufficiency without training a model."""

    rows = validate_training_corpus(document)
    if not isinstance(document, Mapping):
        raise StudyError("the route training corpus is invalid")
    if not isinstance(linked_study, Mapping):
        raise StudyError("the linked option study is invalid")
    linked_pass = (
        linked_study.get("kind") == "option-study-evaluation"
        and linked_study.get("status") == "passed"
        and linked_study.get("real_evidence") is True
        and isinstance(linked_study.get("study_id"), str)
        and HASH_RE.fullmatch(str(linked_study.get("study_id"))) is not None
    )

    valid_rows = [row for row in rows if row["valid"] is True]
    invalid_rows = len(rows) - len(valid_rows)
    row_ids = [str(row["row_commitment"]) for row in rows]
    duplicate_rows = len(row_ids) - len(set(row_ids))
    campaigns = {row["campaign_commitment"] for row in valid_rows}
    targets = {row["target_commitment"] for row in valid_rows}
    row_commitments = {row["row_commitment"] for row in valid_rows}
    namespace_overlaps = len(
        (row_commitments & campaigns)
        | (row_commitments & targets)
        | (campaigns & targets)
    )
    route_counts = {
        route: sum(row["route"] == route for row in valid_rows)
        for route in ROUTE_ORDER
    }
    qualifying_routes = sum(count >= 20 for count in route_counts.values())
    positive = sum(row["label"] is True for row in valid_rows)
    negative = sum(row["label"] is False for row in valid_rows)
    groups: dict[tuple[object, object], set[object]] = {}
    for row in valid_rows:
        group = (row["campaign_commitment"], row["target_commitment"])
        groups.setdefault(group, set()).add(row["split"])
    overlapping_groups = sum(len(splits) != 1 for splits in groups.values())
    split_counts = {
        split: sum(row["split"] == split for row in valid_rows)
        for split in ("train", "test")
    }

    failures: list[str] = []
    if len(valid_rows) < 200:
        failures.append("minimum-valid-rows")
    if len(campaigns) < 50:
        failures.append("minimum-campaigns")
    if len(targets) < 50:
        failures.append("minimum-targets")
    if qualifying_routes < 5:
        failures.append("minimum-qualified-routes")
    if positive < 50:
        failures.append("minimum-positive-labels")
    if negative < 50:
        failures.append("minimum-negative-labels")
    if invalid_rows:
        failures.append("invalid-rows")
    if duplicate_rows:
        failures.append("duplicate-row-commitments")
    if namespace_overlaps:
        failures.append("commitment-namespace-overlap")
    if overlapping_groups:
        failures.append("split-group-overlap")
    if not all(split_counts.values()):
        failures.append("empty-train-or-test-split")
    if document.get("synthetic") is True:
        failures.append("synthetic-evidence")
    if verified_evidence is not True:
        failures.append("unverified-acquisition")
    if not linked_pass:
        failures.append("linked-study-not-passing")

    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "route-policy-training-gate",
        "status": "passed" if not failures else "failed",
        "route_policy_only": True,
        "training_performed": False,
        "verified_evidence": verified_evidence is True,
        "linked_study_id": (
            linked_study.get("study_id") if linked_pass else None
        ),
        "aggregate": {
            "rows": len(rows),
            "valid_rows": len(valid_rows),
            "invalid_rows": invalid_rows,
            "duplicate_row_commitments": duplicate_rows,
            "commitment_namespace_overlaps": namespace_overlaps,
            "campaigns": len(campaigns),
            "targets": len(targets),
            "routes": len([count for count in route_counts.values() if count]),
            "qualifying_routes": qualifying_routes,
            "route_counts": route_counts,
            "positive_labels": positive,
            "negative_labels": negative,
            "groups": len(groups),
            "overlapping_groups": overlapping_groups,
            "split_counts": split_counts,
        },
        "failure_codes": failures,
    }


def list_studies(root: Path = ROOT) -> dict[str, object]:
    """List public declarations without opening private storage."""

    registry, content = read_registry(root)
    preregistrations = registry["preregistrations"]
    assert isinstance(preregistrations, list)
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "option-study-list",
        "status": "available" if preregistrations else "unavailable",
        "registry_sha256": hashlib.sha256(content).hexdigest(),
        "studies": [
            {
                "study_id": item["study_id"],
                "design": item["design"],
                "synthetic": item["synthetic"],
                "population_size": len(item["population"]),
                "declared_treatments": len(item["treatments"]),
            }
            for item in preregistrations
            if isinstance(item, Mapping)
        ],
    }


def _registered_study(
    study_id: str,
    root: Path,
) -> tuple[dict[str, object], bytes, dict[str, object]]:
    _commitment(study_id, "study ID")
    registry, content = read_registry(root)
    preregistration = find_preregistration(registry, study_id)
    if preregistration is None:
        raise StudyError("the study is not preregistered")
    return registry, content, preregistration


def _observation_document(corpus: Mapping[str, object]) -> dict[str, object]:
    rows = corpus.get("rows")
    if not isinstance(rows, list):
        raise StudyError("a stored study row list is invalid")
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "option-study-observations",
        "synthetic": corpus["synthetic"],
        "rows": [
            {key: row[key] for key in OBSERVATION_ROW_KEYS}
            for row in rows
            if isinstance(row, Mapping)
        ],
    }


def _training_document(corpus: Mapping[str, object]) -> dict[str, object]:
    rows = corpus.get("rows")
    if not isinstance(rows, list):
        raise StudyError("a stored training row list is invalid")
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "route-policy-training-corpus",
        "synthetic": corpus["synthetic"],
        "rows": [
            {key: row[key] for key in TRAINING_ROW_KEYS}
            for row in rows
            if isinstance(row, Mapping)
        ],
    }


def _corpus_times(corpus: Mapping[str, object]) -> list[datetime]:
    rows = corpus.get("rows")
    if not isinstance(rows, list):
        raise StudyError("a private study corpus has no rows")
    return [
        _parse_time(row.get("observed_at"), "study observation time")
        for row in rows
        if isinstance(row, Mapping)
    ]


def _result_descriptor(
    manifest: Mapping[str, object],
) -> dict[str, object]:
    entries = manifest.get("entries")
    if not isinstance(entries, list) or len(entries) != 1:
        raise StudyError("a private study storage manifest is invalid")
    entry = entries[0]
    if not isinstance(entry, Mapping):
        raise StudyError("a private study storage entry is invalid")
    return dict(entry)


def _observation_sort_key(row: Mapping[str, object]) -> tuple[str, str, bytes]:
    return (
        str(row["case_commitment"]),
        str(row["arm"]),
        canonical_json(row),
    )


def _training_sort_key(row: Mapping[str, object]) -> tuple[str, bytes]:
    return str(row["row_commitment"]), canonical_json(row)


def _stored_corpus_candidate_locked(
    study_id: str,
    *,
    root: Path,
    category: str,
    preregistration: Mapping[str, object] | None,
) -> tuple[dict[str, object], bytes] | None:
    """Read a complete corpus or its one recognized crash temporary."""

    temporary = f".{CORPUS_FILE_NAME}.tmp"
    try:
        with _study_directory(
            root, (category, study_id), create=True
        ) as directory_fd:
            names = _directory_names(directory_fd)
            if names - {CORPUS_FILE_NAME, temporary}:
                raise StudyError("a private study corpus directory has extra entries")
            if not names:
                return None
            selected = CORPUS_FILE_NAME if CORPUS_FILE_NAME in names else temporary
            if selected == temporary and names != {temporary}:
                raise StudyError("a private study corpus directory is invalid")
            content = replay._read_at(
                directory_fd,
                selected,
                MAX_CORPUS_BYTES,
                "private study corpus",
            )
            if names == {CORPUS_FILE_NAME, temporary}:
                pending = replay._read_at(
                    directory_fd,
                    temporary,
                    MAX_CORPUS_BYTES,
                    "private study temporary corpus",
                )
                if pending != content:
                    raise StudyError(
                        "a private study temporary file has unknown content"
                    )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    document = _strict_json(content, "private study corpus")
    if content != _encoded_document(document):
        raise StudyError("the private study corpus encoding is not canonical")
    _validate_corpus_document(
        category,
        study_id,
        document,
        preregistration=preregistration,
    )
    _descriptor(content, document)
    return document, content


def _matches_observation_request(
    corpus: Mapping[str, object],
    *,
    preregistration: Mapping[str, object],
    study_id: str,
    introduction_commit: str,
    registry_sha256: str,
    synthetic: bool,
    rows: Sequence[Mapping[str, object]],
) -> bool:
    saved_rows = sorted(
        validate_observations(_observation_document(corpus), preregistration),
        key=_observation_sort_key,
    )
    return (
        corpus.get("study_id") == study_id
        and corpus.get("preregistration_commit") == introduction_commit
        and corpus.get("registry_sha256") == registry_sha256
        and corpus.get("acquisition") == "unverified-external"
        and corpus.get("synthetic") is synthetic
        and saved_rows == list(rows)
    )


def ingest_observations(
    study_id: str,
    introduction_commit: str,
    observations: object,
    *,
    root: Path = ROOT,
    clock: Callable[[], datetime] = utc_now,
) -> dict[str, object]:
    """Store one complete immutable corpus in its preregistered slot."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    normalized_rows = sorted(
        validate_observations(observations, preregistration),
        key=_observation_sort_key,
    )
    assert isinstance(observations, Mapping)
    repository = _repository_binding(root, registry_content)
    registry_sha256 = hashlib.sha256(registry_content).hexdigest()
    try:
        with replay._suite_lock(root, create=True, exclusive=True):
            current_repository = _repository_binding(root, registry_content)
            if current_repository != repository:
                raise StudyError("the study repository changed before publication")
            existing = _stored_corpus_candidate_locked(
                study_id,
                root=root,
                category=OBSERVATIONS_NAME,
                preregistration=preregistration,
            )
            if existing is not None:
                corpus, content = existing
                if not _matches_observation_request(
                    corpus,
                    preregistration=preregistration,
                    study_id=study_id,
                    introduction_commit=introduction_commit,
                    registry_sha256=registry_sha256,
                    synthetic=observations["synthetic"] is True,
                    rows=normalized_rows,
                ):
                    raise StudyError(
                        "the private study corpus has other logical content"
                    )
                times = _corpus_times(corpus)
                preregistration_binding = _preregistration_binding(
                    root,
                    preregistration,
                    introduction_commit,
                    times,
                    head=str(repository["head"]),
                    origin=str(repository["origin_head"]),
                )
            else:
                recorded = _clock_time(clock, "study result time")
                recorded_text = _canonical_time(recorded)
                preregistration_binding = _preregistration_binding(
                    root,
                    preregistration,
                    introduction_commit,
                    [recorded for _ in normalized_rows],
                    head=str(repository["head"]),
                    origin=str(repository["origin_head"]),
                )
                corpus = {
                    "schema_version": SCHEMA_VERSION,
                    "kind": "option-study-corpus",
                    "study_id": study_id,
                    "synthetic": observations["synthetic"],
                    "recorded_at": recorded_text,
                    "preregistration_commit": introduction_commit,
                    "registry_sha256": registry_sha256,
                    "acquisition": "unverified-external",
                    "rows": [
                        {**row, "observed_at": recorded_text}
                        for row in normalized_rows
                    ],
                }
                corpus["content_sha256"] = _content_id(corpus)
                _validate_corpus_document(
                    OBSERVATIONS_NAME,
                    study_id,
                    corpus,
                    preregistration=preregistration,
                )
                content = _encoded_document(corpus)
            _publish_corpus_locked(
                root,
                OBSERVATIONS_NAME,
                study_id,
                content,
                preregistration=preregistration,
            )
            saved, saved_content, manifest = _read_corpus_locked(
                root,
                OBSERVATIONS_NAME,
                study_id,
                preregistration=preregistration,
            )
            if saved != corpus or saved_content != content:
                raise StudyError("the stored study corpus changed during publication")
            if _repository_binding(root, registry_content) != repository:
                raise StudyError("the study repository changed during publication")
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "option-study-corpus-commitment",
        "status": "stored",
        "study_id": study_id,
        "result": _result_descriptor(manifest),
        "preregistration": preregistration_binding,
    }


def _evaluate_stored_locked(
    study_id: str,
    *,
    root: Path,
    registry_content: bytes,
    preregistration: Mapping[str, object],
) -> tuple[
    dict[str, object],
    bytes,
    dict[str, object],
    dict[str, object],
    dict[str, object],
    dict[str, object],
]:
    repository = _repository_binding(root, registry_content)
    corpus, content, manifest = _read_corpus_locked(
        root,
        OBSERVATIONS_NAME,
        study_id,
        preregistration=preregistration,
    )
    if corpus.get("registry_sha256") != repository["registry_sha256"]:
        raise StudyError("the stored study corpus uses another registry")
    times = _corpus_times(corpus)
    preregistration_binding = _preregistration_binding(
        root,
        preregistration,
        str(corpus["preregistration_commit"]),
        times,
        head=str(repository["head"]),
        origin=str(repository["origin_head"]),
    )
    evaluation = evaluate_study(preregistration, _observation_document(corpus))
    if _repository_binding(root, registry_content) != repository:
        raise StudyError("the study repository changed during evaluation")
    return (
        corpus,
        content,
        manifest,
        preregistration_binding,
        repository,
        evaluation,
    )


def evaluate_registered_study(
    study_id: str,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Evaluate the only stored corpus for one registered study."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    try:
        with replay._suite_lock(root, create=False, exclusive=False):
            *_, evaluation = _evaluate_stored_locked(
                study_id,
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return evaluation


def _receipt_file_descriptor(
    receipt_id: str,
    content: bytes,
) -> dict[str, object]:
    return {
        "name": f"{receipt_id}.json",
        "bytes": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
        "content_sha256": receipt_id,
    }


def _public_study_receipt(
    receipt: Mapping[str, object],
    content: bytes,
) -> dict[str, object]:
    evaluation = receipt.get("evaluation")
    if not isinstance(evaluation, Mapping):
        raise StudyError("the study receipt has no evaluation")
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "option-study-receipt-summary",
        "status": receipt["status"],
        "certified": receipt["status"] == "passed",
        "study_id": receipt["study_id"],
        "receipt": _receipt_file_descriptor(
            str(receipt["content_sha256"]), content
        ),
        "result": receipt["result"],
        "aggregate": evaluation["aggregate"],
        "treatments": evaluation["treatments"],
        "winner": evaluation["winner"],
        "failure_codes": evaluation["failure_codes"],
    }


def certify_study(
    study_id: str,
    *,
    root: Path = ROOT,
    clock: Callable[[], datetime] = utc_now,
) -> dict[str, object]:
    """Publish one content-addressed aggregate study receipt."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    try:
        with replay._suite_lock(root, create=False, exclusive=True):
            (
                corpus,
                _,
                manifest,
                preregistration_binding,
                repository,
                evaluation,
            ) = _evaluate_stored_locked(
                study_id,
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
            issued = _clock_time(clock, "study receipt time")
            times = _corpus_times(corpus)
            if not times or any(value > issued for value in times):
                raise StudyError("the study receipt predates an observation")
            receipt: dict[str, object] = {
                "schema_version": SCHEMA_VERSION,
                "kind": "option-study-receipt",
                "status": evaluation["status"],
                "study_id": study_id,
                "issued_at": _canonical_time(issued),
                "result": _result_descriptor(manifest),
                "preregistration": preregistration_binding,
                "repository": repository,
                "storage_manifest": manifest,
                "evaluation": evaluation,
            }
            receipt["content_sha256"] = _content_id(receipt)
            _validate_receipt_document(RECEIPTS_NAME, receipt)
            _, receipt_content = _publish_receipt_locked(
                root, RECEIPTS_NAME, receipt
            )
            verified, verified_content = _verify_study_receipt_locked(
                str(receipt["content_sha256"]),
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
            if verified != receipt or verified_content != receipt_content:
                raise StudyError("the study receipt changed during publication")
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return _public_study_receipt(receipt, receipt_content)


def _verify_study_receipt_locked(
    receipt_id: str,
    *,
    root: Path,
    registry_content: bytes,
    preregistration: Mapping[str, object],
) -> tuple[dict[str, object], bytes]:
    receipt, receipt_content = _read_receipt_locked(
        root, RECEIPTS_NAME, receipt_id
    )
    study_id = str(preregistration["study_id"])
    if receipt.get("study_id") != study_id:
        raise StudyError("the study receipt names another preregistration")
    (
        corpus,
        _,
        manifest,
        preregistration_binding,
        repository,
        evaluation,
    ) = _evaluate_stored_locked(
        study_id,
        root=root,
        registry_content=registry_content,
        preregistration=preregistration,
    )
    issued_at = _parse_time(receipt.get("issued_at"), "study receipt time")
    if any(value > issued_at for value in _corpus_times(corpus)):
        raise StudyError("the study receipt predates an observation")
    expected = {
        "schema_version": SCHEMA_VERSION,
        "kind": "option-study-receipt",
        "status": evaluation["status"],
        "study_id": study_id,
        "issued_at": receipt["issued_at"],
        "result": _result_descriptor(manifest),
        "preregistration": preregistration_binding,
        "repository": repository,
        "storage_manifest": manifest,
        "evaluation": evaluation,
    }
    expected["content_sha256"] = _content_id(expected)
    if receipt != expected or receipt_id != expected["content_sha256"]:
        raise StudyError("the study receipt is stale or invalid")
    return receipt, receipt_content


def verify_study_receipt(
    study_id: str,
    receipt_id: str,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Revalidate one immutable receipt against all current inputs."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    try:
        with replay._suite_lock(root, create=False, exclusive=False):
            receipt, content = _verify_study_receipt_locked(
                receipt_id,
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return _public_study_receipt(receipt, content)


def _matches_training_request(
    corpus: Mapping[str, object],
    *,
    study_id: str,
    study_receipt_id: str,
    registry_sha256: str,
    synthetic: bool,
    rows: Sequence[Mapping[str, object]],
) -> bool:
    saved_rows = sorted(
        validate_training_corpus(_training_document(corpus)),
        key=_training_sort_key,
    )
    return (
        corpus.get("study_id") == study_id
        and corpus.get("study_receipt_sha256") == study_receipt_id
        and corpus.get("registry_sha256") == registry_sha256
        and corpus.get("acquisition") == "unverified-external"
        and corpus.get("synthetic") is synthetic
        and saved_rows == list(rows)
    )


def _require_training_chronology(
    corpus: Mapping[str, object], study_receipt: Mapping[str, object]
) -> list[datetime]:
    times = _corpus_times(corpus)
    issued = _parse_time(study_receipt.get("issued_at"), "linked study receipt time")
    if not times or any(value < issued for value in times):
        raise StudyError("training observations must follow the linked study receipt")
    return times


def ingest_training_corpus(
    study_id: str,
    study_receipt_id: str,
    training_corpus: object,
    *,
    root: Path = ROOT,
    clock: Callable[[], datetime] = utc_now,
) -> dict[str, object]:
    """Store one immutable, unverified route-policy corpus."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    rows = sorted(validate_training_corpus(training_corpus), key=_training_sort_key)
    assert isinstance(training_corpus, Mapping)
    repository = _repository_binding(root, registry_content)
    registry_sha256 = hashlib.sha256(registry_content).hexdigest()
    try:
        with replay._suite_lock(root, create=False, exclusive=True):
            if _repository_binding(root, registry_content) != repository:
                raise StudyError("the study repository changed before publication")
            study_receipt, study_receipt_content = _verify_study_receipt_locked(
                study_receipt_id,
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
            existing = _stored_corpus_candidate_locked(
                study_id,
                root=root,
                category=TRAINING_NAME,
                preregistration=None,
            )
            if existing is not None:
                corpus, content = existing
                if not _matches_training_request(
                    corpus,
                    study_id=study_id,
                    study_receipt_id=study_receipt_id,
                    registry_sha256=registry_sha256,
                    synthetic=training_corpus["synthetic"] is True,
                    rows=rows,
                ):
                    raise StudyError(
                        "the private training corpus has other logical content"
                    )
                _require_training_chronology(corpus, study_receipt)
            else:
                recorded = _clock_time(clock, "training result time")
                receipt_time = _parse_time(
                    study_receipt.get("issued_at"), "linked study receipt time"
                )
                if recorded < receipt_time:
                    raise StudyError(
                        "training observations must follow the linked study receipt"
                    )
                recorded_text = _canonical_time(recorded)
                corpus = {
                    "schema_version": SCHEMA_VERSION,
                    "kind": "route-policy-training-corpus-envelope",
                    "study_id": study_id,
                    "study_receipt_sha256": study_receipt_id,
                    "synthetic": training_corpus["synthetic"],
                    "recorded_at": recorded_text,
                    "registry_sha256": registry_sha256,
                    "acquisition": "unverified-external",
                    "rows": [
                        {**row, "observed_at": recorded_text} for row in rows
                    ],
                }
                corpus["content_sha256"] = _content_id(corpus)
                _validate_corpus_document(
                    TRAINING_NAME,
                    study_id,
                    corpus,
                    preregistration=None,
                )
                content = _encoded_document(corpus)
            _publish_corpus_locked(
                root,
                TRAINING_NAME,
                study_id,
                content,
                preregistration=None,
            )
            saved, saved_content, manifest = _read_corpus_locked(
                root,
                TRAINING_NAME,
                study_id,
                preregistration=None,
            )
            if saved != corpus or saved_content != content:
                raise StudyError("the stored training corpus changed during publication")
            if _repository_binding(root, registry_content) != repository:
                raise StudyError("the study repository changed during publication")
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "route-policy-training-corpus-commitment",
        "status": "stored",
        "study_id": study_id,
        "study_receipt": _receipt_file_descriptor(
            study_receipt_id, study_receipt_content
        ),
        "result": _result_descriptor(manifest),
    }


def _evaluate_training_stored_locked(
    study_id: str,
    *,
    root: Path,
    registry_content: bytes,
    preregistration: Mapping[str, object],
) -> tuple[
    dict[str, object],
    bytes,
    dict[str, object],
    dict[str, object],
    bytes,
    dict[str, object],
    dict[str, object],
]:
    repository = _repository_binding(root, registry_content)
    corpus, content, manifest = _read_corpus_locked(
        root,
        TRAINING_NAME,
        study_id,
        preregistration=None,
    )
    if corpus.get("registry_sha256") != repository["registry_sha256"]:
        raise StudyError("the stored training corpus uses another registry")
    receipt_id = _commitment(
        corpus.get("study_receipt_sha256"), "linked study receipt ID"
    )
    study_receipt, study_receipt_content = _verify_study_receipt_locked(
        receipt_id,
        root=root,
        registry_content=registry_content,
        preregistration=preregistration,
    )
    _require_training_chronology(corpus, study_receipt)
    evaluation = evaluate_training_gate(
        _training_document(corpus), study_receipt["evaluation"]
    )
    if _repository_binding(root, registry_content) != repository:
        raise StudyError("the study repository changed during training evaluation")
    return (
        corpus,
        content,
        manifest,
        study_receipt,
        study_receipt_content,
        repository,
        evaluation,
    )


def evaluate_registered_training_gate(
    study_id: str,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Evaluate the only private route-policy corpus for one study."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    try:
        with replay._suite_lock(root, create=False, exclusive=False):
            *_, evaluation = _evaluate_training_stored_locked(
                study_id,
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return evaluation


def _public_training_receipt(
    receipt: Mapping[str, object], content: bytes
) -> dict[str, object]:
    evaluation = receipt.get("evaluation")
    if not isinstance(evaluation, Mapping):
        raise StudyError("the training gate receipt has no evaluation")
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "route-policy-training-gate-receipt-summary",
        "status": receipt["status"],
        "certified": receipt["status"] == "passed",
        "training_performed": False,
        "study_id": receipt["study_id"],
        "receipt": _receipt_file_descriptor(
            str(receipt["content_sha256"]), content
        ),
        "study_receipt": receipt["study_receipt"],
        "corpus": receipt["corpus"],
        "aggregate": evaluation["aggregate"],
        "failure_codes": evaluation["failure_codes"],
    }


def certify_training_gate(
    study_id: str,
    *,
    root: Path = ROOT,
    clock: Callable[[], datetime] = utc_now,
) -> dict[str, object]:
    """Publish one aggregate-only, content-addressed training gate receipt."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    try:
        with replay._suite_lock(root, create=False, exclusive=True):
            (
                corpus,
                _,
                manifest,
                study_receipt,
                study_receipt_content,
                repository,
                evaluation,
            ) = _evaluate_training_stored_locked(
                study_id,
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
            issued = _clock_time(clock, "training gate receipt time")
            times = _corpus_times(corpus)
            if not times or any(value > issued for value in times):
                raise StudyError("the training gate receipt predates an observation")
            receipt: dict[str, object] = {
                "schema_version": SCHEMA_VERSION,
                "kind": "route-policy-training-gate-receipt",
                "status": evaluation["status"],
                "study_id": study_id,
                "issued_at": _canonical_time(issued),
                "study_receipt": _receipt_file_descriptor(
                    str(study_receipt["content_sha256"]), study_receipt_content
                ),
                "corpus": _result_descriptor(manifest),
                "repository": repository,
                "storage_manifest": manifest,
                "evaluation": evaluation,
            }
            receipt["content_sha256"] = _content_id(receipt)
            _validate_receipt_document(TRAINING_RECEIPTS_NAME, receipt)
            _, receipt_content = _publish_receipt_locked(
                root, TRAINING_RECEIPTS_NAME, receipt
            )
            verified, verified_content = _verify_training_receipt_locked(
                str(receipt["content_sha256"]),
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
            if verified != receipt or verified_content != receipt_content:
                raise StudyError("the training gate receipt changed during publication")
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return _public_training_receipt(receipt, receipt_content)


def _verify_training_receipt_locked(
    receipt_id: str,
    *,
    root: Path,
    registry_content: bytes,
    preregistration: Mapping[str, object],
) -> tuple[dict[str, object], bytes]:
    receipt, receipt_content = _read_receipt_locked(
        root, TRAINING_RECEIPTS_NAME, receipt_id
    )
    study_id = str(preregistration["study_id"])
    if receipt.get("study_id") != study_id:
        raise StudyError("the training gate receipt names another study")
    (
        corpus,
        _,
        manifest,
        study_receipt,
        study_receipt_content,
        repository,
        evaluation,
    ) = _evaluate_training_stored_locked(
        study_id,
        root=root,
        registry_content=registry_content,
        preregistration=preregistration,
    )
    issued = _parse_time(receipt.get("issued_at"), "training gate receipt time")
    if any(value > issued for value in _corpus_times(corpus)):
        raise StudyError("the training gate receipt predates an observation")
    expected: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": "route-policy-training-gate-receipt",
        "status": evaluation["status"],
        "study_id": study_id,
        "issued_at": receipt["issued_at"],
        "study_receipt": _receipt_file_descriptor(
            str(study_receipt["content_sha256"]), study_receipt_content
        ),
        "corpus": _result_descriptor(manifest),
        "repository": repository,
        "storage_manifest": manifest,
        "evaluation": evaluation,
    }
    expected["content_sha256"] = _content_id(expected)
    if receipt != expected or receipt_id != expected["content_sha256"]:
        raise StudyError("the training gate receipt is stale or invalid")
    return receipt, receipt_content


def verify_training_gate_receipt(
    study_id: str,
    receipt_id: str,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Revalidate one private training gate receipt against current inputs."""

    root = root.resolve()
    _, registry_content, preregistration = _registered_study(study_id, root)
    try:
        with replay._suite_lock(root, create=False, exclusive=False):
            receipt, content = _verify_training_receipt_locked(
                receipt_id,
                root=root,
                registry_content=registry_content,
                preregistration=preregistration,
            )
    except replay.ReplayError as error:
        raise StudyError(str(error)) from error
    return _public_training_receipt(receipt, content)


def _read_cli_document(path: Path, description: str) -> dict[str, object]:
    """Read one owner-only input without following any path component."""

    absolute = Path(os.path.abspath(path))
    directory_fd = -1
    try:
        directory_fd = replay._open_absolute_directory(absolute.parent)
        content, _ = replay._read_private_file_at(
            directory_fd,
            absolute.name,
            MAX_CORPUS_BYTES,
            description,
        )
    except replay.ReplayError as error:
        raise StudyError(f"the {description} is missing or invalid") from error
    finally:
        if directory_fd >= 0:
            os.close(directory_fd)
    return _strict_json(content, description)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="list public preregistration summaries")

    ingest = commands.add_parser(
        "ingest", help="store one unverified external observation corpus"
    )
    ingest.add_argument("study_id")
    ingest.add_argument("--preregistration-commit", required=True)
    ingest.add_argument("--input", required=True, type=Path)

    evaluate = commands.add_parser(
        "evaluate", help="evaluate the stored observation corpus"
    )
    evaluate.add_argument("study_id")
    evaluate.add_argument("--require-pass", action="store_true")

    certify = commands.add_parser(
        "certify", help="publish a failed aggregate receipt for unverified input"
    )
    certify.add_argument("study_id")
    certify.add_argument("--require-pass", action="store_true")

    verify = commands.add_parser(
        "verify", help="revalidate one content-addressed study receipt"
    )
    verify.add_argument("study_id")
    verify.add_argument("receipt_id")
    verify.add_argument("--require-pass", action="store_true")

    training_ingest = commands.add_parser(
        "training-ingest", help="store one unverified external route corpus"
    )
    training_ingest.add_argument("study_id")
    training_ingest.add_argument("--study-receipt", required=True)
    training_ingest.add_argument("--input", required=True, type=Path)

    training_evaluate = commands.add_parser(
        "training-evaluate", help="evaluate the stored route-policy corpus"
    )
    training_evaluate.add_argument("study_id")
    training_evaluate.add_argument("--require-pass", action="store_true")

    training_certify = commands.add_parser(
        "training-certify",
        help="publish a failed route data-gate receipt for unverified input",
    )
    training_certify.add_argument("study_id")
    training_certify.add_argument("--require-pass", action="store_true")

    training_verify = commands.add_parser(
        "training-verify", help="revalidate one route data-gate receipt"
    )
    training_verify.add_argument("study_id")
    training_verify.add_argument("receipt_id")
    training_verify.add_argument("--require-pass", action="store_true")
    return parser


def _json_output(document: Mapping[str, object], *, stream: object | None = None) -> None:
    print(
        json.dumps(document, indent=2, sort_keys=True),
        file=sys.stdout if stream is None else stream,
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "list":
            document = list_studies(ROOT)
        elif args.command == "ingest":
            observations = _read_cli_document(
                args.input, "private study observation input"
            )
            document = ingest_observations(
                args.study_id,
                args.preregistration_commit,
                observations,
                root=ROOT,
            )
        elif args.command == "evaluate":
            document = evaluate_registered_study(args.study_id, root=ROOT)
        elif args.command == "certify":
            document = certify_study(args.study_id, root=ROOT)
        elif args.command == "verify":
            document = verify_study_receipt(
                args.study_id, args.receipt_id, root=ROOT
            )
        elif args.command == "training-ingest":
            training = _read_cli_document(args.input, "private route corpus input")
            document = ingest_training_corpus(
                args.study_id,
                args.study_receipt,
                training,
                root=ROOT,
            )
        elif args.command == "training-evaluate":
            document = evaluate_registered_training_gate(
                args.study_id, root=ROOT
            )
        elif args.command == "training-certify":
            document = certify_training_gate(args.study_id, root=ROOT)
        elif args.command == "training-verify":
            document = verify_training_gate_receipt(
                args.study_id, args.receipt_id, root=ROOT
            )
        else:
            raise StudyError("the study command is invalid")
        _json_output(document)
        if getattr(args, "require_pass", False) and document.get("status") != "passed":
            return 1
        return 0
    except (OptionError, StudyError) as error:
        _json_output(
            {
                "schema_version": SCHEMA_VERSION,
                "kind": "decomp-study-error",
                "status": "failed",
                "error": str(error),
            },
            stream=sys.stderr,
        )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
