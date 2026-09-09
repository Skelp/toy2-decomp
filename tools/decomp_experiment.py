#!/usr/bin/env python3
"""Manage bounded, provenance-safe source experiment sessions.

The controller only records experiment state. Platform wrappers own builds and
comparison generation, so controller failures cannot trigger hidden work.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager, nullcontext
import ctypes
from datetime import datetime, timezone
import hashlib
import errno
import json
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
from typing import Iterator, Mapping, Sequence
import uuid

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.decomp_mismatch import ROUTE_ORDER  # noqa: E402
from tools.decomp_quality import candidate_quality  # noqa: E402
from tools.decomp_campaigns import (  # noqa: E402
    _snapshot_hash,
    attach_replay_experiment,
    source_worktree_snapshot,
)
from tools.decomp_provenance import (  # noqa: E402
    file_hash,
    provenance_path,
    validate_diff,
    validate_report_artifact,
)
from tools.decomp_status import MatchStatus, read_match_statuses  # noqa: E402
from tools.decomp_verify import experiment_record  # noqa: E402


SCHEMA_VERSION = 1
DEFAULT_MAX_TRIALS = 3
DEFAULT_MAX_NON_IMPROVING = 2
MAX_MAX_TRIALS = 100
MAX_RECORD_BYTES = 2 * 1024 * 1024
MAX_REPORT_BYTES = 64 * 1024 * 1024
MAX_DIFF_BYTES = 32 * 1024 * 1024
MAX_PATCH_BYTES = 32 * 1024 * 1024
MAX_CONTEXT_BYTES = 4 * 1024 * 1024
MAX_BRIEF_BYTES = 16 * 1024 * 1024
MAX_TRAJECTORY_BYTES = 16 * 1024 * 1024
MAX_NORMALIZED_ROWS = 128
MAX_NORMALIZED_TEXT = 240
MAX_TRIAL_DIRECTORY_ENTRIES = MAX_MAX_TRIALS + 16
SCORE_EPSILON = 1e-12

EXPERIMENTS_RELATIVE = Path("build/decomp-experiments")
CAMPAIGN_STATE_RELATIVE = Path("build/decomp-campaign-state.json")
FUNCTION_MAP_RELATIVE = Path("tools/Resources/functions_map.txt")
LABEL_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
SESSION_RE = re.compile(r"[0-9]{8}T[0-9]{6}Z-[0-9a-f]{12}\Z")
TRIAL_RE = re.compile(r"[0-9]{3}-[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
HASH_RE = re.compile(r"[0-9a-f]{64}\Z")
WINDOWS_RESERVED = {
    "con",
    "prn",
    "aux",
    "nul",
    *(f"com{number}" for number in range(1, 10)),
    *(f"lpt{number}" for number in range(1, 10)),
}
FAILURE_STAGES = {
    "build",
    "comparison",
    "diff",
    "normalize",
    "interrupted",
    "baseline-build",
    "baseline-comparison",
}
STATUS_RANK = {"provisional": 1, "effective": 2, "exact": 3}
TOOL_FILES = (
    "reccmp-project.yml",
    "reccmp-user.yml",
    "tools/__init__.py",
    "tools/decomp",
    "tools/decomp.ps1",
    "tools/decomp_campaigns.py",
    "tools/decomp_experiment.py",
    "tools/decomp_diff.py",
    "tools/decomp_mismatch.py",
    "tools/decomp_provenance.py",
    "tools/decomp_quality.py",
    "tools/decomp_status.py",
    "tools/decomp_verify.py",
    "tools/generate-decomp-data-report.py",
)
COMPILER_CONTEXT_KEYS = (
    "git_head",
    "build_context_sha256",
    "build_rules_sha256",
    "compiler_driver_sha256",
    "compiler_backend_sha256",
    "sdk_headers_sha256",
    "vc6_headers_sha256",
    "reccmp_git_head",
)
SOURCE_SNAPSHOT_FIELD = "source_worktree_snapshot"
PARENT_SNAPSHOT_FIELD = "parent_source_worktree_snapshot"


class ExperimentError(ValueError):
    """Report an invalid experiment operation."""


def _reject_json_constant(_value: str) -> object:
    raise ExperimentError("an experiment JSON document contains a non-finite number")


def _strict_json_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise ExperimentError("an experiment JSON document contains a duplicate key")
        value[key] = item
    return value


def _now() -> datetime:
    return datetime.now(timezone.utc)


def _timestamp(value: datetime | None = None) -> str:
    return (value or _now()).isoformat(timespec="seconds")


def _canonical_json(value: object) -> bytes:
    return json.dumps(
        value, allow_nan=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def receipt_id(document: Mapping[str, object]) -> str:
    value = dict(document)
    value.pop("receipt_id", None)
    return hashlib.sha256(_canonical_json(value)).hexdigest()


def normalize_address(value: str | int) -> str:
    try:
        address = value if isinstance(value, int) else int(value, 0)
    except (TypeError, ValueError) as error:
        raise ExperimentError("the experiment address is invalid") from error
    if not 0 <= address <= 0xFFFFFFFF:
        raise ExperimentError("the experiment address is outside the 32-bit range")
    return f"0x{address:08X}"


def _address_int(value: str | int) -> int:
    return int(normalize_address(value), 16)


def validate_label(label: str) -> str:
    if (
        LABEL_RE.fullmatch(label) is None
        or label.endswith((".", " "))
        or label.casefold().split(".", 1)[0] in WINDOWS_RESERVED
        or label.casefold() in {"baseline", "sessions"}
    ):
        raise ExperimentError(
            "give the trial a unique label with letters, numbers, dots, dashes, or underscores"
        )
    return label


def _root(path: Path) -> Path:
    return path.resolve()


def _safe_relative(path: Path, root: Path, *, must_exist: bool = True) -> str:
    root = _root(root)
    candidate = path if path.is_absolute() else root / path
    lexical = Path(os.path.abspath(candidate))
    try:
        relative = lexical.relative_to(root)
    except ValueError as error:
        raise ExperimentError("an experiment path escapes the repository") from error
    current = root
    for part in relative.parts:
        current = current / part
        try:
            metadata = os.lstat(current)
        except FileNotFoundError:
            if must_exist:
                raise ExperimentError("an experiment path is missing")
            break
        if stat.S_ISLNK(metadata.st_mode):
            raise ExperimentError("an experiment path uses a symbolic link")
    resolved = lexical.resolve(strict=must_exist)
    if resolved != lexical:
        raise ExperimentError("an experiment path uses a symbolic link")
    return relative.as_posix()


def _resolve_relative(relative: object, root: Path, *, parent: Path | None = None) -> Path:
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise ExperimentError("an experiment receipt has an invalid path")
    root = _root(root)
    lexical = root / relative
    safe_relative = _safe_relative(lexical, root, must_exist=False)
    path = root / safe_relative
    try:
        path.resolve().relative_to(_root(parent or root))
    except ValueError as error:
        raise ExperimentError("an experiment receipt path escapes its directory") from error
    return path


def _open_read_nofollow(path: Path) -> int:
    """Open one absolute file through no-follow directory descriptors on POSIX."""

    absolute = Path(os.path.abspath(path))
    if os.name != "posix" or os.open not in os.supports_dir_fd:
        for candidate in (absolute, *absolute.parents):
            try:
                if stat.S_ISLNK(os.lstat(candidate).st_mode):
                    raise OSError
            except FileNotFoundError:
                raise OSError
        return os.open(absolute, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    parts = absolute.parts
    directory = os.open(parts[0], os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in parts[1:-1]:
            child = os.open(
                part,
                os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        return os.open(
            parts[-1],
            os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0),
            dir_fd=directory,
        )
    finally:
        os.close(directory)


def _open_directory_nofollow(path: Path) -> int:
    """Open one absolute directory while holding every no-follow component."""

    absolute = Path(os.path.abspath(path))
    if os.name != "posix" or os.open not in os.supports_dir_fd:
        for candidate in (absolute, *absolute.parents):
            try:
                if stat.S_ISLNK(os.lstat(candidate).st_mode):
                    raise OSError
            except FileNotFoundError:
                raise OSError
        return os.open(absolute, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    parts = absolute.parts
    directory = os.open(parts[0], os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in parts[1:]:
            child = os.open(
                part,
                os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        return directory
    except Exception:
        os.close(directory)
        raise


def _ensure_directory_nofollow(path: Path) -> int:
    """Create missing directory components without following symbolic links."""

    absolute = Path(os.path.abspath(path))
    if os.name != "posix" or os.mkdir not in os.supports_dir_fd:
        absolute.mkdir(parents=True, exist_ok=True)
        return _open_directory_nofollow(absolute)
    parts = absolute.parts
    directory = os.open(parts[0], os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in parts[1:]:
            try:
                os.mkdir(part, 0o700, dir_fd=directory)
            except FileExistsError:
                pass
            child = os.open(
                part,
                os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                dir_fd=directory,
            )
            os.close(directory)
            directory = child
        return directory
    except Exception:
        os.close(directory)
        raise


def _read_bytes(
    path: Path, maximum: int, description: str, *, allow_empty: bool = False
) -> bytes:
    try:
        descriptor = _open_read_nofollow(path)
        with os.fdopen(descriptor, "rb") as stream:
            before = os.fstat(stream.fileno())
            content = stream.read(maximum + 1)
            after = os.fstat(stream.fileno())
        current_uid = os.geteuid() if hasattr(os, "geteuid") else None
        stable = (
            before.st_dev,
            before.st_ino,
            before.st_size,
            before.st_mtime_ns,
            before.st_ctime_ns,
        ) == (
            after.st_dev,
            after.st_ino,
            after.st_size,
            after.st_mtime_ns,
            after.st_ctime_ns,
        )
        if (
            not stat.S_ISREG(after.st_mode)
            or after.st_nlink != 1
            or (current_uid is not None and after.st_uid != current_uid)
            or not stable
            or after.st_size > maximum
            or (after.st_size == 0 and not allow_empty)
            or len(content) != after.st_size
        ):
            raise OSError
        return content
    except OSError as error:
        raise ExperimentError(f"the {description} is missing or invalid") from error


def _read_json(path: Path, maximum: int, description: str) -> dict[str, object]:
    raw = _read_bytes(path, maximum, description)
    try:
        value = json.loads(
            raw.decode("utf-8-sig"),
            object_pairs_hook=_strict_json_pairs,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ExperimentError(f"the {description} is not a JSON object") from error
    if not isinstance(value, dict):
        raise ExperimentError(f"the {description} is not a JSON object")
    return value


def _atomic_write(path: Path, content: bytes, *, exclusive: bool = False) -> None:
    if os.name != "posix" or os.open not in os.supports_dir_fd:
        path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
        )
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
            if exclusive:
                os.link(temporary_name, path, follow_symlinks=False)
            else:
                os.replace(temporary_name, path)
        except FileExistsError as error:
            raise ExperimentError("the immutable experiment receipt already exists") from error
        finally:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass
        return

    directory = -1
    descriptor = -1
    temporary_name = f".{path.name}.{uuid.uuid4().hex}.tmp"
    temporary_identity: tuple[int, int] | None = None
    immutable_published = False
    try:
        directory = _ensure_directory_nofollow(path.parent)
        descriptor = os.open(
            temporary_name,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
            0o600,
            dir_fd=directory,
        )
        with os.fdopen(descriptor, "wb") as stream:
            descriptor = -1
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
            metadata = os.fstat(stream.fileno())
            current_uid = os.geteuid() if hasattr(os, "geteuid") else None
            if (
                not stat.S_ISREG(metadata.st_mode)
                or metadata.st_nlink != 1
                or (current_uid is not None and metadata.st_uid != current_uid)
            ):
                raise OSError
            temporary_identity = (metadata.st_dev, metadata.st_ino)
        if exclusive:
            try:
                rename_noreplace = ctypes.CDLL(None, use_errno=True).renameat2
            except AttributeError as error:
                raise ExperimentError("secure experiment receipt publication is unavailable") from error
            rename_noreplace.argtypes = [
                ctypes.c_int,
                ctypes.c_char_p,
                ctypes.c_int,
                ctypes.c_char_p,
                ctypes.c_uint,
            ]
            rename_noreplace.restype = ctypes.c_int
            if rename_noreplace(
                directory,
                os.fsencode(temporary_name),
                directory,
                os.fsencode(path.name),
                1,
            ) != 0:
                saved_errno = ctypes.get_errno()
                if saved_errno == errno.EEXIST:
                    error = FileExistsError(path.name)
                else:
                    raise OSError(saved_errno, os.strerror(saved_errno))
                raise ExperimentError("the immutable experiment receipt already exists") from error
            immutable_published = True
        else:
            os.replace(
                temporary_name,
                path.name,
                src_dir_fd=directory,
                dst_dir_fd=directory,
            )
        published = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory)
        with os.fdopen(published, "rb") as stream:
            published_metadata = os.fstat(stream.fileno())
            saved = stream.read(len(content) + 1)
        current_uid = os.geteuid() if hasattr(os, "geteuid") else None
        if (
            not stat.S_ISREG(published_metadata.st_mode)
            or published_metadata.st_nlink != 1
            or (current_uid is not None and published_metadata.st_uid != current_uid)
            or published_metadata.st_dev != metadata.st_dev
            or published_metadata.st_ino != metadata.st_ino
            or saved != content
            or published_metadata.st_size != len(content)
        ):
            raise OSError
        os.fsync(directory)
    except ExperimentError:
        raise
    except OSError as error:
        if immutable_published and temporary_identity is not None and directory >= 0:
            try:
                published = os.open(
                    path.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory
                )
                try:
                    current = os.fstat(published)
                finally:
                    os.close(published)
                if (current.st_dev, current.st_ino) == temporary_identity:
                    os.unlink(path.name, dir_fd=directory)
                    os.fsync(directory)
            except OSError:
                pass
        raise ExperimentError("cannot publish the experiment receipt") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if directory >= 0:
            try:
                os.unlink(temporary_name, dir_fd=directory)
            except FileNotFoundError:
                pass
            os.close(directory)


def write_receipt(path: Path, document: Mapping[str, object]) -> dict[str, object]:
    value = dict(document)
    value["schema_version"] = SCHEMA_VERSION
    value["receipt_id"] = receipt_id(value)
    _atomic_write(
        path,
        json.dumps(value, indent=2, sort_keys=True).encode("utf-8") + b"\n",
        exclusive=True,
    )
    return value


def read_receipt(
    path: Path, *, kind: str | None = None, maximum: int = MAX_RECORD_BYTES
) -> dict[str, object]:
    value = _read_json(path, maximum, "experiment receipt")
    if (
        type(value.get("schema_version")) is not int
        or value.get("schema_version") != SCHEMA_VERSION
    ):
        raise ExperimentError("the experiment receipt schema is invalid")
    saved_id = value.get("receipt_id")
    if not isinstance(saved_id, str) or saved_id != receipt_id(value):
        raise ExperimentError("the experiment receipt identity is invalid")
    if kind is not None and value.get("kind") != kind:
        raise ExperimentError("the experiment receipt kind is invalid")
    return value


@contextmanager
def _close_descriptor(descriptor: int) -> Iterator[None]:
    try:
        yield
    finally:
        os.close(descriptor)


@contextmanager
def _state_lock(root: Path) -> Iterator[None]:
    path = _root(root) / EXPERIMENTS_RELATIVE / ".state.lock"
    if os.name == "posix" and os.open in os.supports_dir_fd:
        directory = _ensure_directory_nofollow(path.parent)
        try:
            descriptor = os.open(
                path.name,
                os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW,
                0o600,
                dir_fd=directory,
            )
        except OSError as error:
            os.close(directory)
            raise ExperimentError("the experiment state lock is invalid") from error
        stream_context = os.fdopen(descriptor, "a+b")
        directory_context = _close_descriptor(directory)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        stream_context = path.open("a+b")
        directory_context = nullcontext()
    with directory_context, stream_context as stream:
        metadata = os.fstat(stream.fileno())
        current_uid = os.geteuid() if hasattr(os, "geteuid") else None
        if (
            not stat.S_ISREG(metadata.st_mode)
            or getattr(metadata, "st_nlink", 1) != 1
            or metadata.st_mode & 0o077
            or (current_uid is not None and metadata.st_uid != current_uid)
        ):
            raise ExperimentError("the experiment state lock is invalid")
        if os.name == "nt":
            import msvcrt

            stream.seek(0)
            if stream.tell() == 0:
                stream.write(b"0")
                stream.flush()
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_LOCK, 1)
            try:
                yield
            finally:
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl

            fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
            try:
                entry = os.stat(
                    path.name,
                    dir_fd=directory,
                    follow_symlinks=False,
                )
                if (
                    entry.st_dev,
                    entry.st_ino,
                ) != (metadata.st_dev, metadata.st_ino):
                    raise ExperimentError("the experiment state lock changed")
                yield
            finally:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def _head(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    head = result.stdout.strip().lower()
    if result.returncode != 0 or re.fullmatch(r"[0-9a-f]{40,64}", head) is None:
        raise ExperimentError("cannot read the experiment HEAD")
    return head


def _tool_identity(root: Path) -> dict[str, object]:
    return {
        relative: file_hash(root / relative) if (root / relative).is_file() else None
        for relative in TOOL_FILES
    }


def _bound_source_snapshot(
    root: Path, comparison_identity: Mapping[str, object]
) -> dict[str, str]:
    """Return the full source snapshot that one comparison measured."""

    snapshot = source_worktree_snapshot(root)
    if _snapshot_hash(snapshot) != comparison_identity.get("source_worktree_sha256"):
        raise ExperimentError("the comparison source snapshot is stale")
    return snapshot


def _descriptor(
    path: Path,
    root: Path,
    *,
    maximum: int,
    description: str,
    allow_empty: bool = False,
) -> dict[str, object]:
    candidate = path if path.is_absolute() else _root(root) / path
    relative = _safe_relative(candidate, root, must_exist=False)
    raw = _read_bytes(candidate, maximum, description, allow_empty=allow_empty)
    return {
        "path": relative,
        "sha256": hashlib.sha256(raw).hexdigest(),
        "bytes": len(raw),
    }


def _validate_descriptor(
    descriptor: object,
    root: Path,
    *,
    maximum: int,
    description: str,
    parent: Path | None = None,
    allow_empty: bool = False,
) -> Path:
    if not isinstance(descriptor, dict):
        raise ExperimentError(f"the {description} descriptor is invalid")
    path = _resolve_relative(descriptor.get("path"), root, parent=parent)
    raw = _read_bytes(path, maximum, description, allow_empty=allow_empty)
    if descriptor != {
        "path": _safe_relative(path, root),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "bytes": len(raw),
    }:
        raise ExperimentError(f"the {description} changed after it was recorded")
    return path


def _report_descriptor(path: Path, root: Path) -> tuple[dict[str, object], dict[str, object]]:
    report = _descriptor(path, root, maximum=MAX_REPORT_BYTES, description="comparison report")
    try:
        receipt = validate_report_artifact(path)
    except ValueError as error:
        raise ExperimentError(f"the comparison report provenance is invalid: {error}") from error
    sidecar = _descriptor(
        provenance_path(path),
        root,
        maximum=MAX_CONTEXT_BYTES,
        description="comparison report provenance",
    )
    report["provenance"] = sidecar
    report["provenance_receipt_id"] = receipt.get("receipt_id")
    return report, receipt


def _validate_report_descriptor(
    descriptor: object, root: Path, *, parent: Path
) -> tuple[Path, dict[str, object]]:
    if not isinstance(descriptor, dict):
        raise ExperimentError("the comparison report descriptor is invalid")
    core = {key: descriptor.get(key) for key in ("path", "sha256", "bytes")}
    path = _validate_descriptor(
        core,
        root,
        maximum=MAX_REPORT_BYTES,
        description="comparison report",
        parent=parent,
    )
    provenance = descriptor.get("provenance")
    _validate_descriptor(
        provenance,
        root,
        maximum=MAX_CONTEXT_BYTES,
        description="comparison report provenance",
        parent=parent,
    )
    try:
        receipt = validate_report_artifact(path)
    except ValueError as error:
        raise ExperimentError(f"the comparison report provenance is invalid: {error}") from error
    if receipt.get("receipt_id") != descriptor.get("provenance_receipt_id"):
        raise ExperimentError("the comparison report provenance receipt changed")
    return path, receipt


def _campaign_context(root: Path) -> dict[str, object] | None:
    path = root / CAMPAIGN_STATE_RELATIVE
    if not path.exists():
        return None
    value = _read_json(path, MAX_RECORD_BYTES, "active campaign state")
    campaign_id = value.get("campaign_id")
    if not isinstance(campaign_id, str) or not campaign_id:
        raise ExperimentError("the active campaign has no campaign ID")
    addresses = value.get("active_addresses", value.get("addresses", []))
    if not isinstance(addresses, list):
        raise ExperimentError("the active campaign target list is invalid")
    deadlines = value.get("deadlines", {})
    if not isinstance(deadlines, dict):
        raise ExperimentError("the active campaign deadline is invalid")
    deadline = deadlines.get("extension_deadline") or deadlines.get("stop_deadline")
    if deadline is not None and not isinstance(deadline, str):
        raise ExperimentError("the active campaign deadline is invalid")
    return {
        "campaign_id": campaign_id,
        "mode": value.get("mode"),
        "lane": value.get("lane"),
        "active_addresses": [normalize_address(item) for item in addresses],
        "campaign_head": value.get("campaign_head"),
        "deadline": deadline,
    }


def _parse_time(value: object) -> datetime | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise ExperimentError("the campaign deadline is invalid")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise ExperimentError("the campaign deadline is invalid") from error
    if parsed.tzinfo is None:
        raise ExperimentError("the campaign deadline needs a UTC offset")
    return parsed.astimezone(timezone.utc)


def _brief_descriptors(
    brief: Path | None, root: Path, address: str
) -> tuple[dict[str, object] | None, dict[str, object] | None]:
    if brief is None:
        return None, None
    _safe_relative(brief, root)
    value = _read_json(brief, MAX_BRIEF_BYTES, "experiment brief")
    if normalize_address(value.get("target", "")) != address:
        raise ExperimentError("the experiment brief names another target")
    brief_descriptor = _descriptor(
        brief, root, maximum=MAX_BRIEF_BYTES, description="experiment brief"
    )
    inputs = value.get("inputs")
    doctor_value = inputs.get("doctor_receipt") if isinstance(inputs, dict) else None
    doctor_descriptor = None
    if isinstance(doctor_value, dict) and doctor_value.get("path"):
        doctor_path = Path(str(doctor_value["path"]))
        doctor_descriptor = _descriptor(
            doctor_path,
            root,
            maximum=MAX_BRIEF_BYTES,
            description="doctor receipt",
        )
        expected_hash = doctor_value.get("sha256")
        if expected_hash is not None and doctor_descriptor["sha256"] != expected_hash:
            raise ExperimentError("the doctor receipt does not match the brief")
    return brief_descriptor, doctor_descriptor


def _session_base(root: Path, address: str) -> Path:
    return _root(root) / EXPERIMENTS_RELATIVE / address


def _new_session_id(base: Path, now: datetime | None = None) -> str:
    prefix = (now or _now()).strftime("%Y%m%dT%H%M%SZ")
    for _ in range(32):
        value = f"{prefix}-{uuid.uuid4().hex[:12]}"
        if not (base / "sessions" / value).exists():
            return value
    raise ExperimentError("cannot allocate a unique experiment session ID")


def _plan_lines(plan: Mapping[str, object], fields: Sequence[str]) -> str:
    values = []
    for field in fields:
        value = plan.get(field)
        if not isinstance(value, (str, int)):
            raise ExperimentError(f"the experiment plan has no {field}")
        values.append(str(value))
    return "\n".join(values)


def begin_session(
    address_value: str | int,
    *,
    root: Path = ROOT,
    brief: Path | None = None,
    max_trials: int = DEFAULT_MAX_TRIALS,
    max_non_improving: int = DEFAULT_MAX_NON_IMPROVING,
    now: datetime | None = None,
) -> dict[str, object]:
    root = _root(root)
    address = normalize_address(address_value)
    if not 1 <= max_trials <= MAX_MAX_TRIALS:
        raise ExperimentError(f"--max-trials must be from 1 through {MAX_MAX_TRIALS}")
    if not 1 <= max_non_improving <= MAX_MAX_TRIALS:
        raise ExperimentError(
            f"--max-non-improving must be from 1 through {MAX_MAX_TRIALS}"
        )
    brief_descriptor, doctor_descriptor = _brief_descriptors(brief, root, address)
    campaign = _campaign_context(root)
    if campaign and campaign["active_addresses"] and address not in campaign["active_addresses"]:
        raise ExperimentError("the experiment target is not active in the campaign")
    base = _session_base(root, address)
    with _state_lock(root):
        session_id = _new_session_id(base, now)
        directory = base / "sessions" / session_id
        baseline = directory / "baseline"
        baseline.mkdir(parents=True, exist_ok=False)
        plan: dict[str, object] = {
            "kind": "experiment-session-start",
            "address": address,
            "session_id": session_id,
            "created_at": _timestamp(now),
            "head": _head(root),
            "campaign": campaign,
            "brief": brief_descriptor,
            "doctor_receipt": doctor_descriptor,
            "limits": {
                "max_trials": max_trials,
                "max_non_improving": max_non_improving,
            },
            "function_map": _descriptor(
                root / FUNCTION_MAP_RELATIVE,
                root,
                maximum=MAX_CONTEXT_BYTES,
                description="function map",
            ),
            "tool_identity": _tool_identity(root),
        }
        write_receipt(directory / "start.json", plan)
    return {
        "address": address,
        "session_id": session_id,
        "session_directory": str(directory),
        "baseline_report": str(baseline / "report.json"),
        "baseline_data_report": str(baseline / "data-report.json"),
        "compiler_context": str(baseline / "compiler-context.json"),
        "source_patch": str(baseline / "source.patch"),
        "build_lock": str(root / EXPERIMENTS_RELATIVE / "build.lock"),
    }


def _load_start(root: Path, address: str, session_id: str) -> tuple[Path, dict[str, object]]:
    if SESSION_RE.fullmatch(session_id) is None:
        raise ExperimentError("the experiment session ID is invalid")
    directory = _session_base(root, address) / "sessions" / session_id
    start = read_receipt(directory / "start.json", kind="experiment-session-start")
    if start.get("address") != address or start.get("session_id") != session_id:
        raise ExperimentError("the experiment start receipt names another session")
    return directory, start


def _context_compatible(baseline: Mapping[str, object], current: Mapping[str, object]) -> bool:
    return all(
        baseline.get(key) == current.get(key)
        for key in COMPILER_CONTEXT_KEYS
        if key in baseline or key in current
    )


def attach_baseline(
    address_value: str | int,
    session_id: str,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    root = _root(root)
    address = normalize_address(address_value)
    with _state_lock(root):
        directory, start = _load_start(root, address, session_id)
        session_path = directory / "session.json"
        if session_path.exists():
            raise ExperimentError("the experiment session is already active")
        baseline = directory / "baseline"
        report_descriptor, report_receipt = _report_descriptor(
            baseline / "report.json", root
        )
        data_descriptor, data_receipt = _report_descriptor(
            baseline / "data-report.json", root
        )
        report_identity = report_receipt.get("input_identity")
        if not isinstance(report_identity, dict) or report_identity != data_receipt.get(
            "input_identity"
        ):
            raise ExperimentError("the baseline reports have different input identities")
        if report_identity.get("head") != start.get("head"):
            raise ExperimentError("the baseline report HEAD changed after session start")
        function_map = start.get("function_map")
        files = report_identity.get("files")
        if (
            not isinstance(function_map, dict)
            or not isinstance(files, dict)
            or files.get("functions_map") != function_map.get("sha256")
        ):
            raise ExperimentError("the baseline report uses another function map")
        source_snapshot = _bound_source_snapshot(root, report_identity)
        context_path = baseline / "compiler-context.json"
        context = _read_json(context_path, MAX_CONTEXT_BYTES, "compiler context")
        if context.get("git_head") != start.get("head"):
            raise ExperimentError("the compiler context uses another HEAD")
        target = _address_int(address)
        if target not in read_match_statuses(baseline / "report.json"):
            raise ExperimentError("the experiment target is missing from the baseline report")
        document: dict[str, object] = {
            "kind": "experiment-session",
            "address": address,
            "session_id": session_id,
            "created_at": start.get("created_at"),
            "activated_at": _timestamp(),
            "head": start.get("head"),
            "campaign": start.get("campaign"),
            "brief": start.get("brief"),
            "doctor_receipt": start.get("doctor_receipt"),
            "limits": start.get("limits"),
            "function_map": start.get("function_map"),
            "tool_identity": start.get("tool_identity"),
            "compiler_context": context,
            "artifacts": {
                "baseline_report": report_descriptor,
                "baseline_data_report": data_descriptor,
                "compiler_context": _descriptor(
                    context_path,
                    root,
                    maximum=MAX_CONTEXT_BYTES,
                    description="compiler context",
                ),
                "source_patch": _descriptor(
                    baseline / "source.patch",
                    root,
                    maximum=MAX_PATCH_BYTES,
                    description="baseline source patch",
                    allow_empty=True,
                ),
            },
            "comparison_identity": report_identity,
            SOURCE_SNAPSHOT_FIELD: source_snapshot,
        }
        session = write_receipt(session_path, document)
        pointer = {
            "schema_version": SCHEMA_VERSION,
            "kind": "experiment-current-session",
            "address": address,
            "session_id": session_id,
            "session_receipt": _safe_relative(session_path, root),
            "session_receipt_sha256": file_hash(session_path),
            "session_receipt_id": session["receipt_id"],
            "updated_at": _timestamp(),
        }
        pointer["pointer_id"] = receipt_id(pointer)
        _atomic_write(
            _session_base(root, address) / "current-session.json",
            json.dumps(pointer, indent=2, sort_keys=True).encode("utf-8") + b"\n",
        )
    return session


def _read_pointer(root: Path, address: str) -> dict[str, object]:
    path = _session_base(root, address) / "current-session.json"
    value = _read_json(path, MAX_RECORD_BYTES, "current experiment pointer")
    saved_id = value.pop("pointer_id", None)
    expected = receipt_id(value)
    value["pointer_id"] = saved_id
    if value.get("schema_version") != SCHEMA_VERSION or saved_id != expected:
        raise ExperimentError("the current experiment pointer identity is invalid")
    if value.get("kind") != "experiment-current-session" or value.get("address") != address:
        raise ExperimentError("the current experiment pointer names another target")
    return value


def read_session(
    address_value: str | int,
    *,
    root: Path = ROOT,
    session_id: str | None = None,
    validate_artifacts: bool = True,
) -> tuple[Path, dict[str, object]]:
    root = _root(root)
    address = normalize_address(address_value)
    pointer = None
    if session_id is None:
        pointer = _read_pointer(root, address)
        session_id = str(pointer.get("session_id", ""))
    directory, _ = _load_start(root, address, session_id)
    session_path = directory / "session.json"
    session = read_receipt(session_path, kind="experiment-session")
    if session.get("address") != address or session.get("session_id") != session_id:
        raise ExperimentError("the experiment session receipt names another session")
    if pointer is not None and (
        pointer.get("session_receipt") != _safe_relative(session_path, root)
        or pointer.get("session_receipt_sha256") != file_hash(session_path)
        or pointer.get("session_receipt_id") != session.get("receipt_id")
    ):
        raise ExperimentError("the current experiment pointer is stale")
    if validate_artifacts:
        artifacts = session.get("artifacts")
        if not isinstance(artifacts, dict):
            raise ExperimentError("the experiment session has no baseline artifacts")
        report, receipt = _validate_report_descriptor(
            artifacts.get("baseline_report"), root, parent=directory
        )
        _, data_receipt = _validate_report_descriptor(
            artifacts.get("baseline_data_report"), root, parent=directory
        )
        if (
            receipt.get("input_identity") != session.get("comparison_identity")
            or data_receipt.get("input_identity") != session.get("comparison_identity")
        ):
            raise ExperimentError("the baseline report input identity changed")
        source_snapshot = session.get(SOURCE_SNAPSHOT_FIELD)
        comparison_identity = session.get("comparison_identity")
        if source_snapshot is not None and (
            not isinstance(source_snapshot, dict)
            or not isinstance(comparison_identity, dict)
            or _snapshot_hash(source_snapshot)
            != comparison_identity.get("source_worktree_sha256")
        ):
            raise ExperimentError("the baseline source snapshot identity changed")
        _validate_descriptor(
            artifacts.get("compiler_context"),
            root,
            maximum=MAX_CONTEXT_BYTES,
            description="compiler context",
            parent=directory,
        )
        _validate_descriptor(
            artifacts.get("source_patch"),
            root,
            maximum=MAX_PATCH_BYTES,
            description="baseline source patch",
            parent=directory,
            allow_empty=True,
        )
        if _address_int(address) not in read_match_statuses(report):
            raise ExperimentError("the experiment target is missing from the baseline report")
        for name, maximum in (("brief", MAX_BRIEF_BYTES), ("doctor_receipt", MAX_BRIEF_BYTES)):
            descriptor = session.get(name)
            if descriptor is not None:
                _validate_descriptor(
                    descriptor,
                    root,
                    maximum=maximum,
                    description=name.replace("_", " "),
                )
    return directory, session


def _session_is_current(root: Path, session: Mapping[str, object]) -> None:
    if _head(root) != session.get("head"):
        raise ExperimentError("the experiment HEAD changed after session start")
    function_map = session.get("function_map")
    if not isinstance(function_map, dict) or file_hash(root / FUNCTION_MAP_RELATIVE) != function_map.get(
        "sha256"
    ):
        raise ExperimentError("the experiment function map changed after session start")
    if _tool_identity(root) != session.get("tool_identity"):
        raise ExperimentError("the experiment tool identity changed after session start")
    for name, maximum in (("brief", MAX_BRIEF_BYTES), ("doctor_receipt", MAX_BRIEF_BYTES)):
        descriptor = session.get(name)
        if descriptor is not None:
            _validate_descriptor(
                descriptor,
                root,
                maximum=maximum,
                description=name.replace("_", " "),
            )
    bound = session.get("campaign")
    current = _campaign_context(root)
    bound_id = bound.get("campaign_id") if isinstance(bound, dict) else None
    current_id = current.get("campaign_id") if isinstance(current, dict) else None
    if bound_id != current_id:
        raise ExperimentError("the active campaign changed after session start")
    if isinstance(current, dict):
        deadline = _parse_time(current.get("deadline"))
        if deadline is not None and _now() >= deadline:
            raise ExperimentError("the active campaign deadline has passed")


def _trial_receipts(directory: Path) -> list[tuple[Path, dict[str, object], str]]:
    trials = directory / "trials"
    if not trials.exists():
        return []
    descriptor = _open_directory_nofollow(trials)
    try:
        with os.scandir(descriptor) as scan:
            entries = []
            for entry in scan:
                if len(entries) >= MAX_TRIAL_DIRECTORY_ENTRIES:
                    raise ExperimentError("the experiment session has too many trial entries")
                if not entry.is_dir(follow_symlinks=False):
                    raise ExperimentError("the experiment session has an invalid trial directory")
                entries.append(entry.name)
    finally:
        os.close(descriptor)
    result = []
    for name in sorted(entries):
        path = trials / name
        if TRIAL_RE.fullmatch(path.name) is None:
            raise ExperimentError("the experiment session has an invalid trial directory")
        pending = read_receipt(path / "pending.json", kind="experiment-trial-pending")
        reservation = dict(pending)
        terminal = "pending"
        terminal_path = None
        for name, kind in (
            ("completed.json", "experiment-trial"),
            ("failed.json", "experiment-trial-failure"),
        ):
            candidate = path / name
            if candidate.exists():
                if terminal_path is not None:
                    raise ExperimentError("the experiment trial has two terminal receipts")
                terminal_path = candidate
                terminal = "completed" if name == "completed.json" else "failed"
                receipt = read_receipt(candidate, kind=kind)
                if receipt.get("pending_receipt_id") != pending.get("receipt_id"):
                    raise ExperimentError("the trial terminal receipt names another reservation")
                for field in (
                    "address",
                    "session_id",
                    "session_receipt_id",
                    "trial_id",
                    "sequence",
                    "label",
                    "reserved_at",
                    "parent",
                    "question",
                    "route",
                    "model",
                    PARENT_SNAPSHOT_FIELD,
                ):
                    if receipt.get(field) != reservation.get(field):
                        raise ExperimentError(
                            "the trial terminal receipt changed its reservation"
                        )
                pending = receipt
        result.append((path, pending, terminal))
    return result


def _declared_parent_snapshot(
    session: Mapping[str, object],
    trials: Sequence[tuple[Path, Mapping[str, object], str]],
    parent: str,
) -> dict[str, str]:
    """Return the full source snapshot for one declared parent."""

    if parent == "baseline":
        value = session.get(SOURCE_SNAPSHOT_FIELD)
    else:
        matches = [
            _receipt_source_snapshot(receipt)
            for _, receipt, state in trials
            if state == "completed" and receipt.get("trial_id") == parent
        ]
        value = matches[0] if len(matches) == 1 else None
    if not isinstance(value, dict) or not all(
        isinstance(path, str) and isinstance(digest, str)
        for path, digest in value.items()
    ):
        raise ExperimentError("the declared parent has no bound source snapshot")
    return dict(value)


def _receipt_source_snapshot(receipt: Mapping[str, object]) -> dict[str, str]:
    """Validate and return one completed trial source snapshot."""

    value = receipt.get(SOURCE_SNAPSHOT_FIELD)
    identity = receipt.get("comparison_identity")
    if (
        not isinstance(value, dict)
        or not isinstance(identity, dict)
        or not all(
            isinstance(path, str) and isinstance(digest, str)
            for path, digest in value.items()
        )
        or _snapshot_hash(value) != identity.get("source_worktree_sha256")
    ):
        raise ExperimentError("the completed trial source snapshot is invalid")
    return dict(value)


def _validate_parent_snapshot_binding(
    directory: Path,
    session: Mapping[str, object],
    pending: Mapping[str, object],
) -> dict[str, str]:
    """Validate one prepared trial against its declared parent receipt."""

    parent = pending.get("parent")
    if not isinstance(parent, str):
        raise ExperimentError("the prepared trial has no declared parent")
    trials = [
        row
        for row in _trial_receipts(directory)
        if row[1].get("trial_id") != pending.get("trial_id")
    ]
    expected = _declared_parent_snapshot(session, trials, parent)
    saved = pending.get(PARENT_SNAPSHOT_FIELD)
    if not isinstance(saved, dict) or saved != expected:
        raise ExperimentError("the prepared trial parent snapshot is invalid")
    return expected


def reserve_trial(
    address_value: str | int,
    label: str,
    *,
    root: Path = ROOT,
    session_id: str | None = None,
    question: str | None = None,
    parent: str | None = None,
    route: str | None = None,
    model: str | None = None,
    bind_parent: bool = False,
) -> dict[str, object]:
    root = _root(root)
    address = normalize_address(address_value)
    label = validate_label(label)
    if route is not None and route not in ROUTE_ORDER:
        raise ExperimentError("--route is not in the mismatch taxonomy")
    for name, value in (("question", question), ("model", model)):
        if value is not None and (not value.strip() or len(value) > 1000):
            raise ExperimentError(f"--{name} must contain from 1 through 1000 characters")
    with _state_lock(root):
        directory, session = read_session(
            address, root=root, session_id=session_id, validate_artifacts=True
        )
        _session_is_current(root, session)
        trials = _trial_receipts(directory)
        if any(state == "pending" for _, _, state in trials):
            raise ExperimentError("complete or fail the pending trial before another trial")
        best = select_best(directory, session, trials, root=root)
        if best.get("status") in {"exact", "effective"}:
            raise ExperimentError("the experiment already has a terminal best candidate")
        limits = session.get("limits")
        non_improving_limit = (
            limits.get("max_non_improving") if isinstance(limits, dict) else None
        )
        if not isinstance(non_improving_limit, int):
            raise ExperimentError("the experiment non-improvement limit is invalid")
        baseline = _baseline_candidate(directory, session, root)
        consecutive_non_improving, _ = _trajectory(baseline, trials)
        if consecutive_non_improving >= non_improving_limit:
            raise ExperimentError("the experiment reached its non-improvement limit")
        labels = {str(receipt.get("label", "")).casefold() for _, receipt, _ in trials}
        if label.casefold() in labels:
            raise ExperimentError(f"the trial label already exists: {label}")
        maximum = limits.get("max_trials") if isinstance(limits, dict) else None
        if not isinstance(maximum, int) or len(trials) >= maximum:
            raise ExperimentError("the experiment trial budget is exhausted")
        if parent is None:
            parent_value = "baseline" if not trials else str(trials[-1][1].get("trial_id"))
        elif parent == "baseline":
            parent_value = parent
        else:
            matches = [
                receipt
                for _, receipt, state in trials
                if state == "completed"
                and parent in {receipt.get("label"), receipt.get("trial_id")}
            ]
            if len(matches) != 1:
                raise ExperimentError("--parent must name the baseline or one completed trial")
            parent_value = str(matches[0].get("trial_id"))
        parent_snapshot = None
        if bind_parent:
            parent_snapshot = _declared_parent_snapshot(
                session, trials, parent_value
            )
            if source_worktree_snapshot(root) != parent_snapshot:
                raise ExperimentError(
                    "the worktree source does not equal the declared parent"
                )
        sequence = len(trials) + 1
        trial_id = f"{sequence:03d}-{label}"
        trial_directory = directory / "trials" / trial_id
        trial_directory.mkdir(parents=True, exist_ok=False)
        pending = write_receipt(
            trial_directory / "pending.json",
            {
                "kind": "experiment-trial-pending",
                "address": address,
                "session_id": session.get("session_id"),
                "session_receipt_id": session.get("receipt_id"),
                "trial_id": trial_id,
                "sequence": sequence,
                "label": label,
                "reserved_at": _timestamp(),
                "parent": parent_value,
                "question": question,
                "route": route,
                "model": model,
                PARENT_SNAPSHOT_FIELD: parent_snapshot,
            },
        )
    return {
        "address": address,
        "session_id": session.get("session_id"),
        "trial_id": trial_id,
        "sequence": sequence,
        "trial_directory": str(trial_directory),
        "report": str(trial_directory / "report.json"),
        "diff": str(trial_directory / "diff.txt"),
        "source_patch": str(trial_directory / "source.patch"),
        "compiler_context": str(trial_directory / "compiler-context.json"),
        "build_lock": str(root / EXPERIMENTS_RELATIVE / "build.lock"),
        "pending_receipt_id": pending["receipt_id"],
    }


def measure_plan(
    address_value: str | int,
    trial_id: str,
    *,
    root: Path = ROOT,
    session_id: str | None = None,
) -> dict[str, object]:
    """Return paths for a prepared replay-eligible trial."""

    root = _root(root)
    address = normalize_address(address_value)
    with _state_lock(root):
        directory, session, trial_directory, pending = _pending_trial(
            root, address, session_id, trial_id
        )
        _session_is_current(root, session)
        _validate_parent_snapshot_binding(directory, session, pending)
    return {
        "address": address,
        "session_id": session.get("session_id"),
        "trial_id": trial_id,
        "sequence": pending.get("sequence"),
        "trial_directory": str(trial_directory),
        "report": str(trial_directory / "report.json"),
        "diff": str(trial_directory / "diff.txt"),
        "source_patch": str(trial_directory / "source.patch"),
        "compiler_context": str(trial_directory / "compiler-context.json"),
        "build_lock": str(root / EXPERIMENTS_RELATIVE / "build.lock"),
        "pending_receipt_id": pending.get("receipt_id"),
    }


def _pending_trial(
    root: Path, address: str, session_id: str | None, trial_id: str
) -> tuple[Path, dict[str, object], Path, dict[str, object]]:
    if TRIAL_RE.fullmatch(trial_id) is None:
        raise ExperimentError("the experiment trial ID is invalid")
    directory, session = read_session(address, root=root, session_id=session_id)
    trial_directory = directory / "trials" / trial_id
    pending = read_receipt(
        trial_directory / "pending.json", kind="experiment-trial-pending"
    )
    if (
        pending.get("address") != address
        or pending.get("session_id") != session.get("session_id")
        or pending.get("session_receipt_id") != session.get("receipt_id")
        or pending.get("trial_id") != trial_id
    ):
        raise ExperimentError("the pending receipt names another trial")
    if (trial_directory / "completed.json").exists() or (trial_directory / "failed.json").exists():
        raise ExperimentError("the experiment trial already has a terminal receipt")
    return directory, session, trial_directory, pending


def fail_trial(
    address_value: str | int,
    trial_id: str,
    *,
    stage: str,
    root: Path = ROOT,
    session_id: str | None = None,
    exit_code: int | None = None,
    message: str | None = None,
) -> dict[str, object]:
    root = _root(root)
    address = normalize_address(address_value)
    if stage not in FAILURE_STAGES:
        raise ExperimentError("the experiment failure stage is invalid")
    if message is not None and len(message) > 2000:
        raise ExperimentError("the experiment failure message is too long")
    with _state_lock(root):
        _, session, trial_directory, pending = _pending_trial(
            root, address, session_id, trial_id
        )
        document = write_receipt(
            trial_directory / "failed.json",
            {
                "kind": "experiment-trial-failure",
                "address": address,
                "session_id": session.get("session_id"),
                "session_receipt_id": session.get("receipt_id"),
                "pending_receipt_id": pending.get("receipt_id"),
                "trial_id": trial_id,
                "sequence": pending.get("sequence"),
                "label": pending.get("label"),
                "reserved_at": pending.get("reserved_at"),
                "completed_at": _timestamp(),
                "parent": pending.get("parent"),
                "question": pending.get("question"),
                "route": pending.get("route"),
                "model": pending.get("model"),
                PARENT_SNAPSHOT_FIELD: pending.get(PARENT_SNAPSHOT_FIELD),
                "result": "failed",
                "failure_stage": stage,
                "exit_code": exit_code,
                "message": message,
                "eligible": False,
                "first_failing_constraint": f"{stage}-failure",
            },
        )
    return document


def _status_name(
    status: MatchStatus,
    address: int,
    context: Mapping[str, object],
) -> str:
    debt = context.get("source_debt", {})
    has_debt = isinstance(debt, dict) and bool(debt.get(str(address), debt.get(f"0x{address:08X}")))
    if status.exact and not has_debt:
        return "exact"
    if status.effective and not has_debt:
        return "effective"
    return "provisional"


def _effective_total(statuses: Mapping[int, MatchStatus]) -> float:
    return sum(1.0 if status.exact or status.effective else status.matching for status in statuses.values())


def _source_integrity_problems(
    baseline: Mapping[str, object], current: Mapping[str, object], target: int
) -> list[dict[str, object]]:
    problems: list[dict[str, object]] = []
    baseline_implemented = set(baseline.get("implemented_addresses", []))
    current_implemented = set(current.get("implemented_addresses", []))
    baseline_stubs = set(baseline.get("stub_addresses", []))
    current_stubs = set(current.get("stub_addresses", []))
    if target in baseline_implemented and target not in current_implemented:
        problems.append({"kind": "target-source-downgrade", "address": f"0x{target:08X}"})
    if target in baseline_stubs and target not in current_stubs | current_implemented:
        problems.append({"kind": "target-source-missing", "address": f"0x{target:08X}"})
    baseline_debt = baseline.get("source_debt", {})
    current_debt = current.get("source_debt", {})
    if isinstance(baseline_debt, dict) and isinstance(current_debt, dict):
        for address, rules in current_debt.items():
            before = set(baseline_debt.get(address, []))
            after = set(rules) if isinstance(rules, list) else set()
            for rule in sorted(after - before):
                problems.append({"kind": "new-source-debt", "address": address, "rule": rule})
    return problems


def _gate_result(
    baseline_statuses: Mapping[int, MatchStatus],
    current_statuses: Mapping[int, MatchStatus],
    target: int,
    baseline_context: Mapping[str, object],
    current_context: Mapping[str, object],
) -> dict[str, object]:
    baseline_target = baseline_statuses.get(target)
    current_target = current_statuses.get(target)
    constraints: list[dict[str, object]] = []
    source_problems = _source_integrity_problems(baseline_context, current_context, target)
    if source_problems:
        constraints.append({"name": "source-integrity", "details": source_problems})
    if current_target is None:
        constraints.append({"name": "missing-target", "address": f"0x{target:08X}"})
    if baseline_target is not None and current_target is not None:
        before_status = _status_name(baseline_target, target, baseline_context)
        after_status = _status_name(current_target, target, current_context)
        if STATUS_RANK[after_status] < STATUS_RANK[before_status]:
            constraints.append(
                {"name": "target-status-downgrade", "before": before_status, "after": after_status}
            )
        if baseline_target.matching >= 0.5 and current_target.matching + SCORE_EPSILON < 0.5:
            constraints.append(
                {
                    "name": "target-below-threshold",
                    "before": baseline_target.matching,
                    "after": current_target.matching,
                }
            )
        elif current_target.matching + SCORE_EPSILON < baseline_target.matching:
            constraints.append(
                {
                    "name": "target-score-regression",
                    "before": baseline_target.matching,
                    "after": current_target.matching,
                }
            )
    unrelated: list[dict[str, object]] = []
    for address, before in sorted(baseline_statuses.items()):
        if address == target:
            continue
        after = current_statuses.get(address)
        if after is None:
            unrelated.append({"address": f"0x{address:08X}", "kind": "missing"})
            continue
        before_name = _status_name(before, address, baseline_context)
        after_name = _status_name(after, address, current_context)
        if STATUS_RANK[before_name] >= STATUS_RANK["effective"] and STATUS_RANK[after_name] < STATUS_RANK[before_name]:
            unrelated.append(
                {
                    "address": f"0x{address:08X}",
                    "kind": "terminal-regression",
                    "before": before_name,
                    "after": after_name,
                }
            )
        elif after.matching + SCORE_EPSILON < before.matching:
            unrelated.append(
                {
                    "address": f"0x{address:08X}",
                    "kind": "score-regression",
                    "before": before.matching,
                    "after": after.matching,
                }
            )
    if unrelated:
        constraints.append({"name": "unrelated-regression", "details": unrelated})
    return {
        "passed": not constraints,
        "constraints": constraints,
        "unrelated_regressions": unrelated,
        "first_failing_constraint": constraints[0]["name"] if constraints else None,
    }


def _patch_lines(path: Path) -> int:
    count = 0
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(("+++", "---")):
            continue
        if line.startswith(("+", "-")):
            count += 1
    return count


def _bounded_normalized(record: Mapping[str, object]) -> dict[str, object]:
    rows = record.get("normalized_diff", [])
    if not isinstance(rows, list):
        rows = []
    bounded = []
    for row in rows[:MAX_NORMALIZED_ROWS]:
        if not isinstance(row, dict):
            continue
        bounded.append(
            {
                "original": str(row.get("original", ""))[:MAX_NORMALIZED_TEXT],
                "recompiled": str(row.get("recompiled", ""))[:MAX_NORMALIZED_TEXT],
            }
        )
    return {
        "schema_version": SCHEMA_VERSION,
        "mismatch_taxonomy": record.get("mismatch_taxonomy"),
        "structural_changes": record.get("structural_changes"),
        "evidence": bounded,
        "evidence_rows": len(rows),
        "evidence_truncated": len(rows) > len(bounded),
    }


def record_trial(
    address_value: str | int,
    trial_id: str,
    *,
    root: Path = ROOT,
    session_id: str | None = None,
) -> dict[str, object]:
    root = _root(root)
    address = normalize_address(address_value)
    target = _address_int(address)
    with _state_lock(root):
        directory, session, trial_directory, pending = _pending_trial(
            root, address, session_id, trial_id
        )
        artifacts = session.get("artifacts")
        if not isinstance(artifacts, dict):
            raise ExperimentError("the experiment session has no baseline artifacts")
        baseline_report, _ = _validate_report_descriptor(
            artifacts.get("baseline_report"), root, parent=directory
        )
        report_descriptor, report_receipt = _report_descriptor(
            trial_directory / "report.json", root
        )
        report_identity = report_receipt.get("input_identity")
        if not isinstance(report_identity, dict):
            raise ExperimentError("the trial report has no input identity")
        if report_identity.get("head") != session.get("head"):
            raise ExperimentError("the trial report uses another HEAD")
        function_map = session.get("function_map")
        files = report_identity.get("files")
        if (
            not isinstance(function_map, dict)
            or not isinstance(files, dict)
            or files.get("functions_map") != function_map.get("sha256")
        ):
            raise ExperimentError("the trial report uses another function map")
        if report_identity.get("validation_tools") != session.get("comparison_identity", {}).get(
            "validation_tools"
        ):
            raise ExperimentError("the trial report uses another comparison tool identity")
        parent_snapshot = pending.get(PARENT_SNAPSHOT_FIELD)
        if parent_snapshot is not None:
            _validate_parent_snapshot_binding(directory, session, pending)
        source_snapshot = _bound_source_snapshot(root, report_identity)
        diff_path = trial_directory / "diff.txt"
        _read_bytes(diff_path, MAX_DIFF_BYTES, "verbose diff")
        try:
            diff_receipt = validate_diff(
                diff_path,
                target,
                root=root,
                current=report_identity,
                report_receipt=report_receipt,
            )
        except ValueError as error:
            raise ExperimentError(f"the verbose diff provenance is invalid: {error}") from error
        diff_descriptor = _descriptor(
            diff_path, root, maximum=MAX_DIFF_BYTES, description="verbose diff"
        )
        diff_descriptor["provenance"] = _descriptor(
            provenance_path(diff_path),
            root,
            maximum=MAX_CONTEXT_BYTES,
            description="verbose diff provenance",
        )
        diff_descriptor["provenance_receipt_id"] = diff_receipt.get("receipt_id")
        context_path = trial_directory / "compiler-context.json"
        current_context = _read_json(
            context_path, MAX_CONTEXT_BYTES, "trial compiler context"
        )
        baseline_context = session.get("compiler_context")
        if not isinstance(baseline_context, dict) or not _context_compatible(
            baseline_context, current_context
        ):
            raise ExperimentError("the trial compiler context changed")
        patch_path = trial_directory / "source.patch"
        patch_descriptor = _descriptor(
            patch_path,
            root,
            maximum=MAX_PATCH_BYTES,
            description="trial source patch",
            allow_empty=True,
        )
        baseline_statuses = read_match_statuses(baseline_report)
        current_statuses = read_match_statuses(trial_directory / "report.json")
        current_target = current_statuses.get(target)
        for status in current_statuses.values():
            if not math.isfinite(status.matching) or not 0.0 <= status.matching <= 1.0:
                raise ExperimentError("the comparison report has an invalid score")
        normalized_source = experiment_record(trial_directory / "report.json", target)
        normalized = _bounded_normalized(normalized_source)
        normalized_path = trial_directory / "normalized.json"
        _atomic_write(
            normalized_path,
            json.dumps(normalized, indent=2, sort_keys=True).encode("utf-8") + b"\n",
            exclusive=True,
        )
        gate = _gate_result(
            baseline_statuses,
            current_statuses,
            target,
            baseline_context,
            current_context,
        )
        status_name = (
            _status_name(current_target, target, current_context)
            if current_target is not None
            else "missing"
        )
        document = write_receipt(
            trial_directory / "completed.json",
            {
                "kind": "experiment-trial",
                "address": address,
                "session_id": session.get("session_id"),
                "session_receipt_id": session.get("receipt_id"),
                "pending_receipt_id": pending.get("receipt_id"),
                "trial_id": trial_id,
                "sequence": pending.get("sequence"),
                "label": pending.get("label"),
                "reserved_at": pending.get("reserved_at"),
                "completed_at": _timestamp(),
                "parent": pending.get("parent"),
                "question": pending.get("question"),
                "route": pending.get("route"),
                "model": pending.get("model"),
                PARENT_SNAPSHOT_FIELD: parent_snapshot,
                SOURCE_SNAPSHOT_FIELD: source_snapshot,
                "result": "comparison",
                "artifacts": {
                    "report": report_descriptor,
                    "diff": diff_descriptor,
                    "source_patch": patch_descriptor,
                    "compiler_context": _descriptor(
                        context_path,
                        root,
                        maximum=MAX_CONTEXT_BYTES,
                        description="trial compiler context",
                    ),
                    "normalized": _descriptor(
                        normalized_path,
                        root,
                        maximum=MAX_CONTEXT_BYTES,
                        description="normalized mismatch record",
                    ),
                },
                "comparison_identity": report_identity,
                "normalized_taxonomy": normalized.get("mismatch_taxonomy"),
                "target": {
                    "status": status_name,
                    "score": current_target.matching if current_target is not None else None,
                },
                "repository_effective_score": _effective_total(current_statuses),
                "changed_patch_lines": _patch_lines(patch_path),
                "unrelated_regressions": gate["unrelated_regressions"],
                "source_integrity_failure": any(
                    item.get("name") == "source-integrity"
                    for item in gate["constraints"]
                ),
                "gate_constraints": gate["constraints"],
                "first_failing_constraint": gate["first_failing_constraint"],
                "eligible": gate["passed"],
            },
        )
    return document


def _baseline_candidate(
    directory: Path, session: Mapping[str, object], root: Path
) -> dict[str, object]:
    artifacts = session.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ExperimentError("the experiment session has no baseline artifacts")
    report, _ = _validate_report_descriptor(
        artifacts.get("baseline_report"), root, parent=directory
    )
    patch = _validate_descriptor(
        artifacts.get("source_patch"),
        root,
        maximum=MAX_PATCH_BYTES,
        description="baseline source patch",
        parent=directory,
        allow_empty=True,
    )
    statuses = read_match_statuses(report)
    target = _address_int(str(session["address"]))
    status = statuses.get(target)
    if status is None:
        raise ExperimentError("the experiment target is missing from the baseline report")
    context = session.get("compiler_context")
    if not isinstance(context, dict):
        raise ExperimentError("the experiment session has no compiler context")
    return {
        "candidate": "baseline",
        "trial_id": None,
        "sequence": 0,
        "label": "baseline",
        "status": _status_name(status, target, context),
        "score": status.matching,
        "repository_effective_score": _effective_total(statuses),
        "changed_patch_lines": _patch_lines(patch),
        "eligible": True,
    }


def _validate_completed_artifacts(
    receipt: Mapping[str, object], directory: Path, root: Path
) -> None:
    trial_id = str(receipt.get("trial_id", ""))
    trial_directory = directory / "trials" / trial_id
    artifacts = receipt.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ExperimentError("the completed trial has no artifacts")
    report, report_receipt = _validate_report_descriptor(
        artifacts.get("report"), root, parent=trial_directory
    )
    if report_receipt.get("input_identity") != receipt.get("comparison_identity"):
        raise ExperimentError("the completed trial report identity changed")
    source_snapshot = receipt.get(SOURCE_SNAPSHOT_FIELD)
    comparison_identity = receipt.get("comparison_identity")
    if source_snapshot is not None and (
        not isinstance(source_snapshot, dict)
        or not isinstance(comparison_identity, dict)
        or _snapshot_hash(source_snapshot)
        != comparison_identity.get("source_worktree_sha256")
    ):
        raise ExperimentError("the completed trial source snapshot identity changed")
    diff_descriptor = artifacts.get("diff")
    if not isinstance(diff_descriptor, dict):
        raise ExperimentError("the completed trial has no verbose diff")
    diff_core = {key: diff_descriptor.get(key) for key in ("path", "sha256", "bytes")}
    diff = _validate_descriptor(
        diff_core,
        root,
        maximum=MAX_DIFF_BYTES,
        description="verbose diff",
        parent=trial_directory,
    )
    _validate_descriptor(
        diff_descriptor.get("provenance"),
        root,
        maximum=MAX_CONTEXT_BYTES,
        description="verbose diff provenance",
        parent=trial_directory,
    )
    try:
        diff_receipt = validate_diff(
            diff,
            _address_int(str(receipt["address"])),
            root=root,
            current=receipt.get("comparison_identity"),
            report_receipt=report_receipt,
        )
    except ValueError as error:
        raise ExperimentError(f"the verbose diff provenance is invalid: {error}") from error
    if diff_receipt.get("receipt_id") != diff_descriptor.get("provenance_receipt_id"):
        raise ExperimentError("the verbose diff provenance receipt changed")
    for name, maximum in (
        ("source_patch", MAX_PATCH_BYTES),
        ("compiler_context", MAX_CONTEXT_BYTES),
        ("normalized", MAX_CONTEXT_BYTES),
    ):
        _validate_descriptor(
            artifacts.get(name),
            root,
            maximum=maximum,
            description=name.replace("_", " "),
            parent=trial_directory,
            allow_empty=name == "source_patch",
        )
    if _address_int(str(receipt["address"])) not in read_match_statuses(report):
        raise ExperimentError("the completed trial report names another target")


def _trajectory_receipt_descriptor(
    path: Path,
    root: Path,
    receipt: Mapping[str, object],
) -> dict[str, object]:
    """Bind one immutable experiment receipt and its exact bytes."""

    descriptor = _descriptor(
        path,
        root,
        maximum=MAX_RECORD_BYTES,
        description="experiment trajectory receipt",
    )
    saved_id = receipt.get("receipt_id")
    if not isinstance(saved_id, str) or HASH_RE.fullmatch(saved_id) is None:
        raise ExperimentError("the experiment trajectory receipt ID is invalid")
    descriptor["receipt_id"] = saved_id
    return descriptor


def _trajectory_identity_unlocked(
    address_value: str | int,
    session_id: str,
    *,
    root: Path,
) -> dict[str, object]:
    """Build the exact identity for one prepared experiment trajectory."""

    root = _root(root)
    address = normalize_address(address_value)
    directory, session = read_session(
        address,
        root=root,
        session_id=session_id,
        validate_artifacts=True,
    )
    limits = session.get("limits")
    if (
        not isinstance(limits, dict)
        or type(limits.get("max_trials")) is not int
        or limits.get("max_trials") != DEFAULT_MAX_TRIALS
        or type(limits.get("max_non_improving")) is not int
        or limits.get("max_non_improving") != DEFAULT_MAX_NON_IMPROVING
    ):
        raise ExperimentError("the replay experiment limits are invalid")
    baseline_snapshot = session.get(SOURCE_SNAPSHOT_FIELD)
    comparison_identity = session.get("comparison_identity")
    artifacts = session.get("artifacts")
    function_map = session.get("function_map")
    baseline_files = (
        comparison_identity.get("files")
        if isinstance(comparison_identity, dict)
        else None
    )
    if (
        not isinstance(baseline_snapshot, dict)
        or not isinstance(comparison_identity, dict)
        or not isinstance(artifacts, dict)
        or not isinstance(function_map, dict)
        or not isinstance(baseline_files, dict)
        or comparison_identity.get("head") != session.get("head")
        or baseline_files.get("functions_map") != function_map.get("sha256")
        or _snapshot_hash(baseline_snapshot)
        != comparison_identity.get("source_worktree_sha256")
    ):
        raise ExperimentError("the replay experiment baseline is not snapshot-bound")

    trials = _trial_receipts(directory)
    if not 2 <= len(trials) <= DEFAULT_MAX_TRIALS:
        raise ExperimentError("a replay experiment needs two or three completed trials")
    if any(state != "completed" for _, _, state in trials):
        raise ExperimentError("a replay experiment has an incomplete trial")

    known_snapshots: dict[str, dict[str, str]] = {
        "baseline": dict(baseline_snapshot)
    }
    rows: list[dict[str, object]] = []
    for expected_sequence, (trial_directory, receipt, state) in enumerate(
        trials, start=1
    ):
        del state
        sequence = receipt.get("sequence")
        trial_id = receipt.get("trial_id")
        parent = receipt.get("parent")
        route = receipt.get("route")
        if (
            type(sequence) is not int
            or sequence != expected_sequence
            or not isinstance(trial_id, str)
            or TRIAL_RE.fullmatch(trial_id) is None
            or not isinstance(parent, str)
            or parent not in known_snapshots
            or not isinstance(route, str)
            or route not in ROUTE_ORDER
        ):
            raise ExperimentError("the replay experiment trial graph is invalid")
        parent_snapshot = receipt.get(PARENT_SNAPSHOT_FIELD)
        if parent_snapshot != known_snapshots[parent]:
            raise ExperimentError("the replay experiment parent snapshot is invalid")
        _validate_completed_artifacts(receipt, directory, root)
        result_snapshot = _receipt_source_snapshot(receipt)
        known_snapshots[trial_id] = result_snapshot
        trial_artifacts = receipt.get("artifacts")
        trial_identity = receipt.get("comparison_identity")
        if not isinstance(trial_artifacts, dict) or not isinstance(
            trial_identity, dict
        ):
            raise ExperimentError("the replay experiment trial identity is invalid")
        trial_files = trial_identity.get("files")
        if (
            not isinstance(trial_files, dict)
            or trial_identity.get("head") != session.get("head")
            or trial_files.get("functions_map") != function_map.get("sha256")
            or trial_identity.get("validation_tools")
            != comparison_identity.get("validation_tools")
        ):
            raise ExperimentError("the replay experiment trial input identity changed")
        pending = read_receipt(
            trial_directory / "pending.json",
            kind="experiment-trial-pending",
        )
        terminal = read_receipt(
            trial_directory / "completed.json",
            kind="experiment-trial",
        )
        if terminal != receipt:
            raise ExperimentError("the replay experiment terminal receipt changed")
        rows.append(
            {
                "sequence": sequence,
                "trial_id": trial_id,
                "parent": parent,
                "route": route,
                "pending_receipt": _trajectory_receipt_descriptor(
                    trial_directory / "pending.json", root, pending
                ),
                "terminal_receipt": _trajectory_receipt_descriptor(
                    trial_directory / "completed.json", root, terminal
                ),
                "artifact_identity_sha256": _snapshot_hash(trial_artifacts),
                "comparison_identity_sha256": _snapshot_hash(trial_identity),
                PARENT_SNAPSHOT_FIELD: dict(parent_snapshot),
                SOURCE_SNAPSHOT_FIELD: result_snapshot,
            }
        )

    session_path = directory / "session.json"
    identity: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": "experiment-trajectory-identity",
        "address": address,
        "session_id": session_id,
        "head": session.get("head"),
        "campaign": session.get("campaign"),
        "brief": session.get("brief"),
        "doctor_receipt": session.get("doctor_receipt"),
        "limits": dict(limits),
        "session_receipt": _trajectory_receipt_descriptor(
            session_path, root, session
        ),
        "baseline": {
            "artifact_identity_sha256": _snapshot_hash(artifacts),
            "comparison_identity_sha256": _snapshot_hash(comparison_identity),
            SOURCE_SNAPSHOT_FIELD: dict(baseline_snapshot),
        },
        "trials": rows,
    }
    identity["trajectory_sha256"] = _snapshot_hash(identity)
    return identity


def trajectory_identity(
    address_value: str | int,
    session_id: str,
    *,
    root: Path = ROOT,
) -> dict[str, object]:
    """Return a stable, artifact-validated experiment trajectory identity."""

    with _state_lock(_root(root)):
        return _trajectory_identity_unlocked(
            address_value,
            session_id,
            root=_root(root),
        )


def _trajectory_seal_hash(document: Mapping[str, object]) -> str:
    payload = dict(document)
    payload.pop("content_sha256", None)
    return _snapshot_hash(payload)


def _validate_trajectory_descriptor(value: object) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != {
        "path",
        "sha256",
        "bytes",
        "receipt_id",
    }:
        raise ExperimentError("an experiment trajectory receipt descriptor is invalid")
    path = value.get("path")
    byte_count = value.get("bytes")
    if (
        not isinstance(path, str)
        or not path
        or Path(path).is_absolute()
        or ".." in Path(path).parts
        or not isinstance(value.get("sha256"), str)
        or HASH_RE.fullmatch(str(value.get("sha256"))) is None
        or type(byte_count) is not int
        or not 0 < byte_count <= MAX_RECORD_BYTES
        or not isinstance(value.get("receipt_id"), str)
        or HASH_RE.fullmatch(str(value.get("receipt_id"))) is None
    ):
        raise ExperimentError("an experiment trajectory receipt descriptor is invalid")
    return value


def _validate_trajectory_snapshot(value: object) -> dict[str, str]:
    if not isinstance(value, dict):
        raise ExperimentError("an experiment trajectory source snapshot is invalid")
    for path, saved_hash in value.items():
        if (
            not isinstance(path, str)
            or not path
            or Path(path).is_absolute()
            or ".." in Path(path).parts
            or not isinstance(saved_hash, str)
            or (saved_hash != "missing" and HASH_RE.fullmatch(saved_hash) is None)
        ):
            raise ExperimentError("an experiment trajectory source snapshot is invalid")
    return value


def _validate_optional_artifact_descriptor(value: object) -> None:
    if value is None:
        return
    if not isinstance(value, dict) or set(value) != {"path", "sha256", "bytes"}:
        raise ExperimentError("an experiment trajectory artifact descriptor is invalid")
    path = value.get("path")
    byte_count = value.get("bytes")
    if (
        not isinstance(path, str)
        or not path
        or Path(path).is_absolute()
        or ".." in Path(path).parts
        or not isinstance(value.get("sha256"), str)
        or HASH_RE.fullmatch(str(value.get("sha256"))) is None
        or type(byte_count) is not int
        or not 0 < byte_count <= MAX_BRIEF_BYTES
    ):
        raise ExperimentError("an experiment trajectory artifact descriptor is invalid")


def _validate_trajectory_identity(identity: object) -> dict[str, object]:
    if not isinstance(identity, dict) or set(identity) != {
        "schema_version",
        "kind",
        "address",
        "session_id",
        "head",
        "campaign",
        "brief",
        "doctor_receipt",
        "limits",
        "session_receipt",
        "baseline",
        "trials",
        "trajectory_sha256",
    }:
        raise ExperimentError("the experiment trajectory identity is invalid")
    saved_hash = identity.get("trajectory_sha256")
    payload = dict(identity)
    payload.pop("trajectory_sha256", None)
    if (
        type(identity.get("schema_version")) is not int
        or identity.get("schema_version") != SCHEMA_VERSION
        or identity.get("kind") != "experiment-trajectory-identity"
        or not isinstance(saved_hash, str)
        or HASH_RE.fullmatch(saved_hash) is None
        or saved_hash != _snapshot_hash(payload)
    ):
        raise ExperimentError("the experiment trajectory identity is invalid")
    try:
        address = normalize_address(identity.get("address", ""))
    except ExperimentError as error:
        raise ExperimentError("the experiment trajectory identity is invalid") from error
    campaign = identity.get("campaign")
    limits = identity.get("limits")
    baseline = identity.get("baseline")
    trials = identity.get("trials")
    if (
        address != identity.get("address")
        or not isinstance(identity.get("session_id"), str)
        or SESSION_RE.fullmatch(str(identity.get("session_id"))) is None
        or not isinstance(identity.get("head"), str)
        or not re.fullmatch(r"[0-9a-f]{40,64}", str(identity.get("head")))
        or not isinstance(campaign, dict)
        or set(campaign) != {
            "campaign_id",
            "mode",
            "lane",
            "active_addresses",
            "campaign_head",
            "deadline",
        }
        or not isinstance(campaign.get("campaign_id"), str)
        or campaign.get("mode") not in {"coverage", "refinement"}
        or campaign.get("lane") not in {"closure", "production", "research"}
        or (campaign.get("lane"), campaign.get("mode"))
        not in {
            ("closure", "refinement"),
            ("production", "refinement"),
            ("research", "coverage"),
            ("research", "refinement"),
        }
        or not isinstance(campaign.get("active_addresses"), list)
        or any(
            not isinstance(item, str) or normalize_address(item) != item
            for item in campaign.get("active_addresses", [])
        )
        or campaign.get("campaign_head") != identity.get("head")
        or (
            campaign.get("deadline") is not None
            and not isinstance(campaign.get("deadline"), str)
        )
        or not isinstance(limits, dict)
        or set(limits) != {"max_trials", "max_non_improving"}
        or type(limits.get("max_trials")) is not int
        or limits.get("max_trials") != DEFAULT_MAX_TRIALS
        or type(limits.get("max_non_improving")) is not int
        or limits.get("max_non_improving") != DEFAULT_MAX_NON_IMPROVING
        or not isinstance(baseline, dict)
        or set(baseline) != {
            "artifact_identity_sha256",
            "comparison_identity_sha256",
            SOURCE_SNAPSHOT_FIELD,
        }
        or not isinstance(trials, list)
        or not 2 <= len(trials) <= DEFAULT_MAX_TRIALS
    ):
        raise ExperimentError("the experiment trajectory identity is invalid")
    _validate_optional_artifact_descriptor(identity.get("brief"))
    _validate_optional_artifact_descriptor(identity.get("doctor_receipt"))
    _validate_trajectory_descriptor(identity.get("session_receipt"))
    for key in ("artifact_identity_sha256", "comparison_identity_sha256"):
        if not isinstance(baseline.get(key), str) or HASH_RE.fullmatch(
            str(baseline.get(key))
        ) is None:
            raise ExperimentError("the experiment trajectory baseline is invalid")
    known_parents = {"baseline"}
    _validate_trajectory_snapshot(baseline.get(SOURCE_SNAPSHOT_FIELD))
    for expected_sequence, row in enumerate(trials, start=1):
        if not isinstance(row, dict) or set(row) != {
            "sequence",
            "trial_id",
            "parent",
            "route",
            "pending_receipt",
            "terminal_receipt",
            "artifact_identity_sha256",
            "comparison_identity_sha256",
            PARENT_SNAPSHOT_FIELD,
            SOURCE_SNAPSHOT_FIELD,
        }:
            raise ExperimentError("an experiment trajectory trial is invalid")
        trial_id = row.get("trial_id")
        if (
            type(row.get("sequence")) is not int
            or row.get("sequence") != expected_sequence
            or not isinstance(trial_id, str)
            or TRIAL_RE.fullmatch(trial_id) is None
            or row.get("parent") not in known_parents
            or row.get("route") not in ROUTE_ORDER
            or any(
                not isinstance(row.get(key), str)
                or HASH_RE.fullmatch(str(row.get(key))) is None
                for key in (
                    "artifact_identity_sha256",
                    "comparison_identity_sha256",
                )
            )
        ):
            raise ExperimentError("an experiment trajectory trial is invalid")
        _validate_trajectory_descriptor(row.get("pending_receipt"))
        _validate_trajectory_descriptor(row.get("terminal_receipt"))
        _validate_trajectory_snapshot(row.get(PARENT_SNAPSHOT_FIELD))
        _validate_trajectory_snapshot(row.get(SOURCE_SNAPSHOT_FIELD))
        known_parents.add(trial_id)
    return identity


def _validate_trajectory_seal_document(document: object) -> dict[str, object]:
    """Validate one strict content-addressed trajectory seal document."""

    if not isinstance(document, dict) or set(document) != {
        "schema_version",
        "kind",
        "trajectory",
        "content_sha256",
    }:
        raise ExperimentError("the experiment trajectory seal is invalid")
    saved_hash = document.get("content_sha256")
    if (
        type(document.get("schema_version")) is not int
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("kind") != "experiment-trajectory-seal"
        or not isinstance(saved_hash, str)
        or HASH_RE.fullmatch(saved_hash) is None
        or saved_hash != _trajectory_seal_hash(document)
    ):
        raise ExperimentError("the experiment trajectory seal is invalid")
    _validate_trajectory_identity(document.get("trajectory"))
    return document


def validate_trajectory_binding(
    binding: object,
    *,
    root: Path = ROOT,
    assume_locked: bool = False,
) -> dict[str, object]:
    """Validate an active campaign's exact trajectory seal binding."""

    root = _root(root)
    if not isinstance(binding, dict):
        raise ExperimentError("the campaign replay experiment binding is invalid")
    if (
        type(binding.get("schema_version")) is not int
        or binding.get("schema_version") != SCHEMA_VERSION
        or binding.get("kind") != "campaign-replay-experiment-commitment"
        or set(binding)
        != {
            "schema_version",
            "kind",
            "content_sha256",
            "sha256",
            "bytes",
        }
    ):
        raise ExperimentError("the campaign replay experiment binding is invalid")
    content_sha256 = binding.get("content_sha256")
    saved_sha256 = binding.get("sha256")
    saved_bytes = binding.get("bytes")
    if (
        not isinstance(content_sha256, str)
        or HASH_RE.fullmatch(content_sha256) is None
        or not isinstance(saved_sha256, str)
        or HASH_RE.fullmatch(saved_sha256) is None
        or type(saved_bytes) is not int
        or not 0 < saved_bytes <= MAX_TRAJECTORY_BYTES
    ):
        raise ExperimentError("the campaign replay experiment descriptor is invalid")
    from tools.decomp_replay import read_private_trajectory

    try:
        seal = read_private_trajectory(
            binding,
            root=root,
            assume_locked=assume_locked,
        )
    except ValueError as error:
        raise ExperimentError("the campaign replay experiment seal changed") from error
    _validate_trajectory_seal_document(seal)
    return binding


def trajectory_from_binding(
    binding: object,
    *,
    root: Path = ROOT,
    assume_locked: bool = False,
) -> dict[str, object]:
    """Load the private seal named by one opaque campaign commitment."""

    root = _root(root)
    validated = validate_trajectory_binding(
        binding,
        root=root,
        assume_locked=assume_locked,
    )
    from tools.decomp_replay import read_private_trajectory

    document = read_private_trajectory(
        validated,
        root=root,
        assume_locked=assume_locked,
    )
    return _validate_trajectory_seal_document(document)


def seal_trajectory(
    address_value: str | int,
    session_id: str,
    *,
    root: Path = ROOT,
    campaign_state: Path = CAMPAIGN_STATE_RELATIVE,
) -> dict[str, object]:
    """Seal one trajectory and attach it to its active source campaign."""

    root = _root(root)
    address = normalize_address(address_value)
    with _state_lock(root):
        identity = _trajectory_identity_unlocked(
            address,
            session_id,
            root=root,
        )
        seal: dict[str, object] = {
            "schema_version": SCHEMA_VERSION,
            "kind": "experiment-trajectory-seal",
            "trajectory": identity,
        }
        seal["content_sha256"] = _trajectory_seal_hash(seal)
    from tools.decomp_replay import publish_private_trajectory

    try:
        binding = publish_private_trajectory(seal, root=root)
        state_path = (
            campaign_state if campaign_state.is_absolute() else root / campaign_state
        )
        attach_replay_experiment(state_path, binding)
    except Exception as error:
        raise ExperimentError("cannot seal the replay experiment") from error
    return binding


def _candidate_from_trial(receipt: Mapping[str, object]) -> dict[str, object]:
    target = receipt.get("target")
    if not isinstance(target, dict):
        raise ExperimentError("the completed trial has no target result")
    return {
        "candidate": "trial",
        "trial_id": receipt.get("trial_id"),
        "sequence": receipt.get("sequence"),
        "label": receipt.get("label"),
        "status": target.get("status"),
        "score": target.get("score"),
        "repository_effective_score": receipt.get("repository_effective_score"),
        "changed_patch_lines": receipt.get("changed_patch_lines"),
        "eligible": receipt.get("eligible") is True,
    }


def _rank(candidate: Mapping[str, object]) -> int:
    """Return the replay-compatible target-only integer quality."""

    try:
        return candidate_quality(candidate.get("status"), candidate.get("score"))
    except ValueError as error:
        raise ExperimentError("the experiment candidate quality is invalid") from error


def select_best(
    directory: Path,
    session: Mapping[str, object],
    trials: Sequence[tuple[Path, dict[str, object], str]],
    *,
    root: Path,
) -> dict[str, object]:
    candidates = [_baseline_candidate(directory, session, root)]
    for _, receipt, state in trials:
        if state == "completed":
            _validate_completed_artifacts(receipt, directory, root)
            candidate = _candidate_from_trial(receipt)
            if candidate["eligible"]:
                candidates.append(candidate)
    return max(candidates, key=_rank)


def _primary_route(receipt: Mapping[str, object]) -> str | None:
    explicit = receipt.get("route")
    if isinstance(explicit, str) and explicit:
        return explicit
    taxonomy = receipt.get("normalized_taxonomy")
    if isinstance(taxonomy, dict):
        primary = taxonomy.get("primary_route") or taxonomy.get("route")
        if isinstance(primary, str) and primary:
            return primary
        routes = taxonomy.get("routes")
        if isinstance(routes, list) and routes and isinstance(routes[0], str):
            return routes[0]
    return None


def _trajectory(
    baseline: Mapping[str, object],
    trials: Sequence[tuple[Path, dict[str, object], str]],
) -> tuple[int, list[str | None]]:
    best_rank = _rank(baseline)
    consecutive = 0
    routes: list[str | None] = []
    for _, receipt, state in sorted(trials, key=lambda item: int(item[1].get("sequence", 0))):
        improving = False
        if state == "completed" and receipt.get("eligible") is True:
            candidate_rank = _rank(_candidate_from_trial(receipt))
            if candidate_rank > best_rank:
                improving = True
                best_rank = candidate_rank
        if improving:
            consecutive = 0
            routes = []
        else:
            consecutive += 1
            routes.append(_primary_route(receipt))
    return consecutive, routes[-consecutive:] if consecutive else []


def _deadline_state(session: Mapping[str, object], root: Path, now: datetime) -> tuple[bool, str | None]:
    bound = session.get("campaign")
    if not isinstance(bound, dict):
        return False, None
    current = _campaign_context(root)
    if not isinstance(current, dict) or current.get("campaign_id") != bound.get("campaign_id"):
        return True, "the active campaign changed"
    deadline_text = current.get("deadline")
    deadline = _parse_time(deadline_text)
    return (deadline is not None and now >= deadline), str(deadline_text) if deadline_text else None


def status_document(
    address_value: str | int,
    *,
    root: Path = ROOT,
    session_id: str | None = None,
    now: datetime | None = None,
) -> dict[str, object]:
    root = _root(root)
    address = normalize_address(address_value)
    try:
        directory, session = read_session(address, root=root, session_id=session_id)
    except ExperimentError:
        if session_id is None:
            legacy = legacy_status(address, root=root)
            if legacy is not None:
                return legacy
        raise
    trials = _trial_receipts(directory)
    for _, receipt, state in trials:
        if state == "completed":
            _validate_completed_artifacts(receipt, directory, root)
    baseline = _baseline_candidate(directory, session, root)
    best = select_best(directory, session, trials, root=root)
    consecutive, routes = _trajectory(baseline, trials)
    expired, deadline = _deadline_state(session, root, now or _now())
    limits = session.get("limits") if isinstance(session.get("limits"), dict) else {}
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "experiment-status",
        "state": "verified",
        "address": address,
        "session_id": session.get("session_id"),
        "head": session.get("head"),
        "campaign": session.get("campaign"),
        "deadline": deadline,
        "deadline_expired": expired,
        "limits": limits,
        "trials_reserved": len(trials),
        "trials_completed": sum(state == "completed" for _, _, state in trials),
        "trials_failed": sum(state == "failed" for _, _, state in trials),
        "trials_pending": sum(state == "pending" for _, _, state in trials),
        "consecutive_non_improving": consecutive,
        "non_improving_routes": routes,
        "baseline": baseline,
        "best": best,
        "trials": [
            {
                "trial_id": receipt.get("trial_id"),
                "sequence": receipt.get("sequence"),
                "label": receipt.get("label"),
                "state": state,
                "eligible": receipt.get("eligible", False),
                "target": receipt.get("target"),
                "first_failing_constraint": receipt.get("first_failing_constraint"),
                "route": _primary_route(receipt),
            }
            for _, receipt, state in trials
        ],
    }


def advice_document(status: Mapping[str, object]) -> dict[str, object]:
    if status.get("state") != "verified":
        return {"action": "repair", "reason": "the session is legacy and unverified"}
    best = status.get("best")
    if not isinstance(best, dict):
        raise ExperimentError("the experiment status has no best candidate")
    if status.get("deadline_expired"):
        action, reason = "stop", "the active campaign deadline has passed"
    elif best.get("status") in {"exact", "effective"}:
        action, reason = "finalize", "the best candidate is terminal"
    else:
        limits = status.get("limits")
        maximum = limits.get("max_trials") if isinstance(limits, dict) else None
        non_improving_limit = (
            limits.get("max_non_improving") if isinstance(limits, dict) else None
        )
        reserved = status.get("trials_reserved")
        consecutive = status.get("consecutive_non_improving")
        routes = status.get("non_improving_routes")
        if isinstance(maximum, int) and isinstance(reserved, int) and reserved >= maximum:
            action, reason = "stop", "the experiment trial budget is exhausted"
        elif (
            isinstance(non_improving_limit, int)
            and isinstance(consecutive, int)
            and consecutive >= non_improving_limit
        ):
            recent = routes[-non_improving_limit:] if isinstance(routes, list) else []
            if len(recent) == non_improving_limit and recent[0] is not None and len(set(recent)) == 1:
                action, reason = (
                    "get-evidence",
                    f"the {recent[0]} route persisted through the non-improvement limit",
                )
            else:
                action, reason = (
                    "repair",
                    "the recent source models have different mismatch routes",
                )
        else:
            action, reason = "repair", "the session has budget for another source model"
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "experiment-advice",
        "action": action,
        "reason": reason,
        "address": status.get("address"),
        "session_id": status.get("session_id"),
        "best": best,
    }


def legacy_status(address_value: str | int, *, root: Path = ROOT) -> dict[str, object] | None:
    root = _root(root)
    address = normalize_address(address_value)
    directory = _session_base(root, address)
    if not directory.is_dir():
        return None
    names = []
    for path in sorted(directory.glob("*.report.json")):
        if path.is_file() and not path.is_symlink():
            names.append(path.name[: -len(".report.json")])
    flat_markers = names or any(
        (directory / name).exists()
        for name in ("baseline-report.json", "baseline-data-report.json", "compiler-context.json")
    )
    if not flat_markers:
        return None
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "experiment-status",
        "state": "legacy-unverified",
        "address": address,
        "labels": names,
        "best": None,
        "advice": "start a bounded session before you use a trial result",
    }


def report_document(
    address_value: str | int,
    *,
    root: Path = ROOT,
    session_id: str | None = None,
    label: str | None = None,
    now: datetime | None = None,
) -> dict[str, object]:
    status = status_document(
        address_value, root=root, session_id=session_id, now=now
    )
    if label is None:
        result = dict(status)
        result["advice"] = advice_document(status)
        return result
    if status.get("state") == "legacy-unverified":
        labels = status.get("labels", [])
        if label not in labels:
            raise ExperimentError(f"the legacy trial label does not exist: {label}")
        return {
            "schema_version": SCHEMA_VERSION,
            "kind": "experiment-trial-report",
            "state": "legacy-unverified",
            "address": status.get("address"),
            "label": label,
        }
    address = normalize_address(address_value)
    directory, _ = read_session(address, root=root, session_id=session_id)
    matches = [
        (path, receipt, state)
        for path, receipt, state in _trial_receipts(directory)
        if label in {receipt.get("label"), receipt.get("trial_id")}
    ]
    if len(matches) != 1:
        raise ExperimentError(f"the trial label does not exist: {label}")
    _, receipt, state = matches[0]
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "experiment-trial-report",
        "state": state,
        "receipt": receipt,
    }


def _print_document(value: Mapping[str, object], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps(value, indent=2, sort_keys=True))
        return
    if output_format == "summary":
        if value.get("kind") == "experiment-trial":
            target = value.get("target", {})
            print(f"Trial: {value.get('trial_id')}")
            print(f"Target: {value.get('address')}")
            print(f"Status: {target.get('status') if isinstance(target, dict) else 'missing'}")
            score = target.get("score") if isinstance(target, dict) else None
            print(f"Score: {score * 100:.4f}%" if isinstance(score, (int, float)) else "Score: missing")
            print(f"Eligible: {'yes' if value.get('eligible') else 'no'}")
            if value.get("first_failing_constraint"):
                print(f"First failing constraint: {value['first_failing_constraint']}")
            return
        print(json.dumps(value, sort_keys=True))
        return
    raise ExperimentError("the output format is invalid")


START_PLAN_FIELDS = (
    "session_id",
    "session_directory",
    "baseline_report",
    "baseline_data_report",
    "compiler_context",
    "source_patch",
    "build_lock",
)
TRIAL_PLAN_FIELDS = (
    "session_id",
    "trial_id",
    "trial_directory",
    "report",
    "diff",
    "source_patch",
    "compiler_context",
    "build_lock",
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    subparsers = parser.add_subparsers(dest="action", required=True)
    start = subparsers.add_parser("begin-session")
    start.add_argument("address")
    start.add_argument("--brief", type=Path)
    start.add_argument("--max-trials", type=int, default=DEFAULT_MAX_TRIALS)
    start.add_argument(
        "--max-non-improving", type=int, default=DEFAULT_MAX_NON_IMPROVING
    )
    start.add_argument("--format", choices=("json", "lines"), default="json")
    activate = subparsers.add_parser("attach-baseline")
    activate.add_argument("address")
    activate.add_argument("--session", required=True)
    reserve = subparsers.add_parser("reserve")
    reserve.add_argument("address")
    reserve.add_argument("label")
    reserve.add_argument("--session")
    reserve.add_argument("--question")
    reserve.add_argument("--parent")
    reserve.add_argument("--route")
    reserve.add_argument("--model")
    reserve.add_argument("--format", choices=("json", "lines"), default="json")
    branch = subparsers.add_parser("branch")
    branch.add_argument("address")
    branch.add_argument("label")
    branch.add_argument("--session")
    branch.add_argument("--question")
    branch.add_argument("--parent", required=True)
    branch.add_argument("--route", required=True)
    branch.add_argument("--model")
    branch.add_argument("--format", choices=("json", "lines"), default="json")
    measure = subparsers.add_parser("measure-plan")
    measure.add_argument("address")
    measure.add_argument("--session")
    measure.add_argument("--trial", required=True)
    measure.add_argument("--format", choices=("json", "lines"), default="json")
    fail = subparsers.add_parser("fail")
    fail.add_argument("address")
    fail.add_argument("--session")
    fail.add_argument("--trial", required=True)
    fail.add_argument("--stage", required=True, choices=sorted(FAILURE_STAGES))
    fail.add_argument("--exit-code", type=int)
    fail.add_argument("--message")
    record = subparsers.add_parser("record")
    record.add_argument("address")
    record.add_argument("--session")
    record.add_argument("--trial", required=True)
    record.add_argument("--format", choices=("json", "summary"), default="summary")
    seal = subparsers.add_parser("seal")
    seal.add_argument("address")
    seal.add_argument("--session", required=True)
    for name in ("status", "best", "advise", "report"):
        command = subparsers.add_parser(name)
        command.add_argument("address")
        if name == "report":
            command.add_argument("label", nargs="?")
        command.add_argument("--session")
        command.add_argument("--json", action="store_true")
    subparsers.add_parser("lock-path")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    root = _root(arguments.root)
    try:
        if arguments.action == "begin-session":
            value = begin_session(
                arguments.address,
                root=root,
                brief=arguments.brief,
                max_trials=arguments.max_trials,
                max_non_improving=arguments.max_non_improving,
            )
            if arguments.format == "lines":
                print(_plan_lines(value, START_PLAN_FIELDS))
            else:
                _print_document(value, "json")
        elif arguments.action == "attach-baseline":
            _print_document(
                attach_baseline(arguments.address, arguments.session, root=root), "json"
            )
        elif arguments.action == "reserve":
            value = reserve_trial(
                arguments.address,
                arguments.label,
                root=root,
                session_id=arguments.session,
                question=arguments.question,
                parent=arguments.parent,
                route=arguments.route,
                model=arguments.model,
            )
            if arguments.format == "lines":
                print(_plan_lines(value, TRIAL_PLAN_FIELDS))
            else:
                _print_document(value, "json")
        elif arguments.action == "branch":
            value = reserve_trial(
                arguments.address,
                arguments.label,
                root=root,
                session_id=arguments.session,
                question=arguments.question,
                parent=arguments.parent,
                route=arguments.route,
                model=arguments.model,
                bind_parent=True,
            )
            if arguments.format == "lines":
                print(_plan_lines(value, TRIAL_PLAN_FIELDS))
            else:
                _print_document(value, "json")
        elif arguments.action == "measure-plan":
            value = measure_plan(
                arguments.address,
                arguments.trial,
                root=root,
                session_id=arguments.session,
            )
            if arguments.format == "lines":
                print(_plan_lines(value, TRIAL_PLAN_FIELDS))
            else:
                _print_document(value, "json")
        elif arguments.action == "fail":
            _print_document(
                fail_trial(
                    arguments.address,
                    arguments.trial,
                    root=root,
                    session_id=arguments.session,
                    stage=arguments.stage,
                    exit_code=arguments.exit_code,
                    message=arguments.message,
                ),
                "json",
            )
        elif arguments.action == "record":
            _print_document(
                record_trial(
                    arguments.address,
                    arguments.trial,
                    root=root,
                    session_id=arguments.session,
                ),
                arguments.format,
            )
        elif arguments.action == "seal":
            _print_document(
                seal_trajectory(
                    arguments.address,
                    arguments.session,
                    root=root,
                ),
                "json",
            )
        elif arguments.action == "status":
            value = status_document(
                arguments.address, root=root, session_id=arguments.session
            )
            _print_document(value, "json" if arguments.json else "summary")
        elif arguments.action == "best":
            status = status_document(
                arguments.address, root=root, session_id=arguments.session
            )
            value = status.get("best")
            if not isinstance(value, dict):
                raise ExperimentError("the legacy experiment has no verified best result")
            _print_document(value, "json" if arguments.json else "summary")
        elif arguments.action == "advise":
            value = advice_document(
                status_document(
                    arguments.address, root=root, session_id=arguments.session
                )
            )
            _print_document(value, "json" if arguments.json else "summary")
        elif arguments.action == "report":
            value = report_document(
                arguments.address,
                root=root,
                session_id=arguments.session,
                label=arguments.label,
            )
            _print_document(value, "json" if arguments.json else "summary")
        else:
            print(str(root / EXPERIMENTS_RELATIVE / "build.lock"))
    except (ExperimentError, OSError) as error:
        print(f"experiment failed: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
