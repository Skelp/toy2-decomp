#!/usr/bin/env python3
"""Manage the private source-experiment replay benchmark."""

from __future__ import annotations

import argparse
import ctypes
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
import hashlib
import errno
import io
import json
import os
from pathlib import Path, PureWindowsPath
import re
import secrets
import stat
import subprocess
import sys
import tempfile
from typing import Callable, Iterator, Mapping, Sequence
import unicodedata
import uuid

try:
    import fcntl
except ImportError:  # pragma: no cover - replay storage fails closed on Windows.
    fcntl = None  # type: ignore[assignment]

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_mismatch import ROUTE_ORDER  # noqa: E402
from tools.decomp_quality import (  # noqa: E402
    QUALITY_SCORE_SCALE,
    QUALITY_STATUS_STRIDE,
    STATUS_LEVELS,
    quality as _shared_quality,
    quality_from_score as _shared_quality_from_score,
)


SCHEMA_VERSION = 1
POLICY_VERSION = "fixed-incumbent-frontier-v1"
MANIFEST_RELATIVE = Path("tools/Resources/private-replay-manifest.json")
LEDGER_RELATIVE = Path("tools/Resources/campaign-ledger.jsonl")
PRIVATE_RELATIVE = Path(".decomp-replay")
CASES_NAME = "cases"
TRAJECTORIES_NAME = "trajectories"
CERTIFICATES_NAME = "certificates"
CERTIFICATE_POINTER_NAME = "current-certificate.json"
ENROLLMENT_JOURNAL_NAME = "enrollment-transaction.json"

CASE_ID_RE = re.compile(r"[0-9a-f]{32}\Z")
HASH_RE = re.compile(r"[0-9a-f]{64}\Z")
COMMIT_RE = re.compile(r"[0-9a-f]{40}\Z")
SESSION_RE = re.compile(r"[0-9]{8}T[0-9]{6}Z-[0-9a-f]{12}\Z")

MAX_MANIFEST_BYTES = 1024 * 1024
MAX_CASE_BYTES = 1024 * 1024
MAX_TRAJECTORY_BYTES = 16 * 1024 * 1024
MAX_CERTIFICATE_BYTES = 4 * 1024 * 1024
MAX_LEDGER_BYTES = 64 * 1024 * 1024
MAX_LEDGER_RECORDS = 65_536
MAX_TOOL_BYTES = 8 * 1024 * 1024
MAX_TOOL_LIST_BYTES = 2 * 1024 * 1024
MAX_BRIEF_BYTES = 16 * 1024 * 1024
MAX_CASES = 4096
MAX_TEXT = 240
MAX_TRIALS = 3
MAX_NON_IMPROVING = 2

TERMINAL_LEVEL = STATUS_LEVELS["effective"]

THRESHOLDS = {
    "cases": 12,
    "campaigns": 12,
    "targets": 12,
    "subsystems": 4,
    "routes": 4,
    "edges": 24,
    "terminal_oracle_cases": 3,
    "capture_basis_points": 9000,
}
ELIGIBLE_LANE_MODES = frozenset(
    {
        ("research", "coverage"),
        ("research", "refinement"),
        ("closure", "refinement"),
        ("production", "refinement"),
    }
)
SOURCE_IMPACT_CLASSIFICATIONS = {
    "coverage": "added",
    "refinement": "improved",
}

# This list is a security boundary. Keep it explicit so that deleting a tracked
# dependency cannot silently remove that dependency from a certificate.
TRUSTED_INPUTS = (
    ".gitignore",
    ".notes/lint-baseline.tsv",
    "reccmp-project.yml",
    "tools/__init__.py",
    "tools/decomp",
    "tools/decomp.ps1",
    "tools/decomp_annotations.py",
    "tools/decomp_binary.py",
    "tools/decomp_brief.py",
    "tools/decomp_campaigns.py",
    "tools/decomp_candidates.py",
    "tools/decomp_context.py",
    "tools/decomp_dependencies.py",
    "tools/decomp_diff.py",
    "tools/decomp_doctor.py",
    "tools/decomp_evidence.py",
    "tools/decomp_experiment.py",
    "tools/decomp_impact.py",
    "tools/decomp_lint.py",
    "tools/decomp_mismatch.py",
    "tools/decomp_oracle.py",
    "tools/decomp_provenance.py",
    "tools/decomp_quality.py",
    "tools/decomp_replay.py",
    "tools/decomp_resources.py",
    "tools/decomp_route.py",
    "tools/decomp_status.py",
    "tools/decomp_verify.py",
    "tools/generate-decomp-data-report.py",
    "tools/ghidra_sync.py",
    "tools/Resources/functions_map.txt",
    "tools/Resources/leaf-oracles.json",
    "tools/Resources/reconstruction-blockers.tsv",
    "tools/Resources/tool_artifacts.tsv",
)

HISTORICAL_VALIDATION_IDENTITY_FILES = (
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
HISTORICAL_VALIDATION_FILES = tuple(
    name
    for name in HISTORICAL_VALIDATION_IDENTITY_FILES
    if name != "reccmp-user.yml"
)

HISTORICAL_FINALIZE_FILES = (
    "tools/__init__.py",
    "tools/decomp_campaigns.py",
    "tools/decomp_experiment.py",
    "tools/decomp_quality.py",
    "tools/decomp_replay.py",
    "tools/decomp_route.py",
    "tools/decomp_mismatch.py",
    "tools/decomp_impact.py",
    "tools/decomp_oracle.py",
    "tools/decomp_annotations.py",
    "tools/decomp_dependencies.py",
    "tools/decomp_binary.py",
    "tools/decomp_provenance.py",
    "tools/decomp_verify.py",
    "tools/decomp_lint.py",
    "tools/generate-decomp-data-report.py",
    "tools/Resources/functions_map.txt",
    "tools/Resources/leaf-oracles.json",
)

MANIFEST_KEYS = {"schema_version", "kind", "cases"}
MANIFEST_CASE_KEYS = {"case_id", "bytes", "sha256"}
CASE_KEYS = {
    "schema_version",
    "kind",
    "case_id",
    "provenance",
    "classification",
    "budget",
    "graph",
}
PROVENANCE_KEYS = {
    "target",
    "session_id",
    "session_receipt_id",
    "campaign_id",
    "campaign_record_sha256",
    "delivery_receipt_sha256",
    "source_commit",
    "context_pack_sha256",
    "impact_pack_sha256",
    "leaf_oracle_sha256",
    "trajectory_sha256",
}
CLASSIFICATION_KEYS = {"subsystem"}
BUDGET_KEYS = {"max_trials", "max_non_improving"}
GRAPH_KEYS = {"nodes", "edges"}
NODE_KEYS = {"status_level", "score_millionths", "eligible", "routes"}
EDGE_KEYS = {"sequence", "from", "to", "route"}
DELIVERY_ROW_KEYS = {
    "schema_version",
    "record_type",
    "delivery_id",
    "campaign_id",
    "timestamp",
    "status",
    "phase",
    "phase_timestamps",
    "mode",
    "lane",
    "addresses",
    "resource",
    "artifact_sha256",
    "receipt_sha256",
    "base_commit",
    "commit",
    "delivery_receipt_path",
    "note",
}


class ReplayError(ValueError):
    """Report an invalid private replay operation."""


class LedgerRecords(list[dict[str, object]]):
    """Store bounded ledger rows with a campaign lookup built once."""

    def __init__(self, rows: Sequence[dict[str, object]]) -> None:
        super().__init__(rows)
        grouped: dict[str, list[dict[str, object]]] = {}
        for row in rows:
            campaign_id = row.get("campaign_id")
            if isinstance(campaign_id, str):
                grouped.setdefault(campaign_id, []).append(row)
        self.by_campaign = {
            campaign_id: tuple(values) for campaign_id, values in grouped.items()
        }


@dataclass(frozen=True)
class ReplayState:
    """Store one state in a bounded replay search."""

    discovered: frozenset[int]
    selected: tuple[int, ...]
    incumbent: int
    best_quality: int
    non_improving: int
    tried_by_incumbent: tuple[tuple[int, tuple[str, ...]], ...]


def _reject_constant(_value: str) -> object:
    raise ReplayError("a replay JSON document contains a non-finite number")


def _pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ReplayError("a replay JSON document contains a duplicate key")
        result[key] = value
    return result


def _canonical_json(value: object) -> bytes:
    try:
        return json.dumps(
            value,
            allow_nan=False,
            ensure_ascii=True,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
    except (TypeError, ValueError) as error:
        raise ReplayError("a replay document is not canonical JSON") from error


def _json_hash(value: object, *, omit: str | None = None) -> str:
    if omit is not None and isinstance(value, Mapping):
        value = {key: item for key, item in value.items() if key != omit}
    return hashlib.sha256(_canonical_json(value)).hexdigest()


def _strict_json(content: bytes, description: str) -> dict[str, object]:
    try:
        value = json.loads(
            content.decode("utf-8"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ReplayError(f"the {description} is not valid JSON") from error
    if not isinstance(value, dict):
        raise ReplayError(f"the {description} is not a JSON object")
    return value


def _strict_keys(value: Mapping[str, object], keys: set[str], description: str) -> None:
    if set(value) != keys:
        raise ReplayError(f"the {description} has invalid fields")


def _sha256(value: object, description: str) -> str:
    if not isinstance(value, str) or HASH_RE.fullmatch(value) is None:
        raise ReplayError(f"the {description} is invalid")
    return value


def _positive_int(value: object, description: str, maximum: int) -> int:
    if type(value) is not int or not 0 < value <= maximum:
        raise ReplayError(f"the {description} is invalid")
    return value


def _regular_bytes(path: Path, maximum: int, description: str) -> bytes:
    """Read one stable regular file through no-follow directory descriptors."""

    absolute = Path(os.path.abspath(path))
    if os.name != "posix" or os.open not in os.supports_dir_fd:
        raise ReplayError("secure replay file access is unavailable")
    parts = absolute.parts
    directory = -1
    descriptor = -1
    try:
        directory = os.open(parts[0], os.O_RDONLY | os.O_DIRECTORY)
        for part in parts[1:-1]:
            child = os.open(
                part,
                os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        descriptor = os.open(
            parts[-1], os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory
        )
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = -1
            before = os.fstat(stream.fileno())
            content = stream.read(maximum + 1)
            after = os.fstat(stream.fileno())
        current_uid = os.geteuid() if hasattr(os, "geteuid") else None
        stable = (
            before.st_dev,
            before.st_ino,
            before.st_mode,
            before.st_nlink,
            before.st_uid,
            before.st_size,
            before.st_mtime_ns,
            before.st_ctime_ns,
        ) == (
            after.st_dev,
            after.st_ino,
            after.st_mode,
            after.st_nlink,
            after.st_uid,
            after.st_size,
            after.st_mtime_ns,
            after.st_ctime_ns,
        )
        if (
            not stat.S_ISREG(after.st_mode)
            or after.st_nlink != 1
            or (current_uid is not None and after.st_uid != current_uid)
            or not stable
            or after.st_size <= 0
            or after.st_size > maximum
            or len(content) != after.st_size
        ):
            raise OSError
        return content
    except (OSError, ReplayError) as error:
        raise ReplayError(f"the {description} is missing or invalid") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if directory >= 0:
            os.close(directory)


def _tracked_file(path: Path, root: Path, relative: Path, maximum: int) -> bytes:
    expected = root.resolve() / relative
    if not path.is_absolute():
        path = root.resolve() / path
    if path.absolute() != expected or path.resolve() != expected:
        raise ReplayError("the replay manifest path is invalid")
    return _regular_bytes(expected, maximum, "replay manifest")


def manifest_path(root: Path = ROOT) -> Path:
    """Return the exact public replay manifest path."""

    return root.resolve() / MANIFEST_RELATIVE


def _validate_manifest(document: dict[str, object]) -> dict[str, object]:
    """Validate one parsed public replay manifest."""

    _strict_keys(document, MANIFEST_KEYS, "replay manifest")
    rows = document.get("cases")
    if (
        type(document.get("schema_version")) is not int
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("kind") != "private-replay-manifest"
        or not isinstance(rows, list)
        or len(rows) > MAX_CASES
    ):
        raise ReplayError("the replay manifest is invalid")
    normalized: list[dict[str, object]] = []
    for row in rows:
        if not isinstance(row, Mapping):
            raise ReplayError("the replay manifest has an invalid case")
        _strict_keys(row, MANIFEST_CASE_KEYS, "replay manifest case")
        case_id = row.get("case_id")
        if not isinstance(case_id, str) or CASE_ID_RE.fullmatch(case_id) is None:
            raise ReplayError("the replay manifest has an invalid case ID")
        normalized.append(
            {
                "case_id": case_id,
                "bytes": _positive_int(row.get("bytes"), "case byte count", MAX_CASE_BYTES),
                "sha256": _sha256(row.get("sha256"), "case hash"),
            }
        )
    if normalized != sorted(normalized, key=lambda item: str(item["case_id"])):
        raise ReplayError("the replay manifest cases are not sorted")
    if len({str(item["case_id"]) for item in normalized}) != len(normalized):
        raise ReplayError("the replay manifest contains a duplicate case")
    return document


def _manifest_document(root: Path) -> tuple[dict[str, object], bytes]:
    """Read the public manifest and keep its exact bytes."""

    path = manifest_path(root)
    content = _tracked_file(path, root, MANIFEST_RELATIVE, MAX_MANIFEST_BYTES)
    document = _strict_json(content, "replay manifest")
    return _validate_manifest(document), content


def read_manifest(root: Path = ROOT) -> dict[str, object]:
    """Read and validate the public replay manifest."""

    root = root.resolve()
    document, _ = _manifest_document(root)
    return document


def _directory_flags() -> int:
    return os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)


def _open_absolute_directory(path: Path) -> int:
    """Open an absolute directory while holding every no-follow component."""

    absolute = Path(os.path.abspath(path))
    if os.name != "posix" or os.open not in os.supports_dir_fd:
        raise ReplayError("secure private replay storage is unavailable")
    parts = absolute.parts
    directory = os.open(parts[0], os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in parts[1:]:
            child = os.open(part, _directory_flags(), dir_fd=directory)
            os.close(directory)
            directory = child
        return directory
    except Exception:
        os.close(directory)
        raise


def _open_directory(parent_fd: int, name: str, *, create: bool) -> int:
    if create:
        try:
            os.mkdir(name, mode=0o700, dir_fd=parent_fd)
        except FileExistsError:
            pass
    try:
        descriptor = os.open(name, _directory_flags(), dir_fd=parent_fd)
    except OSError as error:
        raise ReplayError("the private replay directory is missing or invalid") from error
    metadata = os.fstat(descriptor)
    current_uid = os.geteuid() if hasattr(os, "geteuid") else None
    if (
        not stat.S_ISDIR(metadata.st_mode)
        or metadata.st_mode & 0o077
        or (current_uid is not None and metadata.st_uid != current_uid)
    ):
        os.close(descriptor)
        raise ReplayError("the private replay directory is missing or invalid")
    return descriptor


@contextmanager
def _private_directory(
    root: Path, child: str | None = None, *, create: bool = False
) -> Iterator[int]:
    root = root.resolve()
    if (
        os.name != "posix"
        or os.open not in os.supports_dir_fd
        or os.mkdir not in os.supports_dir_fd
        or os.rename not in os.supports_dir_fd
    ):
        raise ReplayError("secure private replay storage is unavailable")
    try:
        root_fd = _open_absolute_directory(root)
    except OSError as error:
        raise ReplayError("the repository root is invalid") from error
    private_fd = -1
    child_fd = -1
    try:
        private_fd = _open_directory(root_fd, PRIVATE_RELATIVE.name, create=create)
        if child is None:
            yield private_fd
        else:
            child_fd = _open_directory(private_fd, child, create=create)
            yield child_fd
    finally:
        if child_fd >= 0:
            os.close(child_fd)
        if private_fd >= 0:
            os.close(private_fd)
        os.close(root_fd)


def _read_private_file_at(
    directory_fd: int,
    name: str,
    maximum: int,
    description: str,
    *,
    allow_empty: bool = False,
) -> tuple[bytes, os.stat_result]:
    """Read one stable private file and return its final metadata."""

    if "/" in name or "\\" in name or name in {".", ".."}:
        raise ReplayError(f"the {description} name is invalid")
    try:
        descriptor = os.open(
            name,
            os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory_fd,
        )
        with os.fdopen(descriptor, "rb") as stream:
            before = os.fstat(stream.fileno())
            content = stream.read(maximum + 1)
            after = os.fstat(stream.fileno())
        current_uid = os.geteuid() if hasattr(os, "geteuid") else None
        stable = (
            before.st_dev,
            before.st_ino,
            before.st_mode,
            before.st_nlink,
            before.st_uid,
            before.st_size,
            before.st_mtime_ns,
            before.st_ctime_ns,
        ) == (
            after.st_dev,
            after.st_ino,
            after.st_mode,
            after.st_nlink,
            after.st_uid,
            after.st_size,
            after.st_mtime_ns,
            after.st_ctime_ns,
        )
        if (
            not stat.S_ISREG(after.st_mode)
            or after.st_nlink != 1
            or after.st_mode & 0o077
            or (current_uid is not None and after.st_uid != current_uid)
            or not stable
            or (after.st_size <= 0 and not allow_empty)
            or after.st_size > maximum
            or len(content) != after.st_size
        ):
            raise OSError
        return content, after
    except OSError as error:
        raise ReplayError(f"the {description} is missing or invalid") from error


def _read_at(directory_fd: int, name: str, maximum: int, description: str) -> bytes:
    content, _ = _read_private_file_at(
        directory_fd,
        name,
        maximum,
        description,
    )
    return content


def _unlink_private_identity_at(
    directory_fd: int,
    name: str,
    identity: tuple[int, int],
) -> None:
    """Remove a private file only while it still names the expected inode."""

    try:
        descriptor = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory_fd)
        try:
            metadata = os.fstat(descriptor)
        finally:
            os.close(descriptor)
        if (metadata.st_dev, metadata.st_ino) != identity:
            raise ReplayError("the private replay temporary file changed")
        os.unlink(name, dir_fd=directory_fd)
        os.fsync(directory_fd)
    except FileNotFoundError:
        return
    except OSError as error:
        raise ReplayError("cannot remove a private replay temporary file") from error


def _prepare_private_temp_at(
    directory_fd: int,
    temporary: str,
    content: bytes,
) -> tuple[int, int]:
    """Create or recover one deterministic, owner-only temporary file."""

    try:
        saved, metadata = _read_private_file_at(
            directory_fd,
            temporary,
            MAX_TRAJECTORY_BYTES,
            "private replay temporary file",
            allow_empty=True,
        )
    except ReplayError:
        try:
            os.stat(temporary, dir_fd=directory_fd, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            # An existing unsafe or unreadable object is not safe to remove.
            raise
    else:
        identity = (metadata.st_dev, metadata.st_ino)
        if saved == content:
            return identity
        _unlink_private_identity_at(directory_fd, temporary, identity)

    descriptor = -1
    identity: tuple[int, int] | None = None
    try:
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
            0o600,
            dir_fd=directory_fd,
        )
        with os.fdopen(descriptor, "wb") as stream:
            descriptor = -1
            before = os.fstat(stream.fileno())
            current_uid = os.geteuid() if hasattr(os, "geteuid") else None
            if (
                not stat.S_ISREG(before.st_mode)
                or before.st_nlink != 1
                or before.st_mode & 0o077
                or (current_uid is not None and before.st_uid != current_uid)
            ):
                raise OSError
            identity = (before.st_dev, before.st_ino)
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
            after = os.fstat(stream.fileno())
            if (
                identity != (after.st_dev, after.st_ino)
                or after.st_nlink != 1
                or after.st_size != len(content)
            ):
                raise OSError
        saved, metadata = _read_private_file_at(
            directory_fd,
            temporary,
            max(len(content), 1),
            "private replay temporary file",
            allow_empty=True,
        )
        if saved != content or identity != (metadata.st_dev, metadata.st_ino):
            raise OSError
        return identity
    except ReplayError:
        if identity is not None:
            _unlink_private_identity_at(directory_fd, temporary, identity)
        raise
    except OSError as error:
        if identity is not None:
            try:
                _unlink_private_identity_at(directory_fd, temporary, identity)
            except ReplayError:
                pass
        raise ReplayError("cannot prepare a private replay document") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)


def _write_exclusive_at(directory_fd: int, name: str, content: bytes) -> None:
    if "/" in name or "\\" in name or name in {".", ".."}:
        raise ReplayError("the private replay document name is invalid")
    temporary = f".{name}.tmp"
    published = False
    published_identity: tuple[int, int] | None = None
    try:
        try:
            existing, _ = _read_private_file_at(
                directory_fd,
                name,
                max(len(content), 1),
                "private replay document",
                allow_empty=True,
            )
        except ReplayError:
            try:
                os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise
        else:
            if existing == content:
                try:
                    _, temporary_metadata = _read_private_file_at(
                        directory_fd,
                        temporary,
                        MAX_TRAJECTORY_BYTES,
                        "private replay temporary file",
                        allow_empty=True,
                    )
                except ReplayError:
                    try:
                        os.stat(
                            temporary,
                            dir_fd=directory_fd,
                            follow_symlinks=False,
                        )
                    except FileNotFoundError:
                        os.fsync(directory_fd)
                        return
                    raise
                _unlink_private_identity_at(
                    directory_fd,
                    temporary,
                    (temporary_metadata.st_dev, temporary_metadata.st_ino),
                )
                return
            raise ReplayError("the private replay document path has other content")

        published_identity = _prepare_private_temp_at(
            directory_fd,
            temporary,
            content,
        )
        try:
            rename_noreplace = ctypes.CDLL(None, use_errno=True).renameat2
        except AttributeError as error:
            raise ReplayError("secure private replay storage is unavailable") from error
        rename_noreplace.argtypes = [
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_uint,
        ]
        rename_noreplace.restype = ctypes.c_int
        if rename_noreplace(
            directory_fd,
            os.fsencode(temporary),
            directory_fd,
            os.fsencode(name),
            1,
        ) != 0:
            saved_errno = ctypes.get_errno()
            if saved_errno == errno.EEXIST:
                raise FileExistsError(name)
            raise OSError(saved_errno, os.strerror(saved_errno))
        published = True
        saved, after = _read_private_file_at(
            directory_fd,
            name,
            max(len(content), 1),
            "private replay document",
            allow_empty=True,
        )
        if (
            published_identity != (after.st_dev, after.st_ino)
            or saved != content
        ):
            raise OSError
        os.fsync(directory_fd)
    except (OSError, ReplayError) as error:
        if published and published_identity is not None:
            try:
                current = os.open(
                    name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory_fd
                )
                try:
                    current_metadata = os.fstat(current)
                finally:
                    os.close(current)
                if (current_metadata.st_dev, current_metadata.st_ino) == published_identity:
                    os.unlink(name, dir_fd=directory_fd)
                    os.fsync(directory_fd)
            except OSError:
                pass
        if isinstance(error, ReplayError):
            raise
        raise ReplayError("cannot publish the immutable private replay document") from error
    finally:
        if not published and published_identity is not None:
            try:
                _unlink_private_identity_at(
                    directory_fd,
                    temporary,
                    published_identity,
                )
            except ReplayError:
                pass


def _replace_at(directory_fd: int, name: str, content: bytes) -> None:
    temporary = f".{name}.tmp"
    try:
        _read_private_file_at(
            directory_fd,
            name,
            MAX_CERTIFICATE_BYTES,
            "private replay pointer",
            allow_empty=True,
        )
    except ReplayError:
        try:
            os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise
    temporary_identity = _prepare_private_temp_at(directory_fd, temporary, content)
    try:
        os.replace(temporary, name, src_dir_fd=directory_fd, dst_dir_fd=directory_fd)
        os.fsync(directory_fd)
        saved, metadata = _read_private_file_at(
            directory_fd,
            name,
            max(len(content), 1),
            "private replay pointer",
            allow_empty=True,
        )
        if (
            saved != content
            or temporary_identity != (metadata.st_dev, metadata.st_ino)
        ):
            raise OSError
    except (OSError, ReplayError) as error:
        try:
            _unlink_private_identity_at(
                directory_fd,
                temporary,
                temporary_identity,
            )
        except ReplayError:
            pass
        if isinstance(error, ReplayError):
            raise
        raise ReplayError("cannot update the private replay pointer") from error


def _trajectory_commitment(document: Mapping[str, object], content: bytes) -> dict[str, object]:
    from tools.decomp_experiment import _validate_trajectory_seal_document

    try:
        _validate_trajectory_seal_document(dict(document))
    except Exception as error:
        raise ReplayError("the experiment trajectory seal is invalid") from error
    content_sha256 = document.get("content_sha256")
    if (
        type(document.get("schema_version")) is not int
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("kind") != "experiment-trajectory-seal"
        or not isinstance(content_sha256, str)
        or HASH_RE.fullmatch(content_sha256) is None
        or content_sha256 != _json_hash(document, omit="content_sha256")
    ):
        raise ReplayError("the experiment trajectory seal is invalid")
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "campaign-replay-experiment-commitment",
        "content_sha256": content_sha256,
        "sha256": hashlib.sha256(content).hexdigest(),
        "bytes": len(content),
    }


def publish_private_trajectory(
    document: Mapping[str, object],
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Publish one full trajectory seal only in the private replay root."""

    root = root.resolve()
    content = json.dumps(document, indent=2, sort_keys=True).encode("utf-8") + b"\n"
    if not 0 < len(content) <= MAX_TRAJECTORY_BYTES:
        raise ReplayError("the experiment trajectory seal is too large")
    commitment = _trajectory_commitment(document, content)
    if not _private_root_is_untracked(root):
        raise ReplayError("the private replay root is tracked")
    with _suite_lock(root, create=True, exclusive=True):
        with _private_directory(root, TRAJECTORIES_NAME, create=True) as directory_fd:
            name = f"{commitment['content_sha256']}.json"
            _write_exclusive_at(directory_fd, name, content)
    return commitment


def read_private_trajectory(
    commitment: Mapping[str, object],
    *,
    root: Path = ROOT,
    assume_locked: bool = False,
) -> dict[str, object]:
    """Read the full private seal named by one opaque commitment."""

    root = root.resolve()
    _strict_keys(
        commitment,
        {"schema_version", "kind", "content_sha256", "sha256", "bytes"},
        "experiment trajectory commitment",
    )
    if (
        type(commitment.get("schema_version")) is not int
        or commitment.get("schema_version") != SCHEMA_VERSION
        or commitment.get("kind") != "campaign-replay-experiment-commitment"
    ):
        raise ReplayError("the experiment trajectory commitment is invalid")
    content_sha256 = _sha256(
        commitment.get("content_sha256"), "trajectory content hash"
    )
    expected_sha256 = _sha256(commitment.get("sha256"), "trajectory file hash")
    expected_bytes = _positive_int(
        commitment.get("bytes"), "trajectory byte count", MAX_TRAJECTORY_BYTES
    )

    def read_locked() -> bytes:
        with _private_directory(root, TRAJECTORIES_NAME) as directory_fd:
            return _read_at(
                directory_fd,
                f"{content_sha256}.json",
                MAX_TRAJECTORY_BYTES,
                "private experiment trajectory",
            )

    if assume_locked:
        content = read_locked()
    else:
        with _suite_lock(root, create=False, exclusive=False):
            content = read_locked()
    if len(content) != expected_bytes or hashlib.sha256(content).hexdigest() != expected_sha256:
        raise ReplayError("the private experiment trajectory commitment is stale")
    document = _strict_json(content, "private experiment trajectory")
    if _trajectory_commitment(document, content) != dict(commitment):
        raise ReplayError("the private experiment trajectory commitment is stale")
    return document


@contextmanager
def _suite_lock(root: Path, *, create: bool, exclusive: bool) -> Iterator[None]:
    """Lock all private case and certificate operations."""

    if fcntl is None:
        raise ReplayError("secure private replay storage is unavailable")
    with _private_directory(root, create=create) as directory_fd:
        try:
            descriptor = os.open(
                "suite.lock",
                os.O_RDWR
                | (os.O_CREAT if create else 0)
                | getattr(os, "O_NOFOLLOW", 0),
                0o600,
                dir_fd=directory_fd,
            )
        except OSError as error:
            raise ReplayError("the private replay lock is missing or invalid") from error
        try:
            metadata = os.fstat(descriptor)
            current_uid = os.geteuid() if hasattr(os, "geteuid") else None
            if (
                not stat.S_ISREG(metadata.st_mode)
                or metadata.st_nlink != 1
                or metadata.st_mode & 0o077
                or (current_uid is not None and metadata.st_uid != current_uid)
            ):
                raise ReplayError("the private replay lock is missing or invalid")
            fcntl.flock(
                descriptor,
                fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH,
            )
            current = os.stat(
                "suite.lock",
                dir_fd=directory_fd,
                follow_symlinks=False,
            )
            if (
                (current.st_dev, current.st_ino)
                != (metadata.st_dev, metadata.st_ino)
                or not stat.S_ISREG(current.st_mode)
                or current.st_nlink != 1
                or current.st_mode & 0o077
                or (current_uid is not None and current.st_uid != current_uid)
            ):
                raise ReplayError("the private replay lock is missing or invalid")
            yield
        finally:
            try:
                fcntl.flock(descriptor, fcntl.LOCK_UN)
            finally:
                os.close(descriptor)


def _case_bytes(entry: Mapping[str, object], root: Path) -> bytes:
    case_id = str(entry["case_id"])
    with _private_directory(root, CASES_NAME) as directory_fd:
        content = _read_at(directory_fd, f"{case_id}.json", MAX_CASE_BYTES, "private replay case")
    if len(content) != entry.get("bytes") or hashlib.sha256(content).hexdigest() != entry.get(
        "sha256"
    ):
        raise ReplayError("a private replay case does not match the public manifest")
    return content


def _case_file_names(root: Path) -> set[str]:
    """Return all private case directory entries without following links."""

    root = root.resolve()
    if os.name != "posix" or os.open not in os.supports_dir_fd:
        if not (root / PRIVATE_RELATIVE).exists():
            return set()
        raise ReplayError("secure private replay storage is unavailable")
    root_fd = _open_absolute_directory(root)
    try:
        try:
            private_metadata = os.stat(
                PRIVATE_RELATIVE.name, dir_fd=root_fd, follow_symlinks=False
            )
        except FileNotFoundError:
            return set()
        if not stat.S_ISDIR(private_metadata.st_mode):
            raise ReplayError("the private replay directory is missing or invalid")
    finally:
        os.close(root_fd)
    try:
        with _private_directory(root, CASES_NAME) as directory_fd:
            names: set[str] = set()
            with os.scandir(directory_fd) as entries:
                for entry in entries:
                    if len(names) >= MAX_CASES + 16:
                        raise ReplayError("the private replay case directory is too large")
                    names.add(entry.name)
            return names
    except ReplayError:
        with _private_directory(root) as private_fd:
            try:
                os.stat(CASES_NAME, dir_fd=private_fd, follow_symlinks=False)
            except FileNotFoundError:
                return set()
        raise


def _private_root_exists(root: Path) -> bool:
    """Check for a real private root without following its final component."""

    root_fd = _open_absolute_directory(root.resolve())
    try:
        try:
            metadata = os.stat(
                PRIVATE_RELATIVE.name, dir_fd=root_fd, follow_symlinks=False
            )
        except FileNotFoundError:
            return False
    finally:
        os.close(root_fd)
    if not stat.S_ISDIR(metadata.st_mode):
        raise ReplayError("the private replay directory is missing or invalid")
    return True


def _enrollment_journal_exists(root: Path) -> bool:
    """Return true when an enrollment transaction still needs recovery."""

    if not _private_root_exists(root):
        return False
    with _private_directory(root) as directory_fd:
        for name in (
            ENROLLMENT_JOURNAL_NAME,
            f".{ENROLLMENT_JOURNAL_NAME}.tmp",
        ):
            try:
                os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            except FileNotFoundError:
                continue
            return True
    return False


def _routes(value: object) -> list[str]:
    if not isinstance(value, list):
        raise ReplayError("a replay route list is invalid")
    routes: list[str] = []
    for route in value:
        if not isinstance(route, str) or route not in ROUTE_ORDER:
            raise ReplayError("a replay route is outside the mismatch taxonomy")
        routes.append(route)
    expected = [route for route in ROUTE_ORDER if route in set(routes)]
    if routes != expected:
        raise ReplayError("a replay route list is not unique and ordered")
    return routes


def _subsystem_slug(value: object) -> str:
    """Return one stable ASCII identity for an accepted subsystem label."""

    if not isinstance(value, str):
        raise ReplayError("the private replay subsystem is invalid")
    folded = unicodedata.normalize("NFKD", value.strip().casefold())
    ascii_text = folded.encode("ascii", errors="ignore").decode("ascii")
    slug = re.sub(r"[^a-z0-9]+", "-", ascii_text).strip("-")
    if not slug or len(slug) > MAX_TEXT:
        raise ReplayError("the private replay subsystem is invalid")
    return slug


def quality(status_level: object, score_millionths: object) -> int:
    """Return the exact integer quality for one eligible candidate."""

    try:
        return _shared_quality(status_level, score_millionths)
    except ValueError as error:
        raise ReplayError("a replay candidate quality is invalid") from error


def quality_from_score(status: object, score: object) -> tuple[int, int]:
    """Normalize one source status and score without floating replay math."""

    try:
        return _shared_quality_from_score(status, score)
    except ValueError as error:
        raise ReplayError("a replay source quality is invalid") from error


def validate_case(document: object) -> dict[str, object]:
    """Validate one redacted private replay case."""

    if not isinstance(document, dict):
        raise ReplayError("the private replay case is not an object")
    _strict_keys(document, CASE_KEYS, "private replay case")
    case_id = document.get("case_id")
    if (
        type(document.get("schema_version")) is not int
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("kind") != "private-replay-case"
        or not isinstance(case_id, str)
        or CASE_ID_RE.fullmatch(case_id) is None
    ):
        raise ReplayError("the private replay case identity is invalid")
    provenance = document.get("provenance")
    classification = document.get("classification")
    budget = document.get("budget")
    graph = document.get("graph")
    if not all(isinstance(item, Mapping) for item in (provenance, classification, budget, graph)):
        raise ReplayError("the private replay case has an invalid section")
    assert isinstance(provenance, Mapping)
    assert isinstance(classification, Mapping)
    assert isinstance(budget, Mapping)
    assert isinstance(graph, Mapping)
    _strict_keys(provenance, PROVENANCE_KEYS, "private replay provenance")
    _strict_keys(classification, CLASSIFICATION_KEYS, "private replay classification")
    _strict_keys(budget, BUDGET_KEYS, "private replay budget")
    _strict_keys(graph, GRAPH_KEYS, "private replay graph")
    target = provenance.get("target")
    session_id = provenance.get("session_id")
    campaign_id = provenance.get("campaign_id")
    if (
        not isinstance(target, str)
        or re.fullmatch(r"0x[0-9A-F]{8}", target) is None
        or not isinstance(session_id, str)
        or SESSION_RE.fullmatch(session_id) is None
        or not isinstance(campaign_id, str)
        or len(campaign_id) > MAX_TEXT
        or not campaign_id
        or not isinstance(provenance.get("session_receipt_id"), str)
    ):
        raise ReplayError("the private replay provenance identity is invalid")
    for name in (
        "session_receipt_id",
        "campaign_record_sha256",
        "delivery_receipt_sha256",
        "context_pack_sha256",
        "impact_pack_sha256",
        "trajectory_sha256",
    ):
        _sha256(provenance.get(name), f"private replay {name}")
    leaf_hash = provenance.get("leaf_oracle_sha256")
    if leaf_hash is not None:
        _sha256(leaf_hash, "private replay leaf oracle hash")
    source_commit = provenance.get("source_commit")
    if not isinstance(source_commit, str) or COMMIT_RE.fullmatch(source_commit) is None:
        raise ReplayError("the private replay source commit is invalid")
    subsystem = classification.get("subsystem")
    if subsystem != _subsystem_slug(subsystem):
        raise ReplayError("the private replay classification is invalid")
    if (
        set(budget) != BUDGET_KEYS
        or type(budget.get("max_trials")) is not int
        or budget.get("max_trials") != MAX_TRIALS
        or type(budget.get("max_non_improving")) is not int
        or budget.get("max_non_improving") != MAX_NON_IMPROVING
    ):
        raise ReplayError("the private replay budget is invalid")
    nodes = graph.get("nodes")
    edges = graph.get("edges")
    if not isinstance(edges, list) or len(edges) not in {2, MAX_TRIALS}:
        raise ReplayError("the private replay graph has an invalid edge set")
    if not isinstance(nodes, list) or len(nodes) != len(edges) + 1:
        raise ReplayError("the private replay graph has an invalid node set")
    normalized_nodes: list[dict[str, object]] = []
    for node in nodes:
        if not isinstance(node, Mapping):
            raise ReplayError("a private replay node is invalid")
        _strict_keys(node, NODE_KEYS, "private replay node")
        status_level = node.get("status_level")
        score = node.get("score_millionths")
        eligible = node.get("eligible")
        quality(status_level, score)
        if type(eligible) is not bool:
            raise ReplayError("a private replay node result is invalid")
        normalized_nodes.append(
            {
                "status_level": status_level,
                "score_millionths": score,
                "eligible": eligible,
                "routes": _routes(node.get("routes")),
            }
        )
    if normalized_nodes[0]["eligible"] is not True:
        raise ReplayError("the private replay baseline is ineligible")
    normalized_edges: list[dict[str, object]] = []
    parents: set[int] = set()
    for sequence, edge in enumerate(edges, start=1):
        if not isinstance(edge, Mapping):
            raise ReplayError("a private replay edge is invalid")
        _strict_keys(edge, EDGE_KEYS, "private replay edge")
        source = edge.get("from")
        destination = edge.get("to")
        route = edge.get("route")
        if (
            type(edge.get("sequence")) is not int
            or edge.get("sequence") != sequence
            or type(source) is not int
            or type(destination) is not int
            or destination != sequence
            or not 0 <= source < destination
            or route not in ROUTE_ORDER
            or route not in normalized_nodes[source]["routes"]
            or destination in parents
        ):
            raise ReplayError("a private replay edge binding is invalid")
        parents.add(destination)
        normalized_edges.append(dict(edge))
    outgoing: dict[int, list[dict[str, object]]] = {}
    for edge in normalized_edges:
        outgoing.setdefault(int(edge["from"]), []).append(edge)
    if not any(
        len({str(edge["route"]) for edge in values}) >= 2
        for values in outgoing.values()
    ):
        raise ReplayError("the private replay graph has no branch point")
    if len({str(edge["route"]) for edge in normalized_edges}) < 2:
        raise ReplayError("the private replay graph has fewer than two routes")
    best_quality = quality(
        normalized_nodes[0]["status_level"], normalized_nodes[0]["score_millionths"]
    )
    incumbent = 0
    improvements = 0
    total_non_improving = 0
    consecutive_non_improving = 0
    for edge in normalized_edges:
        if _node_terminal(normalized_nodes[incumbent]):
            raise ReplayError("the private replay sequence continues after a terminal result")
        if consecutive_non_improving >= MAX_NON_IMPROVING:
            raise ReplayError("the private replay sequence exceeds its non-improvement limit")
        node = normalized_nodes[int(edge["to"])]
        node_quality = quality(node["status_level"], node["score_millionths"])
        if node["eligible"] is True and node_quality > best_quality:
            best_quality = node_quality
            incumbent = int(edge["to"])
            improvements += 1
            consecutive_non_improving = 0
        else:
            total_non_improving += 1
            consecutive_non_improving += 1
    if improvements < 1 or total_non_improving < 1:
        raise ReplayError("the private replay trajectory has an invalid improvement count")
    if not (
        _node_terminal(normalized_nodes[incumbent])
        or consecutive_non_improving >= MAX_NON_IMPROVING
        or len(normalized_edges) >= MAX_TRIALS
    ):
        raise ReplayError("the private replay trajectory stops before its budget")
    return document


def _git_blob(
    root: Path,
    revision: str,
    relative: Path,
    maximum: int,
    *,
    modes: frozenset[str] = frozenset({"100644"}),
) -> bytes:
    """Read one bounded regular file blob from Git."""

    object_name = f"{revision}:{relative.as_posix()}"
    tree = subprocess.run(
        ["git", "ls-tree", revision, "--", relative.as_posix()],
        cwd=root,
        check=False,
        capture_output=True,
    )
    size = subprocess.run(
        ["git", "cat-file", "-s", object_name],
        cwd=root,
        check=False,
        capture_output=True,
    )
    try:
        object_size = int(size.stdout.decode("ascii").strip())
    except (UnicodeDecodeError, ValueError) as error:
        raise ReplayError("a required Git evidence blob is invalid") from error
    fields = tree.stdout.decode("ascii", errors="ignore").split()
    if (
        tree.returncode != 0
        or size.returncode != 0
        or not fields
        or fields[0] not in modes
        or not 0 < object_size <= maximum
    ):
        raise ReplayError("a required Git evidence blob is invalid")
    try:
        process = subprocess.Popen(
            ["git", "cat-file", "blob", object_name],
            cwd=root,
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
        raise ReplayError("a required Git evidence blob is unavailable") from error
    if returncode != 0 or len(content) != object_size:
        raise ReplayError("a required Git evidence blob is invalid")
    return content


def _git_ref(root: Path, name: str) -> str | None:
    """Resolve one Git commit ref without changing repository state."""

    try:
        result = subprocess.run(
            ["git", "rev-parse", "--verify", f"{name}^{{commit}}"],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ReplayError("a required Git reference is unavailable") from error
    value = result.stdout.decode("ascii", errors="ignore").strip().lower()
    if result.returncode != 0 or COMMIT_RE.fullmatch(value) is None:
        return None
    return value


def _git_ancestor(root: Path, ancestor: str, descendant: str) -> bool:
    try:
        result = subprocess.run(
            ["git", "merge-base", "--is-ancestor", ancestor, descendant],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ReplayError("a required Git ancestry check failed") from error
    return result.returncode == 0


def _git_first_parent(root: Path, commit: str) -> str:
    """Return one exact first parent without accepting other object types."""

    value = _git_ref(root, f"{commit}^1")
    if value is None:
        raise ReplayError("the source campaign commit has no first parent")
    return value


def _historical_files(
    root: Path,
    revision: str,
    identity: object,
    required: Sequence[str],
    *,
    absolute_keys: bool = False,
) -> None:
    """Compare tracked receipt hashes with exact bounded Git blobs."""

    if not isinstance(identity, Mapping):
        raise ReplayError("a historical validation identity is invalid")
    expected_keys = {
        str((root / relative).resolve()) if absolute_keys else relative
        for relative in required
    }
    available = set(identity)
    if not expected_keys.issubset(available):
        raise ReplayError("a historical validation identity is incomplete")
    for relative in required:
        key = str((root / relative).resolve()) if absolute_keys else relative
        saved_hash = identity.get(key)
        if not isinstance(saved_hash, str) or HASH_RE.fullmatch(saved_hash) is None:
            raise ReplayError("a historical validation identity is invalid")
        modes = frozenset({"100755"}) if relative == "tools/decomp" else frozenset({"100644"})
        content = _git_blob(root, revision, Path(relative), MAX_TOOL_BYTES, modes=modes)
        if hashlib.sha256(content).hexdigest() != saved_hash:
            raise ReplayError("a historical validation dependency changed")


def _gitlink(root: Path, revision: str, relative: str) -> str:
    try:
        result = subprocess.run(
            ["git", "ls-tree", revision, "--", relative],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ReplayError("a historical Git link is unavailable") from error
    fields = result.stdout.decode("ascii", errors="ignore").split()
    if (
        result.returncode != 0
        or len(result.stdout) > 1024
        or len(fields) < 3
        or fields[0] != "160000"
        or fields[1] != "commit"
        or COMMIT_RE.fullmatch(fields[2]) is None
    ):
        raise ReplayError("a historical Git link is invalid")
    return fields[2]


def _submodule_blob(
    checkout: Path,
    revision: str,
    relative: str,
    maximum: int,
) -> bytes:
    """Read one bounded blob from the local submodule object database."""

    object_name = f"{revision}:{relative}"
    try:
        size_result = subprocess.run(
            ["git", "cat-file", "-s", object_name],
            cwd=checkout,
            check=False,
            capture_output=True,
        )
        size_text = size_result.stdout.decode("ascii", errors="strict").strip()
        object_size = int(size_text)
        if (
            size_result.returncode != 0
            or object_size < 0
            or object_size > maximum
        ):
            raise ValueError
        process = subprocess.Popen(
            ["git", "cat-file", "blob", object_name],
            cwd=checkout,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        assert process.stdout is not None
        with process.stdout:
            content = process.stdout.read(maximum + 1)
        if len(content) > maximum:
            process.kill()
        returncode = process.wait()
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise ReplayError("a historical runtime blob is unavailable") from error
    if returncode != 0 or len(content) != object_size:
        raise ReplayError("a historical runtime blob is invalid")
    return content


def _require_submodule_commit(checkout: Path, revision: str) -> None:
    """Require one exact locally available commit, not another Git object type."""

    try:
        result = subprocess.run(
            ["git", "rev-parse", "--verify", f"{revision}^{{commit}}"],
            cwd=checkout,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ReplayError("the historical runtime commit is unavailable") from error
    resolved = result.stdout.decode("ascii", errors="ignore").strip()
    if (
        result.returncode != 0
        or len(result.stdout) > 64
        or resolved != revision
    ):
        raise ReplayError("the historical runtime commit is unavailable")


def _submodule_package_hash(checkout: Path, revision: str) -> str:
    """Reproduce the recorded reccmp package hash from one Git tree."""

    try:
        process = subprocess.Popen(
            ["git", "ls-tree", "-r", "-z", "--name-only", revision, "--", "reccmp"],
            cwd=checkout,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        assert process.stdout is not None
        with process.stdout:
            output = process.stdout.read(MAX_TOOL_LIST_BYTES + 1)
        if len(output) > MAX_TOOL_LIST_BYTES:
            process.kill()
        returncode = process.wait()
    except OSError as error:
        raise ReplayError("the historical runtime package is unavailable") from error
    if returncode != 0 or len(output) > MAX_TOOL_LIST_BYTES:
        raise ReplayError("the historical runtime package is invalid")
    encoded_names = [value for value in output.split(b"\0") if value]
    if not encoded_names or len(encoded_names) > 4096:
        raise ReplayError("the historical runtime package is invalid")
    names: list[str] = []
    for encoded in encoded_names:
        try:
            name = encoded.decode("utf-8", errors="strict")
        except UnicodeDecodeError as error:
            raise ReplayError("the historical runtime package is invalid") from error
        path = Path(name)
        if (
            not path.parts
            or path.parts[0] != "reccmp"
            or "__pycache__" in path.parts
            or path.suffix in {".pyc", ".pyo"}
        ):
            continue
        names.append(name)
    if names != sorted(set(names)):
        raise ReplayError("the historical runtime package is invalid")
    digest = hashlib.sha256()
    total = 0
    for name in names:
        relative = Path(name).relative_to("reccmp").as_posix()
        content = _submodule_blob(checkout, revision, name, MAX_TOOL_BYTES)
        total += len(content)
        if total > MAX_LEDGER_BYTES:
            raise ReplayError("the historical runtime package is too large")
        digest.update(relative.encode("utf-8"))
        digest.update(content)
    return digest.hexdigest()


def _historical_retail_hash(root: Path, revision: str) -> str:
    """Read the expected retail hash from the historical project file."""

    content = _git_blob(
        root,
        revision,
        Path("reccmp-project.yml"),
        MAX_TOOL_BYTES,
    )
    try:
        text = content.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise ReplayError("the historical reccmp project is invalid") from error
    matches = re.findall(r"(?m)^      sha256: ([0-9a-f]{64})$", text)
    if len(matches) != 1:
        raise ReplayError("the historical reccmp project is invalid")
    return matches[0]


def _historical_validation_identity(
    root: Path, revision: str, identity: object
) -> None:
    """Validate one comparison or delivery tool identity at a commit."""

    if not isinstance(identity, Mapping) or set(identity) != {
        "files",
        "reccmp_user",
        "reccmp_runtime",
    }:
        raise ReplayError("a historical validation tool identity is invalid")
    files = identity.get("files")
    if not isinstance(files, Mapping) or set(files) != set(
        HISTORICAL_VALIDATION_IDENTITY_FILES
    ):
        raise ReplayError("a historical validation tool file identity is invalid")
    _historical_files(root, revision, files, HISTORICAL_VALIDATION_FILES)
    user = identity.get("reccmp_user")
    if not isinstance(user, Mapping) or set(user) != {
        "config_path",
        "config_sha256",
        "project_path",
        "project_sha256",
        "retail_path",
        "retail_sha256",
        "retail_expected_sha256",
    }:
        raise ReplayError("the historical reccmp user identity is invalid")
    if (
        user.get("config_path") != "reccmp-user.yml"
        or user.get("project_path") != "reccmp-project.yml"
        or user.get("retail_path") != "original/toy2.exe"
        or user.get("config_sha256") != files.get("reccmp-user.yml")
        or user.get("project_sha256") != files.get("reccmp-project.yml")
        or user.get("retail_sha256") != user.get("retail_expected_sha256")
        or user.get("retail_expected_sha256")
        != _historical_retail_hash(root, revision)
        or HASH_RE.fullmatch(str(user.get("retail_sha256"))) is None
    ):
        raise ReplayError("the historical reccmp user identity is invalid")
    runtime = identity.get("reccmp_runtime")
    if not isinstance(runtime, Mapping) or set(runtime) != {
        "module_origin",
        "module_sha256",
        "package_sha256",
        "checkout",
        "executables",
    }:
        raise ReplayError("the historical reccmp runtime identity is invalid")
    checkout = runtime.get("checkout")
    if not isinstance(checkout, Mapping) or set(checkout) != {
        "path",
        "head",
        "status_sha256",
        "worktree_sha256",
    }:
        raise ReplayError("the historical reccmp checkout identity is invalid")
    checkout_path = (root / "external/submodules/reccmp").resolve()
    checkout_head = checkout.get("head")
    # The project explicitly permits a clean reccmp checkout at a commit that
    # differs from the superproject gitlink. The receipt must still bind that
    # exact clean commit and all comparison identities must agree on it.
    _gitlink(root, revision, "external/submodules/reccmp")
    empty_hash = hashlib.sha256(b"").hexdigest()
    if (
        checkout.get("path") != str(checkout_path)
        or not isinstance(checkout_head, str)
        or COMMIT_RE.fullmatch(checkout_head) is None
        or checkout.get("status_sha256") != empty_hash
        or checkout.get("worktree_sha256") != empty_hash
    ):
        raise ReplayError("the historical reccmp checkout identity is invalid")
    _require_submodule_commit(checkout_path, checkout_head)
    module_origin = str((checkout_path / "reccmp/__init__.py").resolve())
    module_content = _submodule_blob(
        checkout_path,
        checkout_head,
        "reccmp/__init__.py",
        MAX_TOOL_BYTES,
    )
    if (
        runtime.get("module_origin") != module_origin
        or runtime.get("module_sha256") != hashlib.sha256(module_content).hexdigest()
        or runtime.get("package_sha256")
        != _submodule_package_hash(checkout_path, checkout_head)
    ):
        raise ReplayError("the historical reccmp runtime identity is invalid")
    executables = runtime.get("executables")
    if not isinstance(executables, Mapping) or set(executables) != {
        "reccmp-project",
        "reccmp-reccmp",
    }:
        raise ReplayError("the historical reccmp executable identity is invalid")
    for name in ("reccmp-project", "reccmp-reccmp"):
        descriptor = executables.get(name)
        path_value = descriptor.get("path") if isinstance(descriptor, Mapping) else None
        posix_path = Path(path_value) if isinstance(path_value, str) else None
        windows_path = PureWindowsPath(path_value) if isinstance(path_value, str) else None
        canonical_posix = (
            posix_path is not None
            and posix_path.is_absolute()
            and str(posix_path) == path_value
            and posix_path.name in {name, f"{name}.exe"}
        )
        canonical_windows = (
            windows_path is not None
            and windows_path.is_absolute()
            and str(windows_path) == path_value
            and windows_path.name in {name, f"{name}.exe"}
        )
        if (
            not isinstance(descriptor, Mapping)
            or set(descriptor) != {"path", "sha256"}
            or not (canonical_posix or canonical_windows)
            or not isinstance(descriptor.get("sha256"), str)
            or HASH_RE.fullmatch(str(descriptor.get("sha256"))) is None
        ):
            raise ReplayError("the historical reccmp executable identity is invalid")


def _utc_time(value: object, description: str) -> datetime:
    if not isinstance(value, str):
        raise ReplayError(f"the {description} time is invalid")
    try:
        parsed = datetime.fromisoformat(value)
    except ValueError as error:
        raise ReplayError(f"the {description} time is invalid") from error
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise ReplayError(f"the {description} time is not UTC")
    if parsed.isoformat(timespec="seconds") != value:
        raise ReplayError(f"the {description} time is not canonical")
    return parsed


def _doctor_time(value: object, description: str) -> datetime:
    """Parse the canonical millisecond-Z format emitted by the doctor."""

    if not isinstance(value, str):
        raise ReplayError(f"the {description} time is invalid")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise ReplayError(f"the {description} time is invalid") from error
    canonical = (
        parsed.astimezone(timezone.utc)
        .isoformat(timespec="milliseconds")
        .replace("+00:00", "Z")
    )
    if parsed.utcoffset() != timedelta(0) or canonical != value:
        raise ReplayError(f"the {description} time is not canonical")
    return parsed


def _command_has_output(root: Path, command: Sequence[str]) -> tuple[bool, bool]:
    """Run one bounded Git path query and report success plus any output."""

    try:
        process = subprocess.Popen(
            list(command),
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        assert process.stdout is not None
        with process.stdout:
            first = process.stdout.read(1)
        if first:
            process.kill()
        returncode = process.wait()
    except OSError as error:
        raise ReplayError("a private replay Git check failed") from error
    return returncode == 0, bool(first)


def _symbolic_branch(root: Path) -> str | None:
    """Return the exact checked-out branch without accepting detached HEAD."""

    try:
        result = subprocess.run(
            ["git", "symbolic-ref", "--quiet", "--short", "HEAD"],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ReplayError("the replay branch identity is unavailable") from error
    value = result.stdout.decode("utf-8", errors="ignore").strip()
    if result.returncode != 0 or len(value) > 128:
        return None
    return value


def _private_root_is_untracked(root: Path, head: str | None = None) -> bool:
    """Reject indexed or committed private paths and require the exact ignore rule."""

    head = head or _git_ref(root, "HEAD")
    if head is None:
        return False
    try:
        ignore_content = _git_blob(
            root,
            head,
            Path(".gitignore"),
            MAX_TOOL_BYTES,
        )
    except ReplayError:
        return False
    index_ok, index_has_path = _command_has_output(
        root,
        ["git", "ls-files", "-z", "--", PRIVATE_RELATIVE.as_posix()],
    )
    tree_ok, tree_has_path = _command_has_output(
        root,
        [
            "git",
            "ls-tree",
            "-r",
            "-z",
            "--name-only",
            head,
            "--",
            PRIVATE_RELATIVE.as_posix(),
        ],
    )
    try:
        ignored = subprocess.run(
            [
                "git",
                "check-ignore",
                "-v",
                "--no-index",
                "--",
                f"{PRIVATE_RELATIVE.as_posix()}/probe",
            ],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ReplayError("the private replay ignore check failed") from error
    ignore_line = ignored.stdout.decode("utf-8", errors="replace")
    ignore_ok = (
        ignored.returncode == 0
        and len(ignore_line) <= 4096
        and re.fullmatch(
            r"\.gitignore:[0-9]+:/\.decomp-replay/\t\.decomp-replay/probe\r?\n?",
            ignore_line,
        )
        is not None
    )
    return (
        index_ok
        and tree_ok
        and not index_has_path
        and not tree_has_path
        and ignore_ok
        and _tracked_content_matches_head(
            root,
            head,
            Path(".gitignore"),
            ignore_content,
            MAX_TOOL_BYTES,
        )
    )


def _parse_ledger_records(content: bytes) -> LedgerRecords:
    """Parse one bounded JSONL snapshot without an unbounded row expansion."""

    records: list[dict[str, object]] = []
    for line in io.BytesIO(content):
        if len(records) >= MAX_LEDGER_RECORDS:
            raise ReplayError("the campaign ledger has too many records")
        if not line.strip():
            raise ReplayError("the campaign ledger contains an empty row")
        records.append(_strict_json(line, "campaign ledger row"))
    if not records:
        raise ReplayError("the campaign ledger is empty")
    return LedgerRecords(records)


def _campaign_records(
    records: Sequence[dict[str, object]], campaign_id: object
) -> Sequence[dict[str, object]]:
    """Return one campaign's rows without rescanning a parsed ledger."""

    if not isinstance(campaign_id, str):
        return ()
    if isinstance(records, LedgerRecords):
        return records.by_campaign.get(campaign_id, ())
    return tuple(row for row in records if row.get("campaign_id") == campaign_id)


def _ledger_records(root: Path) -> LedgerRecords:
    """Read the campaign ledger with strict JSON-line parsing."""

    content = _git_blob(root.resolve(), "HEAD", LEDGER_RELATIVE, MAX_LEDGER_BYTES)
    return _parse_ledger_records(content)


def _descriptor_path(
    value: object,
    root: Path,
    *,
    maximum: int,
    description: str,
) -> tuple[Path, bytes]:
    """Validate one absolute repository descriptor."""

    if not isinstance(value, Mapping):
        raise ReplayError(f"the {description} descriptor is invalid")
    path_value = value.get("path")
    saved_hash = value.get("sha256")
    if not isinstance(path_value, str) or not isinstance(saved_hash, str):
        raise ReplayError(f"the {description} descriptor is invalid")
    path = Path(path_value)
    if not path.is_absolute():
        path = root.resolve() / path
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise ReplayError(f"the {description} path is outside the repository") from error
    content = _regular_bytes(path, maximum, description)
    if hashlib.sha256(content).hexdigest() != saved_hash:
        raise ReplayError(f"the {description} content changed")
    return path, content


def _historical_combined_hash(
    root: Path,
    revision: str,
    paths: Sequence[Path],
) -> str:
    """Reproduce a brief tool hash from bounded historical blobs."""

    digest = hashlib.sha256()
    for relative in sorted(paths, key=str):
        digest.update(relative.as_posix().encode("utf-8"))
        content = _git_blob(root, revision, relative, MAX_TOOL_BYTES)
        digest.update(hashlib.sha256(content).hexdigest().encode("ascii"))
    return digest.hexdigest()


def _validate_doctor_campaign_binding(
    receipt: Mapping[str, object],
    campaign: Mapping[str, object],
    target: str,
    identity: Mapping[str, object],
) -> None:
    """Bind one doctor receipt and its times to one committed campaign row."""

    selection_started = _doctor_time(
        receipt.get("selection_started_at"), "doctor selection start"
    )
    doctor_started = _doctor_time(receipt.get("doctor_started_at"), "doctor start")
    doctor_ended = _doctor_time(receipt.get("doctor_ended_at"), "doctor end")
    campaign_started = _utc_time(campaign.get("started_at"), "campaign start")
    if (
        receipt.get("started_at") != receipt.get("doctor_started_at")
        or receipt.get("ended_at") != receipt.get("doctor_ended_at")
        or not selection_started <= doctor_started <= doctor_ended
    ):
        raise ReplayError("the target brief doctor times are invalid")
    campaign_doctors = campaign.get("doctor_receipts")
    if (
        not isinstance(campaign_doctors, list)
        or sum(
            1
            for campaign_doctor in campaign_doctors
            if isinstance(campaign_doctor, Mapping)
            and dict(campaign_doctor) == dict(identity)
        )
        != 1
    ):
        raise ReplayError("the target brief doctor is not bound to the campaign")
    time_bindings: list[tuple[tuple[datetime, datetime, datetime], object, bool]] = []
    if campaign.get("doctor_receipt") == identity:
        time_bindings.append(
            (
                (
                    _utc_time(campaign.get("selection_started_at"), "campaign selection start"),
                    _utc_time(campaign.get("doctor_started_at"), "campaign doctor start"),
                    _utc_time(campaign.get("doctor_ended_at"), "campaign doctor end"),
                ),
                campaign.get("started_at"),
                False,
            )
        )
    events = campaign.get("target_events")
    if not isinstance(events, list):
        raise ReplayError("the source campaign target events are invalid")
    for event in events:
        if (
            isinstance(event, Mapping)
            and event.get("address") == target
            and event.get("doctor_receipt") == identity
        ):
            time_bindings.append(
                (
                    (
                        _utc_time(event.get("selection_started_at"), "target selection start"),
                        _utc_time(event.get("doctor_started_at"), "target doctor start"),
                        _utc_time(event.get("doctor_ended_at"), "target doctor end"),
                    ),
                    event.get("added_at"),
                    True,
                )
            )
    expected_times = tuple(
        value.replace(microsecond=0)
        for value in (selection_started, doctor_started, doctor_ended)
    )
    if len(time_bindings) != 1 or time_bindings[0][0] != expected_times:
        raise ReplayError("the target brief doctor times are not bound to the campaign")
    activation = _utc_time(time_bindings[0][1], "target activation")
    is_target_event = time_bindings[0][2]
    stored_selection, _stored_start, stored_end = expected_times
    if (
        stored_end > activation
        or activation - stored_end > timedelta(minutes=60)
        or (is_target_event and campaign_started > stored_selection)
    ):
        raise ReplayError("the target brief doctor handoff is invalid")


def _validate_historical_doctor(
    descriptor: Mapping[str, object],
    *,
    campaign: Mapping[str, object],
    target: str,
    root: Path,
    campaign_head: str,
) -> dict[str, object]:
    """Validate one immutable doctor receipt without using live repository state."""

    from collections import Counter
    from tools import decomp_doctor as doctor

    def valid_check_counts(actual: Counter[str], address_count: int) -> bool:
        common = doctor._required_check_counts(str(campaign.get("mode")), address_count)
        common.pop("native-build-runtime", None)
        common.pop("wine-prefix", None)
        return actual in (
            common + Counter({"native-build-runtime": 1}),
            common + Counter({"wine-prefix": 1}),
        )

    if set(descriptor) != {
        "path",
        "sha256",
        "receipt_id",
        "mode",
        "lane",
        "source_artifacts",
    }:
        raise ReplayError("the target brief doctor descriptor is invalid")
    path, content = _descriptor_path(
        descriptor,
        root,
        maximum=MAX_BRIEF_BYTES,
        description="doctor receipt",
    )
    receipt_id = descriptor.get("receipt_id")
    expected_path = (
        root.resolve()
        / doctor.RECEIPT_DIRECTORY_RELATIVE
        / f"{receipt_id}.json"
    )
    receipt = _strict_json(content, "doctor receipt")
    lane = campaign.get("lane")
    mode = campaign.get("mode")
    addresses = receipt.get("addresses")
    checks = receipt.get("checks")
    input_hashes = receipt.get("input_hashes")
    doctor_keys = {
        "schema",
        "status",
        "ok",
        "campaign_timing_started",
        "root",
        "mode",
        "lane",
        "addresses",
        "resource",
        "target",
        "branch",
        "head",
        "origin_integration",
        "checks",
        "input_hashes",
        "selection_started_at",
        "started_at",
        "ended_at",
        "doctor_started_at",
        "doctor_ended_at",
        "elapsed_seconds",
        "receipt_id",
        "receipt_path",
    }
    input_hash_keys = {
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
        "current_report_sha256",
        "current_report_provenance_sha256",
        "current_data_report_sha256",
        "current_data_report_provenance_sha256",
        "configured_retail_executable_sha256",
    }
    if (
        set(receipt) != doctor_keys
        or
        not isinstance(receipt_id, str)
        or HASH_RE.fullmatch(receipt_id) is None
        or path.absolute() != expected_path
        or path.resolve() != expected_path
        or descriptor.get("sha256") != hashlib.sha256(content).hexdigest()
        or descriptor.get("mode") != mode
        or descriptor.get("lane") != lane
        or type(receipt.get("schema")) is not int
        or receipt.get("schema") != 1
        or receipt.get("status") != "ready"
        or receipt.get("ok") is not True
        or receipt.get("campaign_timing_started") is not False
        or receipt.get("receipt_id") != receipt_id
        or receipt.get("receipt_path")
        != (doctor.RECEIPT_DIRECTORY_RELATIVE / f"{receipt_id}.json").as_posix()
        or doctor._receipt_id(receipt) != receipt_id
        or receipt.get("root") != str(root.resolve())
        or receipt.get("mode") != mode
        or receipt.get("lane") != lane
        or receipt.get("head") != campaign_head
        or receipt.get("resource") is not None
        or not isinstance(addresses, list)
        or addresses != doctor._normalize_addresses(addresses)
        or len(addresses) != len(set(addresses))
        or target not in addresses
        or receipt.get("target")
        != (addresses[0] if len(addresses) == 1 else None)
        or receipt.get("branch")
        != {"actual": "agent/continuous", "expected": "agent/continuous"}
        or not isinstance(checks, list)
        or not checks
        or any(
            not isinstance(check, Mapping)
            or set(check) != {"name", "ok", "detail", "data"}
            or not isinstance(check.get("name"), str)
            or not isinstance(check.get("detail"), str)
            or check.get("ok") is not True
            for check in checks
        )
        or not valid_check_counts(
            Counter(str(check["name"]) for check in checks), len(addresses)
        )
        or not isinstance(input_hashes, Mapping)
        or set(input_hashes) != input_hash_keys
    ):
        raise ReplayError("the target brief doctor receipt is invalid")
    by_name = {str(check["name"]): check for check in checks}
    if (
        by_name.get("branch", {}).get("data") != receipt.get("branch")
        or by_name.get("head", {}).get("data") != {"actual": campaign_head}
        or by_name.get("origin-integration", {}).get("data")
        != receipt.get("origin_integration")
        or by_name.get("selection", {}).get("data")
        != {"addresses": addresses, "resource": None}
        or not isinstance(receipt.get("origin_integration"), Mapping)
        or set(receipt["origin_integration"])
        != {"fetched", "head", "remote", "remote_ref"}
        or receipt["origin_integration"].get("fetched") is not True
        or receipt["origin_integration"].get("head") != campaign_head
        or receipt["origin_integration"].get("remote_ref")
        != "refs/remotes/origin/agent/continuous"
        or not isinstance(receipt["origin_integration"].get("remote"), str)
        or COMMIT_RE.fullmatch(str(receipt["origin_integration"].get("remote")))
        is None
        or not _git_ancestor(
            root,
            str(receipt["origin_integration"].get("remote")),
            campaign_head,
        )
    ):
        raise ReplayError("the target brief doctor checks are inconsistent")
    map_hash = hashlib.sha256(
        _git_blob(
            root,
            campaign_head,
            Path("tools/Resources/functions_map.txt"),
            MAX_TOOL_BYTES,
        )
    ).hexdigest()
    if (
        input_hashes.get("functions_map_sha256") != map_hash
        or input_hashes.get("retail_executable_sha256")
        != _historical_retail_hash(root, campaign_head)
        or input_hashes.get("configured_retail_executable_sha256")
        != _historical_retail_hash(root, campaign_head)
    ):
        raise ReplayError("the target brief doctor inputs are invalid")
    expected_campaign_doctor = {
        "path": str(expected_path),
        "sha256": hashlib.sha256(content).hexdigest(),
        "receipt_id": receipt_id,
        "head": campaign_head,
        "lane": lane,
        "mode": mode,
        "addresses": addresses,
        "resource": None,
        "input_hashes": dict(input_hashes),
        "status": "ready",
    }
    _validate_doctor_campaign_binding(
        receipt,
        campaign,
        target,
        expected_campaign_doctor,
    )
    try:
        artifacts = doctor.source_artifact_descriptors(receipt, target)
    except ValueError as error:
        raise ReplayError("the target brief doctor artifacts are invalid") from error
    if descriptor.get("source_artifacts") != artifacts:
        raise ReplayError("the target brief doctor artifacts changed")
    expected_artifact_parent = (
        root.resolve()
        / "build/decomp-cache/doctor/ghidra"
        / target[2:].lower()
    )
    for kind, artifact in artifacts.items():
        if not isinstance(artifact, Mapping) or set(artifact) != {
            "path",
            "sha256",
            "bytes",
        }:
            raise ReplayError("a target brief doctor artifact is invalid")
        artifact_path, artifact_content = _descriptor_path(
            artifact,
            root,
            maximum=doctor.GHIDRA_ARTIFACT_LIMIT,
            description="doctor evidence artifact",
        )
        byte_count = artifact.get("bytes")
        if (
            type(byte_count) is not int
            or byte_count != len(artifact_content)
            or artifact_path.parent != expected_artifact_parent
            or artifact_path.resolve().parent != expected_artifact_parent
            or not artifact_path.name.startswith(f"{kind}-")
            or artifact_path.name != f"{kind}-{artifact['sha256']}.json"
        ):
            raise ReplayError("a target brief doctor artifact path is invalid")
        _strict_json(artifact_content, "doctor evidence artifact")
        try:
            doctor.load_ghidra_artifact(root, target, kind, artifact)
        except ValueError as error:
            raise ReplayError("a target brief doctor artifact is invalid") from error
    return receipt


def _validate_historical_context_inputs(
    value: object,
    *,
    root: Path,
    campaign_head: str,
    doctor_receipt: Mapping[str, object],
) -> None:
    """Bind tracked context inputs to campaign HEAD and doctor commitments."""

    if not isinstance(value, Mapping) or set(value) != {
        "decoder",
        "retail_image",
        "repository",
        "tools",
    }:
        raise ReplayError("the target context input identity is invalid")
    decoder = value.get("decoder")
    if (
        not isinstance(decoder, Mapping)
        or set(decoder) != {
            "engine",
            "version",
            "pinned_version",
            "architecture",
            "mode",
            "detail",
        }
        or decoder.get("engine") not in {"capstone", "artifact-row-fallback"}
        or decoder.get("pinned_version") != "5.0.9"
        or decoder.get("architecture") != "x86"
        or type(decoder.get("mode")) is not int
        or decoder.get("mode") != 32
        or type(decoder.get("detail")) is not bool
    ):
        raise ReplayError("the target context decoder identity is invalid")
    retail = value.get("retail_image")
    if retail != {
        "path": "original/toy2.exe",
        "sha256": _historical_retail_hash(root, campaign_head),
    }:
        raise ReplayError("the target context retail identity is invalid")
    repository = value.get("repository")
    tools = value.get("tools")
    expected_tools = (
        "tools/__init__.py",
        "tools/decomp_context.py",
        "tools/decomp_annotations.py",
        "tools/decomp_dependencies.py",
        "tools/decomp_binary.py",
        "tools/decomp_verify.py",
        "tools/decomp_lint.py",
        "tools/decomp_status.py",
        "tools/decomp_campaigns.py",
    )
    if not isinstance(tools, Mapping) or set(tools) != set(expected_tools):
        raise ReplayError("the target context tool identity is invalid")
    _historical_files(root, campaign_head, tools, expected_tools)
    input_hashes = doctor_receipt.get("input_hashes")
    if (
        not isinstance(repository, Mapping)
        or set(repository) != {
            "function_map_sha256",
            "function_sizes_sha256",
            "report_sha256",
            "ledger_sha256",
            "lint_baseline_sha256",
        }
        or not isinstance(input_hashes, Mapping)
        or repository.get("function_map_sha256")
        != input_hashes.get("functions_map_sha256")
        or repository.get("function_sizes_sha256")
        != input_hashes.get("function_sizes_file_sha256")
        or repository.get("report_sha256")
        != input_hashes.get("current_report_sha256")
        or repository.get("ledger_sha256")
        != hashlib.sha256(
            _git_blob(root, campaign_head, LEDGER_RELATIVE, MAX_LEDGER_BYTES)
        ).hexdigest()
        or repository.get("lint_baseline_sha256")
        != hashlib.sha256(
            _git_blob(
                root,
                campaign_head,
                Path(".notes/lint-baseline.tsv"),
                MAX_TOOL_BYTES,
            )
        ).hexdigest()
    ):
        raise ReplayError("the target context repository identity is invalid")


def _brief_context(
    campaign: Mapping[str, object],
    target: str,
    root: Path,
    campaign_head: str,
) -> tuple[str, str]:
    """Validate the target brief and its historical context pack."""

    from tools import decomp_brief as brief
    from tools import decomp_context as context

    briefs = campaign.get("briefs")
    matches = [
        brief
        for brief in briefs
        if isinstance(briefs, list)
        and isinstance(brief, Mapping)
        and brief.get("target") == target
    ] if isinstance(briefs, list) else []
    if len(matches) != 1:
        raise ReplayError("the source campaign has no unique target brief")
    descriptor = matches[0]
    path, content = _descriptor_path(
        descriptor,
        root,
        maximum=MAX_BRIEF_BYTES,
        description="target brief",
    )
    expected_parent = root.resolve() / "build/decomp-cache/briefs"
    if path.parent != expected_parent or path.resolve().parent != expected_parent:
        raise ReplayError("the target brief path is not canonical")
    document = _strict_json(content, "target brief")
    content_hash = document.get("content_sha256")
    lane = campaign.get("lane")
    mode = campaign.get("mode")
    inputs = document.get("inputs")
    evidence = document.get("evidence")
    roles = document.get("roles")
    scout_findings = document.get("scout_findings")
    document_keys = {
        "schema",
        "lane",
        "target",
        "cache_key",
        "inputs",
        "roles",
        "evidence",
        "subsystem",
        "scout_findings",
        "content_sha256",
    }
    if (
        set(document) != document_keys
        or
        type(document.get("schema")) is not int
        or document.get("schema") != 1
        or document.get("lane") != lane
        or document.get("target") != target
        or not isinstance(content_hash, str)
        or brief._content_hash(document) != content_hash
        or descriptor.get("content_sha256") != content_hash
        or not isinstance(inputs, Mapping)
        or set(inputs) != {
            "head",
            "lane",
            "target",
            "target_hash",
            "subsystem",
            "report_hashes",
            "map_hash",
            "tool_hash",
            "history_hash",
            "blocker_hash",
            "artifact_hash",
            "dwarf_input",
            "doctor_receipt",
            "scout_reports",
            "production_mismatch",
            "context_inputs",
        }
        or inputs.get("head") != campaign_head
        or inputs.get("lane") != lane
        or inputs.get("target") != target
        or inputs.get("target_hash")
        != hashlib.sha256(target.encode("utf-8")).hexdigest()
        or document.get("cache_key") != _json_hash(inputs)
        or not isinstance(evidence, Mapping)
        or evidence.get("readiness") is not True
        or not isinstance(roles, Mapping)
        or not isinstance(scout_findings, list)
    ):
        raise ReplayError("the target brief identity is invalid")
    expected_name = (
        f"{lane}-{target.lower().replace('0x', '0x')}-"
        f"{document['cache_key']}.json"
    )
    if path.name != expected_name:
        raise ReplayError("the target brief path is not canonical")
    historical_hashes = {
        "map_hash": (Path("tools/Resources/functions_map.txt"), MAX_TOOL_BYTES),
        "history_hash": (LEDGER_RELATIVE, MAX_LEDGER_BYTES),
        "blocker_hash": (Path("tools/Resources/reconstruction-blockers.tsv"), MAX_TOOL_BYTES),
        "artifact_hash": (Path("tools/Resources/tool_artifacts.tsv"), MAX_TOOL_BYTES),
    }
    for key, (relative, maximum) in historical_hashes.items():
        if inputs.get(key) != hashlib.sha256(
            _git_blob(root, campaign_head, relative, maximum)
        ).hexdigest():
            raise ReplayError("the target brief historical input changed")
    if inputs.get("tool_hash") != _historical_combined_hash(
        root,
        campaign_head,
        brief.TOOL_INPUTS,
    ):
        raise ReplayError("the target brief tool identity changed")
    doctor_descriptor = inputs.get("doctor_receipt")
    if not isinstance(doctor_descriptor, Mapping):
        raise ReplayError("the target brief has no doctor receipt")
    doctor_receipt = _validate_historical_doctor(
        doctor_descriptor,
        campaign=campaign,
        target=target,
        root=root,
        campaign_head=campaign_head,
    )
    report_hashes = inputs.get("report_hashes")
    doctor_hashes = doctor_receipt.get("input_hashes")
    if (
        not isinstance(report_hashes, Mapping)
        or not isinstance(doctor_hashes, Mapping)
        or report_hashes.get("function")
        != doctor_hashes.get("current_report_sha256")
        or report_hashes.get("sizes")
        != doctor_hashes.get("function_sizes_file_sha256")
    ):
        raise ReplayError("the target brief report identity changed")
    scouts = inputs.get("scout_reports")
    role_rows = roles.get("scouts")
    if (
        not isinstance(scouts, list)
        or len(scouts) != 2
        or not isinstance(role_rows, list)
        or len(role_rows) != 2
    ):
        raise ReplayError("the target brief needs two read-only scouts")
    validated_scouts: list[dict[str, object]] = []
    for scout in scouts:
        if not isinstance(scout, Mapping) or set(scout) != {"path", "sha256", "content"}:
            raise ReplayError("a target brief scout report is invalid")
        scout_path_value = scout.get("path")
        scout_hash = scout.get("sha256")
        scout_content = scout.get("content")
        scout_path = Path(scout_path_value) if isinstance(scout_path_value, str) else None
        if (
            not isinstance(scout_path_value, str)
            or not scout_path_value
            or scout_path is None
            or not scout_path.is_absolute()
            or scout_path.as_posix() != scout_path_value
            or ".." in scout_path.parts
            or not isinstance(scout_hash, str)
            or HASH_RE.fullmatch(scout_hash) is None
            or not isinstance(scout_content, Mapping)
        ):
            raise ReplayError("a target brief scout report changed")
        try:
            scout_path.relative_to(root.resolve())
            content_document = brief._validated_scout_report_content(
                str(lane),
                target,
                dict(scout_content),
                doctor_descriptor,
            )
        except Exception as error:
            raise ReplayError("a target brief scout report is invalid") from error
        validated = {
            "path": scout_path_value,
            "sha256": scout_hash,
            "content": content_document,
        }
        validated_scouts.append(validated)
    contents = [row["content"] for row in validated_scouts]
    if (
        len({str(row.get("scout_id")) for row in contents}) != 2
        or {row.get("audit") for row in contents} != set(brief.SCOUT_AUDITS)
        or roles != brief.scout_roles(validated_scouts)
        or scout_findings
        != [
            {
                "scout_id": row["content"].get("scout_id"),
                "audit": row["content"].get("audit"),
                "report_sha256": row.get("sha256"),
                "findings": row["content"].get("findings"),
            }
            for row in validated_scouts
        ]
    ):
        raise ReplayError("the target brief scout evidence is inconsistent")
    _validate_historical_context_inputs(
        inputs.get("context_inputs"),
        root=root,
        campaign_head=campaign_head,
        doctor_receipt=doctor_receipt,
    )
    pack = evidence.get("context_pack")
    if not isinstance(pack, Mapping):
        raise ReplayError("the target context pack size is invalid")
    size = pack.get("size")
    expected_pack_keys = {
        "schema",
        "target",
        "abi",
        "size",
        "instructions",
        "control_flow",
        "memory_operands",
        "call_neighbors",
        "accepted_sources",
        "completeness",
        "bindings",
        "limits",
        "content_sha256",
    }
    if type(size) is not int or set(pack) != expected_pack_keys:
        raise ReplayError("the target context pack size is invalid")
    try:
        context.validate_context_pack(pack, target=target, size=size)
    except Exception as error:
        raise ReplayError("the target context pack is invalid") from error
    pack_hash = pack.get("content_sha256")
    bindings = pack.get("bindings")
    source_artifacts = doctor_descriptor.get("source_artifacts")
    expected_limits = {
        "instructions": context.MAX_INSTRUCTIONS,
        "basic_blocks": context.MAX_BASIC_BLOCKS,
        "edges": context.MAX_EDGES,
        "memory_operands": context.MAX_MEMORY_OPERANDS,
        "call_neighbors": context.MAX_CALL_NODES,
        "accepted_sources": context.MAX_ACCEPTED_SOURCES,
        "text_chars": context.MAX_TEXT_CHARS,
        "source_excerpt_chars": context.MAX_SOURCE_EXCERPT_CHARS,
        "bytes": context.MAX_CONTEXT_BYTES,
    }
    expected_evidence_sources = {
        str(kind): {
            "path": artifact.get("path"),
            "sha256": artifact.get("sha256"),
        }
        for kind, artifact in sorted(source_artifacts.items())
        if isinstance(source_artifacts, Mapping)
        and isinstance(artifact, Mapping)
    }
    if (
        not isinstance(pack_hash, str)
        or not isinstance(bindings, Mapping)
        or set(bindings) != {
            "doctor_receipt",
            "artifacts",
            "evidence_sources",
            "context_inputs",
            "decoder",
        }
        or bindings.get("doctor_receipt")
        != {
            "path": Path(str(doctor_descriptor["path"])).resolve().relative_to(root.resolve()).as_posix(),
            "sha256": doctor_descriptor.get("sha256"),
            "receipt_id": doctor_descriptor.get("receipt_id"),
        }
        or bindings.get("artifacts") != doctor_descriptor.get("source_artifacts")
        or bindings.get("evidence_sources") != expected_evidence_sources
        or bindings.get("context_inputs") != inputs.get("context_inputs")
        or not isinstance(inputs.get("context_inputs"), Mapping)
        or bindings.get("decoder") != inputs["context_inputs"].get("decoder")
        or pack.get("limits") != expected_limits
    ):
        raise ReplayError("the target context pack identity is invalid")
    expected_descriptor = {
        "path": str(path),
        "sha256": hashlib.sha256(content).hexdigest(),
        "cache_key": document.get("cache_key"),
        "lane": lane,
        "target": target,
        "subsystem": inputs.get("subsystem"),
        "head": campaign_head,
        "content_sha256": content_hash,
        "dwarf_input": inputs.get("dwarf_input"),
        "doctor_receipt": doctor_descriptor,
        "scout_reports": scouts,
        "research_route": (
            evidence.get("research_route") if lane == "research" else None
        ),
    }
    if dict(descriptor) != expected_descriptor:
        raise ReplayError("the committed target brief descriptor changed")
    return content_hash, pack_hash


def _delivery_evidence(
    campaign: dict[str, object],
    records: Sequence[dict[str, object]],
    finalize_receipt: Mapping[str, object],
    root: Path,
    current_head: str,
    origin_head: str,
    expected_tool_identity: Mapping[str, object],
) -> tuple[str, str]:
    """Validate the accepted-through-pushed delivery chain."""

    from tools import decomp_campaigns as campaigns
    from tools import decomp_oracle as oracle

    campaign_records = _campaign_records(records, campaign.get("campaign_id"))
    delivery_rows = _ordered_delivery_rows(campaign, campaign_records)
    source_commit = delivery_rows[-1].get("commit")
    base_commit = delivery_rows[2].get("base_commit")
    receipt_path_value = delivery_rows[2].get("delivery_receipt_path")
    if (
        not isinstance(source_commit, str)
        or COMMIT_RE.fullmatch(source_commit) is None
        or not isinstance(base_commit, str)
        or COMMIT_RE.fullmatch(base_commit) is None
        or not isinstance(receipt_path_value, str)
        or any(row.get("commit") != source_commit for row in delivery_rows[2:])
        or any(
            row.get("delivery_receipt_path") != receipt_path_value
            for row in delivery_rows[2:]
        )
    ):
        raise ReplayError("the source campaign delivery identity is invalid")
    receipt_path = Path(receipt_path_value)
    expected_parent = (
        root.resolve()
        / "build/decomp-cache/delivery"
        / str(campaign.get("campaign_id"))
    )
    if (
        not receipt_path.is_absolute()
        or receipt_path.parent != expected_parent
        or receipt_path.resolve().parent != expected_parent
        or HASH_RE.fullmatch(receipt_path.stem) is None
    ):
        raise ReplayError("the source campaign delivery receipt path is invalid")
    content = _regular_bytes(
        receipt_path,
        campaigns.MAX_DELIVERY_RECEIPT_BYTES,
        "delivery receipt",
    )
    receipt = _strict_json(content, "delivery receipt")
    content_hash = receipt.get("content_sha256")
    if (
        type(receipt.get("schema_version")) is not int
        or receipt.get("schema_version") != campaigns.SCHEMA_VERSION
        or receipt.get("receipt_version") != campaigns.DELIVERY_RECEIPT_VERSION
        or receipt.get("record_type") != "delivery-receipt"
        or receipt.get("status") != "passed"
        or not isinstance(content_hash, str)
        or receipt_path.name != f"{content_hash}.json"
        or campaigns._delivery_receipt_hash(receipt) != content_hash
        or receipt.get("campaign_id") != campaign.get("campaign_id")
        or receipt.get("campaign_fingerprint")
        != campaigns._record_fingerprint(campaign)
        or receipt.get("campaign_record_sha256") != campaigns._snapshot_hash(campaign)
        or receipt.get("source_commit") != source_commit
        or receipt.get("head") != source_commit
        or receipt.get("base_commit") != base_commit
    ):
        raise ReplayError("the source campaign delivery receipt is invalid")
    finalized_hash = finalize_receipt.get("receipt_sha256")
    _validate_delivery_rows(
        campaign,
        delivery_rows,
        finalized_hash=finalized_hash,
        delivery_hash=content_hash,
        source_commit=source_commit,
        base_commit=base_commit,
        receipt_path=receipt_path_value,
        receipt_created_at=receipt.get("created_at"),
    )
    head_ledger = _git_blob(root, current_head, LEDGER_RELATIVE, MAX_LEDGER_BYTES)
    source_ledger = _git_blob(root, source_commit, LEDGER_RELATIVE, MAX_LEDGER_BYTES)
    _git_blob(root, base_commit, LEDGER_RELATIVE, MAX_LEDGER_BYTES)
    if not source_ledger.endswith(b"\n") or not head_ledger.startswith(source_ledger):
        raise ReplayError("the source campaign ledger history is not durable")
    try:
        campaigns._verify_campaign_in_commit(
            root,
            root / LEDGER_RELATIVE,
            campaign,
            source_commit,
            base_commit,
        )
        resolved = campaigns._verify_finalized_campaign_tree(
            root,
            root / LEDGER_RELATIVE,
            campaign,
            source_commit,
            base_commit,
            receipt=finalize_receipt,
        )
        if receipt.get("resolved_paths") != resolved:
            raise ValueError
        accepted_review = campaigns._accepted_review_from_records(
            list(campaign_records),
            campaign,
            finalize_receipt,
            root=root,
        )
        if receipt.get("accepted_review") != accepted_review:
            raise ValueError
        validation = receipt.get("validation")
        if not isinstance(validation, Mapping):
            raise ValueError
        campaigns._validate_delivery_command_results(validation, campaign, root)
        key_payload = finalize_receipt.get("key_payload")
        if (
            not isinstance(key_payload, Mapping)
            or validation.get("inputs") != key_payload.get("inputs")
            or receipt.get("validation_tool_identity")
            != dict(expected_tool_identity)
        ):
            raise ValueError
        _historical_validation_identity(
            root, source_commit, receipt.get("validation_tool_identity")
        )
        leaf = campaigns._leaf_oracle_artifact(finalize_receipt)
        if leaf is None:
            raise ValueError
        finalized_document, finalized_artifact = leaf
        finalized_replay = oracle.replay_identity(finalized_document)
        proof = validation.get("leaf_oracle")
        if not isinstance(proof, Mapping) or set(proof) != {
            "scope",
            "finalized",
            "integrated",
        }:
            raise ValueError
        expected_finalized = {
            "artifact": finalized_artifact,
            "content_sha256": finalized_document.get("content_sha256"),
            "replay_sha256": campaigns._snapshot_hash(finalized_replay),
        }
        if (
            proof.get("scope") != finalized_replay.get("scope")
            or proof.get("finalized") != expected_finalized
        ):
            raise ValueError
        integrated = proof.get("integrated")
        if not isinstance(integrated, Mapping) or set(integrated) != {
            "artifact",
            "content_sha256",
            "replay_sha256",
        }:
            raise ValueError
        artifact = integrated.get("artifact")
        if not isinstance(artifact, Mapping) or set(artifact) != {"path", "sha256"}:
            raise ValueError
        integrated_path = Path(str(artifact.get("path")))
        integrated_hash = integrated.get("content_sha256")
        expected_oracle_root = root.resolve() / "build/decomp-cache/oracle"
        if (
            not isinstance(integrated_hash, str)
            or integrated_path.parent != expected_oracle_root
            or integrated_path.name != f"{integrated_hash}.json"
        ):
            raise ValueError
        integrated_content = _regular_bytes(
            integrated_path,
            oracle.MAX_RECEIPT_BYTES,
            "integrated leaf oracle receipt",
        )
        if hashlib.sha256(integrated_content).hexdigest() != artifact.get("sha256"):
            raise ValueError
        integrated_document = oracle.validate_receipt(
            integrated_path,
            root=root,
            current=False,
            require_pass=False,
            expected_scope=list(finalized_replay["scope"]),
        )
        oracle.validate_document(
            integrated_document,
            require_pass=True,
            expected_scope=list(finalized_replay["scope"]),
        )
        integrated_replay = oracle.replay_identity(integrated_document)
        if (
            integrated_document.get("content_sha256") != integrated_hash
            or integrated.get("replay_sha256")
            != campaigns._snapshot_hash(integrated_replay)
            or integrated_replay != finalized_replay
        ):
            raise ValueError
        artifacts = validation.get("artifacts")
        expected_paths = [
            root.resolve() / "build/toy2.exe",
            root.resolve() / "build/toy2.pdb",
            root.resolve() / "build/decomp-current-report.json",
            root.resolve() / "build/decomp-current-report.json.provenance.json",
            root.resolve() / "build/decomp-current-data-report.json",
            root.resolve() / "build/decomp-current-data-report.json.provenance.json",
            integrated_path,
        ]
        if not isinstance(artifacts, list) or len(artifacts) != len(expected_paths):
            raise ValueError
        for saved, expected_path in zip(artifacts, expected_paths):
            if (
                not isinstance(saved, Mapping)
                or set(saved) != {"path", "sha256"}
                or saved.get("path") != str(expected_path)
                or HASH_RE.fullmatch(str(saved.get("sha256"))) is None
            ):
                raise ValueError
        if dict(artifact) != dict(artifacts[-1]):
            raise ValueError
    except Exception as error:
        raise ReplayError("the source campaign delivery evidence is invalid") from error
    _validate_delivery_ancestry(
        root,
        campaign.get("campaign_head"),
        base_commit,
        source_commit,
        current_head,
        origin_head,
    )
    return source_commit, content_hash


def _ordered_delivery_rows(
    campaign: Mapping[str, object],
    records: Sequence[dict[str, object]],
) -> list[dict[str, object]]:
    """Return exactly one canonical five-row delivery chain."""

    rows = [row for row in records if row.get("record_type") == "delivery"]
    expected = ["staged", "accepted", "integrated", "committed", "pushed"]
    if [row.get("status") for row in rows] != expected:
        raise ReplayError("the source campaign delivery chain is incomplete")
    if any(row.get("campaign_id") != campaign.get("campaign_id") for row in rows):
        raise ReplayError("the source campaign delivery chain is invalid")
    return rows


def _validate_delivery_rows(
    campaign: Mapping[str, object],
    delivery_rows: Sequence[Mapping[str, object]],
    *,
    finalized_hash: object,
    delivery_hash: object,
    source_commit: object,
    base_commit: object,
    receipt_path: object,
    receipt_created_at: object,
) -> None:
    """Validate exact row schemas, shared identities, IDs, and chronology."""

    from tools import decomp_campaigns as campaigns

    expected = ["staged", "accepted", "integrated", "committed", "pushed"]
    if [row.get("status") for row in delivery_rows] != expected:
        raise ReplayError("the source campaign delivery chain is incomplete")
    if (
        not isinstance(source_commit, str)
        or COMMIT_RE.fullmatch(source_commit) is None
        or not isinstance(base_commit, str)
        or COMMIT_RE.fullmatch(base_commit) is None
        or not isinstance(receipt_path, str)
        or not isinstance(delivery_hash, str)
        or HASH_RE.fullmatch(delivery_hash) is None
        or any(row.get("commit") != source_commit for row in delivery_rows[2:])
        or any(row.get("base_commit") != base_commit for row in delivery_rows[2:])
        or any(
            row.get("delivery_receipt_path") != receipt_path
            for row in delivery_rows[2:]
        )
        or any(row.get("receipt_sha256") != delivery_hash for row in delivery_rows[2:])
    ):
        raise ReplayError("the source campaign delivery identity is invalid")
    phase_names = {
        "staged": "staged",
        "accepted": "accepted",
        "integrated": "integrated",
        "committed": "commit",
        "pushed": "push",
    }
    delivery_ids: set[str] = set()
    delivery_times: list[datetime] = []
    for row, status_name in zip(delivery_rows, expected):
        phase = phase_names[status_name]
        timestamp = row.get("timestamp")
        expected_keys = set(DELIVERY_ROW_KEYS)
        if status_name == "accepted":
            expected_keys.update({"accepted_review", "accepted_claims"})
        delivery_id = row.get("delivery_id")
        try:
            parsed_delivery_id = uuid.UUID(str(delivery_id))
        except (ValueError, AttributeError) as error:
            raise ReplayError("the source campaign delivery ID is invalid") from error
        if (
            set(row) != expected_keys
            or type(row.get("schema_version")) is not int
            or row.get("schema_version") != campaigns.SCHEMA_VERSION
            or row.get("record_type") != "delivery"
            or row.get("campaign_id") != campaign.get("campaign_id")
            or row.get("status") != status_name
            or row.get("phase") != phase
            or not isinstance(timestamp, str)
            or row.get("phase_timestamps") != {phase: timestamp}
            or row.get("mode") != campaign.get("mode")
            or row.get("lane") != campaigns._campaign_lane(campaign)
            or row.get("addresses") != campaign.get("addresses")
            or row.get("resource") != campaign.get("resource")
            or row.get("artifact_sha256") is not None
            or not isinstance(delivery_id, str)
            or str(parsed_delivery_id) != delivery_id
            or parsed_delivery_id.version != 4
            or delivery_id in delivery_ids
            or not isinstance(row.get("note"), str)
        ):
            raise ReplayError("the source campaign delivery row is invalid")
        delivery_ids.add(delivery_id)
        delivery_times.append(_utc_time(timestamp, "delivery"))
        if status_name in {"staged", "accepted"}:
            if (
                row.get("receipt_sha256") != finalized_hash
                or row.get("base_commit") is not None
                or row.get("commit") is not None
                or row.get("delivery_receipt_path") is not None
            ):
                raise ReplayError("the source campaign precommit delivery row is invalid")
        elif (
            row.get("receipt_sha256") != delivery_hash
            or row.get("base_commit") != base_commit
            or row.get("commit") != source_commit
            or row.get("delivery_receipt_path") != receipt_path
        ):
            raise ReplayError("the source campaign postcommit delivery row is invalid")
    campaign_end = _utc_time(campaign.get("ended_at"), "campaign end")
    delivery_receipt_time = _utc_time(receipt_created_at, "delivery receipt")
    if (
        any(value < campaign_end for value in delivery_times)
        or delivery_times != sorted(delivery_times)
        or not delivery_times[1] <= delivery_receipt_time <= delivery_times[2]
    ):
        raise ReplayError("the source campaign delivery times are invalid")


def _validate_delivery_ancestry(
    root: Path,
    campaign_head: object,
    base_commit: object,
    source_commit: object,
    current_head: object,
    origin_head: object,
) -> None:
    """Require one exact first-parent delivery chain on both current tips."""

    if (
        not isinstance(campaign_head, str)
        or COMMIT_RE.fullmatch(campaign_head) is None
        or not isinstance(base_commit, str)
        or COMMIT_RE.fullmatch(base_commit) is None
        or not isinstance(source_commit, str)
        or COMMIT_RE.fullmatch(source_commit) is None
        or not isinstance(current_head, str)
        or COMMIT_RE.fullmatch(current_head) is None
        or not isinstance(origin_head, str)
        or COMMIT_RE.fullmatch(origin_head) is None
    ):
        raise ReplayError("the source campaign commit is not on the delivery branch")
    for revision in (campaign_head, base_commit, source_commit):
        _validate_historical_private_storage(root, revision)
    if (
        _git_first_parent(root, source_commit) != base_commit
        or not _git_ancestor(root, campaign_head, base_commit)
        or not _git_ancestor(root, base_commit, source_commit)
        or not _git_ancestor(root, source_commit, current_head)
        or not _git_ancestor(root, source_commit, origin_head)
    ):
        raise ReplayError("the source campaign commit is not on the delivery branch")


def _validate_historical_private_storage(root: Path, revision: str) -> None:
    """Reject a historical commit that tracks private replay data or drops its ignore."""

    ignore = _git_blob(root, revision, Path(".gitignore"), MAX_TOOL_BYTES)
    try:
        lines = ignore.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ReplayError("the historical private replay ignore file is invalid") from error
    tree_ok, tree_has_path = _command_has_output(
        root,
        [
            "git",
            "ls-tree",
            "-r",
            "-z",
            "--name-only",
            revision,
            "--",
            PRIVATE_RELATIVE.as_posix(),
        ],
    )
    if not tree_ok or tree_has_path or "/.decomp-replay/" not in lines:
        raise ReplayError("historical private replay storage is not excluded")


def _same_artifact_descriptor(
    session_value: object,
    campaign_value: object,
    root: Path,
) -> bool:
    """Compare one session descriptor with one accepted campaign artifact."""

    if not isinstance(session_value, Mapping) or not isinstance(campaign_value, Mapping):
        return False
    session_path_value = session_value.get("path")
    campaign_path_value = campaign_value.get("path")
    if not isinstance(session_path_value, str) or not isinstance(campaign_path_value, str):
        return False
    session_path = Path(session_path_value)
    if not session_path.is_absolute():
        session_path = root.resolve() / session_path
    campaign_path = Path(campaign_path_value)
    return (
        campaign_path.is_absolute()
        and session_path.absolute() == campaign_path.absolute()
        and session_path.resolve() == campaign_path.resolve()
        and session_value.get("sha256") == campaign_value.get("sha256")
    )


def _preflight_campaign_receipts(
    campaign: Mapping[str, object], root: Path
) -> None:
    """Require canonical bounded campaign and finalization receipts."""

    from tools import decomp_campaigns as campaigns

    finalize = campaign.get("finalize_receipt")
    record = campaign.get("campaign_record_receipt")
    if not isinstance(finalize, Mapping) or not isinstance(record, Mapping):
        raise ReplayError("the source campaign receipt identity is invalid")
    key = finalize.get("receipt_key")
    finalize_path_value = finalize.get("path")
    record_hash = record.get("content_sha256")
    record_path_value = record.get("path")
    if (
        not isinstance(key, str)
        or HASH_RE.fullmatch(key) is None
        or not isinstance(finalize_path_value, str)
        or not isinstance(record_hash, str)
        or HASH_RE.fullmatch(record_hash) is None
        or not isinstance(record_path_value, str)
    ):
        raise ReplayError("the source campaign receipt identity is invalid")
    finalize_path = Path(finalize_path_value)
    record_path = Path(record_path_value)
    expected_root = root.resolve() / "build/decomp-cache/finalize"
    expected_finalize = expected_root / f"{key}.json"
    expected_record = expected_root / "campaign-records" / f"{record_hash}.json"
    if (
        not finalize_path.is_absolute()
        or finalize_path.absolute() != expected_finalize
        or finalize_path.resolve() != expected_finalize
        or not record_path.is_absolute()
        or record_path.absolute() != expected_record
        or record_path.resolve() != expected_record
    ):
        raise ReplayError("the source campaign receipt path is invalid")
    finalize_content = _regular_bytes(
        expected_finalize,
        campaigns.MAX_FINALIZE_RECEIPT_BYTES,
        "finalization receipt",
    )
    record_content = _regular_bytes(
        expected_record,
        campaigns.MAX_CAMPAIGN_RECORD_RECEIPT_BYTES,
        "campaign record receipt",
    )
    if (
        hashlib.sha256(finalize_content).hexdigest() != finalize.get("file_sha256")
        or hashlib.sha256(record_content).hexdigest() != record.get("file_sha256")
    ):
        raise ReplayError("the source campaign receipt content changed")


def _validate_trajectory_chronology(
    session: Mapping[str, object],
    trajectory: Mapping[str, object],
    campaign: Mapping[str, object],
    finalize_receipt: Mapping[str, object],
    root: Path,
) -> None:
    """Require an internally consistent campaign and experiment order."""

    from tools import decomp_experiment as experiment

    started = _utc_time(campaign.get("started_at"), "campaign start")
    created = _utc_time(session.get("created_at"), "experiment creation")
    activated = _utc_time(session.get("activated_at"), "experiment activation")
    if not started <= created <= activated:
        raise ReplayError("the experiment session is outside its campaign")
    previous = activated
    rows = trajectory.get("trials")
    if not isinstance(rows, list) or len(rows) not in {2, MAX_TRIALS}:
        raise ReplayError("the experiment trajectory chronology is invalid")
    for row in rows:
        if not isinstance(row, Mapping):
            raise ReplayError("the experiment trajectory chronology is invalid")
        pending_descriptor = row.get("pending_receipt")
        terminal_descriptor = row.get("terminal_receipt")
        if not isinstance(pending_descriptor, Mapping) or not isinstance(
            terminal_descriptor, Mapping
        ):
            raise ReplayError("the experiment trajectory chronology is invalid")
        pending_path = root / str(pending_descriptor.get("path", ""))
        terminal_path = root / str(terminal_descriptor.get("path", ""))
        try:
            pending = experiment.read_receipt(
                pending_path, kind="experiment-trial-pending"
            )
            terminal = experiment.read_receipt(
                terminal_path, kind="experiment-trial"
            )
        except Exception as error:
            raise ReplayError("the experiment trajectory chronology is invalid") from error
        reserved = _utc_time(pending.get("reserved_at"), "trial reservation")
        completed = _utc_time(terminal.get("completed_at"), "trial completion")
        if not previous <= reserved <= completed:
            raise ReplayError("the experiment trial times are out of order")
        previous = completed
    phases = finalize_receipt.get("phase_timestamps")
    finalization_started = (
        phases.get("finalization-started") if isinstance(phases, Mapping) else None
    )
    finalization_time = _utc_time(finalization_started, "finalization start")
    receipt_time = _utc_time(finalize_receipt.get("created_at"), "finalization receipt")
    ended = _utc_time(campaign.get("ended_at"), "campaign end")
    if not previous <= finalization_time <= receipt_time <= ended:
        raise ReplayError("the finalization times are out of order")


def _validate_runtime_file_descriptor(value: object) -> None:
    if not isinstance(value, Mapping) or set(value) != {"path", "sha256"}:
        raise ReplayError("a finalization runtime descriptor is invalid")
    path = value.get("path")
    saved_hash = value.get("sha256")
    if (path is None) != (saved_hash is None):
        raise ReplayError("a finalization runtime descriptor is invalid")
    if path is not None and (
        not isinstance(path, str)
        or not Path(path).is_absolute()
        or not isinstance(saved_hash, str)
        or HASH_RE.fullmatch(saved_hash) is None
    ):
        raise ReplayError("a finalization runtime descriptor is invalid")


def _validate_finalization_inputs(
    root: Path,
    campaign_head: str,
    value: object,
) -> None:
    """Validate the exact standard finalizer input schema and commitments."""

    if not isinstance(value, Mapping) or set(value) != {
        "standard_finalize_version",
        "decoder",
        "python_runtime",
        "files",
        "file_sizes",
        "commands",
    }:
        raise ReplayError("the source finalization input identity is invalid")
    if type(value.get("standard_finalize_version")) is not int or value.get(
        "standard_finalize_version"
    ) != 3:
        raise ReplayError("the source finalization input version is invalid")
    files = value.get("files")
    sizes = value.get("file_sizes")
    if (
        not isinstance(files, Mapping)
        or not isinstance(sizes, Mapping)
        or set(files) != set(sizes)
    ):
        raise ReplayError("the source finalization file identity is invalid")
    tracked = {str((root / name).resolve()) for name in HISTORICAL_FINALIZE_FILES}
    required_local = {
        str((root / "build/build.ninja").resolve()),
        str((root / "build/CMakeCache.txt").resolve()),
        str((root / "original/toy2.exe").resolve()),
    }
    optional_local = {str((root / "build/Makefile").resolve())}
    if not tracked | required_local <= set(files) or set(files) - (
        tracked | required_local | optional_local
    ):
        raise ReplayError("the source finalization file set is invalid")
    for path, saved_hash in files.items():
        byte_count = sizes.get(path)
        if (
            not isinstance(path, str)
            or not isinstance(saved_hash, str)
            or HASH_RE.fullmatch(saved_hash) is None
            or type(byte_count) is not int
            or byte_count <= 0
            or byte_count > MAX_LEDGER_BYTES
        ):
            raise ReplayError("a source finalization file identity is invalid")
    for name in HISTORICAL_FINALIZE_FILES:
        path = str((root / name).resolve())
        content = _git_blob(root, campaign_head, Path(name), MAX_TOOL_BYTES)
        if (
            files.get(path) != hashlib.sha256(content).hexdigest()
            or sizes.get(path) != len(content)
        ):
            raise ReplayError("a historical finalization dependency changed")
    retail_path = str((root / "original/toy2.exe").resolve())
    if files.get(retail_path) != _historical_retail_hash(root, campaign_head):
        raise ReplayError("the finalization retail identity is invalid")
    decoder = value.get("decoder")
    if not isinstance(decoder, Mapping) or set(decoder) != {
        "engine",
        "version",
        "api_version",
        "architecture",
        "mode",
        "detail",
        "module",
        "library",
    }:
        raise ReplayError("the finalization decoder identity is invalid")
    if (
        decoder.get("engine") not in {"capstone", "unavailable"}
        or decoder.get("architecture") != "x86"
        or type(decoder.get("mode")) is not int
        or decoder.get("mode") != 32
        or type(decoder.get("detail")) is not bool
        or (
            decoder.get("api_version") is not None
            and (
                not isinstance(decoder.get("api_version"), list)
                or any(type(item) is not int for item in decoder["api_version"])
            )
        )
        or (
            decoder.get("version") is not None
            and not isinstance(decoder.get("version"), str)
        )
    ):
        raise ReplayError("the finalization decoder identity is invalid")
    _validate_runtime_file_descriptor(decoder.get("module"))
    _validate_runtime_file_descriptor(decoder.get("library"))
    runtime = value.get("python_runtime")
    if not isinstance(runtime, Mapping) or set(runtime) != {
        "implementation",
        "version",
        "executable",
    }:
        raise ReplayError("the finalization Python identity is invalid")
    version = runtime.get("version")
    executable = runtime.get("executable")
    if (
        runtime.get("implementation") not in {"cpython", "pypy"}
        or not isinstance(version, list)
        or len(version) != 3
        or any(type(item) is not int or item < 0 for item in version)
        or not isinstance(executable, Mapping)
        or set(executable) != {"path", "sha256"}
        or not isinstance(executable.get("path"), str)
        or not Path(str(executable.get("path"))).is_absolute()
        or not isinstance(executable.get("sha256"), str)
        or HASH_RE.fullmatch(str(executable.get("sha256"))) is None
    ):
        raise ReplayError("the finalization Python identity is invalid")
    commands = value.get("commands")
    if not isinstance(commands, Mapping) or set(commands) != {
        "build",
        "code_report",
        "data_report",
        "source_scan",
        "validation",
    }:
        raise ReplayError("the source finalization command plan is invalid")
    build = commands.get("build")
    code_report = commands.get("code_report")
    data_report = commands.get("data_report")
    expected_code = [[
        "reccmp-reccmp",
        "--target",
        "TOY2",
        "--silent",
        "--no-color",
        "--json",
        "decomp-current-report.json",
    ]]
    expected_data_tail = [
        "tools/generate-decomp-data-report.py",
        "--original",
        "original/toy2.exe",
        "--recompiled",
        "build/toy2.exe",
        "--pdb",
        "build/toy2.pdb",
        "--source-root",
        "src",
        "--output",
        "build/decomp-current-data-report.json",
    ]
    build_command = build[0] if isinstance(build, list) and len(build) == 2 else None
    build_tail_valid = (
        isinstance(build_command, list)
        and build_command[:4] == ["cmake", "--build", "build", "--"]
        and len(build_command) == 5
        and (
            build_command[4] == "-NOLOGO"
            or (
                isinstance(build_command[4], str)
                and re.fullmatch(r"-j[1-9][0-9]*", build_command[4]) is not None
            )
        )
    )
    if (
        not build_tail_valid
        or build[1] != ["reccmp-project", "detect", "--what", "recompiled"]
        or code_report != expected_code
        or not isinstance(data_report, list)
        or len(data_report) != 1
        or not isinstance(data_report[0], list)
        or data_report[0] != [executable["path"], *expected_data_tail]
        or commands.get("source_scan") != []
        or commands.get("validation") != []
    ):
        raise ReplayError("the source finalization command plan is invalid")


def _verified_campaign_evidence(
    session: Mapping[str, object],
    target: str,
    expected_best: Mapping[str, object],
    expected_source_snapshot: Mapping[str, object],
    expected_comparison_identity: Mapping[str, object],
    root: Path,
    current_head: str,
    origin_head: str,
    trajectory: Mapping[str, object],
    assume_private_lock: bool,
    records: Sequence[dict[str, object]] | None = None,
) -> tuple[dict[str, object], dict[str, object]]:
    """Validate all durable source-campaign evidence for one session."""

    from tools import decomp_campaigns as campaigns
    from tools import decomp_experiment as experiment
    from tools.decomp_impact import impact_artifact

    bound = session.get("campaign")
    if not isinstance(bound, Mapping):
        raise ReplayError("the experiment session has no source campaign")
    campaign_id = bound.get("campaign_id")
    if records is None:
        records = _ledger_records(root)
    matches = [
        row
        for row in _campaign_records(records, campaign_id)
        if row.get("record_type", "campaign") == "campaign"
    ]
    if len(matches) != 1:
        raise ReplayError("the experiment source campaign is not unique")
    campaign = matches[0]
    active = campaign.get("active_addresses")
    campaign_lane = campaigns._campaign_lane(campaign)
    campaign_head = campaign.get("campaign_head")
    if (
        type(campaign.get("schema_version")) is not int
        or campaign.get("schema_version") != campaigns.SCHEMA_VERSION
        or campaign.get("result") != "source"
        or campaign.get("mode") not in {"coverage", "refinement"}
        or (campaign_lane, campaign.get("mode")) not in ELIGIBLE_LANE_MODES
        or campaign.get("impact_review_required") is not True
        or campaign.get("leaf_oracle_required") is not True
        or not isinstance(active, list)
        or target not in active
        or bound.get("campaign_id") != campaign_id
        or bound.get("mode") != campaign.get("mode")
        or bound.get("lane") != campaign_lane
        or bound.get("active_addresses") != active
        or bound.get("campaign_head") != campaign.get("campaign_head")
        or session.get("head") != campaign.get("campaign_head")
        or not isinstance(campaign_head, str)
        or COMMIT_RE.fullmatch(campaign_head) is None
        or not _git_ancestor(root, campaign_head, current_head)
        or not _git_ancestor(root, campaign_head, origin_head)
    ):
        raise ReplayError("the experiment session has no eligible source campaign")
    session_tools = session.get("tool_identity")
    if (
        not isinstance(session_tools, Mapping)
        or set(session_tools) != set(experiment.TOOL_FILES)
    ):
        raise ReplayError("the experiment session tool identity is invalid")
    _historical_files(
        root,
        campaign_head,
        session_tools,
        [name for name in experiment.TOOL_FILES if name != "reccmp-user.yml"],
    )
    comparison_tools = expected_comparison_identity.get("validation_tools")
    _historical_validation_identity(root, campaign_head, comparison_tools)
    if (
        not isinstance(comparison_tools, Mapping)
        or not isinstance(comparison_tools.get("reccmp_user"), Mapping)
        or session_tools.get("reccmp-user.yml")
        != comparison_tools["reccmp_user"].get("config_sha256")
    ):
        raise ReplayError("the experiment session reccmp identity changed")
    briefs = campaign.get("briefs")
    target_briefs = [
        row
        for row in briefs
        if isinstance(briefs, list)
        and isinstance(row, Mapping)
        and row.get("target") == target
    ] if isinstance(briefs, list) else []
    if len(target_briefs) != 1:
        raise ReplayError("the source campaign has no unique target brief")
    target_brief = target_briefs[0]
    if (
        not _same_artifact_descriptor(session.get("brief"), target_brief, root)
        or not _same_artifact_descriptor(
            session.get("doctor_receipt"), target_brief.get("doctor_receipt"), root
        )
    ):
        raise ReplayError("the experiment preflight evidence differs from the campaign")
    _preflight_campaign_receipts(campaign, root)
    try:
        campaigns._validate_campaign_record_receipt(campaign)
        finalize_receipt = campaigns._campaign_finalize_receipt(
            campaign, validate_artifacts=True
        )
        key_payload = finalize_receipt.get("key_payload")
        commitment = campaign.get("replay_experiment")
        if (
            finalize_receipt.get("receipt_version")
            != campaigns.FINALIZE_RECEIPT_VERSION
            or not isinstance(key_payload, Mapping)
            or not isinstance(commitment, Mapping)
            or key_payload.get("replay_experiment") != commitment
        ):
            raise ValueError
        seal = experiment.trajectory_from_binding(
            commitment,
            root=root,
            assume_locked=assume_private_lock,
        )
        if seal.get("trajectory") != trajectory:
            raise ValueError
        impact = impact_artifact(finalize_receipt)
        if impact is None:
            raise ValueError
        impact_pack, _impact_artifact = impact
        leaf = campaigns._leaf_oracle_artifact(finalize_receipt)
        if leaf is None:
            raise ValueError
        leaf_document, _leaf_artifact = leaf
    except Exception as error:
        raise ReplayError("the source campaign finalization evidence is invalid") from error
    key_payload = finalize_receipt.get("key_payload")
    if (
        not isinstance(key_payload, Mapping)
        or key_payload.get("source_root") != str(root.resolve())
        or key_payload.get("campaign_ledger_relative")
        != LEDGER_RELATIVE.as_posix()
        or key_payload.get("campaign_ledger_base_commit")
        != campaign.get("campaign_head")
        or key_payload.get("briefs") != briefs
        or key_payload.get("active_addresses") != active
        or key_payload.get("campaign_id") != campaign_id
        or key_payload.get("campaign_head") != campaign.get("campaign_head")
        or key_payload.get("mode") != campaign.get("mode")
        or key_payload.get("lane") != campaign_lane
        or key_payload.get("source_worktree_snapshot")
        != dict(expected_source_snapshot)
        or key_payload.get("source_worktree_sha256")
        != campaigns._snapshot_hash(dict(expected_source_snapshot))
    ):
        raise ReplayError("the final source differs from the experiment best")
    _validate_trajectory_chronology(
        session, trajectory, campaign, finalize_receipt, root
    )
    finalization_inputs = key_payload.get("inputs")
    _validate_finalization_inputs(root, campaign_head, finalization_inputs)
    finalization_files = (
        finalization_inputs.get("files")
        if isinstance(finalization_inputs, Mapping)
        else None
    )
    _historical_files(
        root,
        campaign_head,
        finalization_files,
        HISTORICAL_FINALIZE_FILES,
        absolute_keys=True,
    )
    _, context_hash = _brief_context(
        campaign,
        target,
        root,
        campaign_head,
    )
    changes = impact_pack.get("comparison_changes")
    target_changes = [
        row
        for row in changes
        if isinstance(changes, list)
        and isinstance(row, Mapping)
        and row.get("address") == target
        and row.get("target") is True
    ] if isinstance(changes, list) else []
    if len(target_changes) != 1:
        raise ReplayError("the final impact pack has no unique target result")
    after = target_changes[0].get("after")
    if (
        not isinstance(after, Mapping)
        or target_changes[0].get("classification")
        != SOURCE_IMPACT_CLASSIFICATIONS.get(str(campaign.get("mode")))
        or target_changes[0].get("improvement") is not True
    ):
        raise ReplayError("the final impact target result is missing")
    final_status = (
        "exact"
        if after.get("exact") is True
        else "effective"
        if after.get("effective") is True
        else "provisional"
    )
    final_level, final_score = quality_from_score(final_status, after.get("matching"))
    if (
        final_level != expected_best.get("status_level")
        or final_score != expected_best.get("score_millionths")
    ):
        raise ReplayError("the delivered target does not retain the experiment best")
    validation_tools = expected_comparison_identity.get("validation_tools")
    if not isinstance(validation_tools, Mapping):
        raise ReplayError("the experiment best has no validation tool identity")
    source_commit, delivery_hash = _delivery_evidence(
        campaign,
        records,
        finalize_receipt,
        root,
        current_head,
        origin_head,
        validation_tools,
    )
    subsystem = _subsystem_slug(campaign.get("subsystem"))
    impact_hash = impact_pack.get("content_sha256")
    leaf_hash = leaf_document.get("content_sha256")
    if (
        not isinstance(impact_hash, str)
        or not isinstance(leaf_hash, str)
    ):
        raise ReplayError("the source campaign classification evidence is invalid")
    provenance = {
        "target": target,
        "session_id": session.get("session_id"),
        "session_receipt_id": session.get("receipt_id"),
        "campaign_id": campaign_id,
        "campaign_record_sha256": campaigns._snapshot_hash(campaign),
        "delivery_receipt_sha256": delivery_hash,
        "source_commit": source_commit,
        "context_pack_sha256": context_hash,
        "impact_pack_sha256": impact_hash,
        "leaf_oracle_sha256": leaf_hash,
    }
    return provenance, {"subsystem": subsystem}


def _taxonomy_routes(taxonomy: object) -> list[str]:
    """Remove evidence rows from one normalized mismatch taxonomy."""

    if (
        not isinstance(taxonomy, Mapping)
        or type(taxonomy.get("schema_version")) is not int
        or taxonomy.get("schema_version") != 1
    ):
        raise ReplayError("an experiment mismatch taxonomy is invalid")
    signals = taxonomy.get("signals")
    if not isinstance(signals, list):
        raise ReplayError("an experiment mismatch taxonomy is invalid")
    routes = []
    for signal in signals:
        route = signal.get("route") if isinstance(signal, Mapping) else None
        if not isinstance(route, str):
            raise ReplayError("an experiment mismatch taxonomy is invalid")
        routes.append(route)
    return _routes(routes)


def _node(candidate: Mapping[str, object], routes: list[str]) -> dict[str, object]:
    status_level, score = quality_from_score(
        candidate.get("status"), candidate.get("score")
    )
    eligible = candidate.get("eligible")
    if type(eligible) is not bool:
        raise ReplayError("an experiment candidate eligibility is invalid")
    return {
        "status_level": status_level,
        "score_millionths": score,
        "eligible": eligible,
        "routes": routes,
    }


def _recomputed_trial(
    receipt: Mapping[str, object],
    directory: Path,
    session: Mapping[str, object],
    target: str,
    root: Path,
) -> tuple[dict[str, object], object]:
    """Recompute one trial result from its bound comparison artifacts."""

    from tools import decomp_experiment as experiment
    from tools.decomp_status import read_match_statuses
    from tools.decomp_verify import experiment_record

    artifacts = receipt.get("artifacts")
    if not isinstance(artifacts, Mapping):
        raise ReplayError("an experiment trial has no artifacts")
    trial_id = receipt.get("trial_id")
    if not isinstance(trial_id, str):
        raise ReplayError("an experiment trial identity is invalid")
    trial_directory = directory / "trials" / trial_id
    report, _report_receipt = experiment._validate_report_descriptor(
        artifacts.get("report"), root, parent=trial_directory
    )
    context_path = experiment._validate_descriptor(
        artifacts.get("compiler_context"),
        root,
        maximum=experiment.MAX_CONTEXT_BYTES,
        description="trial compiler context",
        parent=trial_directory,
    )
    current_context = experiment._read_json(
        context_path, experiment.MAX_CONTEXT_BYTES, "trial compiler context"
    )
    baseline_context = session.get("compiler_context")
    if not isinstance(baseline_context, Mapping) or not experiment._context_compatible(
        baseline_context, current_context
    ):
        raise ReplayError("the experiment compiler context changed")
    baseline_artifacts = session.get("artifacts")
    baseline_descriptor = (
        baseline_artifacts.get("baseline_report")
        if isinstance(baseline_artifacts, Mapping)
        else None
    )
    baseline_report, _ = experiment._validate_report_descriptor(
        baseline_descriptor, root, parent=directory
    )
    baseline_statuses = read_match_statuses(baseline_report)
    current_statuses = read_match_statuses(report)
    target_int = int(target, 16)
    current_target = current_statuses.get(target_int)
    if current_target is None:
        raise ReplayError("an experiment report has no target result")
    gate = experiment._gate_result(
        baseline_statuses,
        current_statuses,
        target_int,
        baseline_context,
        current_context,
    )
    status = experiment._status_name(current_target, target_int, current_context)
    normalized = experiment_record(report, target_int)
    taxonomy = normalized.get("mismatch_taxonomy")
    expected_target = {"status": status, "score": current_target.matching}
    if (
        receipt.get("target") != expected_target
        or receipt.get("eligible") is not gate.get("passed")
        or receipt.get("normalized_taxonomy") != taxonomy
    ):
        raise ReplayError("an experiment trial result differs from its artifacts")
    return {
        "status": status,
        "score": current_target.matching,
        "eligible": gate["passed"],
    }, taxonomy


def reconstruct_case(
    case_id: str,
    target: str,
    session_id: str,
    *,
    root: Path = ROOT,
    records: Sequence[dict[str, object]] | None = None,
    current_head: str | None = None,
    origin_head: str | None = None,
    assume_private_lock: bool = False,
) -> dict[str, object]:
    """Rebuild one private case from all canonical source evidence."""

    from tools import decomp_experiment as experiment
    from tools.decomp_verify import experiment_record

    root = root.resolve()
    current_head = current_head or _git_ref(root, "HEAD")
    origin_head = origin_head or _git_ref(
        root, "refs/remotes/origin/agent/continuous"
    )
    if current_head is None or origin_head is None:
        raise ReplayError("the replay Git references are unavailable")
    if CASE_ID_RE.fullmatch(case_id) is None:
        raise ReplayError("the private replay case ID is invalid")
    target = experiment.normalize_address(target)
    trajectory = experiment.trajectory_identity(target, session_id, root=root)
    directory, session = experiment.read_session(
        target,
        root=root,
        session_id=session_id,
        validate_artifacts=True,
    )
    limits = session.get("limits")
    if (
        not isinstance(limits, Mapping)
        or type(limits.get("max_trials")) is not int
        or limits.get("max_trials") != MAX_TRIALS
        or type(limits.get("max_non_improving")) is not int
        or limits.get("max_non_improving") != MAX_NON_IMPROVING
    ):
        raise ReplayError("the experiment session does not use the replay budget")
    trials = experiment._trial_receipts(directory)
    if len(trials) not in {2, MAX_TRIALS} or any(
        state != "completed" for _, _, state in trials
    ):
        raise ReplayError("the experiment session has incomplete trial outcomes")
    baseline = experiment._baseline_candidate(directory, session, root)
    baseline_snapshot = session.get(experiment.SOURCE_SNAPSHOT_FIELD)
    baseline_identity = session.get("comparison_identity")
    if (
        not isinstance(baseline_snapshot, dict)
        or not isinstance(baseline_identity, Mapping)
        or experiment._snapshot_hash(baseline_snapshot)
        != baseline_identity.get("source_worktree_sha256")
    ):
        raise ReplayError("the experiment baseline has no bound source snapshot")
    artifacts = session.get("artifacts")
    report_descriptor = (
        artifacts.get("baseline_report") if isinstance(artifacts, Mapping) else None
    )
    baseline_report, _ = experiment._validate_report_descriptor(
        report_descriptor,
        root,
        parent=directory,
    )
    normalized_baseline = experiment_record(baseline_report, int(target, 16))
    nodes = [
        _node(
            baseline,
            _taxonomy_routes(normalized_baseline.get("mismatch_taxonomy")),
        )
    ]
    edges: list[dict[str, object]] = []
    source_snapshots: list[dict[str, str]] = [dict(baseline_snapshot)]
    comparison_identities: list[Mapping[str, object] | None] = [baseline_identity]
    trial_nodes: dict[str, int] = {}
    for sequence, (_path, receipt, state) in enumerate(trials, start=1):
        pending = experiment.read_receipt(
            _path / "pending.json", kind="experiment-trial-pending"
        )
        if (
            state != "completed"
            or type(receipt.get("sequence")) is not int
            or receipt.get("sequence") != sequence
            or receipt.get("result") != "comparison"
            or type(receipt.get("eligible")) is not bool
            or receipt.get("address") != target
            or receipt.get("session_id") != session.get("session_id")
            or receipt.get("session_receipt_id") != session.get("receipt_id")
            or _path.name != receipt.get("trial_id")
        ):
            raise ReplayError("an experiment trial outcome is invalid")
        experiment._validate_completed_artifacts(receipt, directory, root)
        trial_id = receipt.get("trial_id")
        explicit_route = receipt.get("route")
        parent = receipt.get("parent")
        if (
            not isinstance(trial_id, str)
            or not isinstance(explicit_route, str)
            or explicit_route not in ROUTE_ORDER
        ):
            raise ReplayError("an experiment trial has no explicit route")
        parent_index = 0 if parent == "baseline" else trial_nodes.get(str(parent), -1)
        if not 0 <= parent_index < sequence:
            raise ReplayError("an experiment trial parent is invalid")
        parent_snapshot = receipt.get(experiment.PARENT_SNAPSHOT_FIELD)
        if (
            not isinstance(parent_snapshot, dict)
            or parent_snapshot != source_snapshots[parent_index]
            or pending.get(experiment.PARENT_SNAPSHOT_FIELD) != parent_snapshot
            or pending.get("receipt_id") != receipt.get("pending_receipt_id")
        ):
            raise ReplayError("an experiment trial has no valid prepared parent")
        try:
            source_snapshot = experiment._receipt_source_snapshot(receipt)
        except Exception as error:
            raise ReplayError("an experiment trial has no bound source snapshot") from error
        candidate, taxonomy = _recomputed_trial(
            receipt, directory, session, target, root
        )
        nodes.append(_node(candidate, _taxonomy_routes(taxonomy)))
        source_snapshots.append(source_snapshot)
        comparison_identity = receipt.get("comparison_identity")
        if not isinstance(comparison_identity, Mapping):
            raise ReplayError("an experiment trial comparison identity is invalid")
        comparison_identities.append(comparison_identity)
        edges.append(
            {
                "sequence": sequence,
                "from": parent_index,
                "to": sequence,
                "route": explicit_route,
            }
        )
        trial_nodes[trial_id] = sequence
    incumbent = 0
    incumbent_quality = _node_quality(nodes[0])
    for index, node in enumerate(nodes[1:], start=1):
        node_quality = _node_quality(node)
        if node.get("eligible") is True and node_quality > incumbent_quality:
            incumbent = index
            incumbent_quality = node_quality
    if incumbent == 0:
        raise ReplayError("the experiment best is still the baseline")
    tied_best = [
        index
        for index, node in enumerate(nodes[1:], start=1)
        if node.get("eligible") is True and _node_quality(node) == incumbent_quality
    ]
    if tied_best != [incumbent]:
        raise ReplayError("the experiment has no unique best eligible trial")
    provenance, classification = _verified_campaign_evidence(
        session,
        target,
        nodes[incumbent],
        source_snapshots[incumbent],
        comparison_identities[incumbent] or {},
        root,
        current_head,
        origin_head,
        trajectory,
        assume_private_lock,
        records,
    )
    trajectory_sha256 = trajectory.get("trajectory_sha256")
    if not isinstance(trajectory_sha256, str):
        raise ReplayError("the experiment trajectory identity is invalid")
    provenance["trajectory_sha256"] = trajectory_sha256
    document: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": "private-replay-case",
        "case_id": case_id,
        "provenance": provenance,
        "classification": classification,
        "budget": {
            "max_trials": MAX_TRIALS,
            "max_non_improving": MAX_NON_IMPROVING,
        },
        "graph": {"nodes": nodes, "edges": edges},
    }
    result = validate_case(document)
    if experiment.trajectory_identity(target, session_id, root=root) != trajectory:
        raise ReplayError("the experiment trajectory changed during reconstruction")
    return result


def _revalidate_case(
    document: dict[str, object],
    root: Path,
    records: Sequence[dict[str, object]],
    current_head: str,
    origin_head: str,
) -> None:
    """Require a private case to equal its current canonical reconstruction."""

    provenance = document.get("provenance")
    if not isinstance(provenance, Mapping):
        raise ReplayError("the private replay provenance is invalid")
    expected = reconstruct_case(
        str(document.get("case_id")),
        str(provenance.get("target")),
        str(provenance.get("session_id")),
        root=root,
        records=records,
        current_head=current_head,
        origin_head=origin_head,
        assume_private_lock=True,
    )
    if expected != document:
        raise ReplayError("the private replay case differs from canonical evidence")


def read_cases(root: Path = ROOT) -> list[dict[str, object]]:
    """Read all manifest-selected private replay cases."""

    manifest = read_manifest(root)
    result: list[dict[str, object]] = []
    campaigns: set[str] = set()
    targets: set[str] = set()
    for entry in manifest["cases"]:
        assert isinstance(entry, Mapping)
        document = _strict_json(_case_bytes(entry, root.resolve()), "private replay case")
        if document.get("case_id") != entry.get("case_id"):
            raise ReplayError("a private replay case names another manifest entry")
        validate_case(document)
        provenance = document["provenance"]
        assert isinstance(provenance, Mapping)
        campaign = str(provenance["campaign_id"])
        target = str(provenance["target"])
        if campaign in campaigns or target in targets:
            raise ReplayError("the private replay suite contains a duplicate source trajectory")
        campaigns.add(campaign)
        targets.add(target)
        result.append(document)
    return result


def _enrollment_campaign(root: Path, current_head: str) -> tuple[str, str]:
    """Require an explicit meta campaign for a tracked batch enrollment."""

    try:
        from tools import decomp_campaigns as campaigns

        state_path = root / "build/decomp-campaign-state.json"
        state = _strict_json(
            _regular_bytes(
                state_path,
                campaigns.MAX_CAMPAIGN_STATE_BYTES,
                "replay enrollment campaign state",
            ),
            "replay enrollment campaign state",
        )
        campaign_id = state.get("campaign_id")
        source_root = state.get("source_worktree_root")
        phase = str(state.get("phase", ""))
        if (
            state.get("mode") != "meta"
            or campaigns._campaign_lane(state) != "meta"
            or state.get("subsystem") != "private-replay-enrollment"
            or not isinstance(campaign_id, str)
            or not campaign_id
            or not isinstance(source_root, str)
            or Path(source_root).resolve() != root
            or source_root != str(root)
            or state.get("campaign_head") != current_head
            or state.get("finalize_receipt") is not None
            or state.get("finalization") is not None
            or phase.startswith("finalize")
            or phase in {"finalizing", "finalized", "aborting", "aborted"}
        ):
            raise ValueError
    except Exception as error:
        raise ReplayError("replay enrollment needs an active meta campaign") from error
    return campaign_id, _json_hash(state)


def _manifest_append_base(
    root: Path,
    head: str,
    manifest: Mapping[str, object],
) -> None:
    """Require the working manifest to preserve every exact HEAD entry."""

    content = _git_blob(root, head, MANIFEST_RELATIVE, MAX_MANIFEST_BYTES)
    base = _validate_manifest(_strict_json(content, "HEAD replay manifest"))
    base_rows = base.get("cases")
    rows = manifest.get("cases")
    if not isinstance(base_rows, list) or not isinstance(rows, list):
        raise ReplayError("the replay manifest append base is invalid")
    current_by_id = {
        str(row["case_id"]): row for row in rows if isinstance(row, Mapping)
    }
    if any(current_by_id.get(str(row["case_id"])) != row for row in base_rows):
        raise ReplayError("replay enrollment cannot change an existing manifest entry")
    try:
        index = subprocess.run(
            [
                "git",
                "diff",
                "--cached",
                "--quiet",
                "--",
                MANIFEST_RELATIVE.as_posix(),
            ],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ReplayError("the replay manifest index check failed") from error
    if index.returncode != 0:
        raise ReplayError("the replay manifest index differs from HEAD")


def _replace_manifest(root: Path, expected: bytes, document: Mapping[str, object]) -> bytes:
    """Atomically replace the public manifest only from one exact byte snapshot."""

    path = manifest_path(root)
    content = json.dumps(document, indent=2, sort_keys=True).encode("utf-8") + b"\n"
    if not 0 < len(content) <= MAX_MANIFEST_BYTES:
        raise ReplayError("the replay manifest exceeds its size limit")
    temporary = f".{path.name}.{secrets.token_hex(16)}.tmp"
    root_fd = -1
    parent_fd = -1
    descriptor = -1
    temporary_identity: tuple[int, int] | None = None
    try:
        root_fd = _open_absolute_directory(root)
        parent_fd = root_fd
        for part in MANIFEST_RELATIVE.parent.parts:
            child = os.open(part, _directory_flags(), dir_fd=parent_fd)
            if parent_fd != root_fd:
                os.close(parent_fd)
            parent_fd = child
        current_descriptor = os.open(
            path.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent_fd
        )
        with os.fdopen(current_descriptor, "rb") as stream:
            before = os.fstat(stream.fileno())
            current = stream.read(MAX_MANIFEST_BYTES + 1)
            after = os.fstat(stream.fileno())
        stable = (
            before.st_dev,
            before.st_ino,
            before.st_mode,
            before.st_nlink,
            before.st_uid,
            before.st_size,
            before.st_mtime_ns,
            before.st_ctime_ns,
        ) == (
            after.st_dev,
            after.st_ino,
            after.st_mode,
            after.st_nlink,
            after.st_uid,
            after.st_size,
            after.st_mtime_ns,
            after.st_ctime_ns,
        )
        if (
            current != expected
            or not stable
            or not stat.S_ISREG(after.st_mode)
            or after.st_nlink != 1
            or (
                hasattr(os, "geteuid")
                and after.st_uid != os.geteuid()
            )
        ):
            raise ReplayError("the replay manifest changed during enrollment")
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
            0o644,
            dir_fd=parent_fd,
        )
        with os.fdopen(descriptor, "wb") as stream:
            descriptor = -1
            stream.write(content)
            stream.flush()
            os.fchmod(stream.fileno(), 0o644)
            os.fsync(stream.fileno())
            metadata = os.fstat(stream.fileno())
            if (
                not stat.S_ISREG(metadata.st_mode)
                or metadata.st_nlink != 1
                or stat.S_IMODE(metadata.st_mode) != 0o644
                or (
                    hasattr(os, "geteuid")
                    and metadata.st_uid != os.geteuid()
                )
            ):
                raise OSError
            temporary_identity = (metadata.st_dev, metadata.st_ino)
        os.replace(
            temporary,
            path.name,
            src_dir_fd=parent_fd,
            dst_dir_fd=parent_fd,
        )
        os.fsync(parent_fd)
        published = os.open(
            path.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=parent_fd
        )
        with os.fdopen(published, "rb") as stream:
            before = os.fstat(stream.fileno())
            saved = stream.read(MAX_MANIFEST_BYTES + 1)
            after = os.fstat(stream.fileno())
        if (
            temporary_identity != (before.st_dev, before.st_ino)
            or (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
            != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns)
            or after.st_nlink != 1
            or stat.S_IMODE(after.st_mode) != 0o644
            or (
                hasattr(os, "geteuid")
                and after.st_uid != os.geteuid()
            )
            or saved != content
        ):
            raise ReplayError("the replay manifest publication is invalid")
    except ReplayError:
        raise
    except OSError as error:
        raise ReplayError("cannot update the replay manifest") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if parent_fd >= 0 and temporary_identity is not None:
            try:
                temporary_fd = os.open(
                    temporary,
                    os.O_RDONLY | os.O_NOFOLLOW,
                    dir_fd=parent_fd,
                )
                try:
                    temporary_metadata = os.fstat(temporary_fd)
                finally:
                    os.close(temporary_fd)
                if (
                    temporary_metadata.st_dev,
                    temporary_metadata.st_ino,
                ) == temporary_identity:
                    os.unlink(temporary, dir_fd=parent_fd)
                    os.fsync(parent_fd)
            except FileNotFoundError:
                pass
            except OSError:
                pass
        if parent_fd >= 0 and parent_fd != root_fd:
            os.close(parent_fd)
        if root_fd >= 0:
            os.close(root_fd)
    return content


def _delete_private_case(root: Path, case_id: str) -> None:
    """Remove only one case that this enrollment allocated but did not publish."""

    name = f"{case_id}.json"
    with _private_directory(root, CASES_NAME) as directory_fd:
        _read_at(directory_fd, name, MAX_CASE_BYTES, "private replay case")
        try:
            os.unlink(name, dir_fd=directory_fd)
            os.fsync(directory_fd)
        except OSError as error:
            raise ReplayError("cannot roll back the private replay enrollment") from error


def _unlink_private_at(directory_fd: int, name: str) -> None:
    try:
        descriptor = os.open(
            name,
            os.O_RDONLY | os.O_NOFOLLOW,
            dir_fd=directory_fd,
        )
        try:
            metadata = os.fstat(descriptor)
        finally:
            os.close(descriptor)
        current_uid = os.geteuid() if hasattr(os, "geteuid") else None
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_nlink != 1
            or metadata.st_mode & 0o077
            or (current_uid is not None and metadata.st_uid != current_uid)
        ):
            raise ReplayError("the private replay transaction file is unsafe")
        _unlink_private_identity_at(
            directory_fd,
            name,
            (metadata.st_dev, metadata.st_ino),
        )
    except FileNotFoundError:
        return
    except OSError as error:
        raise ReplayError("cannot update the private replay transaction") from error


def _journal_document(
    old_manifest: bytes,
    new_manifest: bytes,
    entries: Sequence[Mapping[str, object]],
) -> dict[str, object]:
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "private-replay-enrollment-transaction",
        "old_manifest_sha256": hashlib.sha256(old_manifest).hexdigest(),
        "new_manifest_sha256": hashlib.sha256(new_manifest).hexdigest(),
        "cases": [dict(entry) for entry in entries],
    }


def _validated_journal(content: bytes) -> dict[str, object]:
    """Validate one complete enrollment transaction document."""

    journal = _strict_json(content, "private replay enrollment transaction")
    _strict_keys(
        journal,
        {
            "schema_version",
            "kind",
            "old_manifest_sha256",
            "new_manifest_sha256",
            "cases",
        },
        "private replay enrollment transaction",
    )
    entries = journal.get("cases")
    if (
        type(journal.get("schema_version")) is not int
        or journal.get("schema_version") != SCHEMA_VERSION
        or journal.get("kind") != "private-replay-enrollment-transaction"
        or not isinstance(entries, list)
        or not 0 < len(entries) <= MAX_CASES
    ):
        raise ReplayError("the private replay enrollment transaction is invalid")
    old_hash = _sha256(journal.get("old_manifest_sha256"), "old manifest hash")
    new_hash = _sha256(journal.get("new_manifest_sha256"), "new manifest hash")
    if old_hash == new_hash:
        raise ReplayError("the private replay enrollment transaction is invalid")
    case_ids: set[str] = set()
    for entry in entries:
        if not isinstance(entry, Mapping):
            raise ReplayError("the private replay enrollment transaction is invalid")
        _strict_keys(entry, MANIFEST_CASE_KEYS, "enrollment transaction case")
        _validate_manifest(
            {
                "schema_version": SCHEMA_VERSION,
                "kind": "private-replay-manifest",
                "cases": [dict(entry)],
            }
        )
        case_id = str(entry["case_id"])
        if case_id in case_ids:
            raise ReplayError("the private replay enrollment transaction is invalid")
        case_ids.add(case_id)
    return journal


def _clear_journal(root: Path) -> None:
    with _private_directory(root) as directory_fd:
        _unlink_private_at(directory_fd, ENROLLMENT_JOURNAL_NAME)
        _unlink_private_at(directory_fd, f".{ENROLLMENT_JOURNAL_NAME}.tmp")


def _recover_enrollment(root: Path, manifest_content: bytes) -> None:
    """Finish or roll back one interrupted enrollment under the suite lock."""

    with _private_directory(root) as directory_fd:
        try:
            os.stat(
                ENROLLMENT_JOURNAL_NAME,
                dir_fd=directory_fd,
                follow_symlinks=False,
            )
        except FileNotFoundError:
            temporary = f".{ENROLLMENT_JOURNAL_NAME}.tmp"
            try:
                temporary_content, temporary_metadata = _read_private_file_at(
                    directory_fd,
                    temporary,
                    MAX_CERTIFICATE_BYTES,
                    "private replay enrollment transaction temporary file",
                    allow_empty=True,
                )
            except ReplayError:
                try:
                    os.stat(
                        temporary,
                        dir_fd=directory_fd,
                        follow_symlinks=False,
                    )
                except FileNotFoundError:
                    return
                raise
            try:
                _validated_journal(temporary_content)
            except ReplayError:
                _unlink_private_identity_at(
                    directory_fd,
                    temporary,
                    (temporary_metadata.st_dev, temporary_metadata.st_ino),
                )
                return
            try:
                rename_noreplace = ctypes.CDLL(None, use_errno=True).renameat2
            except AttributeError as error:
                raise ReplayError("secure private replay storage is unavailable") from error
            rename_noreplace.argtypes = [
                ctypes.c_int,
                ctypes.c_char_p,
                ctypes.c_int,
                ctypes.c_char_p,
                ctypes.c_uint,
            ]
            rename_noreplace.restype = ctypes.c_int
            if rename_noreplace(
                directory_fd,
                os.fsencode(temporary),
                directory_fd,
                os.fsencode(ENROLLMENT_JOURNAL_NAME),
                1,
            ) != 0:
                saved_errno = ctypes.get_errno()
                raise ReplayError(
                    "cannot recover the private replay enrollment transaction"
                ) from OSError(saved_errno, os.strerror(saved_errno))
            os.fsync(directory_fd)
        content = _read_at(
            directory_fd,
            ENROLLMENT_JOURNAL_NAME,
            MAX_CERTIFICATE_BYTES,
            "private replay enrollment transaction",
        )
    journal = _validated_journal(content)
    entries = journal.get("cases")
    assert isinstance(entries, list)
    old_hash = _sha256(journal.get("old_manifest_sha256"), "old manifest hash")
    new_hash = _sha256(journal.get("new_manifest_sha256"), "new manifest hash")
    current_hash = hashlib.sha256(manifest_content).hexdigest()
    manifest = _strict_json(manifest_content, "private replay manifest")
    _validate_manifest(manifest)
    selected = manifest.get("cases")
    assert isinstance(selected, list)
    selected_by_id = {
        str(entry["case_id"]): dict(entry)
        for entry in selected
        if isinstance(entry, Mapping)
    }
    if current_hash == new_hash:
        for entry in entries:
            assert isinstance(entry, Mapping)
            if selected_by_id.get(str(entry["case_id"])) != dict(entry):
                raise ReplayError("the private replay enrollment transaction is stale")
            _case_bytes(entry, root)
        _clear_journal(root)
        return
    if current_hash != old_hash:
        raise ReplayError("the private replay enrollment transaction is stale")
    if any(str(entry["case_id"]) in selected_by_id for entry in entries):
        raise ReplayError("the private replay enrollment transaction is stale")
    # Validate every final object before deleting any of them. A corrupt or
    # unrelated object with a journal-owned name is not crash residue.
    with _private_directory(root, CASES_NAME, create=True) as directory_fd:
        for entry in entries:
            assert isinstance(entry, Mapping)
            name = f"{entry['case_id']}.json"
            try:
                os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            except FileNotFoundError:
                continue
            _case_bytes(entry, root)
    with _private_directory(root, CASES_NAME, create=True) as directory_fd:
        for entry in entries:
            assert isinstance(entry, Mapping)
            name = f"{entry['case_id']}.json"
            try:
                os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                _unlink_private_at(directory_fd, name)
            _unlink_private_at(directory_fd, f".{name}.tmp")
    _clear_journal(root)


def _enrollment_inputs(
    root: Path,
    head: str,
    origin: str,
    enrollment_campaign: tuple[str, str],
    manifest_content: bytes,
    ledger_content: bytes,
) -> None:
    if (
        _git_ref(root, "HEAD") != head
        or _git_ref(root, "refs/remotes/origin/agent/continuous") != origin
        or head != origin
        or _symbolic_branch(root) != "agent/continuous"
        or not _private_root_is_untracked(root, head)
        or _enrollment_campaign(root, head) != enrollment_campaign
        or _regular_bytes(
            manifest_path(root), MAX_MANIFEST_BYTES, "replay manifest"
        )
        != manifest_content
        or not _tracked_content_matches_head(
            root, head, LEDGER_RELATIVE, ledger_content, MAX_LEDGER_BYTES
        )
    ):
        raise ReplayError("the replay enrollment inputs changed")
    _tool_binding(root, head)


def enroll_batch(
    candidates: Sequence[tuple[str, str]],
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Enroll one all-or-nothing batch of delivered trajectories."""

    if not candidates or len(candidates) > MAX_CASES:
        raise ReplayError("the replay enrollment batch is invalid")
    if any(
        not isinstance(target, str)
        or not target
        or not isinstance(session_id, str)
        or SESSION_RE.fullmatch(session_id) is None
        for target, session_id in candidates
    ):
        raise ReplayError("the replay enrollment batch is invalid")
    root = root.resolve()
    from tools import decomp_campaigns as campaigns

    state_path = root / "build/decomp-campaign-state.json"
    with campaigns._campaign_state_lock(state_path):
        head = _git_ref(root, "HEAD")
        origin = _git_ref(root, "refs/remotes/origin/agent/continuous")
        if (
            head is None
            or origin is None
            or head != origin
            or _symbolic_branch(root) != "agent/continuous"
            or not _private_root_is_untracked(root, head)
        ):
            raise ReplayError("the replay enrollment repository identity is invalid")
        enrollment_campaign = _enrollment_campaign(root, head)
        with _suite_lock(root, create=True, exclusive=True):
            manifest, manifest_content = _manifest_document(root)
            _recover_enrollment(root, manifest_content)
            manifest, manifest_content = _manifest_document(root)
            _manifest_append_base(root, head, manifest)
            ledger_content = _git_blob(root, head, LEDGER_RELATIVE, MAX_LEDGER_BYTES)
            _enrollment_inputs(
                root,
                head,
                origin,
                enrollment_campaign,
                manifest_content,
                ledger_content,
            )
            records = _parse_ledger_records(ledger_content)
            cases, invalid, storage_invalid = _loaded_cases(
                manifest, root, records, head, origin
            )
            if invalid or storage_invalid:
                raise ReplayError("the existing private replay suite is invalid")
            if len(cases) + len(candidates) > MAX_CASES:
                raise ReplayError("the replay enrollment exceeds the case limit")
            used_ids = {str(case["case_id"]) for case in cases}
            used_campaigns = {
                str(case["provenance"]["campaign_id"]) for case in cases
            }
            used_targets = {str(case["provenance"]["target"]) for case in cases}
            additions: list[tuple[dict[str, object], bytes, dict[str, object]]] = []
            for target, session_id in candidates:
                for _ in range(64):
                    case_id = secrets.token_hex(16)
                    if case_id not in used_ids:
                        used_ids.add(case_id)
                        break
                else:
                    raise ReplayError("cannot allocate a private replay case ID")
                case = reconstruct_case(
                    case_id,
                    target,
                    session_id,
                    root=root,
                    records=records,
                    current_head=head,
                    origin_head=origin,
                    assume_private_lock=True,
                )
                provenance = case.get("provenance")
                if not isinstance(provenance, Mapping):
                    raise ReplayError("the private replay case provenance is invalid")
                campaign_id = str(provenance.get("campaign_id"))
                canonical_target = str(provenance.get("target"))
                if campaign_id in used_campaigns or canonical_target in used_targets:
                    raise ReplayError("the private replay suite already has this source trajectory")
                used_campaigns.add(campaign_id)
                used_targets.add(canonical_target)
                content = json.dumps(case, indent=2, sort_keys=True).encode("utf-8") + b"\n"
                if not 0 < len(content) <= MAX_CASE_BYTES:
                    raise ReplayError("the private replay case exceeds its size limit")
                entry = {
                    "case_id": case_id,
                    "bytes": len(content),
                    "sha256": hashlib.sha256(content).hexdigest(),
                }
                additions.append((case, content, entry))
            rows = [*manifest["cases"], *(entry for _, _, entry in additions)]
            rows.sort(key=lambda row: str(row["case_id"]))
            updated = _validate_manifest(
                {
                    "schema_version": SCHEMA_VERSION,
                    "kind": "private-replay-manifest",
                    "cases": rows,
                }
            )
            updated_content = (
                json.dumps(updated, indent=2, sort_keys=True).encode("utf-8") + b"\n"
            )
            if len(updated_content) > MAX_MANIFEST_BYTES:
                raise ReplayError("the replay manifest exceeds its size limit")
            _enrollment_inputs(
                root,
                head,
                origin,
                enrollment_campaign,
                manifest_content,
                ledger_content,
            )
            entries = [entry for _, _, entry in additions]
            journal = _journal_document(manifest_content, updated_content, entries)
            journal_content = (
                json.dumps(journal, indent=2, sort_keys=True).encode("utf-8") + b"\n"
            )
            try:
                with _private_directory(root) as directory_fd:
                    _write_exclusive_at(
                        directory_fd, ENROLLMENT_JOURNAL_NAME, journal_content
                    )
                with _private_directory(root, CASES_NAME, create=True) as directory_fd:
                    for _, content, entry in additions:
                        name = f"{entry['case_id']}.json"
                        _write_exclusive_at(directory_fd, name, content)
                saved_content = _replace_manifest(root, manifest_content, updated)
                if saved_content != updated_content:
                    raise ReplayError("the replay manifest serialization changed")
                _enrollment_inputs(
                    root,
                    head,
                    origin,
                    enrollment_campaign,
                    updated_content,
                    ledger_content,
                )
                final_manifest, final_content = _manifest_document(root)
                if final_manifest != updated or final_content != updated_content:
                    raise ReplayError("the replay enrollment did not publish atomically")
                _clear_journal(root)
            except Exception as error:
                try:
                    current_manifest, current_content = _manifest_document(root)
                    if current_content == updated_content:
                        _replace_manifest(root, current_content, manifest)
                        current_manifest, current_content = _manifest_document(root)
                    elif current_content != manifest_content:
                        raise ReplayError("the replay manifest rollback is ambiguous")
                    if current_content != manifest_content or current_manifest != manifest:
                        raise ReplayError("the replay manifest rollback is ambiguous")
                    with _private_directory(root, CASES_NAME, create=True) as directory_fd:
                        for entry in entries:
                            case_name = f"{entry['case_id']}.json"
                            _unlink_private_at(directory_fd, case_name)
                            _unlink_private_at(directory_fd, f".{case_name}.tmp")
                    _clear_journal(root)
                except Exception as cleanup_error:
                    raise ReplayError(
                        "the private replay enrollment rollback failed"
                    ) from cleanup_error
                if isinstance(error, ReplayError):
                    raise
                raise ReplayError("the private replay enrollment failed") from error
            return {
                "schema_version": SCHEMA_VERSION,
                "kind": "private-replay-enrollment",
                "status": "enrolled",
                "case_count": len(rows),
                "enrolled_count": len(additions),
            }


def enroll(target: str, session_id: str, *, root: Path = ROOT) -> dict[str, object]:
    """Enroll one case through the all-or-nothing batch transaction."""

    return enroll_batch([(target, session_id)], root=root)


def _loaded_cases(
    manifest: Mapping[str, object],
    root: Path,
    records: Sequence[dict[str, object]],
    current_head: str,
    origin_head: str,
) -> tuple[list[dict[str, object]], list[dict[str, object]], int]:
    """Load selected cases and count private storage mismatches."""

    entries = manifest.get("cases")
    assert isinstance(entries, list)
    expected_names = {f"{entry['case_id']}.json" for entry in entries}
    names = _case_file_names(root)
    storage_invalid = len(names - expected_names)
    valid: list[dict[str, object]] = []
    private_results: list[dict[str, object]] = []
    campaigns: set[str] = set()
    targets: set[str] = set()
    for entry in entries:
        assert isinstance(entry, Mapping)
        case_id = str(entry["case_id"])
        try:
            document = _strict_json(_case_bytes(entry, root), "private replay case")
            if document.get("case_id") != case_id:
                raise ReplayError("a private replay case names another manifest entry")
            validate_case(document)
            _revalidate_case(
                document, root, records, current_head, origin_head
            )
            provenance = document["provenance"]
            assert isinstance(provenance, Mapping)
            campaign = str(provenance["campaign_id"])
            target = str(provenance["target"])
            if campaign in campaigns or target in targets:
                raise ReplayError("the private replay suite contains a duplicate source trajectory")
            campaigns.add(campaign)
            targets.add(target)
            valid.append(document)
        except Exception:
            private_results.append(
                {"case_id": case_id, "status": "invalid", "reason": "invalid-case"}
            )
    return valid, private_results, storage_invalid


def _node_quality(node: Mapping[str, object]) -> int:
    return quality(node["status_level"], node["score_millionths"])


def _node_terminal(node: Mapping[str, object]) -> bool:
    """Return true when one eligible replay result is terminal."""

    return node.get("eligible") is True and int(node["status_level"]) >= TERMINAL_LEVEL


def _tried_map(state: ReplayState) -> dict[int, tuple[str, ...]]:
    return {node: routes for node, routes in state.tried_by_incumbent}


def _advance(
    state: ReplayState,
    edge_index: int,
    nodes: Sequence[Mapping[str, object]],
    edges: Sequence[Mapping[str, object]],
) -> ReplayState:
    edge = edges[edge_index]
    destination = int(edge["to"])
    node = nodes[destination]
    node_quality = _node_quality(node)
    improving = node.get("eligible") is True and node_quality > state.best_quality
    tried = _tried_map(state)
    source = int(edge["from"])
    incumbent_routes = list(tried.get(source, ()))
    incumbent_routes.append(str(edge["route"]))
    tried[source] = tuple(incumbent_routes)
    return ReplayState(
        discovered=state.discovered | {destination},
        selected=state.selected + (edge_index,),
        incumbent=destination if improving else state.incumbent,
        best_quality=node_quality if improving else state.best_quality,
        non_improving=0 if improving else state.non_improving + 1,
        tried_by_incumbent=tuple(sorted(tried.items())),
    )


def _frontier(state: ReplayState, edges: Sequence[Mapping[str, object]]) -> list[int]:
    selected = set(state.selected)
    return [
        index
        for index, edge in enumerate(edges)
        if index not in selected and int(edge["from"]) in state.discovered
    ]


def _stopped(state: ReplayState, nodes: Sequence[Mapping[str, object]]) -> bool:
    return (
        _node_terminal(nodes[state.incumbent])
        or len(state.selected) >= MAX_TRIALS
        or state.non_improving >= MAX_NON_IMPROVING
    )


def _initial_state(nodes: Sequence[Mapping[str, object]]) -> ReplayState:
    return ReplayState(
        discovered=frozenset({0}),
        selected=(),
        incumbent=0,
        best_quality=_node_quality(nodes[0]),
        non_improving=0,
        tried_by_incumbent=(),
    )


def exhaustive_replay(case: Mapping[str, object]) -> dict[str, object]:
    """Enumerate all feasible frontier selections for one case."""

    validate_case(dict(case))
    graph = case["graph"]
    assert isinstance(graph, Mapping)
    nodes = graph["nodes"]
    edges = graph["edges"]
    assert isinstance(nodes, list) and isinstance(edges, list)
    initial = _initial_state(nodes)
    terminal_states: list[ReplayState] = []

    def visit(state: ReplayState) -> None:
        if _stopped(state, nodes):
            terminal_states.append(state)
            return
        frontier = _frontier(state, edges)
        if not frontier:
            return
        for edge_index in frontier:
            visit(_advance(state, edge_index, nodes, edges))

    visit(initial)
    if not terminal_states:
        return {"status": "unsupported", "reason": "missing-frontier"}
    best = max(
        terminal_states,
        key=lambda state: (
            state.best_quality,
            _node_terminal(nodes[state.incumbent]),
            tuple(-value for value in state.selected),
        ),
    )
    return {
        "status": "passed",
        "best_quality": best.best_quality,
        "terminal": _node_terminal(nodes[best.incumbent]),
        "selections": len(best.selected),
        "sequence": [int(edges[index]["sequence"]) for index in best.selected],
    }


def _candidate_edge(
    state: ReplayState,
    nodes: Sequence[Mapping[str, object]],
    edges: Sequence[Mapping[str, object]],
) -> tuple[int | None, str | None]:
    frontier = _frontier(state, edges)
    from_incumbent = [
        index for index in frontier if int(edges[index]["from"]) == state.incumbent
    ]
    tried = set(_tried_map(state).get(state.incumbent, ()))
    routes = nodes[state.incumbent].get("routes")
    assert isinstance(routes, list)
    for route in routes:
        if route in tried:
            continue
        matches = [index for index in from_incumbent if edges[index]["route"] == route]
        if len(matches) > 1:
            return None, "ambiguous-route"
        if len(matches) == 1:
            return matches[0], None
    return None, "missing-incumbent-route"


def candidate_replay(case: Mapping[str, object]) -> dict[str, object]:
    """Run the fixed incumbent-only route policy for one case."""

    validate_case(dict(case))
    graph = case["graph"]
    assert isinstance(graph, Mapping)
    nodes = graph["nodes"]
    edges = graph["edges"]
    assert isinstance(nodes, list) and isinstance(edges, list)
    state = _initial_state(nodes)
    while not _stopped(state, nodes):
        edge_index, reason = _candidate_edge(state, nodes, edges)
        if edge_index is None:
            return {"status": "unsupported", "reason": reason}
        state = _advance(state, edge_index, nodes, edges)
    return {
        "status": "passed",
        "best_quality": state.best_quality,
        "terminal": _node_terminal(nodes[state.incumbent]),
        "selections": len(state.selected),
        "sequence": [int(edges[index]["sequence"]) for index in state.selected],
    }


def replay_case(case: Mapping[str, object]) -> dict[str, object]:
    """Compare the fixed policy with the exhaustive trajectory oracle."""

    try:
        validate_case(dict(case))
        graph = case["graph"]
        assert isinstance(graph, Mapping)
        nodes = graph["nodes"]
        assert isinstance(nodes, list)
        baseline_quality = _node_quality(nodes[0])
        oracle = exhaustive_replay(case)
        candidate = candidate_replay(case)
        if oracle.get("status") != "passed":
            return {
                "status": "unsupported",
                "reason": oracle.get("reason") or "unsupported-replay",
            }
        oracle_gain = int(oracle["best_quality"]) - baseline_quality
        if candidate.get("status") != "passed":
            return {
                "status": "unsupported",
                "reason": candidate.get("reason") or "unsupported-replay",
                "oracle": oracle,
                "oracle_gain": oracle_gain,
                "candidate": candidate,
            }
        candidate_gain = int(candidate["best_quality"]) - baseline_quality
        return {
            "status": "passed",
            "oracle": oracle,
            "candidate": candidate,
            "oracle_gain": oracle_gain,
            "candidate_gain": candidate_gain,
        }
    except (ReplayError, KeyError, TypeError, ValueError):
        return {"status": "invalid", "reason": "invalid-case"}


def _aggregate(cases: Sequence[Mapping[str, object]]) -> tuple[dict[str, object], list[dict[str, object]]]:
    results = [replay_case(case) for case in cases]
    invalid = sum(result.get("status") == "invalid" for result in results)
    unsupported = sum(result.get("status") == "unsupported" for result in results)
    valid = [result for result in results if result.get("status") == "passed"]
    oracle_results = [
        result
        for result in results
        if isinstance(result.get("oracle"), Mapping)
        and result["oracle"].get("status") == "passed"
    ]
    oracle_gain = sum(int(result["oracle_gain"]) for result in oracle_results)
    candidate_gain = sum(int(result["candidate_gain"]) for result in valid)
    capture = (candidate_gain * 10_000 // oracle_gain) if oracle_gain > 0 else 0
    campaigns = set()
    targets = set()
    subsystems = set()
    routes = set()
    edges = 0
    terminal_oracle_cases = 0
    terminal_recovered = 0
    positive_gain_cases = 0
    for case, result in zip(cases, results):
        provenance = case.get("provenance")
        classification = case.get("classification")
        graph = case.get("graph")
        if isinstance(provenance, Mapping):
            campaigns.add(provenance.get("campaign_id"))
            targets.add(provenance.get("target"))
        if isinstance(classification, Mapping):
            subsystems.add(classification.get("subsystem"))
        if isinstance(graph, Mapping) and isinstance(graph.get("edges"), list):
            edge_rows = graph["edges"]
            edges += len(edge_rows)
            routes.update(
                edge.get("route")
                for edge in edge_rows
                if isinstance(edge, Mapping)
            )
        oracle = result.get("oracle")
        candidate = result.get("candidate")
        if isinstance(oracle, Mapping) and oracle.get("terminal") is True:
            terminal_oracle_cases += 1
            if (
                result.get("status") == "passed"
                and isinstance(candidate, Mapping)
                and candidate.get("terminal") is True
            ):
                terminal_recovered += 1
        if result.get("status") == "passed":
            if int(result["candidate_gain"]) > 0:
                positive_gain_cases += 1
    aggregate: dict[str, object] = {
        "cases": len(cases),
        "campaigns": len(campaigns),
        "targets": len(targets),
        "subsystems": len(subsystems),
        "routes": len(routes),
        "edges": edges,
        "terminal_oracle_cases": terminal_oracle_cases,
        "terminal_recovered": terminal_recovered,
        "invalid": invalid,
        "unsupported": unsupported,
        "positive_gain_cases": positive_gain_cases,
        "oracle_gain": oracle_gain,
        "candidate_gain": candidate_gain,
        "capture_basis_points": capture,
    }
    return aggregate, results


def _failure_codes(aggregate: Mapping[str, object]) -> list[str]:
    failures: list[str] = []
    for field in ("cases", "campaigns", "targets", "subsystems", "routes", "edges"):
        if int(aggregate[field]) < THRESHOLDS[field]:
            failures.append(f"minimum-{field}")
    if int(aggregate["terminal_oracle_cases"]) < THRESHOLDS["terminal_oracle_cases"]:
        failures.append("minimum-terminal-oracle-cases")
    if int(aggregate["invalid"]) != 0:
        failures.append("invalid-cases")
    if int(aggregate["unsupported"]) != 0:
        failures.append("unsupported-cases")
    if int(aggregate["positive_gain_cases"]) != int(aggregate["cases"]):
        failures.append("nonpositive-candidate-gain")
    if int(aggregate["candidate_gain"]) * 10 < int(aggregate["oracle_gain"]) * 9:
        failures.append("insufficient-gain-capture")
    if int(aggregate["terminal_recovered"]) != int(aggregate["terminal_oracle_cases"]):
        failures.append("terminal-not-recovered")
    return failures


def _tool_binding(root: Path, head: str) -> dict[str, str]:
    """Bind each trust-critical tool to a clean regular HEAD blob."""
    result: dict[str, str] = {}
    for name in TRUSTED_INPUTS:
        relative = Path(name)
        modes = (
            frozenset({"100755"})
            if relative == Path("tools/decomp")
            else frozenset({"100644"})
        )
        saved = _git_blob(
            root, head, relative, MAX_TOOL_BYTES, modes=modes
        )
        current = _regular_bytes(
            root / relative, MAX_TOOL_BYTES, "replay validation dependency"
        )
        try:
            index = subprocess.run(
                ["git", "diff", "--cached", "--quiet", "--", relative.as_posix()],
                cwd=root,
                check=False,
                capture_output=True,
            )
        except OSError as error:
            raise ReplayError("a replay validation dependency is unavailable") from error
        if current != saved or index.returncode != 0:
            raise ReplayError("a replay validation dependency is not clean at HEAD")
        result[relative.as_posix()] = hashlib.sha256(saved).hexdigest()
    return result


def _manifest_matches_head(root: Path, content: bytes, head: str) -> bool:
    """Return true when HEAD tracks the exact current manifest bytes."""

    relative = MANIFEST_RELATIVE.as_posix()
    mode = subprocess.run(
        ["git", "ls-tree", head, "--", relative],
        cwd=root,
        check=False,
        capture_output=True,
    )
    object_name = f"{head}:{relative}"
    size_result = subprocess.run(
        ["git", "cat-file", "-s", object_name],
        cwd=root,
        check=False,
        capture_output=True,
    )
    try:
        object_size = int(size_result.stdout.decode("ascii").strip())
    except (UnicodeDecodeError, ValueError):
        return False
    if size_result.returncode != 0 or not 0 < object_size <= MAX_MANIFEST_BYTES:
        return False
    saved = subprocess.run(
        ["git", "cat-file", "blob", object_name],
        cwd=root,
        check=False,
        capture_output=True,
    )
    index = subprocess.run(
        ["git", "diff", "--cached", "--quiet", "--", relative],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if (
        mode.returncode != 0
        or saved.returncode != 0
        or len(saved.stdout) != object_size
        or saved.stdout != content
        or index.returncode != 0
    ):
        return False
    fields = mode.stdout.decode("ascii", errors="ignore").split()
    return bool(fields and fields[0] == "100644")


def _tracked_content_matches_head(
    root: Path,
    head: str,
    relative: Path,
    content: bytes,
    maximum: int,
) -> bool:
    """Require working and index bytes to equal one regular captured HEAD blob."""

    try:
        saved = _git_blob(root, head, relative, maximum)
        current = _regular_bytes(
            root / relative,
            maximum,
            "replay tracked input",
        )
        index = subprocess.run(
            ["git", "diff", "--cached", "--quiet", "--", relative.as_posix()],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except (ReplayError, OSError, subprocess.SubprocessError):
        return False
    return saved == content == current and index.returncode == 0


def _evaluate_suite_unlocked(
    root: Path,
    manifest: dict[str, object],
    manifest_content: bytes,
) -> dict[str, object]:
    """Evaluate one exact manifest snapshot while the caller holds the suite lock."""

    entries = manifest["cases"]
    assert isinstance(entries, list)
    if not entries:
        aggregate, results = _aggregate([])
        failures = _failure_codes(aggregate)
        document: dict[str, object] = {
            "schema_version": SCHEMA_VERSION,
            "kind": "private-replay-certificate",
            "status": "unavailable",
            "policy_version": POLICY_VERSION,
            "thresholds": dict(THRESHOLDS),
            "bindings": {
                "manifest_sha256": hashlib.sha256(manifest_content).hexdigest(),
                "manifest_bytes": len(manifest_content),
                "cases": [],
                "tools": {},
                "policy_version": POLICY_VERSION,
                "thresholds_sha256": _json_hash(THRESHOLDS),
            },
            "aggregate": aggregate,
            "results": results,
            "failure_codes": failures,
        }
        document["content_sha256"] = _json_hash(document)
        _, final_manifest_content = _manifest_document(root)
        if final_manifest_content != manifest_content:
            raise ReplayError("the replay manifest changed during certification")
        return document

    head = _git_ref(root, "HEAD")
    origin = _git_ref(root, "refs/remotes/origin/agent/continuous")
    branch = _symbolic_branch(root)
    reference_valid = (
        head is not None
        and origin is not None
        and head == origin
        and branch == "agent/continuous"
    )
    transaction_pending = _enrollment_journal_exists(root)
    records: list[dict[str, object]] | None = None
    ledger_content = b""
    if head is not None:
        try:
            ledger_content = _git_blob(
                root, head, LEDGER_RELATIVE, MAX_LEDGER_BYTES
            )
            records = _parse_ledger_records(ledger_content)
        except (ReplayError, OSError, subprocess.SubprocessError):
            records = None
    if records is None or head is None or origin is None:
        cases: list[dict[str, object]] = []
        invalid_results = [
            {"case_id": str(entry["case_id"]), "status": "invalid", "reason": "invalid-case"}
            for entry in entries
        ]
        storage_invalid = 0
        try:
            storage_invalid = len(_case_file_names(root) - {
                f"{entry['case_id']}.json" for entry in entries
            })
        except ReplayError:
            raise
    else:
        cases, invalid_results, storage_invalid = _loaded_cases(
            manifest, root, records, head, origin
        )
    aggregate, results = _aggregate(cases)
    aggregate["cases"] = len(entries)
    aggregate["invalid"] = (
        int(aggregate["invalid"]) + len(invalid_results) + storage_invalid
    )
    failures = _failure_codes(aggregate)
    if (
        int(aggregate["cases"]) != int(aggregate["campaigns"])
        or int(aggregate["cases"]) != int(aggregate["targets"])
    ):
        failures.append("duplicate-campaign-or-target")
    if head is None or not _manifest_matches_head(root, manifest_content, head):
        failures.append("manifest-not-clean-at-head")
    if not reference_valid:
        failures.append("head-not-current-origin")
    if transaction_pending:
        failures.append("enrollment-transaction-pending")
    if (
        head is None
        or not _private_root_is_untracked(root, head)
    ):
        failures.append("private-root-tracked")
    if (
        head is None
        or not ledger_content
        or not _tracked_content_matches_head(
            root,
            head,
            LEDGER_RELATIVE,
            ledger_content,
            MAX_LEDGER_BYTES,
        )
    ):
        failures.append("campaign-ledger-not-clean-at-head")
    tools: dict[str, str] = {}
    if head is not None:
        try:
            tools = _tool_binding(root, head)
        except (ReplayError, OSError, subprocess.SubprocessError):
            failures.append("validation-dependencies-not-clean")
    else:
        failures.append("validation-dependencies-not-clean")
    failures = list(dict.fromkeys(failures))
    bindings = {
        "head": head,
        "origin_head": origin,
        "branch": branch,
        "ledger_sha256": hashlib.sha256(ledger_content).hexdigest(),
        "ledger_bytes": len(ledger_content),
        "manifest_sha256": hashlib.sha256(manifest_content).hexdigest(),
        "manifest_bytes": len(manifest_content),
        "cases": [
            {
                "case_id": entry["case_id"],
                "bytes": entry["bytes"],
                "sha256": entry["sha256"],
            }
            for entry in entries
        ],
        "tools": tools,
        "policy_version": POLICY_VERSION,
        "thresholds_sha256": _json_hash(THRESHOLDS),
    }
    document = {
        "schema_version": SCHEMA_VERSION,
        "kind": "private-replay-certificate",
        "status": "passed" if not failures else "failed",
        "policy_version": POLICY_VERSION,
        "thresholds": dict(THRESHOLDS),
        "bindings": bindings,
        "aggregate": aggregate,
        "results": [
            {"case_id": case["case_id"], **result}
            for case, result in zip(cases, results)
        ]
        + invalid_results
        + ([{"status": "invalid", "reason": "private-storage-mismatch"}] if storage_invalid else []),
        "failure_codes": failures,
    }
    document["content_sha256"] = _json_hash(document)
    _, final_manifest_content = _manifest_document(root)
    if final_manifest_content != manifest_content:
        raise ReplayError("the replay manifest changed during certification")
    if head != _git_ref(root, "HEAD") or origin != _git_ref(
        root, "refs/remotes/origin/agent/continuous"
    ) or branch != _symbolic_branch(root):
        raise ReplayError("the replay Git references changed during certification")
    if ledger_content and _git_blob(
        root, head or "", LEDGER_RELATIVE, MAX_LEDGER_BYTES
    ) != ledger_content:
        raise ReplayError("the campaign ledger changed during certification")
    return document


def evaluate_suite(root: Path = ROOT) -> dict[str, object]:
    """Evaluate the current private suite without changing it."""

    root = root.resolve()
    manifest, manifest_content = _manifest_document(root)
    if not manifest["cases"]:
        return _evaluate_suite_unlocked(root, manifest, manifest_content)
    if not _private_root_exists(root):
        return _evaluate_suite_unlocked(root, manifest, manifest_content)
    with _suite_lock(root, create=False, exclusive=False):
        locked_manifest, locked_content = _manifest_document(root)
        return _evaluate_suite_unlocked(root, locked_manifest, locked_content)


def _public_certificate(document: Mapping[str, object]) -> dict[str, object]:
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "private-replay-certificate-summary",
        "status": document.get("status"),
        "certified": document.get("status") == "passed",
        "policy_version": POLICY_VERSION,
        "thresholds": document.get("thresholds"),
        "aggregate": document.get("aggregate"),
        "failure_codes": document.get("failure_codes"),
    }


def certify(root: Path = ROOT, *, publish: bool = True) -> dict[str, object]:
    """Evaluate the suite and publish a local content-addressed certificate."""

    root = root.resolve()
    manifest, manifest_content = _manifest_document(root)
    if not manifest["cases"]:
        return _public_certificate(
            _evaluate_suite_unlocked(root, manifest, manifest_content)
        )
    if not _private_root_exists(root):
        return _public_certificate(
            _evaluate_suite_unlocked(root, manifest, manifest_content)
        )
    with _suite_lock(root, create=False, exclusive=True):
        locked_manifest, locked_content = _manifest_document(root)
        document = _evaluate_suite_unlocked(root, locked_manifest, locked_content)
        if not publish:
            return _public_certificate(document)
        head = _git_ref(root, "HEAD")
        origin = _git_ref(root, "refs/remotes/origin/agent/continuous")
        try:
            safe_publication = (
                head is not None
                and head == origin
                and _symbolic_branch(root) == "agent/continuous"
                and _manifest_matches_head(root, locked_content, head)
                and _private_root_is_untracked(root, head)
            )
            if safe_publication:
                _tool_binding(root, head)
        except (ReplayError, OSError, subprocess.SubprocessError):
            safe_publication = False
        if not safe_publication:
            failed = dict(document)
            failed["status"] = "failed"
            failure_codes = document.get("failure_codes")
            existing_codes = failure_codes if isinstance(failure_codes, list) else []
            failed["failure_codes"] = sorted(
                {
                    *(str(code) for code in existing_codes),
                    "publication-inputs-invalid",
                }
            )
            return _public_certificate(failed)
        content = json.dumps(document, indent=2, sort_keys=True).encode("utf-8") + b"\n"
        if len(content) > MAX_CERTIFICATE_BYTES:
            raise ReplayError("the private replay certificate exceeds its size limit")
        with _private_directory(root, CERTIFICATES_NAME, create=True) as directory_fd:
            name = f"{document['content_sha256']}.json"
            _write_exclusive_at(directory_fd, name, content)
        pointer = {
            "schema_version": SCHEMA_VERSION,
            "kind": "private-replay-certificate-pointer",
            "content_sha256": document["content_sha256"],
            "bytes": len(content),
            "sha256": hashlib.sha256(content).hexdigest(),
        }
        pointer_content = json.dumps(pointer, indent=2, sort_keys=True).encode("utf-8") + b"\n"
        with _private_directory(root, create=True) as directory_fd:
            _replace_at(directory_fd, CERTIFICATE_POINTER_NAME, pointer_content)
        return _public_certificate(document)


def _current_certificate(root: Path) -> dict[str, object] | None:
    try:
        with _private_directory(root) as directory_fd:
            pointer_content = _read_at(
                directory_fd,
                CERTIFICATE_POINTER_NAME,
                MAX_CERTIFICATE_BYTES,
                "private replay certificate pointer",
            )
        pointer = _strict_json(pointer_content, "private replay certificate pointer")
        _strict_keys(
            pointer,
            {"schema_version", "kind", "content_sha256", "bytes", "sha256"},
            "private replay certificate pointer",
        )
        if (
            type(pointer.get("schema_version")) is not int
            or pointer.get("schema_version") != SCHEMA_VERSION
            or pointer.get("kind") != "private-replay-certificate-pointer"
        ):
            raise ReplayError("the private replay certificate pointer is invalid")
        content_hash = _sha256(pointer.get("content_sha256"), "certificate content hash")
        expected_bytes = _positive_int(
            pointer.get("bytes"), "certificate byte count", MAX_CERTIFICATE_BYTES
        )
        expected_hash = _sha256(pointer.get("sha256"), "certificate file hash")
        with _private_directory(root, CERTIFICATES_NAME) as directory_fd:
            content = _read_at(
                directory_fd,
                f"{content_hash}.json",
                MAX_CERTIFICATE_BYTES,
                "private replay certificate",
            )
        if len(content) != expected_bytes or hashlib.sha256(content).hexdigest() != expected_hash:
            raise ReplayError("the private replay certificate pointer is stale")
        document = _strict_json(content, "private replay certificate")
        if (
            document.get("content_sha256") != content_hash
            or _json_hash(document, omit="content_sha256") != content_hash
        ):
            raise ReplayError("the private replay certificate content hash is invalid")
        return document
    except ReplayError:
        return None


def current_passing_certificate(root: Path = ROOT) -> dict[str, object] | None:
    """Return a passing certificate only when all current inputs still match."""

    root = root.resolve()
    try:
        manifest, _ = _manifest_document(root)
        if not manifest["cases"] or not _private_root_exists(root):
            return None
        with _suite_lock(root, create=False, exclusive=False):
            locked_manifest, locked_content = _manifest_document(root)
            if not locked_manifest["cases"]:
                return None
            saved = _current_certificate(root)
            if saved is None or saved.get("status") != "passed":
                return None
            current = _evaluate_suite_unlocked(
                root,
                locked_manifest,
                locked_content,
            )
            return saved if current == saved else None
    except Exception:
        return None


def list_document(root: Path = ROOT) -> dict[str, object]:
    """Return the public replay manifest without private case content."""

    manifest = read_manifest(root)
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "private-replay-list",
        "case_count": len(manifest["cases"]),
    }


def _safe_summary(value: Mapping[str, object]) -> str:
    if value.get("kind") == "private-replay-list":
        return f"Private replay cases: {value.get('case_count', 0)}"
    if value.get("kind") == "private-replay-certificate-summary":
        return (
            f"Replay certificate: {value.get('status')}\n"
            f"Cases: {value.get('aggregate', {}).get('cases', 0) if isinstance(value.get('aggregate'), Mapping) else 0}"
        )
    if value.get("kind") == "private-replay-enrollment":
        return f"Private replay cases: {value.get('case_count', 0)}"
    return json.dumps(value, sort_keys=True)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    list_parser = subparsers.add_parser("list", help="List public private-case entries.")
    list_parser.add_argument("--json", action="store_true")
    certify_parser = subparsers.add_parser(
        "certify", help="Evaluate and save the current private replay certificate."
    )
    certify_parser.add_argument("--json", action="store_true")
    run_parser = subparsers.add_parser(
        "run", help="Evaluate the suite without saving a certificate."
    )
    run_parser.add_argument("--json", action="store_true")
    verify_parser = subparsers.add_parser(
        "verify", help="Revalidate the saved replay certificate."
    )
    verify_parser.add_argument("--json", action="store_true")
    enroll_parser = subparsers.add_parser(
        "enroll", help="Enroll one atomic batch of delivered experiments."
    )
    enroll_parser.add_argument(
        "--case",
        action="append",
        required=True,
        metavar="ADDRESS,SESSION_ID",
        help="Add one delivered experiment to this atomic batch.",
    )
    enroll_parser.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "list":
            value = list_document(ROOT)
        elif arguments.command == "certify":
            value = certify(ROOT)
        elif arguments.command == "run":
            value = _public_certificate(evaluate_suite(ROOT))
        elif arguments.command == "verify":
            current = current_passing_certificate(ROOT)
            value = (
                _public_certificate(current)
                if current is not None
                else {
                    "schema_version": SCHEMA_VERSION,
                    "kind": "private-replay-certificate-summary",
                    "status": "withheld",
                    "certified": False,
                    "policy_version": POLICY_VERSION,
                    "thresholds": dict(THRESHOLDS),
                    "aggregate": None,
                    "failure_codes": ["certificate-unavailable"],
                }
            )
        elif arguments.command == "enroll":
            candidates: list[tuple[str, str]] = []
            for item in arguments.case:
                target, separator, session_id = item.partition(",")
                if not separator or not target or not session_id or "," in session_id:
                    raise ReplayError("the replay enrollment case is invalid")
                candidates.append((target, session_id))
            value = enroll_batch(candidates, root=ROOT)
        else:
            raise ReplayError("the replay command is invalid")
        if arguments.json:
            print(json.dumps(value, indent=2, sort_keys=True))
        else:
            print(_safe_summary(value))
        return 0
    except Exception:
        print("error: the private replay operation failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
