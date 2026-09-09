from __future__ import annotations

import argparse
import importlib.util
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from datetime import datetime, timedelta, timezone
from io import StringIO
from pathlib import Path
from unittest.mock import patch

from tools import decomp_doctor


TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "toy2_decomp_campaigns", TOOLS / "decomp_campaigns.py"
)
assert spec is not None and spec.loader is not None
campaigns = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = campaigns
spec.loader.exec_module(campaigns)


class CampaignTests(unittest.TestCase):
    @staticmethod
    def bind_campaign_record_receipt(ledger: Path) -> dict[str, object]:
        records = campaigns.read_records(ledger)
        campaign = records[0]
        finalize = campaign.setdefault("finalize_receipt", {})
        if isinstance(finalize, dict):
            finalize.setdefault(
                "path", str((ledger.parent / "finalize.json").resolve())
            )
            finalize.setdefault("content_sha256", "f" * 64)
        campaigns._bind_campaign_record_receipt(campaign)
        campaigns.write_records(ledger, [campaign, *records[1:]])
        return campaign

    @staticmethod
    def seal_doctor_receipt(receipt: dict[str, object]) -> dict[str, object]:
        payload = dict(receipt)
        payload.pop("receipt_id", None)
        payload.pop("receipt_path", None)
        identity = json.dumps(
            payload, sort_keys=True, separators=(",", ":")
        ).encode()
        receipt["receipt_id"] = hashlib.sha256(identity).hexdigest()
        return receipt

    @staticmethod
    def write_sealed_doctor_receipt(
        root: Path, receipt: dict[str, object]
    ) -> Path:
        CampaignTests.seal_doctor_receipt(receipt)
        receipt["receipt_path"] = (
            decomp_doctor.RECEIPT_DIRECTORY_RELATIVE
            / f"{receipt['receipt_id']}.json"
        ).as_posix()
        path = root / str(receipt["receipt_path"])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(receipt), encoding="utf-8")
        return path

    @staticmethod
    def write_doctor_receipt(
        root: Path,
        lane: str,
        started: datetime | None = None,
        *,
        mode: str | None = None,
        addresses: list[str] | None = None,
        resource: str | None = None,
        map_path: Path | None = None,
        sizes_path: Path | None = None,
    ) -> Path:
        if not (root / ".git").exists():
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        original = root / "original/toy2.exe"
        original.parent.mkdir(parents=True, exist_ok=True)
        if not original.is_file():
            original.write_bytes(b"retail fixture\n")
        project_config = root / "reccmp-project.yml"
        if not project_config.is_file():
            retail_sha256 = hashlib.sha256(original.read_bytes()).hexdigest()
            project_config.write_text(
                "targets:\n"
                "  TOY2:\n"
                "    filename: toy2.exe\n"
                "    hash:\n"
                f"      sha256: {retail_sha256}\n",
                encoding="utf-8",
            )
        user_config = root / "reccmp-user.yml"
        if not user_config.is_file():
            user_config.write_text(
                "targets:\n  TOY2:\n    path: original/toy2.exe\n",
                encoding="utf-8",
            )
        ignore = root / ".gitignore"
        fixture_ignores = {
            "*.json",
            "build/",
            "state.json",
            "first.json",
            "second.json",
            "ledger.jsonl",
            "models.md",
            "woc-dwarf.txt",
            "reccmp-user.yml",
        }
        existing_ignores = (
            set(ignore.read_text(encoding="utf-8").splitlines())
            if ignore.is_file()
            else set()
        )
        if not fixture_ignores.issubset(existing_ignores):
            ignore.write_text(
                "\n".join(sorted(existing_ignores | fixture_ignores)) + "\n",
                encoding="utf-8",
            )
        fixture_marker = root / "build" / ".doctor-fixture-initialized"
        if not fixture_marker.is_file():
            subprocess.run(["git", "add", "-A"], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Campaign Tests",
                    "-c",
                    "user.email=campaign-tests@example.invalid",
                    "commit",
                    "-q",
                    "--allow-empty",
                    "-m",
                    "Prepare doctor fixture",
                ],
                cwd=root,
                check=True,
            )
            fixture_marker.parent.mkdir(parents=True, exist_ok=True)
            fixture_marker.touch()
        head_result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
        )
        if head_result.returncode == 0:
            head = head_result.stdout.strip().lower()
            subprocess.run(
                ["git", "update-ref", "refs/heads/agent/continuous", head],
                cwd=root,
                check=True,
            )
            subprocess.run(
                ["git", "symbolic-ref", "HEAD", "refs/heads/agent/continuous"],
                cwd=root,
                check=True,
            )
        else:
            subprocess.run(
                ["git", "symbolic-ref", "HEAD", "refs/heads/agent/continuous"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Campaign Tests",
                    "-c",
                    "user.email=campaign-tests@example.invalid",
                    "commit",
                    "-q",
                    "--allow-empty",
                    "-m",
                    "Create test HEAD",
                ],
                cwd=root,
                check=True,
            )
            head = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip().lower()
        remote_result = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            cwd=root,
            check=False,
            capture_output=True,
        )
        if remote_result.returncode != 0:
            subprocess.run(
                ["git", "remote", "add", "origin", str(root.resolve())],
                cwd=root,
                check=True,
            )
        subprocess.run(
            [
                "git",
                "update-ref",
                "refs/remotes/origin/agent/continuous",
                head,
            ],
            cwd=root,
            check=True,
        )
        campaign_start = started or datetime.now(timezone.utc)
        selected_mode = mode or lane
        selected_lane = campaigns.DEFAULT_LANES.get(lane, lane)
        selected_addresses = (
            addresses
            if addresses is not None
            else [] if selected_mode == "resource" else ["0x00401000"]
        )
        selected_resource = (
            resource
            if resource is not None
            else "2,127,2057" if selected_mode == "resource" else None
        )
        worktree = campaigns.source_worktree_snapshot(root)
        index = campaigns.source_index_snapshot(root)
        repository = campaigns.repository_worktree_snapshot(root)
        repository_index = campaigns.repository_index_snapshot(root)
        resource_sources = campaigns.resource_source_snapshot(root)
        input_hashes = {
            "source_worktree_sha256": campaigns._snapshot_hash(worktree),
            "source_index_sha256": campaigns._snapshot_hash(index),
            "repository_worktree_sha256": campaigns._snapshot_hash(repository),
            "repository_index_sha256": campaigns._snapshot_hash(repository_index),
            "resource_sources_sha256": campaigns._snapshot_hash(resource_sources),
            "functions_map_sha256": (
                campaigns.file_hash(map_path)
                if map_path is not None and map_path.is_file()
                else None
            ),
            "function_sizes_file_sha256": (
                campaigns.file_hash(sizes_path)
                if sizes_path is not None and sizes_path.is_file()
                else None
            ),
            **campaigns.comparison_artifact_hashes(root),
        }
        checks = [
            {
                "name": "selection",
                "ok": True,
                "detail": "ready",
                "data": {
                    "addresses": selected_addresses,
                    "resource": selected_resource,
                },
            },
            {
                "name": "branch",
                "ok": True,
                "detail": "ready",
                "data": {
                    "actual": "agent/continuous",
                    "expected": "agent/continuous",
                },
            },
            {
                "name": "head",
                "ok": True,
                "detail": "ready",
                "data": {"actual": head},
            },
            {
                "name": "origin-integration",
                "ok": True,
                "detail": "ready",
                "data": {
                    "remote_ref": "refs/remotes/origin/agent/continuous",
                    "remote": head,
                    "head": head,
                    "fetched": True,
                },
            },
        ]
        common_checks = [
            "allowed-dirt",
            "writable-state",
            "wine-runtime",
            "native-build-runtime" if sys.platform.startswith("win") else "wine-prefix",
            "reccmp-inputs",
            "reccmp-runtime",
            "ghidra-runtime",
            "ghidra-config",
            "ghidra-bridge-markers",
            "ghidra-state",
            "ghidra-log-cap",
            "function-map",
            "reports",
            "campaign-input-hashes",
        ]
        checks.extend(
            {"name": name, "ok": True, "detail": "ready"}
            for name in common_checks
        )
        if selected_mode in {"coverage", "refinement"}:
            for artifact_address in selected_addresses:
                disassembly = decomp_doctor._store_ghidra_artifact(
                    root,
                    artifact_address,
                    "disassembly",
                    {
                        "instructions": [
                            {"address": artifact_address, "text": "ret"}
                        ]
                    },
                )
                decompilation = decomp_doctor._store_ghidra_artifact(
                    root,
                    artifact_address,
                    "decompilation",
                    {"code": "void reconstructed(void) { return; }"},
                )
                function = decomp_doctor._store_ghidra_artifact(
                    root,
                    artifact_address,
                    "function",
                    {"address": artifact_address, "name": "reconstructed"},
                )
                xrefs = decomp_doctor._store_ghidra_artifact(
                    root,
                    artifact_address,
                    "xrefs",
                    [],
                )
                checks.append(
                    {
                        "name": "disassembly",
                        "ok": True,
                        "detail": "ready",
                        "data": {
                            "target": artifact_address,
                            "artifact": disassembly,
                        },
                    }
                )
                checks.append(
                    {
                        "name": "function",
                        "ok": True,
                        "detail": "ready",
                        "data": {
                            "target": artifact_address,
                            "artifact": function,
                        },
                    }
                )
                checks.append(
                    {
                        "name": "xrefs",
                        "ok": True,
                        "detail": "ready",
                        "data": {
                            "target": artifact_address,
                            "artifact": xrefs,
                        },
                    }
                )
                checks.append(
                    {
                        "name": "decompilation",
                        "ok": True,
                        "detail": "ready",
                        "data": {
                            "target": artifact_address,
                            "artifact": decompilation,
                        },
                    }
                )
        elif selected_mode == "data":
            for artifact_address in selected_addresses:
                xrefs = decomp_doctor._store_ghidra_artifact(
                    root,
                    artifact_address,
                    "xrefs",
                    [],
                )
                checks.append(
                    {
                        "name": "xrefs",
                        "ok": True,
                        "detail": "ready",
                        "data": {
                            "target": artifact_address,
                            "artifact": xrefs,
                        },
                    }
                )
        dirt = decomp_doctor._dirt_check(
            root,
            decomp_doctor._default_runner,
            mode=selected_mode,
            head=head,
        )
        allowed_dirt = next(
            check for check in checks if check["name"] == "allowed-dirt"
        )
        allowed_dirt.update(
            {"ok": dirt.ok, "detail": dirt.detail, "data": dirt.data}
        )
        receipt = {
            "schema": 1,
            "root": str(root.resolve()),
            "head": head,
            "branch": {
                "actual": "agent/continuous",
                "expected": "agent/continuous",
            },
            "origin_integration": {
                "remote_ref": "refs/remotes/origin/agent/continuous",
                "remote": head,
                "head": head,
                "fetched": True,
            },
            "lane": selected_lane,
            "mode": selected_mode,
            "addresses": selected_addresses,
            "target": (
                selected_addresses[0] if len(selected_addresses) == 1 else None
            ),
            "resource": selected_resource,
            "input_hashes": input_hashes,
            "status": "ready",
            "ok": True,
            "campaign_timing_started": False,
            "checks": checks,
            "selection_started_at": (
                campaign_start - timedelta(minutes=3)
            ).isoformat(),
            "doctor_started_at": (
                campaign_start - timedelta(minutes=2)
            ).isoformat(),
            "doctor_ended_at": (
                campaign_start - timedelta(minutes=1)
            ).isoformat(),
        }
        return CampaignTests.write_sealed_doctor_receipt(root, receipt)

    def test_resource_mode_is_the_only_source_mode_without_addresses(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            for mode in ("coverage", "refinement", "data"):
                with self.assertRaisesRegex(ValueError, "at least one --address"):
                    campaigns.start_campaign(
                        root / f"{mode}.json", mode, [], "Test", worktree_root=root
                    )
            with self.assertRaisesRegex(ValueError, "cannot have source targets"):
                campaigns.start_campaign(
                    root / "bad.json", "resource", ["0x00401000"], "Test",
                    worktree_root=root, resources=[(2, 127, 2057)],
                )
            state_path = root / "resource.json"
            state = campaigns.start_campaign(
                state_path, "resource", [], "Test", worktree_root=root,
                resources=[(2, 127, 2057)],
                doctor_receipt_path=self.write_doctor_receipt(root, "resource"),
            )
            self.assertEqual(state["resource"], "2,127,2057")
            state["baseline_at"] = state["started_at"]
            campaigns.write_state(state_path, state)
            with self.assertRaisesRegex(ValueError, "cannot add a source target"):
                campaigns.add_target(state_path, "0x00401000")

    def test_finalization_rechecks_the_resource_target_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / "state.json"
            campaigns.write_state(
                state_path,
                {
                    "phase": "finalizing",
                    "mode": "resource",
                    "addresses": ["0x00401000"],
                    "resource": "2,127,2057",
                    "finalization": {
                        "item": {
                            "mode": "resource",
                            "addresses": ["0x00401000"],
                            "resource": "2,127,2057",
                        },
                        "ledger_path": str(root / "ledger.jsonl"),
                        "source_models_path": str(root / "models.md"),
                    },
                },
            )
            with self.assertRaisesRegex(ValueError, "cannot have source targets"):
                campaigns._finish_campaign_finalization(
                    state_path, campaigns.read_state(state_path)
                )

    def test_optional_dwarf_identity_accepts_only_the_same_absence_state(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "woc-dwarf.txt"
            campaigns._validate_optional_file_hash(path, None, "DWARF input")
            path.write_text("evidence\n", encoding="utf-8")
            digest = campaigns.file_hash(path)
            campaigns._validate_optional_file_hash(path, digest, "DWARF input")
            with self.assertRaisesRegex(ValueError, "changed after campaign start"):
                campaigns._validate_optional_file_hash(path, None, "DWARF input")
            path.unlink()
            with self.assertRaisesRegex(ValueError, "changed after campaign start"):
                campaigns._validate_optional_file_hash(path, digest, "DWARF input")

    def test_resource_source_result_records_leaf_bytes_separately(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            (root / "resources").mkdir()
            source = root / "resources" / "leaf.bmp"
            source.write_bytes(b"old")
            subprocess.run(["git", "add", "resources/leaf.bmp"], cwd=root, check=True)
            (root / "original").mkdir()
            (root / "build").mkdir()
            (root / "original" / "toy2.exe").write_bytes(b"old")
            (root / "build" / "toy2.exe").write_bytes(b"old")
            function_map = root / "functions.txt"
            function_map.write_text(
                "0x00401000 One\n0x00401064 Two\n", encoding="utf-8"
            )
            sizes = root / "sizes.json"
            sizes.write_text(
                '[{"address":"00401000","size":100}]', encoding="utf-8"
            )
            report = root / "report.json"
            data_report = root / "data.json"
            self.write_code_report(report, 0.75)
            self.write_data_report(data_report, 40)
            state_path = root / "state.json"
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state_path,
                "resource",
                [],
                "Resources",
                started,
                worktree_root=root,
                resources=[(2, 127, 2057)],
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "resource", started
                ),
            )
            before = [{
                "path": ["2", "127", "2057"],
                "size": 8,
                "identity_match": False,
            }]
            after = [{
                "path": ["2", "127", "2057"],
                "size": 8,
                "identity_match": True,
            }]
            baseline_identity = self.seal_report_pair(root, report, data_report)
            with (
                patch.object(campaigns, "resource_rows", return_value=before),
                patch(
                    "tools.decomp_provenance.current_identity",
                    return_value=baseline_identity,
                ),
            ):
                campaigns.attach_baseline(
                    state_path,
                    report,
                    data_report,
                    function_map,
                    sizes,
                    started + timedelta(minutes=1),
                )
            legacy_state = campaigns.read_state(state_path)
            legacy_state["schema_version"] = 2
            legacy_state.pop("finalize_required", None)
            campaigns.write_state(state_path, legacy_state)
            with patch.object(campaigns, "resource_rows", return_value=after):
                with self.assertRaisesRegex(ValueError, "report deltas"):
                    campaigns.record_campaign(
                        root / "ledger.jsonl",
                        state_path,
                        root / "models.md",
                        report,
                        data_report,
                        "no-source",
                        models=["The resource model did not match."],
                        now=started + timedelta(minutes=2),
                    )
            campaigns.mark_resource_score(
                state_path, started + timedelta(minutes=2)
            )
            source.write_bytes(b"new")
            subprocess.run(["git", "add", "resources/leaf.bmp"], cwd=root, check=True)
            with patch.object(campaigns, "resource_rows", return_value=after):
                item = campaigns.record_campaign(
                    root / "ledger.jsonl",
                    state_path,
                    root / "models.md",
                    report,
                    data_report,
                    "source",
                    now=started + timedelta(minutes=3),
                )
            self.assertEqual(item["addresses"], [])
            self.assertEqual(item["resource"], "2,127,2057")
            self.assertEqual(item["resource_explained_bytes"], 8)
            self.assertEqual(item["effective_bytes"], 0)
            self.assertEqual(item["initialized_bytes"], 0)

    def test_resource_no_source_note_uses_the_resource_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / "state.json"
            models_path = root / "models.md"
            item = {
                "campaign_id": "resource-note",
                "ended_at": "2026-08-03T12:03:00+00:00",
                "subsystem": "Resources",
                "mode": "resource",
                "result": "no-source",
                "addresses": [],
                "resource": "2,127,2057",
                "ruled_out_models": ["The payload model did not match."],
                "note": "The source was restored.",
            }
            state = {
                "phase": "finalizing",
                "finalization": {
                    "item": item,
                    "ledger_path": str(root / "ledger.jsonl"),
                    "source_models_path": str(models_path),
                },
            }
            campaigns.write_state(state_path, state)
            campaigns._finish_campaign_finalization(state_path, state)
            self.assertIn(
                "## 2026-08-03 | Resources | 2,127,2057",
                models_path.read_text(encoding="utf-8"),
            )

    @staticmethod
    def write_code_report(path: Path, score: float, *, effective: bool = False):
        row = {"address": "0x401000", "matching": score, "type": 1}
        if effective:
            row["effective"] = True
        path.write_text(json.dumps({"data": [row]}), encoding="utf-8")

    @staticmethod
    def write_data_report(
        path: Path,
        explained: float,
        variables: list[dict[str, object]] | None = None,
    ):
        path.write_text(
            json.dumps(
                {
                    "variables": {
                        "explained_bytes": explained,
                        "variables": variables or [],
                    },
                    "sections": {"sections": []},
                    "vtables": {"explained_bytes": 0},
                    "imports": {"matched_entries": 0},
                    "relocations": {"matched_entries": 0},
                    "debug": {
                        "original_pdb": "C:\\retail\\toy2.pdb",
                        "recompiled_pdb": "Z:\\build\\toy2.pdb",
                    },
                }
            ),
            encoding="utf-8",
        )

    @staticmethod
    def seal_report_pair(root: Path, *reports: Path) -> dict[str, object]:
        from tools import decomp_provenance as provenance

        identity: dict[str, object] = {"fixture": str(root.resolve())}
        for report in reports:
            provenance._write_sidecar(
                provenance.provenance_path(report),
                {
                    "schema_version": provenance.SCHEMA_VERSION,
                    "kind": "comparison-report",
                    "created_at": "2026-08-03T12:00:00+00:00",
                    "artifact": {
                        "path": str(report.resolve()),
                        "sha256": campaigns.file_hash(report),
                    },
                    "input_identity": identity,
                },
            )
        return identity

    def make_measured_campaign(
        self,
        root: Path,
        *,
        mode: str = "refinement",
        addresses: list[str] | None = None,
        family: bool = False,
        family_sizes: dict[int, int] | None = None,
        legacy: bool = True,
        impact_review: bool = False,
    ):
        if not (root / ".git").exists():
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        ledger = root / "ledger.jsonl"
        state = root / "state.json"
        models = root / "models.md"
        function_map = root / "functions_map.txt"
        function_sizes = root / "function_sizes.json"
        before = root / "before.json"
        after = root / "after.json"
        before_data = root / "before-data.json"
        after_data = root / "after-data.json"
        source_root = root / "src"
        source_root.mkdir(exist_ok=True)
        annotation = "FUNCTION" if mode == "refinement" else "STUB"
        (source_root / "Targets.cpp").write_text(
            "".join(
                f"// {annotation}: TOY2 0x0040{index}000 [PROVISIONAL]\n"
                for index in range(1, 6)
            ),
            encoding="utf-8",
        )
        function_map.write_text(
            "".join(
                f"0x0040{index}000 Target{index}\n"
                f"0x0040{index}064 Next{index}\n"
                for index in range(1, 6)
            ),
            encoding="utf-8",
        )
        function_sizes.write_text(
            json.dumps(
                [
                    {"address": f"0040{index}000", "size": 100}
                    for index in range(1, 6)
                ]
            ),
            encoding="utf-8",
        )
        if family_sizes:
            function_sizes.write_text(
                json.dumps(
                    [
                        {
                            "address": f"0040{index}000",
                            "size": family_sizes.get(index, 100),
                        }
                        for index in range(1, 6)
                    ]
                ),
                encoding="utf-8",
            )
        self.write_code_report(before, 0.25)
        self.write_code_report(after, 0.75)
        self.write_data_report(before_data, 40)
        self.write_data_report(after_data, 47)
        baseline_identity = self.seal_report_pair(root, before, before_data)
        self.seal_report_pair(root, after, after_data)
        started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
        start_options: dict[str, object] = {}
        if not impact_review:
            start_options["_legacy_skip_impact_review"] = True
        campaigns.start_campaign(
            state,
            mode,
            addresses or ["0x00401000"],
            "Test",
            started,
            worktree_root=root,
            sizes_path=function_sizes if family else None,
            family=family,
            doctor_receipt_path=self.write_doctor_receipt(
                root,
                mode,
                started,
                addresses=addresses or ["0x00401000"],
                sizes_path=function_sizes if family else None,
            ),
            **start_options,
        )
        with patch(
            "tools.decomp_provenance.current_identity",
            return_value=baseline_identity,
        ):
            campaigns.attach_baseline(
                state,
                before,
                before_data,
                function_map,
                function_sizes,
                started + timedelta(minutes=1),
                source_root,
            )
        if legacy:
            legacy_state = campaigns.read_state(state)
            legacy_state["schema_version"] = 2
            legacy_state.pop("finalize_required", None)
            campaigns.write_state(state, legacy_state)
        return {
            "ledger": ledger,
            "state": state,
            "models": models,
            "map": function_map,
            "sizes": function_sizes,
            "before": before,
            "after": after,
            "before_data": before_data,
            "after_data": after_data,
            "started": started,
        }

    def test_new_function_source_campaign_enables_impact_review_by_default(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch(
                "tools.decomp_provenance.reccmp_user_identity",
                return_value={"retail_sha256": "0" * 64},
            ):
                paths = self.make_measured_campaign(
                    Path(directory), legacy=False, impact_review=True
                )

            self.assertTrue(
                campaigns.read_state(paths["state"])["impact_review_required"]
            )

    @staticmethod
    def set_source_deadlines(
        paths: dict[str, object],
        *,
        expected_bytes: float,
        preflight_minutes: float = 4,
        first_score_minutes: float = 7,
    ) -> None:
        state_path = Path(paths["state"])
        started = paths["started"]
        assert isinstance(started, datetime)
        state = campaigns.read_state(state_path)
        state["expected_minutes"] = 12
        state["expected_retained_bytes"] = expected_bytes
        state["deadlines"] = campaigns._campaign_deadlines(
            started, 12, expected_bytes
        )
        campaigns.write_state(state_path, state)
        campaigns.mark_phase(
            state_path,
            "preflight",
            started + timedelta(minutes=preflight_minutes),
        )
        campaigns.mark_first_score(
            state_path,
            "0x00401000",
            started + timedelta(minutes=first_score_minutes),
            raw_score=75,
            artifact_sha256="a" * 64,
        )

    @staticmethod
    def source_finalize_actions(
        root: Path, paths: dict[str, object]
    ) -> dict[str, object]:
        build_artifact = root / "built.bin"
        build_artifact.write_bytes(b"built")
        return {
            "build": lambda: build_artifact,
            "code_report": lambda: paths["after"],
            "data_report": lambda: paths["after_data"],
            "source_scan": lambda: {"metrics": {}},
            "validation": lambda: {"ok": True},
        }

    def test_read_records_and_address_stats_split_bundle_yield(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaigns.jsonl"
            records = [
                {
                    "mode": "refinement",
                    "result": "source",
                    "addresses": ["0x00401000", "0x00402000"],
                    "minutes": 10,
                    "effective_bytes": 120,
                    "initialized_bytes": 0,
                },
                {
                    "mode": "coverage",
                    "result": "no-source",
                    "addresses": ["0x00401000"],
                    "minutes": 8,
                    "effective_bytes": 0,
                    "initialized_bytes": 0,
                },
            ]
            path.write_text(
                "".join(json.dumps(item) + "\n" for item in records),
                encoding="utf-8",
            )
            stats = campaigns.address_stats(campaigns.read_records(path))
            self.assertEqual(stats[0x00401000].attempts, 2)
            self.assertEqual(stats[0x00401000].zero_yield_attempts, 1)
            self.assertEqual(stats[0x00401000].penalty_attempts, 1)
            self.assertEqual(stats[0x00401000].effective_bytes, 60)
            self.assertEqual(stats[0x00401000].minutes, 13)
            self.assertEqual(stats[0x00402000].attempts, 1)
            self.assertEqual(stats[0x00402000].effective_bytes, 60)

    def test_new_evidence_clears_only_the_active_zero_yield_penalty(self):
        records = [
            {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": "failed-1",
                "timestamp": "2026-08-03T12:00:00+00:00",
                "ended_at": "2026-08-03T12:00:00+00:00",
                "mode": "coverage",
                "lane": "research",
                "result": "no-source",
                "addresses": ["0x00401000"],
                "ruled_out_models": ["The pointer model failed."],
            },
            {
                "schema_version": 3,
                "record_type": "evidence",
                "timestamp": "2026-08-03T12:01:00+00:00",
                "addresses": ["0x00401000"],
                "evidence_kind": "analogue",
                "failed_campaign_id": "failed-1",
                "failed_model": "The pointer model failed.",
                "changed_assumption": "The retail table proves pointer ownership.",
                "changed_source": None,
                "note": "A new analogue is available.",
            },
            {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": "failed-2",
                "timestamp": "2026-08-03T12:02:00+00:00",
                "ended_at": "2026-08-03T12:02:00+00:00",
                "mode": "coverage",
                "lane": "research",
                "result": "no-source",
                "addresses": ["0x00401000"],
                "ruled_out_models": ["The owned table model failed."],
            },
        ]
        stats = campaigns.address_stats(records)[0x00401000]
        self.assertEqual(stats.attempts, 2)
        self.assertEqual(stats.zero_yield_attempts, 2)
        self.assertEqual(stats.penalty_attempts, 1)
        self.assertEqual(stats.evidence_events, 1)
        self.assertEqual(stats.retry_credits, 1)

    def test_retry_evidence_uses_per_target_under_yield(self):
        failed = {
            "schema_version": 3,
            "record_type": "campaign",
            "campaign_id": "bundle-1",
            "timestamp": "2026-08-03T12:00:00+00:00",
            "ended_at": "2026-08-03T12:00:00+00:00",
            "mode": "refinement",
            "lane": "production",
            "result": "source",
            "addresses": ["0x00401000", "0x00402000"],
            "expected_retained_bytes": 200,
            "effective_bytes": 105,
            "target_deltas": {
                "0x00401000": {
                    "effective_bytes": 5,
                    "initialized_bytes": 0,
                },
                "0x00402000": {
                    "effective_bytes": 100,
                    "initialized_bytes": 0,
                },
            },
            "ruled_out_models": ["The narrow-index model failed."],
        }
        evidence = {
            "schema_version": 3,
            "record_type": "evidence",
            "timestamp": "2026-08-03T12:01:00+00:00",
            "addresses": ["0x00401000"],
            "failed_campaign_id": "bundle-1",
            "failed_model": "The narrow-index model failed.",
            "changed_assumption": "The retail caller proves a wider index.",
        }
        self.assertTrue(campaigns.valid_retry_evidence(evidence, failed))
        evidence["addresses"] = ["0x00402000"]
        self.assertFalse(campaigns.valid_retry_evidence(evidence, failed))

    def test_retry_evidence_uses_each_pivot_prediction(self):
        failed = {
            "schema_version": 3,
            "record_type": "campaign",
            "campaign_id": "pivot-1",
            "timestamp": "2026-08-03T12:00:00+00:00",
            "ended_at": "2026-08-03T12:00:00+00:00",
            "mode": "refinement",
            "lane": "production",
            "result": "source",
            "addresses": ["0x00401000", "0x00402000"],
            "prediction": {"expected_retained_bytes": 100},
            "prediction_events": [
                {
                    "addresses": ["0x00401000"],
                    "prediction": {"expected_retained_bytes": 100},
                },
                {
                    "addresses": ["0x00402000"],
                    "prediction": {"expected_retained_bytes": 1000},
                    "replaces": "0x00401000",
                },
            ],
            "target_deltas": {
                "0x00401000": {
                    "effective_bytes": 50,
                    "initialized_bytes": 0,
                },
                "0x00402000": {
                    "effective_bytes": 50,
                    "initialized_bytes": 0,
                },
            },
            "ruled_out_models": ["The pivot ownership model failed."],
        }
        evidence = {
            "schema_version": 3,
            "record_type": "evidence",
            "timestamp": "2026-08-03T12:01:00+00:00",
            "addresses": ["0x00402000"],
            "failed_campaign_id": "pivot-1",
            "failed_model": "The pivot ownership model failed.",
            "changed_source": "A new retail caller shows shared ownership.",
        }
        self.assertTrue(campaigns.valid_retry_evidence(evidence, failed))
        evidence["addresses"] = ["0x00401000"]
        self.assertFalse(campaigns.valid_retry_evidence(evidence, failed))

    def test_add_target_records_a_pivot_and_rejects_a_duplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            state = paths["state"]
            started = paths["started"]
            original_deadlines = campaigns.read_state(state)["deadlines"]
            updated = campaigns.add_target(
                state, "0x00402000", started + timedelta(minutes=2)
            )
            self.assertEqual(
                updated["addresses"], ["0x00401000", "0x00402000"]
            )
            self.assertEqual(
                updated["target_events"],
                [
                    {
                        "address": "0x00402000",
                        "added_at": "2026-08-03T12:02:00+00:00",
                    }
                ],
            )
            self.assertEqual(updated["deadlines"], original_deadlines)
            with self.assertRaisesRegex(ValueError, "already contains"):
                campaigns.add_target(state, "0x00402000")

    def test_strict_pivot_replaces_the_active_target_and_binds_new_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            state = campaigns.read_state(paths["state"])
            state["briefs_required"] = True
            state["prediction_required"] = True
            campaigns.write_state(paths["state"], state)
            pivot_time = paths["started"] + timedelta(minutes=5)
            forecast = {
                "success_probability": 0.5,
                "cohort_sample_size": 8,
                "median_retained_bytes": 80,
                "median_minutes": 12,
            }
            with self.assertRaisesRegex(ValueError, "complete forecast"):
                campaigns.add_target(
                    paths["state"],
                    "0x00402000",
                    pivot_time,
                    replaces="0x00401000",
                )
            pivot_doctor = self.write_doctor_receipt(
                root,
                "production",
                pivot_time,
                mode="refinement",
                addresses=["0x00402000"],
                map_path=paths["map"],
                sizes_path=paths["sizes"],
            )
            receipt = json.loads(pivot_doctor.read_text(encoding="utf-8"))
            brief_path = root / "build" / "pivot-brief.json"
            brief_identity = {
                "path": str(brief_path.resolve()),
                "sha256": "1" * 64,
                "content_sha256": "2" * 64,
                "cache_key": "3" * 64,
                "lane": "production",
                "target": "0x00402000",
                "subsystem": "Test",
                "doctor_receipt": {
                    "path": str(pivot_doctor.resolve()),
                    "sha256": campaigns.file_hash(pivot_doctor),
                    "receipt_id": receipt["receipt_id"],
                    "mode": "refinement",
                    "lane": "production",
                },
            }
            prediction_args = {
                "expected_minutes": 12,
                "expected_retained_bytes": 80,
                "prediction_version": "cohort-v1",
                "prediction_lower_bound_bytes": 20,
                "prediction_features": forecast,
            }
            with self.assertRaisesRegex(ValueError, "needs --replace"):
                campaigns.add_target(
                    paths["state"],
                    "0x00402000",
                    pivot_time,
                    map_path=paths["map"],
                    sizes_path=paths["sizes"],
                    doctor_receipt_path=pivot_doctor,
                    brief_path=brief_path,
                    **prediction_args,
                )
            with patch(
                "tools.decomp_brief.validate_brief",
                return_value=brief_identity,
            ):
                updated = campaigns.add_target(
                    paths["state"],
                    "0x00402000",
                    pivot_time,
                    map_path=paths["map"],
                    sizes_path=paths["sizes"],
                    source_root=root / "src",
                    doctor_receipt_path=pivot_doctor,
                    brief_path=brief_path,
                    replaces="0x00401000",
                    **prediction_args,
                )
            self.assertEqual(
                updated["addresses"], ["0x00401000", "0x00402000"]
            )
            self.assertEqual(updated["active_addresses"], ["0x00402000"])
            self.assertEqual(updated["retired_addresses"], ["0x00401000"])
            self.assertEqual(updated["target_events"][0]["replaces"], "0x00401000")
            self.assertEqual(
                updated["prediction_events"][-1]["prediction"][
                    "expected_retained_bytes"
                ],
                80,
            )
            with self.assertRaisesRegex(ValueError, "--target disagrees"):
                campaigns.finalize_standard(
                    paths["state"],
                    "source",
                    mode="refinement",
                    targets=["0x00401000"],
                )
            with self.assertRaisesRegex(ValueError, "requires --staged"):
                campaigns.finalize_standard(
                    paths["state"],
                    "source",
                    mode="refinement",
                    targets=["0x00402000"],
                )

    def test_finalization_rejects_a_head_change_after_preflight(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Campaign Tests",
                    "-c",
                    "user.email=campaign-tests@example.invalid",
                    "commit",
                    "-q",
                    "--allow-empty",
                    "-m",
                    "Advance HEAD",
                ],
                cwd=root,
                check=True,
            )
            with self.assertRaisesRegex(ValueError, "HEAD changed"):
                campaigns._validate_state_briefs(
                    campaigns.read_state(paths["state"])
                )

    def test_add_target_needs_an_active_campaign(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "active campaign state does not exist"):
                campaigns.add_target(
                    Path(directory) / "missing.json", "0x00402000"
                )

    def test_unlinked_legacy_evidence_does_not_reset_a_retry_penalty(self):
        records = [
            {
                "record_type": "evidence",
                "timestamp": "2026-08-03T12:10:00+00:00",
                "addresses": ["0x00401000"],
            },
            {
                "timestamp": "2026-08-03T12:00:00+00:00",
                "result": "no-source",
                "addresses": ["0x00401000"],
            },
        ]
        stats = campaigns.address_stats(records)[0x00401000]
        self.assertEqual(stats.zero_yield_attempts, 1)
        self.assertEqual(stats.penalty_attempts, 1)
        self.assertEqual(stats.retry_credits, 0)

    def test_legacy_migration_preserves_duplicate_campaigns_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            ledger = root / "ledger.jsonl"
            legacy = root / "legacy.jsonl"
            first = {"addresses": ["0x00401000"], "result": "no-source"}
            second = {"addresses": ["0x00402000"], "result": "source"}
            ledger.write_text(json.dumps(first) + "\n", encoding="utf-8")
            legacy.write_text(
                json.dumps(first) + "\n" + json.dumps(first) + "\n" + json.dumps(second) + "\n",
                encoding="utf-8",
            )
            with patch.object(campaigns, "DEFAULT_LEDGER", ledger):
                self.assertEqual(campaigns.migrate_legacy_ledger(ledger, legacy), 2)
                self.assertEqual(campaigns.migrate_legacy_ledger(ledger, legacy), 0)
            self.assertEqual(campaigns._read_records(ledger), [first, first, second])

    def test_legacy_overlay_does_not_change_the_tracked_ledger(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = root / "ledger.jsonl"
            legacy = root / "legacy.jsonl"
            committed = {"addresses": ["0x00401000"], "result": "source"}
            local = {"addresses": ["0x00402000"], "result": "no-source"}
            original = json.dumps(committed) + "\n"
            ledger.write_text(original, encoding="utf-8")
            legacy.write_text(json.dumps(local) + "\n", encoding="utf-8")
            with (
                patch.object(campaigns, "DEFAULT_LEDGER", ledger),
                patch.object(campaigns, "LEGACY_LEDGER", legacy),
            ):
                self.assertEqual(campaigns.read_records(ledger), [committed, local])
            self.assertEqual(ledger.read_text(encoding="utf-8"), original)

    def test_invalid_json_reports_the_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaigns.jsonl"
            path.write_text("{}\nnot-json\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "line 2"):
                campaigns.read_records(path)

    def test_summary_reports_meta_separately_from_production_rate(self):
        records = [
            {
                "started_at": "2026-08-03T12:00:00+00:00",
                "ended_at": "2026-08-03T12:10:00+00:00",
                "timestamp": "2026-08-03T12:10:00+00:00",
                "mode": "refinement",
                "result": "source",
                "addresses": ["0x00401000"],
                "minutes": 10,
                "effective_bytes": 100,
            },
            {
                "started_at": "2026-08-03T12:10:00+00:00",
                "ended_at": "2026-08-03T12:10:00+00:00",
                "timestamp": "2026-08-03T12:10:00+00:00",
                "mode": "coverage",
                "result": "no-source",
                "addresses": ["0x00402000"],
                "minutes": 0,
            },
            {
                "started_at": "2026-08-03T12:10:00+00:00",
                "ended_at": "2026-08-03T12:30:00+00:00",
                "timestamp": "2026-08-03T12:30:00+00:00",
                "mode": "meta",
                "result": "meta-fix",
                "addresses": [],
                "minutes": 20,
            },
        ]
        summary = campaigns.summarize_records(records, limit=0)
        self.assertEqual(summary["normal_minutes"], 10)
        self.assertEqual(summary["meta_minutes"], 20)
        self.assertEqual(summary["production_bytes_per_minute"], 10)

    def test_duplicate_uses_mode_result_addresses_and_commit(self):
        existing = {
            "mode": "data",
            "result": "source",
            "addresses": ["0x00401000"],
            "commit": "abc123",
            "minutes": 5,
        }
        duplicate = dict(existing, minutes=7, note="new note")
        other = dict(existing, commit="def456")
        self.assertTrue(campaigns.is_duplicate([existing], duplicate))
        self.assertFalse(campaigns.is_duplicate([existing], other))

    def test_measured_retries_use_the_campaign_start_for_identity(self):
        first = {
            "schema_version": 2,
            "record_type": "campaign",
            "started_at": "2026-08-03T12:00:00+00:00",
            "mode": "refinement",
            "addresses": ["0x00401000"],
            "commit": "",
        }
        retry = dict(first, started_at="2026-08-03T13:00:00+00:00")
        duplicate = dict(first, timestamp="2026-08-03T12:10:00+00:00")
        self.assertFalse(campaigns.is_duplicate([first], retry))
        self.assertTrue(campaigns.is_duplicate([first], duplicate))

    def test_record_measures_reports_and_uses_the_start_size_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            first_score = paths["started"] + timedelta(minutes=3)
            self.assertEqual(
                campaigns.mark_first_score(
                    paths["state"], "0x00401000", first_score
                ),
                "recorded",
            )
            paths["map"].write_text(
                "0x00401000 Target\n0x00402000 Later\n", encoding="utf-8"
            )
            paths["sizes"].write_text(
                json.dumps([{"address": "00401000", "size": 200}]),
                encoding="utf-8",
            )
            item = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "source",
                now=paths["started"] + timedelta(minutes=10),
            )
            self.assertEqual(item["effective_bytes_before"], 25)
            self.assertEqual(item["effective_bytes_after"], 75)
            self.assertEqual(item["effective_bytes"], 50)
            self.assertEqual(item["initialized_bytes"], 7)
            self.assertEqual(item["minutes"], 10)
            self.assertEqual(item["first_score_minutes"], 3)
            self.assertEqual(item["post_first_score_minutes"], 7)
            self.assertFalse(paths["state"].exists())

    def test_record_rejects_a_caller_byte_value_that_disagrees(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            with self.assertRaisesRegex(ValueError, "--effective-bytes disagrees"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "source",
                    supplied_effective_bytes=51,
                    now=paths["started"] + timedelta(minutes=10),
                )
            self.assertTrue(paths["state"].exists())
            self.assertFalse(paths["ledger"].exists())

    def test_refinement_source_can_remove_debt_without_a_byte_delta(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            self.write_code_report(paths["after"], 0.25)
            self.write_data_report(paths["after_data"], 40)
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=3),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            item = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "source",
                note="The campaign removed tracked source debt.",
                now=paths["started"] + timedelta(minutes=10),
            )
            self.assertEqual(item["effective_bytes"], 0)
            self.assertEqual(item["initialized_bytes"], 0)

    def test_no_source_result_rejects_a_nonzero_report_delta(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            with self.assertRaisesRegex(ValueError, "no-source disagrees"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "no-source",
                    models=["The loop model did not compile to the retail form."],
                    now=paths["started"] + timedelta(minutes=10),
                )

    def test_record_rejects_a_changed_baseline_report(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            paths["before"].write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "baseline code report changed"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "source",
                    now=paths["started"] + timedelta(minutes=10),
                )

    def test_no_source_requires_and_indexes_a_ruled_out_model(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            self.write_code_report(paths["after"], 0.25)
            self.write_data_report(paths["after_data"], 40)
            with self.assertRaisesRegex(ValueError, "needs at least one --model"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "no-source",
                    now=paths["started"] + timedelta(minutes=4),
                )
            item = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "no-source",
                models=["The array-backed edge model scored 9 percent."],
                now=paths["started"] + timedelta(minutes=4),
            )
            self.assertEqual(
                item["ruled_out_models"],
                ["The array-backed edge model scored 9 percent."],
            )
            self.assertIn("Ruled out: The array-backed", paths["models"].read_text())

    def test_no_source_rejects_campaign_source_edits_but_keeps_preexisting_dirt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src" / "Dirty.cpp"
            source.parent.mkdir()
            original = "// This change existed before the campaign.\n"
            source.write_text(original, encoding="utf-8")
            paths = self.make_measured_campaign(root)
            self.write_code_report(paths["after"], 0.25)
            self.write_data_report(paths["after_data"], 40)

            source.write_text(original + "// Campaign trial.\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "source and index changes"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "no-source",
                    models=["The trial did not match the retail body."],
                    now=paths["started"] + timedelta(minutes=5),
                )

            source.write_text(original, encoding="utf-8")
            item = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "no-source",
                models=["The trial did not match the retail body."],
                now=paths["started"] + timedelta(minutes=5),
            )
            self.assertEqual(item["result"], "no-source")

    def test_no_source_rejects_a_changed_source_index_with_restored_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            source = root / "src" / "Tracked.h"
            source.parent.mkdir()
            source.write_text("#define VALUE 1\n", encoding="utf-8")
            subprocess.run(["git", "add", "src/Tracked.h"], cwd=root, check=True)
            paths = self.make_measured_campaign(root)
            self.write_code_report(paths["after"], 0.25)
            self.write_data_report(paths["after_data"], 40)

            source.write_text("#define VALUE 2\n", encoding="utf-8")
            subprocess.run(["git", "add", "src/Tracked.h"], cwd=root, check=True)
            source.write_text("#define VALUE 1\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "index changes"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "no-source",
                    models=["The source model was not supported."],
                    now=paths["started"] + timedelta(minutes=5),
                )

    def test_no_source_rejects_a_changed_tracked_workflow_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            debt = root / ".notes" / "refactor-debt.md"
            debt.parent.mkdir()
            original = "# Refactor debt\n"
            debt.write_text(original, encoding="utf-8")
            subprocess.run(
                ["git", "add", ".notes/refactor-debt.md"], cwd=root, check=True
            )
            paths = self.make_measured_campaign(root)
            self.write_code_report(paths["after"], 0.25)
            self.write_data_report(paths["after_data"], 40)

            debt.write_text(original + "\n- Campaign-only workflow change.\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "tracked repository files"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "no-source",
                    models=["The source model was not supported."],
                    now=paths["started"] + timedelta(minutes=5),
                )

            debt.write_text(original, encoding="utf-8")
            item = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "no-source",
                models=["The source model was not supported."],
                now=paths["started"] + timedelta(minutes=5),
            )
            self.assertEqual(item["result"], "no-source")

    def test_no_source_finalization_recovers_after_ledger_failure_without_note_duplication(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            self.write_code_report(paths["after"], 0.25)
            self.write_data_report(paths["after_data"], 40)
            arguments = (
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "no-source",
            )
            with patch.object(
                campaigns, "append_record", side_effect=OSError("ledger unavailable")
            ):
                with self.assertRaisesRegex(ValueError, "finalization is incomplete"):
                    campaigns.record_campaign(
                        *arguments,
                        models=["The branch model was not supported."],
                        note="The campaign exhausted the available evidence.",
                        commit="legacy-label",
                        now=paths["started"] + timedelta(minutes=5),
                    )
            pending = campaigns.read_state(paths["state"])
            campaign_id = pending["campaign_id"]
            self.assertEqual(pending["phase"], "finalizing")
            self.assertEqual(
                paths["models"].read_text().count(f"campaign-id: {campaign_id}"), 1
            )

            with self.assertRaisesRegex(ValueError, "--model disagrees"):
                campaigns.record_campaign(
                    *arguments,
                    models=["A different model."],
                )
            with self.assertRaisesRegex(ValueError, "--note disagrees"):
                campaigns.record_campaign(*arguments, note="A different note.")
            with self.assertRaisesRegex(ValueError, "--commit disagrees"):
                campaigns.record_campaign(*arguments, commit="different-label")

            item = campaigns.record_campaign(*arguments)
            self.assertEqual(item["campaign_id"], campaign_id)
            self.assertEqual(len(campaigns._read_records(paths["ledger"])), 1)
            self.assertEqual(
                paths["models"].read_text().count(f"campaign-id: {campaign_id}"), 1
            )
            self.assertFalse(paths["state"].exists())

    def test_abort_recovery_rejects_a_different_ledger(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root)
            other_ledger = root / "other-ledger.jsonl"
            original_unlink = Path.unlink

            def fail_state_unlink(path: Path, *args, **kwargs):
                if path == paths["state"]:
                    raise OSError("state is busy")
                return original_unlink(path, *args, **kwargs)

            with patch.object(Path, "unlink", fail_state_unlink):
                with self.assertRaisesRegex(ValueError, "abort is incomplete"):
                    campaigns.abort_campaign(
                        paths["ledger"],
                        paths["state"],
                        "The supervisor stopped the campaign.",
                        paths["started"] + timedelta(minutes=2),
                    )

            with self.assertRaisesRegex(ValueError, "--file disagrees"):
                campaigns.abort_campaign(
                    other_ledger,
                    paths["state"],
                    "The supervisor stopped the campaign.",
                    paths["started"] + timedelta(minutes=3),
                )
            self.assertFalse(other_ledger.exists())
            item = campaigns.abort_campaign(
                paths["ledger"],
                paths["state"],
                "The supervisor stopped the campaign.",
                paths["started"] + timedelta(minutes=3),
            )
            self.assertEqual(item["record_type"], "abort")
            self.assertEqual(len(campaigns._read_records(paths["ledger"])), 1)

    def test_finalization_recovers_after_state_removal_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            original_unlink = Path.unlink

            def fail_state_unlink(path: Path, *args, **kwargs):
                if path == paths["state"]:
                    raise OSError("state is busy")
                return original_unlink(path, *args, **kwargs)

            with patch.object(Path, "unlink", fail_state_unlink):
                with self.assertRaisesRegex(ValueError, "finalization is incomplete"):
                    campaigns.record_campaign(
                        paths["ledger"],
                        paths["state"],
                        paths["models"],
                        paths["after"],
                        paths["after_data"],
                        "source",
                        now=paths["started"] + timedelta(minutes=5),
                    )
            self.assertEqual(len(campaigns._read_records(paths["ledger"])), 1)
            with self.assertRaisesRegex(ValueError, "finalization is pending"):
                campaigns.abort_campaign(
                    paths["ledger"],
                    paths["state"],
                    "Do not create a false abort.",
                    paths["started"] + timedelta(minutes=6),
                )
            campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "source",
            )
            self.assertEqual(len(campaigns._read_records(paths["ledger"])), 1)
            self.assertFalse(paths["state"].exists())

    def test_source_result_requires_a_first_score_and_mode_result_pairs_match(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            with self.assertRaisesRegex(ValueError, "first-score stamp"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "source",
                    now=paths["started"] + timedelta(minutes=5),
                )
            with self.assertRaisesRegex(ValueError, "requires an active meta"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "meta-fix",
                    now=paths["started"] + timedelta(minutes=5),
                )
        with self.assertRaisesRegex(ValueError, "must use --result meta-fix"):
            campaigns._validate_result_mode("meta", "source")

    def test_severe_under_yield_source_result_requires_a_model(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            self.write_code_report(paths["after"], 0.26)
            state = campaigns.read_state(paths["state"])
            state["expected_retained_bytes"] = 100
            state["prediction"] = campaigns._prediction_metadata(10, 100)
            campaigns.write_state(paths["state"], state)
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            arguments = (
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "source",
            )
            with self.assertRaisesRegex(ValueError, "below 10 percent"):
                campaigns.record_campaign(
                    *arguments,
                    now=paths["started"] + timedelta(minutes=5),
                )
            item = campaigns.record_campaign(
                *arguments,
                models=["The narrow-index model did not explain the body."],
                now=paths["started"] + timedelta(minutes=5),
            )
            self.assertEqual(
                item["ruled_out_models"],
                ["The narrow-index model did not explain the body."],
            )

    def test_lifecycle_rejects_impossible_timestamp_orderings(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            with self.assertRaisesRegex(ValueError, "before the campaign baseline"):
                campaigns.mark_first_score(
                    paths["state"],
                    "0x00401000",
                    paths["started"],
                )
            state = campaigns.read_state(paths["state"])
            state["first_score_at"] = campaigns.timestamp(
                paths["started"] + timedelta(minutes=9)
            )
            state["first_score_address"] = "0x00401000"
            campaigns.write_state(paths["state"], state)
            with self.assertRaisesRegex(ValueError, "first-score time is outside"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "source",
                    now=paths["started"] + timedelta(minutes=8),
                )
            with self.assertRaisesRegex(ValueError, "end time is before"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "source",
                    now=paths["started"] - timedelta(minutes=1),
                )

    def test_target_limits_and_family_anchor_rules(self):
        started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                campaigns.start_campaign(
                    root / "duplicate.json",
                    "coverage",
                    ["0x00401000", "0x00401000"],
                    "Test",
                    started,
                    worktree_root=root,
                )
            with self.assertRaisesRegex(ValueError, "at most three"):
                campaigns.start_campaign(
                    root / "many.json",
                    "coverage",
                    ["0x00401000", "0x00402000", "0x00403000", "0x00404000"],
                    "Test",
                    started,
                    worktree_root=root,
                )

        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            with self.assertRaisesRegex(ValueError, "before the campaign baseline"):
                campaigns.add_target(
                    paths["state"],
                    "0x00402000",
                    paths["started"],
                )
            campaigns.add_target(
                paths["state"],
                "0x00402000",
                paths["started"] + timedelta(minutes=2),
            )
            campaigns.add_target(
                paths["state"],
                "0x00403000",
                paths["started"] + timedelta(minutes=3),
            )
            with self.assertRaisesRegex(ValueError, "at most three targets"):
                campaigns.add_target(
                    paths["state"],
                    "0x00404000",
                    paths["started"] + timedelta(minutes=4),
                )

        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(
                Path(directory),
                addresses=["0x00401000", "0x00402000", "0x00403000"],
            )
            with self.assertRaisesRegex(ValueError, "at most three targets"):
                campaigns.add_target(
                    paths["state"],
                    "0x00404000",
                    paths["started"] + timedelta(minutes=2),
                )

            first_pivot = campaigns.add_target(
                paths["state"],
                "0x00404000",
                paths["started"] + timedelta(minutes=3),
                replaces="0x00401000",
            )
            self.assertEqual(len(first_pivot["active_addresses"]), 3)
            second_pivot = campaigns.add_target(
                paths["state"],
                "0x00405000",
                paths["started"] + timedelta(minutes=4),
                replaces="0x00402000",
            )
            self.assertEqual(
                second_pivot["active_addresses"],
                ["0x00404000", "0x00405000", "0x00403000"],
            )
            with self.assertRaisesRegex(ValueError, "at most two pivots"):
                campaigns.add_target(
                    paths["state"],
                    "0x00406000",
                    paths["started"] + timedelta(minutes=5),
                    replaces="0x00403000",
                )

        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory), family=True)
            with self.assertRaisesRegex(ValueError, "first-score anchor"):
                campaigns.add_target(
                    paths["state"],
                    "0x00402000",
                    paths["started"] + timedelta(minutes=2),
                )
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=3),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            updated = paths
            for index in range(2, 6):
                updated = campaigns.add_target(
                    paths["state"],
                    f"0x0040{index}000",
                    paths["started"] + timedelta(minutes=index + 2),
                    sizes_path=paths["sizes"],
                )
            self.assertEqual(len(updated["addresses"]), 5)
            with self.assertRaisesRegex(ValueError, "comparison for every member"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "source",
                    now=paths["started"] + timedelta(minutes=10),
                )
            for index in range(2, 6):
                campaigns.mark_first_score(
                    paths["state"],
                    f"0x0040{index}000",
                    paths["started"] + timedelta(minutes=index + 7),
                )
            item = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "source",
                now=paths["started"] + timedelta(minutes=14),
            )
            self.assertEqual(len(item["scored_addresses"]), 5)

            with self.assertRaisesRegex(ValueError, "one anchor"):
                campaigns.start_campaign(
                    Path(directory) / "second-family.json",
                    "coverage",
                    ["0x00410000", "0x00411000"],
                    "Test",
                    started,
                    worktree_root=Path(directory),
                    family=True,
                )

        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(
                Path(directory), family=True, family_sizes={2: 98}
            )
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            with self.assertRaisesRegex(ValueError, "more than one percent"):
                campaigns.add_target(
                    paths["state"],
                    "0x00402000",
                    paths["started"] + timedelta(minutes=3),
                    sizes_path=paths["sizes"],
                )

        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory), family=True)
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            paths["sizes"].write_text("[]\n", encoding="utf-8")
            updated = campaigns.add_target(
                paths["state"],
                "0x00402000",
                paths["started"] + timedelta(minutes=3),
                sizes_path=paths["sizes"],
            )
            self.assertEqual(updated["addresses"][-1], "0x00402000")

        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(
                Path(directory), family=True, family_sizes={2: 99, 3: 101}
            )
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            campaigns.add_target(
                paths["state"],
                "0x00402000",
                paths["started"] + timedelta(minutes=3),
                sizes_path=paths["sizes"],
            )
            with self.assertRaisesRegex(ValueError, "more than one percent"):
                campaigns.add_target(
                    paths["state"],
                    "0x00403000",
                    paths["started"] + timedelta(minutes=4),
                    sizes_path=paths["sizes"],
                )

        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(
                Path(directory),
                mode="data",
                addresses=["0x00401000", "0x00402000", "0x00403000"],
            )
            with self.assertRaisesRegex(ValueError, "at most three targets"):
                campaigns.add_target(
                    paths["state"],
                    "0x00404000",
                    paths["started"] + timedelta(minutes=2),
                )

    def test_campaign_ids_do_not_collide_within_one_second(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            first = campaigns.start_campaign(
                root / "first.json",
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "coverage", started
                ),
            )
            second = campaigns.start_campaign(
                root / "second.json",
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "coverage", started
                ),
            )
            self.assertNotEqual(first["campaign_id"], second["campaign_id"])

    def test_campaign_start_fails_closed_without_git_fingerprints(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / "state.json"
            with self.assertRaisesRegex(ValueError, "cannot fingerprint"):
                campaigns.start_campaign(
                    state,
                    "coverage",
                    ["0x00401000"],
                    "Test",
                    datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc),
                    worktree_root=root,
                    doctor_receipt_path=root / "unused-doctor.json",
                )
            self.assertFalse(state.exists())

    def test_duplicate_evidence_replay_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            ledger = Path(directory) / "ledger.jsonl"
            when = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            failed_model = "The pointer-table model failed."
            campaigns.write_records(
                ledger,
                [
                    {
                        "schema_version": 3,
                        "record_type": "campaign",
                        "campaign_id": "failed-1",
                        "timestamp": (when - timedelta(minutes=1)).isoformat(),
                        "ended_at": (when - timedelta(minutes=1)).isoformat(),
                        "mode": "coverage",
                        "lane": "coverage",
                        "result": "no-source",
                        "addresses": ["0x00401000"],
                        "ruled_out_models": [failed_model],
                    }
                ],
            )
            first = campaigns.record_evidence(
                ledger,
                ["0x00401000"],
                "analogue",
                "A matching retail analogue is available.",
                when,
                failed_campaign_id="failed-1",
                failed_model=failed_model,
                changed_source="The analogue adds a typed table declaration.",
            )
            self.assertIn("evidence_id", first)
            with self.assertRaisesRegex(ValueError, "already exists"):
                campaigns.record_evidence(
                    ledger,
                    ["0x00401000"],
                    "analogue",
                    "A matching retail analogue is available.",
                    when + timedelta(minutes=1),
                    failed_campaign_id="failed-1",
                    failed_model=failed_model,
                    changed_source="The analogue adds a typed table declaration.",
                )
            self.assertEqual(len(campaigns._read_records(ledger)), 2)

    def test_summary_sorts_records_before_applying_the_limit(self):
        records = [
            {
                "timestamp": "2026-08-03T12:10:00+00:00",
                "mode": "coverage",
                "result": "source",
                "addresses": ["0x00402000"],
            },
            {
                "timestamp": "2026-08-03T12:00:00+00:00",
                "mode": "coverage",
                "result": "source",
                "addresses": ["0x00401000"],
            },
        ]
        output = StringIO()
        with redirect_stdout(output):
            campaigns.print_summary(records, 1)
        self.assertIn("0x00402000", output.getvalue())
        self.assertNotIn("0x00401000", output.getvalue())

    def test_function_size_snapshot_excludes_unmapped_ghidra_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            function_map = root / "functions_map.txt"
            sizes_path = root / "sizes.json"
            report = root / "report.json"
            function_map.write_text(
                "0x00401000 First\n0x00402000 Last\n", encoding="utf-8"
            )
            sizes_path.write_text(
                json.dumps(
                    [
                        {"address": "00401000", "size": 100},
                        {"address": "00402000", "size": 80},
                        {"address": "00500000", "size": 400},
                    ]
                ),
                encoding="utf-8",
            )
            report.write_text(
                json.dumps(
                    {
                        "data": [
                            {"address": "0x00500000", "matching": 1.0, "type": 1}
                        ]
                    }
                ),
                encoding="utf-8",
            )
            sizes = campaigns.read_function_sizes(function_map, sizes_path)
            self.assertEqual(sizes[0x00402000], 80)
            self.assertNotIn(0x00500000, sizes)
            self.assertEqual(campaigns.effective_code_bytes(report, sizes), 0)

    def test_direct_source_targets_match_the_map_and_campaign_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "src"
            source_root.mkdir()
            (source_root / "Targets.cpp").write_text(
                "// FUNCTION: TOY2 0x00401000\n"
                "// STUB: TOY2 0x00402000\n"
                "// LIBRARY: TOY2 0x00403000\n",
                encoding="utf-8",
            )
            function_map = root / "functions_map.txt"
            function_map.write_text(
                "0x00401000 Function\n"
                "0x00402000 Stub\n"
                "0x00403000 Library\n"
                "0x00404000 Next\n",
                encoding="utf-8",
            )
            sizes = root / "sizes.json"
            sizes.write_text(
                json.dumps(
                    [
                        {"address": "00401000", "size": 3000},
                        {"address": "00402000", "size": 3000},
                        {"address": "00403000", "size": 3000},
                    ]
                ),
                encoding="utf-8",
            )
            campaigns.validate_source_target(
                "0x00401000", "refinement", function_map, sizes, source_root
            )
            campaigns.validate_source_target(
                "0x00402000", "coverage", function_map, sizes, source_root
            )
            with self.assertRaisesRegex(ValueError, "coverage target"):
                campaigns.validate_source_target(
                    "0x00401000", "coverage", function_map, sizes, source_root
                )
            with self.assertRaisesRegex(ValueError, "must be FUNCTION"):
                campaigns.validate_source_target(
                    "0x00402000", "refinement", function_map, sizes, source_root
                )
            with self.assertRaisesRegex(ValueError, "must be FUNCTION"):
                campaigns.validate_source_target(
                    "0x00403000", "refinement", function_map, sizes, source_root
                )
            with self.assertRaisesRegex(ValueError, "not in functions_map"):
                campaigns.validate_source_target(
                    "0x00500000", "coverage", function_map, sizes, source_root
                )
            sizes.write_text(
                json.dumps([{"address": "00402000", "size": 10}]),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "map defect"):
                campaigns.validate_source_target(
                    "0x00402000", "coverage", function_map, sizes, source_root
                )

    def test_source_target_validation_uses_only_active_annotations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "src"
            source_root.mkdir()
            (source_root / "Targets.cpp").write_text(
                "#if 0\n"
                "// FUNCTION: TOY2 0x00401000\n"
                "#else\n"
                "// STUB: TOY2 0x00402000\n"
                "#endif\n"
                "// FUNCTION: TOY2 0x00403000\n",
                encoding="utf-8",
            )
            function_map = root / "functions_map.txt"
            function_map.write_text(
                "0x00401000 Inactive\n"
                "0x00402000 ElseStub\n"
                "0x00403000 Active\n"
                "0x00404000 Next\n",
                encoding="utf-8",
            )
            sizes = root / "sizes.json"
            sizes.write_text(
                json.dumps(
                    [
                        {"address": "00401000", "size": 0x1000},
                        {"address": "00402000", "size": 0x1000},
                    ]
                ),
                encoding="utf-8",
            )

            campaigns.validate_source_target(
                "0x00401000", "coverage", function_map, sizes, source_root
            )
            campaigns.validate_source_target(
                "0x00402000", "coverage", function_map, sizes, source_root
            )
            campaigns.validate_source_target(
                "0x00403000", "refinement", function_map, sizes, source_root
            )
            with self.assertRaisesRegex(ValueError, "is unannotated"):
                campaigns.validate_source_target(
                    "0x00401000", "refinement", function_map, sizes, source_root
                )

    def test_standard_source_scan_uses_only_active_function_annotations(self):
        source = (
            "#if 0\n"
            "// FUNCTION: TOY2 0x00401000\n"
            "#else\n"
            "// FUNCTION: TOY2 0x00402000\n"
            "#endif\n"
            "// FUNCTION: TOY2 0x00403000\n"
        )
        statuses = {
            address: argparse.Namespace(matching=1.0, effective=False)
            for address in (0x00401000, 0x00402000, 0x00403000)
        }
        state = {
            "mode": "coverage",
            "function_sizes": {
                "0x00401000": 10,
                "0x00402000": 20,
                "0x00403000": 30,
            },
        }
        with (
            patch(
                "tools.decomp_lint.target_units",
                return_value=[argparse.Namespace(text=source)],
            ),
            patch("tools.decomp_lint.scan_units", return_value=[]),
            patch("tools.decomp_lint.read_baseline", return_value=[]),
            patch("tools.decomp_lint.apply_baseline", return_value=([], [])),
            patch("tools.decomp_status.read_match_statuses", return_value=statuses),
            patch.object(campaigns, "effective_code_bytes", return_value=50),
        ):
            scan, _ = campaigns._standard_source_scan(
                state, Path("report.json"), staged=True
            )

        self.assertEqual(scan["metrics"]["implemented"], 2)
        self.assertEqual(scan["metrics"]["terminal"], 2)
        self.assertEqual(scan["metrics"]["terminal_bytes"], 50)

    def test_integrated_delivery_uses_only_active_function_annotations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src" / "Targets.cpp"
            source.parent.mkdir()
            source.write_text(
                "#if 0\n"
                "// FUNCTION: TOY2 0x00401000\n"
                "#else\n"
                "// FUNCTION: TOY2 0x00402000\n"
                "#endif\n"
                "// FUNCTION: TOY2 0x00403000\n",
                encoding="utf-8",
            )
            commands = {
                "build": [["build-tool"]],
                "code_report": [["code-report-tool"]],
                "data_report": [],
                "source_scan": [],
                "validation": [],
            }
            completed = argparse.Namespace(returncode=0, stdout="", stderr="")
            statuses = {
                address: argparse.Namespace(
                    matching=1.0, effective=False, exact=True
                )
                for address in (0x00401000, 0x00402000, 0x00403000)
            }
            sizes = {
                0x00401000: 10,
                0x00402000: 20,
                0x00403000: 30,
            }
            metrics = {
                "implemented": 2,
                "terminal": 2,
                "terminal_bytes": 50,
                "effective_bytes": 50,
                "source_debt": 0,
            }

            def campaign(active: list[str]) -> dict[str, object]:
                return {
                    "mode": "refinement",
                    "lane": "production",
                    "result": "source",
                    "active_addresses": active,
                    "target_deltas": {
                        address: {"effective_after": 10} for address in active
                    },
                    "implemented_after": 0,
                    "terminal_after": 0,
                    "terminal_bytes_after": 0,
                    "effective_bytes_after": 0,
                    "source_debt_after": 0,
                }

            with (
                patch.object(
                    campaigns, "standard_finalize_commands", return_value=commands
                ),
                patch.object(
                    campaigns, "_standard_command", return_value=completed
                ),
                patch("tools.decomp_provenance._artifact_state", return_value={}),
                patch("tools.decomp_provenance.current_identity", return_value={}),
                patch("tools.decomp_provenance.seal_report"),
                patch("tools.decomp_provenance.validate_report"),
                patch.object(campaigns, "read_function_sizes", return_value=sizes),
                patch.object(
                    campaigns,
                    "_standard_source_scan",
                    return_value=(
                        {"metrics": metrics},
                        {"new_errors": [], "new_warnings": [], "stale": []},
                    ),
                ),
                patch("tools.decomp_status.read_match_statuses", return_value=statuses),
                patch.object(
                    campaigns, "effective_code_by_address", return_value=sizes
                ),
                patch.object(
                    campaigns,
                    "_source_paths",
                    return_value=[Path("src/Targets.cpp")],
                ),
                patch.object(campaigns, "_standard_input_hashes", return_value={}),
                patch.object(campaigns, "_delivery_artifacts", return_value={}),
            ):
                with self.assertRaisesRegex(ValueError, "not a valid function"):
                    campaigns._run_delivery_validation(
                        campaign(["0x00401000"]), root, {"code": {}}
                    )
                result = campaigns._run_delivery_validation(
                    campaign(["0x00402000", "0x00403000"]),
                    root,
                    {"code": {}},
                )

        self.assertEqual(len(result["commands"]), 4)

    def test_baseline_attachment_rechecks_a_coverage_map_defect(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            state = root / "state.json"
            function_map = root / "functions_map.txt"
            function_sizes = root / "function_sizes.json"
            before = root / "before.json"
            before_data = root / "before-data.json"
            function_map.write_text(
                "0x00401000 Target\n0x00401100 Next\n", encoding="utf-8"
            )
            function_sizes.write_text(
                json.dumps([{"address": "00401000", "size": 100}]),
                encoding="utf-8",
            )
            self.write_code_report(before, 0.25)
            self.write_data_report(before_data, 40)
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state,
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "coverage", started
                ),
            )
            with self.assertRaisesRegex(ValueError, "map defect"):
                campaigns.attach_baseline(
                    state,
                    before,
                    before_data,
                    function_map,
                    function_sizes,
                    started + timedelta(minutes=1),
                )

    def test_baseline_rejects_an_empty_function_size_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            state = root / "state.json"
            function_map = root / "functions_map.txt"
            function_sizes = root / "function_sizes.json"
            before = root / "before.json"
            before_data = root / "before-data.json"
            function_map.write_text(
                "0x00401000 Target\n0x00401100 Next\n", encoding="utf-8"
            )
            function_sizes.write_text("[]\n", encoding="utf-8")
            self.write_code_report(before, 0.25)
            self.write_data_report(before_data, 40)
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state,
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "coverage", started
                ),
            )
            with self.assertRaisesRegex(ValueError, "nonempty retail"):
                campaigns.attach_baseline(
                    state,
                    before,
                    before_data,
                    function_map,
                    function_sizes,
                    started + timedelta(minutes=1),
                )

    def test_family_start_can_use_the_snapshot_refreshed_for_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            state = root / "state.json"
            function_map = root / "functions_map.txt"
            function_sizes = root / "function_sizes.json"
            before = root / "before.json"
            before_data = root / "before-data.json"
            function_map.write_text(
                "0x00401000 Target\n0x00401100 Next\n", encoding="utf-8"
            )
            self.write_code_report(before, 0.25)
            self.write_data_report(before_data, 40)
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state,
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                family=True,
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "coverage", started
                ),
            )
            function_sizes.write_text(
                json.dumps([{"address": "00401000", "size": 250}]),
                encoding="utf-8",
            )
            baseline_identity = self.seal_report_pair(root, before, before_data)
            with patch(
                "tools.decomp_provenance.current_identity",
                return_value=baseline_identity,
            ):
                attached = campaigns.attach_baseline(
                    state,
                    before,
                    before_data,
                    function_map,
                    function_sizes,
                    started + timedelta(minutes=1),
                )
            self.assertEqual(
                attached["analyzed_function_sizes"], {"0x00401000": 250}
            )

    def test_status_reports_estimates_and_absolute_deadlines_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            state = root / "state.json"
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state,
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                expected_minutes=24,
                expected_retained_bytes=96,
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "coverage", started
                ),
            )
            before = state.read_bytes()
            output = StringIO()
            with redirect_stdout(output):
                campaigns.print_status(state, started + timedelta(minutes=5))
            self.assertEqual(state.read_bytes(), before)
            text = output.getvalue()
            self.assertIn("Preflight deadline: 2026-08-03T12:10:00+00:00", text)
            self.assertIn("First-score deadline: 2026-08-03T12:16:00+00:00", text)
            self.assertIn("Stop deadline: 2026-08-03T12:24:00+00:00", text)
            self.assertIn("Extension deadline: not available", text)
            self.assertIn("Expected retained bytes: 96.00", text)

    def test_extension_deadline_needs_at_least_100_expected_bytes(self):
        started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
        below = campaigns._campaign_deadlines(started, 24, 99)
        eligible = campaigns._campaign_deadlines(started, 24, 100)
        self.assertIsNone(below["extension_deadline"])
        self.assertEqual(
            eligible["extension_deadline"],
            "2026-08-03T12:30:00+00:00",
        )

    def test_prediction_handoff_keeps_median_and_lower_bound_distinct(self):
        incomplete = campaigns._prediction_metadata(12, 80)
        with self.assertRaisesRegex(ValueError, "lower-bound"):
            campaigns._validate_prediction_handoff(
                "production", incomplete, required=True
            )
        complete = campaigns._prediction_metadata(
            12,
            80,
            version="cohort-v1",
            lower_bound_retained_bytes=20,
            features={
                "success_probability": 0.5,
                "cohort_sample_size": 8,
                "median_retained_bytes": 80,
                "median_minutes": 12,
            },
        )
        campaigns._validate_prediction_handoff(
            "production", complete, required=True
        )
        self.assertEqual(complete["expected_retained_bytes"], 80)
        self.assertEqual(complete["lower_bound_retained_bytes"], 20)
        invalid_features = (
            ("success_probability", 2.0, "success probability"),
            ("cohort_sample_size", -1, "sample count"),
            ("cohort_sample_size", True, "sample count"),
            ("median_retained_bytes", "80", "predicted median bytes"),
            ("median_minutes", 0, "predicted median minutes"),
        )
        for name, value, message in invalid_features:
            with self.subTest(name=name, value=value):
                invalid = campaigns._prediction_metadata(
                    12,
                    80,
                    version="cohort-v1",
                    lower_bound_retained_bytes=20,
                    features={
                        "success_probability": 0.5,
                        "cohort_sample_size": 8,
                        "median_retained_bytes": 80,
                        "median_minutes": 12,
                    },
                )
                invalid["features"][name] = value
                with self.assertRaisesRegex(ValueError, message):
                    campaigns._validate_prediction_handoff(
                        "production", invalid, required=True
                    )
        with self.assertRaisesRegex(ValueError, "finite"):
            campaigns._validate_estimate("forecast", "12", allow_zero=False)

    def test_selector_handoff_supplies_all_campaign_prediction_arguments(self):
        from tools import decomp_candidates

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "prediction.json"
            fingerprint = decomp_candidates.candidate_selection_fingerprint(
                dependency_mode=True
            )
            payload = {
                "schema_version": 2,
                "generated_at": datetime.now(timezone.utc).isoformat(),
                "selection_fingerprint": fingerprint,
                "selection_fingerprint_sha256": (
                    decomp_candidates.candidate_cache_key(fingerprint)
                ),
                "prediction_version": "cohort-v1",
                "lane": "production",
                "mode": "refinement",
                "address": "0x00401000",
                "success_probability": 0.5,
                "cohort_sample_size": 8,
                "median_retained_bytes": 80,
                "median_minutes": 12,
                "expected_retained_bytes": 80,
                "expected_minutes": 12,
                "prediction_lower_bound_bytes": 20,
            }
            path.write_text(json.dumps(payload), encoding="utf-8")
            current = dict(payload)
            current["generated_at"] = "2026-08-03T12:00:00+00:00"
            with patch.object(
                decomp_candidates,
                "current_prediction_feature_handoff",
                return_value=current,
            ):
                values = campaigns._prediction_handoff_arguments(
                    path,
                    lane="production",
                    mode="refinement",
                    addresses=["0x00401000"],
                )
            self.assertEqual(values["expected_minutes"], 12)
            self.assertEqual(values["expected_retained_bytes"], 80)
            self.assertEqual(values["prediction_features"], payload)
            with self.assertRaisesRegex(ValueError, "target disagrees"):
                campaigns._prediction_handoff_arguments(
                    path,
                    lane="production",
                    mode="refinement",
                    addresses=["0x00402000"],
                )

            tampered = dict(payload)
            tampered["expected_minutes"] = 99
            path.write_text(json.dumps(tampered), encoding="utf-8")
            with patch.object(
                decomp_candidates,
                "current_prediction_feature_handoff",
                return_value=current,
            ), self.assertRaisesRegex(ValueError, "current selection"):
                campaigns._prediction_handoff_arguments(
                    path,
                    lane="production",
                    mode="refinement",
                    addresses=["0x00401000"],
                )

    def test_single_target_production_cli_requires_selector_handoff(self):
        arguments = argparse.Namespace(
            prediction_handoff=None,
            expected_minutes=12,
            expected_retained_bytes=80,
            prediction_version="cohort-v1",
            prediction_lower_bound_bytes=20,
            prediction_features=None,
        )
        with self.assertRaisesRegex(ValueError, "need one --prediction-handoff"):
            campaigns._cli_prediction_arguments(
                arguments,
                lane="production",
                mode="refinement",
                addresses=["0x00401000"],
            )

    def test_multi_target_cli_combines_exact_selector_handoffs(self):
        addresses = ["0x00401000", "0x00402000"]
        arguments = argparse.Namespace(
            prediction_handoff=[Path("first.json"), Path("second.json")],
            expected_minutes=None,
            expected_retained_bytes=None,
            prediction_version=None,
            prediction_lower_bound_bytes=None,
            prediction_features=None,
        )
        handoffs = [
            {
                "expected_minutes": 9,
                "expected_retained_bytes": 90,
                "prediction_version": "cohort-v1",
                "prediction_lower_bound_bytes": 20,
                "prediction_features": {
                    "success_probability": 0.7,
                    "cohort_sample_size": 8,
                },
                "subsystem": "Renderer",
            },
            {
                "expected_minutes": 3,
                "expected_retained_bytes": 10,
                "prediction_version": "cohort-v1",
                "prediction_lower_bound_bytes": 2,
                "prediction_features": {
                    "success_probability": 0.5,
                    "cohort_sample_size": 4,
                },
                "subsystem": "Renderer",
            },
        ]
        with patch.object(
            campaigns, "_prediction_handoff_arguments", side_effect=handoffs
        ) as validate:
            result = campaigns._cli_prediction_arguments(
                arguments,
                lane="production",
                mode="refinement",
                addresses=addresses,
            )
        self.assertEqual(validate.call_count, 2)
        self.assertEqual(result["expected_retained_bytes"], 100)
        self.assertEqual(result["expected_minutes"], 12)
        self.assertEqual(result["prediction_lower_bound_bytes"], 22)
        self.assertEqual(
            result["prediction_features"]["per_target"][addresses[0]][
                "median_retained_bytes"
            ],
            90,
        )
        campaigns._validate_prediction_handoff(
            "production",
            campaigns._prediction_metadata(
                result["expected_minutes"],
                result["expected_retained_bytes"],
                version=result["prediction_version"],
                lower_bound_retained_bytes=result[
                    "prediction_lower_bound_bytes"
                ],
                features=result["prediction_features"],
            ),
            required=True,
            addresses=addresses,
        )

        arguments.prediction_handoff = [Path("first.json")]
        with self.assertRaisesRegex(ValueError, "one --prediction-handoff"):
            campaigns._cli_prediction_arguments(
                arguments,
                lane="production",
                mode="refinement",
                addresses=addresses,
            )

    def test_multi_target_forecast_rejects_missing_extra_and_wrong_sums(self):
        addresses = ["0x00401000", "0x00402000"]

        def prediction(per_target: dict[str, object]):
            return campaigns._prediction_metadata(
                12,
                100,
                version="bundle-v1",
                lower_bound_retained_bytes=22,
                features={
                    "success_probability": 0.5,
                    "cohort_sample_size": 4,
                    "median_retained_bytes": 100,
                    "median_minutes": 12,
                    "per_target": per_target,
                },
            )

        valid = {
            addresses[0]: {
                "median_retained_bytes": 90,
                "lower_retained_bytes": 20,
            },
            addresses[1]: {
                "median_retained_bytes": 10,
                "lower_retained_bytes": 2,
            },
        }
        bad_values = [
            {addresses[0]: valid[addresses[0]]},
            {**valid, "0x00403000": valid[addresses[1]]},
            {
                **valid,
                addresses[0]: {
                    "median_retained_bytes": 80,
                    "lower_retained_bytes": 20,
                },
            },
        ]
        for value in bad_values:
            with self.subTest(value=value), self.assertRaises(ValueError):
                campaigns._validate_prediction_handoff(
                    "production",
                    prediction(value),
                    required=True,
                    addresses=addresses,
                )

    def test_target_metrics_do_not_credit_an_abandoned_pivot(self):
        records = [
            {
                "timestamp": "2026-08-03T12:10:00+00:00",
                "mode": "refinement",
                "result": "source",
                "addresses": ["0x00401000", "0x00402000"],
                "minutes": 8,
                "effective_bytes": 50,
                "initialized_bytes": 0.75,
                "target_deltas": {
                    "0x00401000": {
                        "effective_bytes": 50,
                        "initialized_bytes": 0,
                    },
                    "0x00402000": {
                        "effective_bytes": 0,
                        "initialized_bytes": 0.75,
                    },
                },
            }
        ]
        stats = campaigns.address_stats(records)
        self.assertEqual(stats[0x00401000].effective_bytes, 50)
        self.assertEqual(stats[0x00402000].effective_bytes, 0)
        self.assertEqual(stats[0x00402000].initialized_bytes, 0.75)

    def test_abort_keeps_an_audit_record_and_removes_active_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            ledger = root / "ledger.jsonl"
            state = root / "state.json"
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state,
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "coverage", started
                ),
            )
            item = campaigns.abort_campaign(
                ledger,
                state,
                "The user stopped the campaign.",
                started + timedelta(minutes=2),
            )
            self.assertEqual(item["record_type"], "abort")
            self.assertEqual(item["minutes"], 2)
            self.assertFalse(state.exists())

    def test_abort_cannot_discard_a_finalized_campaign(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / "state.json"
            ledger = root / "ledger.jsonl"
            campaigns.write_state(
                state,
                {
                    "schema_version": 3,
                    "campaign_id": "campaign-1",
                    "phase": "finalized",
                    "finalize_receipt": {"content_sha256": "a" * 64},
                },
            )
            before = state.read_bytes()
            with self.assertRaisesRegex(ValueError, "campaign is finalized"):
                campaigns.abort_campaign(ledger, state, "Stop")
            self.assertEqual(state.read_bytes(), before)
            self.assertFalse(ledger.exists())
            self.assertEqual(campaigns._read_records(ledger), [])

    def test_schema_versions_remain_readable_and_new_state_uses_v3(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = root / "ledger.jsonl"
            records = [
                {"mode": "coverage", "result": "no-source"},
                {
                    "schema_version": 2,
                    "record_type": "campaign",
                    "campaign_id": "v2",
                },
                {
                    "schema_version": 3,
                    "record_type": "delivery",
                    "delivery_id": "v3",
                },
            ]
            campaigns.write_records(ledger, records)
            self.assertEqual(campaigns.read_records(ledger), records)

            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            state = campaigns.start_campaign(
                root / "state.json",
                "refinement",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
                progress_before={
                    "implemented": 10,
                    "terminal": 4,
                    "terminal_bytes": 200,
                    "effective_bytes": 300.5,
                    "source_debt_functions": 2,
                },
                doctor_receipt_path=self.write_doctor_receipt(
                    root, "refinement", started
                ),
            )
            self.assertEqual(state["schema_version"], 3)
            self.assertEqual(state["lane"], "production")
            self.assertEqual(state["implemented_before"], 10)
            self.assertEqual(state["source_debt_before"], 2)
            self.assertIn("selection", state["phase_timestamps"])

    def test_doctor_receipt_checks_target_hashes_mode_and_freshness(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            receipt = self.write_doctor_receipt(root, "coverage", started)
            payload = json.loads(receipt.read_text(encoding="utf-8"))
            payload["addresses"] = ["0x00402000"]
            receipt = self.write_sealed_doctor_receipt(root, payload)
            with self.assertRaisesRegex(ValueError, "selection check is inconsistent"):
                campaigns.start_campaign(
                    root / "wrong-target.json",
                    "coverage",
                    ["0x00401000"],
                    "Test",
                    started,
                    worktree_root=root,
                    doctor_receipt_path=receipt,
                )

            receipt = self.write_doctor_receipt(root, "coverage", started)
            payload = json.loads(receipt.read_text(encoding="utf-8"))
            payload["doctor_ended_at"] = (
                started - timedelta(minutes=61)
            ).isoformat()
            payload["doctor_started_at"] = (
                started - timedelta(minutes=62)
            ).isoformat()
            payload["selection_started_at"] = (
                started - timedelta(minutes=63)
            ).isoformat()
            receipt = self.write_sealed_doctor_receipt(root, payload)
            with self.assertRaisesRegex(ValueError, "too old"):
                campaigns.start_campaign(
                    root / "stale.json",
                    "coverage",
                    ["0x00401000"],
                    "Test",
                    started,
                    worktree_root=root,
                    doctor_receipt_path=receipt,
                )

            receipt = self.write_doctor_receipt(root, "coverage", started)
            payload = json.loads(receipt.read_text(encoding="utf-8"))
            payload["input_hashes"]["source_index_sha256"] = "0" * 64
            receipt = self.write_sealed_doctor_receipt(root, payload)
            with self.assertRaisesRegex(ValueError, "input hashes are stale"):
                campaigns.start_campaign(
                    root / "stale-input.json",
                    "coverage",
                    ["0x00401000"],
                    "Test",
                    started,
                    worktree_root=root,
                    doctor_receipt_path=receipt,
                )

    def test_lane_mode_pairs_are_closed_and_lane_stats_are_isolated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            with self.assertRaisesRegex(ValueError, "cannot use coverage"):
                campaigns.start_campaign(
                    root / "bad-lane.json",
                    "coverage",
                    ["0x00401000"],
                    "Test",
                    started,
                    worktree_root=root,
                    lane="closure",
                    doctor_receipt_path=self.write_doctor_receipt(
                        root, "closure", started, mode="coverage"
                    ),
                )

        records = [
            {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": "research-fail",
                "timestamp": "2026-08-03T12:00:00+00:00",
                "ended_at": "2026-08-03T12:00:00+00:00",
                "mode": "coverage",
                "lane": "research",
                "result": "no-source",
                "addresses": ["0x00401000"],
                "ruled_out_models": ["The research model failed."],
            },
            {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": "production-fail",
                "timestamp": "2026-08-03T12:01:00+00:00",
                "ended_at": "2026-08-03T12:01:00+00:00",
                "mode": "refinement",
                "lane": "production",
                "result": "no-source",
                "addresses": ["0x00401000"],
                "ruled_out_models": ["The production model failed."],
            },
            {
                "schema_version": 3,
                "record_type": "evidence",
                "timestamp": "2026-08-03T12:02:00+00:00",
                "addresses": ["0x00401000"],
                "failed_campaign_id": "research-fail",
                "failed_model": "The research model failed.",
                "changed_assumption": "The caller proves a different ABI.",
                "changed_source": None,
            },
        ]
        self.assertEqual(
            campaigns.address_stats(records, lane="research")[
                0x00401000
            ].penalty_attempts,
            0,
        )
        self.assertEqual(
            campaigns.address_stats(records, lane="production")[
                0x00401000
            ].penalty_attempts,
            1,
        )
        aggregate = campaigns.address_stats(records)[0x00401000]
        self.assertEqual(aggregate.attempts, 2)
        self.assertEqual(aggregate.zero_yield_attempts, 2)
        self.assertEqual(aggregate.retry_credits, 1)

    def test_evidence_requires_one_valid_failed_model_link(self):
        with tempfile.TemporaryDirectory() as directory:
            ledger = Path(directory) / "ledger.jsonl"
            failed = {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": "failed",
                "timestamp": "2026-08-03T12:00:00+00:00",
                "ended_at": "2026-08-03T12:00:00+00:00",
                "mode": "coverage",
                "lane": "research",
                "result": "no-source",
                "addresses": ["0x00401000"],
                "ruled_out_models": ["The table model failed."],
            }
            campaigns.write_records(ledger, [failed])
            when = datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc)
            with self.assertRaisesRegex(ValueError, "changed-assumption"):
                campaigns.record_evidence(
                    ledger,
                    ["0x00401000"],
                    "layout",
                    "The layout changed.",
                    when,
                    failed_campaign_id="failed",
                    failed_model="The table model failed.",
                )
            with self.assertRaisesRegex(ValueError, "use one of its models"):
                campaigns.record_evidence(
                    ledger,
                    ["0x00401000"],
                    "layout",
                    "The layout changed.",
                    when,
                    failed_campaign_id="failed",
                    failed_model="A different model failed.",
                    changed_assumption="The field width changed.",
                )
            item = campaigns.record_evidence(
                ledger,
                ["0x00401000"],
                "layout",
                "The layout changed.",
                when,
                failed_campaign_id="failed",
                failed_model="The table model failed.",
                changed_assumption="The field width changed.",
            )
            self.assertEqual(item["schema_version"], 3)
            self.assertEqual(item["lane"], "research")

    def test_research_start_rejects_a_target_without_routing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            resources = root / "tools/Resources"
            resources.mkdir(parents=True)
            (resources / "functions_map.txt").write_text(
                "0x00401000 Probe\n",
                encoding="utf-8",
            )
            (resources / "campaign-ledger.jsonl").write_text("", encoding="utf-8")
            (resources / "reconstruction-blockers.tsv").write_text(
                "", encoding="utf-8"
            )
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            doctor_path = self.write_doctor_receipt(
                root,
                "research",
                started,
                mode="coverage",
            )
            from tools import decomp_brief

            route = decomp_brief.research_route_evidence(root, "0x00401000")
            forged_route = dict(route)
            forged_route["eligible"] = True
            forged_route["routes"] = [
                {
                    "kind": "blocker",
                    "blocker_kind": "layout",
                    "blocked_by": [],
                    "reason": "Forged route.",
                }
            ]
            identity = {
                "target": "0x00401000",
                "lane": "research",
                "subsystem": route["subsystem"],
                "research_route": forged_route,
            }
            with (
                patch(
                    "tools.decomp_brief.validate_brief",
                    return_value=identity,
                ),
                self.assertRaisesRegex(
                    ValueError, "does not match current routing evidence"
                ),
            ):
                campaigns.start_campaign(
                    root / "state.json",
                    "coverage",
                    ["0x00401000"],
                    "",
                    started,
                    worktree_root=root,
                    lane="research",
                    expected_minutes=10,
                    expected_retained_bytes=16,
                    prediction_version="research-v1",
                    prediction_lower_bound_bytes=4,
                    prediction_features={"cohort": "manual", "sample_count": 1},
                    doctor_receipt_path=doctor_path,
                    brief_paths=[root / "brief.json"],
                    require_brief=True,
                    require_prediction_metadata=True,
                )
            self.assertFalse((root / "state.json").exists())

    def test_research_start_accepts_a_hash_bound_blocker_route(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            resources = root / "tools/Resources"
            resources.mkdir(parents=True)
            (resources / "functions_map.txt").write_text(
                "0x00401000 Probe\n",
                encoding="utf-8",
            )
            (resources / "campaign-ledger.jsonl").write_text("", encoding="utf-8")
            (resources / "reconstruction-blockers.tsv").write_text(
                "0x00401000\t0x00402000\tNeeds the producer layout.\tlayout\n",
                encoding="utf-8",
            )
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            doctor_path = self.write_doctor_receipt(
                root,
                "research",
                started,
                mode="coverage",
            )
            from tools import decomp_brief

            route = decomp_brief.research_route_evidence(root, "0x00401000")
            identity = {
                "target": "0x00401000",
                "lane": "research",
                "subsystem": route["subsystem"],
                "research_route": route,
            }
            with patch(
                "tools.decomp_brief.validate_brief",
                return_value=identity,
            ):
                state = campaigns.start_campaign(
                    root / "state.json",
                    "coverage",
                    ["0x00401000"],
                    "",
                    started,
                    worktree_root=root,
                    lane="research",
                    expected_minutes=10,
                    expected_retained_bytes=16,
                    prediction_version="research-v1",
                    prediction_lower_bound_bytes=4,
                    prediction_features={"cohort": "manual", "sample_count": 1},
                    doctor_receipt_path=doctor_path,
                    brief_paths=[root / "brief.json"],
                    require_brief=True,
                    require_prediction_metadata=True,
                )

        self.assertEqual(state["lane"], "research")
        self.assertEqual(state["briefs"][0]["research_route"], route)

    def test_research_start_rejects_a_circuit_for_an_inactive_function(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            resources = root / "tools/Resources"
            resources.mkdir(parents=True)
            (resources / "functions_map.txt").write_text(
                "0x00401000 Probe\n",
                encoding="utf-8",
            )
            (resources / "reconstruction-blockers.tsv").write_text(
                "", encoding="utf-8"
            )
            source = root / "src/Probe.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(
                "// FUNCTION: TOY2 0x00401000\nvoid Probe() {}\n",
                encoding="utf-8",
            )
            failures = [
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
                    "minutes": 10,
                    "subsystem": "Probe",
                }
                for index, address in enumerate(
                    (0x00402000, 0x00403000, 0x00404000)
                )
            ]
            (resources / "campaign-ledger.jsonl").write_text(
                "".join(json.dumps(item) + "\n" for item in failures),
                encoding="utf-8",
            )
            started = datetime(2026, 8, 3, 12, 10, tzinfo=timezone.utc)
            doctor_path = self.write_doctor_receipt(
                root,
                "research",
                started,
                mode="refinement",
            )
            from tools import decomp_brief

            forged_route = decomp_brief.research_route_evidence(
                root,
                "0x00401000",
                base_lane_resolver=lambda _root, _address: "production",
            )
            identity = {
                "target": "0x00401000",
                "lane": "research",
                "subsystem": forged_route["subsystem"],
                "research_route": forged_route,
            }
            with (
                patch(
                    "tools.decomp_brief.validate_brief",
                    return_value=identity,
                ),
                patch(
                    "tools.decomp_candidates.current_base_lane",
                    return_value="inactive",
                ),
                self.assertRaisesRegex(
                    ValueError, "does not match current routing evidence"
                ),
            ):
                campaigns.start_campaign(
                    root / "state.json",
                    "refinement",
                    ["0x00401000"],
                    "",
                    started,
                    worktree_root=root,
                    lane="research",
                    expected_minutes=10,
                    expected_retained_bytes=16,
                    prediction_version="research-v1",
                    prediction_lower_bound_bytes=4,
                    prediction_features={"cohort": "manual", "sample_count": 1},
                    doctor_receipt_path=doctor_path,
                    brief_paths=[root / "brief.json"],
                    require_brief=True,
                    require_prediction_metadata=True,
                )
            self.assertFalse((root / "state.json").exists())

    def test_summary_window_reconciles_time_and_exposes_canary_inputs(self):
        records = [
            {
                "schema_version": 3,
                "record_type": "campaign",
                "timestamp": "2026-08-03T12:10:00+00:00",
                "started_at": "2026-08-03T12:00:00+00:00",
                "ended_at": "2026-08-03T12:10:00+00:00",
                "mode": "refinement",
                "lane": "production",
                "result": "source",
                "minutes": 10,
                "effective_bytes": 100,
                "expected_retained_bytes": 100,
                "prediction": {
                    "expected_retained_bytes": 100,
                    "lower_bound_retained_bytes": 75,
                },
                "terminal_before": 4,
                "terminal_after": 4,
            },
            {"record_type": "delivery", "timestamp": "2026-08-03T12:12:00+00:00"},
            {
                "schema_version": 3,
                "record_type": "campaign",
                "timestamp": "2026-08-03T12:20:00+00:00",
                "started_at": "2026-08-03T12:15:00+00:00",
                "ended_at": "2026-08-03T12:20:00+00:00",
                "mode": "refinement",
                "lane": "closure",
                "result": "no-source",
                "minutes": 5,
                "effective_bytes": 0,
                "prediction": {
                    "expected_retained_bytes": 100,
                    "lower_bound_retained_bytes": 50,
                },
                "terminal_before": 4,
                "terminal_after": 5,
            },
            {
                "schema_version": 3,
                "record_type": "campaign",
                "timestamp": "2026-08-03T12:30:00+00:00",
                "started_at": "2026-08-03T12:25:00+00:00",
                "ended_at": "2026-08-03T12:30:00+00:00",
                "mode": "meta",
                "lane": "meta",
                "result": "meta-fix",
                "minutes": 5,
            },
            {
                "schema_version": 3,
                "record_type": "abort",
                "timestamp": "2026-08-03T12:40:00+00:00",
                "started_at": "2026-08-03T12:35:00+00:00",
                "ended_at": "2026-08-03T12:40:00+00:00",
                "mode": "coverage",
                "lane": "research",
                "minutes": 5,
            },
        ]
        summary = campaigns.summarize_records(records, window=4, limit=1)
        self.assertEqual(summary["record_count"], 4)
        self.assertEqual(summary["display_count"], 1)
        self.assertEqual(summary["window_minutes"], 40)
        self.assertEqual(summary["normal_minutes"], 15)
        self.assertEqual(summary["meta_minutes"], 5)
        self.assertEqual(summary["abort_minutes"], 5)
        self.assertEqual(summary["unattributed_gap_minutes"], 15)
        self.assertAlmostEqual(
            summary["normal_share"]
            + summary["meta_share"]
            + summary["abort_share"]
            + summary["unattributed_gap_share"],
            1,
        )
        self.assertEqual(summary["production_bytes_per_minute"], 10)
        self.assertEqual(summary["all_in_bytes_per_minute"], 2.5)
        self.assertEqual(summary["zero_yield_rate"], 0.5)
        self.assertEqual(summary["forecast_realization"], 0.5)
        self.assertEqual(summary["forecast_band_rate"], 0.5)
        self.assertEqual(summary["forecast_lower_bound_coverage"], 1)
        self.assertEqual(summary["forecast_lower_bound_hit_rate"], 0.5)
        self.assertEqual(summary["closure_terminal_conversions"], 1)
        self.assertEqual(summary["closure_minutes"], 5)
        self.assertEqual(summary["terminal_conversions_per_refinement"], 0.5)
        self.assertEqual(summary["per_lane"]["production"]["source"], 1)

        last_two = campaigns.summarize_records(records, window=2, limit=10)
        self.assertEqual(last_two["record_count"], 2)
        self.assertEqual(last_two["meta_minutes"], 5)
        self.assertEqual(last_two["abort_minutes"], 5)
        self.assertEqual(last_two["unattributed_gap_minutes"], 5)

    def test_summary_uses_the_final_active_target_prediction(self):
        record = {
            "schema_version": 3,
            "record_type": "campaign",
            "campaign_id": "pivot-summary",
            "timestamp": "2026-08-03T12:10:00+00:00",
            "started_at": "2026-08-03T12:00:00+00:00",
            "ended_at": "2026-08-03T12:10:00+00:00",
            "mode": "refinement",
            "lane": "production",
            "result": "source",
            "minutes": 10,
            "effective_bytes": 50,
            "addresses": ["0x00401000", "0x00402000"],
            "active_addresses": ["0x00402000"],
            "retired_addresses": ["0x00401000"],
            "prediction": {
                "expected_retained_bytes": 100,
                "lower_bound_retained_bytes": 10,
            },
            "prediction_events": [
                {
                    "addresses": ["0x00401000"],
                    "prediction": {
                        "expected_retained_bytes": 100,
                        "lower_bound_retained_bytes": 10,
                    },
                },
                {
                    "addresses": ["0x00402000"],
                    "prediction": {
                        "expected_retained_bytes": 1000,
                        "lower_bound_retained_bytes": 100,
                    },
                    "replaces": "0x00401000",
                },
            ],
            "target_deltas": {
                "0x00401000": {
                    "effective_bytes": 0,
                    "initialized_bytes": 0,
                },
                "0x00402000": {
                    "effective_bytes": 50,
                    "initialized_bytes": 0,
                },
            },
        }
        summary = campaigns.summarize_records([record], window=1)
        self.assertEqual(summary["forecast_count"], 1)
        self.assertEqual(summary["forecast_realization"], 0.05)
        self.assertEqual(summary["forecast_band_hits"], 0)
        self.assertEqual(summary["forecast_lower_bound_hits"], 0)

    def test_summary_includes_selection_and_delivery_latency(self):
        records = [
            {
                "schema_version": 3,
                "record_type": "campaign",
                "campaign_id": "campaign-1",
                "selection_started_at": "2026-08-03T11:55:00+00:00",
                "started_at": "2026-08-03T12:00:00+00:00",
                "ended_at": "2026-08-03T12:10:00+00:00",
                "timestamp": "2026-08-03T12:10:00+00:00",
                "mode": "refinement",
                "lane": "production",
                "result": "source",
                "effective_bytes": 150,
            },
            {
                "schema_version": 3,
                "record_type": "delivery",
                "campaign_id": "campaign-1",
                "status": "committed",
                "timestamp": "2026-08-03T12:18:00+00:00",
            },
            {
                "schema_version": 3,
                "record_type": "delivery",
                "campaign_id": "campaign-1",
                "status": "pushed",
                "timestamp": "2026-08-03T12:20:00+00:00",
            },
        ]
        summary = campaigns.summarize_records(records, limit=0)
        self.assertEqual(summary["record_count"], 1)
        self.assertEqual(summary["window_minutes"], 25)
        self.assertEqual(summary["production_minutes"], 25)
        self.assertEqual(summary["all_in_bytes_per_minute"], 6)
        self.assertEqual(summary["delivery_event_count"], 2)
        self.assertEqual(summary["delivered_campaigns"], 1)
        self.assertEqual(summary["pushed_campaigns"], 1)
        self.assertEqual(summary["delivery_latency_minutes"], 10)
        self.assertEqual(summary["median_delivery_latency_minutes"], 10)

    def test_source_finalization_enforces_preflight_and_score_deadlines(self):
        cases = (
            (6, 7, "preflight phase"),
            (4, 9, "first score"),
        )
        for preflight_minutes, score_minutes, message in cases:
            with self.subTest(message=message), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                paths = self.make_measured_campaign(root, legacy=False)
                self.set_source_deadlines(
                    paths,
                    expected_bytes=99,
                    preflight_minutes=preflight_minutes,
                    first_score_minutes=score_minutes,
                )
                with self.assertRaisesRegex(ValueError, message):
                    campaigns.finalize_campaign(
                        Path(paths["state"]),
                        "source",
                        self.source_finalize_actions(root, paths),
                        cache_root=root / "cache",
                        clock=lambda: paths["started"] + timedelta(minutes=10),
                    )

    def test_source_finalization_enforces_stop_and_extension_deadlines(self):
        cases = (
            (99, 12, 1, "stop deadline"),
            (100, 15, 1, "extension deadline"),
        )
        for expected_bytes, minutes, seconds, message in cases:
            with self.subTest(message=message), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                paths = self.make_measured_campaign(root, legacy=False)
                self.set_source_deadlines(paths, expected_bytes=expected_bytes)
                with self.assertRaisesRegex(ValueError, message):
                    campaigns.finalize_campaign(
                        Path(paths["state"]),
                        "source",
                        self.source_finalize_actions(root, paths),
                        cache_root=root / "cache",
                        clock=lambda: paths["started"]
                        + timedelta(minutes=minutes, seconds=seconds),
                    )

    def test_source_finalization_can_finish_after_an_on_time_start(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            self.set_source_deadlines(paths, expected_bytes=99)
            started = paths["started"]
            clock_values = iter(
                [
                    started + timedelta(minutes=11, seconds=59),
                    *[started + timedelta(minutes=13)] * 10,
                ]
            )
            receipt = campaigns.finalize_campaign(
                Path(paths["state"]),
                "source",
                self.source_finalize_actions(root, paths),
                cache_root=root / "cache",
                clock=lambda: next(clock_values),
            )
            self.assertEqual(receipt["status"], "passed")
            self.assertEqual(
                receipt["phase_timestamps"]["finalization-started"],
                "2026-08-03T12:11:59+00:00",
            )
            self.assertEqual(
                receipt["phase_timestamps"]["build_ended"],
                "2026-08-03T12:13:00+00:00",
            )

    def test_finalize_receipt_reuses_immutable_report_copies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            calls = {name: 0 for name in campaigns.FINALIZE_STEPS}
            build_artifact = root / "built.bin"
            build_artifact.write_bytes(b"built")

            def action(name, value):
                def run():
                    calls[name] += 1
                    return value

                return run

            actions = {
                "build": action("build", build_artifact),
                "code_report": action("code_report", paths["after"]),
                "data_report": action("data_report", paths["after_data"]),
                "source_scan": action(
                    "source_scan",
                    {
                        "metrics": {
                            "implemented": 11,
                            "terminal": 5,
                            "terminal_bytes": 300,
                            "effective_bytes": 75,
                            "source_debt": 1,
                        }
                    },
                ),
                "validation": action("validation", {"ok": True}),
            }
            fixed = paths["started"] + timedelta(minutes=3)
            receipt = campaigns.finalize_campaign(
                paths["state"],
                "source",
                actions,
                cache_root=root / "cache",
                inputs={"tool": "v1"},
                clock=lambda: fixed,
            )
            self.assertFalse(receipt["reused"])
            self.assertEqual(calls, {name: 1 for name in campaigns.FINALIZE_STEPS})
            reused = campaigns.finalize_campaign(
                paths["state"],
                "source",
                actions,
                cache_root=root / "cache",
                inputs={"tool": "v1"},
                clock=lambda: fixed,
            )
            self.assertTrue(reused["reused"])
            self.assertEqual(calls, {name: 1 for name in campaigns.FINALIZE_STEPS})

            self.write_code_report(paths["after"], 0.8)
            build_artifact.write_bytes(b"rebuilt")
            reused = campaigns.finalize_campaign(
                paths["state"],
                "source",
                actions,
                cache_root=root / "cache",
                inputs={"tool": "v1"},
                clock=lambda: fixed,
            )
            self.assertTrue(reused["reused"])
            immutable = {
                Path(item["source_path"]): Path(item["path"])
                for item in receipt["immutable_artifacts"]
            }
            cached_report = immutable[paths["after"].resolve()]
            from tools.decomp_provenance import provenance_path

            cached_provenance = immutable[provenance_path(paths["after"]).resolve()]
            report_bytes = cached_report.read_bytes()
            cached_report.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "immutable.*changed"):
                campaigns.finalize_campaign(
                    paths["state"],
                    "source",
                    actions,
                    cache_root=root / "cache",
                    inputs={"tool": "v1"},
                    clock=lambda: fixed,
                )
            cached_report.write_bytes(report_bytes)
            cached_provenance.unlink()
            with self.assertRaisesRegex(ValueError, "immutable.*missing"):
                campaigns.finalize_campaign(
                    paths["state"],
                    "source",
                    actions,
                    cache_root=root / "cache",
                    inputs={"tool": "v1"},
                    clock=lambda: fixed,
                )

    def test_invalid_finalizer_artifact_records_failure_phase(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            actions = {
                name: (lambda: {"ok": True})
                for name in campaigns.FINALIZE_STEPS
            }
            actions["build"] = lambda: root / "missing-artifact.bin"
            fixed = paths["started"] + timedelta(minutes=3)
            with self.assertRaisesRegex(ValueError, "build step failed"):
                campaigns.finalize_campaign(
                    paths["state"],
                    "source",
                    actions,
                    cache_root=root / "cache",
                    clock=lambda: fixed,
                )
            state = campaigns.read_state(paths["state"])
            self.assertEqual(state["phase"], "finalize_failed")
            self.assertIn("build_failed", state["phase_timestamps"])

    def test_v3_record_uses_receipt_reports_and_captures_metrics_and_phases(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            state = campaigns.read_state(paths["state"])
            state["metrics_before"] = {
                "implemented": 10,
                "terminal": 4,
                "terminal_bytes": 200,
                "effective_bytes": 25,
                "source_debt": 2,
            }
            state.update(
                {
                    "implemented_before": 10,
                    "terminal_before": 4,
                    "terminal_bytes_before": 200,
                    "source_debt_before": 2,
                }
            )
            campaigns.write_state(paths["state"], state)
            campaigns.mark_phase(
                paths["state"],
                "preflight",
                paths["started"] + timedelta(minutes=1, seconds=30),
            )
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                score_ceiling=90,
                ceiling_relative_score=83.33,
                model="The typed-table model.",
                artifact_sha256="a" * 64,
                effective=True,
            )
            build_artifact = root / "built.bin"
            build_artifact.write_bytes(b"built")
            actions = {
                "build": lambda: build_artifact,
                "code_report": lambda: paths["after"],
                "data_report": lambda: paths["after_data"],
                "source_scan": lambda: {
                    "metrics": {
                        "implemented": 11,
                        "terminal": 5,
                        "terminal_bytes": 300,
                        "effective_bytes": 75,
                        "source_debt": 1,
                    }
                },
                "validation": lambda: {"ok": True},
            }
            fixed = paths["started"] + timedelta(minutes=3)
            campaigns.finalize_campaign(
                paths["state"],
                "source",
                actions,
                cache_root=root / "cache",
                inputs={"tool": "v1"},
                clock=lambda: fixed,
            )
            arbitrary = root / "arbitrary.json"
            arbitrary.write_text("not a report", encoding="utf-8")
            item = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                arbitrary,
                arbitrary,
                "source",
                now=paths["started"] + timedelta(minutes=5),
            )
            self.assertEqual(item["schema_version"], 3)
            self.assertEqual(item["implemented_before"], 10)
            self.assertEqual(item["implemented_after"], 11)
            self.assertEqual(item["terminal_before"], 4)
            self.assertEqual(item["terminal_after"], 5)
            self.assertEqual(item["source_debt_before"], 2)
            self.assertEqual(item["source_debt_after"], 1)
            self.assertEqual(item["first_score_model"], "The typed-table model.")
            self.assertTrue(item["first_score_effective"])
            for name in (
                "selection",
                "baseline",
                "preflight",
                "first-score",
                "validation",
                "record",
            ):
                self.assertIn(name, item["phase_timestamps"])

    def test_v3_record_recovers_after_tracked_ledger_write_and_unlink_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            ledger = root / "tools/Resources/campaign-ledger.jsonl"
            ledger.parent.mkdir(parents=True)
            ledger.write_text("", encoding="utf-8")
            subprocess.run(["git", "add", str(ledger.relative_to(root))], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Campaign Tests",
                    "-c",
                    "user.email=campaign-tests@example.invalid",
                    "commit",
                    "-q",
                    "-m",
                    "Track ledger",
                ],
                cwd=root,
                check=True,
            )
            paths = self.make_measured_campaign(root, legacy=False)
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            build_artifact = root / "built.bin"
            build_artifact.write_bytes(b"built")
            actions = {
                "build": lambda: build_artifact,
                "code_report": lambda: paths["after"],
                "data_report": lambda: paths["after_data"],
                "source_scan": lambda: {"metrics": {}},
                "validation": lambda: {"ok": True},
            }
            fixed = paths["started"] + timedelta(minutes=3)
            campaigns.finalize_campaign(
                paths["state"],
                "source",
                actions,
                cache_root=root / "cache",
                clock=lambda: fixed,
            )
            original_unlink = Path.unlink

            def fail_state_unlink(path: Path, *args, **kwargs):
                if path == paths["state"]:
                    raise OSError("state is busy")
                return original_unlink(path, *args, **kwargs)

            arguments = (
                ledger,
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "source",
            )
            with patch.object(Path, "unlink", fail_state_unlink):
                with self.assertRaisesRegex(ValueError, "finalization is incomplete"):
                    campaigns.record_campaign(
                        *arguments,
                        now=paths["started"] + timedelta(minutes=5),
                    )
            self.assertEqual(len(campaigns._read_records(ledger)), 1)
            item = campaigns.record_campaign(*arguments)
            self.assertEqual(item["campaign_id"], campaigns._read_records(ledger)[0]["campaign_id"])
            self.assertFalse(paths["state"].exists())

    def test_v3_pending_record_uses_immutable_finalizer_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            build_artifact = root / "built.bin"
            build_artifact.write_bytes(b"built")
            actions = {
                "build": lambda: build_artifact,
                "code_report": lambda: paths["after"],
                "data_report": lambda: paths["after_data"],
                "source_scan": lambda: {"metrics": {}},
                "validation": lambda: {"ok": True},
            }
            fixed = paths["started"] + timedelta(minutes=3)
            campaigns.finalize_campaign(
                paths["state"],
                "source",
                actions,
                cache_root=root / "cache",
                clock=lambda: fixed,
            )
            arguments = (
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "source",
            )
            with patch.object(
                campaigns, "append_record", side_effect=OSError("ledger unavailable")
            ):
                with self.assertRaisesRegex(ValueError, "finalization is incomplete"):
                    campaigns.record_campaign(
                        *arguments,
                        now=paths["started"] + timedelta(minutes=5),
                    )
            self.write_code_report(paths["after"], 0.8)
            item = campaigns.record_campaign(*arguments)
            self.assertEqual(item["effective_bytes"], 50)

    def test_v3_record_rejects_a_missing_finalization_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory), legacy=False)
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256="a" * 64,
            )
            with self.assertRaisesRegex(ValueError, "finalize the schema-v3"):
                campaigns.record_campaign(
                    paths["ledger"],
                    paths["state"],
                    paths["models"],
                    paths["after"],
                    paths["after_data"],
                    "source",
                    now=paths["started"] + timedelta(minutes=3),
                )

    def test_finalized_campaign_state_is_immutable(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory), legacy=False)
            state = campaigns.read_state(paths["state"])
            state["phase"] = "finalized"
            campaigns.write_state(paths["state"], state)
            with self.assertRaisesRegex(ValueError, "immutable"):
                campaigns.mark_phase(paths["state"], "preflight")
            with self.assertRaisesRegex(ValueError, "immutable"):
                campaigns.mark_first_score(
                    paths["state"], "0x00401000"
                )
            with self.assertRaisesRegex(ValueError, "immutable"):
                campaigns.add_target(paths["state"], "0x00402000")

    def test_finalize_step_phase_blocks_concurrent_campaign_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            state_path = Path(directory) / "state.json"
            campaigns.write_state(
                state_path,
                {
                    "schema_version": 3,
                    "campaign_id": "campaign-1",
                    "phase": "finalize_build",
                    "started_at": "2026-08-03T12:00:00+00:00",
                    "finalization": {"result": "source"},
                },
            )
            before = state_path.read_bytes()
            with self.assertRaisesRegex(ValueError, "finalization owns"):
                campaigns.mark_phase(
                    state_path,
                    "editing",
                    datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
                )
            self.assertEqual(state_path.read_bytes(), before)

    def test_no_source_finalization_reuses_baseline_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, legacy=False)
            calls = {name: 0 for name in campaigns.FINALIZE_STEPS}

            def action(name):
                def run():
                    calls[name] += 1
                    return {"ok": True}

                return run

            receipt = campaigns.finalize_campaign(
                paths["state"],
                "no-source",
                {name: action(name) for name in campaigns.FINALIZE_STEPS},
                cache_root=root / "cache",
                inputs={"tool": "v1"},
                clock=lambda: paths["started"] + timedelta(minutes=2),
            )
            self.assertTrue(receipt["baseline_reused"])
            self.assertEqual(calls["build"], 0)
            self.assertEqual(calls["code_report"], 0)
            self.assertEqual(calls["data_report"], 0)
            self.assertEqual(calls["source_scan"], 1)
            self.assertEqual(calls["validation"], 1)

    def test_new_function_no_source_delivery_does_not_require_review(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch(
                "tools.decomp_provenance.reccmp_user_identity",
                return_value={"retail_sha256": "0" * 64},
            ):
                paths = self.make_measured_campaign(
                    root, legacy=False, impact_review=True
                )
            self.write_code_report(paths["after"], 0.25)
            self.write_data_report(paths["after_data"], 40)
            campaigns.finalize_campaign(
                paths["state"],
                "no-source",
                {},
                cache_root=root / "build/decomp-cache/finalize",
                inputs={"fixture": "no-source-impact-policy"},
                clock=lambda: paths["started"] + timedelta(minutes=2),
            )
            campaign = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                paths["after"],
                paths["after_data"],
                "no-source",
                models=["The trial source model was not supported."],
                now=paths["started"] + timedelta(minutes=3),
            )
            self.assertFalse(campaign["impact_review_required"])
            campaigns.record_delivery(
                paths["ledger"],
                campaign["campaign_id"],
                "staged",
                root=root,
                now=paths["started"] + timedelta(minutes=4),
            )
            accepted = campaigns.record_delivery(
                paths["ledger"],
                campaign["campaign_id"],
                "accepted",
                root=root,
                now=paths["started"] + timedelta(minutes=5),
            )
            self.assertNotIn("accepted_review", accepted)

    def test_meta_baseline_and_finalizer_do_not_require_build_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            state_path = root / "state.json"
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state_path,
                "meta",
                [],
                "Workflow",
                started,
                worktree_root=root,
            )
            campaigns.attach_meta_baseline(
                state_path,
                started + timedelta(minutes=1),
                {"implemented": 10, "terminal": 4},
            )
            calls = {"source_scan": 0, "validation": 0}

            def action(name):
                def run():
                    calls[name] += 1
                    return {"metrics": {}} if name == "source_scan" else {"ok": True}

                return run

            with (
                patch.object(campaigns, "_ensure_staged_reproducible"),
                patch.object(
                    campaigns,
                    "_staged_paths",
                    return_value=["tools/decomp_campaigns.py"],
                ),
                patch.object(
                    campaigns,
                    "_standard_meta_actions",
                    return_value={name: action(name) for name in calls},
                ),
            ):
                receipt = campaigns.finalize_standard(
                    state_path,
                    "meta-fix",
                    mode="meta",
                    staged=True,
                    cache_root=root / "cache",
                    clock=lambda: started + timedelta(minutes=2),
                )
            self.assertEqual(calls, {"source_scan": 1, "validation": 1})
            self.assertEqual(
                set(receipt["step_results"]), {"source_scan", "validation"}
            )

    def test_meta_finalization_rejects_every_staged_path_outside_workflow_scope(self):
        for invalid_path in (
            "README.md",
            "tools/Resources/functions_map.txt",
            "unrelated.txt",
        ):
            with self.subTest(path=invalid_path), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                subprocess.run(["git", "init", "-q", root], check=True)
                workflow = root / "tools/decomp_campaigns.py"
                workflow.parent.mkdir(parents=True)
                workflow.write_text("before\n", encoding="utf-8")
                existing = root / invalid_path
                if invalid_path != "unrelated.txt":
                    existing.parent.mkdir(parents=True, exist_ok=True)
                    existing.write_text("before\n", encoding="utf-8")
                subprocess.run(["git", "add", "."], cwd=root, check=True)
                subprocess.run(
                    [
                        "git",
                        "-c",
                        "user.name=Meta Tests",
                        "-c",
                        "user.email=meta@example.invalid",
                        "commit",
                        "-qm",
                        "Base",
                    ],
                    cwd=root,
                    check=True,
                )
                state_path = root / "build/state.json"
                started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
                campaigns.start_campaign(
                    state_path,
                    "meta",
                    [],
                    "Workflow",
                    started,
                    worktree_root=root,
                )
                campaigns.attach_meta_baseline(
                    state_path, started + timedelta(minutes=1), {}
                )
                workflow.write_text("after\n", encoding="utf-8")
                existing.parent.mkdir(parents=True, exist_ok=True)
                existing.write_text("after\n", encoding="utf-8")
                subprocess.run(
                    ["git", "add", "tools/decomp_campaigns.py", invalid_path],
                    cwd=root,
                    check=True,
                )
                with self.assertRaisesRegex(ValueError, "outside workflow scope"):
                    campaigns.finalize_standard(
                        state_path,
                        "meta-fix",
                        mode="meta",
                        staged=True,
                        cache_root=root / "cache",
                    )
                self.assertTrue(state_path.is_file())

    def test_meta_workflow_scope_accepts_only_the_root_roadmap(self):
        self.assertTrue(campaigns._meta_workflow_path("ROADMAP.md"))
        self.assertTrue(campaigns._meta_workflow_path("docs/decomp-agent.md"))
        self.assertFalse(campaigns._meta_workflow_path("docs/ROADMAP.md"))
        self.assertFalse(campaigns._meta_workflow_path("docs/decomp-worker.md"))
        self.assertFalse(campaigns._meta_workflow_path("README.md"))

    def test_schema_two_baseline_ledger_is_not_an_unstaged_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            ledger = root / "tools/Resources/campaign-ledger.jsonl"
            ledger.parent.mkdir(parents=True)
            ledger.write_text("committed\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Meta Tests",
                    "-c",
                    "user.email=meta@example.invalid",
                    "commit",
                    "-qm",
                    "Base",
                ],
                cwd=root,
                check=True,
            )
            ledger.write_text("committed\nbaseline abort\n", encoding="utf-8")
            state = {
                "schema_version": 2,
                "repository_worktree": campaigns.repository_worktree_snapshot(root),
            }

            campaigns._ensure_staged_reproducible(state, root)

            ledger.write_text(
                "committed\nbaseline abort\nlate change\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "stage or restore"):
                campaigns._ensure_staged_reproducible(state, root)

    def test_start_rejects_a_tracked_source_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            object_id = subprocess.run(
                ["git", "hash-object", "-w", "--stdin"],
                cwd=root,
                check=True,
                input=b"../../outside.cpp",
                capture_output=True,
            ).stdout.decode().strip()
            subprocess.run(
                [
                    "git",
                    "update-index",
                    "--add",
                    "--cacheinfo",
                    "120000",
                    object_id,
                    "src/Probe.cpp",
                ],
                cwd=root,
                check=True,
            )

            with self.assertRaisesRegex(ValueError, "symbolic links"):
                campaigns.start_campaign(
                    root / "state.json",
                    "meta",
                    [],
                    "Workflow",
                    worktree_root=root,
                    lane="meta",
                )
            self.assertFalse((root / "state.json").exists())

    def test_source_finalization_checks_the_build_graph_without_a_manifest_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root)
            source = root / "src/Targets.cpp"
            source.write_text(
                source.read_text(encoding="utf-8") + "int source_change;\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "src/Targets.cpp"], cwd=root, check=True)
            rogue = root / "build/Rogue.cpp"
            rogue.parent.mkdir(parents=True, exist_ok=True)
            rogue.write_text("int rogue_source;\n", encoding="utf-8")
            ignored = subprocess.run(
                ["git", "check-ignore", "-q", "build/Rogue.cpp"],
                cwd=root,
                check=False,
            )
            self.assertEqual(ignored.returncode, 0)

            with (
                patch.object(
                    campaigns,
                    "_configured_build_sources",
                    return_value=[{"path": rogue.absolute(), "generated": False}],
                ),
                self.assertRaisesRegex(ValueError, "absent from the staged Git tree"),
            ):
                campaigns.finalize_standard(
                    paths["state"],
                    "source",
                    mode="refinement",
                    targets=["0x00401000"],
                    staged=True,
                    cache_root=root / "cache",
                )

    def test_finalization_rejects_a_changed_brief_or_doctor_receipt(self):
        for changed_artifact in ("brief", "doctor", "evidence", "scout", "dwarf"):
            with self.subTest(changed_artifact=changed_artifact), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
                doctor_path = self.write_doctor_receipt(root, "refinement", started)
                doctor_document = json.loads(doctor_path.read_text(encoding="utf-8"))
                doctor_identity = {
                    "path": str(doctor_path.resolve()),
                    "sha256": campaigns.file_hash(doctor_path),
                    "receipt_id": doctor_document["receipt_id"],
                    "mode": "refinement",
                    "lane": "production",
                }
                scout_reports = []
                scout_paths = []
                for index in range(2):
                    scout_path = root / f"scout-{index}.json"
                    scout_content = {"scout": index}
                    scout_path.write_text(json.dumps(scout_content), encoding="utf-8")
                    scout_paths.append(scout_path)
                    scout_reports.append(
                        {
                            "path": str(scout_path.resolve()),
                            "sha256": campaigns.file_hash(scout_path),
                            "content": scout_content,
                        }
                    )
                dwarf_path = root / "woc-dwarf.txt"
                dwarf_path.write_text("DWARF evidence\n", encoding="utf-8")
                dwarf_identity = {
                    "path": str(dwarf_path),
                    "sha256": campaigns.file_hash(dwarf_path),
                }
                brief_path = root / "brief.json"
                brief_document = {
                    "schema": 1,
                    "cache_key": "test-brief",
                    "inputs": {
                        "tool_hash": __import__(
                            "tools.decomp_brief", fromlist=["_combined_hash"]
                        )._combined_hash(
                            root,
                            __import__(
                                "tools.decomp_brief", fromlist=["TOOL_INPUTS"]
                            ).TOOL_INPUTS,
                        ),
                        "dwarf_input": dwarf_identity,
                        "doctor_receipt": doctor_identity,
                        "scout_reports": scout_reports,
                    },
                    "lane": "production",
                    "target": "0x00401000",
                    "subsystem": "Test",
                    "roles": {"scouts": []},
                    "evidence": {"readiness": True},
                }
                brief_document["content_sha256"] = campaigns._snapshot_hash(
                    brief_document
                )
                brief_path.write_text(
                    json.dumps(brief_document, sort_keys=True), encoding="utf-8"
                )
                brief_identity = {
                    "path": str(brief_path.resolve()),
                    "sha256": campaigns.file_hash(brief_path),
                    "cache_key": brief_document["cache_key"],
                    "lane": "production",
                    "target": "0x00401000",
                    "subsystem": "Test",
                    "head": doctor_document["head"],
                    "content_sha256": brief_document["content_sha256"],
                    "dwarf_input": dwarf_identity,
                    "doctor_receipt": doctor_identity,
                    "scout_reports": scout_reports,
                }
                state_path = root / "state.json"
                with patch(
                    "tools.decomp_brief.validate_brief",
                    return_value=brief_identity,
                ):
                    campaigns.start_campaign(
                        state_path,
                        "refinement",
                        ["0x00401000"],
                        "Test",
                        started,
                        worktree_root=root,
                        doctor_receipt_path=doctor_path,
                        brief_paths=[brief_path],
                        require_brief=True,
                    )

                if changed_artifact == "brief":
                    changed_path = brief_path
                elif changed_artifact == "doctor":
                    changed_path = doctor_path
                elif changed_artifact == "scout":
                    changed_path = scout_paths[0]
                elif changed_artifact == "dwarf":
                    changed_path = dwarf_path
                else:
                    disassembly_check = next(
                        check
                        for check in doctor_document["checks"]
                        if check["name"] == "disassembly"
                    )
                    changed_path = root / disassembly_check["data"]["artifact"]["path"]
                changed_path.write_text(
                    changed_path.read_text(encoding="utf-8") + "\n",
                    encoding="utf-8",
                )
                with self.assertRaisesRegex(
                    ValueError,
                    "brief changed|doctor receipt changed|evidence artifact changed|scout report changed|DWARF input changed",
                ):
                    campaigns.finalize_campaign(
                        state_path,
                        "source",
                        {},
                        cache_root=root / "cache",
                        clock=lambda: started + timedelta(minutes=2),
                    )

    def test_standard_finalize_commands_support_posix_and_windows_builds(self):
        posix = campaigns.standard_finalize_commands(
            Path("."), platform_name="posix"
        )
        windows = campaigns.standard_finalize_commands(
            Path("."), platform_name="nt"
        )
        self.assertIn("-j4", posix["build"][0])
        self.assertNotIn("-j4", windows["build"][0])
        self.assertIn("-NOLOGO", windows["build"][0])
        self.assertEqual(
            sum(
                command[0] == "reccmp-reccmp"
                for commands in posix.values()
                for command in commands
            ),
            1,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build").mkdir()
            (root / "build" / "Makefile").write_text("all:\n", encoding="utf-8")
            actions = campaigns._standard_finalize_actions(
                {
                    "source_worktree_root": str(root),
                    "baseline_report": str(root / "baseline.json"),
                    "baseline_data_report": str(root / "baseline-data.json"),
                },
                mode="refinement",
                targets=["0x00401000"],
                resource=None,
                staged=True,
            )
            self.assertEqual(set(actions), set(campaigns.FINALIZE_STEPS))

    def test_impact_producer_changes_update_finalize_input_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tools = root / "tools"
            tools.mkdir()
            for name in (
                "decomp_impact.py",
                "decomp_annotations.py",
                "decomp_dependencies.py",
                "decomp_binary.py",
            ):
                (tools / name).write_text(f"{name} v1\n", encoding="utf-8")

            standard_before = campaigns._standard_input_hashes(root)
            meta_before = campaigns._meta_input_hashes(root)
            (tools / "decomp_dependencies.py").write_text(
                "decomp_dependencies.py v2\n", encoding="utf-8"
            )
            standard_after = campaigns._standard_input_hashes(root)
            meta_after = campaigns._meta_input_hashes(root)

            self.assertNotEqual(standard_before, standard_after)
            self.assertNotEqual(meta_before, meta_after)
            for name in (
                "decomp_impact.py",
                "decomp_annotations.py",
                "decomp_dependencies.py",
                "decomp_binary.py",
            ):
                path = str((tools / name).resolve())
                self.assertIn(path, standard_after["files"])
                self.assertIn(path, meta_after["files"])

    def test_reviewed_source_scan_freezes_impact_and_baseline_evidence(self):
        from tools.decomp_provenance import provenance_path

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            (build / "Makefile").write_text("all:\n", encoding="utf-8")
            baseline = build / "decomp-baseline-report.json"
            baseline_data = build / "decomp-baseline-data-report.json"
            current = build / "decomp-current-report.json"
            current_data = build / "decomp-current-data-report.json"
            for path in (baseline, baseline_data, current, current_data):
                path.write_text("{}\n", encoding="utf-8")
                provenance_path(path).write_text("{}\n", encoding="utf-8")
            impact_path = build / "decomp-cache/impact/campaign/pack.json"
            impact_path.parent.mkdir(parents=True)
            impact_path.write_text("{}\n", encoding="utf-8")
            state = {
                "source_worktree_root": str(root),
                "baseline_report": str(baseline),
                "baseline_data_report": str(baseline_data),
                "impact_review_required": True,
            }
            scan_context = {
                "debt": {},
                "lint_change": object(),
                "new_errors": [],
                "new_warnings": [],
                "stale": [],
            }
            with (
                patch(
                    "tools.decomp_provenance.validate_report",
                    return_value={"input_identity": {"build": "same"}},
                ),
                patch.object(
                    campaigns,
                    "_standard_source_scan",
                    return_value=({"ok": True}, scan_context),
                ),
                patch(
                    "tools.decomp_impact.build_impact_pack",
                    return_value={"kind": "test-impact"},
                ) as build_pack,
                patch(
                    "tools.decomp_impact.write_impact_pack",
                    return_value=impact_path,
                ),
            ):
                result = campaigns._standard_finalize_actions(
                    state,
                    mode="refinement",
                    targets=["0x00401000"],
                    resource=None,
                    staged=True,
                )["source_scan"]()

            self.assertEqual(
                result["artifacts"],
                [
                    impact_path,
                    baseline,
                    provenance_path(baseline),
                    baseline_data,
                    provenance_path(baseline_data),
                ],
            )
            build_pack.assert_called_once_with(
                state=state,
                baseline_report=baseline,
                current_report=current,
                root=root,
            )

    def test_score_artifact_metadata_cannot_be_overridden(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "report.json"
            function_map = root / "functions.txt"
            function_sizes = root / "sizes.json"
            source_root = root / "src"
            source_root.mkdir()
            self.write_code_report(report, 0.75)
            function_map.write_text(
                "0x00401000 Target\n0x00401100 Next\n", encoding="utf-8"
            )
            function_sizes.write_text(
                '[{"address":"00401000","size":256}]', encoding="utf-8"
            )
            with patch("tools.decomp_provenance.validate_report"):
                metadata = campaigns.score_artifact_metadata(
                    "0x00401000",
                    report=report,
                    functions_map=function_map,
                    function_sizes=function_sizes,
                    source_root=source_root,
                )
            self.assertEqual(metadata["raw_score"], 75)
            merged = campaigns._first_score_metadata(
                metadata,
                raw_score=75,
                score_ceiling=metadata["score_ceiling"],
                ceiling_relative_score=metadata["ceiling_relative_score"],
                artifact_sha256=metadata["artifact_sha256"],
                effective=False,
            )
            self.assertEqual(merged, metadata)
            with self.assertRaisesRegex(ValueError, "raw-score disagrees"):
                campaigns._first_score_metadata(
                    metadata,
                    raw_score=20,
                    score_ceiling=None,
                    ceiling_relative_score=None,
                    artifact_sha256=None,
                    effective=False,
                )
            with self.assertRaisesRegex(ValueError, "effective disagrees"):
                campaigns._first_score_metadata(
                    metadata,
                    raw_score=None,
                    score_ceiling=None,
                    ceiling_relative_score=None,
                    artifact_sha256=None,
                    effective=True,
                )

    def test_score_artifact_metadata_rejects_an_unsealed_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "report.json"
            source_root = root / "src"
            source_root.mkdir()
            self.write_code_report(report, 0.75)
            with self.assertRaisesRegex(ValueError, "comparison provenance"):
                campaigns.score_artifact_metadata(
                    "0x00401000",
                    report=report,
                    source_root=source_root,
                )

    def test_delivery_records_follow_receipt_bound_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = root / "ledger.jsonl"
            delivery_path = root / "delivery.json"
            receipt_hash = "f" * 64
            delivery_hash = "d" * 64
            campaigns.write_records(
                ledger,
                [
                    {
                        "schema_version": 3,
                        "record_type": "campaign",
                        "campaign_id": "campaign-1",
                        "timestamp": "2026-08-03T12:00:00+00:00",
                        "ended_at": "2026-08-03T12:00:00+00:00",
                        "mode": "refinement",
                        "lane": "closure",
                        "addresses": ["0x00401000"],
                        "finalize_receipt": {
                            "content_sha256": receipt_hash,
                        },
                    }
                ],
            )
            self.bind_campaign_record_receipt(ledger)
            with self.assertRaisesRegex(ValueError, "must be staged"):
                campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "committed",
                    commit="a" * 40,
                    now=datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
                )
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "staged",
                now=datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
            )
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "accepted",
                now=datetime(2026, 8, 3, 12, 2, tzinfo=timezone.utc),
            )
            delivery_receipt = {
                "content_sha256": delivery_hash,
                "source_commit": "a" * 40,
                "base_commit": "b" * 40,
            }
            with (
                patch.object(
                    campaigns,
                    "_validated_delivery_receipt",
                    return_value=delivery_receipt,
                ),
                patch.object(
                    campaigns,
                    "_git_commit",
                    side_effect=lambda _root, value, _field: value,
                ),
                patch.object(campaigns, "_verify_remote_contains"),
            ):
                campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "integrated",
                    base_commit="b" * 40,
                    commit="a" * 40,
                    delivery_receipt_path=delivery_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 3, tzinfo=timezone.utc),
                )
                committed = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "committed",
                    commit="a" * 40,
                    delivery_receipt_path=delivery_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 4, tzinfo=timezone.utc),
                )
                pushed = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "pushed",
                    commit="a" * 40,
                    delivery_receipt_path=delivery_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 5, tzinfo=timezone.utc),
                )
            self.assertIn("commit", committed["phase_timestamps"])
            self.assertIn("push", pushed["phase_timestamps"])
            self.assertEqual(committed["receipt_sha256"], delivery_hash)
            with (
                patch.object(
                    campaigns,
                    "_validated_delivery_receipt",
                    return_value=delivery_receipt,
                ),
                patch.object(
                    campaigns,
                    "_git_commit",
                    side_effect=lambda _root, value, _field: value,
                ),
                patch.object(campaigns, "_verify_remote_contains"),
            ):
                repeated = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "pushed",
                    commit="a" * 40,
                    delivery_receipt_path=delivery_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 6, tzinfo=timezone.utc),
                )
            self.assertEqual(repeated["delivery_id"], pushed["delivery_id"])
            with self.assertRaisesRegex(ValueError, "already terminal"):
                campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "rejected",
                    now=datetime(2026, 8, 3, 12, 7, tzinfo=timezone.utc),
                )

    def test_new_source_delivery_requires_and_binds_independent_review(self):
        from tools import decomp_impact as impact
        from tools.tests.test_decomp_impact import (
            ImpactValidationTests,
            accept_template,
            json_text,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = root / "ledger.jsonl"
            campaigns.write_records(
                ledger,
                [
                    {
                        "schema_version": 3,
                        "record_type": "campaign",
                        "campaign_id": "campaign-1",
                        "timestamp": "2026-08-03T12:00:00+00:00",
                        "ended_at": "2026-08-03T12:00:00+00:00",
                        "mode": "refinement",
                        "lane": "production",
                        "addresses": ["0x00401000"],
                        "impact_review_required": True,
                        "finalize_receipt": {
                            "content_sha256": "b" * 64,
                        },
                    }
                ],
            )
            self.bind_campaign_record_receipt(ledger)
            finalize, _pack, _frozen = ImpactValidationTests().make_finalize(root)
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "staged",
                root=root,
                now=datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
            )
            with (
                patch.object(
                    campaigns,
                    "_campaign_finalize_receipt",
                    return_value=finalize,
                ),
                self.assertRaisesRegex(ValueError, "needs --review-report"),
            ):
                campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "accepted",
                    root=root,
                    now=datetime(2026, 8, 3, 12, 2, tzinfo=timezone.utc),
                )

            report = impact.make_review_template(finalize, "acceptance-reviewer")
            accept_template(report)
            report["content_sha256"] = impact._document_hash(report)
            report_path = root / "review.json"
            report_path.write_text(json_text(report), encoding="utf-8")
            with patch.object(
                campaigns,
                "_campaign_finalize_receipt",
                return_value=finalize,
            ):
                accepted = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "accepted",
                    review_report_path=report_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 2, tzinfo=timezone.utc),
                )
                repeated = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "accepted",
                    review_report_path=report_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 3, tzinfo=timezone.utc),
                )
                repeated_without_report = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "accepted",
                    root=root,
                    now=datetime(2026, 8, 3, 12, 3, tzinfo=timezone.utc),
                )
                report_path.write_text(json.dumps(report), encoding="utf-8")
                whitespace_retry = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "accepted",
                    review_report_path=report_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 3, tzinfo=timezone.utc),
                )
            self.assertEqual(repeated, accepted)
            self.assertEqual(repeated_without_report, accepted)
            self.assertEqual(whitespace_retry, accepted)
            self.assertEqual(
                accepted["accepted_claims"], list(impact.PROMOTION_CLAIMS)
            )
            self.assertEqual(
                accepted["accepted_review"]["reviewer_id"],
                "acceptance-reviewer",
            )

            changed = dict(report)
            changed["reviewer_id"] = "different-reviewer"
            changed["content_sha256"] = impact._document_hash(changed)
            report_path.write_text(json_text(changed), encoding="utf-8")
            with (
                patch.object(
                    campaigns,
                    "_campaign_finalize_receipt",
                    return_value=finalize,
                ),
                self.assertRaisesRegex(ValueError, "existing delivery"),
            ):
                campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "accepted",
                    review_report_path=report_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 4, tzinfo=timezone.utc),
                )

    def test_delivery_receipt_revalidates_the_accepted_review(self):
        from tools import decomp_impact as impact
        from tools.tests.test_decomp_impact import (
            ImpactValidationTests,
            accept_template,
            json_text,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            commit_command = [
                "git",
                "-c",
                "user.name=Delivery Tests",
                "-c",
                "user.email=delivery@example.invalid",
                "commit",
                "-qm",
            ]
            subprocess.run(
                [*commit_command, "Base", "--allow-empty"], cwd=root, check=True
            )
            base = campaigns._git_head(root)
            ledger = root / "tools/Resources/campaign-ledger.jsonl"
            campaigns.write_records(
                ledger,
                [
                    {
                        "schema_version": 3,
                        "record_type": "campaign",
                        "campaign_id": "campaign-1",
                        "timestamp": "2026-08-03T12:00:00+00:00",
                        "ended_at": "2026-08-03T12:00:00+00:00",
                        "mode": "refinement",
                        "lane": "production",
                        "addresses": ["0x00401000"],
                        "impact_review_required": True,
                        "finalize_receipt": {
                            "content_sha256": "b" * 64,
                        },
                    }
                ],
            )
            campaign = self.bind_campaign_record_receipt(ledger)
            finalize, _pack, _frozen = ImpactValidationTests().make_finalize(root)
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "staged",
                root=root,
                now=datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
            )
            report = impact.make_review_template(finalize, "acceptance-reviewer")
            accept_template(report)
            report["content_sha256"] = impact._document_hash(report)
            report_path = root / "review.json"
            report_path.write_text(json_text(report), encoding="utf-8")
            with patch.object(
                campaigns,
                "_campaign_finalize_receipt",
                return_value=finalize,
            ):
                accepted = campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "accepted",
                    review_report_path=report_path,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 2, tzinfo=timezone.utc),
                )
            subprocess.run(["git", "add", "-f", "tools"], cwd=root, check=True)
            subprocess.run([*commit_command, "Campaign"], cwd=root, check=True)
            head = campaigns._git_head(root)
            validation = {"commands": [], "inputs": {}, "artifacts": []}
            with (
                patch.object(
                    campaigns,
                    "_campaign_finalize_receipt",
                    return_value=finalize,
                ),
                patch.object(campaigns, "_verify_campaign_in_commit"),
                patch.object(
                    campaigns, "_verify_finalized_campaign_tree", return_value=[]
                ),
                patch.object(campaigns, "_delivery_reference", return_value={}),
                patch.object(
                    campaigns, "_run_delivery_validation", return_value=validation
                ),
                patch.object(campaigns, "_validate_delivery_command_results"),
                patch(
                    "tools.decomp_provenance.validation_tool_identity",
                    return_value={},
                ),
            ):
                receipt = campaigns.create_delivery_receipt(
                    ledger,
                    "campaign-1",
                    head,
                    base,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 3, tzinfo=timezone.utc),
                )
            self.assertEqual(receipt["accepted_review"], accepted["accepted_review"])

            receipt_path = Path(str(receipt["path"]))
            with (
                patch.object(
                    campaigns,
                    "_campaign_finalize_receipt",
                    return_value=finalize,
                ),
                patch.object(campaigns, "_verify_campaign_in_commit"),
                patch.object(
                    campaigns, "_verify_finalized_campaign_tree", return_value=[]
                ),
                patch.object(campaigns, "_standard_input_hashes", return_value={}),
                patch.object(campaigns, "_validate_delivery_command_results"),
                patch(
                    "tools.decomp_provenance.validation_tool_identity",
                    return_value={},
                ),
            ):
                campaigns._validated_delivery_receipt(
                    receipt_path, campaign, ledger, root=root
                )

            accepted_path = Path(str(accepted["accepted_review"]["path"]))
            accepted_path.write_text("{}\n", encoding="utf-8")
            with (
                patch.object(
                    campaigns,
                    "_campaign_finalize_receipt",
                    return_value=finalize,
                ),
                patch.object(campaigns, "_verify_campaign_in_commit"),
                self.assertRaisesRegex(ValueError, "accepted impact review"),
            ):
                campaigns._validated_delivery_receipt(
                    receipt_path, campaign, ledger, root=root
                )

    def test_delivery_rejects_wrong_receipt_and_commit_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            ledger = Path(directory) / "ledger.jsonl"
            campaigns.write_records(
                ledger,
                [
                    {
                        "schema_version": 3,
                        "record_type": "campaign",
                        "campaign_id": "campaign-1",
                        "ended_at": "2026-08-03T12:00:00+00:00",
                        "mode": "refinement",
                        "lane": "closure",
                        "addresses": ["0x00401000"],
                        "finalize_receipt": {
                            "content_sha256": "f" * 64,
                        },
                    }
                ],
            )
            self.bind_campaign_record_receipt(ledger)
            with self.assertRaisesRegex(ValueError, "disagrees"):
                campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "staged",
                    receipt_sha256="e" * 64,
                    now=datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
                )
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "staged",
                now=datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
            )
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "accepted",
                now=datetime(2026, 8, 3, 12, 2, tzinfo=timezone.utc),
            )
            with self.assertRaisesRegex(ValueError, "delivery-receipt"):
                campaigns.record_delivery(
                    ledger,
                    "campaign-1",
                    "integrated",
                    now=datetime(2026, 8, 3, 12, 3, tzinfo=timezone.utc),
                )

    def test_delivery_rejects_an_integrated_source_symlink_before_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            commit = [
                "git",
                "-c",
                "user.name=Delivery Tests",
                "-c",
                "user.email=delivery@example.invalid",
                "commit",
                "-qm",
            ]
            subprocess.run([*commit, "Base", "--allow-empty"], cwd=root, check=True)
            base = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            object_id = subprocess.run(
                ["git", "hash-object", "-w", "--stdin"],
                cwd=root,
                check=True,
                input=b"../../outside.cpp",
                capture_output=True,
            ).stdout.decode().strip()
            subprocess.run(
                [
                    "git",
                    "update-index",
                    "--add",
                    "--cacheinfo",
                    "120000",
                    object_id,
                    "src/Probe.cpp",
                ],
                cwd=root,
                check=True,
            )
            subprocess.run([*commit, "Campaign"], cwd=root, check=True)
            head = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            campaign = {
                "record_type": "campaign",
                "campaign_id": "campaign-1",
                "mode": "refinement",
                "lane": "production",
                "result": "source",
                "addresses": ["0x00401000"],
            }
            with (
                patch.object(campaigns, "_read_records", return_value=[campaign]),
                patch.object(campaigns, "_validate_campaign_record_receipt"),
                patch.object(
                    campaigns, "_campaign_finalize_receipt", return_value={}
                ),
                patch.object(campaigns, "_verify_campaign_in_commit"),
                patch.object(
                    campaigns, "_verify_finalized_campaign_tree", return_value=[]
                ),
                patch.object(campaigns, "_delivery_relevant_changes", return_value=[]),
                patch.object(campaigns, "_run_delivery_validation") as validation,
                self.assertRaisesRegex(ValueError, "symbolic links"),
            ):
                campaigns.create_delivery_receipt(
                    root / "ledger.jsonl",
                    "campaign-1",
                    head,
                    base,
                    root=root,
                )
            validation.assert_not_called()

    def test_delivery_checks_integrated_configured_sources_before_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            (root / ".gitignore").write_text("build/\n", encoding="utf-8")
            subprocess.run(["git", "add", ".gitignore"], cwd=root, check=True)
            commit = [
                "git",
                "-c",
                "user.name=Delivery Tests",
                "-c",
                "user.email=delivery@example.invalid",
                "commit",
                "-qm",
            ]
            subprocess.run([*commit, "Base"], cwd=root, check=True)
            base = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            (root / "CMakeLists.txt").write_text(
                "add_executable(game build/Rogue.cpp)\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "CMakeLists.txt"], cwd=root, check=True)
            subprocess.run([*commit, "Campaign"], cwd=root, check=True)
            head = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            rogue = root / "build/Rogue.cpp"
            rogue.parent.mkdir(parents=True)
            rogue.write_text("int rogue_source;\n", encoding="utf-8")
            campaign = {
                "record_type": "campaign",
                "campaign_id": "campaign-1",
                "mode": "refinement",
                "lane": "production",
                "result": "source",
                "addresses": ["0x00401000"],
            }
            with (
                patch.object(campaigns, "_read_records", return_value=[campaign]),
                patch.object(campaigns, "_validate_campaign_record_receipt"),
                patch.object(
                    campaigns, "_campaign_finalize_receipt", return_value={}
                ),
                patch.object(campaigns, "_verify_campaign_in_commit"),
                patch.object(
                    campaigns, "_verify_finalized_campaign_tree", return_value=[]
                ),
                patch.object(campaigns, "_delivery_relevant_changes", return_value=[]),
                patch.object(
                    campaigns,
                    "_configured_build_sources",
                    return_value=[{"path": rogue.absolute(), "generated": False}],
                ),
                patch.object(campaigns, "_run_delivery_validation") as validation,
                self.assertRaisesRegex(
                    ValueError, "absent from the delivery commit tree"
                ),
            ):
                campaigns.create_delivery_receipt(
                    root / "ledger.jsonl",
                    "campaign-1",
                    head,
                    base,
                    root=root,
                )
            validation.assert_not_called()

    def test_delivery_receipt_binds_a_real_head_base_and_committed_ledger(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            (root / "README.md").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "add", "README.md"], cwd=root, check=True)
            commit = [
                "git",
                "-c",
                "user.name=Delivery Tests",
                "-c",
                "user.email=delivery@example.invalid",
                "commit",
                "-qm",
            ]
            subprocess.run([*commit, "Base"], cwd=root, check=True)
            base = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            ledger = root / "tools/Resources/campaign-ledger.jsonl"
            campaigns.write_records(
                ledger,
                [
                    {
                        "schema_version": 3,
                        "record_type": "campaign",
                        "campaign_id": "campaign-1",
                        "timestamp": "2026-08-03T12:00:00+00:00",
                        "ended_at": "2026-08-03T12:00:00+00:00",
                        "mode": "meta",
                        "lane": "meta",
                        "result": "meta-fix",
                        "addresses": [],
                        "finalize_receipt": {"content_sha256": "f" * 64},
                    }
                ],
            )
            campaign = self.bind_campaign_record_receipt(ledger)
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "staged",
                now=datetime(2026, 8, 3, 12, 1, tzinfo=timezone.utc),
            )
            campaigns.record_delivery(
                ledger,
                "campaign-1",
                "accepted",
                now=datetime(2026, 8, 3, 12, 2, tzinfo=timezone.utc),
            )
            subprocess.run(["git", "add", "tools"], cwd=root, check=True)
            subprocess.run([*commit, "Campaign"], cwd=root, check=True)
            head = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            fake_finalize = {
                "key_payload": {},
                "receipt_sha256": "f" * 64,
            }
            delivery_cache = root / "build/decomp-cache/delivery"
            completed = subprocess.CompletedProcess([], 0, "", "")
            with patch.object(
                campaigns,
                "_standard_command",
                return_value=completed,
            ), patch.object(
                campaigns, "_verify_finalized_campaign_tree", return_value=[]
            ), patch.object(
                campaigns, "_campaign_finalize_receipt", return_value=fake_finalize
            ), patch.object(
                campaigns, "_verify_campaign_in_commit"
            ):
                receipt = campaigns.create_delivery_receipt(
                    ledger,
                    "campaign-1",
                    head,
                    base,
                    root=root,
                    now=datetime(2026, 8, 3, 12, 3, tzinfo=timezone.utc),
                )
            with patch.object(
                campaigns, "_verify_finalized_campaign_tree", return_value=[]
            ), patch.object(
                campaigns, "_verify_campaign_in_commit"
            ):
                validated = campaigns._validated_delivery_receipt(
                    Path(str(receipt["path"])),
                    campaign,
                    ledger,
                    root=root,
                )
            self.assertEqual(validated["source_commit"], head)

            receipt_path = Path(str(receipt["path"]))
            copied_path = root / "copied-delivery-receipt.json"
            copied_path.write_bytes(receipt_path.read_bytes())
            with self.assertRaisesRegex(ValueError, "canonical content-addressed"):
                campaigns._validated_delivery_receipt(
                    copied_path,
                    campaign,
                    ledger,
                    root=root,
                )

            forged = json.loads(receipt_path.read_text(encoding="utf-8"))
            forged["validation"]["commands"] = []
            forged["content_sha256"] = campaigns._delivery_receipt_hash(forged)
            forged_path = (
                delivery_cache
                / "campaign-1"
                / f"{forged['content_sha256']}.json"
            )
            forged_path.write_text(json.dumps(forged), encoding="utf-8")
            with patch.object(
                campaigns, "_verify_finalized_campaign_tree", return_value=[]
            ), patch.object(campaigns, "_verify_campaign_in_commit"):
                with self.assertRaisesRegex(ValueError, "command results"):
                    campaigns._validated_delivery_receipt(
                        forged_path,
                        campaign,
                        ledger,
                        root=root,
                    )

            with self.assertRaisesRegex(ValueError, "Git commit"):
                campaigns.create_delivery_receipt(
                    ledger,
                    "campaign-1",
                    "a" * 40,
                    base,
                    root=root,
                )

    def test_delivery_retry_uses_immutable_finalizer_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = self.make_measured_campaign(root, mode="data", legacy=False)
            build = root / "build"
            executable = build / "toy2.exe"
            symbols = build / "toy2.pdb"
            code_report = build / "decomp-current-report.json"
            data_report = build / "decomp-current-data-report.json"
            executable.write_bytes(b"finalized executable")
            symbols.write_bytes(b"finalized symbols")
            self.write_code_report(code_report, 0.75)
            self.write_data_report(data_report, 47)
            self.seal_report_pair(root, code_report, data_report)
            campaigns.mark_first_score(
                paths["state"],
                "0x00401000",
                paths["started"] + timedelta(minutes=2),
                raw_score=75,
                artifact_sha256=campaigns.file_hash(code_report),
            )
            from tools.decomp_provenance import provenance_path

            finalized = campaigns.finalize_campaign(
                paths["state"],
                "source",
                {
                    "build": lambda: {"artifacts": [executable, symbols]},
                    "code_report": lambda: {
                        "artifacts": [code_report, provenance_path(code_report)]
                    },
                    "data_report": lambda: {
                        "artifacts": [data_report, provenance_path(data_report)]
                    },
                    "source_scan": lambda: {"metrics": {}},
                    "validation": lambda: {"ok": True},
                },
                cache_root=root / "finalize-cache",
                inputs={"fixture": "delivery-retry"},
                clock=lambda: paths["started"] + timedelta(minutes=3),
            )
            campaign = campaigns.record_campaign(
                paths["ledger"],
                paths["state"],
                paths["models"],
                root / "unused-code-report.json",
                root / "unused-data-report.json",
                "source",
                now=paths["started"] + timedelta(minutes=4),
            )
            campaigns.record_delivery(
                paths["ledger"],
                campaign["campaign_id"],
                "staged",
                now=paths["started"] + timedelta(minutes=5),
            )
            campaigns.record_delivery(
                paths["ledger"],
                campaign["campaign_id"],
                "accepted",
                now=paths["started"] + timedelta(minutes=6),
            )
            base = campaigns._git_head(root)
            subprocess.run(
                ["git", "add", "-f", paths["ledger"].name],
                cwd=root,
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Delivery Tests",
                    "-c",
                    "user.email=delivery@example.invalid",
                    "commit",
                    "-qm",
                    "Campaign",
                ],
                cwd=root,
                check=True,
            )
            commit = campaigns._git_head(root)
            references: list[dict[str, object]] = []

            def integrated_validation(_campaign, _root, reference):
                references.append(reference)
                executable.write_bytes(b"integrated executable")
                symbols.write_bytes(b"integrated symbols")
                self.write_code_report(code_report, 0.9)
                self.write_data_report(data_report, 80)
                self.seal_report_pair(root, code_report, data_report)
                commands = campaigns._delivery_validation_command_plan(
                    _campaign, _root
                )
                return {
                    "commands": [
                        {
                            "command": command,
                            "returncode": 0,
                            "stdout_sha256": "0" * 64,
                            "stderr_sha256": "0" * 64,
                        }
                        for command in commands
                    ],
                    "inputs": {},
                    "artifacts": [],
                }

            verify_calls = 0

            def fail_once_after_validation(*_args, **_kwargs):
                nonlocal verify_calls
                verify_calls += 1
                if verify_calls == 2:
                    raise ValueError("late delivery failure")

            patches = (
                patch.object(
                    campaigns,
                    "_run_delivery_validation",
                    side_effect=integrated_validation,
                ),
                patch.object(
                    campaigns,
                    "_verify_campaign_in_commit",
                    side_effect=fail_once_after_validation,
                ),
                patch.object(
                    campaigns, "_verify_finalized_campaign_tree", return_value=[]
                ),
                patch.object(campaigns, "_delivery_relevant_changes", return_value=[]),
                patch.object(campaigns, "_configured_build_sources", return_value=[]),
                patch(
                    "tools.decomp_provenance.validation_tool_identity",
                    return_value={},
                ),
            )
            with (
                patches[0],
                patches[1],
                patches[2],
                patches[3],
                patches[4],
                patches[5],
            ):
                with self.assertRaisesRegex(ValueError, "late delivery failure"):
                    campaigns.create_delivery_receipt(
                        paths["ledger"],
                        campaign["campaign_id"],
                        commit,
                        base,
                        root=root,
                        cache_root=root / "delivery-cache",
                    )
                receipt = campaigns.create_delivery_receipt(
                    paths["ledger"],
                    campaign["campaign_id"],
                    commit,
                    base,
                    root=root,
                    cache_root=root / "delivery-cache",
                )

            self.assertEqual(receipt["status"], "passed")
            self.assertEqual(len(references), 2)
            for reference in references:
                self.assertEqual(
                    reference["code"]["0x00401000"]["matching"], 0.75
                )
                self.assertEqual(reference["initialized_bytes"], 47)
            immutable = {
                Path(item["source_path"]): Path(item["path"])
                for item in finalized["immutable_artifacts"]
            }
            self.assertEqual(immutable[executable.resolve()].read_bytes(), b"finalized executable")
            self.assertEqual(immutable[symbols.resolve()].read_bytes(), b"finalized symbols")
            self.assertEqual(executable.read_bytes(), b"integrated executable")
            self.assertEqual(symbols.read_bytes(), b"integrated symbols")

    def test_resource_delivery_reference_uses_the_finalized_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "original/toy2.exe"
            executable = root / "build/toy2.exe"
            original.parent.mkdir(parents=True)
            executable.parent.mkdir(parents=True)
            original.write_bytes(b"retail")
            executable.write_bytes(b"finalized A")
            cached = campaigns._cache_finalize_artifact(
                executable, root / "finalize-cache"
            )
            executable.write_bytes(b"integrated B")
            code_report = root / "code.json"
            data_report = root / "data.json"
            self.write_code_report(code_report, 0.75)
            self.write_data_report(data_report, 47)
            receipt = {
                "receipt_version": campaigns.FINALIZE_RECEIPT_VERSION,
                "immutable_artifacts": [cached],
            }
            campaign = {"mode": "resource", "result": "source"}
            observed: list[Path] = []

            def rows(_original: Path, rebuilt: Path):
                observed.append(rebuilt)
                values = [
                    {
                        "path": [2, 127, 2057],
                        "identity_match": True,
                        "size": 8,
                    }
                ]
                if rebuilt.read_bytes() == b"finalized A":
                    values.append(
                        {
                            "path": [3, 5, 1033],
                            "identity_match": True,
                            "size": 4,
                        }
                    )
                return values

            with patch.object(
                campaigns,
                "_receipt_step_artifact",
                side_effect=lambda _receipt, name: (
                    code_report if name == "code_report" else data_report
                ),
            ), patch.object(campaigns, "resource_rows", side_effect=rows):
                reference = campaigns._delivery_reference(receipt, campaign, root)
                current = campaigns._matching_resource_bytes(original, executable)

            self.assertEqual(observed[0], Path(cached["path"]))
            self.assertEqual(observed[1], executable)
            self.assertEqual(reference["resources"]["3,5,1033"], 4)
            with self.assertRaisesRegex(
                ValueError, "integrated resource 3,5,1033 regressed"
            ):
                campaigns._validate_resource_reference(
                    reference["resources"], current
                )

    def test_remote_delivery_check_refreshes_the_exact_tracking_ref(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            origin = parent / "origin.git"
            root = parent / "work"
            subprocess.run(["git", "init", "-q", "--bare", origin], check=True)
            subprocess.run(["git", "init", "-q", root], check=True)
            subprocess.run(
                ["git", "config", "user.name", "Delivery Tests"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                ["git", "config", "user.email", "delivery@example.invalid"],
                cwd=root,
                check=True,
            )
            (root / "file.txt").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "add", "file.txt"], cwd=root, check=True)
            subprocess.run(["git", "commit", "-qm", "Base"], cwd=root, check=True)
            subprocess.run(
                ["git", "branch", "-M", "agent/continuous"], cwd=root, check=True
            )
            subprocess.run(
                ["git", "remote", "add", "origin", str(origin)],
                cwd=root,
                check=True,
            )
            subprocess.run(
                ["git", "push", "-qu", "origin", "agent/continuous"],
                cwd=root,
                check=True,
            )
            base = campaigns._git_head(root)
            (root / "file.txt").write_text("delivery\n", encoding="utf-8")
            subprocess.run(["git", "commit", "-qam", "Delivery"], cwd=root, check=True)
            delivery = campaigns._git_head(root)
            subprocess.run(
                ["git", "push", "-q", "origin", "agent/continuous"],
                cwd=root,
                check=True,
            )
            campaigns._verify_remote_contains(root, delivery)

            subprocess.run(
                [
                    "git",
                    "--git-dir",
                    str(origin),
                    "update-ref",
                    "refs/heads/agent/continuous",
                    base,
                ],
                check=True,
            )
            with self.assertRaisesRegex(ValueError, "does not contain"):
                campaigns._verify_remote_contains(root, delivery)

            subprocess.run(
                ["git", "remote", "set-url", "origin", str(parent / "missing")],
                cwd=root,
                check=True,
            )
            with self.assertRaisesRegex(ValueError, "cannot refresh"):
                campaigns._verify_remote_contains(root, delivery)

    def test_delivery_rejects_same_path_upstream_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", root], check=True)
            commit = [
                "git",
                "-c",
                "user.name=Delivery Tests",
                "-c",
                "user.email=delivery@example.invalid",
                "commit",
                "-qam",
            ]
            source = root / "workflow.txt"
            source.write_text("first\nsecond\n", encoding="utf-8")
            subprocess.run(["git", "add", "workflow.txt"], cwd=root, check=True)
            subprocess.run([*commit, "Base"], cwd=root, check=True)
            base = campaigns._git_head(root)
            before = campaigns._git_tree_snapshot(root, base)
            campaign_content = b"first\nchanged by campaign\n"
            object_id = subprocess.run(
                ["git", "hash-object", "-w", "--stdin"],
                cwd=root,
                check=True,
                input=campaign_content,
                capture_output=True,
            ).stdout.decode().strip()
            after = dict(before)
            after["workflow.txt"] = [f"100644 {object_id} 0"]
            receipt = {
                "key_payload": {
                    "campaign_repository_index_snapshot": before,
                    "repository_index_snapshot": after,
                }
            }
            campaign = {"result": "meta-fix"}
            ledger = root / "tools/Resources/campaign-ledger.jsonl"

            source.write_bytes(campaign_content)
            subprocess.run([*commit, "Campaign"], cwd=root, check=True)
            campaign_commit = campaigns._git_head(root)
            self.assertEqual(
                campaigns._verify_finalized_campaign_tree(
                    root, ledger, campaign, campaign_commit, base, receipt=receipt
                ),
                [],
            )

            subprocess.run(
                ["git", "switch", "-q", "-c", "upstream", base],
                cwd=root,
                check=True,
            )
            source.write_text("upstream first\nsecond\n", encoding="utf-8")
            subprocess.run([*commit, "Upstream"], cwd=root, check=True)
            upstream = campaigns._git_head(root)
            source.write_text(
                "upstream first\nchanged by campaign\n", encoding="utf-8"
            )
            subprocess.run([*commit, "Resolved"], cwd=root, check=True)
            resolved = campaigns._git_head(root)
            with self.assertRaisesRegex(ValueError, "changed finalized campaign"):
                campaigns._verify_finalized_campaign_tree(
                    root,
                    ledger,
                    campaign,
                    resolved,
                    upstream,
                    receipt=receipt,
                )

    def test_typed_data_regression_check_covers_each_evidence_group(self):
        before = {
            "variables": {
                "variable_count": 2,
                "scored_bytes": 20,
                "explained_bytes": 20,
                "variables": [
                    {
                        "original_address": 0x501000,
                        "size": 10,
                        "matched_bytes": 10,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x502000,
                        "size": 10,
                        "matched_bytes": 10,
                        "score": 0.5,
                    },
                ],
            },
            "sections": {
                "scored_bytes": 40,
                "explained_bytes": 20,
                "sections": [
                    {
                        "name": ".data",
                        "size": 40,
                        "evidence_bytes": 20,
                        "explained_bytes": 20,
                        "score": 0.5,
                    }
                ]
            },
            "vtables": {
                "table_count": 2,
                "scored_bytes": 16,
                "explained_bytes": 8,
                "tables": [
                    {
                        "original_address": 0x510000,
                        "size": 8,
                        "matched_bytes": 8,
                        "score": 1.0,
                    },
                    {
                        "original_address": 0x510100,
                        "size": 8,
                        "matched_bytes": 0,
                        "score": 0.0,
                    },
                ],
            },
            "imports": {
                "entry_count": 4,
                "matched_entries": 4,
                "score": 1.0,
                "entries": [
                    {
                        "module": "KERNEL32.dll",
                        "name": "CreateFileA",
                        "ordinal": None,
                        "match": True,
                    },
                    {
                        "module": "KERNEL32.dll",
                        "name": "CloseHandle",
                        "ordinal": None,
                        "match": True,
                    },
                    {
                        "module": "USER32.dll",
                        "name": "MessageBoxA",
                        "ordinal": None,
                        "match": True,
                    },
                    {
                        "module": "WINMM.dll",
                        "name": "timeGetTime",
                        "ordinal": None,
                        "match": True,
                    },
                ],
            },
            "relocations": {
                "entry_count": 8,
                "mapped_entries": 7,
                "matched_entries": 6,
                "score": 0.75,
            },
            "debug": {
                "original_pdb": "C:\\retail\\toy2.pdb",
                "recompiled_pdb": "Z:\\build\\toy2.pdb",
            },
        }
        after = json.loads(json.dumps(before))
        after["variables"]["variables"][0]["matched_bytes"] = 9
        after["variables"]["variables"][1]["matched_bytes"] = 11
        problems = campaigns._data_regression_problems(before, after)
        self.assertIn("0x00501000: data bytes regressed", problems)

        masked_vtable = json.loads(json.dumps(before))
        masked_vtable["vtables"]["tables"][0]["matched_bytes"] = 4
        masked_vtable["vtables"]["tables"][0]["score"] = 0.5
        masked_vtable["vtables"]["tables"][1]["matched_bytes"] = 4
        masked_vtable["vtables"]["tables"][1]["score"] = 0.5
        self.assertIn(
            "0x00510000: vtable bytes regressed",
            campaigns._data_regression_problems(before, masked_vtable),
        )

        masked_import = json.loads(json.dumps(before))
        masked_import["imports"]["entries"][0]["match"] = False
        masked_import["imports"]["entries"].append(
            {
                "module": "ADVAPI32.dll",
                "name": "RegCloseKey",
                "ordinal": None,
                "match": True,
            }
        )
        self.assertIn(
            "kernel32.dll:CreateFileA: import match regressed",
            campaigns._data_regression_problems(before, masked_import),
        )

        lost_mapping = json.loads(json.dumps(before))
        lost_mapping["relocations"]["mapped_entries"] -= 1
        self.assertIn(
            "relocations mapped evidence regressed",
            campaigns._data_regression_problems(before, lost_mapping),
        )

        for group in ("imports", "relocations"):
            with self.subTest(score_group=group):
                lower_score = json.loads(json.dumps(before))
                lower_score[group]["score"] -= 0.1
                self.assertIn(
                    f"{group} score regressed",
                    campaigns._data_regression_problems(before, lower_score),
                )

        for group, key in (
            ("vtables", "explained_bytes"),
            ("imports", "matched_entries"),
            ("relocations", "matched_entries"),
        ):
            with self.subTest(group=group):
                changed = json.loads(json.dumps(before))
                changed[group][key] -= 1
                self.assertIn(
                    f"{group} data evidence regressed",
                    campaigns._data_regression_problems(before, changed),
                )
                missing = json.loads(json.dumps(before))
                del missing[group]
                self.assertIn(
                    f"current typed-data group {group} is missing",
                    campaigns._data_regression_problems(before, missing),
                )

        moved = json.loads(json.dumps(before))
        moved["debug"]["recompiled_pdb"] = "D:\\other\\toy2.pdb"
        self.assertEqual(campaigns._data_regression_problems(before, moved), [])
        changed = json.loads(json.dumps(before))
        changed["debug"]["original_pdb"] = "C:\\other\\toy2.pdb"
        self.assertIn(
            "original PDB identity changed",
            campaigns._data_regression_problems(before, changed),
        )

    def test_source_delivery_adds_typed_data_only_for_the_new_review_policy(self):
        legacy = {"mode": "refinement", "result": "source"}
        reviewed = {
            "mode": "refinement",
            "result": "source",
            "impact_review_required": True,
        }

        legacy_commands = campaigns._delivery_validation_command_plan(
            legacy, campaigns.ROOT
        )
        reviewed_commands = campaigns._delivery_validation_command_plan(
            reviewed, campaigns.ROOT
        )

        self.assertFalse(
            any(
                any("generate-decomp-data-report.py" in part for part in command)
                for command in legacy_commands
            )
        )
        self.assertTrue(
            any(
                any("generate-decomp-data-report.py" in part for part in command)
                for command in reviewed_commands
            )
        )


if __name__ == "__main__":
    unittest.main()
