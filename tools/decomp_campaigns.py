#!/usr/bin/env python3
"""Record measured campaign results and summarize reconstruction throughput."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import time
import uuid
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Mapping

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
sys.path.insert(0, str(ROOT))
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
DEFAULT_FINALIZE_CACHE = ROOT / "build" / "decomp-cache" / "finalize"
SCHEMA_VERSION = 3
FINALIZE_RECEIPT_VERSION = 2
LEGACY_FINALIZE_RECEIPT_VERSION = 1
DELIVERY_RECEIPT_VERSION = 2
DOCTOR_RECEIPT_MAX_AGE = timedelta(minutes=60)
PREDICTION_HANDOFF_MAX_AGE = timedelta(minutes=60)
FINALIZE_STEPS = (
    "build",
    "code_report",
    "data_report",
    "source_scan",
    "validation",
)
PROGRESS_METRICS = (
    "implemented",
    "terminal",
    "terminal_bytes",
    "effective_bytes",
    "source_debt",
)
DELIVERY_STATUSES = (
    "staged",
    "accepted",
    "integrated",
    "committed",
    "pushed",
    "rejected",
)
DELIVERY_SEQUENCE = (
    "staged",
    "accepted",
    "integrated",
    "committed",
    "pushed",
)
DEFAULT_LANES = {
    "coverage": "research",
    "refinement": "production",
    "data": "data",
    "resource": "resource",
    "meta": "meta",
}
LANE_MODES = {
    "closure": {"refinement"},
    "production": {"refinement"},
    "research": {"coverage", "refinement"},
    "data": {"data"},
    "resource": {"resource"},
    "meta": {"meta"},
}

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

REPRODUCIBLE_GENERATED_SOURCES = {
    "build/CMakeFiles/toy2_resources",
    "build/CMakeFiles/toy2_resources.rule",
    "build/generated/resources/toy2.res",
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
    retry_credits: int = 0
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


def _clean_optional_line(value: str | None, field: str) -> str | None:
    if value is None:
        return None
    return _single_line(value, field)


def _campaign_lane(record: Mapping[str, object]) -> str:
    value = record.get("lane")
    if isinstance(value, str) and value.strip():
        return value.strip()
    mode = record.get("mode")
    return DEFAULT_LANES.get(str(mode).strip(), "legacy") if mode else "legacy"


def _validate_lane(mode: str, lane: str | None) -> str:
    value = _single_line(lane or DEFAULT_LANES.get(mode, mode), "--lane")
    allowed_modes = LANE_MODES.get(value)
    if allowed_modes is None:
        raise ValueError(f"unknown campaign lane: {value}")
    if mode not in allowed_modes:
        raise ValueError(f"the {value} lane cannot use {mode} mode")
    return value


def _normalize_progress_metrics(
    payload: Mapping[str, object] | None,
) -> dict[str, int | float | None]:
    aliases = {
        "implemented": ("implemented", "implemented_functions"),
        "terminal": ("terminal", "terminal_functions"),
        "terminal_bytes": ("terminal_bytes",),
        "effective_bytes": ("effective_bytes",),
        "source_debt": ("source_debt", "source_debt_functions"),
    }
    result: dict[str, int | float | None] = {
        name: None for name in PROGRESS_METRICS
    }
    if payload is None:
        return result
    for name, names in aliases.items():
        value = next((payload.get(key) for key in names if key in payload), None)
        if value is None:
            continue
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise ValueError(f"the {name.replace('_', '-')} metric must be a number")
        number = float(value)
        if not math.isfinite(number) or number < 0:
            raise ValueError(
                f"the {name.replace('_', '-')} metric must be finite and zero or greater"
            )
        result[name] = int(value) if name != "effective_bytes" else number
    return result


def _prediction_metadata(
    expected_minutes: float | None,
    expected_retained_bytes: float | None,
    *,
    version: str | None = None,
    lower_bound_retained_bytes: float | None = None,
    features: Mapping[str, object] | None = None,
) -> dict[str, object]:
    _validate_estimate(
        "--prediction-lower-bound-bytes",
        lower_bound_retained_bytes,
        allow_zero=True,
    )
    if (
        lower_bound_retained_bytes is not None
        and expected_retained_bytes is not None
        and lower_bound_retained_bytes > expected_retained_bytes
    ):
        raise ValueError(
            "the prediction lower bound cannot exceed the retained-byte estimate"
        )
    clean_version = _clean_optional_line(version, "--prediction-version")
    feature_values = dict(features or {})
    try:
        json.dumps(feature_values, sort_keys=True, allow_nan=False)
    except (TypeError, ValueError) as error:
        raise ValueError("prediction features must contain JSON values") from error
    return {
        "version": clean_version,
        "expected_minutes": expected_minutes,
        "expected_retained_bytes": expected_retained_bytes,
        "lower_bound_retained_bytes": lower_bound_retained_bytes,
        "features": feature_values,
    }


def _validate_prediction_handoff(
    lane: str,
    prediction: Mapping[str, object],
    *,
    required: bool,
    addresses: list[str] | None = None,
) -> None:
    """Require a complete selector forecast when a CLI start supplies estimates."""

    expected_minutes = prediction.get("expected_minutes")
    expected_bytes = prediction.get("expected_retained_bytes")
    lower_bytes = prediction.get("lower_bound_retained_bytes")
    version = prediction.get("version")
    features = prediction.get("features")
    supplied = any(
        value is not None
        for value in (expected_minutes, expected_bytes, lower_bytes, version)
    ) or bool(features)
    if not required:
        return
    if not supplied:
        raise ValueError("a campaign start needs a complete forecast handoff")
    if (
        isinstance(expected_minutes, bool)
        or not isinstance(expected_minutes, (int, float))
        or not math.isfinite(float(expected_minutes))
        or expected_minutes <= 0
    ):
        raise ValueError("a forecast handoff needs --expected-minutes")
    if (
        isinstance(expected_bytes, bool)
        or not isinstance(expected_bytes, (int, float))
        or not math.isfinite(float(expected_bytes))
        or expected_bytes < 0
    ):
        raise ValueError("a forecast handoff needs --expected-retained-bytes")
    if (
        isinstance(lower_bytes, bool)
        or not isinstance(lower_bytes, (int, float))
        or not math.isfinite(float(lower_bytes))
        or lower_bytes < 0
        or lower_bytes > expected_bytes
    ):
        raise ValueError("a forecast handoff needs --prediction-lower-bound-bytes")
    if not isinstance(version, str) or not version:
        raise ValueError("a forecast handoff needs --prediction-version")
    if not isinstance(features, dict) or not features:
        raise ValueError("a forecast handoff needs --prediction-features")
    try:
        json.dumps(features, sort_keys=True, allow_nan=False)
    except (TypeError, ValueError) as error:
        raise ValueError("prediction features must contain finite JSON values") from error
    target_addresses = addresses or []
    if len(target_addresses) > 1:
        per_target = features.get("per_target")
        if not isinstance(per_target, dict) or set(per_target) != set(
            target_addresses
        ):
            raise ValueError(
                "a multi-target forecast needs one per_target entry for each address"
            )
        target_medians: list[float] = []
        target_minutes: list[float] = []
        minutes_supplied: list[bool] = []
        for address in target_addresses:
            target = per_target.get(address)
            if not isinstance(target, dict):
                raise ValueError(f"the per_target forecast for {address} is invalid")
            median = target.get("median_retained_bytes")
            lower = target.get("lower_retained_bytes")
            if (
                isinstance(median, bool)
                or not isinstance(median, (int, float))
                or not math.isfinite(float(median))
                or float(median) < 0.0
                or isinstance(lower, bool)
                or not isinstance(lower, (int, float))
                or not math.isfinite(float(lower))
                or float(lower) < 0.0
                or float(lower) > float(median)
            ):
                raise ValueError(
                    f"the per_target byte forecast for {address} is invalid"
                )
            target_medians.append(float(median))
            target_minute = target.get("median_minutes")
            minutes_supplied.append(target_minute is not None)
            if target_minute is not None:
                if (
                    isinstance(target_minute, bool)
                    or not isinstance(target_minute, (int, float))
                    or not math.isfinite(float(target_minute))
                    or float(target_minute) <= 0.0
                ):
                    raise ValueError(
                        f"the per_target minute forecast for {address} is invalid"
                    )
                target_minutes.append(float(target_minute))
        if not math.isclose(
            sum(target_medians), float(expected_bytes), abs_tol=0.01
        ):
            raise ValueError(
                "the per_target median bytes must sum to --expected-retained-bytes"
            )
        if any(minutes_supplied) and not all(minutes_supplied):
            raise ValueError(
                "per_target median_minutes must be present for every address or none"
            )
        if target_minutes and not math.isclose(
            sum(target_minutes), float(expected_minutes), abs_tol=0.01
        ):
            raise ValueError(
                "the per_target median minutes must sum to --expected-minutes"
            )
    if lane not in ("closure", "production"):
        return
    required_features = (
        "success_probability",
        "cohort_sample_size",
        "median_retained_bytes",
        "median_minutes",
    )
    missing = [name for name in required_features if name not in features]
    if missing:
        raise ValueError(
            "prediction features are missing: " + ", ".join(missing)
        )
    success_probability = features.get("success_probability")
    cohort_sample_size = features.get("cohort_sample_size")
    median_bytes = features.get("median_retained_bytes")
    median_minutes = features.get("median_minutes")
    if (
        isinstance(success_probability, bool)
        or not isinstance(success_probability, (int, float))
        or not math.isfinite(float(success_probability))
        or not 0.0 <= float(success_probability) <= 1.0
    ):
        raise ValueError("the forecast success probability must be from zero through one")
    if (
        isinstance(cohort_sample_size, bool)
        or not isinstance(cohort_sample_size, int)
        or cohort_sample_size < 0
    ):
        raise ValueError("the forecast cohort sample count must be zero or greater")
    if (
        isinstance(median_bytes, bool)
        or not isinstance(median_bytes, (int, float))
        or not math.isfinite(float(median_bytes))
        or float(median_bytes) < 0.0
        or not math.isclose(float(median_bytes), float(expected_bytes), abs_tol=0.01)
    ):
        raise ValueError(
            "--expected-retained-bytes must equal the predicted median bytes"
        )
    if (
        isinstance(median_minutes, bool)
        or not isinstance(median_minutes, (int, float))
        or not math.isfinite(float(median_minutes))
        or float(median_minutes) <= 0.0
        or not math.isclose(float(median_minutes), float(expected_minutes), abs_tol=0.01)
    ):
        raise ValueError("--expected-minutes must equal the predicted median minutes")


def _address_prediction(
    state: Mapping[str, object], address: str
) -> tuple[Mapping[str, object] | None, float | None]:
    events = state.get("prediction_events")
    if isinstance(events, list):
        for event in reversed(events):
            if not isinstance(event, dict):
                continue
            addresses = event.get("addresses")
            prediction = event.get("prediction")
            if (
                isinstance(addresses, list)
                and address in addresses
                and isinstance(prediction, dict)
            ):
                value = prediction.get("expected_retained_bytes")
                features = prediction.get("features")
                per_target = (
                    features.get("per_target")
                    if isinstance(features, dict)
                    else None
                )
                target_prediction = (
                    per_target.get(address)
                    if isinstance(per_target, dict)
                    else None
                )
                if isinstance(target_prediction, dict):
                    median = target_prediction.get("median_retained_bytes")
                    lower = target_prediction.get("lower_retained_bytes")
                    if (
                        isinstance(median, (int, float))
                        and not isinstance(median, bool)
                        and math.isfinite(float(median))
                        and isinstance(lower, (int, float))
                        and not isinstance(lower, bool)
                        and math.isfinite(float(lower))
                    ):
                        selected = dict(prediction)
                        selected["expected_retained_bytes"] = float(median)
                        selected["lower_bound_retained_bytes"] = float(lower)
                        minutes = target_prediction.get("median_minutes")
                        if isinstance(minutes, (int, float)) and not isinstance(
                            minutes, bool
                        ):
                            selected["expected_minutes"] = float(minutes)
                        return selected, float(median)
                if (
                    addresses
                    and isinstance(value, (int, float))
                    and not isinstance(value, bool)
                    and math.isfinite(float(value))
                ):
                    return prediction, float(value) / len(addresses)
    prediction = state.get("prediction")
    addresses = state.get("addresses")
    value = (
        prediction.get("expected_retained_bytes")
        if isinstance(prediction, dict)
        else state.get("expected_retained_bytes")
    )
    expected = (
        float(value) / len(addresses)
        if isinstance(addresses, list)
        and addresses
        and isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(float(value))
        else None
    )
    return prediction if isinstance(prediction, dict) else None, expected


def _phase_timestamps(state: Mapping[str, object]) -> dict[str, str]:
    value = state.get("phase_timestamps")
    if value is None:
        result: dict[str, str] = {}
        for name, field in (
            ("started", "started_at"),
            ("baseline", "baseline_at"),
            ("first-score", "first_score_at"),
            ("ended", "ended_at"),
        ):
            timestamp_value = state.get(field)
            if isinstance(timestamp_value, str) and timestamp_value:
                result[name] = timestamp_value
        return result
    if not isinstance(value, dict) or not all(
        isinstance(name, str) and isinstance(item, str)
        for name, item in value.items()
    ):
        raise ValueError("active campaign has invalid phase timestamps")
    return dict(value)


def _stamp_phase(
    state: dict[str, object], name: str, when: datetime, *, replace: bool = False
) -> str:
    clean_name = _single_line(name, "phase name").replace(" ", "_")
    value = timestamp(when)
    phases = _phase_timestamps(state)
    if clean_name in phases and not replace:
        raise ValueError(f"the {clean_name.replace('_', '-')} phase already has a time")
    phases[clean_name] = value
    state["phase_timestamps"] = phases
    return value


def _require_mutable_campaign(state: Mapping[str, object]) -> None:
    phase = state.get("phase")
    if isinstance(phase, str) and (
        phase == "finalizing" or phase.startswith("finalize_")
    ) or state.get("finalization") is not None:
        raise ValueError("campaign finalization owns the active state")
    if phase == "finalized":
        raise ValueError("the finalized campaign is immutable. Record its result")


def _validate_sha256(value: str | None, field: str) -> str | None:
    if value is None:
        return None
    text = value.strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", text):
        raise ValueError(f"{field} must be a SHA-256 value")
    return text


def _validate_commit_id(value: str | None, field: str) -> str | None:
    clean = _clean_optional_line(value, field)
    if clean is None:
        return None
    if not re.fullmatch(r"[0-9a-fA-F]{7,64}", clean):
        raise ValueError(f"{field} must be a hexadecimal commit ID")
    return clean.lower()


def _read_doctor_receipt(
    path: Path | None,
    worktree_root: Path,
    lane: str,
    mode: str,
    addresses: list[str],
    resource: str | None,
    input_hashes: Mapping[str, object],
    started: datetime,
    runner: Callable[..., subprocess.CompletedProcess] | None = None,
) -> tuple[dict[str, str | None], dict[str, object] | None]:
    empty = {
        "selection_started_at": None,
        "doctor_started_at": None,
        "doctor_ended_at": None,
    }
    if path is None:
        return empty, None
    if not path.is_absolute():
        path = worktree_root / path
    path = path.resolve()
    try:
        from tools.decomp_doctor import (
            RECEIPT_DIRECTORY_RELATIVE,
            validate_doctor_receipt,
        )

        validation_arguments: dict[str, object] = {
            "root": worktree_root,
            "expected_input_hashes": input_hashes,
        }
        if runner is not None:
            validation_arguments["runner"] = runner
        receipt = validate_doctor_receipt(path, **validation_arguments)
    except ValueError as error:
        raise ValueError(f"the doctor receipt is invalid: {error}") from error
    checks = receipt.get("checks")
    if (
        receipt.get("schema") != 1
        or receipt.get("status") != "ready"
        or receipt.get("ok") is not True
        or receipt.get("campaign_timing_started") is not False
        or not isinstance(checks, list)
        or not checks
        or any(
            not isinstance(check, dict) or check.get("ok") is not True
            for check in checks
        )
    ):
        raise ValueError("the doctor receipt is not ready")
    saved_receipt_id = receipt.get("receipt_id")
    identity_payload = dict(receipt)
    identity_payload.pop("receipt_id", None)
    identity_payload.pop("receipt_path", None)
    computed_receipt_id = hashlib.sha256(
        json.dumps(
            identity_payload, sort_keys=True, separators=(",", ":")
        ).encode()
    ).hexdigest()
    if saved_receipt_id != computed_receipt_id:
        raise ValueError("the doctor receipt identity is invalid")
    expected_receipt_path = (
        RECEIPT_DIRECTORY_RELATIVE / f"{saved_receipt_id}.json"
    ).as_posix()
    if receipt.get("receipt_path") != expected_receipt_path:
        raise ValueError("the doctor receipt path is invalid")
    immutable_path = (worktree_root / expected_receipt_path).resolve()
    try:
        immutable_path.relative_to(worktree_root.resolve())
    except ValueError as error:
        raise ValueError("the doctor receipt path escaped the worktree") from error
    if not immutable_path.is_file() or file_hash(immutable_path) != file_hash(path):
        raise ValueError("the immutable doctor receipt does not match the input")
    receipt_lane = receipt.get("lane")
    if not isinstance(receipt_lane, str) or receipt_lane != lane:
        raise ValueError("the doctor receipt lane disagrees with --lane")
    if receipt.get("mode") != mode:
        raise ValueError("the doctor receipt mode disagrees with --mode")
    if receipt.get("addresses") != addresses:
        raise ValueError("the doctor receipt addresses disagree with --address")
    if receipt.get("resource") != resource:
        raise ValueError("the doctor receipt resource disagrees with --resource")
    receipt_inputs = receipt.get("input_hashes")
    if not isinstance(receipt_inputs, dict) or receipt_inputs != dict(input_hashes):
        raise ValueError("the doctor receipt input hashes are stale")
    times: dict[str, str | None] = {}
    previous: datetime | None = None
    for field in ("selection_started_at", "doctor_started_at", "doctor_ended_at"):
        value = receipt.get(field)
        if not isinstance(value, str) or not value:
            raise ValueError(f"the doctor receipt has no {field}")
        parsed = _parse_time(value, field)
        if previous is not None and parsed < previous:
            raise ValueError("the doctor receipt timestamps are out of order")
        if parsed > started:
            raise ValueError("the doctor receipt timestamp is after the handoff time")
        previous = parsed
        times[field] = timestamp(parsed)
    doctor_ended = _parse_time(times["doctor_ended_at"], "doctor_ended_at")
    if started - doctor_ended > DOCTOR_RECEIPT_MAX_AGE:
        raise ValueError("the doctor receipt is too old")
    identity = {
        "path": str(immutable_path),
        "sha256": file_hash(immutable_path),
        "receipt_id": receipt.get("receipt_id"),
        "head": receipt.get("head"),
        "lane": receipt_lane,
        "mode": mode,
        "addresses": addresses,
        "resource": resource,
        "input_hashes": dict(input_hashes),
        "status": "ready",
    }
    return times, identity


def _validate_state_briefs(state: Mapping[str, object]) -> None:
    """Reject a brief or doctor receipt that changed after campaign start."""

    briefs = state.get("briefs", [])
    if not isinstance(briefs, list) or any(
        not isinstance(item, dict) for item in briefs
    ):
        raise ValueError("the active campaign has invalid brief identities")
    if state.get("mode") == "meta":
        if briefs:
            raise ValueError("a meta campaign cannot have target briefs")
        return
    root_value = state.get("source_worktree_root")
    campaign_head = state.get("campaign_head")
    if isinstance(root_value, str) and isinstance(campaign_head, str):
        current_head = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=Path(root_value),
            check=False,
            capture_output=True,
            text=True,
        )
        if (
            current_head.returncode != 0
            or current_head.stdout.strip().lower() != campaign_head
        ):
            raise ValueError(
                "the campaign HEAD changed after preflight. Integrate before a new campaign"
            )
    doctor = state.get("doctor_receipt")
    if not isinstance(doctor, dict):
        raise ValueError("the active campaign has no doctor receipt identity")
    doctor_receipts = state.get("doctor_receipts")
    if doctor_receipts is None:
        doctor_receipts = [doctor]
    if (
        not isinstance(doctor_receipts, list)
        or not doctor_receipts
        or any(not isinstance(item, dict) for item in doctor_receipts)
        or doctor_receipts[0] != doctor
    ):
        raise ValueError("the active campaign has invalid doctor receipt identities")
    root = Path(root_value).resolve() if isinstance(root_value, str) else ROOT.resolve()
    valid_doctors: set[tuple[str, str, object]] = set()
    for receipt in doctor_receipts:
        doctor_path = receipt.get("path")
        doctor_hash = receipt.get("sha256")
        if (
            not isinstance(doctor_path, str)
            or not isinstance(doctor_hash, str)
            or file_hash(Path(doctor_path)) != doctor_hash
        ):
            raise ValueError("a doctor receipt changed after campaign start")
        valid_doctors.add((doctor_path, doctor_hash, receipt.get("receipt_id")))
        doctor_document = _read_json_object(Path(doctor_path), "doctor receipt")
        if doctor_document.get("mode") in {"coverage", "refinement", "data"}:
            try:
                from tools.decomp_doctor import (
                    data_artifact_descriptors,
                    load_ghidra_artifact,
                    source_artifact_descriptors,
                )

                addresses = doctor_document.get("addresses")
                if not isinstance(addresses, list) or any(
                    not isinstance(address, str) for address in addresses
                ):
                    raise ValueError("the doctor receipt has invalid source targets")
                for address in addresses:
                    descriptors = (
                        source_artifact_descriptors(doctor_document, address)
                        if doctor_document.get("mode") in {"coverage", "refinement"}
                        else data_artifact_descriptors(doctor_document, address)
                    )
                    for kind, descriptor in descriptors.items():
                        load_ghidra_artifact(root, address, kind, descriptor)
            except ValueError as error:
                raise ValueError(
                    f"a doctor evidence artifact changed after campaign start: {error}"
                ) from error
    expected_targets = (
        [state.get("resource")]
        if state.get("mode") == "resource"
        else state.get("addresses")
    )
    if state.get("briefs_required") is True and (
        not isinstance(expected_targets, list)
        or [item.get("target") for item in briefs] != expected_targets
    ):
        raise ValueError("the active campaign needs one brief for each ordered target")
    from tools.decomp_brief import TOOL_INPUTS, _combined_hash

    current_brief_tool_hash = _combined_hash(root, TOOL_INPUTS)
    for identity in briefs:
        path_value = identity.get("path")
        saved_file_hash = identity.get("sha256")
        saved_content_hash = identity.get("content_sha256")
        if (
            not isinstance(path_value, str)
            or not isinstance(saved_file_hash, str)
            or not isinstance(saved_content_hash, str)
        ):
            raise ValueError("the active campaign has an invalid brief identity")
        path = Path(path_value)
        if file_hash(path) != saved_file_hash:
            raise ValueError("a target brief changed after campaign start")
        document = _read_json_object(path, "target brief")
        content = dict(document)
        content.pop("content_sha256", None)
        computed_content_hash = hashlib.sha256(
            json.dumps(content, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
        if (
            document.get("content_sha256") != saved_content_hash
            or computed_content_hash != saved_content_hash
            or document.get("cache_key") != identity.get("cache_key")
            or document.get("lane") != identity.get("lane")
            or document.get("target") != identity.get("target")
        ):
            raise ValueError("a target brief content identity changed")
        inputs = document.get("inputs")
        if (
            not isinstance(inputs, dict)
            or inputs.get("tool_hash") != current_brief_tool_hash
        ):
            raise ValueError("a target brief tool input changed after campaign start")
        brief_dwarf = inputs.get("dwarf_input") if isinstance(inputs, dict) else None
        if brief_dwarf != identity.get("dwarf_input"):
            raise ValueError("a target brief DWARF identity changed")
        if not isinstance(brief_dwarf, dict):
            raise ValueError("a target brief has no DWARF input identity")
        dwarf_path = brief_dwarf.get("path")
        dwarf_hash = brief_dwarf.get("sha256")
        if not isinstance(dwarf_path, str) or (
            dwarf_hash is not None and not isinstance(dwarf_hash, str)
        ):
            raise ValueError("a target brief has an invalid DWARF input identity")
        _validate_optional_file_hash(
            Path(dwarf_path), dwarf_hash, "a target brief DWARF input"
        )
        brief_doctor = inputs.get("doctor_receipt") if isinstance(inputs, dict) else None
        if brief_doctor != identity.get("doctor_receipt"):
            raise ValueError("a target brief doctor identity changed")
        if not isinstance(brief_doctor, dict) or (
            brief_doctor.get("path"),
            brief_doctor.get("sha256"),
            brief_doctor.get("receipt_id"),
        ) not in valid_doctors:
            raise ValueError("a target brief belongs to another doctor receipt")
        brief_reports = inputs.get("scout_reports") if isinstance(inputs, dict) else None
        identity_reports = identity.get("scout_reports")
        if brief_reports != identity_reports:
            raise ValueError("a target brief scout report identity changed")
        if state.get("briefs_required") is True and (
            not isinstance(identity_reports, list) or len(identity_reports) != 2
        ):
            raise ValueError("a target brief needs two bound scout reports")
        for report in identity_reports or []:
            report_path = report.get("path") if isinstance(report, dict) else None
            report_hash = report.get("sha256") if isinstance(report, dict) else None
            if (
                not isinstance(report_path, str)
                or not isinstance(report_hash, str)
                or file_hash(Path(report_path)) != report_hash
            ):
                raise ValueError("a scout report changed after campaign start")


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


def _record_datetime(record: Mapping[str, object], field: str) -> datetime | None:
    value = record.get(field)
    if not isinstance(value, str) or not value:
        return None
    try:
        parsed = datetime.fromisoformat(value)
    except (TypeError, ValueError):
        return None
    if parsed.tzinfo is None:
        return None
    return parsed.astimezone(timezone.utc)


def valid_retry_evidence(
    evidence: Mapping[str, object], failed_campaign: Mapping[str, object]
) -> bool:
    """Return true when an evidence record supports one bounded retry."""

    if evidence.get("record_type") != "evidence":
        return False
    campaign_id = evidence.get("failed_campaign_id")
    if not isinstance(campaign_id, str) or campaign_id != failed_campaign.get(
        "campaign_id"
    ):
        return False
    if failed_campaign.get("record_type", "campaign") != "campaign":
        return False
    failed_addresses = set(_record_addresses(dict(failed_campaign)))
    evidence_addresses = set(_record_addresses(dict(evidence)))
    if not evidence_addresses or not evidence_addresses.issubset(failed_addresses):
        return False
    if failed_campaign.get("result") != "no-source":
        expected_by_address: dict[int, float] = {}
        for address in failed_addresses:
            _, expected = _address_prediction(
                failed_campaign, f"0x{address:08X}"
            )
            if expected is not None and math.isfinite(expected) and expected > 0.0:
                expected_by_address[address] = expected
        target_deltas = failed_campaign.get("target_deltas")
        severe_addresses: set[int] = set()
        if isinstance(target_deltas, dict):
            for address in failed_addresses:
                expected = expected_by_address.get(address)
                if expected is None:
                    continue
                delta = target_deltas.get(f"0x{address:08X}")
                if not isinstance(delta, dict):
                    continue
                retained = 0.0
                valid_delta = True
                for name in (
                    "effective_bytes",
                    "initialized_bytes",
                    "resource_explained_bytes",
                ):
                    value = delta.get(name, 0.0)
                    if value is None:
                        continue
                    if (
                        isinstance(value, bool)
                        or not isinstance(value, (int, float))
                        or not math.isfinite(float(value))
                    ):
                        valid_delta = False
                        break
                    retained += float(value)
                if valid_delta and retained / expected < 0.10:
                    severe_addresses.add(address)
        elif len(expected_by_address) == len(failed_addresses):
            expected = sum(expected_by_address.values())
            if expected > 0.0 and _retained_bytes(dict(failed_campaign)) / expected < 0.10:
                severe_addresses = failed_addresses
        if not evidence_addresses.issubset(severe_addresses):
            return False
    model = evidence.get("failed_model")
    model_values = failed_campaign.get("ruled_out_models")
    models = list(model_values) if isinstance(model_values, list) else []
    first_score_model = failed_campaign.get("first_score_model")
    if isinstance(first_score_model, str) and first_score_model.strip():
        models.append(first_score_model)
    if (
        not isinstance(model, str)
        or not model.strip()
        or not isinstance(models, list)
        or model not in models
    ):
        return False
    changed_assumption = evidence.get("changed_assumption")
    changed_source = evidence.get("changed_source")
    if not any(
        isinstance(value, str) and bool(value.strip())
        for value in (changed_assumption, changed_source)
    ):
        return False
    evidence_time = _record_datetime(evidence, "timestamp")
    failed_time = _record_datetime(failed_campaign, "ended_at") or _record_datetime(
        failed_campaign, "timestamp"
    )
    return bool(
        evidence_time is not None
        and failed_time is not None
        and evidence_time > failed_time
    )


def _address_lane_stats(
    records: list[dict[str, object]],
) -> dict[tuple[str, int], AddressStats]:
    totals: dict[tuple[str, int], dict[str, float]] = defaultdict(
        lambda: {
            "attempts": 0,
            "zero_yield_attempts": 0,
            "penalty_attempts": 0,
            "evidence_events": 0,
            "retry_credits": 0,
            "minutes": 0.0,
            "effective_bytes": 0.0,
            "initialized_bytes": 0.0,
        }
    )
    campaigns_by_id: dict[str, dict[str, object]] = {}
    used_retry_links: set[tuple[str, str, int]] = set()
    for record in _chronological_records(records):
        addresses = _record_addresses(record)
        if not addresses:
            continue
        if record.get("record_type") == "evidence":
            failed_id = record.get("failed_campaign_id")
            failed = (
                campaigns_by_id.get(failed_id)
                if isinstance(failed_id, str)
                else None
            )
            valid = failed is not None and valid_retry_evidence(record, failed)
            lane = _campaign_lane(failed) if failed is not None else _campaign_lane(record)
            for address in addresses:
                item = totals[(lane, address)]
                item["evidence_events"] += 1
                link = (str(failed_id), str(record.get("failed_model", "")), address)
                if (
                    valid
                    and link not in used_retry_links
                    and item["penalty_attempts"] > 0
                ):
                    item["penalty_attempts"] -= 1
                    item["retry_credits"] += 1
                    used_retry_links.add(link)
            continue
        if record.get("record_type", "campaign") != "campaign":
            continue
        campaign_id = record.get("campaign_id")
        if isinstance(campaign_id, str) and campaign_id:
            campaigns_by_id[campaign_id] = record
        lane = _campaign_lane(record)
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
            item = totals[(lane, address)]
            item["attempts"] += 1
            item["zero_yield_attempts"] += int(is_zero)
            item["penalty_attempts"] += int(is_zero)
            item["minutes"] += minutes / share
            item["effective_bytes"] += address_effective
            item["initialized_bytes"] += address_initialized
    return {
        key: AddressStats(
            attempts=int(values["attempts"]),
            zero_yield_attempts=int(values["zero_yield_attempts"]),
            penalty_attempts=int(values["penalty_attempts"]),
            evidence_events=int(values["evidence_events"]),
            retry_credits=int(values["retry_credits"]),
            minutes=values["minutes"],
            effective_bytes=values["effective_bytes"],
            initialized_bytes=values["initialized_bytes"],
        )
        for key, values in totals.items()
    }


def address_stats(
    records: list[dict[str, object]], lane: str | None = None
) -> dict[int, AddressStats]:
    """Return address history, optionally isolated to one campaign lane."""

    lane_values = _address_lane_stats(records)
    totals: dict[int, dict[str, float]] = defaultdict(
        lambda: {
            "attempts": 0,
            "zero_yield_attempts": 0,
            "penalty_attempts": 0,
            "evidence_events": 0,
            "retry_credits": 0,
            "minutes": 0.0,
            "effective_bytes": 0.0,
            "initialized_bytes": 0.0,
        }
    )
    for (item_lane, address), values in lane_values.items():
        if lane is not None and item_lane != lane:
            continue
        item = totals[address]
        for name in item:
            item[name] += float(getattr(values, name))
    return {
        address: AddressStats(
            attempts=int(values["attempts"]),
            zero_yield_attempts=int(values["zero_yield_attempts"]),
            penalty_attempts=int(values["penalty_attempts"]),
            evidence_events=int(values["evidence_events"]),
            retry_credits=int(values["retry_credits"]),
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
    for field in ("delivery_id", "evidence_id", "abort_id", "campaign_id"):
        value = record.get(field)
        if isinstance(value, str) and value:
            return field, value
    return None


@contextmanager
def _ledger_lock(path: Path):
    """Serialize ledger replacement so concurrent agents cannot lose rows."""

    path.parent.mkdir(parents=True, exist_ok=True)
    lock_path = path.with_name(f".{path.name}.lock")
    deadline = time.monotonic() + 10.0
    descriptor: int | None = None
    while descriptor is None:
        try:
            descriptor = os.open(
                lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600
            )
        except FileExistsError:
            try:
                stale = time.time() - lock_path.stat().st_mtime > 300.0
            except OSError:
                stale = False
            if stale:
                try:
                    lock_path.unlink()
                except OSError:
                    pass
                continue
            if time.monotonic() >= deadline:
                raise ValueError("the campaign ledger is busy")
            time.sleep(0.05)
    try:
        os.write(descriptor, f"{os.getpid()}\n".encode("ascii"))
        os.close(descriptor)
        descriptor = None
        yield
    finally:
        if descriptor is not None:
            os.close(descriptor)
        try:
            lock_path.unlink()
        except FileNotFoundError:
            pass


def append_record(
    path: Path,
    record: dict[str, object],
    *,
    expected_records: list[dict[str, object]] | None = None,
) -> None:
    with _ledger_lock(path):
        records = _read_records(path)
        if expected_records is not None and records != expected_records:
            raise ValueError("the campaign ledger changed. Retry the command")
        identity = _record_identity(record)
        if identity is not None:
            for existing in records:
                if _record_identity(existing) != identity:
                    continue
                if _record_fingerprint(existing) != _record_fingerprint(record):
                    raise ValueError(
                        f"{identity[0]} already identifies a different record"
                    )
                return
        write_records(path, [*records, record])


def is_duplicate(records: list[dict[str, object]], record: dict[str, object]) -> bool:
    identity = _record_identity(record)
    if identity is not None:
        return any(_record_identity(item) == identity for item in records)
    if isinstance(record.get("schema_version"), int) and int(
        record["schema_version"]
    ) >= 2:
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


def _validate_optional_file_hash(
    path: Path, expected: str | None, description: str
) -> None:
    current = file_hash(path) if path.is_file() else None
    if current != expected:
        raise ValueError(f"{description} changed after campaign start")


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


def _validate_baseline_report_artifacts(
    state: Mapping[str, object],
) -> tuple[Path, Path]:
    """Validate immutable baseline reports and their captured provenance."""

    from tools.decomp_provenance import provenance_path, validate_report_artifact

    code = Path(str(state.get("baseline_report", "")))
    data = Path(str(state.get("baseline_data_report", "")))
    if not code.is_file() or file_hash(code) != state.get("baseline_report_sha256"):
        raise ValueError("the baseline code report changed after campaign start")
    if not data.is_file() or file_hash(data) != state.get(
        "baseline_data_report_sha256"
    ):
        raise ValueError("the baseline typed-data report changed after campaign start")
    code_receipt = validate_report_artifact(code)
    data_receipt = validate_report_artifact(data)
    expected_code_provenance = state.get("baseline_report_provenance_sha256")
    expected_data_provenance = state.get(
        "baseline_data_report_provenance_sha256"
    )
    expected_identity = state.get("baseline_comparison_identity_sha256")
    if (
        not isinstance(expected_code_provenance, str)
        or file_hash(provenance_path(code)) != expected_code_provenance
        or not isinstance(expected_data_provenance, str)
        or file_hash(provenance_path(data)) != expected_data_provenance
    ):
        raise ValueError("the baseline comparison provenance changed")
    code_identity = code_receipt.get("input_identity")
    data_identity = data_receipt.get("input_identity")
    if (
        not isinstance(code_identity, dict)
        or code_identity != data_identity
        or not isinstance(expected_identity, str)
        or _snapshot_hash(code_identity) != expected_identity
    ):
        raise ValueError("the baseline comparison identity changed")
    return code, data


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
    if any(
        result.returncode != 0
        for result in (head, status, worktree, untracked)
    ):
        raise ValueError(f"cannot fingerprint tracked directory: {path}")
    payload = head.stdout + b"\0" + status.stdout + b"\0" + worktree.stdout
    digest = hashlib.sha256(payload)
    for encoded in sorted(value for value in untracked.stdout.split(b"\0") if value):
        relative = Path(encoded.decode("utf-8", errors="surrogateescape"))
        candidate = path / relative
        digest.update(encoded)
        if candidate.is_file() and not candidate.is_symlink():
            with candidate.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
        elif candidate.is_symlink():
            digest.update(str(candidate.readlink()).encode("utf-8"))
    return f"directory:{digest.hexdigest()}"


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
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(float(value))
        or value < 0
        or (not allow_zero and value == 0)
    ):
        qualifier = "zero or greater" if allow_zero else "greater than zero"
        raise ValueError(f"{name} must be finite and {qualifier}")


def _campaign_input_hashes(
    worktree_root: Path,
    map_path: Path | None,
    sizes_path: Path | None,
    *,
    require_reccmp_user: bool = True,
) -> tuple[
    dict[str, object],
    dict[str, str],
    dict[str, str],
    dict[str, str],
    dict[str, list[str]],
    dict[str, str],
]:
    worktree = source_worktree_snapshot(worktree_root)
    index = source_index_snapshot(worktree_root)
    repository_worktree = repository_worktree_snapshot(worktree_root)
    repository_index = repository_index_snapshot(worktree_root)
    resource_sources = resource_source_snapshot(worktree_root)
    hashes: dict[str, object] = {
        "source_worktree_sha256": _snapshot_hash(worktree),
        "source_index_sha256": _snapshot_hash(index),
        "repository_worktree_sha256": _snapshot_hash(repository_worktree),
        "repository_index_sha256": _snapshot_hash(repository_index),
        "resource_sources_sha256": _snapshot_hash(resource_sources),
        "functions_map_sha256": (
            file_hash(map_path) if map_path is not None and map_path.is_file() else None
        ),
        "function_sizes_file_sha256": (
            file_hash(sizes_path)
            if sizes_path is not None and sizes_path.is_file()
            else None
        ),
        **comparison_artifact_hashes(
            worktree_root, require_reccmp_user=require_reccmp_user
        ),
    }
    return (
        hashes,
        worktree,
        index,
        repository_worktree,
        repository_index,
        resource_sources,
    )


def comparison_artifact_hashes(
    root: Path, *, require_reccmp_user: bool = True
) -> dict[str, str | None]:
    """Return the build and report identities used during target selection."""

    from tools.decomp_provenance import reccmp_user_identity

    user_identity = (
        reccmp_user_identity(root) if require_reccmp_user else None
    )

    paths = {
        "retail_executable_sha256": root / "original/toy2.exe",
        "recompiled_executable_sha256": root / "build/toy2.exe",
        "recompiled_symbols_sha256": root / "build/toy2.pdb",
        "reccmp_build_sha256": root / "build/reccmp-build.yml",
        "reccmp_user_sha256": root / "reccmp-user.yml",
        "current_report_sha256": root / "build/decomp-current-report.json",
        "current_report_provenance_sha256": (
            root / "build/decomp-current-report.json.provenance.json"
        ),
        "current_data_report_sha256": (
            root / "build/decomp-current-data-report.json"
        ),
        "current_data_report_provenance_sha256": (
            root / "build/decomp-current-data-report.json.provenance.json"
        ),
    }
    hashes = {
        name: file_hash(path) if path.is_file() else None
        for name, path in paths.items()
    }
    hashes["configured_retail_executable_sha256"] = (
        str(user_identity["retail_sha256"])
        if user_identity is not None
        else hashes["retail_executable_sha256"]
    )
    return hashes


def _campaign_deadlines(
    started: datetime,
    expected_minutes: float | None,
    expected_retained_bytes: float | None,
) -> dict[str, str | None]:
    if expected_minutes is None:
        return {f"{name}_deadline": None for name in DEADLINE_FACTORS}
    started = started.replace(microsecond=0)
    deadlines = {
        f"{name}_deadline": timestamp(
            started + timedelta(minutes=expected_minutes * factor)
        )
        for name, factor in DEADLINE_FACTORS.items()
    }
    if expected_retained_bytes is None or expected_retained_bytes < 100:
        deadlines["extension_deadline"] = None
    return deadlines


def _first_finalization_start(
    phases: Mapping[str, object],
) -> datetime | None:
    values: list[datetime] = []
    for name in (
        "finalization-started",
        *(f"{step}_started" for step in FINALIZE_STEPS),
    ):
        value = phases.get(name)
        if value is not None:
            values.append(_parse_time(value, f"{name} phase"))
    return min(values) if values else None


def _validate_source_deadlines(
    state: Mapping[str, object],
    result: str,
    *,
    finalization_started: datetime | None = None,
    phase_timestamps: Mapping[str, object] | None = None,
) -> None:
    """Enforce the absolute gates for a forecast-backed source result."""

    if (
        result != "source"
        or state.get("mode") == "meta"
        or _validated_schema_version(state) < SCHEMA_VERSION
    ):
        return
    expected_minutes = state.get("expected_minutes")
    if expected_minutes is None:
        return
    expected_bytes = state.get("expected_retained_bytes")
    if (
        isinstance(expected_minutes, bool)
        or not isinstance(expected_minutes, (int, float))
        or not math.isfinite(float(expected_minutes))
        or float(expected_minutes) <= 0
    ):
        raise ValueError("the active campaign has invalid expected minutes")
    if (
        expected_bytes is not None
        and (
            isinstance(expected_bytes, bool)
            or not isinstance(expected_bytes, (int, float))
            or not math.isfinite(float(expected_bytes))
            or float(expected_bytes) < 0
        )
    ):
        raise ValueError("the active campaign has invalid expected retained bytes")
    started = _parse_time(state.get("started_at"), "started_at")
    deadlines = _campaign_deadlines(
        started,
        float(expected_minutes),
        float(expected_bytes) if expected_bytes is not None else None,
    )
    if state.get("deadlines") != deadlines:
        raise ValueError("the active campaign deadlines changed after start")

    baseline_value = state.get("baseline_at")
    if not isinstance(baseline_value, str) or not baseline_value:
        raise ValueError("a source campaign needs a baseline time")
    baseline = _parse_time(baseline_value, "baseline_at")
    preflight_deadline = _parse_time(
        deadlines["preflight_deadline"], "preflight deadline"
    )
    if baseline > preflight_deadline:
        raise ValueError("the campaign baseline missed the preflight deadline")

    phases = phase_timestamps or _phase_timestamps(state)
    preflight_value = phases.get("preflight")
    if not isinstance(preflight_value, str) or not preflight_value:
        raise ValueError("a source campaign needs a preflight phase stamp")
    preflight = _parse_time(preflight_value, "preflight phase")
    if preflight < baseline or preflight > preflight_deadline:
        raise ValueError("the preflight phase missed the preflight deadline")

    first_score_value = state.get("first_score_at")
    if not isinstance(first_score_value, str) or not first_score_value:
        raise ValueError("a source campaign needs a successful first-score stamp")
    first_score = _parse_time(first_score_value, "first_score_at")
    first_score_deadline = _parse_time(
        deadlines["first_score_deadline"], "first-score deadline"
    )
    if first_score > first_score_deadline:
        raise ValueError("the first score missed the first-score deadline")

    recorded_start = _first_finalization_start(phases)
    actual_start = recorded_start or finalization_started
    if actual_start is None:
        return
    if actual_start < started:
        raise ValueError("the finalization start time is before the campaign start")
    cutoff_name = (
        "extension"
        if deadlines["extension_deadline"] is not None
        else "stop"
    )
    cutoff = _parse_time(
        deadlines[f"{cutoff_name}_deadline"], f"{cutoff_name} deadline"
    )
    if actual_start > cutoff:
        raise ValueError(
            f"campaign finalization started after the {cutoff_name} deadline"
        )


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
        active_addresses = state.get("active_addresses", addresses)
        if (
            not isinstance(active_addresses, list)
            or not active_addresses
            or not all(isinstance(address, str) for address in active_addresses)
            or len(active_addresses) != len(set(active_addresses))
            or set(active_addresses) - set(addresses)
        ):
            raise ValueError("active campaign has invalid active addresses")
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


def _active_addresses(state: Mapping[str, object]) -> list[str]:
    addresses = state.get("addresses", [])
    active = state.get("active_addresses", addresses)
    if not isinstance(active, list) or not all(
        isinstance(address, str) for address in active
    ):
        raise ValueError("active campaign has invalid active addresses")
    return list(active)


def _validate_research_start_routes(
    root: Path,
    mode: str,
    addresses: list[str],
    brief_receipts: list[dict[str, object]],
) -> None:
    """Recompute each research route before campaign start."""

    from tools.decomp_brief import research_route_evidence

    if len(addresses) != len(brief_receipts):
        raise ValueError("research needs one route for each ordered target")
    for address, receipt in zip(addresses, brief_receipts):
        current = research_route_evidence(root, address)
        if receipt.get("research_route") != current:
            raise ValueError(
                "the brief research route does not match current routing evidence"
            )
        if current.get("eligible") is not True:
            raise ValueError(
                "research needs an active blocker, a target cooldown, or an open subsystem circuit"
            )
        if current.get("mode") != mode:
            raise ValueError("the research target state does not match the campaign mode")


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
    lane: str | None = None,
    prediction_version: str | None = None,
    prediction_lower_bound_bytes: float | None = None,
    prediction_features: Mapping[str, object] | None = None,
    progress_before: Mapping[str, object] | None = None,
    doctor_receipt_path: Path | None = None,
    brief_paths: list[Path] | None = None,
    require_brief: bool = False,
    require_prediction_metadata: bool = False,
    campaign_records: list[dict[str, object]] | None = None,
    campaign_ledger_path: Path | None = None,
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
    clean_lane = _validate_lane(mode, lane)
    clean_subsystem = subsystem.strip()
    if mode != "meta" and doctor_receipt_path is None:
        raise ValueError(f"a {mode} campaign needs --doctor-receipt")
    prediction = _prediction_metadata(
        expected_minutes,
        expected_retained_bytes,
        version=prediction_version,
        lower_bound_retained_bytes=prediction_lower_bound_bytes,
        features=prediction_features,
    )
    _validate_prediction_handoff(
        clean_lane,
        prediction,
        required=require_prediction_metadata,
        addresses=addresses if mode in ("coverage", "refinement", "data") else None,
    )
    metrics_before = _normalize_progress_metrics(progress_before)
    resource_text = format_resource(resources[0]) if resources else None
    (
        doctor_input_hashes,
        worktree,
        index,
        repository_worktree,
        repository_index,
        resource_sources,
    ) = _campaign_input_hashes(
        worktree_root,
        map_path,
        sizes_path,
        require_reccmp_user=mode != "meta",
    )
    _validate_relevant_git_modes(repository_index, "campaign repository index")
    doctor_times, doctor_receipt = _read_doctor_receipt(
        doctor_receipt_path,
        worktree_root,
        clean_lane,
        mode,
        addresses,
        resource_text,
        doctor_input_hashes,
        started,
    )
    brief_receipts: list[dict[str, object]] = []
    supplied_briefs = brief_paths or []
    expected_brief_targets = (
        [] if mode == "meta" else [resource_text] if mode == "resource" else addresses
    )
    if require_brief and len(supplied_briefs) != len(expected_brief_targets):
        raise ValueError(
            f"a {mode} campaign needs one --brief for each ordered target"
        )
    if supplied_briefs:
        if len(supplied_briefs) != len(expected_brief_targets):
            raise ValueError("the brief count disagrees with the campaign targets")
        try:
            from tools.decomp_brief import BriefError, validate_brief

            for brief_path, brief_target in zip(
                supplied_briefs, expected_brief_targets
            ):
                assert brief_target is not None
                resolved_brief_path = (
                    brief_path
                    if brief_path.is_absolute()
                    else worktree_root / brief_path
                )
                brief_receipts.append(
                    validate_brief(
                        resolved_brief_path,
                        clean_lane,
                        brief_target,
                        root=worktree_root,
                        doctor_receipt_path=doctor_receipt_path,
                    )
                )
        except BriefError as error:
            raise ValueError(f"the target brief is invalid: {error}") from error
    if mode in ("coverage", "refinement"):
        if brief_receipts:
            brief_subsystems = {
                item.get("subsystem") for item in brief_receipts
            }
            if (
                len(brief_subsystems) != 1
                or not all(
                    isinstance(value, str) and value
                    for value in brief_subsystems
                )
            ):
                raise ValueError(
                    "all source campaign targets need one authoritative subsystem"
                )
            authoritative_subsystem = str(next(iter(brief_subsystems)))
            if clean_subsystem and clean_subsystem != authoritative_subsystem:
                raise ValueError(
                    "--subsystem disagrees with the target briefs"
                )
            clean_subsystem = authoritative_subsystem
        if not clean_subsystem:
            raise ValueError("a source campaign needs a nonempty --subsystem")
    if clean_lane == "research" and brief_receipts:
        _validate_research_start_routes(
            worktree_root,
            mode,
            addresses,
            brief_receipts,
        )
    if clean_lane in {"closure", "production"}:
        from tools.decomp_candidates import (
            retry_evidence_for_target,
            source_circuit_open,
        )

        records = campaign_records
        if records is None:
            campaign_ledger = campaign_ledger_path or (
                worktree_root / "tools/Resources/campaign-ledger.jsonl"
            )
            records = (
                _read_records(campaign_ledger)
                if campaign_ledger.is_file()
                else []
            )
        if source_circuit_open(records, clean_subsystem):
            unsupported = [
                address
                for address in addresses
                if retry_evidence_for_target(
                    records,
                    int(address, 16),
                    mode,
                    clean_lane,
                )
                is None
            ]
            if unsupported:
                raise ValueError(
                    "the source cohort circuit breaker is open. Use the research lane"
                )
    phase_times = {
        "selection": doctor_times["selection_started_at"],
        "doctor-started": doctor_times["doctor_started_at"],
        "doctor-ended": doctor_times["doctor_ended_at"],
    }
    phase_times = {name: value for name, value in phase_times.items() if value}
    phase_times["started"] = started_at
    ledger_path = campaign_ledger_path or (
        worktree_root / "tools/Resources/campaign-ledger.jsonl"
    )
    try:
        ledger_relative = ledger_path.resolve().relative_to(
            worktree_root.resolve()
        ).as_posix()
    except ValueError as error:
        raise ValueError("the campaign ledger is outside the worktree") from error
    ledger_text = (
        ledger_path.read_text(encoding="utf-8") if ledger_path.is_file() else ""
    )
    campaign_head = (
        doctor_receipt.get("head")
        if isinstance(doctor_receipt, dict)
        else None
    )
    if not isinstance(campaign_head, str):
        head_result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=worktree_root,
            check=False,
            capture_output=True,
            text=True,
        )
        head_value = head_result.stdout.strip().lower()
        campaign_head = (
            head_value
            if head_result.returncode == 0
            and re.fullmatch(r"[0-9a-f]{40,64}", head_value) is not None
            else None
        )
    state: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "finalize_required": True,
        "campaign_id": campaign_id or str(uuid.uuid4()),
        "mode": mode,
        "lane": clean_lane,
        "addresses": addresses,
        "active_addresses": list(addresses),
        "retired_addresses": [],
        "resource": resource_text,
        "target_events": [],
        "subsystem": clean_subsystem,
        "family": family,
        "started_at": started_at,
        "phase_timestamps": phase_times,
        **doctor_times,
        "doctor_receipt": doctor_receipt,
        "doctor_receipts": [] if doctor_receipt is None else [doctor_receipt],
        "campaign_head": campaign_head,
        "briefs": brief_receipts,
        "briefs_required": require_brief,
        "first_score_at": None,
        "scored_addresses": [],
        "score_events": [],
        "phase": "started",
        "expected_minutes": expected_minutes,
        "expected_retained_bytes": expected_retained_bytes,
        "prediction": prediction,
        "prediction_required": require_prediction_metadata,
        "prediction_events": (
            []
            if mode == "meta"
            else [
                {
                    "addresses": list(addresses),
                    "resource": resource_text,
                    "selected_at": doctor_times["selection_started_at"]
                    or started_at,
                    "prediction": prediction,
                }
            ]
        ),
        "metrics_before": metrics_before,
        "implemented_before": metrics_before["implemented"],
        "terminal_before": metrics_before["terminal"],
        "terminal_bytes_before": metrics_before["terminal_bytes"],
        "effective_bytes_before": metrics_before["effective_bytes"],
        "source_debt_before": metrics_before["source_debt"],
        "deadlines": _campaign_deadlines(
            started, expected_minutes, expected_retained_bytes
        ),
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
        "campaign_ledger_relative": ledger_relative,
        "campaign_ledger_text": ledger_text,
        "campaign_ledger_sha256": hashlib.sha256(ledger_text.encode()).hexdigest(),
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
    progress_metrics: Mapping[str, object] | None = None,
) -> dict[str, object]:
    state = read_state(state_path)
    _require_mutable_campaign(state)
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
    worktree_root = Path(str(state.get("source_worktree_root", ROOT)))
    if mode in ("coverage", "refinement"):
        campaign_source_root = source_root or worktree_root / "src"
        for address in addresses:
            validate_source_target(
                str(address),
                mode,
                map_path,
                sizes_path,
                campaign_source_root,
            )
    from tools.decomp_provenance import (
        current_identity,
        provenance_path,
        validate_report,
    )

    comparison_identity = current_identity(worktree_root)
    code_receipt = validate_report(
        report_path, root=worktree_root, current=comparison_identity
    )
    data_receipt = validate_report(
        data_report_path, root=worktree_root, current=comparison_identity
    )
    if code_receipt.get("input_identity") != data_receipt.get("input_identity"):
        raise ValueError("the baseline reports use different comparison inputs")
    snapshot = _size_snapshot(sizes)
    analyzed_snapshot = _size_snapshot(analyzed_sizes)
    effective_before = effective_code_bytes(report_path, sizes)
    initialized_before = initialized_data_bytes(data_report_path)
    saved_metrics = state.get("metrics_before")
    metrics_before = _normalize_progress_metrics(
        progress_metrics
        if progress_metrics is not None
        else saved_metrics if isinstance(saved_metrics, dict) else None
    )
    metrics_before["effective_bytes"] = effective_before
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
    baseline_at = timestamp(attached)
    phases = _phase_timestamps(state)
    phases["baseline"] = baseline_at
    state.update(
        {
            "baseline_report": str(report_path.resolve()),
            "baseline_report_sha256": file_hash(report_path),
            "baseline_report_provenance_sha256": file_hash(
                provenance_path(report_path)
            ),
            "baseline_data_report": str(data_report_path.resolve()),
            "baseline_data_report_sha256": file_hash(data_report_path),
            "baseline_data_report_provenance_sha256": file_hash(
                provenance_path(data_report_path)
            ),
            "baseline_comparison_identity_sha256": _snapshot_hash(
                comparison_identity
            ),
            "effective_bytes_before": effective_before,
            "metrics_before": metrics_before,
            "implemented_before": metrics_before["implemented"],
            "terminal_before": metrics_before["terminal"],
            "terminal_bytes_before": metrics_before["terminal_bytes"],
            "source_debt_before": metrics_before["source_debt"],
            "initialized_bytes_before": initialized_before,
            "resource_before": resource_before,
            "baseline_at": baseline_at,
            "phase_timestamps": phases,
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


def attach_meta_baseline(
    state_path: Path,
    now: datetime | None = None,
    progress_metrics: Mapping[str, object] | None = None,
) -> dict[str, object]:
    """Attach a repository-only baseline without build dependencies."""

    state = read_state(state_path)
    _require_mutable_campaign(state)
    if state.get("mode") != "meta":
        raise ValueError("a repository-only baseline requires a meta campaign")
    if state.get("baseline_at"):
        raise ValueError("the active campaign already has a baseline")
    started = _parse_time(state.get("started_at"), "started_at")
    attached = now or utc_now()
    if attached < started:
        raise ValueError("the baseline time is before the campaign start time")
    baseline_at = timestamp(attached)
    phases = _phase_timestamps(state)
    phases["baseline"] = baseline_at
    metrics = _normalize_progress_metrics(progress_metrics)
    state.update(
        {
            "baseline_at": baseline_at,
            "baseline_kind": "repository",
            "metrics_before": metrics,
            "implemented_before": metrics["implemented"],
            "terminal_before": metrics["terminal"],
            "terminal_bytes_before": metrics["terminal_bytes"],
            "effective_bytes_before": metrics["effective_bytes"],
            "source_debt_before": metrics["source_debt"],
            "phase_timestamps": phases,
            "phase": "baseline",
        }
    )
    write_state(state_path, state)
    return state


def _score_value(value: float | None, field: str) -> float | None:
    if value is None:
        return None
    if not math.isfinite(value) or value < 0 or value > 100:
        raise ValueError(f"{field} must be between zero and 100")
    return float(value)


def mark_first_score(
    state_path: Path,
    address: str,
    now: datetime | None = None,
    *,
    raw_score: float | None = None,
    score_ceiling: float | None = None,
    ceiling_relative_score: float | None = None,
    model: str | None = None,
    artifact_sha256: str | None = None,
    effective: bool = False,
) -> str:
    if not state_path.exists():
        return "inactive"
    state = read_state(state_path)
    _require_mutable_campaign(state)
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
    clean_model = _clean_optional_line(model, "--model")
    artifact_hash = _validate_sha256(artifact_sha256, "--artifact-sha256")
    if _validated_schema_version(state) >= 3 and (
        artifact_hash is None or raw_score is None
    ):
        raise ValueError(
            "a schema-v3 first score needs a saved comparison artifact and score"
        )
    scored_addresses = [*scored_addresses, address]
    score_event = {
        "address": address,
        "scored_at": scored_at,
        "raw_score": _score_value(raw_score, "--raw-score"),
        "score_ceiling": _score_value(score_ceiling, "--score-ceiling"),
        "ceiling_relative_score": _score_value(
            ceiling_relative_score, "--ceiling-relative-score"
        ),
        "model": clean_model,
        "artifact_sha256": artifact_hash,
        "effective": bool(effective),
    }
    score_events = [*score_events, score_event]
    state["scored_addresses"] = scored_addresses
    state["score_events"] = score_events
    if not state.get("first_score_at"):
        state["first_score_at"] = scored_at
        state["first_score_address"] = address
        state["first_score_model"] = clean_model
        state["first_score_raw"] = score_event["raw_score"]
        state["first_score_ceiling"] = score_event["score_ceiling"]
        state["first_score_relative"] = score_event["ceiling_relative_score"]
        state["first_score_artifact_sha256"] = artifact_hash
        state["first_score_effective"] = bool(effective)
        phases = _phase_timestamps(state)
        phases["first-score"] = scored_at
        state["phase_timestamps"] = phases
    state["phase"] = "scoring"
    write_state(state_path, state)
    return "recorded"


def mark_resource_score(
    state_path: Path,
    now: datetime | None = None,
    *,
    raw_score: float | None = None,
    model: str | None = None,
    artifact_sha256: str | None = None,
    effective: bool = False,
) -> str:
    if not state_path.exists():
        return "inactive"
    state = read_state(state_path)
    _require_mutable_campaign(state)
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
    state["first_score_model"] = _clean_optional_line(model, "--model")
    score_value = _score_value(raw_score, "--raw-score")
    artifact_hash = _validate_sha256(
        artifact_sha256, "--artifact-sha256"
    )
    if _validated_schema_version(state) >= 3 and (
        score_value is None or artifact_hash is None
    ):
        raise ValueError(
            "a schema-v3 resource score needs a saved score artifact"
        )
    state["first_score_raw"] = score_value
    state["first_score_artifact_sha256"] = artifact_hash
    state["first_score_effective"] = bool(effective)
    phases = _phase_timestamps(state)
    phases["first-score"] = str(state["first_score_at"])
    state["phase_timestamps"] = phases
    state["phase"] = "scoring"
    write_state(state_path, state)
    return "recorded"


def mark_phase(
    state_path: Path, name: str, now: datetime | None = None
) -> dict[str, object]:
    state = read_state(state_path)
    _require_mutable_campaign(state)
    when = now or utc_now()
    started = _parse_time(state.get("started_at"), "started_at")
    if when < started:
        raise ValueError("the phase time is before the campaign start time")
    _stamp_phase(state, name, when)
    state["phase"] = _single_line(name, "phase name").replace(" ", "_")
    write_state(state_path, state)
    return state


def add_target(
    state_path: Path,
    address: str,
    now: datetime | None = None,
    map_path: Path | None = None,
    sizes_path: Path | None = None,
    source_root: Path | None = None,
    doctor_receipt_path: Path | None = None,
    brief_path: Path | None = None,
    require_brief: bool = False,
    replaces: str | None = None,
    expected_minutes: float | None = None,
    expected_retained_bytes: float | None = None,
    prediction_version: str | None = None,
    prediction_lower_bound_bytes: float | None = None,
    prediction_features: Mapping[str, object] | None = None,
    require_prediction_metadata: bool = False,
) -> dict[str, object]:
    state = read_state(state_path)
    _require_mutable_campaign(state)
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
    active_addresses = _active_addresses(state)
    require_handoff = require_brief or state.get("briefs_required") is True
    require_prediction = (
        require_prediction_metadata or state.get("prediction_required") is True
    )
    pivot_prediction = _prediction_metadata(
        expected_minutes,
        expected_retained_bytes,
        version=prediction_version,
        lower_bound_retained_bytes=prediction_lower_bound_bytes,
        features=prediction_features,
    )
    _validate_prediction_handoff(
        _campaign_lane(state),
        pivot_prediction,
        required=require_prediction,
        addresses=[address],
    )
    if family and replaces is not None:
        raise ValueError("a family expansion cannot use --replace")
    if not family and require_handoff and replaces is None:
        raise ValueError("a non-family pivot needs --replace")
    if replaces is not None and replaces not in active_addresses:
        raise ValueError("--replace is not an active campaign target")
    pivot_count = sum(
        isinstance(event, dict) and event.get("replaces") is not None
        for event in events
    )
    if not family and replaces is not None and pivot_count >= 2:
        raise ValueError("a campaign can have at most two pivots")
    if not family and replaces is None and len(active_addresses) >= 3:
        raise ValueError("a campaign can have at most three targets active")
    if family and not state.get("first_score_at"):
        raise ValueError(
            "a family campaign needs a first-score anchor before it adds a target"
        )
    if family and (
        state.get("first_score_artifact_sha256") is None
        or state.get("first_score_raw") is None
    ):
        raise ValueError(
            "a family campaign needs an artifact-backed anchor comparison"
        )
    if state.get("mode") == "data" and replaces is None and len(active_addresses) >= 3:
        raise ValueError("a data campaign can have at most three targets active")
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
    pivot_doctor: dict[str, object] | None = None
    pivot_brief: dict[str, object] | None = None
    doctor_times: dict[str, str | None] = {
        "selection_started_at": None,
        "doctor_started_at": None,
        "doctor_ended_at": None,
    }
    if require_handoff:
        if doctor_receipt_path is None:
            raise ValueError("a pivot target needs --doctor-receipt")
        if brief_path is None:
            raise ValueError("a pivot target needs --brief")
        root_value = state.get("source_worktree_root")
        if not isinstance(root_value, str) or not root_value:
            raise ValueError("the active campaign has no source worktree root")
        worktree_root = Path(root_value)
        doctor_input_hashes, *_ = _campaign_input_hashes(
            worktree_root, map_path, sizes_path
        )
        doctor_times, pivot_doctor = _read_doctor_receipt(
            doctor_receipt_path,
            worktree_root,
            _campaign_lane(state),
            str(state.get("mode", "")),
            [address],
            None,
            doctor_input_hashes,
            added,
        )
        if pivot_doctor is None or pivot_doctor.get("head") != state.get(
            "campaign_head"
        ):
            raise ValueError("the pivot doctor receipt uses another campaign HEAD")
        try:
            from tools.decomp_brief import BriefError, validate_brief

            resolved_brief_path = (
                brief_path
                if brief_path.is_absolute()
                else worktree_root / brief_path
            )
            pivot_brief = validate_brief(
                resolved_brief_path,
                _campaign_lane(state),
                address,
                root=worktree_root,
                doctor_receipt_path=doctor_receipt_path,
            )
            if state.get("mode") in ("coverage", "refinement") and (
                pivot_brief.get("subsystem") != state.get("subsystem")
            ):
                raise ValueError(
                    "the pivot brief subsystem disagrees with the campaign"
                )
        except BriefError as error:
            raise ValueError(f"the pivot brief is invalid: {error}") from error
    addresses = [*addresses, address]
    event: dict[str, object] = {
        "address": address,
        "added_at": timestamp(added),
    }
    if replaces is not None:
        event["replaces"] = replaces
    if require_prediction or any(
        value is not None
        for value in (
            expected_minutes,
            expected_retained_bytes,
            prediction_version,
            prediction_lower_bound_bytes,
        )
    ) or bool(prediction_features):
        event["prediction"] = pivot_prediction
    if pivot_doctor is not None and pivot_brief is not None:
        event["selection_started_at"] = doctor_times["selection_started_at"]
        event["doctor_started_at"] = doctor_times["doctor_started_at"]
        event["doctor_ended_at"] = doctor_times["doctor_ended_at"]
        event["doctor_receipt"] = pivot_doctor
        event["brief"] = pivot_brief
        receipts = state.get("doctor_receipts", [state.get("doctor_receipt")])
        briefs = state.get("briefs", [])
        if not isinstance(receipts, list) or not isinstance(briefs, list):
            raise ValueError("the active campaign has invalid evidence identities")
        state["doctor_receipts"] = [*receipts, pivot_doctor]
        state["briefs"] = [*briefs, pivot_brief]
    events = [*events, event]
    if "prediction" in event:
        prediction_events = state.get("prediction_events", [])
        if not isinstance(prediction_events, list):
            raise ValueError("the active campaign has invalid prediction events")
        state["prediction_events"] = [
            *prediction_events,
            {
                "addresses": [address],
                "resource": None,
                "selected_at": doctor_times["selection_started_at"]
                or timestamp(added),
                "prediction": pivot_prediction,
                "replaces": replaces,
            },
        ]
    state["addresses"] = addresses
    if replaces is None:
        state["active_addresses"] = [*active_addresses, address]
    else:
        state["active_addresses"] = [
            address if item == replaces else item for item in active_addresses
        ]
        retired = state.get("retired_addresses", [])
        if not isinstance(retired, list):
            raise ValueError("the active campaign has invalid retired addresses")
        state["retired_addresses"] = [*retired, replaces]
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
    write_text(
        path,
        _source_models_content(
            existing,
            campaign_id,
            ended_at,
            subsystem,
            mode,
            addresses,
            resource,
            models,
            note,
        ),
    )


def _source_models_content(
    existing: str,
    campaign_id: str,
    ended_at: str,
    subsystem: str,
    mode: str,
    addresses: list[str],
    resource: str | None,
    models: list[str],
    note: str,
) -> str:
    marker = f"<!-- campaign-id: {campaign_id} -->"
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
    return existing + block


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
    pivot_events = [
        event
        for event in events
        if isinstance(event, dict) and event.get("replaces") is not None
    ]
    if len(pivot_events) > 2 and state.get("family") is not True:
        raise ValueError("active campaign has too many pivot events")
    addresses = state.get("addresses", [])
    if not isinstance(addresses, list) or len(addresses) != len(set(addresses)):
        raise ValueError("active campaign has invalid addresses")
    event_addresses = {
        event.get("address")
        for event in events
        if isinstance(event, dict) and isinstance(event.get("address"), str)
    }
    reconstructed_active = [
        str(address) for address in addresses if address not in event_addresses
    ]
    reconstructed_retired: list[str] = []
    added_times = {str(address): started for address in reconstructed_active}
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
        replaced = event.get("replaces")
        if state.get("family") is True:
            if replaced is not None:
                raise ValueError("a family target event cannot replace a target")
            reconstructed_active.append(str(address))
        elif replaced is None:
            reconstructed_active.append(str(address))
        elif not isinstance(replaced, str) or replaced not in reconstructed_active:
            raise ValueError("a pivot did not replace an active target")
        else:
            reconstructed_active = [
                str(address) if item == replaced else item
                for item in reconstructed_active
            ]
            reconstructed_retired.append(replaced)
        if state.get("family") is not True and len(reconstructed_active) > 3:
            raise ValueError("a campaign can have at most three targets active")
        added_times[str(address)] = added
        previous = added

    if _active_addresses(state) != reconstructed_active:
        raise ValueError("active campaign targets disagree with pivot events")
    retired = state.get("retired_addresses")
    if retired is not None and retired != reconstructed_retired:
        raise ValueError("retired campaign targets disagree with pivot events")

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
        code_before_value = code_before.get(address, 0.0)
        code_after_value = code_after.get(address, 0.0)
        data_before_value = data_before.get(address, 0.0)
        data_after_value = data_after.get(address, 0.0)
        result[address_text] = {
            "effective_before": code_before_value,
            "effective_after": code_after_value,
            "effective_bytes": code_after_value - code_before_value,
            "initialized_before": data_before_value,
            "initialized_after": data_after_value,
            "initialized_bytes": data_after_value - data_before_value,
        }
    return result


def _requires_finalize_receipt(state: Mapping[str, object]) -> bool:
    schema_value = state.get("schema_version", 1)
    phase = str(state.get("phase", ""))
    return (
        isinstance(schema_value, int)
        and not isinstance(schema_value, bool)
        and schema_value >= SCHEMA_VERSION
    ) or (
        state.get("finalize_required") is True
        or state.get("finalize_receipt") is not None
        or phase == "finalized"
        or phase.startswith("finalize_")
    )


def _validated_schema_version(state: Mapping[str, object]) -> int:
    value = state.get("schema_version", 1)
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < 1
        or value > SCHEMA_VERSION
    ):
        raise ValueError("active campaign has an unsupported schema version")
    return value


def _prediction_handoff_arguments(
    path: Path,
    *,
    lane: str,
    mode: str,
    addresses: list[str],
) -> dict[str, object]:
    handoff = _read_json_object(path, "prediction handoff")
    if handoff.get("schema_version") != 2:
        raise ValueError("the prediction handoff schema is invalid")
    if len(addresses) != 1 or handoff.get("address") != addresses[0]:
        raise ValueError("the prediction handoff target disagrees with --address")
    if handoff.get("lane") != lane:
        raise ValueError("the prediction handoff lane disagrees with --lane")
    if handoff.get("mode") != mode:
        raise ValueError("the prediction handoff mode disagrees with --mode")
    generated_at = handoff.get("generated_at")
    if not isinstance(generated_at, str):
        raise ValueError("the prediction handoff has no generation time")
    try:
        generated = datetime.fromisoformat(generated_at)
    except (TypeError, ValueError) as error:
        raise ValueError("the prediction handoff generation time is invalid") from error
    if generated.tzinfo is None:
        raise ValueError("the prediction handoff generation time needs a UTC offset")
    age = utc_now() - generated.astimezone(timezone.utc)
    if age < timedelta(0) or age > PREDICTION_HANDOFF_MAX_AGE:
        raise ValueError("the prediction handoff is stale")
    from tools.decomp_candidates import (
        candidate_selection_fingerprint,
        candidate_cache_key,
        current_prediction_feature_handoff,
    )

    fingerprint = candidate_selection_fingerprint(dependency_mode=True)
    if (
        handoff.get("selection_fingerprint") != fingerprint
        or handoff.get("selection_fingerprint_sha256")
        != candidate_cache_key(fingerprint)
    ):
        raise ValueError("the prediction handoff input identity is stale")
    expected = current_prediction_feature_handoff(int(addresses[0], 16), lane)
    expected.pop("generated_at", None)
    supplied = dict(handoff)
    supplied.pop("generated_at", None)
    if supplied != expected:
        raise ValueError("the prediction handoff disagrees with current selection")
    return {
        "expected_minutes": handoff.get("expected_minutes"),
        "expected_retained_bytes": handoff.get("expected_retained_bytes"),
        "prediction_version": handoff.get("prediction_version"),
        "prediction_lower_bound_bytes": handoff.get(
            "prediction_lower_bound_bytes"
        ),
        "prediction_features": handoff,
        "subsystem": handoff.get("subsystem"),
    }


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
    item_phases = item.get("phase_timestamps")
    _validate_source_deadlines(
        state,
        str(item.get("result", "")),
        phase_timestamps=item_phases if isinstance(item_phases, dict) else None,
    )
    ledger_value = finalization.get("ledger_path")
    models_value = finalization.get("source_models_path")
    if not isinstance(ledger_value, str) or not isinstance(models_value, str):
        raise ValueError("active campaign has invalid finalization paths")
    if _requires_finalize_receipt(state):
        allowed_changes: set[str] = set()
        root_value = state.get("source_worktree_root")
        if isinstance(root_value, str):
            root = Path(root_value).resolve()

            def relative_bookkeeping_path(value: str) -> str | None:
                try:
                    return Path(value).resolve().relative_to(root).as_posix()
                except ValueError:
                    return None

            if item.get("result") == "no-source":
                model_relative = relative_bookkeeping_path(models_value)
                if model_relative is not None:
                    allowed_changes.add(model_relative)
            ledger_records = _read_records(Path(ledger_value))
            if any(
                _record_identity(record) == _record_identity(item)
                and _record_fingerprint(record) == _record_fingerprint(item)
                for record in ledger_records
            ):
                ledger_relative = relative_bookkeeping_path(ledger_value)
                if ledger_relative is not None:
                    allowed_changes.add(ledger_relative)
        _validated_finalize_receipt(
            state,
            str(item.get("result", "")),
            allowed_repository_changes=allowed_changes,
        )
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


def _campaign_record_receipt_hash(document: Mapping[str, object]) -> str:
    payload = dict(document)
    payload.pop("content_sha256", None)
    return _snapshot_hash(payload)


def _bind_campaign_record_receipt(item: dict[str, object]) -> None:
    """Bind a completed ledger row to its finalization receipt."""

    finalize_identity = item.get("finalize_receipt")
    if not isinstance(finalize_identity, dict):
        return
    finalize_path = finalize_identity.get("path")
    finalize_hash = finalize_identity.get("content_sha256")
    if not isinstance(finalize_path, str) or not isinstance(finalize_hash, str):
        return
    campaign = dict(item)
    campaign.pop("campaign_record_receipt", None)
    document: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "campaign-record-receipt",
        "finalize_receipt_sha256": finalize_hash,
        "campaign": campaign,
    }
    document["content_sha256"] = _campaign_record_receipt_hash(document)
    path = (
        Path(finalize_path).resolve().parent
        / "campaign-records"
        / f"{document['content_sha256']}.json"
    )
    encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
    if path.exists():
        if path.read_text(encoding="utf-8") != encoded:
            raise ValueError("the campaign record receipt cache has different content")
    else:
        write_text(path, encoded)
    item["campaign_record_receipt"] = {
        "path": str(path),
        "file_sha256": file_hash(path),
        "content_sha256": document["content_sha256"],
    }


def _validate_campaign_record_receipt(
    campaign: Mapping[str, object],
) -> dict[str, object]:
    identity = campaign.get("campaign_record_receipt")
    if not isinstance(identity, dict):
        raise ValueError("the campaign has no immutable record receipt")
    path_value = identity.get("path")
    if not isinstance(path_value, str):
        raise ValueError("the campaign record receipt identity is invalid")
    path = Path(path_value)
    if file_hash(path) != identity.get("file_sha256"):
        raise ValueError("the campaign record receipt file changed")
    document = _read_json_object(path, "campaign record receipt")
    if (
        document.get("schema_version") != SCHEMA_VERSION
        or document.get("record_type") != "campaign-record-receipt"
        or document.get("content_sha256")
        != _campaign_record_receipt_hash(document)
        or document.get("content_sha256") != identity.get("content_sha256")
    ):
        raise ValueError("the campaign record receipt is invalid")
    finalize_identity = campaign.get("finalize_receipt")
    expected_finalize = (
        finalize_identity.get("content_sha256")
        if isinstance(finalize_identity, dict)
        else None
    )
    if document.get("finalize_receipt_sha256") != expected_finalize:
        raise ValueError("the campaign record uses another finalization receipt")
    recorded_campaign = dict(campaign)
    recorded_campaign.pop("campaign_record_receipt", None)
    if document.get("campaign") != recorded_campaign:
        raise ValueError("the completed campaign row changed after recording")
    return document


def _record_meta_campaign(
    ledger_path: Path,
    state_path: Path,
    source_models_path: Path,
    state: dict[str, object],
    *,
    commit: str | None,
    note: str | None,
    supplied_minutes: float | None,
    supplied_effective_bytes: float | None,
    supplied_initialized_bytes: float | None,
    now: datetime,
    progress_after: Mapping[str, object] | None,
) -> dict[str, object]:
    if not state.get("baseline_at"):
        raise ValueError("the active meta campaign has no repository baseline")
    started, _, first_score = _validate_timeline(state, now)
    if first_score is not None:
        raise ValueError("a meta campaign cannot have a first-score event")
    elapsed_minutes = (now - started).total_seconds() / 60.0
    _assert_close("--minutes", supplied_minutes, elapsed_minutes)
    _assert_close("--effective-bytes", supplied_effective_bytes, 0.0)
    _assert_close("--initialized-bytes", supplied_initialized_bytes, 0.0)
    before_value = state.get("metrics_before")
    after_value = state.get("metrics_after")
    metrics_before = _normalize_progress_metrics(
        before_value if isinstance(before_value, dict) else None
    )
    metrics_after = _normalize_progress_metrics(
        progress_after
        if progress_after is not None
        else after_value if isinstance(after_value, dict) else metrics_before
    )
    ended_at = timestamp(now)
    phases = _phase_timestamps(state)
    phases["ended"] = ended_at
    phases["record-started"] = ended_at
    phases["record"] = ended_at
    prediction = state.get("prediction")
    if not isinstance(prediction, dict):
        prediction = _prediction_metadata(None, None)
    item: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "campaign",
        "campaign_id": _campaign_id(state),
        "timestamp": ended_at,
        "started_at": state["started_at"],
        "first_score_at": None,
        "resource_score_at": None,
        "ended_at": ended_at,
        "record_started_at": ended_at,
        "record_ended_at": ended_at,
        "first_score_minutes": None,
        "post_first_score_minutes": None,
        "mode": "meta",
        "lane": _campaign_lane(state),
        "result": "meta-fix",
        "addresses": [],
        "active_addresses": [],
        "retired_addresses": [],
        "resource": None,
        "target_events": [],
        "scored_addresses": [],
        "score_events": [],
        "target_deltas": {},
        "resource_before": None,
        "resource_after": None,
        "resource_explained_bytes": 0,
        "subsystem": str(state.get("subsystem", "")),
        "family": False,
        "minutes": elapsed_minutes,
        "effective_bytes": 0.0,
        "initialized_bytes": 0.0,
        "metrics_before": metrics_before,
        "metrics_after": metrics_after,
        "implemented_before": metrics_before["implemented"],
        "implemented_after": metrics_after["implemented"],
        "terminal_before": metrics_before["terminal"],
        "terminal_after": metrics_after["terminal"],
        "terminal_bytes_before": metrics_before["terminal_bytes"],
        "terminal_bytes_after": metrics_after["terminal_bytes"],
        "source_debt_before": metrics_before["source_debt"],
        "source_debt_after": metrics_after["source_debt"],
        "initialized_bytes_before": 0.0,
        "initialized_bytes_after": 0.0,
        "function_size_snapshot_sha256": state.get(
            "function_size_snapshot_sha256"
        ),
        "functions_map_sha256": state.get("functions_map_sha256"),
        "function_sizes_file_sha256": state.get("function_sizes_file_sha256"),
        "baseline_report_sha256": state.get("baseline_report_sha256"),
        "current_report_sha256": None,
        "baseline_data_report_sha256": state.get("baseline_data_report_sha256"),
        "current_data_report_sha256": None,
        "measurement": "repository",
        "commit": commit or "",
        "note": _single_line(note, "--note") if note and note.strip() else "",
        "ruled_out_models": [],
        "expected_minutes": state.get("expected_minutes"),
        "expected_retained_bytes": state.get("expected_retained_bytes"),
        "prediction": prediction,
        "prediction_events": state.get("prediction_events", []),
        "prediction_required": state.get("prediction_required") is True,
        "selection_started_at": state.get("selection_started_at"),
        "doctor_started_at": state.get("doctor_started_at"),
        "doctor_ended_at": state.get("doctor_ended_at"),
        "doctor_receipt": state.get("doctor_receipt"),
        "doctor_receipts": state.get("doctor_receipts", []),
        "campaign_head": state.get("campaign_head"),
        "briefs": state.get("briefs", []),
        "briefs_required": state.get("briefs_required") is True,
        "phase_timestamps": phases,
        "finalize_receipt": state.get("finalize_receipt"),
        "deadlines": state.get("deadlines", {}),
    }
    _bind_campaign_record_receipt(item)
    state["phase"] = "finalizing"
    state["finalization"] = {
        "item": item,
        "ledger_path": str(ledger_path.resolve()),
        "source_models_path": str(source_models_path.resolve()),
    }
    write_state(state_path, state)
    return _finish_campaign_finalization(state_path, state)


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
    progress_after: Mapping[str, object] | None = None,
) -> dict[str, object]:
    state = read_state(state_path)
    _validated_schema_version(state)
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

    record_started = now or utc_now()
    record_started_at = timestamp(record_started)
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
    _validated_schema_version(state)
    if _requires_finalize_receipt(state):
        if state.get("phase") != "finalized":
            raise ValueError("finalize the schema-v3 campaign before recording it")
        receipt = _validated_finalize_receipt(state, result)
        receipt_phases = receipt.get("phase_timestamps")
        _validate_source_deadlines(
            state,
            result,
            phase_timestamps=(
                receipt_phases if isinstance(receipt_phases, dict) else None
            ),
        )
        if mode != "meta":
            current_report = _receipt_step_artifact(receipt, "code_report")
            current_data_report = _receipt_step_artifact(receipt, "data_report")
        receipt_metrics = receipt.get("metrics_after")
        if isinstance(receipt_metrics, dict):
            state["metrics_after"] = receipt_metrics
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
    if mode == "meta":
        if models:
            raise ValueError("a meta-fix campaign cannot record rejected source models")
        return _record_meta_campaign(
            ledger_path,
            state_path,
            source_models_path,
            state,
            commit=commit,
            note=note,
            supplied_minutes=supplied_minutes,
            supplied_effective_bytes=supplied_effective_bytes,
            supplied_initialized_bytes=supplied_initialized_bytes,
            now=record_started,
            progress_after=progress_after,
        )
    if result == "no-source" and not models:
        raise ValueError("a no-source campaign needs at least one --model")
    if result == "no-source":
        _validate_no_source_tree(state)
    if not state.get("baseline_report") or not state.get("baseline_data_report"):
        raise ValueError("the active campaign has no attached baseline reports")
    baseline_report, baseline_data_report = _validate_baseline_report_artifacts(
        state
    )

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
    saved_after_metrics = state.get("metrics_after")
    metrics_after = _normalize_progress_metrics(
        progress_after
        if progress_after is not None
        else saved_after_metrics if isinstance(saved_after_metrics, dict) else None
    )
    metrics_after["effective_bytes"] = effective_after
    saved_before_metrics = state.get("metrics_before")
    metrics_before = _normalize_progress_metrics(
        saved_before_metrics if isinstance(saved_before_metrics, dict) else None
    )
    metrics_before["effective_bytes"] = effective_before
    if _campaign_lane(state) == "closure" and result == "source":
        from tools.decomp_status import read_match_statuses

        statuses = read_match_statuses(current_report)
        closure_addresses = _active_addresses(state)
        nonterminal = [
            address
            for address in closure_addresses
            if int(address, 16) not in statuses
            or not (
                statuses[int(address, 16)].exact
                or statuses[int(address, 16)].effective
            )
        ]
        if nonterminal:
            raise ValueError(
                "closure targets are not terminal: " + ", ".join(nonterminal)
            )
        if (
            metrics_after["terminal"] <= metrics_before["terminal"]
            and metrics_after["source_debt"] >= metrics_before["source_debt"]
        ):
            raise ValueError(
                "a closure campaign must add a terminal function or remove source debt"
            )
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
    phases = _phase_timestamps(state)
    phases["ended"] = ended_at
    phases["record-started"] = record_started_at
    phases["record"] = ended_at
    prediction = state.get("prediction")
    if not isinstance(prediction, dict):
        prediction = _prediction_metadata(
            state.get("expected_minutes")
            if isinstance(state.get("expected_minutes"), (int, float))
            else None,
            state.get("expected_retained_bytes")
            if isinstance(state.get("expected_retained_bytes"), (int, float))
            else None,
        )
    target_deltas = _target_deltas(
        addresses,
        baseline_report,
        current_report,
        baseline_data_report,
        current_data_report,
        sizes,
    )
    if result == "source" and mode in ("coverage", "refinement"):
        severe_targets = []
        for address in addresses:
            _, target_forecast = _address_prediction(state, address)
            if target_forecast is None or target_forecast <= 0:
                continue
            delta = target_deltas.get(address, {})
            retained = float(delta.get("effective_bytes", 0.0)) + float(
                delta.get("initialized_bytes", 0.0)
            )
            if retained / target_forecast < 0.10:
                severe_targets.append(address)
        if severe_targets and not models:
            raise ValueError(
                "a source result below 10 percent of forecast needs --model for "
                + ", ".join(severe_targets)
            )
    item: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "campaign",
        "campaign_id": campaign_id,
        "timestamp": ended_at,
        "started_at": state["started_at"],
        "first_score_at": first_score_at,
        "resource_score_at": state.get("resource_score_at"),
        "ended_at": ended_at,
        "record_started_at": record_started_at,
        "record_ended_at": ended_at,
        "first_score_minutes": first_score_minutes,
        "post_first_score_minutes": post_first_score_minutes,
        "mode": mode,
        "lane": _campaign_lane(state),
        "result": result,
        "addresses": addresses,
        "active_addresses": _active_addresses(state),
        "retired_addresses": state.get("retired_addresses", []),
        "resource": state.get("resource"),
        "target_events": state.get("target_events", []),
        "scored_addresses": scored_addresses,
        "score_events": state.get("score_events", []),
        "first_score_model": state.get("first_score_model"),
        "first_score_raw": state.get("first_score_raw"),
        "first_score_ceiling": state.get("first_score_ceiling"),
        "first_score_relative": state.get("first_score_relative"),
        "first_score_artifact_sha256": state.get(
            "first_score_artifact_sha256"
        ),
        "first_score_effective": state.get("first_score_effective"),
        "target_deltas": target_deltas,
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
        "metrics_before": metrics_before,
        "metrics_after": metrics_after,
        "implemented_before": metrics_before["implemented"],
        "implemented_after": metrics_after["implemented"],
        "terminal_before": metrics_before["terminal"],
        "terminal_after": metrics_after["terminal"],
        "terminal_bytes_before": metrics_before["terminal_bytes"],
        "terminal_bytes_after": metrics_after["terminal_bytes"],
        "source_debt_before": metrics_before["source_debt"],
        "source_debt_after": metrics_after["source_debt"],
        "initialized_bytes_before": initialized_before,
        "initialized_bytes_after": initialized_after,
        "function_size_snapshot_sha256": state["function_size_snapshot_sha256"],
        "functions_map_sha256": state.get("functions_map_sha256"),
        "function_sizes_file_sha256": state.get("function_sizes_file_sha256"),
        "baseline_report_sha256": file_hash(baseline_report),
        "baseline_report_provenance_sha256": state.get(
            "baseline_report_provenance_sha256"
        ),
        "current_report_sha256": file_hash(current_report),
        "baseline_data_report_sha256": file_hash(baseline_data_report),
        "baseline_data_report_provenance_sha256": state.get(
            "baseline_data_report_provenance_sha256"
        ),
        "baseline_comparison_identity_sha256": state.get(
            "baseline_comparison_identity_sha256"
        ),
        "current_data_report_sha256": file_hash(current_data_report),
        "measurement": "reports",
        "commit": commit or "",
        "note": _single_line(note, "--note") if note and note.strip() else "",
        "ruled_out_models": models,
        "expected_minutes": state.get("expected_minutes"),
        "expected_retained_bytes": state.get("expected_retained_bytes"),
        "prediction": prediction,
        "prediction_events": state.get("prediction_events", []),
        "prediction_required": state.get("prediction_required") is True,
        "selection_started_at": state.get("selection_started_at"),
        "doctor_started_at": state.get("doctor_started_at"),
        "doctor_ended_at": state.get("doctor_ended_at"),
        "doctor_receipt": state.get("doctor_receipt"),
        "doctor_receipts": state.get("doctor_receipts", []),
        "campaign_head": state.get("campaign_head"),
        "briefs": state.get("briefs", []),
        "briefs_required": state.get("briefs_required") is True,
        "phase_timestamps": phases,
        "finalize_receipt": state.get("finalize_receipt"),
        "deadlines": state.get("deadlines", {}),
    }
    _bind_campaign_record_receipt(item)
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
    *,
    failed_campaign_id: str | None = None,
    failed_model: str | None = None,
    changed_assumption: str | None = None,
    changed_source: str | None = None,
) -> dict[str, object]:
    if not addresses:
        raise ValueError("new evidence needs at least one --address")
    if len(addresses) != len(set(addresses)):
        raise ValueError("new evidence contains a duplicate address")
    if evidence_kind not in EVIDENCE_KINDS:
        raise ValueError(f"unknown evidence kind: {evidence_kind}")
    clean_note = _single_line(note, "--note")
    clean_campaign_id = _clean_optional_line(
        failed_campaign_id, "--failed-campaign-id"
    )
    clean_model = _clean_optional_line(failed_model, "--failed-model")
    if clean_campaign_id is None or clean_model is None:
        raise ValueError(
            "retry evidence needs --failed-campaign-id and --failed-model"
        )
    clean_assumption = _clean_optional_line(
        changed_assumption, "--changed-assumption"
    )
    clean_source = _clean_optional_line(changed_source, "--changed-source")
    if clean_assumption is None and clean_source is None:
        raise ValueError(
            "retry evidence needs --changed-assumption or --changed-source"
        )
    records = _read_records(ledger_path)
    failed = next(
        (
            item
            for item in records
            if item.get("record_type", "campaign") == "campaign"
            and item.get("campaign_id") == clean_campaign_id
        ),
        None,
    )
    if failed is None:
        raise ValueError("--failed-campaign-id does not identify a campaign")
    recorded_at = timestamp(now or utc_now())
    item: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "evidence",
        "evidence_id": evidence_id or str(uuid.uuid4()),
        "timestamp": recorded_at,
        "addresses": addresses,
        "lane": _campaign_lane(failed),
        "evidence_kind": evidence_kind,
        "failed_campaign_id": clean_campaign_id,
        "failed_model": clean_model,
        "changed_assumption": clean_assumption,
        "changed_source": clean_source,
        "note": clean_note,
    }
    if not valid_retry_evidence(item, failed):
        raise ValueError(
            "retry evidence must follow the failed campaign, use one of its models, "
            "and use only its targets"
        )
    for existing in records:
        if (
            existing.get("record_type") == "evidence"
            and existing.get("addresses") == addresses
            and existing.get("evidence_kind") == evidence_kind
            and existing.get("failed_campaign_id") == clean_campaign_id
            and existing.get("failed_model") == clean_model
            and existing.get("changed_assumption") == clean_assumption
            and existing.get("changed_source") == clean_source
            and existing.get("note") == clean_note
        ):
            raise ValueError("this evidence event already exists")
    append_record(ledger_path, item, expected_records=records)
    return item


def _git_commit(root: Path, value: str, field: str) -> str:
    clean = _validate_commit_id(value, field)
    assert clean is not None
    completed = subprocess.run(
        ["git", "rev-parse", "--verify", f"{clean}^{{commit}}"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    resolved = completed.stdout.strip().lower()
    if completed.returncode != 0 or re.fullmatch(r"[0-9a-f]{40,64}", resolved) is None:
        raise ValueError(f"{field} does not identify a Git commit")
    return resolved


def _git_head(root: Path) -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    value = completed.stdout.strip().lower()
    if completed.returncode != 0 or re.fullmatch(r"[0-9a-f]{40,64}", value) is None:
        raise ValueError("cannot read the delivery HEAD")
    return value


def _git_is_ancestor(root: Path, ancestor: str, descendant: str) -> bool:
    return subprocess.run(
        ["git", "merge-base", "--is-ancestor", ancestor, descendant],
        cwd=root,
        check=False,
        capture_output=True,
    ).returncode == 0


def _git_first_parent(root: Path, commit: str) -> str:
    completed = subprocess.run(
        ["git", "rev-parse", f"{commit}^"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    value = completed.stdout.strip().lower()
    if completed.returncode != 0 or re.fullmatch(r"[0-9a-f]{40,64}", value) is None:
        raise ValueError("the delivery commit has no first parent")
    return value


def _records_from_text(content: str, label: str) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for line_number, raw in enumerate(content.splitlines(), 1):
        if not raw.strip():
            continue
        try:
            value = json.loads(raw)
        except json.JSONDecodeError as error:
            raise ValueError(f"{label} line {line_number} is invalid JSON") from error
        if not isinstance(value, dict):
            raise ValueError(f"{label} line {line_number} is not an object")
        records.append(value)
    return records


def _committed_file_text(
    root: Path,
    relative: str,
    commit: str,
    *,
    allow_missing: bool = False,
) -> str:
    completed = subprocess.run(
        ["git", "show", f"{commit}:{relative}"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        if allow_missing:
            return ""
        raise ValueError(f"the delivery commit does not contain {relative}")
    return completed.stdout


def _committed_ledger_records(
    root: Path, ledger_path: Path, commit: str
) -> list[dict[str, object]]:
    try:
        relative = ledger_path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise ValueError("the delivery ledger is outside the repository") from error
    content = _committed_file_text(root, relative, commit)
    return _records_from_text(content, "committed campaign ledger")


def _verify_campaign_in_commit(
    root: Path,
    ledger_path: Path,
    campaign: dict[str, object],
    commit: str,
    base_commit: str,
) -> None:
    _validate_campaign_record_receipt(campaign)
    try:
        relative = ledger_path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise ValueError("the delivery ledger is outside the repository") from error
    committed_text = _committed_file_text(root, relative, commit)
    base_text = _committed_file_text(
        root, relative, base_commit, allow_missing=True
    )
    committed = _records_from_text(committed_text, "committed campaign ledger")
    campaign_id = campaign.get("campaign_id")
    campaign_indexes = [
        index
        for index, item in enumerate(committed)
        if item.get("record_type", "campaign") == "campaign"
        and item.get("campaign_id") == campaign_id
    ]
    if len(campaign_indexes) != 1:
        raise ValueError("the delivery commit needs one campaign record")
    campaign_index = campaign_indexes[0]
    if committed[campaign_index] != campaign:
        raise ValueError("the delivery commit does not contain this campaign record")
    suffix = committed[campaign_index:]
    if len(suffix) != 3:
        raise ValueError(
            "the delivery commit has records outside the authorized campaign append"
        )
    delivery_rows = suffix[1:]
    if [item.get("status") for item in delivery_rows] != ["staged", "accepted"]:
        raise ValueError(
            "the delivery commit must contain the staged and accepted events"
        )
    finalize_identity = campaign.get("finalize_receipt")
    finalize_hash = (
        finalize_identity.get("content_sha256")
        if isinstance(finalize_identity, dict)
        else None
    )
    delivery_ids: set[str] = set()
    for status, item in zip(("staged", "accepted"), delivery_rows):
        delivery_id = item.get("delivery_id")
        if (
            item.get("record_type") != "delivery"
            or item.get("campaign_id") != campaign_id
            or item.get("mode") != campaign.get("mode")
            or item.get("lane") != _campaign_lane(campaign)
            or item.get("addresses") != campaign.get("addresses", [])
            or item.get("resource") != campaign.get("resource")
            or item.get("receipt_sha256") != finalize_hash
            or item.get("delivery_receipt_path") is not None
            or item.get("base_commit") is not None
            or item.get("commit") is not None
            or item.get("phase") != status
            or not isinstance(delivery_id, str)
            or not delivery_id
            or delivery_id in delivery_ids
        ):
            raise ValueError(f"the committed {status} delivery event is invalid")
        delivery_ids.add(delivery_id)

    lines = committed_text.splitlines(keepends=True)
    if len(lines) != len(committed) or any(
        not line.endswith("\n") for line in lines
    ):
        raise ValueError("the committed campaign ledger format is invalid")
    pre_campaign_text = "".join(lines[:campaign_index])
    receipt = _campaign_finalize_receipt(campaign, validate_artifacts=False)
    key = receipt.get("key_payload")
    if not isinstance(key, dict) or key.get("campaign_ledger_relative") != relative:
        raise ValueError("the finalization receipt names another campaign ledger")
    start_text = key.get("campaign_ledger_text")
    original_commit = key.get("campaign_ledger_base_commit")
    if not isinstance(start_text, str) or not isinstance(original_commit, str):
        raise ValueError("the finalization receipt has no campaign ledger baseline")
    original_text = _committed_file_text(
        root, relative, original_commit, allow_missing=True
    )
    if not start_text.startswith(original_text):
        raise ValueError(
            "the campaign-start ledger changed committed history"
        )
    if not base_text.startswith(original_text):
        raise ValueError("the integrated base changed prior campaign history")
    local_start_append = start_text[len(original_text):]
    if pre_campaign_text != base_text + local_start_append:
        raise ValueError(
            "the delivery commit changed prior history or added an unrelated record"
        )


def _git_tree_snapshot(root: Path, commit: str) -> dict[str, list[str]]:
    completed = subprocess.run(
        ["git", "ls-tree", "-r", "-z", commit],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise ValueError("cannot read the delivery commit tree")
    snapshot: dict[str, list[str]] = {}
    for raw in completed.stdout.split(b"\0"):
        if not raw or b"\t" not in raw:
            continue
        metadata, encoded_path = raw.split(b"\t", 1)
        fields = metadata.decode("ascii", errors="replace").split()
        if len(fields) != 3:
            raise ValueError("the delivery commit tree is invalid")
        mode, _kind, object_id = fields
        path = encoded_path.decode("utf-8", errors="surrogateescape")
        snapshot[path] = [f"{mode} {object_id} 0"]
    return snapshot


def _campaign_finalize_receipt(
    campaign: Mapping[str, object], *, validate_artifacts: bool = True
) -> dict[str, object]:
    identity = campaign.get("finalize_receipt")
    if not isinstance(identity, dict):
        raise ValueError("the campaign has no finalization receipt")
    path_value = identity.get("path")
    key = identity.get("receipt_key")
    if not isinstance(path_value, str) or not isinstance(key, str):
        raise ValueError("the campaign finalization receipt identity is invalid")
    path = Path(path_value)
    if file_hash(path) != identity.get("file_sha256"):
        raise ValueError("the campaign finalization receipt file changed")
    receipt = _load_finalize_receipt(
        path, key, validate_artifacts=validate_artifacts
    )
    if receipt is None or receipt.get("receipt_sha256") != identity.get(
        "content_sha256"
    ):
        raise ValueError("the campaign finalization receipt content changed")
    return receipt


def _verify_source_models_append(
    root: Path,
    campaign: Mapping[str, object],
    commit: str,
    base_commit: str,
) -> None:
    relative = ".notes/source-models.md"
    base_text = _committed_file_text(
        root, relative, base_commit, allow_missing=True
    )
    committed_text = _committed_file_text(root, relative, commit)
    models = campaign.get("ruled_out_models")
    addresses = campaign.get("addresses")
    if not isinstance(models, list) or not all(
        isinstance(model, str) for model in models
    ):
        raise ValueError("the no-source campaign has invalid rejected models")
    if not isinstance(addresses, list) or not all(
        isinstance(address, str) for address in addresses
    ):
        raise ValueError("the no-source campaign has invalid targets")
    expected = _source_models_content(
        base_text if base_text else "# Source models\n",
        str(campaign.get("campaign_id", "")),
        str(campaign.get("ended_at", "")),
        str(campaign.get("subsystem", "")),
        str(campaign.get("mode", "")),
        list(addresses),
        str(campaign["resource"]) if campaign.get("resource") else None,
        list(models),
        str(campaign.get("note", "")),
    )
    if committed_text != expected:
        raise ValueError(
            "the delivery commit changed source-model history or its campaign append"
        )


def _verify_finalized_campaign_tree(
    root: Path,
    ledger_path: Path,
    campaign: Mapping[str, object],
    commit: str,
    base_commit: str,
    *,
    receipt: Mapping[str, object] | None = None,
) -> list[str]:
    receipt = (
        dict(receipt)
        if receipt is not None
        else _campaign_finalize_receipt(campaign, validate_artifacts=False)
    )
    key = receipt.get("key_payload")
    if not isinstance(key, dict):
        raise ValueError("the finalization receipt has no tree identity")
    before = key.get("campaign_repository_index_snapshot")
    after = key.get("repository_index_snapshot")
    if not isinstance(before, dict) or not isinstance(after, dict):
        raise ValueError("the finalization receipt predates committed-tree verification")
    expected_changes = set(source_worktree_changes(before, after))
    base_tree = _git_tree_snapshot(root, base_commit)
    commit_tree = _git_tree_snapshot(root, commit)
    committed_changes = set(source_worktree_changes(base_tree, commit_tree))
    try:
        ledger_relative = ledger_path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise ValueError("the delivery ledger is outside the repository") from error
    allowed_bookkeeping = {ledger_relative}
    if campaign.get("result") == "no-source":
        allowed_bookkeeping.add(".notes/source-models.md")
    unexpected = committed_changes - expected_changes - allowed_bookkeeping
    if unexpected:
        raise ValueError(
            "the delivery commit contains files outside the finalized campaign: "
            + ", ".join(sorted(unexpected)[:8])
        )
    missing = expected_changes - committed_changes
    if missing:
        raise ValueError(
            "the delivery commit omits finalized campaign files: "
            + ", ".join(sorted(missing)[:8])
        )
    changed_content: list[str] = []
    resolved_paths: list[str] = []
    for path in expected_changes:
        if base_tree.get(path) == before.get(path):
            if commit_tree.get(path) != after.get(path):
                changed_content.append(path)
        else:
            changed_content.append(path)
    if changed_content:
        raise ValueError(
            "the delivery commit changed finalized campaign content: "
            + ", ".join(sorted(changed_content)[:8])
        )
    if campaign.get("result") == "no-source":
        _verify_source_models_append(root, campaign, commit, base_commit)
    return sorted(resolved_paths)


def _delivery_receipt_hash(receipt: Mapping[str, object]) -> str:
    payload = dict(receipt)
    payload.pop("content_sha256", None)
    return _snapshot_hash(payload)


def _delivery_artifacts(root: Path, mode: str) -> list[dict[str, str]]:
    if mode == "meta":
        return []
    paths = [root / "build/toy2.exe", root / "build/toy2.pdb"]
    from tools.decomp_provenance import provenance_path

    code = root / "build/decomp-current-report.json"
    paths.extend((code, provenance_path(code)))
    if mode in ("data", "resource"):
        data = root / "build/decomp-current-data-report.json"
        paths.extend((data, provenance_path(data)))
    return [_finalize_artifact(path) for path in paths if path.is_file()]


def _receipt_finalizer_artifact(
    receipt: Mapping[str, object], source: Path
) -> Path:
    """Return the immutable finalizer output for one canonical path."""

    source = source.resolve()
    receipt_version = receipt.get("receipt_version")
    if receipt_version == FINALIZE_RECEIPT_VERSION:
        descriptor = _immutable_artifact_map(
            receipt, validate_content=False
        ).get(source)
        if descriptor is None:
            raise ValueError(
                f"the finalization receipt has no immutable artifact for {source}"
            )
        artifact = Path(str(descriptor["path"]))
        saved_hash = descriptor["sha256"]
    elif receipt_version == LEGACY_FINALIZE_RECEIPT_VERSION:
        values = receipt.get("artifacts")
        if not isinstance(values, list):
            raise ValueError("the finalization receipt has no artifact list")
        matches = [
            value
            for value in values
            if isinstance(value, dict)
            and isinstance(value.get("path"), str)
            and Path(str(value["path"])).resolve() == source
        ]
        if len(matches) != 1 or not isinstance(matches[0].get("sha256"), str):
            raise ValueError(f"the finalization receipt has no artifact for {source}")
        artifact = source
        saved_hash = matches[0]["sha256"]
    else:
        raise ValueError("the finalization receipt version is invalid")
    try:
        current_hash = file_hash(artifact)
    except ValueError as error:
        raise ValueError(f"the finalized artifact is missing: {source}") from error
    if current_hash != saved_hash:
        raise ValueError(f"the finalized artifact changed: {source}")
    return artifact


def _matching_resource_bytes(original: Path, recompiled: Path) -> dict[str, int]:
    resources: dict[str, int] = defaultdict(int)
    for row in resource_rows(original, recompiled):
        path = row.get("path")
        if isinstance(path, list) and row.get("identity_match") is True:
            resources[",".join(str(value) for value in path)] += int(
                row.get("size", 0)
            )
    return dict(resources)


def _validate_resource_reference(
    prior: Mapping[str, object], current: Mapping[str, int]
) -> None:
    for resource_key, prior_value in prior.items():
        if not isinstance(prior_value, int) or current.get(
            str(resource_key), 0
        ) < prior_value:
            raise ValueError(f"integrated resource {resource_key} regressed")


def _delivery_reference(
    receipt: Mapping[str, object],
    campaign: Mapping[str, object],
    root: Path,
) -> dict[str, object]:
    """Capture finalization outcomes before integrated validation replaces reports."""

    if campaign.get("result") != "source" or campaign.get("mode") == "meta":
        return {}
    from tools.decomp_status import read_match_statuses

    code_report = _receipt_step_artifact(receipt, "code_report")
    statuses = read_match_statuses(code_report)
    reference: dict[str, object] = {
        "code": {
            f"0x{address:08X}": {
                "matching": status.matching,
                "effective": status.effective,
            }
            for address, status in statuses.items()
        }
    }
    mode = str(campaign.get("mode", ""))
    if mode in ("data", "resource"):
        data_report = _receipt_step_artifact(receipt, "data_report")
        reference["data_payload"] = _read_json_object(
            data_report, "finalized typed-data report"
        )
        reference["data"] = {
            f"0x{address:08X}": value
            for address, value in initialized_data_by_address(data_report).items()
        }
        reference["initialized_bytes"] = initialized_data_bytes(data_report)
    if mode == "resource":
        finalized_executable = _receipt_finalizer_artifact(
            receipt, root / "build/toy2.exe"
        )
        reference["resources"] = _matching_resource_bytes(
            root / "original/toy2.exe", finalized_executable
        )
    return reference


def _data_regression_problems(
    before: Mapping[str, object], after: Mapping[str, object]
) -> list[str]:
    """Return all typed-data evidence that regressed after integration."""

    from tools.decomp_verify import data_sections

    problems: list[str] = []

    def variables(payload: Mapping[str, object]) -> dict[int, Mapping[str, object]]:
        group = payload.get("variables")
        rows = group.get("variables", []) if isinstance(group, Mapping) else []
        values: dict[int, Mapping[str, object]] = {}
        if not isinstance(rows, list):
            return values
        for row in rows:
            if not isinstance(row, Mapping):
                continue
            address = _address_int(row.get("original_address"))
            if address is not None:
                values[address] = row
        return values

    before_variables = variables(before)
    after_variables = variables(after)
    for address, old in before_variables.items():
        new = after_variables.get(address)
        if new is None:
            problems.append(f"0x{address:08X}: data item disappeared")
            continue
        if float(new.get("matched_bytes", 0)) + 1e-12 < float(
            old.get("matched_bytes", 0)
        ):
            problems.append(f"0x{address:08X}: data bytes regressed")
        elif float(new.get("score", 0)) + 1e-12 < float(old.get("score", 0)):
            problems.append(f"0x{address:08X}: data score regressed")

    before_group = before.get("variables")
    after_group = after.get("variables")
    if not isinstance(before_group, Mapping) or not isinstance(after_group, Mapping):
        problems.append("typed-data totals are missing")
    elif float(after_group.get("explained_bytes", 0)) + 1e-12 < float(
        before_group.get("explained_bytes", 0)
    ):
        problems.append("initialized-data explained bytes regressed")

    before_sections = data_sections(dict(before))
    after_sections = data_sections(dict(after))
    for name, old in before_sections.items():
        new = after_sections.get(name)
        if new is None:
            problems.append(f"{name}: scored data section disappeared")
            continue
        if float(new.get("explained_bytes", 0)) + 1e-12 < float(
            old.get("explained_bytes", 0)
        ):
            problems.append(f"{name}: explained section bytes regressed")
        elif float(new.get("score", 0)) + 1e-12 < float(old.get("score", 0)):
            problems.append(f"{name}: data section score regressed")

    for group_name in ("vtables", "imports", "relocations"):
        old = before.get(group_name, {})
        new = after.get(group_name, {})
        if not isinstance(old, Mapping):
            problems.append(f"{group_name}: finalized data reference is invalid")
            continue
        if not isinstance(new, Mapping):
            problems.append(f"{group_name}: integrated data evidence is invalid")
            continue
        key = "explained_bytes" if group_name == "vtables" else "matched_entries"
        if float(new.get(key, 0)) + 1e-12 < float(old.get(key, 0)):
            problems.append(f"{group_name}: data evidence regressed")
    return problems


def _delivery_validation_command_plan(
    campaign: Mapping[str, object], root: Path
) -> list[list[str]]:
    """Return the exact external command plan for integrated validation."""

    mode = str(campaign.get("mode", ""))
    result = str(campaign.get("result", ""))
    if mode == "meta":
        inputs = _meta_input_hashes(root)
        values = inputs["commands"]
        assert isinstance(values, dict)
        return [list(values[name]) for name in ("tests", "map", "diff")]
    if result != "source":
        return [["git", "diff", "--check"]]

    standard = standard_finalize_commands(root)
    commands = [list(command) for command in standard["build"]]
    commands.extend(list(command) for command in standard["code_report"])
    if mode in ("data", "resource"):
        commands.extend(list(command) for command in standard["data_report"])
    commands.extend(
        (
            [sys.executable, "tools/ghidra_sync.py", "check"],
            ["git", "diff", "--check"],
        )
    )
    return commands


def _validate_delivery_command_results(
    validation: Mapping[str, object],
    campaign: Mapping[str, object],
    root: Path,
) -> None:
    """Reject a receipt without the exact successful external command plan."""

    results = validation.get("commands")
    expected = _delivery_validation_command_plan(campaign, root)
    if not isinstance(results, list) or len(results) != len(expected):
        raise ValueError("the delivery receipt command results are invalid")
    expected_keys = {"command", "returncode", "stdout_sha256", "stderr_sha256"}
    for result, command in zip(results, expected, strict=True):
        if (
            not isinstance(result, dict)
            or set(result) != expected_keys
            or result.get("command") != command
            or type(result.get("returncode")) is not int
            or result.get("returncode") != 0
            or not isinstance(result.get("stdout_sha256"), str)
            or re.fullmatch(r"[0-9a-f]{64}", str(result["stdout_sha256"])) is None
            or not isinstance(result.get("stderr_sha256"), str)
            or re.fullmatch(r"[0-9a-f]{64}", str(result["stderr_sha256"])) is None
        ):
            raise ValueError("the delivery receipt command results are invalid")


def _run_delivery_validation(
    campaign: Mapping[str, object],
    root: Path,
    reference: Mapping[str, object] | None = None,
) -> dict[str, object]:
    mode = str(campaign.get("mode", ""))
    result = str(campaign.get("result", ""))
    commands: list[list[str]] = []
    results: list[dict[str, object]] = []

    def run(command: list[str], *, cwd: Path | None = None) -> None:
        completed = _standard_command(command, root, cwd=cwd)
        commands.append(command)
        results.append(
            {
                "command": command,
                "returncode": completed.returncode,
                "stdout_sha256": hashlib.sha256(completed.stdout.encode()).hexdigest(),
                "stderr_sha256": hashlib.sha256(completed.stderr.encode()).hexdigest(),
            }
        )

    if mode == "meta":
        inputs = _meta_input_hashes(root)
        values = inputs["commands"]
        assert isinstance(values, dict)
        for name in ("tests", "map", "diff"):
            run(list(values[name]))
    elif result == "source":
        from tools.decomp_provenance import (
            _artifact_state,
            current_identity,
            provenance_path,
            seal_report,
            validate_report,
        )

        standard = standard_finalize_commands(root)
        build = root / "build"
        for index, command in enumerate(standard["build"]):
            run(command, cwd=build if index else root)
        code_report = build / "decomp-current-report.json"
        identity = current_identity(root)
        code_before = _artifact_state(code_report)
        run(standard["code_report"][0], cwd=build)
        seal_report(
            code_report,
            root=root,
            expected_identity=identity,
            expected_artifact=code_before,
        )
        if mode in ("data", "resource"):
            data_report = build / "decomp-current-data-report.json"
            identity = current_identity(root)
            data_before = _artifact_state(data_report)
            run(standard["data_report"][0])
            seal_report(
                data_report,
                root=root,
                expected_identity=identity,
                expected_artifact=data_before,
            )
            validate_report(data_report, root=root)
        validate_report(code_report, root=root)
        sizes = read_function_sizes(
            root / "tools/Resources/functions_map.txt",
            root / "build/decomp-function-sizes.json",
        )
        scan_state = {
            "mode": mode,
            "function_sizes": _size_snapshot(sizes),
        }
        metrics, scan = _standard_source_scan(
            scan_state, code_report, staged=False
        )
        if scan["new_errors"] or scan["new_warnings"] or scan["stale"]:
            raise ValueError("the integrated source has new source-debt findings")
        target_deltas = campaign.get("target_deltas")
        if not isinstance(target_deltas, dict):
            raise ValueError("the campaign has no target outcome evidence")
        active = campaign.get("active_addresses", campaign.get("addresses", []))
        if not isinstance(active, list):
            raise ValueError("the campaign has invalid active targets")
        from tools.decomp_status import read_match_statuses

        statuses = read_match_statuses(code_report)
        prior_code = reference.get("code") if isinstance(reference, Mapping) else None
        if not isinstance(prior_code, dict):
            raise ValueError("the delivery has no finalized code reference")
        for address_text, prior in prior_code.items():
            if not isinstance(prior, dict):
                raise ValueError("the delivery code reference is invalid")
            address = int(str(address_text), 16)
            current_status = statuses.get(address)
            prior_matching = prior.get("matching")
            if not isinstance(prior_matching, (int, float)):
                raise ValueError("the delivery code reference is invalid")
            prior_effective = bool(prior.get("effective"))
            prior_score = 1.0 if prior_effective else float(prior_matching)
            current_score = (
                1.0
                if current_status is not None
                and (current_status.effective or current_status.exact)
                else current_status.matching
                if current_status is not None
                else -1.0
            )
            if current_score + 1e-12 < prior_score:
                raise ValueError(f"integrated function {address_text} regressed")

        current_data: dict[int, float] = {}
        if mode in ("data", "resource"):
            prior_data_payload = (
                reference.get("data_payload")
                if isinstance(reference, Mapping)
                else None
            )
            if not isinstance(prior_data_payload, dict):
                raise ValueError("the delivery has no finalized data reference")
            current_data_payload = _read_json_object(
                data_report, "integrated typed-data report"
            )
            data_problems = _data_regression_problems(
                prior_data_payload, current_data_payload
            )
            if data_problems:
                raise ValueError(
                    "integrated typed-data evidence regressed: "
                    + "; ".join(data_problems[:8])
                )
            current_data = initialized_data_by_address(data_report)

        if mode in ("coverage", "refinement"):
            current_code = effective_code_by_address(code_report, sizes)
            implemented: set[int] = set()
            for path in _source_paths(root):
                source_path = root / path
                if not source_path.is_file():
                    continue
                text = source_path.read_text(encoding="utf-8", errors="ignore")
                implemented.update(
                    int(address, 16)
                    for kind, address in SOURCE_ANNOTATION_RE.findall(text)
                    if kind == "FUNCTION"
                )
            for address_text in active:
                address = int(str(address_text), 16)
                delta = target_deltas.get(address_text)
                if not isinstance(delta, dict) or not isinstance(
                    delta.get("effective_after"), (int, float)
                ):
                    raise ValueError(
                        "the campaign target outcome predates delivery verification"
                    )
                if current_code.get(address, 0.0) + 1e-6 < float(
                    delta["effective_after"]
                ):
                    raise ValueError(
                        f"integrated target {address_text} lost retained code bytes"
                    )
                status = statuses.get(address)
                if address not in implemented or status is None or (
                    not status.effective and status.matching < 0.5
                ):
                    raise ValueError(
                        f"integrated target {address_text} is not a valid function"
                    )
                if _campaign_lane(campaign) == "closure" and not (
                    status.effective
                    or math.isclose(status.matching, 1.0, abs_tol=1e-12)
                ):
                    raise ValueError(
                        f"integrated closure target {address_text} is not terminal"
                    )
        elif mode == "data":
            for address_text in active:
                delta = target_deltas.get(address_text)
                if not isinstance(delta, dict) or not isinstance(
                    delta.get("initialized_after"), (int, float)
                ):
                    raise ValueError(
                        "the campaign data outcome predates delivery verification"
                    )
                address = int(str(address_text), 16)
                if current_data.get(address, 0.0) + 1e-6 < float(
                    delta["initialized_after"]
                ):
                    raise ValueError(
                        f"integrated data target {address_text} lost explained bytes"
                    )
        elif mode == "resource":
            expected = campaign.get("resource_after")
            resource = campaign.get("resource")
            if not isinstance(expected, dict) or not isinstance(resource, str):
                raise ValueError("the campaign has no resource outcome evidence")
            current = selected_evidence(
                resource_rows(root / "original/toy2.exe", root / "build/toy2.exe"),
                parse_resource(resource),
            )
            if int(current.get("explained_bytes", 0)) < int(
                expected.get("explained_bytes", 0)
            ):
                raise ValueError("the integrated resource lost explained bytes")
            prior_resources = (
                reference.get("resources")
                if isinstance(reference, Mapping)
                else None
            )
            if isinstance(prior_resources, dict):
                current_resources = _matching_resource_bytes(
                    root / "original/toy2.exe", root / "build/toy2.exe"
                )
                _validate_resource_reference(prior_resources, current_resources)
        current_metrics = metrics.get("metrics")
        if not isinstance(current_metrics, dict):
            raise ValueError("delivery source metrics are invalid")
        if int(current_metrics.get("implemented", 0)) < int(
            campaign.get("implemented_after", 0)
        ) or int(current_metrics.get("terminal", 0)) < int(
            campaign.get("terminal_after", 0)
        ) or int(current_metrics.get("source_debt", 0)) > int(
            campaign.get("source_debt_after", 0)
        ):
            raise ValueError("the integrated repository lost campaign progress")
        if float(current_metrics.get("terminal_bytes", 0.0)) + 1e-6 < float(
            campaign.get("terminal_bytes_after", 0.0)
        ) or float(current_metrics.get("effective_bytes", 0.0)) + 1e-6 < float(
            campaign.get("effective_bytes_after", 0.0)
        ):
            raise ValueError("the integrated repository lost retained coverage bytes")
        run([sys.executable, "tools/ghidra_sync.py", "check"])
        run(["git", "diff", "--check"])
        inputs = _standard_input_hashes(root)
    else:
        run(["git", "diff", "--check"])
        inputs = _standard_input_hashes(root)
    if commands != _delivery_validation_command_plan(campaign, root):
        raise ValueError("the delivery validation command plan changed")
    return {
        "commands": results,
        "inputs": inputs,
        "artifacts": _delivery_artifacts(root, mode),
    }


def _delivery_relevant_changes(root: Path, ledger_path: Path) -> list[str]:
    try:
        allowed = ledger_path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        allowed = ""
    paths: set[str] = set()
    for command in (
        ["git", "diff", "--name-only", "-z"],
        ["git", "diff", "--cached", "--name-only", "-z"],
        ["git", "ls-files", "--others", "--exclude-standard", "-z"],
    ):
        completed = subprocess.run(command, cwd=root, check=False, capture_output=True)
        if completed.returncode != 0:
            raise ValueError("cannot inspect delivery repository changes")
        paths.update(
            value.decode("utf-8", errors="surrogateescape")
            for value in completed.stdout.split(b"\0")
            if value
        )
    return sorted(
        path
        for path in paths
        if path != allowed and _finalizer_relevant_path(path)
    )


def create_delivery_receipt(
    ledger_path: Path,
    campaign_id: str,
    commit: str,
    base_commit: str,
    *,
    root: Path = ROOT,
    cache_root: Path | None = None,
    now: datetime | None = None,
) -> dict[str, object]:
    """Validate the integrated commit and write its immutable delivery receipt."""

    root = root.resolve()
    if cache_root is None:
        cache_root = root / "build" / "decomp-cache" / "delivery"
    records = _read_records(ledger_path)
    campaign = next(
        (
            item
            for item in records
            if item.get("record_type", "campaign") == "campaign"
            and item.get("campaign_id") == campaign_id
        ),
        None,
    )
    if campaign is None:
        raise ValueError("--campaign-id does not identify a completed campaign")
    source_commit = _git_commit(root, commit, "--commit")
    integrated_base = _git_commit(root, base_commit, "--base-commit")
    _validate_campaign_record_receipt(campaign)
    finalize_receipt = _campaign_finalize_receipt(
        campaign, validate_artifacts=True
    )
    if _git_head(root) != source_commit:
        raise ValueError("--commit must be the current delivery HEAD")
    if _git_first_parent(root, source_commit) != integrated_base:
        raise ValueError("--base-commit must be the first parent of --commit")
    campaign_head = campaign.get("campaign_head")
    if isinstance(campaign_head, str):
        original_head = _git_commit(root, campaign_head, "campaign HEAD")
        if not _git_is_ancestor(root, original_head, integrated_base):
            raise ValueError("the integrated base does not contain the campaign HEAD")
    _verify_campaign_in_commit(
        root, ledger_path, campaign, source_commit, integrated_base
    )
    resolved_paths = _verify_finalized_campaign_tree(
        root,
        ledger_path,
        campaign,
        source_commit,
        integrated_base,
        receipt=finalize_receipt,
    )
    dirty = _delivery_relevant_changes(root, ledger_path)
    if dirty:
        raise ValueError(
            "commit or restore delivery files before verification: "
            + ", ".join(dirty[:8])
        )
    integrated_tree = _git_tree_snapshot(root, source_commit)
    _validate_relevant_git_modes(integrated_tree, "delivery commit tree")
    if campaign.get("result") == "source" and campaign.get("mode") != "meta":
        _validate_configured_build_sources(
            root, integrated_tree, "delivery commit tree"
        )
    reference = _delivery_reference(finalize_receipt, campaign, root)
    validation = _run_delivery_validation(campaign, root, reference)
    _validate_delivery_command_results(validation, campaign, root)
    if _git_head(root) != source_commit:
        raise ValueError("the delivery HEAD changed during validation")
    _verify_campaign_in_commit(
        root, ledger_path, campaign, source_commit, integrated_base
    )
    current_finalize_receipt = _campaign_finalize_receipt(
        campaign, validate_artifacts=False
    )
    if current_finalize_receipt != finalize_receipt:
        raise ValueError("the finalization receipt changed during delivery validation")
    current_resolved_paths = _verify_finalized_campaign_tree(
        root,
        ledger_path,
        campaign,
        source_commit,
        integrated_base,
        receipt=finalize_receipt,
    )
    if current_resolved_paths != resolved_paths:
        raise ValueError("the delivery commit tree changed during validation")
    dirty = _delivery_relevant_changes(root, ledger_path)
    if dirty:
        raise ValueError("delivery validation changed a tracked workflow file")
    from tools.decomp_provenance import validation_tool_identity

    receipt: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "receipt_version": DELIVERY_RECEIPT_VERSION,
        "record_type": "delivery-receipt",
        "status": "passed",
        "created_at": timestamp(now or utc_now()),
        "campaign_id": campaign_id,
        "campaign_fingerprint": _record_fingerprint(campaign),
        "campaign_record_sha256": _snapshot_hash(campaign),
        "source_commit": source_commit,
        "base_commit": integrated_base,
        "head": source_commit,
        "resolved_paths": resolved_paths,
        "validation": validation,
        "validation_tool_identity": validation_tool_identity(
            root, require_reccmp_user=campaign.get("mode") != "meta"
        ),
    }
    receipt["content_sha256"] = _delivery_receipt_hash(receipt)
    path = cache_root / campaign_id / f"{receipt['content_sha256']}.json"
    existing = _read_json_object(path, "delivery receipt") if path.exists() else None
    if existing is not None and existing != receipt:
        raise ValueError("the delivery receipt cache contains different content")
    if existing is None:
        write_text(path, json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    result = dict(receipt)
    result["path"] = str(path.resolve())
    return result


def _validated_delivery_receipt(
    path: Path,
    campaign: dict[str, object],
    ledger_path: Path,
    *,
    root: Path,
) -> dict[str, object]:
    receipt = _read_json_object(path, "delivery receipt")
    if (
        receipt.get("schema_version") != SCHEMA_VERSION
        or receipt.get("receipt_version") != DELIVERY_RECEIPT_VERSION
        or receipt.get("record_type") != "delivery-receipt"
        or receipt.get("status") != "passed"
        or receipt.get("content_sha256") != _delivery_receipt_hash(receipt)
    ):
        raise ValueError("the delivery receipt is invalid")
    if (
        receipt.get("campaign_id") != campaign.get("campaign_id")
        or receipt.get("campaign_fingerprint") != _record_fingerprint(campaign)
        or receipt.get("campaign_record_sha256") != _snapshot_hash(campaign)
    ):
        raise ValueError("the delivery receipt belongs to another campaign")
    content_hash = str(receipt["content_sha256"])
    canonical_root = (root / "build" / "decomp-cache" / "delivery").resolve()
    canonical_path = (
        canonical_root
        / str(campaign["campaign_id"])
        / f"{content_hash}.json"
    )
    if path.resolve() != canonical_path:
        raise ValueError(
            "the delivery receipt is not in its canonical content-addressed path"
        )
    commit = _git_commit(root, str(receipt.get("source_commit", "")), "receipt commit")
    base = _git_commit(root, str(receipt.get("base_commit", "")), "receipt base commit")
    if (
        receipt.get("head") != commit
        or _git_head(root) != commit
        or _git_first_parent(root, commit) != base
    ):
        raise ValueError("the delivery receipt commit identity is stale")
    _validate_campaign_record_receipt(campaign)
    _verify_campaign_in_commit(root, ledger_path, campaign, commit, base)
    resolved_paths = _verify_finalized_campaign_tree(
        root, ledger_path, campaign, commit, base
    )
    if receipt.get("resolved_paths") != resolved_paths:
        raise ValueError("the delivery receipt conflict resolution identity is stale")
    validation = receipt.get("validation")
    artifacts = validation.get("artifacts") if isinstance(validation, dict) else None
    if not isinstance(artifacts, list):
        raise ValueError("the delivery receipt has no validation artifacts")
    for artifact in artifacts:
        if (
            not isinstance(artifact, dict)
            or not isinstance(artifact.get("path"), str)
            or not isinstance(artifact.get("sha256"), str)
            or file_hash(Path(str(artifact["path"]))) != artifact["sha256"]
        ):
            raise ValueError("a delivery validation artifact changed")
    mode = str(campaign.get("mode", ""))
    expected_inputs = (
        _meta_input_hashes(root) if mode == "meta" else _standard_input_hashes(root)
    )
    if not isinstance(validation, dict) or validation.get("inputs") != expected_inputs:
        raise ValueError("a delivery validation input changed")
    _validate_delivery_command_results(validation, campaign, root)
    if mode != "meta" and campaign.get("result") == "source":
        from tools.decomp_provenance import validate_report

        validate_report(root / "build/decomp-current-report.json", root=root)
        if mode in ("data", "resource"):
            validate_report(root / "build/decomp-current-data-report.json", root=root)
    from tools.decomp_provenance import validation_tool_identity

    if receipt.get("validation_tool_identity") != validation_tool_identity(
        root, require_reccmp_user=mode != "meta"
    ):
        raise ValueError("a delivery validation tool changed")
    dirty = _delivery_relevant_changes(root, ledger_path)
    if dirty:
        raise ValueError("a delivery source or workflow file changed after validation")
    return receipt


def _verify_remote_contains(root: Path, commit: str) -> None:
    fetched = subprocess.run(
        [
            "git",
            "fetch",
            "--quiet",
            "--no-tags",
            "origin",
            "+refs/heads/agent/continuous:refs/remotes/origin/agent/continuous",
        ],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    if fetched.returncode != 0:
        raise ValueError("cannot refresh origin/agent/continuous for delivery")
    remote = subprocess.run(
        ["git", "rev-parse", "--verify", "refs/remotes/origin/agent/continuous^{commit}"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    remote_commit = remote.stdout.strip().lower()
    if remote.returncode != 0 or not _git_is_ancestor(root, commit, remote_commit):
        raise ValueError("origin/agent/continuous does not contain the delivery commit")


def record_delivery(
    ledger_path: Path,
    campaign_id: str,
    status: str,
    *,
    artifact_sha256: str | None = None,
    receipt_sha256: str | None = None,
    delivery_receipt_path: Path | None = None,
    base_commit: str | None = None,
    commit: str | None = None,
    note: str | None = None,
    now: datetime | None = None,
    delivery_id: str | None = None,
    root: Path = ROOT,
) -> dict[str, object]:
    if status not in DELIVERY_STATUSES:
        raise ValueError(f"unknown delivery status: {status}")
    clean_campaign_id = _single_line(campaign_id, "--campaign-id")
    records = _read_records(ledger_path)
    campaign = next(
        (
            item
            for item in records
            if item.get("record_type", "campaign") == "campaign"
            and item.get("campaign_id") == clean_campaign_id
        ),
        None,
    )
    if campaign is None:
        raise ValueError("--campaign-id does not identify a completed campaign")
    _validate_campaign_record_receipt(campaign)
    delivery_records = [
        item
        for item in records
        if item.get("record_type") == "delivery"
        and item.get("campaign_id") == clean_campaign_id
    ]
    prior_statuses = [str(item.get("status", "")) for item in delivery_records]
    recorded = now or utc_now()
    recorded_at = timestamp(recorded)
    ended = _record_datetime(campaign, "ended_at") or _record_datetime(
        campaign, "timestamp"
    )
    if ended is not None and recorded < ended:
        raise ValueError("the delivery time is before the campaign end time")
    if delivery_records:
        previous_time = _record_datetime(delivery_records[-1], "timestamp")
        if previous_time is None or recorded < previous_time:
            raise ValueError("delivery timestamps are out of order")
    repeated_status = any(item.get("status") == status for item in delivery_records)
    if (
        prior_statuses
        and prior_statuses[-1] in ("pushed", "rejected")
        and not repeated_status
    ):
        raise ValueError("the campaign delivery is already terminal")
    if status != "rejected" and not repeated_status:
        expected_index = len(prior_statuses)
        if (
            expected_index >= len(DELIVERY_SEQUENCE)
            or status != DELIVERY_SEQUENCE[expected_index]
            or prior_statuses != list(DELIVERY_SEQUENCE[:expected_index])
        ):
            expected = (
                DELIVERY_SEQUENCE[expected_index]
                if expected_index < len(DELIVERY_SEQUENCE)
                else "no further status"
            )
            raise ValueError(f"the next delivery status must be {expected}")
    artifact_hash = _validate_sha256(artifact_sha256, "--artifact-sha256")
    receipt_hash = _validate_sha256(receipt_sha256, "--receipt-sha256")
    clean_base = _validate_commit_id(base_commit, "--base-commit")
    clean_commit = _validate_commit_id(commit, "--commit")
    delivery_receipt: dict[str, object] | None = None
    if status in {"integrated", "committed", "pushed"}:
        if delivery_receipt_path is None:
            raise ValueError(
                f"a {status} delivery needs --delivery-receipt"
            )
        delivery_receipt = _validated_delivery_receipt(
            delivery_receipt_path,
            campaign,
            ledger_path,
            root=root.resolve(),
        )
        delivery_hash = str(delivery_receipt["content_sha256"])
        if receipt_hash is not None and receipt_hash != delivery_hash:
            raise ValueError(
                "--receipt-sha256 disagrees with the delivery receipt"
            )
        receipt_hash = delivery_hash
        receipt_commit = str(delivery_receipt["source_commit"])
        receipt_base = str(delivery_receipt["base_commit"])
        if clean_commit is not None and _git_commit(
            root, clean_commit, "--commit"
        ) != receipt_commit:
            raise ValueError("--commit disagrees with the delivery receipt")
        if clean_base is not None and _git_commit(
            root, clean_base, "--base-commit"
        ) != receipt_base:
            raise ValueError("--base-commit disagrees with the delivery receipt")
        clean_commit = receipt_commit
        clean_base = receipt_base
    else:
        if delivery_receipt_path is not None:
            raise ValueError(
                "--delivery-receipt requires an integrated, committed, or pushed status"
            )
        finalize_identity = campaign.get("finalize_receipt")
        expected_receipt = (
            finalize_identity.get("content_sha256")
            if isinstance(finalize_identity, dict)
            else None
        )
        if isinstance(expected_receipt, str):
            expected_receipt = _validate_sha256(
                expected_receipt, "the campaign finalization receipt"
            )
            if receipt_hash is not None and receipt_hash != expected_receipt:
                raise ValueError(
                    "--receipt-sha256 disagrees with the campaign finalization receipt"
                )
            receipt_hash = expected_receipt
        elif receipt_hash is not None:
            raise ValueError(
                "the completed campaign has no finalization receipt to bind"
            )
    if status != "rejected" and artifact_hash is None and receipt_hash is None:
        raise ValueError("a delivery event needs a bound artifact or receipt SHA-256 value")
    clean_note = _clean_optional_line(note, "--note") or ""
    existing_status = next(
        (item for item in delivery_records if item.get("status") == status),
        None,
    )
    if existing_status is not None:
        supplied_identity = {
            "artifact_sha256": artifact_hash,
            "receipt_sha256": receipt_hash,
            "base_commit": clean_base,
            "commit": clean_commit,
            "delivery_receipt_path": (
                str(delivery_receipt_path.resolve())
                if delivery_receipt_path is not None
                else None
            ),
        }
        for field, supplied in supplied_identity.items():
            if supplied is not None and existing_status.get(field) != supplied:
                raise ValueError(
                    f"--{field.replace('_', '-')} disagrees with the existing delivery"
                )
        if note is not None and existing_status.get("note", "") != clean_note:
            raise ValueError("--note disagrees with the existing delivery")
        if delivery_id is not None and existing_status.get("delivery_id") != delivery_id:
            raise ValueError("delivery_id disagrees with the existing delivery")
        return dict(existing_status)
    if status == "integrated" and (clean_base is None or clean_commit is None):
        raise ValueError("an integrated delivery needs --base-commit and --commit")
    if status in ("committed", "pushed") and clean_commit is None:
        raise ValueError(f"a {status} delivery needs --commit")
    if status == "pushed":
        prior_commit = delivery_records[-1].get("commit") if delivery_records else None
        if clean_commit != prior_commit:
            raise ValueError("the pushed commit disagrees with the committed delivery")
        _verify_remote_contains(root.resolve(), clean_commit)
    phase_name = {"committed": "commit", "pushed": "push"}.get(status, status)
    item: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "delivery",
        "delivery_id": delivery_id or str(uuid.uuid4()),
        "campaign_id": clean_campaign_id,
        "timestamp": recorded_at,
        "status": status,
        "phase": phase_name,
        "phase_timestamps": {phase_name: recorded_at},
        "mode": campaign.get("mode"),
        "lane": _campaign_lane(campaign),
        "addresses": campaign.get("addresses", []),
        "resource": campaign.get("resource"),
        "artifact_sha256": artifact_hash,
        "receipt_sha256": receipt_hash,
        "base_commit": clean_base,
        "commit": clean_commit,
        "delivery_receipt_path": (
            str(delivery_receipt_path.resolve())
            if delivery_receipt_path is not None
            else None
        ),
        "note": clean_note,
    }
    append_record(ledger_path, item, expected_records=records)
    return item


def _finalize_receipt_hash(receipt: Mapping[str, object]) -> str:
    payload = dict(receipt)
    payload.pop("receipt_sha256", None)
    return _snapshot_hash(payload)


def _finalize_artifact(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise ValueError(f"a finalization artifact does not exist: {path}")
    return {"path": str(path.resolve()), "sha256": file_hash(path)}


def _step_artifact_descriptors(
    step: Mapping[str, object], name: str
) -> list[dict[str, str]]:
    values = step.get("artifacts")
    if values is None:
        path_value = step.get("path")
        saved_hash = step.get("sha256")
        if isinstance(path_value, str) and isinstance(saved_hash, str):
            return [{"path": path_value, "sha256": saved_hash}]
        raise ValueError(
            f"the finalization receipt has no {name.replace('_', '-')} artifacts"
        )
    if not isinstance(values, list) or not values:
        raise ValueError(
            f"the finalization receipt has no {name.replace('_', '-')} artifacts"
        )
    descriptors: list[dict[str, str]] = []
    for value in values:
        if not isinstance(value, dict):
            raise ValueError(
                f"the finalization receipt has invalid {name.replace('_', '-')} artifacts"
            )
        path_value = value.get("path")
        saved_hash = value.get("sha256")
        if not isinstance(path_value, str) or not isinstance(saved_hash, str):
            raise ValueError(
                f"the finalization receipt has invalid {name.replace('_', '-')} artifacts"
            )
        descriptors.append({"path": path_value, "sha256": saved_hash})
    return descriptors


def _primary_report_descriptor(
    step: Mapping[str, object], name: str
) -> dict[str, str]:
    descriptors = _step_artifact_descriptors(step, name)
    reports = [
        item
        for item in descriptors
        if not item["path"].endswith(".provenance.json")
    ]
    if len(reports) != 1:
        raise ValueError(
            f"the finalization receipt has no unique {name.replace('_', '-')} artifact"
        )
    return reports[0]


def _cache_finalize_artifact(
    source: Path,
    cache_root: Path,
    *,
    expected_sha256: str | None = None,
) -> dict[str, str]:
    try:
        content = source.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read a finalization artifact: {source}: {error}") from error
    digest = hashlib.sha256(content).hexdigest()
    if expected_sha256 is not None and digest != expected_sha256:
        raise ValueError(f"a finalization artifact changed before it was cached: {source}")
    cached = (cache_root / "artifacts" / "sha256" / digest).resolve()
    if cached.exists():
        if not cached.is_file() or file_hash(cached) != digest:
            raise ValueError("the finalization artifact cache has different content")
    else:
        cached.parent.mkdir(parents=True, exist_ok=True)
        temporary = cached.with_name(f".{cached.name}.{uuid.uuid4().hex}.tmp")
        try:
            temporary.write_bytes(content)
            temporary.replace(cached)
        finally:
            if temporary.exists():
                temporary.unlink()
    return {
        "path": str(cached),
        "sha256": digest,
        "source_path": str(source.resolve()),
    }


def _freeze_finalize_artifacts(
    artifacts: list[dict[str, str]],
    step_results: Mapping[str, Mapping[str, object]],
    cache_root: Path,
    *,
    require_reports: bool,
) -> list[dict[str, str]]:
    """Copy all finalizer outputs into the receipt content store."""

    from tools.decomp_provenance import provenance_path, validate_report_artifact

    immutable: dict[Path, dict[str, str]] = {}

    def cache(descriptor: Mapping[str, str]) -> dict[str, str]:
        source = Path(descriptor["path"]).resolve()
        cached = _cache_finalize_artifact(
            source,
            cache_root,
            expected_sha256=descriptor["sha256"],
        )
        previous = immutable.get(source)
        if previous is not None and previous != cached:
            raise ValueError(f"a finalization artifact has two identities: {source}")
        immutable[source] = cached
        return cached

    for descriptor in artifacts:
        cache(descriptor)
    for name in ("code_report", "data_report"):
        step = step_results.get(name)
        if not isinstance(step, Mapping):
            if require_reports:
                raise ValueError(
                    f"the finalization receipt has no {name.replace('_', '-')} artifact"
                )
            continue
        report_descriptor = _primary_report_descriptor(step, name)
        report = Path(report_descriptor["path"]).resolve()
        sidecar = provenance_path(report).resolve()
        sealed = validate_report_artifact(report)
        cached_report = cache(report_descriptor)
        descriptors = _step_artifact_descriptors(step, name)
        sidecar_descriptor = next(
            (
                item
                for item in descriptors
                if Path(item["path"]).resolve() == sidecar
            ),
            None,
        )
        if sidecar_descriptor is None:
            sidecar_descriptor = {
                "path": str(sidecar),
                "sha256": file_hash(sidecar),
            }
        cached_sidecar = cache(sidecar_descriptor)
        if sealed.get("artifact") != {
            "path": str(report),
            "sha256": cached_report["sha256"],
        }:
            raise ValueError(
                f"the cached {name.replace('_', '-')} provenance is invalid"
            )
        if cached_sidecar["source_path"] != str(sidecar):
            raise ValueError(
                f"the cached {name.replace('_', '-')} provenance is invalid"
            )
    return list(immutable.values())


def _immutable_artifact_map(
    receipt: Mapping[str, object],
    *,
    cache_root: Path | None = None,
    validate_content: bool = True,
) -> dict[Path, dict[str, object]]:
    """Validate the receipt content store and index it by canonical path."""

    values = receipt.get("immutable_artifacts")
    if not isinstance(values, list):
        raise ValueError("the finalization receipt has no immutable artifact list")
    immutable: dict[Path, dict[str, object]] = {}
    expected_parent = (
        (cache_root / "artifacts" / "sha256").resolve()
        if cache_root is not None
        else None
    )
    for descriptor in values:
        if not isinstance(descriptor, dict) or set(descriptor) != {
            "path",
            "sha256",
            "source_path",
        }:
            raise ValueError("the immutable finalization artifact identity is invalid")
        path_value = descriptor.get("path")
        saved_hash = descriptor.get("sha256")
        source_value = descriptor.get("source_path")
        if (
            not isinstance(path_value, str)
            or not isinstance(saved_hash, str)
            or re.fullmatch(r"[0-9a-f]{64}", saved_hash) is None
            or not isinstance(source_value, str)
        ):
            raise ValueError("the immutable finalization artifact identity is invalid")
        path = Path(path_value).resolve()
        source = Path(source_value).resolve()
        if (
            path_value != str(path)
            or source_value != str(source)
            or path.name != saved_hash
            or path.parent.name != "sha256"
            or path.parent.parent.name != "artifacts"
            or (expected_parent is not None and path.parent != expected_parent)
            or source in immutable
        ):
            raise ValueError("the immutable finalization artifact cache path is invalid")
        if validate_content:
            try:
                current_hash = file_hash(path)
            except ValueError as error:
                raise ValueError("an immutable finalization artifact is missing") from error
            if current_hash != saved_hash:
                raise ValueError("an immutable finalization artifact changed")
        immutable[source] = descriptor
    return immutable


def _immutable_report_artifact(
    receipt: Mapping[str, object],
    name: str,
    *,
    cache_root: Path | None = None,
    required: bool = True,
) -> Path | None:
    """Validate and return one receipt-local report copy."""

    from tools.decomp_provenance import _read_sidecar, provenance_path

    immutable = _immutable_artifact_map(
        receipt,
        cache_root=cache_root,
        validate_content=False,
    )
    steps = receipt.get("step_results")
    if not isinstance(steps, dict) or not isinstance(steps.get(name), dict):
        if not required:
            return None
        raise ValueError(
            f"the finalization receipt has no {name.replace('_', '-')} artifact"
        )
    source_report_descriptor = _primary_report_descriptor(steps[name], name)
    source_report = Path(source_report_descriptor["path"]).resolve()
    source_sidecar = provenance_path(source_report).resolve()

    report_descriptor = immutable.get(source_report)
    sidecar_descriptor = immutable.get(source_sidecar)
    if report_descriptor is None or sidecar_descriptor is None:
        raise ValueError(
            f"the finalization receipt has no immutable {name.replace('_', '-')} artifact"
        )
    if report_descriptor["sha256"] != source_report_descriptor["sha256"]:
        raise ValueError(
            f"the immutable {name.replace('_', '-')} report disagrees with finalization"
        )
    cached_report = Path(str(report_descriptor["path"]))
    cached_sidecar = Path(str(sidecar_descriptor["path"]))
    for cached, descriptor in (
        (cached_report, report_descriptor),
        (cached_sidecar, sidecar_descriptor),
    ):
        try:
            cached_hash = file_hash(cached)
        except ValueError as error:
            raise ValueError(
                f"an immutable {name.replace('_', '-')} artifact is missing"
            ) from error
        if cached_hash != descriptor["sha256"]:
            raise ValueError(
                f"an immutable {name.replace('_', '-')} artifact changed"
            )
    try:
        sidecar_document = _read_sidecar(cached_sidecar)
    except ValueError as error:
        raise ValueError(
            f"the immutable {name.replace('_', '-')} provenance is invalid"
        ) from error
    if (
        sidecar_document.get("kind") != "comparison-report"
        or sidecar_document.get("artifact")
        != {
            "path": str(source_report),
            "sha256": report_descriptor["sha256"],
        }
        or not isinstance(sidecar_document.get("input_identity"), dict)
    ):
        raise ValueError(
            f"the immutable {name.replace('_', '-')} provenance is invalid"
        )
    return cached_report


def _finalize_step_result(
    name: str, result: object, root: Path
) -> tuple[dict[str, object], list[dict[str, str]]]:
    artifacts: list[dict[str, str]] = []
    if isinstance(result, bool) and not result:
        raise ValueError(f"the {name.replace('_', '-')} step failed")
    if isinstance(result, (str, Path)):
        path = Path(result)
        if not path.is_absolute():
            path = root / path
        artifact = _finalize_artifact(path)
        artifacts.append(artifact)
        return {"kind": "artifact", **artifact}, artifacts
    if isinstance(result, Mapping):
        if result.get("ok") is False:
            raise ValueError(f"the {name.replace('_', '-')} step failed")
        paths = result.get("artifacts", [])
        if isinstance(paths, (str, Path)):
            paths = [paths]
        if not isinstance(paths, list):
            raise ValueError(
                f"the {name.replace('_', '-')} step has invalid artifacts"
            )
        for value in paths:
            if not isinstance(value, (str, Path)):
                raise ValueError(
                    f"the {name.replace('_', '-')} step has an invalid artifact path"
                )
            path = Path(value)
            if not path.is_absolute():
                path = root / path
            artifacts.append(_finalize_artifact(path))
        serializable = dict(result)
        serializable["artifacts"] = artifacts
        try:
            value_hash = _snapshot_hash(serializable)
        except (TypeError, ValueError) as error:
            raise ValueError(
                f"the {name.replace('_', '-')} step returned invalid data"
            ) from error
        return {
            "kind": "result",
            "value_sha256": value_hash,
            "artifacts": artifacts,
        }, artifacts
    if result is not None:
        raise ValueError(
            f"the {name.replace('_', '-')} step returned an unsupported result"
        )
    return {"kind": "result", "value_sha256": _snapshot_hash(None)}, artifacts


def _finalize_key_payload(
    state: Mapping[str, object],
    result: str,
    action_names: list[str],
    inputs: Mapping[str, object],
    reuse_no_source_baseline: bool,
) -> dict[str, object]:
    root_value = state.get("source_worktree_root")
    if not isinstance(root_value, str) or not root_value:
        raise ValueError("the active campaign has no source root")
    root = Path(root_value)
    if result == "no-source" and reuse_no_source_baseline:
        source_worktree = state.get("source_worktree")
        source_index = state.get("source_index")
        repository_worktree = state.get("repository_worktree")
        repository_index = state.get("repository_index")
        worktree_hash = state.get("source_worktree_sha256")
        index_hash = state.get("source_index_sha256")
        repository_hash = state.get("repository_worktree_sha256")
        repository_index_hash = state.get("repository_index_sha256")
    else:
        source_worktree = source_worktree_snapshot(root)
        source_index = source_index_snapshot(root)
        repository_worktree = repository_worktree_snapshot(root)
        repository_index = repository_index_snapshot(root)
        worktree_hash = _snapshot_hash(source_worktree)
        index_hash = _snapshot_hash(source_index)
        repository_hash = _snapshot_hash(repository_worktree)
        repository_index_hash = _snapshot_hash(repository_index)
    try:
        json.dumps(dict(inputs), sort_keys=True, allow_nan=False)
    except (TypeError, ValueError) as error:
        raise ValueError("finalization inputs must contain JSON values") from error
    campaign_ledger_relative = str(
        state.get(
            "campaign_ledger_relative",
            "tools/Resources/campaign-ledger.jsonl",
        )
    )
    campaign_ledger_path = root / campaign_ledger_relative
    current_campaign_ledger_text = (
        campaign_ledger_path.read_text(encoding="utf-8")
        if campaign_ledger_path.is_file()
        else ""
    )
    saved_campaign_ledger_text = state.get("campaign_ledger_text")
    campaign_ledger_text = (
        saved_campaign_ledger_text
        if isinstance(saved_campaign_ledger_text, str)
        else current_campaign_ledger_text
    )
    saved_ledger_hash = state.get("campaign_ledger_sha256")
    if not isinstance(saved_ledger_hash, str):
        saved_repository = state.get("repository_worktree")
        saved_ledger_hash = (
            saved_repository.get(campaign_ledger_relative)
            if isinstance(saved_repository, dict)
            else None
        )
    current_ledger_hash = hashlib.sha256(
        current_campaign_ledger_text.encode()
    ).hexdigest()
    expected_ledger_hash = hashlib.sha256(
        campaign_ledger_text.encode()
    ).hexdigest()
    if (
        saved_ledger_hash not in {None, expected_ledger_hash}
        or current_ledger_hash != expected_ledger_hash
    ):
        raise ValueError("the campaign ledger changed after campaign start")
    return {
        "receipt_version": FINALIZE_RECEIPT_VERSION,
        "campaign_id": _campaign_id(dict(state)),
        "result": result,
        "mode": state.get("mode"),
        "lane": _campaign_lane(state),
        "addresses": state.get("addresses"),
        "active_addresses": state.get("active_addresses"),
        "retired_addresses": state.get("retired_addresses"),
        "resource": state.get("resource"),
        "family": state.get("family") is True,
        "target_events": state.get("target_events"),
        "scored_addresses": state.get("scored_addresses"),
        "score_events": state.get("score_events"),
        "prediction_events": state.get("prediction_events"),
        "prediction_required": state.get("prediction_required") is True,
        "doctor_receipt": state.get("doctor_receipt"),
        "doctor_receipts": state.get("doctor_receipts"),
        "campaign_head": state.get("campaign_head"),
        "briefs": state.get("briefs"),
        "briefs_required": state.get("briefs_required") is True,
        "started_at": state.get("started_at"),
        "baseline_report_sha256": state.get("baseline_report_sha256"),
        "baseline_report_provenance_sha256": state.get(
            "baseline_report_provenance_sha256"
        ),
        "baseline_data_report_sha256": state.get("baseline_data_report_sha256"),
        "baseline_data_report_provenance_sha256": state.get(
            "baseline_data_report_provenance_sha256"
        ),
        "baseline_comparison_identity_sha256": state.get(
            "baseline_comparison_identity_sha256"
        ),
        "function_size_snapshot_sha256": state.get(
            "function_size_snapshot_sha256"
        ),
        "source_worktree_sha256": worktree_hash,
        "source_worktree_snapshot": source_worktree,
        "source_index_sha256": index_hash,
        "source_index_snapshot": source_index,
        "repository_worktree_sha256": repository_hash,
        "repository_worktree_snapshot": repository_worktree,
        "repository_index_sha256": repository_index_hash,
        "repository_index_snapshot": repository_index,
        "campaign_repository_worktree_snapshot": state.get(
            "repository_worktree"
        ),
        "campaign_repository_index_snapshot": state.get("repository_index"),
        "campaign_ledger_relative": campaign_ledger_relative,
        "campaign_ledger_text": campaign_ledger_text,
        "campaign_ledger_base_commit": (
            state.get("campaign_head")
            if isinstance(state.get("campaign_head"), str)
            else (
                lambda result: (
                    result.stdout.strip().lower()
                    if result.returncode == 0
                    and re.fullmatch(
                        r"[0-9a-f]{40,64}", result.stdout.strip().lower()
                    )
                    else ""
                )
            )(
                subprocess.run(
                    ["git", "rev-parse", "HEAD"],
                    cwd=root,
                    check=False,
                    capture_output=True,
                    text=True,
                )
            )
        ),
        "reuse_no_source_baseline": reuse_no_source_baseline,
        "actions": action_names,
        "inputs": dict(inputs),
    }


def _load_finalize_receipt(
    path: Path, expected_key: str, *, validate_artifacts: bool = True
) -> dict[str, object] | None:
    if not path.exists():
        return None
    receipt = _read_json_object(path, "finalization receipt")
    receipt_version = receipt.get("receipt_version")
    if (
        receipt.get("schema_version") != SCHEMA_VERSION
        or receipt_version
        not in (LEGACY_FINALIZE_RECEIPT_VERSION, FINALIZE_RECEIPT_VERSION)
        or receipt.get("receipt_key") != expected_key
        or receipt.get("status") != "passed"
        or receipt.get("receipt_sha256") != _finalize_receipt_hash(receipt)
    ):
        raise ValueError("the cached finalization receipt is invalid")
    key_payload = receipt.get("key_payload")
    if not isinstance(key_payload, dict) or _snapshot_hash(key_payload) != expected_key:
        raise ValueError("the cached finalization key is invalid")
    artifacts = receipt.get("artifacts")
    if not isinstance(artifacts, list):
        raise ValueError("the cached finalization receipt has no artifact list")
    if receipt_version == FINALIZE_RECEIPT_VERSION:
        immutable = _immutable_artifact_map(receipt, cache_root=path.parent)
        required_reports = (
            ()
            if receipt.get("mode") == "meta"
            else ("code_report", "data_report")
        )
        for name in required_reports:
            _immutable_report_artifact(
                receipt,
                name,
                cache_root=path.parent,
            )
        source_artifacts: dict[Path, str] = {}
        for artifact in artifacts:
            if not isinstance(artifact, dict):
                raise ValueError("the cached finalization receipt has invalid artifacts")
            source_value = artifact.get("path")
            saved_hash = artifact.get("sha256")
            if not isinstance(source_value, str) or not isinstance(saved_hash, str):
                raise ValueError("the cached finalization receipt has invalid artifacts")
            source = Path(source_value).resolve()
            if source in source_artifacts and source_artifacts[source] != saved_hash:
                raise ValueError("a cached finalization artifact has two identities")
            source_artifacts[source] = saved_hash
        for source, saved_hash in source_artifacts.items():
            cached = immutable.get(source)
            if cached is None or cached.get("sha256") != saved_hash:
                raise ValueError(
                    "the immutable finalization artifacts disagree with finalization"
                )
        allowed_sources = set(source_artifacts)
        steps = receipt.get("step_results")
        assert isinstance(steps, dict)
        if required_reports:
            from tools.decomp_provenance import provenance_path

            for name in required_reports:
                step = steps.get(name)
                assert isinstance(step, dict)
                report = Path(_primary_report_descriptor(step, name)["path"])
                allowed_sources.add(provenance_path(report).resolve())
        if set(immutable) != allowed_sources:
            raise ValueError(
                "the immutable finalization artifacts disagree with finalization"
            )
    elif receipt.get("immutable_artifacts") is not None:
        raise ValueError("the legacy finalization receipt has invalid immutable artifacts")
    for artifact in artifacts:
        if not isinstance(artifact, dict):
            raise ValueError("the cached finalization receipt has invalid artifacts")
        path_value = artifact.get("path")
        saved_hash = artifact.get("sha256")
        if not isinstance(path_value, str) or not isinstance(saved_hash, str):
            raise ValueError("the cached finalization receipt has invalid artifacts")
        if validate_artifacts and receipt_version == LEGACY_FINALIZE_RECEIPT_VERSION:
            artifact_path = Path(path_value).resolve()
            if not artifact_path.is_file() or file_hash(artifact_path) != saved_hash:
                raise ValueError("a cached finalization artifact changed")
    return receipt


def _save_finalize_receipt_on_state(
    state_path: Path,
    state: dict[str, object],
    receipt_path: Path,
    receipt: Mapping[str, object],
) -> None:
    state["finalize_receipt"] = {
        "path": str(receipt_path.resolve()),
        "file_sha256": file_hash(receipt_path),
        "content_sha256": receipt.get("receipt_sha256"),
        "receipt_key": receipt.get("receipt_key"),
    }
    state["phase"] = "finalized"
    write_state(state_path, state)


def _validated_finalize_receipt(
    state: Mapping[str, object],
    result: str,
    *,
    allowed_repository_changes: set[str] | None = None,
) -> dict[str, object]:
    _validate_state_briefs(state)
    identity = state.get("finalize_receipt")
    if not isinstance(identity, dict):
        raise ValueError("finalize the schema-v3 campaign before recording it")
    path_value = identity.get("path")
    key = identity.get("receipt_key")
    if not isinstance(path_value, str) or not isinstance(key, str):
        raise ValueError("the active campaign has an invalid finalization receipt")
    path = Path(path_value)
    saved_file_hash = identity.get("file_sha256")
    if not isinstance(saved_file_hash, str) or file_hash(path) != saved_file_hash:
        raise ValueError("the finalization receipt file changed")
    receipt = _load_finalize_receipt(path, key)
    if receipt is None:
        raise ValueError("the finalization receipt does not exist")
    if identity.get("content_sha256") != receipt.get("receipt_sha256"):
        raise ValueError("the finalization receipt content hash changed")
    if receipt.get("campaign_id") != _campaign_id(dict(state)):
        raise ValueError("the finalization receipt belongs to another campaign")
    if receipt.get("result") != result:
        raise ValueError("--result disagrees with the finalization receipt")
    if receipt.get("mode") != state.get("mode"):
        raise ValueError("the finalization receipt mode changed")
    if receipt.get("lane") != _campaign_lane(state):
        raise ValueError("the finalization receipt lane changed")
    key_payload = receipt.get("key_payload")
    root_value = state.get("source_worktree_root")
    if not isinstance(key_payload, dict) or not isinstance(root_value, str):
        raise ValueError("the finalization receipt has invalid input identity")
    expected_identity = {
        "campaign_id": _campaign_id(dict(state)),
        "mode": state.get("mode"),
        "lane": _campaign_lane(state),
        "addresses": state.get("addresses"),
        "active_addresses": state.get("active_addresses"),
        "retired_addresses": state.get("retired_addresses"),
        "resource": state.get("resource"),
        "family": state.get("family") is True,
        "target_events": state.get("target_events"),
        "scored_addresses": state.get("scored_addresses"),
        "score_events": state.get("score_events"),
        "prediction_events": state.get("prediction_events"),
        "prediction_required": state.get("prediction_required") is True,
        "doctor_receipt": state.get("doctor_receipt"),
        "doctor_receipts": state.get("doctor_receipts"),
        "campaign_head": state.get("campaign_head"),
        "briefs": state.get("briefs"),
        "briefs_required": state.get("briefs_required") is True,
        "started_at": state.get("started_at"),
        "baseline_report_sha256": state.get("baseline_report_sha256"),
        "baseline_report_provenance_sha256": state.get(
            "baseline_report_provenance_sha256"
        ),
        "baseline_data_report_sha256": state.get("baseline_data_report_sha256"),
        "baseline_data_report_provenance_sha256": state.get(
            "baseline_data_report_provenance_sha256"
        ),
        "baseline_comparison_identity_sha256": state.get(
            "baseline_comparison_identity_sha256"
        ),
    }
    for field, value in expected_identity.items():
        if key_payload.get(field) != value:
            raise ValueError(
                f"the finalization receipt {field.replace('_', ' ')} changed"
            )
    root = Path(root_value)
    current_values = {
        "source_worktree": source_worktree_snapshot(root),
        "source_index": source_index_snapshot(root),
        "repository_worktree": repository_worktree_snapshot(root),
        "repository_index": repository_index_snapshot(root),
    }
    allowed = allowed_repository_changes or set()
    for name, current_snapshot in current_values.items():
        saved_snapshot = key_payload.get(f"{name}_snapshot")
        saved_hash = key_payload.get(f"{name}_sha256")
        if isinstance(saved_snapshot, dict):
            changed = set(
                source_worktree_changes(saved_snapshot, current_snapshot)
            )
            if name.startswith("repository_"):
                changed -= allowed
            if changed:
                raise ValueError(
                    "a finalization input changed after the receipt was created"
                )
        elif saved_hash != _snapshot_hash(current_snapshot):
            raise ValueError(
                "a finalization input changed after the receipt was created"
            )
    receipt_inputs = key_payload.get("inputs")
    if (
        isinstance(receipt_inputs, dict)
        and receipt_inputs.get("standard_finalize_version") is not None
        and receipt_inputs != _standard_input_hashes(root)
    ):
        raise ValueError(
            "a standard finalization input changed after the receipt was created"
        )
    if (
        isinstance(receipt_inputs, dict)
        and receipt_inputs.get("meta_finalize_version") is not None
        and receipt_inputs != _meta_input_hashes(root)
    ):
        raise ValueError(
            "a meta finalization input changed after the receipt was created"
        )
    return receipt


def score_artifact_metadata(
    address: str,
    *,
    report: Path | None = None,
    diff: Path | None = None,
    data_report: Path | None = None,
    functions_map: Path = DEFAULT_FUNCTION_MAP,
    function_sizes: Path = DEFAULT_FUNCTION_SIZES,
    source_root: Path = ROOT / "src",
) -> dict[str, object]:
    """Read score telemetry from one saved comparison artifact."""

    if sum(value is not None for value in (report, diff, data_report)) != 1:
        raise ValueError(
            "score telemetry needs exactly one report, diff, or data-report artifact"
        )
    parsed_address = int(address, 16)
    artifact = report or diff or data_report
    assert artifact is not None
    if not artifact.is_file():
        raise ValueError(f"the score artifact does not exist: {artifact}")
    artifact_root = source_root.resolve().parent
    try:
        from tools.decomp_provenance import validate_diff, validate_report
    except ModuleNotFoundError:  # Direct invocation uses tools/ as sys.path[0].
        from decomp_provenance import (  # type: ignore[no-redef]
            validate_diff,
            validate_report,
        )
    if diff is not None:
        validate_diff(diff, parsed_address, root=artifact_root)
    else:
        validate_report(artifact, root=artifact_root)
    ceiling: float | None = None
    if data_report is not None:
        payload = _read_json_object(data_report, "typed-data report")
        variables = payload.get("variables")
        rows = variables.get("variables") if isinstance(variables, dict) else None
        if not isinstance(rows, list):
            raise ValueError("the typed-data report has no variable rows")
        matches = [
            row
            for row in rows
            if isinstance(row, dict)
            and _address_int(
                row.get("original_address", row.get("address"))
            )
            == parsed_address
        ]
        if len(matches) != 1:
            raise ValueError(f"the typed-data report has no unique row for {address}")
        value = matches[0].get("score")
        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(float(value))
            or not 0.0 <= float(value) <= 1.0
        ):
            raise ValueError("the typed-data row has an invalid score")
        score = float(value)
        effective = math.isclose(score, 1.0, abs_tol=1e-12)
    elif report is not None:
        try:
            from tools.decomp_status import read_match_statuses
        except ModuleNotFoundError:  # Direct invocation uses tools/ as sys.path[0].
            from decomp_status import read_match_statuses  # type: ignore[no-redef]

        status = read_match_statuses(report).get(parsed_address)
        if status is None:
            raise ValueError(f"the score report has no row for {address}")
        score = status.matching
        effective = status.effective
    else:
        try:
            from tools.decomp_diff import (
                is_noncoverage_target,
                read_score_ceiling,
                read_similarity,
            )
        except ModuleNotFoundError:  # Direct invocation uses tools/ as sys.path[0].
            from decomp_diff import (  # type: ignore[no-redef]
                is_noncoverage_target,
                read_score_ceiling,
                read_similarity,
            )

        text = diff.read_text(encoding="utf-8", errors="replace")
        score = read_similarity(text)
        if score is None:
            raise ValueError("the saved diff has no similarity verdict")
        effective = bool(re.search(r"\beffective\s+match\b", text, re.IGNORECASE))
        if not is_noncoverage_target(parsed_address, source_root):
            ceiling = read_score_ceiling(
                parsed_address, functions_map, function_sizes
            )
    if report is not None:
        try:
            from tools.decomp_diff import is_noncoverage_target, read_score_ceiling
        except ModuleNotFoundError:  # Direct invocation uses tools/ as sys.path[0].
            from decomp_diff import (  # type: ignore[no-redef]
                is_noncoverage_target,
                read_score_ceiling,
            )

        if not is_noncoverage_target(parsed_address, source_root):
            ceiling = read_score_ceiling(
                parsed_address, functions_map, function_sizes
            )
    relative = score / ceiling if ceiling and ceiling > 0 else None
    return {
        "raw_score": score * 100.0,
        "score_ceiling": ceiling * 100.0 if ceiling is not None else None,
        "ceiling_relative_score": relative * 100.0 if relative is not None else None,
        "artifact_sha256": file_hash(artifact),
        "effective": effective,
    }


def resource_score_artifact_metadata(
    state: Mapping[str, object],
    *,
    original_exe: Path,
    recompiled_exe: Path,
    artifact: Path,
) -> dict[str, object]:
    """Write one reproducible resource score artifact."""

    from tools import decomp_resources

    resource_value = state.get("resource")
    if not isinstance(resource_value, str):
        raise ValueError("the active campaign has no resource target")
    resource = decomp_resources.parse_resource(resource_value)
    rows = decomp_resources.resource_rows(original_exe, recompiled_exe)
    selected = decomp_resources.selected_evidence(rows, resource)
    scored = selected["scored_bytes"]
    if scored <= 0:
        raise ValueError("the resource target has no scored bytes")
    score = selected["explained_bytes"] / scored
    document = {
        "schema_version": 1,
        "kind": "resource-score",
        "resource": resource_value,
        "original_exe_sha256": file_hash(original_exe),
        "recompiled_exe_sha256": file_hash(recompiled_exe),
        "selected_evidence": selected,
        "score": score,
    }
    write_text(artifact, json.dumps(document, indent=2, sort_keys=True) + "\n")
    return {
        "raw_score": score * 100.0,
        "artifact_sha256": file_hash(artifact),
        "effective": math.isclose(score, 1.0, abs_tol=1e-12),
    }


def _first_score_metadata(
    artifact: Mapping[str, object],
    *,
    raw_score: float | None,
    score_ceiling: float | None,
    ceiling_relative_score: float | None,
    artifact_sha256: str | None,
    effective: bool,
) -> dict[str, object]:
    """Merge explicit score fields without permitting artifact overrides."""

    explicit_values: dict[str, object | None] = {
        "raw_score": raw_score,
        "score_ceiling": score_ceiling,
        "ceiling_relative_score": ceiling_relative_score,
        "artifact_sha256": artifact_sha256,
    }
    result: dict[str, object] = {}
    for name, explicit in explicit_values.items():
        derived = artifact.get(name)
        if artifact and explicit is not None:
            if isinstance(explicit, (int, float)) and isinstance(
                derived, (int, float)
            ):
                agrees = math.isclose(
                    float(explicit), float(derived), rel_tol=0.0, abs_tol=1e-6
                )
            else:
                agrees = explicit == derived
            if not agrees:
                raise ValueError(
                    f"--{name.replace('_', '-')} disagrees with the score artifact"
                )
        result[name] = derived if artifact else explicit
    derived_effective = bool(artifact.get("effective")) if artifact else False
    if artifact and effective and not derived_effective:
        raise ValueError("--effective disagrees with the score artifact")
    result["effective"] = derived_effective if artifact else effective
    return result


def _receipt_step_artifact(receipt: Mapping[str, object], name: str) -> Path:
    receipt_version = receipt.get("receipt_version")
    if receipt_version == FINALIZE_RECEIPT_VERSION:
        artifact = _immutable_report_artifact(receipt, name)
        assert artifact is not None
        return artifact
    if receipt_version != LEGACY_FINALIZE_RECEIPT_VERSION:
        raise ValueError("the finalization receipt version is invalid")
    steps = receipt.get("step_results")
    if not isinstance(steps, dict) or not isinstance(steps.get(name), dict):
        raise ValueError(f"the finalization receipt has no {name.replace('_', '-')} artifact")
    return Path(_primary_report_descriptor(steps[name], name)["path"])


def finalize_campaign(
    state_path: Path,
    result: str,
    actions: Mapping[str, Callable[[], object]],
    *,
    cache_root: Path = DEFAULT_FINALIZE_CACHE,
    inputs: Mapping[str, object] | None = None,
    reuse_no_source_baseline: bool = True,
    clock: Callable[[], datetime] = utc_now,
) -> dict[str, object]:
    """Run each finalization step once and save a hash-checked receipt."""

    state = read_state(state_path)
    mode = str(state.get("mode", ""))
    _validate_campaign_targets(state)
    _validate_state_briefs(state)
    _validate_result_mode(mode, result)
    _validate_source_deadlines(state, result)
    unknown = sorted(set(actions) - set(FINALIZE_STEPS))
    if unknown:
        raise ValueError(f"unknown finalization step: {unknown[0]}")
    baseline_reuse = result == "no-source" and reuse_no_source_baseline
    if baseline_reuse:
        _validate_no_source_tree(state)
        _validate_baseline_report_artifacts(state)
        action_names = [
            name for name in ("source_scan", "validation") if name in actions
        ]
    elif mode == "meta" and result == "meta-fix":
        required = ("source_scan", "validation")
        missing = [name for name in required if name not in actions]
        if missing:
            raise ValueError(
                "meta finalization needs these steps: " + ", ".join(missing)
            )
        action_names = list(required)
    else:
        missing = [name for name in FINALIZE_STEPS if name not in actions]
        if missing:
            raise ValueError(
                "finalization needs these steps: " + ", ".join(missing)
            )
        action_names = list(FINALIZE_STEPS)
    key_payload = _finalize_key_payload(
        state,
        result,
        action_names,
        inputs or {},
        baseline_reuse,
    )
    receipt_key = _snapshot_hash(key_payload)
    receipt_path = cache_root / f"{receipt_key}.json"
    cached = _load_finalize_receipt(receipt_path, receipt_key)
    if cached is not None:
        cached_metrics = cached.get("metrics_after")
        if isinstance(cached_metrics, dict):
            state["metrics_after"] = cached_metrics
        cached_phases = cached.get("phase_timestamps")
        if isinstance(cached_phases, dict):
            state["phase_timestamps"] = cached_phases
            _validate_source_deadlines(
                state,
                result,
                phase_timestamps=cached_phases,
            )
        _save_finalize_receipt_on_state(state_path, state, receipt_path, cached)
        result_value = dict(cached)
        result_value["reused"] = True
        return result_value

    root = Path(str(state["source_worktree_root"]))
    artifacts: list[dict[str, str]] = []
    step_results: dict[str, dict[str, object]] = {}
    phases = _phase_timestamps(state)
    if baseline_reuse:
        from tools.decomp_provenance import provenance_path

        for name, field, hash_field, provenance_hash_field in (
            (
                "code_report",
                "baseline_report",
                "baseline_report_sha256",
                "baseline_report_provenance_sha256",
            ),
            (
                "data_report",
                "baseline_data_report",
                "baseline_data_report_sha256",
                "baseline_data_report_provenance_sha256",
            ),
        ):
            report = Path(str(state[field])).resolve()
            artifact = {
                "path": str(report),
                "sha256": str(state[hash_field]),
            }
            provenance_artifact = {
                "path": str(provenance_path(report).resolve()),
                "sha256": str(state[provenance_hash_field]),
            }
            artifacts.extend((artifact, provenance_artifact))
            step_results[name] = {
                "kind": "baseline",
                **artifact,
                "artifacts": [artifact, provenance_artifact],
            }
        metrics_after = _normalize_progress_metrics(
            state.get("metrics_before")
            if isinstance(state.get("metrics_before"), dict)
            else None
        )
    else:
        metrics_after = _normalize_progress_metrics(None)

    for index, name in enumerate(action_names):
        started = clock()
        if index == 0:
            _validate_source_deadlines(
                state,
                result,
                finalization_started=started,
                phase_timestamps=phases,
            )
            prior_start = _first_finalization_start(phases)
            phases.setdefault(
                "finalization-started", timestamp(prior_start or started)
            )
        phases[f"{name}_started"] = timestamp(started)
        state["phase"] = f"finalize_{name}"
        state["phase_timestamps"] = phases
        write_state(state_path, state)
        try:
            raw_result = actions[name]()
            descriptor, step_artifacts = _finalize_step_result(
                name, raw_result, root
            )
            ended = clock()
            if ended < started:
                raise ValueError("the finalization clock moved backward")
        except Exception as error:
            phases[f"{name}_failed"] = timestamp(clock())
            state["phase"] = "finalize_failed"
            state["phase_timestamps"] = phases
            write_state(state_path, state)
            raise ValueError(
                f"the {name.replace('_', '-')} step failed: {error}"
            ) from error
        phases[f"{name}_ended"] = timestamp(ended)
        if name == "validation":
            phases["validation"] = timestamp(ended)
        step_results[name] = descriptor
        artifacts.extend(step_artifacts)
        if name == "source_scan" and isinstance(raw_result, Mapping):
            metric_value = raw_result.get("metrics")
            if metric_value is None and any(
                key in raw_result
                for key in (*PROGRESS_METRICS, "source_debt_functions")
            ):
                metric_value = raw_result
            if isinstance(metric_value, Mapping):
                metrics_after = _normalize_progress_metrics(metric_value)

    try:
        immutable_artifacts = _freeze_finalize_artifacts(
            artifacts,
            step_results,
            cache_root,
            require_reports=mode != "meta",
        )
    except Exception as error:
        phases["artifact_cache_failed"] = timestamp(clock())
        state["phase"] = "finalize_failed"
        state["phase_timestamps"] = phases
        write_state(state_path, state)
        raise ValueError(f"cannot cache finalization artifacts: {error}") from error

    state["metrics_after"] = metrics_after
    state["phase_timestamps"] = phases
    created_at = timestamp(clock())
    receipt: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "receipt_version": FINALIZE_RECEIPT_VERSION,
        "record_type": "finalize-receipt",
        "status": "passed",
        "receipt_key": receipt_key,
        "campaign_id": _campaign_id(state),
        "result": result,
        "mode": mode,
        "lane": _campaign_lane(state),
        "created_at": created_at,
        "key_payload": key_payload,
        "baseline_reused": baseline_reuse,
        "step_results": step_results,
        "artifacts": artifacts,
        "immutable_artifacts": immutable_artifacts,
        "metrics_after": metrics_after,
        "phase_timestamps": phases,
    }
    receipt["receipt_sha256"] = _finalize_receipt_hash(receipt)
    write_text(receipt_path, json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    _save_finalize_receipt_on_state(state_path, state, receipt_path, receipt)
    result_value = dict(receipt)
    result_value["reused"] = False
    return result_value


def _finalize_plan_actions(
    plan: Mapping[str, object], root: Path
) -> dict[str, Callable[[], object]]:
    step_values = plan.get("steps")
    if not isinstance(step_values, dict):
        raise ValueError("the finalization plan has no steps object")
    actions: dict[str, Callable[[], object]] = {}
    for name, value in step_values.items():
        if name not in FINALIZE_STEPS or not isinstance(value, dict):
            raise ValueError(f"the finalization plan has an invalid step: {name}")
        command = value.get("command")
        artifact_values = value.get("artifacts", [])
        metrics_path = value.get("metrics_path")
        if (
            not isinstance(command, list)
            or not command
            or not all(isinstance(part, str) and part for part in command)
        ):
            raise ValueError(f"the {name.replace('_', '-')} command is invalid")
        if not isinstance(artifact_values, list) or not all(
            isinstance(path, str) and path for path in artifact_values
        ):
            raise ValueError(f"the {name.replace('_', '-')} artifacts are invalid")
        if metrics_path is not None and not isinstance(metrics_path, str):
            raise ValueError(f"the {name.replace('_', '-')} metrics path is invalid")

        def action(
            *,
            step_name: str = name,
            step_command: list[str] = list(command),
            step_artifacts: list[str] = list(artifact_values),
            step_metrics_path: str | None = metrics_path,
        ) -> object:
            completed = subprocess.run(
                step_command,
                cwd=root,
                check=False,
                capture_output=True,
                text=True,
            )
            if completed.returncode != 0:
                detail = completed.stderr.strip() or completed.stdout.strip()
                raise ValueError(
                    f"command exited with {completed.returncode}: {detail or step_name}"
                )
            result: dict[str, object] = {
                "ok": True,
                "artifacts": step_artifacts,
                "stdout_sha256": hashlib.sha256(
                    completed.stdout.encode("utf-8")
                ).hexdigest(),
            }
            if step_metrics_path is not None:
                metrics_file = Path(step_metrics_path)
                if not metrics_file.is_absolute():
                    metrics_file = root / metrics_file
                result["metrics"] = _read_json_object(
                    metrics_file, "source-scan metrics"
                )
            return result

        actions[name] = action
    return actions


def finalize_from_plan(
    state_path: Path,
    result: str,
    plan_path: Path,
    *,
    cache_root: Path = DEFAULT_FINALIZE_CACHE,
) -> dict[str, object]:
    state = read_state(state_path)
    root_value = state.get("source_worktree_root")
    if not isinstance(root_value, str) or not root_value:
        raise ValueError("the active campaign has no source root")
    plan = _read_json_object(plan_path, "finalization plan")
    plan_inputs = plan.get("inputs", {})
    if not isinstance(plan_inputs, dict):
        raise ValueError("the finalization plan inputs must be an object")
    inputs = dict(plan_inputs)
    inputs["plan_sha256"] = file_hash(plan_path)
    return finalize_campaign(
        state_path,
        result,
        _finalize_plan_actions(plan, Path(root_value)),
        cache_root=cache_root,
        inputs=inputs,
        reuse_no_source_baseline=bool(plan.get("reuse_no_source_baseline", True)),
    )


def _standard_command(
    command: list[str], root: Path, *, cwd: Path | None = None
) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        cwd=cwd or root,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise ValueError(
            f"command exited with {completed.returncode}: {detail or command[0]}"
        )
    return completed


def standard_finalize_commands(
    root: Path, *, platform_name: str | None = None
) -> dict[str, list[list[str]]]:
    """Return the commands for one standard finalization pass."""

    platform_value = platform_name or os.name
    if platform_value == "nt":
        build_command = ["cmake", "--build", "build", "--", "-NOLOGO"]
    else:
        jobs = os.environ.get("TOY2_BUILD_JOBS") or os.environ.get(
            "TOY2_LOCAL_BUILD_JOBS", "4"
        )
        build_command = ["cmake", "--build", "build", "--", f"-j{jobs}"]
    return {
        "build": [
            build_command,
            ["reccmp-project", "detect", "--what", "recompiled"],
        ],
        "code_report": [
            [
                "reccmp-reccmp",
                "--target",
                "TOY2",
                "--silent",
                "--no-color",
                "--json",
                "decomp-current-report.json",
            ]
        ],
        "data_report": [
            [
                sys.executable,
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
        ],
        "source_scan": [],
        "validation": [],
    }


def _standard_source_scan(
    state: Mapping[str, object], current_report: Path, *, staged: bool
) -> tuple[dict[str, object], dict[str, object]]:
    from tools import decomp_lint, decomp_verify
    from tools.decomp_status import read_match_statuses

    units = decomp_lint.target_units(staged, [])
    raw_findings = decomp_lint.scan_units(units)
    entries = decomp_lint.read_baseline(staged=staged)
    findings, stale = decomp_lint.apply_baseline(raw_findings, entries)
    debt: dict[int, list[str]] = {}
    for finding in findings:
        if finding.owner_address and not finding.suppressed:
            debt.setdefault(int(finding.owner_address, 16), []).append(finding.rule)
    implemented: set[int] = set()
    for unit in units:
        implemented.update(
            int(address, 16)
            for kind, address in SOURCE_ANNOTATION_RE.findall(unit.text)
            if kind == "FUNCTION"
        )
    snapshot = state.get("function_sizes")
    if not isinstance(snapshot, dict):
        raise ValueError("the active campaign has no function-size snapshot")
    sizes = _decode_size_snapshot(snapshot)
    statuses = read_match_statuses(current_report)
    terminal = {
        address
        for address in implemented
        if address in statuses
        and (
            statuses[address].effective
            or math.isclose(statuses[address].matching, 1.0, abs_tol=1e-12)
        )
        and address not in debt
    }
    metrics = {
        "implemented": len(implemented),
        "terminal": len(terminal),
        "terminal_bytes": sum(sizes.get(address, 0) for address in terminal),
        "effective_bytes": effective_code_bytes(current_report, sizes),
        "source_debt": len(debt),
    }
    new_errors = [
        finding
        for finding in findings
        if finding.severity == "error"
        and not finding.legacy
        and not finding.suppressed
    ]
    new_warnings = [
        finding
        for finding in findings
        if finding.severity == "warning"
        and not finding.legacy
        and not finding.suppressed
    ]
    lint_change = decomp_verify.LintDebtChange()
    if state.get("mode") == "refinement":
        head_findings = decomp_lint.scan_units(
            decomp_lint.target_units(False, [], revision="HEAD")
        )
        lint_change = decomp_verify.classify_lint_debt_change(
            head_findings,
            decomp_lint.read_baseline(revision="HEAD"),
            raw_findings,
            entries,
        )
    public = {
        "ok": True,
        "metrics": metrics,
        "lint": {
            "new_errors": len(new_errors),
            "new_warnings": len(new_warnings),
            "stale_baseline": len(stale),
        },
    }
    context: dict[str, object] = {
        "debt": debt,
        "lint_change": lint_change,
        "new_errors": new_errors,
        "new_warnings": new_warnings,
        "stale": stale,
    }
    return public, context


def _standard_input_hashes(root: Path) -> dict[str, object]:
    paths = (
        Path(__file__),
        root / "tools" / "decomp_verify.py",
        root / "tools" / "decomp_lint.py",
        root / "tools" / "generate-decomp-data-report.py",
        root / "tools" / "Resources" / "functions_map.txt",
        root / "build" / "build.ninja",
        root / "build" / "Makefile",
        root / "build" / "CMakeCache.txt",
        root / "original" / "toy2.exe",
    )
    return {
        "standard_finalize_version": 1,
        "files": {
            str(path.resolve()): file_hash(path)
            for path in paths
            if path.is_file()
        },
        "commands": standard_finalize_commands(root),
    }


def _meta_input_hashes(root: Path) -> dict[str, object]:
    paths = (
        Path(__file__),
        root / "tools" / "decomp",
        root / "tools" / "decomp.ps1",
        root / "tools" / "decomp_lint.py",
        root / "tools" / "ghidra_sync.py",
    )
    return {
        "meta_finalize_version": 1,
        "files": {
            str(path.resolve()): file_hash(path)
            for path in paths
            if path.is_file()
        },
        "commands": {
            "tests": [
                sys.executable,
                "-m",
                "unittest",
                "discover",
                "-s",
                "tools/tests",
                "-v",
            ],
            "map": [sys.executable, "tools/ghidra_sync.py", "check"],
            "diff": ["git", "diff", "--check"],
        },
    }


def _standard_meta_actions(
    state: Mapping[str, object], *, staged: bool
) -> dict[str, Callable[[], object]]:
    root = Path(str(state["source_worktree_root"]))
    inputs = _meta_input_hashes(root)
    commands = inputs["commands"]
    assert isinstance(commands, dict)

    def source_scan_action() -> object:
        metrics = state.get("metrics_before")
        return {
            "ok": True,
            "metrics": _normalize_progress_metrics(
                metrics if isinstance(metrics, dict) else None
            ),
        }

    def validation_action() -> object:
        for name in ("tests", "map", "diff"):
            command = commands[name]
            assert isinstance(command, list)
            _standard_command(command, root)
        return {"ok": True}

    return {
        "source_scan": source_scan_action,
        "validation": validation_action,
    }


def _finalizer_relevant_path(path: str) -> bool:
    relative = Path(path)
    return (
        _is_source_path(relative)
        or path == "AGENTS.md"
        or relative.parts[:1] in (("tools",), ("resources",), (".agents",))
    )


def _ensure_staged_reproducible(
    state: Mapping[str, object], root: Path
) -> None:
    """Reject campaign changes that the staged commit cannot reproduce."""

    path_sets: list[set[str]] = []
    for command in (
        ["git", "diff", "--name-only", "-z"],
        ["git", "ls-files", "--others", "--exclude-standard", "-z"],
    ):
        completed = subprocess.run(
            command, cwd=root, check=False, capture_output=True
        )
        if completed.returncode != 0:
            raise ValueError("cannot inspect unstaged campaign files")
        path_sets.append(
            {
                value.decode("utf-8", errors="surrogateescape")
                for value in completed.stdout.split(b"\0")
                if value
            }
        )
    unstaged, untracked = path_sets
    ledger_relative_value = state.get("campaign_ledger_relative")
    ledger_relative = (
        ledger_relative_value
        if isinstance(ledger_relative_value, str) and ledger_relative_value
        else "tools/Resources/campaign-ledger.jsonl"
    )
    ledger_text = state.get("campaign_ledger_text")
    ledger_hash = state.get("campaign_ledger_sha256")
    if not isinstance(ledger_hash, str):
        repository_worktree = state.get("repository_worktree")
        ledger_hash = (
            repository_worktree.get(ledger_relative)
            if isinstance(repository_worktree, dict)
            else None
        )

    def retained_ledger(path: str) -> bool:
        if path != ledger_relative:
            return False
        candidate = root / path
        try:
            contents = candidate.read_bytes()
        except OSError:
            return False
        if isinstance(ledger_text, str):
            return contents == ledger_text.encode()
        return (
            isinstance(ledger_hash, str)
            and hashlib.sha256(contents).hexdigest() == ledger_hash
        )

    relevant_untracked = {
        path
        for path in untracked
        if _finalizer_relevant_path(path) and not retained_ledger(path)
    }
    if relevant_untracked:
        raise ValueError(
            "stage or remove these untracked campaign files before finalization: "
            + ", ".join(sorted(relevant_untracked)[:8])
        )
    relevant = {
        path
        for path in unstaged
        if _finalizer_relevant_path(path) and not retained_ledger(path)
    }
    if relevant:
        raise ValueError(
            "stage or restore these campaign files before finalization: "
            + ", ".join(sorted(relevant)[:8])
        )


def _staged_relevant_paths(root: Path) -> list[str]:
    completed = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "-z"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise ValueError("cannot inspect staged campaign files")
    return sorted(
        value.decode("utf-8", errors="surrogateescape")
        for value in completed.stdout.split(b"\0")
        if value
        and _finalizer_relevant_path(
            value.decode("utf-8", errors="surrogateescape")
        )
    )


def _staged_paths(root: Path) -> list[str]:
    """Return every staged path without applying a campaign scope filter."""

    completed = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "-z"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise ValueError("cannot inspect staged campaign files")
    return sorted(
        value.decode("utf-8", errors="surrogateescape")
        for value in completed.stdout.split(b"\0")
        if value
    )


def _cmake_codemodel_reference(value: object) -> str | None:
    """Return the File API codemodel file from one CMake index reply."""

    if not isinstance(value, dict):
        return None
    for key, child in value.items():
        if (
            isinstance(key, str)
            and key.startswith("codemodel-v2")
            and isinstance(child, dict)
            and isinstance(child.get("jsonFile"), str)
        ):
            return str(child["jsonFile"])
        nested = _cmake_codemodel_reference(child)
        if nested is not None:
            return nested
    return None


def _cmake_reply_path(reply_root: Path, value: object, description: str) -> Path:
    if not isinstance(value, str) or not value or Path(value).name != value:
        raise ValueError(f"the CMake {description} path is invalid")
    path = (reply_root / value).resolve()
    try:
        path.relative_to(reply_root.resolve())
    except ValueError as error:
        raise ValueError(f"the CMake {description} path escaped its reply") from error
    return path


def _configured_build_sources(root: Path) -> list[dict[str, object]]:
    """Read all configured target sources from the current CMake graph."""

    build = root / "build"
    if not (build / "CMakeCache.txt").is_file():
        raise ValueError("configure the build before finalization")
    client = f"client-toy2-decomp-{uuid.uuid4().hex}"
    query = build / ".cmake/api/v1/query" / client / "codemodel-v2"
    query.parent.mkdir(parents=True, exist_ok=True)
    query.touch()
    try:
        _standard_command(
            ["cmake", "-S", str(root), "-B", str(build)],
            root,
        )
    finally:
        query.unlink(missing_ok=True)
        try:
            query.parent.rmdir()
        except OSError:
            pass
    reply_root = build / ".cmake/api/v1/reply"
    replies: list[dict[str, object]] = []
    for path in reply_root.glob("index-*.json"):
        index = _read_json_object(path, "CMake File API index")
        values = index.get("reply")
        if isinstance(values, dict) and client in values:
            reply = values[client]
            if not isinstance(reply, dict):
                raise ValueError("the CMake File API client reply is invalid")
            replies.append(reply)
    if len(replies) != 1:
        raise ValueError("CMake did not write one File API reply for this check")
    reference = _cmake_codemodel_reference(replies[0])
    codemodel_path = _cmake_reply_path(
        reply_root, reference, "codemodel"
    )
    codemodel = _read_json_object(codemodel_path, "CMake codemodel")
    configurations = codemodel.get("configurations")
    if not isinstance(configurations, list) or not configurations:
        raise ValueError("the CMake codemodel has no configuration")
    sources: list[dict[str, object]] = []
    target_files: set[Path] = set()
    for configuration in configurations:
        targets = (
            configuration.get("targets")
            if isinstance(configuration, dict)
            else None
        )
        if not isinstance(targets, list):
            raise ValueError("the CMake codemodel target list is invalid")
        for target in targets:
            target_file = target.get("jsonFile") if isinstance(target, dict) else None
            path = _cmake_reply_path(reply_root, target_file, "target")
            if path in target_files:
                continue
            target_files.add(path)
            document = _read_json_object(path, "CMake target")
            values = document.get("sources", [])
            if not isinstance(values, list):
                raise ValueError("a CMake target source list is invalid")
            for source in values:
                source_path = source.get("path") if isinstance(source, dict) else None
                if not isinstance(source_path, str) or not source_path:
                    raise ValueError("a CMake target source path is invalid")
                candidate = Path(source_path)
                if not candidate.is_absolute():
                    candidate = root / candidate
                sources.append(
                    {
                        "path": candidate.absolute(),
                        "generated": source.get("isGenerated") is True,
                    }
                )
    return sources


def _validate_relevant_git_modes(
    snapshot: Mapping[str, list[str]], description: str
) -> None:
    """Require regular stage-zero blobs for all tracked campaign files."""

    symbolic_links: list[str] = []
    invalid: list[str] = []
    for path, entries in snapshot.items():
        if not _finalizer_relevant_path(path):
            continue
        parsed = [entry.split() for entry in entries]
        if any(fields and fields[0] == "120000" for fields in parsed):
            symbolic_links.append(path)
            continue
        if (
            len(parsed) != 1
            or len(parsed[0]) != 3
            or parsed[0][0] not in {"100644", "100755"}
            or parsed[0][2] != "0"
        ):
            invalid.append(path)
    if symbolic_links:
        raise ValueError(
            f"{description} contains symbolic links for campaign files: "
            + ", ".join(sorted(symbolic_links)[:8])
        )
    if invalid:
        raise ValueError(
            f"{description} contains invalid modes for campaign files: "
            + ", ".join(sorted(invalid)[:8])
        )


def _validate_configured_build_sources(
    root: Path,
    snapshot: Mapping[str, list[str]],
    description: str,
) -> None:
    """Require each configured source in the reproducible Git tree."""

    root_absolute = root.absolute()
    problems: list[str] = []
    for source in _configured_build_sources(root):
        path = source.get("path")
        if not isinstance(path, Path):
            raise ValueError("the configured build source identity is invalid")
        try:
            relative = path.relative_to(root_absolute).as_posix()
        except ValueError:
            problems.append(str(path))
            continue
        entries = snapshot.get(relative, [])
        regular = (
            len(entries) == 1
            and len(entries[0].split()) == 3
            and entries[0].split()[0] in {"100644", "100755"}
            and entries[0].split()[2] == "0"
        )
        if regular:
            continue
        if (
            source.get("generated") is True
            and relative in REPRODUCIBLE_GENERATED_SOURCES
        ):
            continue
        problems.append(relative)
    if problems:
        raise ValueError(
            f"configured build sources are absent from the {description}: "
            + ", ".join(sorted(set(problems))[:8])
        )


def _meta_workflow_path(path: str) -> bool:
    """Return true for files that a meta-fix campaign may deliver."""

    relative = Path(path)
    if path in {
        "AGENTS.md",
        "decomp_utils.py",
        "tools/decomp",
        "tools/decomp.ps1",
        "tools/linux-decomp-env.sh",
    }:
        return True
    if relative.parts[:2] == ("tools", "tests"):
        return True
    if (
        len(relative.parts) == 2
        and relative.parts[0] == "tools"
        and relative.suffix in {".py", ".sh"}
    ):
        return True
    return relative.parts[:3] in {
        (".agents", "skills", "continue-decomp"),
        (".agents", "skills", "decomp-expert"),
    }


def _normal_campaign_path(mode: str, path: str) -> bool:
    """Return true for product evidence that a normal campaign may change."""

    relative = Path(path)
    if relative.parts[:1] == ("src",):
        return _is_source_path(relative)
    if relative.name == "CMakeLists.txt" and "tools" not in relative.parts:
        return True
    if path == "tools/Resources/functions_map.txt":
        return True
    if mode == "resource" and relative.parts[:1] == ("resources",):
        return True
    return False


def _ensure_normal_validation_inputs_unchanged(
    state: Mapping[str, object], root: Path, staged_paths: list[str]
) -> None:
    """Reject self-modified validators and runtime inputs."""

    mode = str(state.get("mode", ""))
    invalid_staged = [
        path for path in staged_paths if not _normal_campaign_path(mode, path)
    ]
    if invalid_staged:
        raise ValueError(
            "a normal campaign cannot change workflow or validation files: "
            + ", ".join(invalid_staged[:8])
        )
    baseline = state.get("repository_worktree")
    if not isinstance(baseline, dict):
        raise ValueError("the campaign has no repository input snapshot")
    current = repository_worktree_snapshot(root)
    changed = source_worktree_changes(baseline, current)
    invalid_changes = sorted(
        path for path in changed if not _normal_campaign_path(mode, path)
    )
    if invalid_changes:
        raise ValueError(
            "a validation input changed after campaign start: "
            + ", ".join(invalid_changes[:8])
        )


def _standard_finalize_actions(
    state: Mapping[str, object],
    *,
    mode: str,
    targets: list[str],
    resource: str | None,
    staged: bool,
) -> dict[str, Callable[[], object]]:
    from tools import decomp_verify

    root = Path(str(state["source_worktree_root"]))
    build = root / "build"
    if not any((build / name).is_file() for name in ("build.ninja", "Makefile")):
        raise ValueError("configure the build before finalization")
    commands = standard_finalize_commands(root)
    current_report = build / "decomp-current-report.json"
    current_data = build / "decomp-current-data-report.json"
    scan_context: dict[str, object] = {}

    def validate_current_reports() -> None:
        from tools.decomp_provenance import validate_report

        code_receipt = validate_report(current_report, root=root)
        data_receipt = validate_report(current_data, root=root)
        if code_receipt.get("input_identity") != data_receipt.get(
            "input_identity"
        ):
            raise ValueError(
                "the code and typed-data reports use different comparison inputs"
            )

    def build_action() -> object:
        _standard_command(commands["build"][0], root)
        _standard_command(commands["build"][1], root, cwd=build)
        return {"artifacts": [build / "toy2.exe", build / "toy2.pdb"]}

    def code_report_action() -> object:
        from tools.decomp_provenance import (
            _artifact_state,
            current_identity,
            provenance_path,
            seal_report,
        )

        identity = current_identity(root)
        artifact_before = _artifact_state(current_report)
        _standard_command(commands["code_report"][0], root, cwd=build)
        seal_report(
            current_report,
            root=root,
            expected_identity=identity,
            expected_artifact=artifact_before,
        )
        return {"artifacts": [current_report, provenance_path(current_report)]}

    def data_report_action() -> object:
        from tools.decomp_provenance import (
            _artifact_state,
            current_identity,
            provenance_path,
            seal_report,
        )

        identity = current_identity(root)
        artifact_before = _artifact_state(current_data)
        _standard_command(commands["data_report"][0], root)
        seal_report(
            current_data,
            root=root,
            expected_identity=identity,
            expected_artifact=artifact_before,
        )
        return {"artifacts": [current_data, provenance_path(current_data)]}

    def source_scan_action() -> object:
        validate_current_reports()
        public, private = _standard_source_scan(
            state, current_report, staged=staged
        )
        scan_context.update(private)
        return public

    def validation_action() -> object:
        validate_current_reports()
        if not scan_context:
            raise ValueError("run the source scan before validation")
        if scan_context["new_errors"]:
            raise ValueError("staged source has new source-debt errors")
        if scan_context["new_warnings"]:
            raise ValueError("staged source has new source-debt warnings")
        if scan_context["stale"]:
            raise ValueError("staged lint baseline has stale rows")
        if _campaign_lane(state) == "closure":
            from tools.decomp_status import read_match_statuses

            statuses = read_match_statuses(current_report)
            debt = scan_context["debt"]
            for address_text in targets:
                address = int(address_text, 16)
                status = statuses.get(address)
                if status is None or not (status.exact or status.effective):
                    raise ValueError(
                        f"closure target {address_text} is not exact or effective"
                    )
                if isinstance(debt, dict) and debt.get(address):
                    raise ValueError(
                        f"closure target {address_text} still has source debt"
                    )
        if mode != "meta":
            saved_read_debt = decomp_verify.read_source_debt
            saved_read_change = decomp_verify.read_lint_debt_change
            decomp_verify.read_source_debt = lambda *_args, **_kwargs: scan_context[
                "debt"
            ]
            decomp_verify.read_lint_debt_change = lambda: scan_context[
                "lint_change"
            ]
            try:
                status = decomp_verify.validate(
                    Path(str(state["baseline_report"])),
                    current_report,
                    {int(address, 16) for address in targets},
                    False,
                    metadata_path=(
                        build / "decomp-baseline-meta.json"
                        if (build / "decomp-baseline-meta.json").is_file()
                        else None
                    ),
                    source_root=root / "src",
                    mode=mode,
                    baseline_data_path=(
                        Path(str(state["baseline_data_report"]))
                        if mode in ("data", "resource")
                        else None
                    ),
                    current_data_path=(
                        current_data if mode in ("data", "resource") else None
                    ),
                    staged=staged,
                    resource=parse_resource(resource) if resource else None,
                )
            finally:
                decomp_verify.read_source_debt = saved_read_debt
                decomp_verify.read_lint_debt_change = saved_read_change
            if status:
                raise ValueError("campaign report validation failed")
        _standard_command(
            [sys.executable, "tools/ghidra_sync.py", "check"], root
        )
        _standard_command(["git", "diff", "--check"], root)
        return {"ok": True}

    return {
        "build": build_action,
        "code_report": code_report_action,
        "data_report": data_report_action,
        "source_scan": source_scan_action,
        "validation": validation_action,
    }


def finalize_standard(
    state_path: Path,
    result: str,
    *,
    mode: str,
    targets: list[str] | None = None,
    resource: str | None = None,
    staged: bool = False,
    cache_root: Path = DEFAULT_FINALIZE_CACHE,
    clock: Callable[[], datetime] = utc_now,
) -> dict[str, object]:
    state = read_state(state_path)
    active_mode = str(state.get("mode", ""))
    if mode != active_mode:
        raise ValueError("--mode disagrees with the active campaign")
    active_targets = _active_addresses(state)
    selected_targets = targets or []
    if selected_targets != active_targets:
        raise ValueError("--target disagrees with the active campaign")
    active_resource = state.get("resource")
    if resource != active_resource:
        raise ValueError("--resource disagrees with the active campaign")
    if not staged:
        raise ValueError("standard finalization requires --staged")
    root = Path(str(state.get("source_worktree_root", ROOT)))
    _ensure_staged_reproducible(state, root)
    staged_paths = _staged_paths(root)
    repository_index = repository_index_snapshot(root)
    _validate_relevant_git_modes(repository_index, "campaign repository index")
    if result == "source" and mode != "meta":
        _validate_configured_build_sources(
            root, repository_index, "staged Git tree"
        )
    if mode == "meta":
        workflow_paths = [
            path for path in staged_paths if _meta_workflow_path(path)
        ]
        if not workflow_paths:
            raise ValueError("a meta-fix finalization needs a staged workflow change")
        invalid_meta_paths = [
            path for path in staged_paths if not _meta_workflow_path(path)
        ]
        if invalid_meta_paths:
            raise ValueError(
                "a meta-fix cannot include files outside workflow scope: "
                + ", ".join(invalid_meta_paths[:8])
            )
    else:
        _ensure_normal_validation_inputs_unchanged(
            state, root, _staged_relevant_paths(root)
        )
    if result == "no-source":
        actions: dict[str, Callable[[], object]] = {}
    elif mode == "meta":
        actions = _standard_meta_actions(state, staged=staged)
    else:
        actions = _standard_finalize_actions(
            state,
            mode=mode,
            targets=selected_targets,
            resource=resource,
            staged=staged,
        )
    return finalize_campaign(
        state_path,
        result,
        actions,
        cache_root=cache_root,
        inputs=(
            _meta_input_hashes(root)
            if mode == "meta"
            else _standard_input_hashes(root)
        ),
        reuse_no_source_baseline=True,
        clock=clock,
    )


def abort_campaign(
    ledger_path: Path,
    state_path: Path,
    reason: str,
    now: datetime | None = None,
) -> dict[str, object]:
    state = read_state(state_path)
    if state.get("phase") == "finalized" or state.get("finalize_receipt") is not None:
        raise ValueError("the campaign is finalized. Run campaigns record")
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
    ended_at = timestamp(ended)
    phases = _phase_timestamps(state)
    phases["ended"] = ended_at
    item: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "abort",
        "campaign_id": _campaign_id(state),
        "abort_id": f"abort:{_campaign_id(state)}",
        "timestamp": ended_at,
        "started_at": state["started_at"],
        "ended_at": ended_at,
        "first_score_at": first_score_value,
        "first_score_minutes": first_score_minutes,
        "minutes": (ended - started).total_seconds() / 60.0,
        "mode": state.get("mode", ""),
        "lane": _campaign_lane(state),
        "addresses": state.get("addresses", []),
        "active_addresses": _active_addresses(state),
        "retired_addresses": state.get("retired_addresses", []),
        "resource": state.get("resource"),
        "target_events": state.get("target_events", []),
        "subsystem": state.get("subsystem", ""),
        "prediction": state.get("prediction"),
        "prediction_events": state.get("prediction_events", []),
        "doctor_receipt": state.get("doctor_receipt"),
        "doctor_receipts": state.get("doctor_receipts", []),
        "briefs": state.get("briefs", []),
        "metrics_before": state.get("metrics_before"),
        "implemented_before": state.get("implemented_before"),
        "terminal_before": state.get("terminal_before"),
        "terminal_bytes_before": state.get("terminal_bytes_before"),
        "effective_bytes_before": state.get("effective_bytes_before"),
        "source_debt_before": state.get("source_debt_before"),
        "phase_timestamps": phases,
        "first_score_model": state.get("first_score_model"),
        "first_score_raw": state.get("first_score_raw"),
        "first_score_ceiling": state.get("first_score_ceiling"),
        "first_score_relative": state.get("first_score_relative"),
        "first_score_artifact_sha256": state.get(
            "first_score_artifact_sha256"
        ),
        "first_score_effective": state.get("first_score_effective"),
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


def _record_interval(
    record: Mapping[str, object],
    index: int,
    delivery_end: datetime | None = None,
) -> tuple[datetime, datetime, str, int] | None:
    if record.get("record_type", "campaign") not in ("campaign", "abort"):
        return None
    campaign_ended = _record_datetime(record, "ended_at") or _record_datetime(
        record, "timestamp"
    )
    if campaign_ended is None:
        return None
    ended = (
        delivery_end
        if delivery_end is not None and delivery_end > campaign_ended
        else campaign_ended
    )
    started = _record_datetime(record, "selection_started_at") or _record_datetime(
        record, "started_at"
    )
    if started is None:
        minutes = record.get("minutes")
        if isinstance(minutes, (int, float)) and minutes >= 0:
            started = campaign_ended - timedelta(minutes=float(minutes))
        else:
            started = campaign_ended
    if ended < started:
        return None
    record_type = record.get("record_type", "campaign")
    if record_type == "abort":
        category = "abort"
    elif record.get("mode") == "meta" or record.get("result") == "meta-fix":
        category = "meta"
    elif record.get("result") == "no-source":
        category = "no_source"
    else:
        category = "source"
    return started, ended, category, index


def _retained_bytes(record: Mapping[str, object]) -> float:
    return sum(
        float(record.get(name, 0.0) or 0.0)
        for name in (
            "effective_bytes",
            "initialized_bytes",
            "resource_explained_bytes",
        )
    )


def _forecast_observation(
    record: Mapping[str, object], retained: float
) -> tuple[float | None, float | None, float]:
    addresses = record.get("active_addresses")
    if not isinstance(addresses, list) or not addresses:
        prediction = record.get("prediction")
        expected = record.get("expected_retained_bytes")
        lower = None
        if isinstance(prediction, dict):
            expected = prediction.get("expected_retained_bytes", expected)
            lower = prediction.get("lower_bound_retained_bytes")
        return (
            float(expected)
            if isinstance(expected, (int, float))
            and not isinstance(expected, bool)
            and math.isfinite(float(expected))
            else None,
            float(lower)
            if isinstance(lower, (int, float))
            and not isinstance(lower, bool)
            and math.isfinite(float(lower))
            else None,
            retained,
        )

    expected_parts: list[float] = []
    lower_parts: list[float] = []
    target_deltas = record.get("target_deltas")
    actual = 0.0
    for address in addresses:
        if not isinstance(address, str):
            return None, None, retained
        prediction, expected = _address_prediction(record, address)
        if expected is None or not math.isfinite(expected):
            return None, None, retained
        expected_parts.append(expected)
        if isinstance(prediction, dict):
            prediction_total = prediction.get("expected_retained_bytes")
            lower_total = prediction.get("lower_bound_retained_bytes")
            if (
                isinstance(prediction_total, (int, float))
                and not isinstance(prediction_total, bool)
                and float(prediction_total) > 0.0
                and isinstance(lower_total, (int, float))
                and not isinstance(lower_total, bool)
                and math.isfinite(float(lower_total))
            ):
                lower_parts.append(
                    float(lower_total) * expected / float(prediction_total)
                )
        if isinstance(target_deltas, dict):
            delta = target_deltas.get(address)
            if isinstance(delta, dict):
                for name in (
                    "effective_bytes",
                    "initialized_bytes",
                    "resource_explained_bytes",
                ):
                    value = delta.get(name, 0.0)
                    if (
                        isinstance(value, (int, float))
                        and not isinstance(value, bool)
                        and math.isfinite(float(value))
                    ):
                        actual += float(value)
        else:
            actual = retained
    lower = sum(lower_parts) if len(lower_parts) == len(addresses) else None
    return sum(expected_parts), lower, max(actual, 0.0)


def _timing_buckets(
    intervals: list[tuple[datetime, datetime, str, int]],
    window_start: datetime,
    window_end: datetime,
) -> tuple[dict[str, float], float]:
    categories = {name: 0.0 for name in ("source", "no_source", "meta", "abort")}
    clipped = [
        (max(start, window_start), min(end, window_end), category, index)
        for start, end, category, index in intervals
        if end > window_start and start < window_end
    ]
    boundaries = sorted(
        {
            window_start,
            window_end,
            *(start for start, _, _, _ in clipped),
            *(end for _, end, _, _ in clipped),
        }
    )
    gap = 0.0
    priority = {"source": 0, "no_source": 1, "meta": 2, "abort": 3}
    for start, end in zip(boundaries, boundaries[1:]):
        minutes = (end - start).total_seconds() / 60.0
        active = [
            item for item in clipped if item[0] <= start and item[1] >= end
        ]
        if not active:
            gap += minutes
            continue
        chosen = max(active, key=lambda item: (priority[item[2]], item[3]))
        categories[chosen[2]] += minutes
    return categories, gap


def summarize_records(
    records: list[dict[str, object]],
    *,
    window: int | None = None,
    limit: int = 10,
) -> dict[str, object]:
    if window is not None and window < 0:
        raise ValueError("--window must be zero or greater")
    if limit < 0:
        raise ValueError("--limit must be zero or greater")
    ordered = _chronological_records(records)
    delivery_by_campaign: dict[str, list[dict[str, object]]] = defaultdict(list)
    for record in ordered:
        campaign_id = record.get("campaign_id")
        if (
            record.get("record_type") == "delivery"
            and isinstance(campaign_id, str)
            and _record_datetime(record, "timestamp") is not None
        ):
            delivery_by_campaign[campaign_id].append(record)
    timed: list[tuple[dict[str, object], tuple[datetime, datetime, str, int]]] = []
    for index, record in enumerate(ordered):
        campaign_id = record.get("campaign_id")
        delivery_events = (
            delivery_by_campaign.get(campaign_id, [])
            if isinstance(campaign_id, str)
            else []
        )
        delivery_end = max(
            (
                value
                for value in (
                    _record_datetime(event, "timestamp")
                    for event in delivery_events
                )
                if value is not None
            ),
            default=None,
        )
        interval = _record_interval(record, index, delivery_end)
        if interval is not None:
            timed.append((record, interval))
    if not timed:
        return {
            "schema_version": SCHEMA_VERSION,
            "record_count": 0,
            "display_count": 0,
            "window_started_at": None,
            "window_ended_at": None,
            "window_minutes": 0.0,
            "normal_minutes": 0.0,
            "source_minutes": 0.0,
            "no_source_minutes": 0.0,
            "meta_minutes": 0.0,
            "abort_minutes": 0.0,
            "unattributed_gap_minutes": 0.0,
            "normal_share": 0.0,
            "source_share": 0.0,
            "no_source_share": 0.0,
            "meta_share": 0.0,
            "abort_share": 0.0,
            "unattributed_gap_share": 0.0,
            "zero_yield_rate": None,
            "production_bytes": 0.0,
            "production_minutes": 0.0,
            "production_bytes_per_minute": None,
            "all_in_bytes_per_minute": None,
            "forecast_realization": None,
            "forecast_band_rate": None,
            "forecast_lower_bound_count": 0,
            "forecast_lower_bound_hits": 0,
            "forecast_lower_bound_coverage": None,
            "forecast_lower_bound_hit_rate": None,
            "closure_terminal_conversions": 0,
            "closure_minutes": 0.0,
            "terminal_conversions_per_minute": None,
            "terminal_conversions_per_refinement": None,
            "refinement_campaigns": 0,
            "closure_refinements": 0,
            "delivery_event_count": 0,
            "delivered_campaigns": 0,
            "pushed_campaigns": 0,
            "delivery_latency_minutes": 0.0,
            "median_delivery_latency_minutes": None,
            "per_lane": {},
            "recent_records": [],
        }
    selected = timed[-window:] if window else timed
    latest_end = max(interval[1] for _, interval in selected)
    window_start = min(interval[0] for _, interval in selected)
    buckets, gap = _timing_buckets(
        [interval for _, interval in selected], window_start, latest_end
    )
    window_minutes = (latest_end - window_start).total_seconds() / 60.0
    normal_minutes = buckets["source"] + buckets["no_source"]
    campaign_records = [
        record
        for record, _ in selected
        if record.get("record_type", "campaign") == "campaign"
    ]
    normal_records = [
        record
        for record in campaign_records
        if record.get("mode") != "meta" and record.get("result") != "meta-fix"
    ]
    source_records = [
        record for record in normal_records if record.get("result") == "source"
    ]
    all_source_bytes = sum(_retained_bytes(record) for record in source_records)
    production_bytes = sum(
        _retained_bytes(record)
        for record in source_records
        if _campaign_lane(record) == "production"
    )
    zero_yield = sum(
        record.get("result") == "no-source" or _retained_bytes(record) <= 0
        for record in normal_records
    )
    expected_total = 0.0
    forecast_actual = 0.0
    forecast_count = 0
    band_count = 0
    band_hits = 0
    lower_bound_count = 0
    lower_bound_hits = 0
    terminal_conversions = 0
    closure_minutes = 0.0
    production_minutes = 0.0
    refinements = 0
    closure_refinements = 0
    selected_ids = {
        str(record.get("campaign_id"))
        for record, _ in selected
        if record.get("record_type", "campaign") == "campaign"
        and isinstance(record.get("campaign_id"), str)
    }
    selected_delivery_events = [
        event
        for campaign_id in selected_ids
        for event in delivery_by_campaign.get(campaign_id, [])
    ]
    delivery_latencies: list[float] = []
    pushed_campaigns = 0
    for record, _ in selected:
        campaign_id = record.get("campaign_id")
        if not isinstance(campaign_id, str):
            continue
        events = delivery_by_campaign.get(campaign_id, [])
        if not events:
            continue
        campaign_end = _record_datetime(record, "ended_at") or _record_datetime(
            record, "timestamp"
        )
        event_times = [
            value
            for value in (
                _record_datetime(event, "timestamp") for event in events
            )
            if value is not None
        ]
        if campaign_end is not None and event_times:
            delivery_latencies.append(
                max(0.0, (max(event_times) - campaign_end).total_seconds() / 60.0)
            )
        pushed_campaigns += int(any(event.get("status") == "pushed" for event in events))
    per_lane: dict[str, dict[str, int | float]] = defaultdict(
        lambda: {
            "records": 0,
            "source": 0,
            "no_source": 0,
            "meta": 0,
            "abort": 0,
            "zero_yield": 0,
            "minutes": 0.0,
            "retained_bytes": 0.0,
            "terminal_conversions": 0,
        }
    )
    for record, interval in selected:
        lane = _campaign_lane(record)
        lane_row = per_lane[lane]
        lane_row["records"] += 1
        category = interval[2]
        lane_row[category] += 1
        lane_row["minutes"] += max(
            0.0,
            (
                min(interval[1], latest_end) - max(interval[0], window_start)
            ).total_seconds()
            / 60.0,
        )
        if record.get("record_type", "campaign") != "campaign":
            continue
        record_minutes = max(
            0.0,
            (
                min(interval[1], latest_end) - max(interval[0], window_start)
            ).total_seconds()
            / 60.0,
        )
        if lane == "production":
            production_minutes += record_minutes
        if lane == "closure":
            closure_minutes += record_minutes
        retained = _retained_bytes(record)
        lane_row["retained_bytes"] += retained
        if category in ("source", "no_source") and (
            record.get("result") == "no-source" or retained <= 0
        ):
            lane_row["zero_yield"] += 1
        expected, lower, forecast_retained = _forecast_observation(
            record, retained
        )
        if isinstance(expected, (int, float)) and expected > 0:
            expected_total += float(expected)
            forecast_actual += forecast_retained
            forecast_count += 1
            band_count += 1
            band_hits += int(
                float(expected) * 0.5
                <= forecast_retained
                <= float(expected) * 1.5
            )
        if isinstance(lower, (int, float)) and lower >= 0:
            lower_bound_count += 1
            lower_bound_hits += int(forecast_retained >= float(lower))
        before = record.get("terminal_before")
        after = record.get("terminal_after")
        conversion = (
            max(0, int(after) - int(before))
            if isinstance(before, (int, float))
            and isinstance(after, (int, float))
            else 0
        )
        if lane == "closure":
            terminal_conversions += conversion
        lane_row["terminal_conversions"] += conversion
        refinements += int(record.get("mode") == "refinement")
        closure_refinements += int(
            record.get("mode") == "refinement" and lane == "closure"
        )
    denominator = window_minutes if window_minutes > 0 else None
    ordered_delivery_latencies = sorted(delivery_latencies)
    delivery_middle = len(ordered_delivery_latencies) // 2
    if not ordered_delivery_latencies:
        median_delivery_latency = None
    elif len(ordered_delivery_latencies) % 2:
        median_delivery_latency = ordered_delivery_latencies[delivery_middle]
    else:
        median_delivery_latency = (
            ordered_delivery_latencies[delivery_middle - 1]
            + ordered_delivery_latencies[delivery_middle]
        ) / 2.0
    recent_pairs = selected[-limit:] if limit else selected
    recent_records = [record for record, _ in recent_pairs]
    result: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "record_count": len(selected),
        "display_count": len(recent_records),
        "window_started_at": timestamp(window_start),
        "window_ended_at": timestamp(latest_end),
        "window_minutes": window_minutes,
        "normal_minutes": normal_minutes,
        "source_minutes": buckets["source"],
        "no_source_minutes": buckets["no_source"],
        "meta_minutes": buckets["meta"],
        "abort_minutes": buckets["abort"],
        "unattributed_gap_minutes": gap,
        "normal_share": normal_minutes / denominator if denominator else 0.0,
        "source_share": buckets["source"] / denominator if denominator else 0.0,
        "no_source_share": (
            buckets["no_source"] / denominator if denominator else 0.0
        ),
        "meta_share": buckets["meta"] / denominator if denominator else 0.0,
        "abort_share": buckets["abort"] / denominator if denominator else 0.0,
        "unattributed_gap_share": gap / denominator if denominator else 0.0,
        "normal_campaigns": len(normal_records),
        "source_campaigns": len(source_records),
        "no_source_campaigns": sum(
            record.get("result") == "no-source" for record in normal_records
        ),
        "zero_yield_campaigns": zero_yield,
        "zero_yield_rate": zero_yield / len(normal_records) if normal_records else None,
        "production_bytes": production_bytes,
        "production_minutes": production_minutes,
        "production_bytes_per_minute": (
            production_bytes / production_minutes if production_minutes > 0 else None
        ),
        "all_in_bytes_per_minute": (
            all_source_bytes / window_minutes if window_minutes > 0 else None
        ),
        "forecast_count": forecast_count,
        "forecast_realization": (
            forecast_actual / expected_total if expected_total > 0 else None
        ),
        "forecast_band_count": band_count,
        "forecast_band_hits": band_hits,
        "forecast_band_rate": band_hits / band_count if band_count else None,
        "forecast_lower_bound_count": lower_bound_count,
        "forecast_lower_bound_hits": lower_bound_hits,
        "forecast_lower_bound_coverage": (
            lower_bound_count / forecast_count if forecast_count else None
        ),
        "forecast_lower_bound_hit_rate": (
            lower_bound_hits / lower_bound_count if lower_bound_count else None
        ),
        "closure_terminal_conversions": terminal_conversions,
        "closure_minutes": closure_minutes,
        "terminal_conversions_per_minute": (
            terminal_conversions / closure_minutes if closure_minutes > 0 else None
        ),
        "refinement_campaigns": refinements,
        "closure_refinements": closure_refinements,
        "terminal_conversions_per_refinement": (
            terminal_conversions / refinements
            if refinements
            else None
        ),
        "per_lane": {lane: dict(values) for lane, values in sorted(per_lane.items())},
        "delivery_event_count": len(selected_delivery_events),
        "delivered_campaigns": len(delivery_latencies),
        "pushed_campaigns": pushed_campaigns,
        "delivery_latency_minutes": sum(delivery_latencies),
        "median_delivery_latency_minutes": median_delivery_latency,
        "recent_records": recent_records,
    }
    return result


def print_summary(
    records: list[dict[str, object]],
    limit: int,
    window: int | None = None,
    json_output: bool = False,
) -> dict[str, object]:
    summary = summarize_records(
        records, window=window, limit=limit
    )
    if json_output:
        print(json.dumps(summary, indent=2, sort_keys=True))
        return summary
    if summary["record_count"] == 0:
        print("No timed campaign records exist.")
        return summary
    print(f"Window: {float(summary['window_minutes']):.1f} minutes")
    print(
        "Time: "
        f"{float(summary['normal_minutes']):.1f} normal, "
        f"{float(summary['meta_minutes']):.1f} meta, "
        f"{float(summary['abort_minutes']):.1f} abort, "
        f"{float(summary['unattributed_gap_minutes']):.1f} unattributed minutes"
    )
    rate = summary["production_bytes_per_minute"]
    if isinstance(rate, (int, float)):
        print(f"Production rate: {float(rate):.2f} bytes/minute")
    zero_rate = summary["zero_yield_rate"]
    if isinstance(zero_rate, (int, float)):
        print(f"Zero-yield rate: {float(zero_rate):.1%}")
    delivery_latency = summary["median_delivery_latency_minutes"]
    if isinstance(delivery_latency, (int, float)):
        print(
            "Median delivery latency: "
            f"{float(delivery_latency):.1f} minute(s)"
        )
    print("Recent results:")
    for item in summary["recent_records"]:
        if not isinstance(item, dict):
            continue
        addresses = ",".join(str(value) for value in item.get("addresses", [])) or "-"
        record_type = item.get("record_type", "campaign")
        result = "abort" if record_type == "abort" else item.get("result", "-")
        print(
            f"  {_campaign_lane(item):<10} {str(result):<9} "
            f"{float(item.get('minutes', 0.0) or 0.0):>5.1f} min  "
            f"{_retained_bytes(item):>8.2f} bytes  {addresses}"
        )
    return summary


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
    print(
        "Active targets: "
        + (", ".join(_active_addresses(state)) or "-")
    )
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
        deadlines = _campaign_deadlines(started, None, None)
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
    start.add_argument("--lane")
    start.add_argument("--family", action="store_true")
    start.add_argument("--expected-minutes", type=float)
    start.add_argument("--expected-retained-bytes", type=float)
    start.add_argument("--prediction-version")
    start.add_argument("--prediction-lower-bound-bytes", type=float)
    start.add_argument("--prediction-features", type=Path)
    start.add_argument("--prediction-handoff", action="append", default=[], type=Path)
    start.add_argument("--progress-before", type=Path)
    start.add_argument("--doctor-receipt", required=False, type=Path)
    start.add_argument("--brief", action="append", default=[], type=Path)

    baseline = subparsers.add_parser(
        "set-baseline", help="attach reports after the wrapper saves a baseline"
    )
    baseline.add_argument("--report", type=Path, default=DEFAULT_BASELINE_REPORT)
    baseline.add_argument("--data-report", type=Path, default=DEFAULT_BASELINE_DATA_REPORT)
    baseline.add_argument("--progress", type=Path)
    baseline.add_argument("--quiet", action="store_true")

    meta_baseline = subparsers.add_parser(
        "set-meta-baseline", help="attach a repository-only meta baseline"
    )
    meta_baseline.add_argument("--progress", type=Path)
    meta_baseline.add_argument("--quiet", action="store_true")

    first_score = subparsers.add_parser("first-score", help="stamp the first compiled score")
    first_score.add_argument("--address", required=True, type=parse_address)
    first_score.add_argument("--raw-score", type=float)
    first_score.add_argument("--score-ceiling", type=float)
    first_score.add_argument("--ceiling-relative-score", type=float)
    first_score.add_argument("--model")
    first_score.add_argument("--artifact-sha256")
    score_artifact = first_score.add_mutually_exclusive_group()
    score_artifact.add_argument("--report", type=Path)
    score_artifact.add_argument("--diff", type=Path)
    score_artifact.add_argument("--data-report", type=Path)
    first_score.add_argument("--score-functions-map", type=Path, default=DEFAULT_FUNCTION_MAP)
    first_score.add_argument("--score-function-sizes", type=Path, default=DEFAULT_FUNCTION_SIZES)
    first_score.add_argument("--score-source-root", type=Path, default=ROOT / "src")
    first_score.add_argument("--effective", action="store_true")
    first_score.add_argument("--quiet", action="store_true")

    resource_score = subparsers.add_parser(
        "resource-score", help="stamp the first resource comparison"
    )
    resource_score.add_argument("--quiet", action="store_true")
    resource_score.add_argument("--raw-score", type=float)
    resource_score.add_argument("--model")
    resource_score.add_argument("--artifact-sha256")
    resource_score.add_argument("--effective", action="store_true")
    resource_score.add_argument(
        "--original-exe", type=Path, default=ROOT / "original/toy2.exe"
    )
    resource_score.add_argument(
        "--recompiled-exe", type=Path, default=ROOT / "build/toy2.exe"
    )
    resource_score.add_argument(
        "--artifact",
        type=Path,
        default=ROOT / "build/decomp-resource-score.json",
    )

    phase = subparsers.add_parser("phase", help="stamp one campaign phase")
    phase.add_argument("--name", required=True)

    add_target_parser = subparsers.add_parser(
        "add-target", help="add a campaign target or pivot"
    )
    add_target_parser.add_argument("--address", required=True, type=parse_address)
    add_target_parser.add_argument("--replace", type=parse_address)
    add_target_parser.add_argument("--doctor-receipt", required=True, type=Path)
    add_target_parser.add_argument("--brief", required=True, type=Path)
    add_target_parser.add_argument("--expected-minutes", type=float)
    add_target_parser.add_argument(
        "--expected-retained-bytes", type=float
    )
    add_target_parser.add_argument("--prediction-version")
    add_target_parser.add_argument(
        "--prediction-lower-bound-bytes", type=float
    )
    add_target_parser.add_argument("--prediction-features", type=Path)
    add_target_parser.add_argument(
        "--prediction-handoff", action="append", default=[], type=Path
    )

    record = subparsers.add_parser("record", help="record one completed measured campaign")
    record.add_argument("--result", required=True, choices=("source", "no-source", "meta-fix"))
    record.add_argument(
        "--mode",
        choices=("coverage", "refinement", "data", "resource", "meta"),
        help="optional assertion against the active campaign",
    )
    record.add_argument("--progress-after", type=Path, help=argparse.SUPPRESS)
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
        "--model",
        action="append",
        help="a source model ruled out by no-source or severe under-yield",
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
    evidence.add_argument("--failed-campaign-id", required=True)
    evidence.add_argument("--failed-model", required=True)
    evidence.add_argument("--changed-assumption")
    evidence.add_argument("--changed-source")

    delivery = subparsers.add_parser(
        "delivery", help="record a campaign delivery event"
    )
    delivery.add_argument("--campaign-id", required=True)
    delivery.add_argument("--status", required=True, choices=DELIVERY_STATUSES)
    delivery.add_argument("--artifact-sha256")
    delivery.add_argument("--receipt-sha256")
    delivery.add_argument("--delivery-receipt", type=Path)
    delivery.add_argument("--base-commit")
    delivery.add_argument("--commit")
    delivery.add_argument("--note")

    delivery_verify = subparsers.add_parser(
        "delivery-verify",
        help="validate an integrated campaign commit and write its receipt",
    )
    delivery_verify.add_argument("--campaign-id", required=True)
    delivery_verify.add_argument("--commit", required=True)
    delivery_verify.add_argument("--base-commit", required=True)
    finalize = subparsers.add_parser(
        "finalize", help="run one hash-checked finalization plan"
    )
    finalize.add_argument(
        "--result", required=True, choices=("source", "no-source", "meta-fix")
    )
    finalize.add_argument(
        "--mode", choices=("coverage", "refinement", "data", "resource", "meta")
    )
    finalize.add_argument("--target", action="append", default=[], type=parse_address)
    finalize.add_argument("--resource", type=parse_resource)
    finalize.add_argument("--staged", action="store_true")
    finalize.add_argument("--cache-root", type=Path, default=DEFAULT_FINALIZE_CACHE)

    abort = subparsers.add_parser("abort", help="stop a campaign and keep an audit record")
    abort.add_argument("--reason", required=True)
    subparsers.add_parser("migrate", help="import untracked legacy campaign history")
    subparsers.add_parser("status", help="show the active campaign and its deadlines")

    summary = subparsers.add_parser("summary", help="show recent throughput")
    summary.add_argument("--limit", type=int, default=10)
    summary.add_argument(
        "--window", type=int, help="use the last N completed campaign or abort records"
    )
    summary.add_argument("--json", action="store_true")
    return parser


def _cli_prediction_arguments(
    arguments: argparse.Namespace,
    *,
    lane: str,
    mode: str,
    addresses: list[str],
) -> dict[str, object]:
    raw_handoffs = arguments.prediction_handoff
    handoff_paths = (
        list(raw_handoffs)
        if isinstance(raw_handoffs, list)
        else [] if raw_handoffs is None else [raw_handoffs]
    )
    individual_values = (
        arguments.expected_minutes,
        arguments.expected_retained_bytes,
        arguments.prediction_version,
        arguments.prediction_lower_bound_bytes,
        arguments.prediction_features,
    )
    if handoff_paths:
        if any(value is not None for value in individual_values):
            raise ValueError(
                "--prediction-handoff cannot be combined with individual forecast options"
            )
        if lane not in ("closure", "production"):
            raise ValueError(
                "--prediction-handoff requires the closure or production lane"
            )
        if len(handoff_paths) != len(addresses):
            raise ValueError(
                "closure and production need one --prediction-handoff for each target"
            )
        values = [
            _prediction_handoff_arguments(
                path,
                lane=lane,
                mode=mode,
                addresses=[address],
            )
            for path, address in zip(handoff_paths, addresses)
        ]
        if len(values) == 1:
            return values[0]
        subsystems = {value.get("subsystem") for value in values}
        if len(subsystems) != 1 or not all(
            isinstance(value, str) and value for value in subsystems
        ):
            raise ValueError(
                "all prediction handoffs in a bundle need one subsystem"
            )
        features: dict[str, object] = {
            "success_probability": min(
                float(value["prediction_features"]["success_probability"])
                for value in values
            ),
            "cohort_sample_size": min(
                int(value["prediction_features"]["cohort_sample_size"])
                for value in values
            ),
            "median_retained_bytes": sum(
                float(value["expected_retained_bytes"]) for value in values
            ),
            "median_minutes": sum(
                float(value["expected_minutes"]) for value in values
            ),
            "per_target": {
                address: {
                    "median_retained_bytes": float(
                        value["expected_retained_bytes"]
                    ),
                    "lower_retained_bytes": float(
                        value["prediction_lower_bound_bytes"]
                    ),
                    "median_minutes": float(value["expected_minutes"]),
                    "handoff": value["prediction_features"],
                }
                for address, value in zip(addresses, values)
            },
        }
        return {
            "expected_minutes": features["median_minutes"],
            "expected_retained_bytes": features["median_retained_bytes"],
            "prediction_version": "bundle-v1",
            "prediction_lower_bound_bytes": sum(
                float(value["prediction_lower_bound_bytes"])
                for value in values
            ),
            "prediction_features": features,
            "subsystem": next(iter(subsystems)),
        }
    if lane in ("closure", "production"):
        raise ValueError(
            "closure and production need one --prediction-handoff for each target"
        )
    return {
        "expected_minutes": arguments.expected_minutes,
        "expected_retained_bytes": arguments.expected_retained_bytes,
        "prediction_version": arguments.prediction_version,
        "prediction_lower_bound_bytes": arguments.prediction_lower_bound_bytes,
        "prediction_features": (
            _read_json_object(arguments.prediction_features, "prediction features")
            if arguments.prediction_features is not None
            else None
        ),
        "subsystem": None,
    }


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    try:
        if args.command == "migrate":
            imported = migrate_legacy_ledger(args.file)
            print(f"Imported {imported} legacy campaign record(s).")
            return 0
        if args.command == "start":
            prediction_arguments = _cli_prediction_arguments(
                args,
                lane=_validate_lane(args.mode, args.lane),
                mode=args.mode,
                addresses=args.address,
            )
            progress_before = (
                _read_json_object(args.progress_before, "progress report")
                if args.progress_before is not None
                else None
            )
            handoff_subsystem = prediction_arguments.get("subsystem")
            if handoff_subsystem is not None and (
                not isinstance(handoff_subsystem, str)
                or not handoff_subsystem.strip()
            ):
                raise ValueError("the prediction handoff has no subsystem")
            if (
                args.subsystem.strip()
                and isinstance(handoff_subsystem, str)
                and args.subsystem.strip() != handoff_subsystem.strip()
            ):
                raise ValueError(
                    "--subsystem disagrees with the prediction handoff"
                )
            campaign_subsystem = args.subsystem.strip()
            if not campaign_subsystem and handoff_subsystem is not None:
                campaign_subsystem = str(handoff_subsystem).strip()
            state = start_campaign(
                args.state_file,
                args.mode,
                args.address,
                campaign_subsystem,
                utc_now(),
                worktree_root=args.worktree_root,
                map_path=args.functions_map,
                sizes_path=args.function_sizes,
                source_root=args.source_root,
                expected_minutes=prediction_arguments["expected_minutes"],
                expected_retained_bytes=prediction_arguments[
                    "expected_retained_bytes"
                ],
                family=args.family,
                resources=args.resource,
                lane=args.lane,
                prediction_version=prediction_arguments["prediction_version"],
                prediction_lower_bound_bytes=prediction_arguments[
                    "prediction_lower_bound_bytes"
                ],
                prediction_features=prediction_arguments["prediction_features"],
                progress_before=progress_before,
                doctor_receipt_path=args.doctor_receipt,
                brief_paths=args.brief,
                require_brief=args.mode != "meta",
                require_prediction_metadata=args.mode != "meta",
                campaign_records=_read_records(args.file),
                campaign_ledger_path=args.file,
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
                _read_json_object(args.progress, "progress report")
                if args.progress is not None
                else None,
            )
            if not args.quiet:
                print("Attached the baseline reports to the active campaign.")
            return 0
        if args.command == "set-meta-baseline":
            attach_meta_baseline(
                args.state_file,
                utc_now(),
                _read_json_object(args.progress, "progress report")
                if args.progress is not None
                else None,
            )
            if not args.quiet:
                print("Attached the repository baseline to the active meta campaign.")
            return 0
        if args.command == "first-score":
            artifact_metadata: dict[str, object] = {}
            if args.report is None and args.diff is None and args.data_report is None:
                raise ValueError(
                    "first-score needs --report, --diff, or --data-report"
                )
            artifact_metadata = score_artifact_metadata(
                args.address,
                report=args.report,
                diff=args.diff,
                data_report=args.data_report,
                functions_map=args.score_functions_map,
                function_sizes=args.score_function_sizes,
                source_root=args.score_source_root,
            )
            score_metadata = _first_score_metadata(
                artifact_metadata,
                raw_score=args.raw_score,
                score_ceiling=args.score_ceiling,
                ceiling_relative_score=args.ceiling_relative_score,
                artifact_sha256=args.artifact_sha256,
                effective=args.effective,
            )
            status = mark_first_score(
                args.state_file,
                args.address,
                utc_now(),
                raw_score=score_metadata["raw_score"],
                score_ceiling=score_metadata["score_ceiling"],
                ceiling_relative_score=score_metadata[
                    "ceiling_relative_score"
                ],
                model=args.model,
                artifact_sha256=score_metadata["artifact_sha256"],
                effective=bool(score_metadata["effective"]),
            )
            if not args.quiet and status != "inactive":
                print(f"First-score stamp: {status}.")
            return 0
        if args.command == "resource-score":
            if not args.state_file.exists():
                return 0
            resource_state = read_state(args.state_file)
            if resource_state.get("mode") != "resource":
                return 0
            artifact_metadata = resource_score_artifact_metadata(
                resource_state,
                original_exe=args.original_exe,
                recompiled_exe=args.recompiled_exe,
                artifact=args.artifact,
            )
            if args.raw_score is not None and not math.isclose(
                args.raw_score,
                float(artifact_metadata["raw_score"]),
                abs_tol=1e-6,
            ):
                raise ValueError("--raw-score disagrees with the resource artifact")
            if args.artifact_sha256 is not None and (
                args.artifact_sha256 != artifact_metadata["artifact_sha256"]
            ):
                raise ValueError(
                    "--artifact-sha256 disagrees with the resource artifact"
                )
            if args.effective and not artifact_metadata["effective"]:
                raise ValueError("--effective disagrees with the resource artifact")
            status = mark_resource_score(
                args.state_file,
                utc_now(),
                raw_score=float(artifact_metadata["raw_score"]),
                model=args.model,
                artifact_sha256=str(artifact_metadata["artifact_sha256"]),
                effective=bool(artifact_metadata["effective"]),
            )
            if not args.quiet and status != "inactive":
                print(f"First resource-score stamp: {status}.")
            return 0
        if args.command == "phase":
            state = mark_phase(args.state_file, args.name, utc_now())
            print(f"Recorded the {state['phase']} campaign phase.")
            return 0
        if args.command == "add-target":
            active_state = read_state(args.state_file)
            prediction_arguments = _cli_prediction_arguments(
                args,
                lane=_campaign_lane(active_state),
                mode=str(active_state.get("mode", "")),
                addresses=[args.address],
            )
            pivot_subsystem = prediction_arguments.get("subsystem")
            if pivot_subsystem is not None and pivot_subsystem != active_state.get(
                "subsystem"
            ):
                raise ValueError(
                    "the pivot prediction subsystem disagrees with the campaign"
                )
            state = add_target(
                args.state_file,
                args.address,
                utc_now(),
                args.functions_map,
                args.function_sizes,
                args.source_root,
                doctor_receipt_path=args.doctor_receipt,
                brief_path=args.brief,
                require_brief=True,
                replaces=args.replace,
                expected_minutes=prediction_arguments["expected_minutes"],
                expected_retained_bytes=prediction_arguments[
                    "expected_retained_bytes"
                ],
                prediction_version=prediction_arguments["prediction_version"],
                prediction_lower_bound_bytes=prediction_arguments[
                    "prediction_lower_bound_bytes"
                ],
                prediction_features=prediction_arguments["prediction_features"],
                require_prediction_metadata=True,
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
                progress_after=(
                    _read_json_object(args.progress_after, "progress report")
                    if args.progress_after is not None
                    else None
                ),
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
            record_evidence(
                args.file,
                args.address,
                args.kind,
                args.note,
                utc_now(),
                failed_campaign_id=args.failed_campaign_id,
                failed_model=args.failed_model,
                changed_assumption=args.changed_assumption,
                changed_source=args.changed_source,
            )
            print(f"Recorded new evidence for {len(args.address)} target(s).")
            return 0
        if args.command == "delivery":
            item = record_delivery(
                args.file,
                args.campaign_id,
                args.status,
                artifact_sha256=args.artifact_sha256,
                receipt_sha256=args.receipt_sha256,
                delivery_receipt_path=args.delivery_receipt,
                base_commit=args.base_commit,
                commit=args.commit,
                note=args.note,
                now=utc_now(),
                root=args.worktree_root,
            )
            print(f"Recorded the {item['status']} delivery event.")
            return 0
        if args.command == "delivery-verify":
            receipt = create_delivery_receipt(
                args.file,
                args.campaign_id,
                args.commit,
                args.base_commit,
                root=args.worktree_root,
            )
            print(json.dumps(receipt, indent=2, sort_keys=True))
            return 0
        if args.command == "finalize":
            if args.mode is None:
                raise ValueError("standard finalization needs --mode")
            receipt = finalize_standard(
                args.state_file,
                args.result,
                mode=args.mode,
                targets=args.target,
                resource=format_resource(args.resource)
                if args.resource is not None
                else None,
                staged=args.staged,
                cache_root=args.cache_root,
            )
            action = "Reused" if receipt.get("reused") else "Created"
            print(f"{action} finalization receipt {receipt['receipt_key']}.")
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
        print_summary(
            read_records(args.file),
            args.limit,
            window=args.window,
            json_output=args.json,
        )
        return 0
    except ValueError as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
