from __future__ import annotations

import importlib.util
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


TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "toy2_decomp_campaigns", TOOLS / "decomp_campaigns.py"
)
assert spec is not None and spec.loader is not None
campaigns = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = campaigns
spec.loader.exec_module(campaigns)


class CampaignTests(unittest.TestCase):
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
            with patch.object(campaigns, "resource_rows", return_value=before):
                campaigns.attach_baseline(
                    state_path,
                    report,
                    data_report,
                    function_map,
                    sizes,
                    started + timedelta(minutes=1),
                )
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
                    }
                }
            ),
            encoding="utf-8",
        )

    def make_measured_campaign(
        self,
        root: Path,
        *,
        mode: str = "refinement",
        addresses: list[str] | None = None,
        family: bool = False,
        family_sizes: dict[int, int] | None = None,
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
        started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
        campaigns.start_campaign(
            state,
            mode,
            addresses or ["0x00401000"],
            "Test",
            started,
            worktree_root=root,
            sizes_path=function_sizes if family else None,
            family=family,
        )
        campaigns.attach_baseline(
            state,
            before,
            before_data,
            function_map,
            function_sizes,
            started + timedelta(minutes=1),
            source_root,
        )
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
                "mode": "coverage",
                "result": "no-source",
                "addresses": ["0x00401000"],
            },
            {
                "schema_version": 2,
                "record_type": "evidence",
                "addresses": ["0x00401000"],
                "evidence_kind": "analogue",
                "note": "A new analogue is available.",
            },
            {
                "mode": "coverage",
                "result": "no-source",
                "addresses": ["0x00401000"],
            },
        ]
        stats = campaigns.address_stats(records)[0x00401000]
        self.assertEqual(stats.attempts, 2)
        self.assertEqual(stats.zero_yield_attempts, 2)
        self.assertEqual(stats.penalty_attempts, 1)
        self.assertEqual(stats.evidence_events, 1)

    def test_add_target_records_a_pivot_and_rejects_a_duplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.make_measured_campaign(Path(directory))
            state = paths["state"]
            started = paths["started"]
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
            with self.assertRaisesRegex(ValueError, "already contains"):
                campaigns.add_target(state, "0x00402000")

    def test_add_target_needs_an_active_campaign(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "active campaign state does not exist"):
                campaigns.add_target(
                    Path(directory) / "missing.json", "0x00402000"
                )

    def test_evidence_reset_uses_timestamps_after_a_legacy_merge(self):
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
        self.assertEqual(stats.penalty_attempts, 0)

    def test_legacy_migration_preserves_duplicate_campaigns_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
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

    def test_summary_excludes_meta_and_imported_records_from_rate(self):
        records = [
            {
                "mode": "refinement",
                "result": "source",
                "addresses": ["0x00401000"],
                "minutes": 10,
                "effective_bytes": 100,
            },
            {
                "mode": "coverage",
                "result": "no-source",
                "addresses": ["0x00402000"],
                "minutes": 0,
            },
            {
                "mode": "meta",
                "result": "meta-fix",
                "addresses": [],
                "minutes": 20,
            },
        ]
        output = StringIO()
        with redirect_stdout(output):
            campaigns.print_summary(records, 0)
        text = output.getvalue()
        self.assertIn("Timed campaigns: 1 (1 source, 0 no-source)", text)
        self.assertIn("Retained rate: 10.00 bytes/minute", text)

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
            )
            second = campaigns.start_campaign(
                root / "second.json",
                "coverage",
                ["0x00401000"],
                "Test",
                started,
                worktree_root=root,
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
                )
            self.assertFalse(state.exists())

    def test_duplicate_evidence_replay_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            ledger = Path(directory) / "ledger.jsonl"
            when = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            first = campaigns.record_evidence(
                ledger,
                ["0x00401000"],
                "analogue",
                "A matching retail analogue is available.",
                when,
            )
            self.assertIn("evidence_id", first)
            with self.assertRaisesRegex(ValueError, "already exists"):
                campaigns.record_evidence(
                    ledger,
                    ["0x00401000"],
                    "analogue",
                    "A matching retail analogue is available.",
                    when + timedelta(minutes=1),
                )
            self.assertEqual(len(campaigns._read_records(ledger)), 1)

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
            )
            function_sizes.write_text(
                json.dumps([{"address": "00401000", "size": 250}]),
                encoding="utf-8",
            )
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
            self.assertIn("Extension deadline: 2026-08-03T12:30:00+00:00", text)
            self.assertIn("Expected retained bytes: 96.00", text)

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
            ledger = root / "ledger.jsonl"
            state = root / "state.json"
            started = datetime(2026, 8, 3, 12, 0, tzinfo=timezone.utc)
            campaigns.start_campaign(
                state, "coverage", ["0x00401000"], "Test", started
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
            self.assertEqual(campaigns._read_records(ledger), [item])


if __name__ == "__main__":
    unittest.main()
