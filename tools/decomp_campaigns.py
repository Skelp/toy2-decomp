#!/usr/bin/env python3
"""Record measured campaign results and summarize reconstruction throughput."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import uuid
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path

try:
    from tools.decomp_resources import (
        format_resource,
        parse_resource,
        resource_rows,
        resource_source_snapshot,
        selected_evidence,
        staged_resource_source_problems,
    )
except ModuleNotFoundError:  # Direct invocation uses tools/ as sys.path[0].
    from decomp_resources import (  # type: ignore[no-redef]
        format_resource,
        parse_resource,
        resource_rows,
        resource_source_snapshot,
        selected_evidence,
        staged_resource_source_problems,
    )


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LEDGER = ROOT / "tools" / "Resources" / "campaign-ledger.jsonl"
LEGACY_LEDGER = ROOT / ".git" / "decomp-campaigns.jsonl"
DEFAULT_STATE = ROOT / "build" / "decomp-campaign-state.json"
DEFAULT_SOURCE_MODELS = ROOT / ".notes" / "source-models.md"
DEFAULT_FUNCTION_MAP = ROOT / "tools" / "Resources" / "functions_map.txt"
DEFAULT_FUNCTION_SIZES = ROOT / "build" / "decomp-function-sizes.json"
DEFAULT_BASELINE_REPORT = ROOT / "build" / "decomp-baseline-report.json"
DEFAULT_CURRENT_REPORT = ROOT / "build" / "decomp-current-report.json"
DEFAULT_BASELINE_DATA_REPORT = ROOT / "build" / "decomp-baseline-data-report.json"
DEFAULT_CURRENT_DATA_REPORT = ROOT / "build" / "decomp-current-data-report.json"

EVIDENCE_KINDS = (
    "abi",
    "analogue",
    "callee",
    "caller",
    "compiler",
    "dispatch",
    "dwarf",
    "layout",
    "map",
    "retail-data",
    "source-change",
    "source-model",
    "other",
)

SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".def",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".inl",
    ".ipp",
    ".rc",
    ".s",
    ".asm",
    ".cmake",
}

DEADLINE_FACTORS = {
    "preflight": 5.0 / 12.0,
    "first_score": 2.0 / 3.0,
    "stop": 1.0,
    "extension": 5.0 / 4.0,
}

SOURCE_ANNOTATION_RE = re.compile(
    r"//\s*(FUNCTION|LIBRARY|STUB):\s*TOY2\s+0x([0-9a-fA-F]+)"
)


@dataclass(frozen=True)
class AddressStats:
    attempts: int = 0
    zero_yield_attempts: int = 0
    penalty_attempts: int = 0
    evidence_events: int = 0
    minutes: float = 0.0
    effective_bytes: float = 0.0
    initialized_bytes: float = 0.0


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def timestamp(value: datetime) -> str:
    if value.tzinfo is None:
        raise ValueError("a campaign timestamp needs a UTC offset")
    return value.astimezone(timezone.utc).isoformat(timespec="seconds")


def parse_address(value: str) -> str:
    try:
        address = int(value, 16)
    except (TypeError, ValueError) as error:
        raise argparse.ArgumentTypeError("use an address such as 0x00401000") from error
    if address < 0 or address > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("the address must fit in 32 bits")
    return f"0x{address:08X}"


def _read_records(path: Path) -> list[dict[str, object]]:
    if not path.exists():
        return []
    records: list[dict[str, object]] = []
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip():
            continue
        try:
            record = json.loads(raw)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid campaign record at line {line_number}: {error}") from error
        if not isinstance(record, dict):
            raise ValueError(f"invalid campaign record at line {line_number}: use a JSON object")
        records.append(record)
    return records


def _record_fingerprint(record: dict[str, object]) -> str:
    return json.dumps(record, sort_keys=True, separators=(",", ":"))


def merge_records(
    committed: list[dict[str, object]], legacy: list[dict[str, object]]
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    """Merge legacy records without removing intentional duplicate campaigns."""

    available = Counter(_record_fingerprint(item) for item in committed)
    additions: list[dict[str, object]] = []
    for item in legacy:
        key = _record_fingerprint(item)
        if available[key]:
            available[key] -= 1
        else:
            additions.append(item)
    return [*committed, *additions], additions


def read_records(path: Path = DEFAULT_LEDGER) -> list[dict[str, object]]:
    records = _read_records(path)
    if path == DEFAULT_LEDGER and LEGACY_LEDGER.exists():
        records, _ = merge_records(records, _read_records(LEGACY_LEDGER))
    return records


def migrate_legacy_ledger(
    path: Path = DEFAULT_LEDGER, legacy_path: Path = LEGACY_LEDGER
) -> int:
    """Append local legacy history that the tracked ledger does not contain."""

    if path != DEFAULT_LEDGER or not legacy_path.exists():
        return 0
    committed = _read_records(path)
    merged, additions = merge_records(committed, _read_records(legacy_path))
    if additions:
        write_records(path, merged)
    return len(additions)


def _record_addresses(record: dict[str, object]) -> list[int]:
    values = record.get("addresses", [])
    if not isinstance(values, list):
        return []
    addresses = []
    for value in values:
        try:
            addresses.append(int(str(value), 16))
        except (TypeError, ValueError):
            continue
    return addresses


def _chronological_records(
    records: list[dict[str, object]],
) -> list[dict[str, object]]:
    def sort_key(indexed: tuple[int, dict[str, object]]) -> tuple[float, int]:
        index, record = indexed
        try:
            value = datetime.fromisoformat(str(record.get("timestamp")))
            if value.tzinfo is None:
                return (float("-inf"), index)
            return (value.timestamp(), index)
        except (TypeError, ValueError, OverflowError):
            return (float("-inf"), index)

    return [record for _, record in sorted(enumerate(records), key=sort_key)]


def address_stats(records: list[dict[str, object]]) -> dict[int, AddressStats]:
    totals: dict[int, dict[str, float]] = defaultdict(
        lambda: {
            "attempts": 0,
            "zero_yield_attempts": 0,
            "penalty_attempts": 0,
            "evidence_events": 0,
            "minutes": 0.0,
            "effective_bytes": 0.0,
            "initialized_bytes": 0.0,
        }
    )
    for record in _chronological_records(records):
        addresses = _record_addresses(record)
        if not addresses:
            continue
        if record.get("record_type") == "evidence":
            for address in addresses:
                totals[address]["penalty_attempts"] = 0
                totals[address]["evidence_events"] += 1
            continue
        if record.get("record_type", "campaign") != "campaign":
            continue
        effective = float(record.get("effective_bytes", 0.0) or 0.0)
        initialized = float(record.get("initialized_bytes", 0) or 0)
        minutes = float(record.get("minutes", 0.0) or 0.0)
        share = float(len(addresses))
        for address in addresses:
            target_deltas = record.get("target_deltas")
            target = (
                target_deltas.get(f"0x{address:08X}", {})
                if isinstance(target_deltas, dict)
                else {}
            )
            if isinstance(target_deltas, dict) and isinstance(target, dict):
                address_effective = float(target.get("effective_bytes", 0.0) or 0.0)
                address_initialized = float(
                    target.get("initialized_bytes", 0.0) or 0.0
                )
            else:
                address_effective = effective / share
                address_initialized = initialized / share
            is_zero = address_effective <= 0.0 and address_initialized <= 0.0
            item = totals[address]
            item["attempts"] += 1
            item["zero_yield_attempts"] += int(is_zero)
            item["penalty_attempts"] += int(is_zero)
            item["minutes"] += minutes / share
            item["effective_bytes"] += address_effective
            item["initialized_bytes"] += address_initialized
    return {
        address: AddressStats(
            attempts=int(values["attempts"]),
            zero_yield_attempts=int(values["zero_yield_attempts"]),
            penalty_attempts=int(values["penalty_attempts"]),
            evidence_events=int(values["evidence_events"]),
            minutes=values["minutes"],
            effective_bytes=values["effective_bytes"],
            initialized_bytes=values["initialized_bytes"],
        )
        for address, values in totals.items()
    }


def write_records(path: Path, records: list[dict[str, object]]) -> None:
    content = "".join(
        json.dumps(record, sort_keys=True) + "\n" for record in records
    )
    write_text(path, content)


def _record_identity(record: dict[str, object]) -> tuple[str, str] | None:
    for field in ("campaign_id", "evidence_id", "abort_id"):
        value = record.get(field)
        if isinstance(value, str) and value:
            return field, value
    return None


def append_record(path: Path, record: dict[str, object]) -> None:
    records = _read_records(path)
    identity = _record_identity(record)
    if identity is not None:
        for existing in records:
            if _record_identity(existing) != identity:
                continue
            if _record_fingerprint(existing) != _record_fingerprint(record):
                raise ValueError(f"{identity[0]} already identifies a different record")
            return
    write_records(path, [*records, record])


def is_duplicate(records: list[dict[str, object]], record: dict[str, object]) -> bool:
    identity = _record_identity(record)
    if identity is not None:
        return any(_record_identity(item) == identity for item in records)
    if record.get("schema_version") == 2:
        return any(
            item.get("schema_version") == 2
            and item.get("record_type") == record.get("record_type")
            and item.get("started_at") == record.get("started_at")
            for item in records
        )
    keys = ("mode", "result", "addresses", "commit")
    record_type = record.get("record_type", "campaign")
    return any(
        item.get("record_type", "campaign") == record_type
        and all(item.get(key) == record.get(key) for key in keys)
        for item in records
    )


def _read_json_object(path: Path, description: str) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise ValueError(f"{description} does not exist: {path}") from error
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {description}: {path}: {error}") from error
    if not isinstance(payload, dict):
        raise ValueError(f"{description} must contain a JSON object: {path}")
    return payload


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise ValueError(f"cannot hash report: {path}: {error}") from error
    return digest.hexdigest()


def _address_int(value: object) -> int | None:
    if isinstance(value, int):
        return value
    try:
        return int(str(value), 16)
    except (TypeError, ValueError):
        return None


def read_function_sizes(map_path: Path, sizes_path: Path) -> dict[int, int]:
    addresses: list[int] = []
    try:
        for raw in map_path.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            try:
                addresses.append(int(line.split(None, 1)[0], 16))
            except ValueError:
                continue
    except OSError as error:
        raise ValueError(f"cannot read the function map: {map_path}: {error}") from error
    addresses = sorted(set(addresses))
    mapped = set(addresses)
    sizes = {
        address: following - address
        for address, following in zip(addresses, addresses[1:])
        if following > address
    }
    if not sizes_path.exists():
        return sizes
    try:
        rows = json.loads(sizes_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read function sizes: {sizes_path}: {error}") from error
    if not isinstance(rows, list):
        raise ValueError(f"function sizes must contain a JSON list: {sizes_path}")
    for row in rows:
        if not isinstance(row, dict):
            continue
        address = _address_int(row.get("address") or row.get("entry_point"))
        size = row.get("original_size", row.get("size"))
        if (
            address not in mapped
            or not isinstance(size, int)
            or size <= 1
        ):
            continue
        sizes[address] = size
    return sizes


def _size_snapshot(sizes: dict[int, int]) -> dict[str, int]:
    return {f"0x{address:08X}": size for address, size in sorted(sizes.items())}


def _size_snapshot_hash(snapshot: dict[str, int]) -> str:
    encoded = json.dumps(snapshot, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _decode_size_snapshot(value: object) -> dict[int, int]:
    if not isinstance(value, dict):
        raise ValueError("active campaign has no function-size snapshot")
    sizes: dict[int, int] = {}
    for address_text, size in value.items():
        address = _address_int(address_text)
        if address is None or not isinstance(size, int) or size <= 0:
            raise ValueError("active campaign has an invalid function-size snapshot")
        sizes[address] = size
    return sizes


def effective_code_by_address(
    report_path: Path, sizes: dict[int, int]
) -> dict[int, float]:
    payload = _read_json_object(report_path, "code report")
    rows = payload.get("data")
    if not isinstance(rows, list):
        raise ValueError(f"code report has no data list: {report_path}")
    values: dict[int, float] = {}
    for row in rows:
        if not isinstance(row, dict) or row.get("stub"):
            continue
        address = _address_int(row.get("address"))
        if address not in sizes:
            continue
        try:
            score = float(row.get("matching", 0.0) or 0.0)
        except (TypeError, ValueError) as error:
            raise ValueError(f"code report has an invalid score: {report_path}") from error
        values[address] = sizes[address] * (
            1.0 if row.get("effective") else score
        )
    return values


def effective_code_bytes(report_path: Path, sizes: dict[int, int]) -> float:
    return sum(effective_code_by_address(report_path, sizes).values())


def initialized_data_bytes(report_path: Path) -> float:
    payload = _read_json_object(report_path, "typed-data report")
    variables = payload.get("variables")
    if not isinstance(variables, dict):
        raise ValueError(f"typed-data report has no variables object: {report_path}")
    value = variables.get("explained_bytes")
    if not isinstance(value, (int, float)):
        raise ValueError(f"typed-data report has no explained byte count: {report_path}")
    return float(value)


def initialized_data_by_address(report_path: Path) -> dict[int, float]:
    payload = _read_json_object(report_path, "typed-data report")
    variables = payload.get("variables")
    if not isinstance(variables, dict):
        raise ValueError(f"typed-data report has no variables object: {report_path}")
    rows = variables.get("variables", [])
    if not isinstance(rows, list):
        raise ValueError(f"typed-data report has no variable list: {report_path}")
    values: dict[int, float] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        address = _address_int(row.get("original_address"))
        matched = row.get("matched_bytes")
        if address is None or not isinstance(matched, (int, float)):
            continue
        values[address] = float(matched)
    return values


def write_state(path: Path, state: dict[str, object]) -> None:
    write_text(path, json.dumps(state, indent=2, sort_keys=True) + "\n")


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        temporary.write_text(content, encoding="utf-8")
        temporary.replace(path)
    finally:
        if temporary.exists():
            temporary.unlink()


def read_state(path: Path) -> dict[str, object]:
    return _read_json_object(path, "active campaign state")


def _is_source_path(path: Path) -> bool:
    return path.name == "CMakeLists.txt" or path.suffix.casefold() in SOURCE_SUFFIXES


def _source_paths(root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if result.returncode == 0:
        paths = [
            Path(value.decode("utf-8", errors="surrogateescape"))
            for value in result.stdout.split(b"\0")
            if value
        ]
        return sorted(path for path in paths if _is_source_path(path))
    excluded = {".git", ".tooling", "build", "external", "original"}
    return sorted(
        path.relative_to(root)
        for path in root.rglob("*")
        if path.is_file()
        and not excluded.intersection(path.relative_to(root).parts)
        and _is_source_path(path)
    )


def source_worktree_snapshot(root: Path) -> dict[str, str]:
    snapshot: dict[str, str] = {}
    for relative in _source_paths(root):
        path = root / relative
        if not path.exists():
            snapshot[relative.as_posix()] = "missing"
        elif path.is_symlink():
            snapshot[relative.as_posix()] = f"symlink:{path.readlink()}"
        else:
            snapshot[relative.as_posix()] = file_hash(path)
    return snapshot


def source_index_snapshot(root: Path) -> dict[str, list[str]]:
    result = subprocess.run(
        ["git", "ls-files", "--stage", "-z"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        return {}
    snapshot: dict[str, list[str]] = defaultdict(list)
    for raw in result.stdout.split(b"\0"):
        if not raw or b"\t" not in raw:
            continue
        metadata, encoded_path = raw.split(b"\t", 1)
        relative = Path(encoded_path.decode("utf-8", errors="surrogateescape"))
        if _is_source_path(relative):
            snapshot[relative.as_posix()].append(
                metadata.decode("ascii", errors="replace")
            )
    return {
        path: sorted(entries)
        for path, entries in sorted(snapshot.items())
    }


def _repository_paths(root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(
            f"cannot fingerprint the campaign repository: {detail or root}"
        )
    return sorted(
        Path(value.decode("utf-8", errors="surrogateescape"))
        for value in result.stdout.split(b"\0")
        if value
    )


def _directory_fingerprint(path: Path) -> str:
    head = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
    )
    status = subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain=v2", "-z"],
        check=False,
        capture_output=True,
    )
    payload = head.stdout + b"\0" + status.stdout
    if head.returncode != 0 or status.returncode != 0:
        raise ValueError(f"cannot fingerprint tracked directory: {path}")
    return f"directory:{hashlib.sha256(payload).hexdigest()}"


def repository_worktree_snapshot(root: Path) -> dict[str, str]:
    snapshot: dict[str, str] = {}
    for relative in _repository_paths(root):
        path = root / relative
        if not path.exists():
            snapshot[relative.as_posix()] = "missing"
        elif path.is_symlink():
            snapshot[relative.as_posix()] = f"symlink:{path.readlink()}"
        elif path.is_dir():
            snapshot[relative.as_posix()] = _directory_fingerprint(path)
        else:
            snapshot[relative.as_posix()] = file_hash(path)
    return snapshot


def repository_index_snapshot(root: Path) -> dict[str, list[str]]:
    result = subprocess.run(
        ["git", "ls-files", "--stage", "-z"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(
            f"cannot fingerprint the campaign repository index: {detail or root}"
        )
    snapshot: dict[str, list[str]] = defaultdict(list)
    for raw in result.stdout.split(b"\0"):
        if not raw or b"\t" not in raw:
            continue
        metadata, encoded_path = raw.split(b"\t", 1)
        relative = encoded_path.decode("utf-8", errors="surrogateescape")
        snapshot[relative].append(metadata.decode("ascii", errors="replace"))
    return {
        path: sorted(entries)
        for path, entries in sorted(snapshot.items())
    }


def source_worktree_changes(
    before: dict[str, object], after: dict[str, object]
) -> list[str]:
    return sorted(
        path
        for path in before.keys() | after.keys()
        if before.get(path) != after.get(path)
    )


def _snapshot_hash(snapshot: object) -> str:
    payload = json.dumps(snapshot, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(payload).hexdigest()


def _mapped_spans(path: Path) -> tuple[set[int], dict[int, int]]:
    addresses: list[int] = []
    try:
        for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = raw.strip()
            if line and not line.startswith("#"):
                addresses.append(int(line.split(None, 1)[0], 16))
    except (OSError, ValueError) as error:
        raise ValueError(f"cannot read the function map: {path}: {error}") from error
    ordered = sorted(set(addresses))
    return set(ordered), {
        address: following - address
        for address, following in zip(ordered, ordered[1:])
    }


def _analyzed_sizes(path: Path) -> dict[int, int]:
    if not path.exists():
        return {}
    try:
        rows = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read function sizes: {path}: {error}") from error
    if not isinstance(rows, list):
        raise ValueError(f"function sizes must contain a JSON list: {path}")
    sizes: dict[int, int] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        address = _address_int(row.get("address") or row.get("entry_point"))
        size = row.get("original_size", row.get("size"))
        if address is not None and isinstance(size, int) and size > 1:
            sizes[address] = size
    return sizes


def _source_annotations(source_root: Path) -> dict[int, str]:
    addresses: dict[int, str] = {}
    if not source_root.exists():
        return addresses
    for path in source_root.rglob("*"):
        if not path.is_file() or not _is_source_path(path):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for state, address in SOURCE_ANNOTATION_RE.findall(text):
            addresses[int(address, 16)] = state
    return addresses


def validate_source_target(
    address_text: str,
    mode: str,
    map_path: Path,
    sizes_path: Path,
    source_root: Path,
) -> None:
    if mode not in ("coverage", "refinement"):
        return
    address = int(address_text, 16)
    mapped, _ = _mapped_spans(map_path)
    if address not in mapped:
        raise ValueError(f"{address_text} is not in functions_map.txt")
    annotation = _source_annotations(source_root).get(address)
    if mode == "coverage" and annotation not in (None, "STUB"):
        raise ValueError(
            f"{address_text} is {annotation}. A coverage target must be STUB or unannotated"
        )
    if mode == "refinement" and annotation != "FUNCTION":
        state = annotation or "unannotated"
        raise ValueError(
            f"{address_text} is {state}. A refinement target must be FUNCTION"
        )
    if mode == "refinement":
        return
    validate_coverage_map_ceiling(address_text, map_path, sizes_path)


def validate_coverage_map_ceiling(
    address_text: str, map_path: Path, sizes_path: Path
) -> None:
    """Reject a coverage target whose saved body cannot reach the pilot gate."""

    address = int(address_text, 16)
    _, spans = _mapped_spans(map_path)
    analyzed_size = _analyzed_sizes(sizes_path).get(address)
    map_size = spans.get(address)
    if analyzed_size is not None and map_size and analyzed_size / map_size < 0.6:
        raise ValueError(
            f"{address_text} has a map defect. Repair the function map before a campaign"
        )


def validate_family_size_values(
    addresses: list[str], sizes: dict[int, int]
) -> None:
    member_sizes = [sizes.get(int(address, 16)) for address in addresses]
    if any(size is None for size in member_sizes):
        raise ValueError(
            "a family campaign needs saved retail body sizes for every member"
        )
    known_sizes = [int(size) for size in member_sizes if size is not None]
    if (max(known_sizes) - min(known_sizes)) / max(known_sizes) > 0.01:
        raise ValueError(
            "the family retail body sizes differ by more than one percent"
        )


def validate_family_sizes(
    anchor_text: str, member_text: str, sizes_path: Path
) -> None:
    validate_family_size_values(
        [anchor_text, member_text], _analyzed_sizes(sizes_path)
    )


def _validate_estimate(name: str, value: float | None, *, allow_zero: bool) -> None:
    if value is None:
        return
    if not math.isfinite(value) or value < 0 or (not allow_zero and value == 0):
        qualifier = "zero or greater" if allow_zero else "greater than zero"
        raise ValueError(f"{name} must be finite and {qualifier}")


def _campaign_deadlines(
    started: datetime, expected_minutes: float | None
) -> dict[str, str | None]:
    if expected_minutes is None:
        return {f"{name}_deadline": None for name in DEADLINE_FACTORS}
    return {
        f"{name}_deadline": timestamp(
            started + timedelta(minutes=expected_minutes * factor)
        )
        for name, factor in DEADLINE_FACTORS.items()
    }


def _validate_campaign_targets(state: dict[str, object]) -> None:
    mode = str(state.get("mode", ""))
    addresses = state.get("addresses")
    if not isinstance(addresses, list) or not all(
        isinstance(address, str) for address in addresses
    ):
        raise ValueError("active campaign has invalid addresses")
    resource = state.get("resource")
    if mode in ("coverage", "refinement", "data"):
        if not addresses:
            raise ValueError("a source campaign needs at least one --address")
        if resource is not None:
            raise ValueError("a source campaign cannot have a resource target")
    elif mode == "resource":
        if addresses:
            raise ValueError("a resource campaign cannot have source targets")
        if not isinstance(resource, str):
            raise ValueError("a resource campaign needs exactly one resource")
        parse_resource(resource)
    elif mode == "meta":
        if addresses or resource is not None:
            raise ValueError("a meta campaign cannot have source targets")
    else:
        raise ValueError(f"active campaign has invalid mode: {mode}")


def start_campaign(
    path: Path,
    mode: str,
    addresses: list[str],
    subsystem: str,
    now: datetime | None = None,
    worktree_root: Path = ROOT,
    campaign_id: str | None = None,
    map_path: Path | None = None,
    sizes_path: Path | None = None,
    source_root: Path | None = None,
    expected_minutes: float | None = None,
    expected_retained_bytes: float | None = None,
    family: bool = False,
    resources: list[tuple[int, int, int]] | None = None,
) -> dict[str, object]:
    resources = resources or []
    if path.exists():
        raise ValueError("an active campaign already exists. Record or abort it first")
    if mode in ("coverage", "refinement", "data") and not addresses:
        raise ValueError("a source campaign needs at least one --address")
    if mode in ("meta", "resource") and addresses:
        label = "meta" if mode == "meta" else "resource"
        raise ValueError(f"a {label} campaign cannot have source targets")
    if mode == "resource" and len(resources) != 1:
        raise ValueError("a resource campaign needs exactly one --resource")
    if mode != "resource" and resources:
        raise ValueError("--resource requires --mode resource")
    if family and mode not in ("coverage", "refinement"):
        raise ValueError("--family requires --mode coverage or refinement")
    if family and len(addresses) != 1:
        raise ValueError("a family campaign must start with one anchor target")
    if len(addresses) != len(set(addresses)):
        raise ValueError("the initial campaign targets contain a duplicate address")
    if len(addresses) > 3:
        raise ValueError("a campaign can start with at most three targets")
    if mode == "data" and len(addresses) > 3:
        raise ValueError("a data campaign can have at most three targets")
    _validate_estimate("--expected-minutes", expected_minutes, allow_zero=False)
    _validate_estimate(
        "--expected-retained-bytes", expected_retained_bytes, allow_zero=True
    )
    if campaign_id is not None and not campaign_id.strip():
        raise ValueError("campaign_id cannot be empty")
    if map_path is not None and sizes_path is not None:
        for address in addresses:
            validate_source_target(
                address,
                mode,
                map_path,
                sizes_path,
                source_root or worktree_root / "src",
            )
    started = now or utc_now()
    started_at = timestamp(started)
    worktree = source_worktree_snapshot(worktree_root)
    index = source_index_snapshot(worktree_root)
    repository_worktree = repository_worktree_snapshot(worktree_root)
    repository_index = repository_index_snapshot(worktree_root)
    resource_sources = resource_source_snapshot(worktree_root)
    state: dict[str, object] = {
        "schema_version": 2,
        "campaign_id": campaign_id or str(uuid.uuid4()),
        "mode": mode,
        "addresses": addresses,
        "resource": format_resource(resources[0]) if resources else None,
        "target_events": [],
        "subsystem": subsystem.strip(),
        "family": family,
        "started_at": started_at,
        "first_score_at": None,
        "scored_addresses": [],
        "score_events": [],
        "phase": "started",
        "expected_minutes": expected_minutes,
        "expected_retained_bytes": expected_retained_bytes,
        "deadlines": _campaign_deadlines(started, expected_minutes),
        "source_worktree_root": str(worktree_root.resolve()),
        "source_worktree": worktree,
        "source_worktree_sha256": _snapshot_hash(worktree),
        "source_index": index,
        "source_index_sha256": _snapshot_hash(index),
        "repository_worktree": repository_worktree,
        "repository_worktree_sha256": _snapshot_hash(repository_worktree),
        "repository_index": repository_index,
        "repository_index_sha256": _snapshot_hash(repository_index),
        "resource_sources": resource_sources,
        "resource_sources_sha256": _snapshot_hash(resource_sources),
    }
    write_state(path, state)
    return state


def attach_baseline(
    state_path: Path,
    report_path: Path,
    data_report_path: Path,
    map_path: Path,
    sizes_path: Path,
    now: datetime | None = None,
    source_root: Path | None = None,
) -> dict[str, object]:
    state = read_state(state_path)
    if state.get("phase") == "finalizing":
        raise ValueError("campaign finalization is pending. Retry campaigns record")
    if state.get("baseline_report") or state.get("baseline_data_report"):
        raise ValueError("the active campaign already has baseline reports")
    started = _parse_time(state.get("started_at"), "started_at")
    attached = now or utc_now()
    timestamp(attached)
    if attached < started:
        raise ValueError("the baseline time is before the campaign start time")
    sizes = read_function_sizes(map_path, sizes_path)
    analyzed_sizes = _analyzed_sizes(sizes_path)
    if not analyzed_sizes:
        raise ValueError(
            "the campaign baseline needs a nonempty retail function-size snapshot"
        )
    addresses = state.get("addresses", [])
    if not isinstance(addresses, list):
        raise ValueError("active campaign has invalid addresses")
    if state.get("family") is True:
        if len(addresses) != 1:
            raise ValueError("a family campaign baseline needs one anchor target")
        validate_family_size_values([str(addresses[0])], analyzed_sizes)
    mode = str(state.get("mode", ""))
    _validate_campaign_targets(state)
    if mode in ("coverage", "refinement"):
        worktree_root = Path(str(state.get("source_worktree_root", ROOT)))
        campaign_source_root = source_root or worktree_root / "src"
        for address in addresses:
            validate_source_target(
                str(address),
                mode,
                map_path,
                sizes_path,
                campaign_source_root,
            )
    snapshot = _size_snapshot(sizes)
    analyzed_snapshot = _size_snapshot(analyzed_sizes)
    effective_before = effective_code_bytes(report_path, sizes)
    initialized_before = initialized_data_bytes(data_report_path)
    resource_before = None
    if mode == "resource":
        resource = parse_resource(str(state["resource"]))
        worktree_root = Path(str(state.get("source_worktree_root", ROOT)))
        rows = resource_rows(
            worktree_root / "original" / "toy2.exe",
            worktree_root / "build" / "toy2.exe",
        )
        resource_before = selected_evidence(rows, resource)
        if not resource_before["leaf_count"]:
            raise ValueError(f"retail resource {state['resource']} does not exist")
    state.update(
        {
            "baseline_report": str(report_path.resolve()),
            "baseline_report_sha256": file_hash(report_path),
            "baseline_data_report": str(data_report_path.resolve()),
            "baseline_data_report_sha256": file_hash(data_report_path),
            "effective_bytes_before": effective_before,
            "initialized_bytes_before": initialized_before,
            "resource_before": resource_before,
            "baseline_at": timestamp(attached),
            "function_sizes": snapshot,
            "function_size_snapshot_sha256": _size_snapshot_hash(snapshot),
            "analyzed_function_sizes": analyzed_snapshot,
            "analyzed_function_sizes_sha256": _size_snapshot_hash(analyzed_snapshot),
            "functions_map_sha256": file_hash(map_path),
            "function_sizes_file_sha256": (
                file_hash(sizes_path) if sizes_path.exists() else None
            ),
            "phase": "baseline",
        }
    )
    write_state(state_path, state)
    return state


def mark_first_score(
    state_path: Path, address: str, now: datetime | None = None
) -> str:
    if not state_path.exists():
        return "inactive"
    state = read_state(state_path)
    if state.get("phase") == "finalizing":
        raise ValueError("campaign finalization is pending. Retry campaigns record")
    if address not in state.get("addresses", []):
        return "unrelated"
    scored_addresses = state.get("scored_addresses", [])
    if not isinstance(scored_addresses, list):
        raise ValueError("active campaign has invalid scored addresses")
    if address in scored_addresses:
        return "already-recorded"
    if not state.get("baseline_at"):
        raise ValueError("attach the campaign baseline before recording a score")
    scored = now or utc_now()
    timestamp(scored)
    started = _parse_time(state.get("started_at"), "started_at")
    baseline = _parse_time(state.get("baseline_at"), "baseline_at")
    if scored < started or scored < baseline:
        raise ValueError("the first-score time is before the campaign baseline")
    events = state.get("target_events", [])
    if not isinstance(events, list):
        raise ValueError("active campaign has invalid target events")
    for event in events:
        if not isinstance(event, dict):
            raise ValueError("active campaign has invalid target events")
        if event.get("address") == address:
            added = _parse_time(event.get("added_at"), "target added_at")
            if scored < added:
                raise ValueError("the first-score time is before the target was added")
    score_events = state.get("score_events", [])
    if not isinstance(score_events, list):
        raise ValueError("active campaign has invalid score events")
    scored_at = timestamp(scored)
    scored_addresses = [*scored_addresses, address]
    score_events = [*score_events, {"address": address, "scored_at": scored_at}]
    state["scored_addresses"] = scored_addresses
    state["score_events"] = score_events
    if not state.get("first_score_at"):
        state["first_score_at"] = scored_at
        state["first_score_address"] = address
    state["phase"] = "scoring"
    write_state(state_path, state)
    return "recorded"


def mark_resource_score(state_path: Path, now: datetime | None = None) -> str:
    if not state_path.exists():
        return "inactive"
    state = read_state(state_path)
    _validate_campaign_targets(state)
    if state.get("mode") != "resource":
        return "unrelated"
    if state.get("first_score_at"):
        return "already-recorded"
    if not state.get("baseline_at"):
        raise ValueError("attach the campaign baseline before recording a score")
    scored = now or utc_now()
    started = _parse_time(state.get("started_at"), "started_at")
    baseline = _parse_time(state.get("baseline_at"), "baseline_at")
    if scored < started or scored < baseline:
        raise ValueError("the first-score time is before the campaign baseline")
    state["first_score_at"] = timestamp(scored)
    state["resource_score_at"] = state["first_score_at"]
    state["phase"] = "scoring"
    write_state(state_path, state)
    return "recorded"


def add_target(
    state_path: Path,
    address: str,
    now: datetime | None = None,
    map_path: Path | None = None,
    sizes_path: Path | None = None,
    source_root: Path | None = None,
) -> dict[str, object]:
    state = read_state(state_path)
    if state.get("phase") == "finalizing":
        raise ValueError("campaign finalization is pending. Retry campaigns record")
    if state.get("mode") in ("meta", "resource"):
        raise ValueError(f"a {state.get('mode')} campaign cannot add a source target")
    if not state.get("baseline_at"):
        raise ValueError("attach the campaign baseline before adding a target")
    addresses = state.get("addresses")
    if not isinstance(addresses, list):
        raise ValueError("active campaign has invalid addresses")
    if address in addresses:
        raise ValueError(f"the active campaign already contains {address}")
    events = state.get("target_events", [])
    if not isinstance(events, list):
        raise ValueError("active campaign has invalid target events")
    family = state.get("family") is True
    if not family and len(addresses) >= 3:
        raise ValueError("a campaign can have at most three targets")
    if family and not state.get("first_score_at"):
        raise ValueError(
            "a family campaign needs a first-score anchor before it adds a target"
        )
    if state.get("mode") == "data" and len(addresses) >= 3:
        raise ValueError("a data campaign can have at most three targets")
    added = now or utc_now()
    timestamp(added)
    started = _parse_time(state.get("started_at"), "started_at")
    baseline = _parse_time(state.get("baseline_at"), "baseline_at")
    if added < started or added < baseline:
        raise ValueError("the target add time is before the campaign baseline")
    for event in events:
        if not isinstance(event, dict):
            raise ValueError("active campaign has invalid target events")
        if added < _parse_time(event.get("added_at"), "target added_at"):
            raise ValueError("the target add time is before an earlier pivot")
    if family:
        family_snapshot = state.get("analyzed_function_sizes")
        if (
            not isinstance(family_snapshot, dict)
            or _size_snapshot_hash(family_snapshot)
            != state.get("analyzed_function_sizes_sha256")
        ):
            raise ValueError("the active campaign family-size snapshot is invalid")
        validate_family_size_values(
            [*(str(item) for item in addresses), address],
            _decode_size_snapshot(family_snapshot),
        )
    if map_path is not None and sizes_path is not None:
        validate_source_target(
            address,
            str(state.get("mode", "")),
            map_path,
            sizes_path,
            source_root or ROOT / "src",
        )
    addresses = [*addresses, address]
    events = [*events, {"address": address, "added_at": timestamp(added)}]
    state["addresses"] = addresses
    state["target_events"] = events
    write_state(state_path, state)
    return state


def _parse_time(value: object, field: str) -> datetime:
    try:
        parsed = datetime.fromisoformat(str(value))
    except (TypeError, ValueError) as error:
        raise ValueError(f"active campaign has an invalid {field}") from error
    if parsed.tzinfo is None:
        raise ValueError(f"active campaign {field} needs a UTC offset")
    return parsed.astimezone(timezone.utc)


def _assert_close(name: str, supplied: float | int | None, measured: float) -> None:
    if supplied is None:
        return
    if not math.isclose(float(supplied), measured, rel_tol=1e-9, abs_tol=0.01):
        raise ValueError(
            f"{name} disagrees with the reports: caller={float(supplied):.6f}, "
            f"measured={measured:.6f}"
        )


def _single_line(value: str, field: str) -> str:
    text = " ".join(value.split()).strip()
    if not text:
        raise ValueError(f"{field} cannot be empty")
    return text


def append_source_models(
    path: Path,
    campaign_id: str,
    ended_at: str,
    subsystem: str,
    mode: str,
    addresses: list[str],
    resource: str | None,
    models: list[str],
    note: str,
) -> None:
    marker = f"<!-- campaign-id: {campaign_id} -->"
    existing = path.read_text(encoding="utf-8") if path.exists() else "# Source models\n"
    if marker in existing:
        return
    target_text = ", ".join(addresses) or resource or "-"
    heading = subsystem or "Unspecified subsystem"
    lines = [
        marker,
        f"## {ended_at[:10]} | {heading} | {target_text}",
        "",
        f"- Mode: {mode}.",
    ]
    lines.extend(f"- Ruled out: {_single_line(model, '--model')}" for model in models)
    if note.strip():
        lines.append(f"- Result note: {_single_line(note, '--note')}")
    block = "\n".join(lines) + "\n"
    if existing and not existing.endswith("\n"):
        existing += "\n"
    if not existing.endswith("\n\n"):
        existing += "\n"
    write_text(path, existing + block)


def _campaign_id(state: dict[str, object]) -> str:
    value = state.get("campaign_id")
    if isinstance(value, str) and value:
        return value
    seed = json.dumps(
        {
            "started_at": state.get("started_at"),
            "mode": state.get("mode"),
            "addresses": state.get("addresses"),
            "resource": state.get("resource"),
        },
        sort_keys=True,
    )
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"toy2-decomp:{seed}"))


def _validate_result_mode(mode: str, result: str) -> None:
    if mode == "meta" and result != "meta-fix":
        raise ValueError("a meta campaign must use --result meta-fix")
    if mode != "meta" and result == "meta-fix":
        raise ValueError("--result meta-fix requires an active meta campaign")


def _validate_timeline(
    state: dict[str, object], ended: datetime
) -> tuple[datetime, datetime, datetime | None]:
    _validate_campaign_targets(state)
    started = _parse_time(state.get("started_at"), "started_at")
    if ended < started:
        raise ValueError("the campaign end time is before its start time")
    baseline_value = state.get("baseline_at")
    if not baseline_value:
        raise ValueError("the active campaign has no baseline time")
    baseline = _parse_time(baseline_value, "baseline_at")
    if baseline < started or baseline > ended:
        raise ValueError("the baseline time is outside the campaign interval")

    first_score: datetime | None = None
    first_score_value = state.get("first_score_at")
    if first_score_value:
        first_score = _parse_time(first_score_value, "first_score_at")
        if first_score < started or first_score < baseline or first_score > ended:
            raise ValueError("the first-score time is outside the campaign interval")

    previous = baseline
    events = state.get("target_events", [])
    if not isinstance(events, list):
        raise ValueError("active campaign has invalid target events")
    if len(events) > 2 and state.get("family") is not True:
        raise ValueError("active campaign has too many pivot events")
    addresses = state.get("addresses", [])
    if not isinstance(addresses, list) or len(addresses) != len(set(addresses)):
        raise ValueError("active campaign has invalid addresses")
    if state.get("family") is not True and len(addresses) > 3:
        raise ValueError("a campaign can have at most three targets")
    if state.get("mode") == "data" and len(addresses) > 3:
        raise ValueError("a data campaign can have at most three targets")
    added_times = {str(addresses[0]): started} if addresses else {}
    seen: set[object] = set()
    for event in events:
        if not isinstance(event, dict) or event.get("address") not in addresses:
            raise ValueError("active campaign has invalid target events")
        address = event.get("address")
        if address in seen:
            raise ValueError("active campaign has duplicate target events")
        seen.add(address)
        added = _parse_time(event.get("added_at"), "target added_at")
        if added < started or added < previous or added > ended:
            raise ValueError("a target add time is outside the campaign interval")
        if state.get("family") is True and (
            first_score is None or added < first_score
        ):
            raise ValueError(
                "a family campaign needs a first-score anchor before it adds a target"
            )
        added_times[str(address)] = added
        previous = added

    scored_addresses = state.get("scored_addresses", [])
    score_events = state.get("score_events", [])
    if not isinstance(scored_addresses, list) or not isinstance(score_events, list):
        raise ValueError("active campaign has invalid score records")
    if len(scored_addresses) != len(set(scored_addresses)):
        raise ValueError("active campaign has duplicate scored addresses")
    if set(scored_addresses) - set(addresses):
        raise ValueError("a scored target is not in the active campaign")
    if len(score_events) != len(scored_addresses):
        raise ValueError("active campaign has incomplete score records")
    previous_score = baseline
    seen_scores: set[object] = set()
    for event in score_events:
        if not isinstance(event, dict) or event.get("address") not in addresses:
            raise ValueError("active campaign has invalid score events")
        address = event.get("address")
        if address in seen_scores:
            raise ValueError("active campaign has duplicate score events")
        seen_scores.add(address)
        scored_at = _parse_time(event.get("scored_at"), "target scored_at")
        if scored_at < previous_score or scored_at > ended:
            raise ValueError("a target score time is outside the campaign interval")
        if scored_at < added_times.get(str(address), started):
            raise ValueError("a target score time is before the target was added")
        previous_score = scored_at
    if seen_scores != set(scored_addresses):
        raise ValueError("active campaign scored addresses disagree with score events")

    if state.get("mode") == "resource":
        resource_score_value = state.get("resource_score_at")
        if first_score_value and resource_score_value != first_score_value:
            raise ValueError("the resource score disagrees with the first-score time")
        if resource_score_value and not first_score_value:
            raise ValueError("the active campaign has a resource score without a first score")
        return started, baseline, first_score

    if first_score_value:
        scored_address = state.get("first_score_address")
        if scored_address is not None and scored_address not in addresses:
            raise ValueError("the first-score target is not in the active campaign")
        for event in events:
            if event.get("address") == scored_address:
                added = _parse_time(event.get("added_at"), "target added_at")
                if first_score < added:
                    raise ValueError("the first-score time is before the target was added")
        if not score_events:
            raise ValueError("the active campaign has no first-score event")
        if (
            score_events[0].get("address") != scored_address
            or _parse_time(score_events[0].get("scored_at"), "target scored_at")
            != first_score
        ):
            raise ValueError("the first-score fields disagree with the score events")
    elif score_events:
        raise ValueError("the active campaign has score events without a first score")
    return started, baseline, first_score


def _validate_no_source_tree(state: dict[str, object]) -> None:
    saved_worktree = state.get("source_worktree")
    saved_index = state.get("source_index")
    saved_repository = state.get("repository_worktree")
    saved_repository_index = state.get("repository_index")
    if (
        not isinstance(saved_worktree, dict)
        or not isinstance(saved_index, dict)
        or not isinstance(saved_repository, dict)
        or not isinstance(saved_repository_index, dict)
    ):
        raise ValueError(
            "the active campaign has no repository fingerprint. Abort it and start again"
        )
    if _snapshot_hash(saved_worktree) != state.get("source_worktree_sha256"):
        raise ValueError("the active campaign source fingerprint is invalid")
    if _snapshot_hash(saved_index) != state.get("source_index_sha256"):
        raise ValueError("the active campaign index fingerprint is invalid")
    if _snapshot_hash(saved_repository) != state.get(
        "repository_worktree_sha256"
    ):
        raise ValueError("the active campaign repository fingerprint is invalid")
    if _snapshot_hash(saved_repository_index) != state.get(
        "repository_index_sha256"
    ):
        raise ValueError("the active campaign repository index fingerprint is invalid")
    root_value = state.get("source_worktree_root")
    if not isinstance(root_value, str) or not root_value:
        raise ValueError("the active campaign has no source root")
    root = Path(root_value)
    current_worktree = source_worktree_snapshot(root)
    current_index = source_index_snapshot(root)
    current_repository = repository_worktree_snapshot(root)
    current_repository_index = repository_index_snapshot(root)
    changes = source_worktree_changes(saved_worktree, current_worktree)
    changes.extend(
        f"{path} (index)"
        for path in source_worktree_changes(saved_index, current_index)
    )
    changes.extend(
        f"{path} (repository)"
        for path in source_worktree_changes(saved_repository, current_repository)
    )
    changes.extend(
        f"{path} (repository index)"
        for path in source_worktree_changes(
            saved_repository_index, current_repository_index
        )
    )
    if changes:
        summary = ", ".join(sorted(set(changes))[:5])
        if len(set(changes)) > 5:
            summary += ", ..."
        raise ValueError(
            "--result no-source requires the campaign source and index changes "
            f"to be restored, including tracked repository files: {summary}"
        )


def _target_deltas(
    addresses: list[str],
    baseline_report: Path,
    current_report: Path,
    baseline_data_report: Path,
    current_data_report: Path,
    sizes: dict[int, int],
) -> dict[str, dict[str, float]]:
    code_before = effective_code_by_address(baseline_report, sizes)
    code_after = effective_code_by_address(current_report, sizes)
    data_before = initialized_data_by_address(baseline_data_report)
    data_after = initialized_data_by_address(current_data_report)
    result: dict[str, dict[str, float]] = {}
    for address_text in addresses:
        address = int(address_text, 16)
        result[address_text] = {
            "effective_bytes": code_after.get(address, 0.0)
            - code_before.get(address, 0.0),
            "initialized_bytes": data_after.get(address, 0.0)
            - data_before.get(address, 0.0),
        }
    return result


def _finish_campaign_finalization(
    state_path: Path, state: dict[str, object]
) -> dict[str, object]:
    finalization = state.get("finalization")
    if not isinstance(finalization, dict):
        raise ValueError("active campaign has invalid finalization state")
    item = finalization.get("item")
    if not isinstance(item, dict):
        raise ValueError("active campaign has no pending campaign record")
    _validate_campaign_targets(item)
    ledger_value = finalization.get("ledger_path")
    models_value = finalization.get("source_models_path")
    if not isinstance(ledger_value, str) or not isinstance(models_value, str):
        raise ValueError("active campaign has invalid finalization paths")
    try:
        if item.get("result") == "no-source":
            append_source_models(
                Path(models_value),
                str(item["campaign_id"]),
                str(item["ended_at"]),
                str(item.get("subsystem", "")),
                str(item.get("mode", "")),
                list(item.get("addresses", [])),
                str(item["resource"]) if item.get("resource") else None,
                list(item.get("ruled_out_models", [])),
                str(item.get("note", "")),
            )
        append_record(Path(ledger_value), item)
        state_path.unlink()
    except Exception as error:
        raise ValueError(
            "campaign finalization is incomplete. Retry the same campaigns record command: "
            f"{error}"
        ) from error
    return item


def record_campaign(
    ledger_path: Path,
    state_path: Path,
    source_models_path: Path,
    current_report: Path,
    current_data_report: Path,
    result: str,
    commit: str | None = None,
    note: str | None = None,
    models: list[str] | None = None,
    supplied_mode: str | None = None,
    supplied_addresses: list[str] | None = None,
    supplied_resources: list[tuple[int, int, int]] | None = None,
    supplied_minutes: float | None = None,
    supplied_effective_bytes: float | None = None,
    supplied_initialized_bytes: float | None = None,
    now: datetime | None = None,
) -> dict[str, object]:
    state = read_state(state_path)
    pending = state.get("finalization")
    if state.get("phase") == "finalizing" or pending is not None:
        if not isinstance(pending, dict) or not isinstance(pending.get("item"), dict):
            raise ValueError("active campaign has invalid finalization state")
        pending_item = pending["item"]
        if pending_item.get("result") != result:
            raise ValueError("--result disagrees with the pending campaign finalization")
        if supplied_mode is not None and pending_item.get("mode") != supplied_mode:
            raise ValueError("--mode disagrees with the pending campaign finalization")
        if supplied_addresses and pending_item.get("addresses") != supplied_addresses:
            raise ValueError("--address disagrees with the pending campaign finalization")
        if supplied_resources:
            if len(supplied_resources) != 1:
                raise ValueError("--resource disagrees with the pending campaign finalization")
            supplied_resource = format_resource(supplied_resources[0])
            if pending_item.get("resource") != supplied_resource:
                raise ValueError("--resource disagrees with the pending campaign finalization")
        if commit is not None and pending_item.get("commit", "") != commit:
            raise ValueError("--commit disagrees with the pending campaign finalization")
        if note is not None:
            clean_note = _single_line(note, "--note") if note.strip() else ""
            if pending_item.get("note", "") != clean_note:
                raise ValueError("--note disagrees with the pending campaign finalization")
        if models is not None:
            clean_models = [_single_line(model, "--model") for model in models]
            if pending_item.get("ruled_out_models", []) != clean_models:
                raise ValueError(
                    "--model disagrees with the pending campaign finalization"
                )
        _assert_close(
            "--minutes", supplied_minutes, float(pending_item.get("minutes", 0.0))
        )
        _assert_close(
            "--effective-bytes",
            supplied_effective_bytes,
            float(pending_item.get("effective_bytes", 0.0)),
        )
        _assert_close(
            "--initialized-bytes",
            supplied_initialized_bytes,
            float(pending_item.get("initialized_bytes", 0.0)),
        )
        return _finish_campaign_finalization(state_path, state)

    mode = str(state.get("mode", ""))
    _validate_campaign_targets(state)
    addresses = state.get("addresses", [])
    if not isinstance(addresses, list) or not all(isinstance(item, str) for item in addresses):
        raise ValueError("active campaign has invalid addresses")
    if len(addresses) != len(set(addresses)):
        raise ValueError("active campaign has duplicate addresses")
    if mode == "data" and len(addresses) > 3:
        raise ValueError("a data campaign can have at most three targets")
    _validate_result_mode(mode, result)
    if supplied_mode is not None and supplied_mode != mode:
        raise ValueError(f"--mode disagrees with the active campaign: {supplied_mode} != {mode}")
    if supplied_addresses and supplied_addresses != addresses:
        raise ValueError("--address disagrees with the active campaign")
    if supplied_resources:
        if len(supplied_resources) != 1 or state.get("resource") != format_resource(
            supplied_resources[0]
        ):
            raise ValueError("--resource disagrees with the active campaign")
    models = [_single_line(model, "--model") for model in (models or [])]
    if result == "no-source" and not models:
        raise ValueError("a no-source campaign needs at least one --model")
    if result == "no-source":
        _validate_no_source_tree(state)
    baseline_value = state.get("baseline_report")
    baseline_data_value = state.get("baseline_data_report")
    if not baseline_value or not baseline_data_value:
        raise ValueError("the active campaign has no attached baseline reports")
    baseline_report = Path(str(baseline_value))
    baseline_data_report = Path(str(baseline_data_value))
    if file_hash(baseline_report) != state.get("baseline_report_sha256"):
        raise ValueError("the baseline code report changed after campaign start")
    if file_hash(baseline_data_report) != state.get("baseline_data_report_sha256"):
        raise ValueError("the baseline typed-data report changed after campaign start")

    snapshot = state.get("function_sizes")
    if not isinstance(snapshot, dict) or _size_snapshot_hash(snapshot) != state.get(
        "function_size_snapshot_sha256"
    ):
        raise ValueError("the active campaign function-size snapshot is invalid")
    sizes = _decode_size_snapshot(snapshot)
    if state.get("family") is True:
        family_snapshot = state.get("analyzed_function_sizes")
        if (
            not isinstance(family_snapshot, dict)
            or _size_snapshot_hash(family_snapshot)
            != state.get("analyzed_function_sizes_sha256")
        ):
            raise ValueError("the active campaign family-size snapshot is invalid")
        validate_family_size_values(
            addresses, _decode_size_snapshot(family_snapshot)
        )
    effective_before = effective_code_bytes(baseline_report, sizes)
    saved_effective_before = state.get("effective_bytes_before")
    if not isinstance(saved_effective_before, (int, float)) or not math.isclose(
        float(saved_effective_before), effective_before, rel_tol=1e-12, abs_tol=1e-9
    ):
        raise ValueError("the saved baseline effective-byte count is invalid")
    initialized_before = initialized_data_bytes(baseline_data_report)
    saved_initialized_before = state.get("initialized_bytes_before")
    if not isinstance(saved_initialized_before, (int, float)) or not math.isclose(
        initialized_before,
        float(saved_initialized_before),
        rel_tol=1e-12,
        abs_tol=1e-9,
    ):
        raise ValueError("the saved baseline initialized-byte count is invalid")
    effective_after = effective_code_bytes(current_report, sizes)
    initialized_after = initialized_data_bytes(current_data_report)
    effective_delta = effective_after - effective_before
    initialized_delta = initialized_after - initialized_before
    resource_after = None
    resource_delta = 0
    if mode == "resource":
        resource = parse_resource(str(state["resource"]))
        root = Path(str(state.get("source_worktree_root", ROOT)))
        resource_after = selected_evidence(
            resource_rows(
                root / "original" / "toy2.exe", root / "build" / "toy2.exe"
            ),
            resource,
        )
        resource_before = state.get("resource_before")
        if not isinstance(resource_before, dict):
            raise ValueError("the active resource campaign has no baseline leaf evidence")
        resource_delta = int(resource_after["explained_bytes"]) - int(
            resource_before.get("explained_bytes", 0)
        )
        if result == "source" and resource_delta <= 0:
            raise ValueError("the selected resource leaf did not improve explained bytes")
        baseline_sources = state.get("resource_sources")
        if (
            not isinstance(baseline_sources, dict)
            or _snapshot_hash(baseline_sources) != state.get("resource_sources_sha256")
        ):
            raise ValueError("the active campaign has no valid resource source fingerprint")
        staged_problems = staged_resource_source_problems(root, baseline_sources)
        if result == "source" and staged_problems:
            raise ValueError(staged_problems[0])
    if result == "no-source" and (
        not math.isclose(effective_delta, 0.0, rel_tol=0.0, abs_tol=0.01)
        or not math.isclose(initialized_delta, 0.0, rel_tol=0.0, abs_tol=0.01)
        or not math.isclose(resource_delta, 0.0, rel_tol=0.0, abs_tol=0.01)
    ):
        raise ValueError("--result no-source disagrees with the report deltas")
    _assert_close("--effective-bytes", supplied_effective_bytes, effective_delta)
    _assert_close("--initialized-bytes", supplied_initialized_bytes, initialized_delta)

    ended = now or utc_now()
    timestamp(ended)
    started, _, first_score = _validate_timeline(state, ended)
    scored_addresses = state.get("scored_addresses", [])
    if state.get("family") is True and set(scored_addresses) != set(addresses):
        missing = sorted(set(addresses) - set(scored_addresses))
        raise ValueError(
            "a family campaign needs a comparison for every member: "
            + ", ".join(missing)
        )
    elapsed_minutes = (ended - started).total_seconds() / 60.0
    _assert_close("--minutes", supplied_minutes, elapsed_minutes)
    first_score_value = state.get("first_score_at")
    first_score_at = str(first_score_value) if first_score_value else None
    first_score_minutes = None
    post_first_score_minutes = None
    if first_score is not None:
        first_score_minutes = (first_score - started).total_seconds() / 60.0
        post_first_score_minutes = (ended - first_score).total_seconds() / 60.0
    if result == "source" and first_score is None:
        raise ValueError("a source campaign needs a successful first-score stamp")

    ended_at = timestamp(ended)
    campaign_id = _campaign_id(state)
    item: dict[str, object] = {
        "schema_version": 2,
        "record_type": "campaign",
        "campaign_id": campaign_id,
        "timestamp": ended_at,
        "started_at": state["started_at"],
        "first_score_at": first_score_at,
        "resource_score_at": state.get("resource_score_at"),
        "ended_at": ended_at,
        "first_score_minutes": first_score_minutes,
        "post_first_score_minutes": post_first_score_minutes,
        "mode": mode,
        "result": result,
        "addresses": addresses,
        "resource": state.get("resource"),
        "target_events": state.get("target_events", []),
        "scored_addresses": scored_addresses,
        "score_events": state.get("score_events", []),
        "target_deltas": _target_deltas(
            addresses,
            baseline_report,
            current_report,
            baseline_data_report,
            current_data_report,
            sizes,
        ),
        "resource_before": state.get("resource_before"),
        "resource_after": resource_after,
        "resource_explained_bytes": resource_delta,
        "subsystem": str(state.get("subsystem", "")),
        "family": state.get("family") is True,
        "minutes": elapsed_minutes,
        "effective_bytes": effective_delta,
        "initialized_bytes": initialized_delta,
        "effective_bytes_before": effective_before,
        "effective_bytes_after": effective_after,
        "initialized_bytes_before": initialized_before,
        "initialized_bytes_after": initialized_after,
        "function_size_snapshot_sha256": state["function_size_snapshot_sha256"],
        "functions_map_sha256": state.get("functions_map_sha256"),
        "function_sizes_file_sha256": state.get("function_sizes_file_sha256"),
        "baseline_report_sha256": file_hash(baseline_report),
        "current_report_sha256": file_hash(current_report),
        "baseline_data_report_sha256": file_hash(baseline_data_report),
        "current_data_report_sha256": file_hash(current_data_report),
        "measurement": "reports",
        "commit": commit or "",
        "note": _single_line(note, "--note") if note and note.strip() else "",
        "ruled_out_models": models,
        "expected_minutes": state.get("expected_minutes"),
        "expected_retained_bytes": state.get("expected_retained_bytes"),
        "deadlines": state.get("deadlines", {}),
    }
    state["campaign_id"] = campaign_id
    state["phase"] = "finalizing"
    state["finalization"] = {
        "item": item,
        "ledger_path": str(ledger_path.resolve()),
        "source_models_path": str(source_models_path.resolve()),
    }
    write_state(state_path, state)
    return _finish_campaign_finalization(state_path, state)


def record_evidence(
    ledger_path: Path,
    addresses: list[str],
    evidence_kind: str,
    note: str,
    now: datetime | None = None,
    evidence_id: str | None = None,
) -> dict[str, object]:
    if not addresses:
        raise ValueError("new evidence needs at least one --address")
    if len(addresses) != len(set(addresses)):
        raise ValueError("new evidence contains a duplicate address")
    clean_note = _single_line(note, "--note")
    for existing in _read_records(ledger_path):
        if (
            existing.get("record_type") == "evidence"
            and existing.get("addresses") == addresses
            and existing.get("evidence_kind") == evidence_kind
            and existing.get("note") == clean_note
        ):
            raise ValueError("this evidence event already exists")
    item: dict[str, object] = {
        "schema_version": 2,
        "record_type": "evidence",
        "evidence_id": evidence_id or str(uuid.uuid4()),
        "timestamp": timestamp(now or utc_now()),
        "addresses": addresses,
        "evidence_kind": evidence_kind,
        "note": clean_note,
    }
    append_record(ledger_path, item)
    return item


def abort_campaign(
    ledger_path: Path,
    state_path: Path,
    reason: str,
    now: datetime | None = None,
) -> dict[str, object]:
    state = read_state(state_path)
    if state.get("phase") == "finalizing" or state.get("finalization") is not None:
        raise ValueError("campaign finalization is pending. Retry campaigns record")
    pending_abort = state.get("abort_finalization")
    if state.get("phase") == "aborting" or pending_abort is not None:
        if not isinstance(pending_abort, dict) or not isinstance(
            pending_abort.get("item"), dict
        ):
            raise ValueError("active campaign has invalid abort state")
        item = pending_abort["item"]
        saved_ledger = pending_abort.get("ledger_path")
        if not isinstance(saved_ledger, str):
            raise ValueError("active campaign abort has no saved ledger path")
        if str(ledger_path.resolve()) != saved_ledger:
            raise ValueError("--file disagrees with the pending campaign abort")
        if _single_line(reason, "--reason") != item.get("reason"):
            raise ValueError("--reason disagrees with the pending campaign abort")
        try:
            append_record(Path(saved_ledger), item)
            state_path.unlink()
        except Exception as error:
            raise ValueError(
                "campaign abort is incomplete. Retry the same campaigns abort command: "
                f"{error}"
            ) from error
        return item
    ended = now or utc_now()
    timestamp(ended)
    started = _parse_time(state.get("started_at"), "started_at")
    if ended < started:
        raise ValueError("the campaign abort time is before its start time")
    baseline_value = state.get("baseline_at")
    if baseline_value:
        _validate_timeline(state, ended)
    elif state.get("first_score_at") or state.get("target_events"):
        raise ValueError("the active campaign has events before its baseline")
    first_score_value = state.get("first_score_at")
    first_score_minutes = None
    if first_score_value:
        first_score = _parse_time(first_score_value, "first_score_at")
        if first_score < started or first_score > ended:
            raise ValueError("the first-score time is outside the campaign interval")
        first_score_minutes = (first_score - started).total_seconds() / 60.0
    clean_reason = _single_line(reason, "--reason")
    item: dict[str, object] = {
        "schema_version": 2,
        "record_type": "abort",
        "campaign_id": _campaign_id(state),
        "abort_id": f"abort:{_campaign_id(state)}",
        "timestamp": timestamp(ended),
        "started_at": state["started_at"],
        "ended_at": timestamp(ended),
        "first_score_at": first_score_value,
        "first_score_minutes": first_score_minutes,
        "minutes": (ended - started).total_seconds() / 60.0,
        "mode": state.get("mode", ""),
        "addresses": state.get("addresses", []),
        "resource": state.get("resource"),
        "target_events": state.get("target_events", []),
        "subsystem": state.get("subsystem", ""),
        "reason": clean_reason,
    }
    state["campaign_id"] = item["campaign_id"]
    state["phase"] = "aborting"
    state["abort_finalization"] = {
        "item": item,
        "ledger_path": str(ledger_path.resolve()),
    }
    write_state(state_path, state)
    try:
        append_record(ledger_path, item)
        state_path.unlink()
    except Exception as error:
        raise ValueError(
            "campaign abort is incomplete. Retry the same campaigns abort command: "
            f"{error}"
        ) from error
    return item


def print_summary(records: list[dict[str, object]], limit: int) -> None:
    campaigns = _chronological_records([
        item for item in records if item.get("record_type", "campaign") == "campaign"
    ])
    selected = campaigns[-limit:] if limit else campaigns
    if not selected:
        print("No campaign records exist.")
        return
    measured = [
        item
        for item in selected
        if item.get("mode") != "meta" and float(item.get("minutes", 0.0) or 0.0) > 0
    ]
    effective = sum(float(item.get("effective_bytes", 0.0) or 0.0) for item in measured)
    initialized = sum(float(item.get("initialized_bytes", 0) or 0) for item in measured)
    resources = sum(
        float(item.get("resource_explained_bytes", 0) or 0) for item in measured
    )
    minutes = sum(float(item.get("minutes", 0.0) or 0.0) for item in measured)
    sources = sum(item.get("result") == "source" for item in measured)
    no_sources = sum(item.get("result") == "no-source" for item in measured)
    report_measured = sum(item.get("measurement") == "reports" for item in measured)
    print(f"Records: {len(selected)}")
    print(f"Timed campaigns: {len(measured)} ({sources} source, {no_sources} no-source)")
    print(f"Report-derived campaigns: {report_measured}")
    print(f"Elapsed: {minutes:.1f} minutes")
    print(f"Effective code: {effective:+.2f} bytes")
    print(f"Initialized data: {initialized:+.2f} bytes")
    print(f"Resources: {resources:+.2f} bytes")
    if minutes > 0:
        print(
            f"Retained rate: {(effective + initialized + resources) / minutes:.2f} bytes/minute"
        )
    first_scores = [
        float(item["first_score_minutes"])
        for item in measured
        if isinstance(item.get("first_score_minutes"), (int, float))
    ]
    if first_scores:
        print(f"Mean time to first score: {sum(first_scores) / len(first_scores):.1f} minutes")
    print("Recent results:")
    for item in selected:
        addresses = ",".join(str(value) for value in item.get("addresses", [])) or "-"
        retained = (
            float(item.get("effective_bytes", 0.0) or 0.0)
            + float(item.get("initialized_bytes", 0) or 0)
            + float(item.get("resource_explained_bytes", 0) or 0)
        )
        record_kind = "reports" if item.get("measurement") == "reports" else "legacy"
        print(
            f"  {item.get('mode', '-'):<10} {item.get('result', '-'):<9} "
            f"{float(item.get('minutes', 0.0) or 0.0):>5.1f} min  "
            f"{retained:>8.2f} bytes  {record_kind:<7} {addresses}"
        )


def print_status(state_path: Path, now: datetime | None = None) -> None:
    if not state_path.exists():
        print("No active campaign exists.")
        return
    state = read_state(state_path)
    current = now or utc_now()
    timestamp(current)
    started = _parse_time(state.get("started_at"), "started_at")
    if current < started:
        raise ValueError("the status time is before the campaign start time")
    addresses = state.get("addresses", [])
    if not isinstance(addresses, list):
        raise ValueError("active campaign has invalid addresses")
    print(f"Campaign: {state.get('campaign_id', '-')}")
    print(f"Mode: {state.get('mode', '-')}")
    print(f"Phase: {state.get('phase', 'started')}")
    print(f"Family: {'yes' if state.get('family') is True else 'no'}")
    print(f"Targets: {', '.join(str(value) for value in addresses) or '-'}")
    print(f"Resource: {state.get('resource') or '-'}")
    scored_addresses = state.get("scored_addresses", [])
    if not isinstance(scored_addresses, list):
        scored_addresses = []
    print(
        "Compared targets: "
        + (", ".join(str(value) for value in scored_addresses) or "-")
    )
    print(f"Started: {state.get('started_at')}")
    print(f"Elapsed: {(current - started).total_seconds() / 60.0:.2f} minutes")
    expected_minutes = state.get("expected_minutes")
    if isinstance(expected_minutes, (int, float)):
        print(f"Expected duration: {float(expected_minutes):.2f} minutes")
    else:
        print("Expected duration: not supplied")
    expected_retained = state.get("expected_retained_bytes")
    if isinstance(expected_retained, (int, float)):
        print(f"Expected retained bytes: {float(expected_retained):.2f}")
    else:
        print("Expected retained bytes: not supplied")
    deadlines = state.get("deadlines")
    if not isinstance(deadlines, dict):
        deadlines = _campaign_deadlines(started, None)
    for name in ("preflight", "first_score", "stop", "extension"):
        label = name.replace("_", "-").capitalize()
        value = deadlines.get(f"{name}_deadline")
        print(f"{label} deadline: {value or 'not available'}")
    print(f"First score: {state.get('first_score_at') or 'not recorded'}")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Measure and summarize campaign throughput.")
    parser.add_argument("--file", type=Path, default=DEFAULT_LEDGER, help=argparse.SUPPRESS)
    parser.add_argument("--state-file", type=Path, default=DEFAULT_STATE, help=argparse.SUPPRESS)
    parser.add_argument(
        "--source-models", type=Path, default=DEFAULT_SOURCE_MODELS, help=argparse.SUPPRESS
    )
    parser.add_argument(
        "--functions-map", type=Path, default=DEFAULT_FUNCTION_MAP, help=argparse.SUPPRESS
    )
    parser.add_argument(
        "--function-sizes", type=Path, default=DEFAULT_FUNCTION_SIZES, help=argparse.SUPPRESS
    )
    parser.add_argument("--worktree-root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    parser.add_argument("--source-root", type=Path, default=ROOT / "src", help=argparse.SUPPRESS)
    subparsers = parser.add_subparsers(dest="command", required=True)

    start = subparsers.add_parser("start", help="start a measured campaign")
    start.add_argument(
        "--mode", required=True,
        choices=("coverage", "refinement", "data", "resource", "meta"),
    )
    start.add_argument("--address", action="append", default=[], type=parse_address)
    start.add_argument("--resource", action="append", default=[], type=parse_resource)
    start.add_argument("--subsystem", default="")
    start.add_argument("--family", action="store_true")
    start.add_argument("--expected-minutes", type=float)
    start.add_argument("--expected-retained-bytes", type=float)

    baseline = subparsers.add_parser(
        "set-baseline", help="attach reports after the wrapper saves a baseline"
    )
    baseline.add_argument("--report", type=Path, default=DEFAULT_BASELINE_REPORT)
    baseline.add_argument("--data-report", type=Path, default=DEFAULT_BASELINE_DATA_REPORT)
    baseline.add_argument("--quiet", action="store_true")

    first_score = subparsers.add_parser("first-score", help="stamp the first compiled score")
    first_score.add_argument("--address", required=True, type=parse_address)
    first_score.add_argument("--quiet", action="store_true")

    resource_score = subparsers.add_parser(
        "resource-score", help="stamp the first resource comparison"
    )
    resource_score.add_argument("--quiet", action="store_true")

    add_target_parser = subparsers.add_parser(
        "add-target", help="add a campaign target or pivot"
    )
    add_target_parser.add_argument("--address", required=True, type=parse_address)

    record = subparsers.add_parser("record", help="record one completed measured campaign")
    record.add_argument("--result", required=True, choices=("source", "no-source", "meta-fix"))
    record.add_argument(
        "--mode",
        choices=("coverage", "refinement", "data", "resource", "meta"),
        help="optional assertion against the active campaign",
    )
    record.add_argument(
        "--address",
        action="append",
        default=[],
        type=parse_address,
        help="optional assertion against the active targets",
    )
    record.add_argument(
        "--resource", action="append", default=[], type=parse_resource,
        help="optional assertion against the active resource target",
    )
    record.add_argument(
        "--minutes", type=float, help="optional assertion against measured minutes"
    )
    record.add_argument(
        "--effective-bytes",
        type=float,
        help="optional assertion against the code reports",
    )
    record.add_argument(
        "--initialized-bytes",
        type=float,
        help="optional assertion against the typed-data reports",
    )
    record.add_argument(
        "--commit", help="legacy commit label for an imported workflow"
    )
    record.add_argument("--note")
    record.add_argument(
        "--model", action="append", help="a source model ruled out by no-source"
    )
    record.add_argument(
        "--current-report",
        type=Path,
        default=DEFAULT_CURRENT_REPORT,
        help=argparse.SUPPRESS,
    )
    record.add_argument(
        "--current-data-report",
        type=Path,
        default=DEFAULT_CURRENT_DATA_REPORT,
        help=argparse.SUPPRESS,
    )

    evidence = subparsers.add_parser("evidence", help="record new evidence for a target")
    evidence.add_argument("--address", action="append", required=True, type=parse_address)
    evidence.add_argument("--kind", required=True, choices=EVIDENCE_KINDS)
    evidence.add_argument("--note", required=True)

    abort = subparsers.add_parser("abort", help="stop a campaign and keep an audit record")
    abort.add_argument("--reason", required=True)
    subparsers.add_parser("migrate", help="import untracked legacy campaign history")
    subparsers.add_parser("status", help="show the active campaign and its deadlines")

    summary = subparsers.add_parser("summary", help="show recent throughput")
    summary.add_argument("--limit", type=int, default=10)
    return parser


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    try:
        if args.command == "migrate":
            imported = migrate_legacy_ledger(args.file)
            print(f"Imported {imported} legacy campaign record(s).")
            return 0
        if args.command == "start":
            state = start_campaign(
                args.state_file,
                args.mode,
                args.address,
                args.subsystem,
                utc_now(),
                worktree_root=args.worktree_root,
                map_path=args.functions_map,
                sizes_path=args.function_sizes,
                source_root=args.source_root,
                expected_minutes=args.expected_minutes,
                expected_retained_bytes=args.expected_retained_bytes,
                family=args.family,
                resources=args.resource,
            )
            print(f"Started {state['mode']} campaign at {state['started_at']}.")
            return 0
        if args.command == "set-baseline":
            attach_baseline(
                args.state_file,
                args.report,
                args.data_report,
                args.functions_map,
                args.function_sizes,
                utc_now(),
                args.source_root,
            )
            if not args.quiet:
                print("Attached the baseline reports to the active campaign.")
            return 0
        if args.command == "first-score":
            status = mark_first_score(args.state_file, args.address, utc_now())
            if not args.quiet and status != "inactive":
                print(f"First-score stamp: {status}.")
            return 0
        if args.command == "resource-score":
            status = mark_resource_score(args.state_file, utc_now())
            if not args.quiet and status != "inactive":
                print(f"First resource-score stamp: {status}.")
            return 0
        if args.command == "add-target":
            state = add_target(
                args.state_file,
                args.address,
                utc_now(),
                args.functions_map,
                args.function_sizes,
                args.source_root,
            )
            print(
                f"Added {args.address} to the active {state['mode']} campaign."
            )
            return 0
        if args.command == "record":
            item = record_campaign(
                args.file,
                args.state_file,
                args.source_models,
                args.current_report,
                args.current_data_report,
                args.result,
                commit=args.commit,
                note=args.note,
                models=args.model,
                supplied_mode=args.mode,
                supplied_addresses=args.address,
                supplied_resources=args.resource,
                supplied_minutes=args.minutes,
                supplied_effective_bytes=args.effective_bytes,
                supplied_initialized_bytes=args.initialized_bytes,
                now=utc_now(),
            )
            print(
                f"Recorded {item['mode']} {item['result']} campaign: "
                f"{item['effective_bytes']:+.2f} code byte(s), "
                f"{item['initialized_bytes']:+.2f} data byte(s), "
                f"{item.get('resource_explained_bytes', 0):+.2f} resource byte(s), "
                f"{item['minutes']:.2f} minute(s)."
            )
            return 0
        if args.command == "evidence":
            record_evidence(args.file, args.address, args.kind, args.note, utc_now())
            print(f"Recorded new evidence for {len(args.address)} target(s).")
            return 0
        if args.command == "abort":
            item = abort_campaign(args.file, args.state_file, args.reason, utc_now())
            print(
                f"Aborted {item['mode']} campaign after {item['minutes']:.2f} minute(s)."
            )
            return 0
        if args.command == "status":
            print_status(args.state_file)
            return 0
        if args.limit < 0:
            parser.error("--limit must be zero or greater")
        print_summary(read_records(args.file), args.limit)
        return 0
    except ValueError as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
