#!/usr/bin/env python3
"""Validate the default-off decompilation option registry."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
from typing import Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
REGISTRY_RELATIVE = Path("tools/Resources/decomp-options.json")
SCHEMA_VERSION = 1
MAX_REGISTRY_BYTES = 2 * 1024 * 1024
MAX_OPTIONS = 128
MAX_STUDIES = 1024
MAX_POPULATION = 4096
MAX_TREATMENTS = 16
MAX_PROTECTED_METRICS = 32
MAX_TEXT = 160

HASH_RE = re.compile(r"[0-9a-f]{64}\Z")
NAME_RE = re.compile(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*\Z")

REGISTRY_KEYS = {
    "schema_version",
    "kind",
    "options",
    "preregistrations",
    "route_training",
}
OPTION_KEYS = {
    "name",
    "description",
    "default",
    "enabled",
    "activation_study_id",
}
TRAINING_KEYS = {
    "kind",
    "enabled",
    "activation_study_id",
    "data_gate_receipt_sha256",
}
PREREGISTRATION_KEYS = {
    "schema_version",
    "kind",
    "study_id",
    "design",
    "synthetic",
    "population",
    "baseline",
    "treatments",
    "primary_metric",
    "protected_metrics",
    "budget",
    "stop_policy",
    "row_policy",
}
POPULATION_KEYS = {
    "case_commitment",
    "campaign_commitment",
    "target_commitment",
}
ARM_KEYS = {"id", "options"}
METRIC_KEYS = {"name", "direction"}
BUDGET_KEYS = {"unit", "per_case_limit"}
STOP_POLICY_KEYS = {"kind", "early_stop"}
ROW_POLICY_KEYS = {"missing", "error", "invalid", "extra"}


class OptionError(ValueError):
    """Report an invalid or unavailable option."""


def _reject_constant(value: str) -> object:
    raise OptionError(f"the option registry contains {value}")


def _object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise OptionError(f"the option registry repeats the {key!r} key")
        result[key] = value
    return result


def _strict_json(content: bytes) -> dict[str, object]:
    try:
        text = content.decode("utf-8")
        document = json.loads(
            text,
            object_pairs_hook=_object,
            parse_constant=_reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise OptionError(f"cannot parse the option registry: {error}") from error
    if not isinstance(document, dict):
        raise OptionError("the option registry must be a JSON object")
    return document


def _strict_keys(
    value: Mapping[str, object], keys: set[str], description: str
) -> None:
    if set(value) != keys:
        raise OptionError(f"the {description} keys are invalid")


def _name(value: object, description: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) > MAX_TEXT
        or NAME_RE.fullmatch(value) is None
    ):
        raise OptionError(f"the {description} is invalid")
    return value


def _text(value: object, description: str) -> str:
    if (
        not isinstance(value, str)
        or not value.strip()
        or value != value.strip()
        or len(value) > MAX_TEXT
        or any(ord(character) < 0x20 for character in value)
    ):
        raise OptionError(f"the {description} is invalid")
    return value


def _commitment(value: object, description: str) -> str:
    if not isinstance(value, str) or HASH_RE.fullmatch(value) is None:
        raise OptionError(f"the {description} is invalid")
    return value


def _positive_int(value: object, description: str, maximum: int) -> int:
    if type(value) is not int or value <= 0 or value > maximum:
        raise OptionError(f"the {description} is invalid")
    return value


def canonical_json(value: object) -> bytes:
    """Encode one object for a stable content hash."""

    return json.dumps(
        value,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def content_sha256(value: object) -> str:
    """Return the canonical SHA-256 value for one object."""

    return hashlib.sha256(canonical_json(value)).hexdigest()


def preregistration_id(value: Mapping[str, object]) -> str:
    """Return the content ID without the stored ID field."""

    payload = dict(value)
    payload.pop("study_id", None)
    return content_sha256(payload)


def _validate_metric(value: object, description: str) -> dict[str, object]:
    if not isinstance(value, Mapping):
        raise OptionError(f"the {description} is invalid")
    _strict_keys(value, METRIC_KEYS, description)
    name = _name(value.get("name"), f"{description} name")
    direction = value.get("direction")
    if direction not in {"maximize", "minimize"}:
        raise OptionError(f"the {description} direction is invalid")
    return {"name": name, "direction": direction}


def _validate_options_map(
    value: object,
    *,
    option_names: set[str],
    description: str,
) -> dict[str, bool]:
    if not isinstance(value, Mapping) or not value:
        raise OptionError(f"the {description} is invalid")
    result: dict[str, bool] = {}
    for raw_name, raw_enabled in value.items():
        name = _name(raw_name, f"{description} option name")
        if name not in option_names or type(raw_enabled) is not bool:
            raise OptionError(f"the {description} is invalid")
        result[name] = raw_enabled
    return dict(sorted(result.items()))


def validate_preregistration(
    value: object,
    *,
    option_names: set[str],
) -> dict[str, object]:
    """Validate one fixed population study declaration."""

    if not isinstance(value, Mapping):
        raise OptionError("a study preregistration is invalid")
    _strict_keys(value, PREREGISTRATION_KEYS, "study preregistration")
    if (
        type(value.get("schema_version")) is not int
        or value.get("schema_version") != SCHEMA_VERSION
        or value.get("kind") != "option-study-preregistration"
        or type(value.get("synthetic")) is not bool
    ):
        raise OptionError("a study preregistration identity is invalid")
    design = value.get("design")
    if design not in {"paired", "treatment-sweep"}:
        raise OptionError("the study design is invalid")

    population = value.get("population")
    if (
        not isinstance(population, list)
        or not population
        or len(population) > MAX_POPULATION
    ):
        raise OptionError("the study population is invalid")
    normalized_population: list[dict[str, str]] = []
    case_keys: set[str] = set()
    triples: set[tuple[str, str, str]] = set()
    for item in population:
        if not isinstance(item, Mapping):
            raise OptionError("a study population row is invalid")
        _strict_keys(item, POPULATION_KEYS, "study population row")
        row = {
            key: _commitment(item.get(key), f"study {key}")
            for key in sorted(POPULATION_KEYS)
        }
        triple = (
            row["case_commitment"],
            row["campaign_commitment"],
            row["target_commitment"],
        )
        if row["case_commitment"] in case_keys or triple in triples:
            raise OptionError("the study population repeats a case")
        case_keys.add(row["case_commitment"])
        triples.add(triple)
        normalized_population.append(row)

    baseline = value.get("baseline")
    if not isinstance(baseline, Mapping):
        raise OptionError("the study baseline is invalid")
    _strict_keys(baseline, ARM_KEYS, "study baseline")
    if baseline.get("id") != "off":
        raise OptionError("the study baseline ID must be off")
    baseline_options = _validate_options_map(
        baseline.get("options"),
        option_names=option_names,
        description="study baseline option map",
    )
    if any(baseline_options.values()):
        raise OptionError("each study baseline option must be off")
    if set(baseline_options) != option_names:
        raise OptionError("the study baseline must declare every registered option")

    treatments = value.get("treatments")
    if (
        not isinstance(treatments, list)
        or not treatments
        or len(treatments) > MAX_TREATMENTS
    ):
        raise OptionError("the study treatment list is invalid")
    if design == "paired" and len(treatments) != 1:
        raise OptionError("a paired study must declare one treatment")
    normalized_treatments: list[dict[str, object]] = []
    treatment_ids: set[str] = set()
    treatment_maps: set[tuple[tuple[str, bool], ...]] = set()
    baseline_items = tuple(sorted(baseline_options.items()))
    for item in treatments:
        if not isinstance(item, Mapping):
            raise OptionError("a study treatment is invalid")
        _strict_keys(item, ARM_KEYS, "study treatment")
        treatment_id = _name(item.get("id"), "study treatment ID")
        options = _validate_options_map(
            item.get("options"),
            option_names=option_names,
            description="study treatment option map",
        )
        option_items = tuple(sorted(options.items()))
        if set(options) != set(baseline_options):
            raise OptionError("each study arm must declare the same options")
        if (
            treatment_id == "off"
            or treatment_id in treatment_ids
            or option_items in treatment_maps
            or option_items == baseline_items
        ):
            raise OptionError("the study treatments are not distinct")
        treatment_ids.add(treatment_id)
        treatment_maps.add(option_items)
        normalized_treatments.append({"id": treatment_id, "options": options})

    primary = _validate_metric(value.get("primary_metric"), "primary metric")
    protected = value.get("protected_metrics")
    if not isinstance(protected, list) or len(protected) > MAX_PROTECTED_METRICS:
        raise OptionError("the protected metric list is invalid")
    normalized_protected = [
        _validate_metric(item, "protected metric") for item in protected
    ]
    metric_names = [str(primary["name"])] + [
        str(item["name"]) for item in normalized_protected
    ]
    if len(metric_names) != len(set(metric_names)):
        raise OptionError("the study metric names are not distinct")

    budget = value.get("budget")
    if not isinstance(budget, Mapping):
        raise OptionError("the study budget is invalid")
    _strict_keys(budget, BUDGET_KEYS, "study budget")
    normalized_budget = {
        "unit": _name(budget.get("unit"), "study budget unit"),
        "per_case_limit": _positive_int(
            budget.get("per_case_limit"),
            "study per-case budget",
            2**63 - 1,
        ),
    }

    stop_policy = value.get("stop_policy")
    if not isinstance(stop_policy, Mapping):
        raise OptionError("the study stop policy is invalid")
    _strict_keys(stop_policy, STOP_POLICY_KEYS, "study stop policy")
    if (
        stop_policy.get("kind") != "fixed-population"
        or stop_policy.get("early_stop") is not False
    ):
        raise OptionError("the study stop policy is invalid")

    row_policy = value.get("row_policy")
    if not isinstance(row_policy, Mapping):
        raise OptionError("the study row policy is invalid")
    _strict_keys(row_policy, ROW_POLICY_KEYS, "study row policy")
    if any(row_policy.get(key) != "fail" for key in ROW_POLICY_KEYS):
        raise OptionError("the study row policy must fail closed")

    normalized: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "kind": "option-study-preregistration",
        "study_id": _commitment(value.get("study_id"), "study ID"),
        "design": design,
        "synthetic": value.get("synthetic"),
        "population": normalized_population,
        "baseline": {"id": "off", "options": baseline_options},
        "treatments": normalized_treatments,
        "primary_metric": primary,
        "protected_metrics": normalized_protected,
        "budget": normalized_budget,
        "stop_policy": {"kind": "fixed-population", "early_stop": False},
        "row_policy": {key: "fail" for key in sorted(ROW_POLICY_KEYS)},
    }
    if normalized["study_id"] != preregistration_id(normalized):
        raise OptionError("the study ID does not match the preregistration")
    return normalized


def validate_registry(document: object) -> dict[str, object]:
    """Validate the complete option registry."""

    if not isinstance(document, Mapping):
        raise OptionError("the option registry must be a JSON object")
    _strict_keys(document, REGISTRY_KEYS, "option registry")
    if (
        type(document.get("schema_version")) is not int
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("kind") != "decomp-option-registry"
    ):
        raise OptionError("the option registry identity is invalid")

    options = document.get("options")
    if not isinstance(options, list) or len(options) > MAX_OPTIONS:
        raise OptionError("the option list is invalid")
    normalized_options: list[dict[str, object]] = []
    option_names: set[str] = set()
    for item in options:
        if not isinstance(item, Mapping):
            raise OptionError("an option entry is invalid")
        _strict_keys(item, OPTION_KEYS, "option entry")
        name = _name(item.get("name"), "option name")
        if name in option_names:
            raise OptionError("the option registry repeats an option")
        if (
            item.get("default") is not False
            or item.get("enabled") is not False
            or item.get("activation_study_id") is not None
        ):
            raise OptionError("each option must remain disabled and unlinked")
        option_names.add(name)
        normalized_options.append(
            {
                "name": name,
                "description": _text(item.get("description"), "option description"),
                "default": False,
                "enabled": False,
                "activation_study_id": None,
            }
        )

    preregistrations = document.get("preregistrations")
    if (
        not isinstance(preregistrations, list)
        or len(preregistrations) > MAX_STUDIES
    ):
        raise OptionError("the preregistration list is invalid")
    normalized_preregistrations = [
        validate_preregistration(item, option_names=option_names)
        for item in preregistrations
    ]
    study_ids = [str(item["study_id"]) for item in normalized_preregistrations]
    if len(study_ids) != len(set(study_ids)):
        raise OptionError("the option registry repeats a study")

    training = document.get("route_training")
    if not isinstance(training, Mapping):
        raise OptionError("the route training entry is invalid")
    _strict_keys(training, TRAINING_KEYS, "route training entry")
    if (
        training.get("kind") != "route-policy"
        or training.get("enabled") is not False
        or training.get("activation_study_id") is not None
        or training.get("data_gate_receipt_sha256") is not None
    ):
        raise OptionError("route training must remain disabled and unlinked")

    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "decomp-option-registry",
        "options": normalized_options,
        "preregistrations": normalized_preregistrations,
        "route_training": {
            "kind": "route-policy",
            "enabled": False,
            "activation_study_id": None,
            "data_gate_receipt_sha256": None,
        },
    }


def _supports_descriptor_walk() -> bool:
    return os.name == "posix" and os.open in os.supports_dir_fd


def _read_bounded_regular_file(path: Path) -> bytes:
    """Read one stable file without following a path component."""

    absolute = Path(os.path.abspath(path))
    if not _supports_descriptor_walk():
        raise OptionError("secure option registry access is unavailable")

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
            content = stream.read(MAX_REGISTRY_BYTES + 1)
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
            or after.st_size > MAX_REGISTRY_BYTES
            or len(content) != after.st_size
        ):
            raise OSError
        return content
    except (OSError, TypeError, NotImplementedError) as error:
        raise OptionError("the option registry is missing or invalid") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if directory >= 0:
            os.close(directory)


def read_registry(root: Path = ROOT) -> tuple[dict[str, object], bytes]:
    """Read one bounded regular registry file."""

    path = root.resolve() / REGISTRY_RELATIVE
    content = _read_bounded_regular_file(path)
    return validate_registry(_strict_json(content)), content


def find_preregistration(
    registry: Mapping[str, object], study_id: str
) -> dict[str, object] | None:
    """Return one preregistration by its content ID."""

    if HASH_RE.fullmatch(study_id) is None:
        raise OptionError("the study ID is invalid")
    values = registry.get("preregistrations")
    if not isinstance(values, list):
        raise OptionError("the preregistration list is invalid")
    for item in values:
        if isinstance(item, dict) and item.get("study_id") == study_id:
            return item
    return None


def public_status(root: Path = ROOT) -> dict[str, object]:
    """Return a non-identifying default-off registry summary."""

    registry, content = read_registry(root)
    options = registry["options"]
    preregistrations = registry["preregistrations"]
    assert isinstance(options, list) and isinstance(preregistrations, list)
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "decomp-option-status",
        "status": "unavailable",
        "registry_sha256": hashlib.sha256(content).hexdigest(),
        "options": [
            {
                "name": item["name"],
                "enabled": False,
                "available": False,
            }
            for item in options
            if isinstance(item, dict)
        ],
        "preregistered_studies": len(preregistrations),
        "route_training": {"enabled": False, "available": False},
    }


def require_option(name: str, root: Path = ROOT) -> None:
    """Fail closed because this campaign cannot activate options."""

    registry, _ = read_registry(root)
    _name(name, "option name")
    options = registry.get("options")
    assert isinstance(options, list)
    if not any(isinstance(item, dict) and item.get("name") == name for item in options):
        raise OptionError(f"option {name!r} is not registered")
    raise OptionError(f"option {name!r} is unavailable")


def _print(document: Mapping[str, object], *, as_json: bool) -> None:
    if as_json:
        print(json.dumps(document, indent=2, sort_keys=True))
        return
    print(f"Status: {document.get('status', 'unavailable')}")
    print(f"Preregistered studies: {document.get('preregistered_studies', 0)}")
    print("Route training: unavailable")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    status = subparsers.add_parser("status", help="show the default-off registry")
    status.add_argument("--json", action="store_true")
    require = subparsers.add_parser("require", help="require one active option")
    require.add_argument("name")
    require.add_argument("--json", action="store_true")
    training = subparsers.add_parser(
        "training-status", help="show the route training gate"
    )
    training.add_argument("--json", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "require":
            require_option(args.name, ROOT)
            return 0
        document = public_status(ROOT)
        if args.command == "training-status":
            document = {
                "schema_version": SCHEMA_VERSION,
                "kind": "route-training-status",
                "status": "unavailable",
                "enabled": False,
                "available": False,
                "data_gate_receipt_sha256": None,
                "activation_study_id": None,
            }
        _print(document, as_json=args.json)
        return 0
    except OptionError as error:
        if getattr(args, "json", False):
            print(
                json.dumps(
                    {
                        "schema_version": SCHEMA_VERSION,
                        "kind": "decomp-option-error",
                        "status": "unavailable",
                        "error": str(error),
                    },
                    indent=2,
                    sort_keys=True,
                ),
                file=sys.stderr,
            )
        else:
            print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
