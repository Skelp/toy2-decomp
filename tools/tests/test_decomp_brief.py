from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import decomp_brief as brief  # noqa: E402
from tools import decomp_doctor as doctor  # noqa: E402


class HeadRunner:
    def __init__(self):
        self.calls = 0

    def __call__(self, command: list[str], **kwargs: object) -> subprocess.CompletedProcess:
        self.calls += 1
        if command == ["git", "rev-parse", "HEAD"]:
            return subprocess.CompletedProcess(command, 0, "a" * 40 + "\n", "")
        return subprocess.CompletedProcess(command, 1, "", "unexpected command")


def prepare_root(root: Path) -> None:
    for relative, content in {
        "tools/Resources/functions_map.txt": "0x00401000 Probe\n",
        "build/decomp-current-report.json": '{"data":[{"address":"0x00401000","matching":0.6}]}',
        "build/decomp-function-sizes.json": '[{"address":"0x00401000","size":8}]',
        "tools/Resources/campaign-ledger.jsonl": "",
        "tools/Resources/reconstruction-blockers.tsv": "",
        "tools/Resources/tool_artifacts.tsv": "",
    }.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def evidence(*, disassembly: bool = True, decompilation: bool = True) -> dict[str, object]:
    return {
        "source_evidence": {
            "disassembly_nonempty": disassembly,
            "decompilation_nonempty": decompilation,
        },
        "readiness": True,
        "rejection_reasons": [],
    }


def source_receipt(root: Path) -> dict[str, object]:
    target = "0x00401000"
    disassembly = doctor._store_ghidra_artifact(
        root,
        target,
        "disassembly",
        [{"address": target, "bytes": "c3", "mnemonic": "ret", "operands": []}],
    )
    decompilation = doctor._store_ghidra_artifact(
        root,
        target,
        "decompilation",
        [{"code": "void Probe() {}"}],
    )
    function = doctor._store_ghidra_artifact(
        root,
        target,
        "function",
        {"address": target, "name": "Probe"},
    )
    xrefs = doctor._store_ghidra_artifact(root, target, "xrefs", [])
    return {
        "receipt_id": "d" * 64,
        "mode": "refinement",
        "lane": "production",
        "addresses": [target],
        "resource": None,
        "checks": [
            {
                "name": "disassembly",
                "ok": True,
                "data": {"target": target, "artifact": disassembly},
            },
            {
                "name": "decompilation",
                "ok": True,
                "data": {"target": target, "artifact": decompilation},
            },
            {
                "name": "function",
                "ok": True,
                "data": {"target": target, "artifact": function},
            },
            {
                "name": "xrefs",
                "ok": True,
                "data": {"target": target, "artifact": xrefs},
            },
        ],
    }


def write_scout_report(
    path: Path,
    doctor_path: Path,
    scout_id: str,
    *,
    lane: str = "production",
    target: str = "0x00401000",
    doctor_id: str = "d" * 64,
    doctor_hash: str | None = None,
    findings: list[dict[str, object]] | None = None,
    audit: str = "retail-abi-control-flow-evidence",
) -> Path:
    categories = sorted(brief.SCOUT_AUDIT_CATEGORIES[audit])
    default_findings = [
        {
            "category": category,
            "claim": f"{scout_id} confirmed {category} evidence.",
            "evidence": [
                {"source": "retail", "locator": f"{target}:{category}"}
            ],
        }
        for category in categories
    ]
    document = {
        "schema": 2,
        "scout_id": scout_id,
        "audit": audit,
        "lane": lane,
        "target": target,
        "access": "read-only",
        "owns_mutations": False,
        "doctor_receipt": {
            "receipt_id": doctor_id,
            "sha256": doctor_hash or brief._sha256(doctor_path),
        },
        "findings": findings or default_findings,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document), encoding="utf-8")
    return path


def write_scout_reports(root: Path, doctor_path: Path) -> list[Path]:
    return [
        write_scout_report(
            root / "build/scouts/retail.json",
            doctor_path,
            "retail-evidence-scout",
        ),
        write_scout_report(
            root / "build/scouts/abi.json",
            doctor_path,
            "abi-evidence-scout",
            audit="callers-types-layout-translation-unit-analogue",
        ),
    ]


class EvidenceRunner:
    def __init__(self):
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], **_kwargs: object) -> subprocess.CompletedProcess:
        self.commands.append(command)
        if command == ["git", "rev-parse", "HEAD"]:
            return subprocess.CompletedProcess(command, 0, "a" * 40 + "\n", "")
        if "x-ref" in command:
            payload: object = []
        elif "disasm" in command:
            payload = [{"address": "0x00401000", "mnemonic": "ret"}]
        elif "decompile" in command:
            payload = [{"code": "void Probe() {}"}]
        else:
            payload = {"address": "0x00401000", "name": "Probe"}
        return subprocess.CompletedProcess(command, 0, json.dumps(payload), "")


class BriefTests(unittest.TestCase):
    def test_source_subsystem_uses_translation_unit_for_global_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            source = root / "src/PlayerControl.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// FUNCTION: TOY2 0x00401000 [PROVISIONAL]\n",
                encoding="utf-8",
            )
            self.assertEqual(
                brief._source_subsystem(root, 0x00401000), "PlayerControl"
            )
            self.assertEqual(brief._source_subsystem(root, 0x00402000), "(global)")

    def setUp(self):
        self.production_descriptor_original = brief._production_mismatch_descriptor
        self.provenance_patch = patch.object(
            brief, "_validate_report_provenance", return_value=None
        )
        self.provenance_patch.start()
        self.addCleanup(self.provenance_patch.stop)
        self.production_diff_patch = patch.object(
            brief,
            "_production_mismatch_descriptor",
            return_value={"path": "fixture-diff", "sha256": "a" * 64},
        )
        self.production_diff_patch.start()
        self.addCleanup(self.production_diff_patch.stop)

    def test_control_flow_counts_an_internal_backward_branch(self):
        rows = [
            {
                "address": "0x00401008",
                "mnemonic": "mov",
                "operands": "eax, ecx",
            },
            {
                "address": "0x00401020",
                "mnemonic": "jne",
                "operands": "0x00401008",
            },
        ]
        control, _ = brief._control_flow(rows, 0x00401000, 0x40)
        self.assertEqual(control["back_edge_count"], 1)

    def test_brief_rejects_a_doctor_mode_from_another_lane(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            doctor_path = root / "doctor.json"
            doctor_path.write_text("{}", encoding="utf-8")
            receipt = {
                "mode": "coverage",
                "lane": "closure",
                "addresses": ["0x00401000"],
                "resource": None,
            }
            with (
                patch.object(brief, "validate_doctor_receipt", return_value=receipt) as validator,
                self.assertRaisesRegex(
                    brief.BriefError, "closure lane needs a refinement"
                ),
            ):
                brief._doctor_descriptor(
                    root,
                    "closure",
                    "0x00401000",
                    doctor_path,
                    HeadRunner(),
                )
            self.assertEqual(validator.call_args.kwargs["expected_mode"], "refinement")

    def test_cache_is_warm_until_an_input_hash_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = HeadRunner()
            builds: list[str] = []

            def builder(lane: str, target: str, _root: Path, _runner: brief.Runner) -> dict[str, object]:
                builds.append(f"{lane}:{target}")
                return evidence()

            cold_ticks = iter([10.0, 12.5])
            cold = brief.build_brief(
                "production",
                "0x401000",
                root=root,
                runner=runner,
                evidence_builder=builder,
                clock=lambda: next(cold_ticks),
            )
            warm_ticks = iter([20.0, 20.25])
            warm = brief.build_brief(
                "production",
                "0x401000",
                root=root,
                runner=runner,
                evidence_builder=builder,
                clock=lambda: next(warm_ticks),
            )
            report = root / brief.REPORT_RELATIVE
            report.write_text('{"data":[{"address":"0x00401000","matching":0.7}]}', encoding="utf-8")
            changed_ticks = iter([30.0, 31.0])
            changed = brief.build_brief(
                "production",
                "0x401000",
                root=root,
                runner=runner,
                evidence_builder=builder,
                clock=lambda: next(changed_ticks),
            )

            self.assertFalse(cold.cache_hit)
            self.assertEqual(cold.elapsed_seconds, 2.5)
            self.assertTrue(warm.cache_hit)
            self.assertEqual(warm.elapsed_seconds, 0.25)
            self.assertFalse(changed.cache_hit)
            self.assertNotEqual(cold.path, changed.path)
            self.assertEqual(builds, ["production:0x00401000", "production:0x00401000"])
            cache = (root / brief.CACHE_RELATIVE).resolve()
            self.assertTrue(cold.path.resolve().is_relative_to(cache))

    def test_production_mismatch_descriptor_binds_diff_and_sidecar(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = "0x00401000"
            mismatch = root / "build/decomp-diffs" / f"{target}.txt"
            sidecar = mismatch.with_suffix(mismatch.suffix + ".provenance.json")
            mismatch.parent.mkdir(parents=True)
            mismatch.write_text("diff\n", encoding="utf-8")
            sidecar.write_text("receipt\n", encoding="utf-8")
            receipt = {"kind": "function-diff", "address": target}
            with patch(
                "tools.decomp_provenance.validate_diff", return_value=receipt
            ) as validator:
                descriptor = self.production_descriptor_original(root, target)
            validator.assert_called_once_with(mismatch.resolve(), 0x00401000, root=root)
            self.assertEqual(descriptor["receipt"], receipt)
            self.assertEqual(descriptor["sha256"], brief._sha256(mismatch))
            self.assertEqual(
                descriptor["provenance_sha256"], brief._sha256(sidecar)
            )

    def test_production_mismatch_descriptor_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory, patch(
            "tools.decomp_provenance.validate_diff",
            side_effect=ValueError("stale mismatch"),
        ):
            with self.assertRaisesRegex(
                brief.BriefError, "needs a current .*bc 0x00401000"
            ):
                self.production_descriptor_original(
                    Path(directory), "0x00401000"
                )

    def test_cache_identity_mismatch_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = HeadRunner()
            result = brief.build_brief(
                "production", "0x401000", root=root, runner=runner, evidence_builder=lambda *_: evidence()
            )
            document = json.loads(result.path.read_text(encoding="utf-8"))
            document["inputs"]["head"] = "b" * 40
            result.path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(brief.BriefError, "input hashes"):
                brief.build_brief(
                    "production", "0x401000", root=root, runner=runner, evidence_builder=lambda *_: evidence()
                )

    def test_cache_content_hash_detects_evidence_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = HeadRunner()
            result = brief.build_brief(
                "production", "0x401000", root=root, runner=runner, evidence_builder=lambda *_: evidence()
            )
            document = json.loads(result.path.read_text(encoding="utf-8"))
            document["evidence"]["readiness"] = False
            result.path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(brief.BriefError, "content hash"):
                brief.validate_brief(
                    result.path,
                    "production",
                    "0x401000",
                    root=root,
                    runner=runner,
                    require_scout_reports=False,
                )

    def test_brief_identity_includes_the_doctor_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = HeadRunner()
            receipt_path = root / "build/decomp-cache/doctor/latest.json"
            receipt_path.parent.mkdir(parents=True)
            receipt_path.write_text('{"receipt":"first"}\n', encoding="utf-8")
            validated = source_receipt(root)
            with patch.object(brief, "validate_doctor_receipt", return_value=validated):
                result = brief.build_brief(
                    "production",
                    "0x401000",
                    root=root,
                    runner=runner,
                    evidence_builder=lambda *_: evidence(),
                    doctor_receipt_path=receipt_path,
                )
                descriptor = result.brief["inputs"]["doctor_receipt"]
                self.assertEqual(descriptor["path"], str(receipt_path.resolve()))
                self.assertEqual(descriptor["receipt_id"], "d" * 64)
                self.assertEqual(descriptor["sha256"], brief._sha256(receipt_path))
                self.assertEqual(
                    set(descriptor["source_artifacts"]),
                    {"disassembly", "decompilation", "function", "xrefs"},
                )
                self.assertEqual(
                    brief.validate_brief(
                        result.path,
                        "production",
                        "0x401000",
                        root=root,
                        runner=runner,
                        doctor_receipt_path=receipt_path,
                        require_scout_reports=False,
                    )["doctor_receipt"],
                    descriptor,
                )

                receipt_path.write_text('{"receipt":"second"}\n', encoding="utf-8")
                with self.assertRaisesRegex(brief.BriefError, "path does not match"):
                    brief.validate_brief(
                        result.path,
                        "production",
                        "0x401000",
                        root=root,
                        runner=runner,
                        doctor_receipt_path=receipt_path,
                        require_scout_reports=False,
                    )

    def test_doctor_bound_source_evidence_uses_no_live_queries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            receipt = source_receipt(root)
            artifacts = doctor.source_artifact_descriptors(receipt, "0x00401000")
            cached_runner = EvidenceRunner()
            live_runner = EvidenceRunner()
            with (
                patch.object(brief.shutil, "which", return_value="/bin/ghidra"),
                patch.object(brief, "mirror_ghidra_bridge_markers"),
                patch.object(brief, "prune_ghidra_logs"),
                patch.object(
                    brief,
                    "_dwarf_evidence",
                    return_value={"available": False, "matches": []},
                ),
            ):
                cached = brief._source_evidence(
                    "production",
                    "0x00401000",
                    root,
                    cached_runner,
                    artifacts,
                )
                live = brief._source_evidence(
                    "production",
                    "0x00401000",
                    root,
                    live_runner,
                )

        self.assertEqual(len(cached_runner.commands), 0)
        self.assertEqual(len(live_runner.commands), 4)
        self.assertEqual(cached["source_evidence"]["origin"], "doctor-receipt")
        self.assertEqual(live["source_evidence"]["origin"], "live-query")

    def test_doctor_bound_brief_uses_the_reuse_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            receipt_path = root / doctor.RECEIPT_RELATIVE
            receipt_path.parent.mkdir(parents=True)
            receipt_path.write_text('{"receipt":"ready"}\n', encoding="utf-8")
            validated = source_receipt(root)
            runner = EvidenceRunner()
            with (
                patch.object(
                    brief,
                    "validate_doctor_receipt",
                    return_value=validated,
                ),
                patch.object(brief.shutil, "which", return_value="/bin/ghidra"),
                patch.object(brief, "mirror_ghidra_bridge_markers"),
                patch.object(brief, "prune_ghidra_logs"),
                patch.object(
                    brief,
                    "_dwarf_evidence",
                    return_value={"available": False, "matches": []},
                ),
            ):
                result = brief.build_brief(
                    "production",
                    "0x00401000",
                    root=root,
                    runner=runner,
                    doctor_receipt_path=receipt_path,
                )

        ghidra_commands = [
            command for command in runner.commands if command[0] == "/bin/ghidra"
        ]
        self.assertFalse(result.cache_hit)
        self.assertEqual(ghidra_commands, [])
        self.assertEqual(
            result.brief["evidence"]["source_evidence"]["origin"],
            "doctor-receipt",
        )

    def test_latest_doctor_receipt_resolves_to_its_immutable_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            validated = source_receipt(root)
            immutable_relative = (
                doctor.RECEIPT_DIRECTORY_RELATIVE
                / f"{validated['receipt_id']}.json"
            )
            validated["receipt_path"] = immutable_relative.as_posix()
            latest = root / doctor.RECEIPT_RELATIVE
            immutable = root / immutable_relative
            latest.parent.mkdir(parents=True, exist_ok=True)
            immutable.parent.mkdir(parents=True, exist_ok=True)
            latest.write_text('{"receipt":"same"}\n', encoding="utf-8")
            immutable.write_text('{"receipt":"same"}\n', encoding="utf-8")

            with patch.object(
                brief,
                "validate_doctor_receipt",
                return_value=validated,
            ):
                descriptor = brief._doctor_descriptor(
                    root,
                    "production",
                    "0x00401000",
                    latest,
                    HeadRunner(),
                )
            immutable_hash = brief._sha256(immutable)
            latest.write_text('{"receipt":"new"}\n', encoding="utf-8")

            self.assertEqual(descriptor["path"], str(immutable.resolve()))
            self.assertEqual(descriptor["sha256"], immutable_hash)

    def test_supplied_doctor_artifact_failure_does_not_call_custom_builder(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            receipt_path = root / "build/decomp-cache/doctor/latest.json"
            receipt_path.parent.mkdir(parents=True)
            receipt_path.write_text('{"receipt":"ready"}\n', encoding="utf-8")
            validated = source_receipt(root)
            artifacts = doctor.source_artifact_descriptors(validated, "0x00401000")
            missing = root / str(artifacts["disassembly"]["path"])
            missing.unlink()
            builds: list[str] = []

            def builder(
                lane: str,
                target: str,
                _root: Path,
                _runner: brief.Runner,
            ) -> dict[str, object]:
                builds.append(f"{lane}:{target}")
                return evidence()

            with (
                patch.object(brief, "validate_doctor_receipt", return_value=validated),
                self.assertRaisesRegex(brief.BriefError, "artifact is invalid.*missing"),
            ):
                brief.build_brief(
                    "production",
                    "0x00401000",
                    root=root,
                    runner=HeadRunner(),
                    evidence_builder=builder,
                    doctor_receipt_path=receipt_path,
                )
        self.assertEqual(builds, [])

    def test_source_brief_rejects_each_empty_evidence_form(self):
        for result in (evidence(disassembly=False), evidence(decompilation=False)):
            with self.subTest(result=result), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                prepare_root(root)
                with self.assertRaisesRegex(brief.BriefError, "nonempty disassembly and decompilation"):
                    brief.build_brief(
                        "production",
                        "0x401000",
                        root=root,
                        runner=HeadRunner(),
                        evidence_builder=lambda *_args, value=result: value,
                    )

    def test_unbound_brief_does_not_claim_scout_work(self):
        roles = brief.scout_roles()
        self.assertEqual(roles["scouts"], [])
        self.assertEqual(roles["writer"]["access"], "writer")
        self.assertTrue(roles["writer"]["owns_mutations"])

    def test_scout_contract_uses_two_bound_read_only_reports(self):
        reports = [
            {
                "path": "/work/scout-a.json",
                "sha256": "a" * 64,
                "content": {
                    "scout_id": "scout-a",
                    "audit": "retail-abi-control-flow-evidence",
                },
            },
            {
                "path": "/work/scout-b.json",
                "sha256": "b" * 64,
                "content": {
                    "scout_id": "scout-b",
                    "audit": "callers-types-layout-translation-unit-analogue",
                },
            },
        ]
        roles = brief.scout_roles(reports)
        scouts = roles["scouts"]
        self.assertEqual(len(scouts), 2)
        self.assertTrue(all(item["access"] == "read-only" for item in scouts))
        self.assertTrue(all(item["owns_mutations"] is False for item in scouts))
        self.assertEqual([item["id"] for item in scouts], ["scout-a", "scout-b"])
        self.assertEqual(scouts[0]["report_sha256"], "a" * 64)
        self.assertEqual(roles["writer"]["access"], "writer")
        self.assertTrue(roles["writer"]["owns_mutations"])

    def test_bound_scout_reports_merge_into_the_brief(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            receipt_path = root / "build/decomp-cache/doctor/latest.json"
            receipt_path.parent.mkdir(parents=True)
            receipt_path.write_text('{"receipt":"ready"}\n', encoding="utf-8")
            report_paths = write_scout_reports(root, receipt_path)
            validated = source_receipt(root)
            with patch.object(
                brief, "validate_doctor_receipt", return_value=validated
            ):
                result = brief.build_brief(
                    "production",
                    "0x00401000",
                    root=root,
                    runner=HeadRunner(),
                    evidence_builder=lambda *_: evidence(),
                    doctor_receipt_path=receipt_path,
                    scout_report_paths=report_paths,
                )
                checked = brief.validate_brief(
                    result.path,
                    "production",
                    "0x00401000",
                    root=root,
                    runner=HeadRunner(),
                    doctor_receipt_path=receipt_path,
                )
                tampered = json.loads(result.path.read_text(encoding="utf-8"))
                tampered["evidence"]["context_pack"]["size"] = 9
                tampered["content_sha256"] = brief._content_hash(tampered)
                result.path.write_text(json.dumps(tampered), encoding="utf-8")
                with self.assertRaisesRegex(brief.BriefError, "context pack"):
                    brief.validate_brief(
                        result.path,
                        "production",
                        "0x00401000",
                        root=root,
                        runner=HeadRunner(),
                        doctor_receipt_path=receipt_path,
                    )

        self.assertEqual(
            [item["scout_id"] for item in result.brief["scout_findings"]],
            ["retail-evidence-scout", "abi-evidence-scout"],
        )
        self.assertEqual(
            {
                item["category"]
                for item in result.brief["scout_findings"][0]["findings"]
            },
            {"abi", "control-flow", "retail-evidence"},
        )
        self.assertEqual(len(checked["scout_reports"]), 2)
        context_pack = result.brief["evidence"]["context_pack"]
        self.assertEqual(context_pack["schema"], 1)
        self.assertEqual(context_pack["target"], "0x00401000")
        self.assertEqual(context_pack["size"], 8)
        self.assertEqual(
            context_pack["bindings"]["doctor_receipt"]["sha256"],
            result.brief["inputs"]["doctor_receipt"]["sha256"],
        )
        self.assertTrue(
            all("content" in item for item in result.brief["inputs"]["scout_reports"])
        )

    def test_bound_scout_reports_require_two_different_files_and_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            receipt_path = root / "build/decomp-cache/doctor/latest.json"
            receipt_path.parent.mkdir(parents=True)
            receipt_path.write_text('{"receipt":"ready"}\n', encoding="utf-8")
            first = write_scout_report(
                root / "build/scouts/first.json", receipt_path, "scout-a"
            )
            second = write_scout_report(
                root / "build/scouts/second.json",
                receipt_path,
                "scout-a",
                audit="callers-types-layout-translation-unit-analogue",
            )
            validated = source_receipt(root)
            with patch.object(
                brief, "validate_doctor_receipt", return_value=validated
            ):
                with self.assertRaisesRegex(brief.BriefError, "exactly two"):
                    brief.build_brief(
                        "production",
                        "0x00401000",
                        root=root,
                        runner=HeadRunner(),
                        evidence_builder=lambda *_: evidence(),
                        doctor_receipt_path=receipt_path,
                        scout_report_paths=[first],
                    )
                with self.assertRaisesRegex(brief.BriefError, "different scout report files"):
                    brief.build_brief(
                        "production",
                        "0x00401000",
                        root=root,
                        runner=HeadRunner(),
                        evidence_builder=lambda *_: evidence(),
                        doctor_receipt_path=receipt_path,
                        scout_report_paths=[first, first],
                    )
                with self.assertRaisesRegex(brief.BriefError, "different scout IDs"):
                    brief.build_brief(
                        "production",
                        "0x00401000",
                        root=root,
                        runner=HeadRunner(),
                        evidence_builder=lambda *_: evidence(),
                        doctor_receipt_path=receipt_path,
                        scout_report_paths=[first, second],
                    )
                second = write_scout_report(
                    second,
                    receipt_path,
                    "scout-b",
                )
                with self.assertRaisesRegex(brief.BriefError, "each required audit"):
                    brief.build_brief(
                        "production",
                        "0x00401000",
                        root=root,
                        runner=HeadRunner(),
                        evidence_builder=lambda *_: evidence(),
                        doctor_receipt_path=receipt_path,
                        scout_report_paths=[first, second],
                    )

    def test_bound_scout_reports_reject_mismatched_identity_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            receipt_path = root / "build/decomp-cache/doctor/latest.json"
            receipt_path.parent.mkdir(parents=True)
            receipt_path.write_text('{"receipt":"ready"}\n', encoding="utf-8")
            good = write_scout_report(
                root / "build/scouts/good.json", receipt_path, "scout-a"
            )
            validated = source_receipt(root)
            cases = {
                "lane": {"lane": "closure"},
                "target": {"target": "0x00402000"},
                "doctor receipt": {"doctor_hash": "e" * 64},
            }
            with patch.object(
                brief, "validate_doctor_receipt", return_value=validated
            ):
                for label, changes in cases.items():
                    with self.subTest(label=label):
                        bad = write_scout_report(
                            root / f"build/scouts/{label.replace(' ', '-')}.json",
                            receipt_path,
                            f"scout-{label.replace(' ', '-')}",
                            **changes,
                        )
                        with self.assertRaisesRegex(brief.BriefError, label):
                            brief.build_brief(
                                "production",
                                "0x00401000",
                                root=root,
                                runner=HeadRunner(),
                                evidence_builder=lambda *_: evidence(),
                                doctor_receipt_path=receipt_path,
                                scout_report_paths=[good, bad],
                            )

    def test_scout_report_mutation_invalidates_the_cached_brief(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            receipt_path = root / "build/decomp-cache/doctor/latest.json"
            receipt_path.parent.mkdir(parents=True)
            receipt_path.write_text('{"receipt":"ready"}\n', encoding="utf-8")
            report_paths = write_scout_reports(root, receipt_path)
            validated = source_receipt(root)
            with patch.object(
                brief, "validate_doctor_receipt", return_value=validated
            ):
                first = brief.build_brief(
                    "production",
                    "0x00401000",
                    root=root,
                    runner=HeadRunner(),
                    evidence_builder=lambda *_: evidence(),
                    doctor_receipt_path=receipt_path,
                    scout_report_paths=report_paths,
                )
                changed = json.loads(report_paths[0].read_text(encoding="utf-8"))
                changed["findings"].append(
                    {
                        "category": "abi",
                        "claim": "The caller preserves ESI.",
                        "evidence": [
                            {"source": "retail", "locator": "0x00401000:entry"}
                        ],
                    }
                )
                report_paths[0].write_text(json.dumps(changed), encoding="utf-8")
                with self.assertRaisesRegex(brief.BriefError, "input identity"):
                    brief.validate_brief(
                        first.path,
                        "production",
                        "0x00401000",
                        root=root,
                        runner=HeadRunner(),
                        doctor_receipt_path=receipt_path,
                    )
                rebuilt = brief.build_brief(
                    "production",
                    "0x00401000",
                    root=root,
                    runner=HeadRunner(),
                    evidence_builder=lambda *_: evidence(),
                    doctor_receipt_path=receipt_path,
                    scout_report_paths=report_paths,
                )

        self.assertFalse(rebuilt.cache_hit)
        self.assertNotEqual(first.path, rebuilt.path)
        self.assertEqual(
            rebuilt.brief["scout_findings"][0]["findings"][-1]["claim"],
            "The caller preserves ESI.",
        )

    def test_main_lane_names_match_the_candidate_queues(self):
        self.assertEqual(brief.SOURCE_LANES, {"closure", "production", "research"})

    def test_public_cli_requires_a_doctor_receipt(self):
        action = next(
            item
            for item in brief._parser()._actions
            if item.dest == "doctor_receipt"
        )
        self.assertTrue(action.required)

    def test_public_cli_requires_exactly_two_scout_report_options(self):
        action = next(
            item for item in brief._parser()._actions if item.dest == "scout_report"
        )
        self.assertTrue(action.required)
        for count in (1, 3):
            arguments = [
                "decomp_brief.py",
                "--lane",
                "production",
                "--target",
                "0x00401000",
                "--doctor-receipt",
                "doctor.json",
            ]
            for index in range(count):
                arguments.extend(["--scout-report", f"scout-{index}.json"])
            with (
                self.subTest(count=count),
                patch.object(sys, "argv", arguments),
                patch.object(brief, "build_brief") as build,
                patch("builtins.print"),
            ):
                self.assertEqual(brief.main(), 1)
                build.assert_not_called()

    def test_json_parse_errors_do_not_build_a_brief(self):
        base = [
            "--doctor-receipt",
            "doctor.json",
            "--scout-report",
            "retail.json",
            "--scout-report",
            "context.json",
            "--json",
        ]
        cases = {
            "lane": ["--lane", "invalid", "--target", "0x00401000", *base],
            "missing-scout": [
                "--lane",
                "production",
                "--target",
                "0x00401000",
                "--doctor-receipt",
                "doctor.json",
                "--json",
            ],
            "target": [
                "--lane",
                "production",
                "--target",
                "not-an-address",
                *base,
            ],
        }
        for name, options in cases.items():
            output = StringIO()
            error = StringIO()
            with (
                self.subTest(name=name),
                patch.object(sys, "argv", ["decomp_brief.py", *options]),
                patch.object(brief, "build_brief") as build,
                redirect_stdout(output),
                redirect_stderr(error),
            ):
                self.assertEqual(brief.main(), 2)
            self.assertFalse(json.loads(output.getvalue())["ok"])
            self.assertEqual(error.getvalue(), "")
            build.assert_not_called()

    def test_lane_readiness_matches_selection_policy(self):
        mismatch = {"available": True, "matching": 0.90, "class": "instruction"}
        ready, reasons = brief._source_readiness(
            "production",
            "FUNCTION",
            mismatch,
            {},
            None,
            None,
            False,
            True,
            retail_size=1200,
            unresolved_bytes=120,
            weak_dependency_count=2,
            actionable_mismatch=True,
        )
        self.assertTrue(ready)
        self.assertEqual(reasons, [])

        ready, reasons = brief._source_readiness(
            "production",
            "FUNCTION",
            mismatch,
            {},
            None,
            None,
            False,
            True,
            retail_size=1200,
            unresolved_bytes=120,
            weak_dependency_count=2,
            actionable_mismatch=False,
        )
        self.assertFalse(ready)
        self.assertIn(
            "production refinement needs a current actionable mismatch", reasons
        )

        ready, _ = brief._source_readiness(
            "closure",
            "FUNCTION",
            {"available": True, "matching": 1.0, "class": "exact"},
            {},
            None,
            None,
            True,
            True,
        )
        self.assertTrue(ready)

        ready, reasons = brief._source_readiness(
            "research",
            "STUB",
            {"available": True, "matching": 0.0, "class": "instruction"},
            {"cooldown": True},
            {"reason": "Needs a layout."},
            None,
            False,
            True,
            research_route={"eligible": True},
        )
        self.assertTrue(ready)
        self.assertEqual(reasons, [])

        ready, reasons = brief._source_readiness(
            "research",
            "STUB",
            {"available": False},
            {},
            None,
            None,
            False,
            True,
        )
        self.assertFalse(ready)
        self.assertIn("the target is absent from the function comparison report", reasons)

        ready, reasons = brief._source_readiness(
            "research",
            "FUNCTION",
            {"available": True, "matching": 0.6, "class": "instruction"},
            {},
            None,
            None,
            False,
            True,
            research_route={"eligible": False},
        )
        self.assertFalse(ready)
        self.assertIn(
            "research needs an active blocker, a target cooldown, or an open subsystem circuit",
            reasons,
        )

    def test_mismatch_summary_keeps_legacy_class_and_adds_taxonomy(self):
        diff = [
            [
                "@@ -0x401000,1 +0x501000,1 @@",
                [
                    {
                        "orig": [["0x401000", "call Original"]],
                        "recomp": [["0x501000", "call Recompiled"]],
                    }
                ],
            ]
        ]
        summary = brief._mismatch_summary(
            {"matching": 0.6, "effective": False, "diff": diff}
        )
        self.assertEqual(summary["class"], "call-shape")
        self.assertEqual(summary["taxonomy"]["schema_version"], 1)
        self.assertEqual(summary["taxonomy"]["primary_route"], "call")

    def test_research_route_uses_a_specific_hash_bound_blocker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            empty = brief.research_route_evidence(root, "0x00401000")
            self.assertFalse(empty["eligible"])

            blocker_path = root / brief.BLOCKERS_RELATIVE
            blocker_path.write_text(
                "0x00401000\t0x00402000\tNeeds the producer layout.\tlayout\n",
                encoding="utf-8",
            )
            routed = brief.research_route_evidence(
                root,
                "0x00401000",
                base_lane_resolver=lambda _root, _address: "production",
            )
            blocker_hash = brief._sha256(blocker_path)
            history_hash = brief._sha256(root / brief.LEDGER_RELATIVE)

        self.assertTrue(routed["eligible"])
        self.assertEqual(
            routed["routes"],
            [
                {
                    "kind": "blocker",
                    "blocker_kind": "layout",
                    "blocked_by": ["0x00402000"],
                    "reason": "Needs the producer layout.",
                }
            ],
        )
        self.assertEqual(routed["blocker_sha256"], blocker_hash)
        self.assertEqual(routed["history_sha256"], history_hash)

    def test_research_route_uses_a_qualifying_target_cooldown(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            source = root / "src/Probe.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// FUNCTION: TOY2 0x00401000\nvoid Probe() {}\n",
                encoding="utf-8",
            )
            failed = {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": "production-fail",
                "timestamp": "2026-08-03T12:10:00+00:00",
                "ended_at": "2026-08-03T12:10:00+00:00",
                "mode": "refinement",
                "lane": "production",
                "result": "no-source",
                "addresses": ["0x00401000"],
                "active_addresses": ["0x00401000"],
                "minutes": 10,
                "ruled_out_models": ["The first model failed."],
                "subsystem": "Probe",
            }
            (root / brief.LEDGER_RELATIVE).write_text(
                json.dumps(failed) + "\n",
                encoding="utf-8",
            )

            routed = brief.research_route_evidence(
                root,
                "0x00401000",
                base_lane_resolver=lambda _root, _address: "production",
            )

        self.assertTrue(routed["eligible"])
        self.assertEqual(
            routed["routes"],
            [
                {
                    "kind": "cooldown",
                    "campaign_id": "production-fail",
                    "lane": "production",
                    "mode": "refinement",
                    "result": "no-source",
                }
            ],
        )

    def test_subsystem_circuit_needs_a_current_refinement_base_lane(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            source = root / "src/Probe.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// STUB: TOY2 0x00401000\nvoid Probe() {}\n",
                encoding="utf-8",
            )
            failures = []
            for index, address in enumerate((0x00402000, 0x00403000, 0x00404000)):
                failures.append(
                    {
                        "schema_version": 3,
                        "record_type": "campaign",
                        "campaign_id": f"production-fail-{index}",
                        "timestamp": f"2026-08-03T12:0{index}:00+00:00",
                        "ended_at": f"2026-08-03T12:0{index}:00+00:00",
                        "mode": "refinement",
                        "lane": "production",
                        "result": "no-source",
                        "addresses": [f"0x{address:08X}"],
                        "active_addresses": [f"0x{address:08X}"],
                        "minutes": 10,
                        "subsystem": "Probe",
                    }
                )
            (root / brief.LEDGER_RELATIVE).write_text(
                "".join(json.dumps(item) + "\n" for item in failures),
                encoding="utf-8",
            )

            coverage_route = brief.research_route_evidence(root, "0x00401000")
            source.write_text(
                "// FUNCTION: TOY2 0x00401000\nvoid Probe() {}\n",
                encoding="utf-8",
            )
            inactive_route = brief.research_route_evidence(
                root,
                "0x00401000",
                base_lane_resolver=lambda _root, _address: "inactive",
            )
            refinement_route = brief.research_route_evidence(
                root,
                "0x00401000",
                base_lane_resolver=lambda _root, _address: "production",
            )

        self.assertFalse(coverage_route["eligible"])
        self.assertFalse(inactive_route["eligible"])
        self.assertEqual(inactive_route["selector_base_lane"], "inactive")
        self.assertTrue(refinement_route["eligible"])
        self.assertEqual(
            refinement_route["routes"],
            [{"kind": "circuit", "subsystem": "Probe"}],
        )

    def test_data_readiness_needs_typed_retail_bytes_and_supporting_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            data_report = root / brief.DATA_REPORT_RELATIVE
            data_report.write_text(
                json.dumps(
                    {
                        "variables": {
                            "variables": [
                                {
                                    "original_address": 0x401000,
                                    "name": "g_probe",
                                    "size": 4,
                                    "matched_bytes": 0,
                                    "score": 0.0,
                                    "raw_only": False,
                                    "error": None,
                                    "fields": [],
                                }
                            ]
                        }
                    }
                ),
                encoding="utf-8",
            )

            with (
                patch.object(brief, "_data_callers", return_value=[]),
                patch.object(
                    brief,
                    "_dwarf_evidence",
                    return_value={"available": True, "matches": []},
                ),
            ):
                unsupported = brief._data_evidence(
                    "0x00401000", root, HeadRunner()
                )
            self.assertFalse(unsupported["readiness"])
            self.assertIn(
                "data work needs caller or DWARF evidence",
                unsupported["rejection_reasons"],
            )

            with (
                patch.object(
                    brief,
                    "_data_callers",
                    return_value=[{"from": "0x00402000"}],
                ),
                patch.object(
                    brief,
                    "_dwarf_evidence",
                    return_value={"available": True, "matches": []},
                ),
            ):
                caller_backed = brief._data_evidence(
                    "0x00401000", root, HeadRunner()
                )
            self.assertTrue(caller_backed["readiness"])

            with (
                patch.object(brief, "_data_callers", return_value=[]),
                patch.object(
                    brief,
                    "_dwarf_evidence",
                    return_value={"available": True, "matches": ["g_probe"]},
                ),
            ):
                dwarf_backed = brief._data_evidence(
                    "0x00401000", root, HeadRunner()
                )
            self.assertTrue(dwarf_backed["readiness"])

            document = json.loads(data_report.read_text(encoding="utf-8"))
            document["variables"]["variables"][0]["raw_only"] = True
            data_report.write_text(json.dumps(document), encoding="utf-8")
            with (
                patch.object(
                    brief,
                    "_data_callers",
                    return_value=[{"from": "0x00402000"}],
                ),
                patch.object(
                    brief,
                    "_dwarf_evidence",
                    return_value={"available": True, "matches": ["g_probe"]},
                ),
            ):
                raw_only = brief._data_evidence("0x00401000", root, HeadRunner())
            self.assertFalse(raw_only["readiness"])
            self.assertIn(
                "the typed data report does not score retail bytes at this address",
                raw_only["rejection_reasons"],
            )

    def test_resource_readiness_needs_a_selected_nonexact_retail_leaf(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            exact = [
                {
                    "path": ["2", "127", "2057"],
                    "size": 8,
                    "identity_match": True,
                }
            ]
            with patch("tools.decomp_resources.resource_rows", return_value=exact):
                result = brief._resource_evidence("2,127,2057", root)
            self.assertFalse(result["readiness"])
            self.assertIn(
                "the selected resource leaf is already exact",
                result["rejection_reasons"],
            )

            nonexact = [
                {
                    "path": ["2", "127", "2057"],
                    "size": 8,
                    "identity_match": False,
                },
                {
                    "path": ["2", "128", "2057"],
                    "size": 4,
                    "identity_match": True,
                },
            ]
            with patch("tools.decomp_resources.resource_rows", return_value=nonexact):
                result = brief._resource_evidence("2,127,2057", root)
            self.assertTrue(result["readiness"])
            self.assertEqual(result["mismatch"]["matching"], 0.0)
            self.assertEqual(
                result["layout"]["selected_evidence"]["scored_bytes"], 8
            )

            with patch("tools.decomp_resources.resource_rows", return_value=nonexact):
                missing = brief._resource_evidence("2,129,2057", root)
            self.assertFalse(missing["readiness"])
            self.assertIn(
                "the selected resource leaf is not present in the retail executable",
                missing["rejection_reasons"],
            )

if __name__ == "__main__":
    unittest.main()
