import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import decomp_options as options


ROOT = Path(__file__).resolve().parents[2]


def digest(label: str) -> str:
    return hashlib.sha256(label.encode("utf-8")).hexdigest()


def make_preregistration(*, sweep: bool = False, synthetic: bool = False):
    option_names = ["candidate-gate", "context-route"] if sweep else ["candidate-gate"]
    baseline = {name: False for name in option_names}
    treatments = [
        {
            "id": "candidate-on",
            "options": {
                name: name == "candidate-gate" for name in option_names
            },
        }
    ]
    if sweep:
        treatments.append(
            {
                "id": "context-on",
                "options": {
                    name: name == "context-route" for name in option_names
                },
            }
        )
    value = {
        "schema_version": 1,
        "kind": "option-study-preregistration",
        "study_id": "0" * 64,
        "design": "treatment-sweep" if sweep else "paired",
        "synthetic": synthetic,
        "population": [
            {
                "case_commitment": digest(f"case-{index}"),
                "campaign_commitment": digest(f"campaign-{index}"),
                "target_commitment": digest(f"target-{index}"),
            }
            for index in range(20)
        ],
        "baseline": {"id": "off", "options": baseline},
        "treatments": treatments,
        "primary_metric": {"name": "retained-bytes", "direction": "maximize"},
        "protected_metrics": [
            {"name": "terminal-bytes", "direction": "maximize"},
            {"name": "elapsed-budget", "direction": "minimize"},
        ],
        "budget": {"unit": "attempts", "per_case_limit": 7},
        "stop_policy": {"kind": "fixed-population", "early_stop": False},
        "row_policy": {
            "missing": "fail",
            "error": "fail",
            "invalid": "fail",
            "extra": "fail",
        },
    }
    value["study_id"] = options.preregistration_id(value)
    return value


def make_registry(*, sweep: bool = False, include_study: bool = True):
    names = ["candidate-gate", "context-route"] if sweep else ["candidate-gate"]
    return {
        "schema_version": 1,
        "kind": "decomp-option-registry",
        "options": [
            {
                "name": name,
                "description": f"Test {name}.",
                "default": False,
                "enabled": False,
                "activation_study_id": None,
            }
            for name in names
        ],
        "preregistrations": (
            [make_preregistration(sweep=sweep)] if include_study else []
        ),
        "route_training": {
            "kind": "route-policy",
            "enabled": False,
            "activation_study_id": None,
            "data_gate_receipt_sha256": None,
        },
    }


class DecompOptionTests(unittest.TestCase):
    def test_committed_registry_is_empty_and_default_off(self):
        registry, _ = options.read_registry(ROOT)
        self.assertEqual(registry["options"], [])
        self.assertEqual(registry["preregistrations"], [])
        self.assertEqual(
            registry["route_training"],
            {
                "kind": "route-policy",
                "enabled": False,
                "activation_study_id": None,
                "data_gate_receipt_sha256": None,
            },
        )
        self.assertEqual(options.public_status(ROOT)["status"], "unavailable")

    def test_registry_accepts_a_complete_preregistration(self):
        registry = options.validate_registry(make_registry(sweep=True))
        study = registry["preregistrations"][0]
        self.assertEqual(study["study_id"], options.preregistration_id(study))
        self.assertEqual(len(study["treatments"]), 2)

    def test_registry_rejects_enabled_or_linked_controls(self):
        cases = []
        registry = make_registry(include_study=False)
        registry["options"][0]["enabled"] = True
        cases.append(registry)
        registry = make_registry(include_study=False)
        registry["options"][0]["activation_study_id"] = digest("study")
        cases.append(registry)
        registry = make_registry(include_study=False)
        registry["route_training"]["enabled"] = True
        cases.append(registry)
        registry = make_registry(include_study=False)
        registry["route_training"]["data_gate_receipt_sha256"] = digest("gate")
        cases.append(registry)
        for value in cases:
            with self.subTest(value=value), self.assertRaises(options.OptionError):
                options.validate_registry(value)

    def test_require_has_no_activation_bypass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / options.REGISTRY_RELATIVE
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(make_registry(include_study=False)), encoding="utf-8")
            with mock.patch.dict(os.environ, {"TOY2_CANDIDATE_GATE": "1"}):
                with self.assertRaisesRegex(options.OptionError, "unavailable"):
                    options.require_option("candidate-gate", root)

    def test_preregistration_rejects_duplicate_or_baseline_treatments(self):
        value = make_preregistration(sweep=True)
        value["treatments"][1]["id"] = value["treatments"][0]["id"]
        value["study_id"] = options.preregistration_id(value)
        with self.assertRaisesRegex(options.OptionError, "not distinct"):
            options.validate_registry(
                {**make_registry(sweep=True), "preregistrations": [value]}
            )
        value = make_preregistration()
        value["treatments"][0]["options"] = dict(value["baseline"]["options"])
        value["study_id"] = options.preregistration_id(value)
        with self.assertRaisesRegex(options.OptionError, "not distinct"):
            options.validate_registry(
                {**make_registry(), "preregistrations": [value]}
            )

    def test_preregistration_requires_every_option_in_each_arm(self):
        value = make_preregistration(sweep=True)
        del value["baseline"]["options"]["context-route"]
        value["study_id"] = options.preregistration_id(value)
        with self.assertRaisesRegex(options.OptionError, "every registered option"):
            options.validate_registry(
                {**make_registry(sweep=True), "preregistrations": [value]}
            )

    def test_preregistration_rejects_boolean_budget_and_changed_id(self):
        value = make_preregistration()
        value["budget"]["per_case_limit"] = True
        value["study_id"] = options.preregistration_id(value)
        with self.assertRaisesRegex(options.OptionError, "per-case budget"):
            options.validate_registry(
                {**make_registry(), "preregistrations": [value]}
            )
        value = make_preregistration()
        value["study_id"] = digest("wrong")
        with self.assertRaisesRegex(options.OptionError, "does not match"):
            options.validate_registry(
                {**make_registry(), "preregistrations": [value]}
            )

    def test_strict_json_rejects_duplicate_keys_and_nonfinite_numbers(self):
        with self.assertRaisesRegex(options.OptionError, "repeats"):
            options._strict_json(b'{"kind": 1, "kind": 2}')
        with self.assertRaisesRegex(options.OptionError, "NaN"):
            options._strict_json(b'{"value": NaN}')

    def test_reader_rejects_final_and_intermediate_symlinks_and_hardlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            real = root / "real"
            (real / "Resources").mkdir(parents=True)
            content = json.dumps(make_registry(include_study=False))
            source = real / "Resources/decomp-options.json"
            source.write_text(content, encoding="utf-8")
            (root / "tools").symlink_to(real, target_is_directory=True)
            with self.assertRaises(options.OptionError):
                options.read_registry(root)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / options.REGISTRY_RELATIVE
            path.parent.mkdir(parents=True)
            outside = root / "outside.json"
            outside.write_text(json.dumps(make_registry(include_study=False)), encoding="utf-8")
            path.symlink_to(outside)
            with self.assertRaises(options.OptionError):
                options.read_registry(root)
            path.unlink()
            os.link(outside, path)
            with self.assertRaises(options.OptionError):
                options.read_registry(root)

    def test_reader_refuses_without_descriptor_relative_access(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / options.REGISTRY_RELATIVE
            path.parent.mkdir(parents=True)
            path.write_text(
                json.dumps(make_registry(include_study=False)), encoding="utf-8"
            )
            with mock.patch.object(options, "_supports_descriptor_walk", return_value=False):
                with self.assertRaisesRegex(options.OptionError, "access is unavailable"):
                    options.read_registry(root)


if __name__ == "__main__":
    unittest.main()
