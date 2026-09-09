from __future__ import annotations

from datetime import datetime, timedelta, timezone
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from contextlib import ExitStack


TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import decomp_experiment as experiment  # noqa: E402
from tools.decomp_status import MatchStatus  # noqa: E402


class ExperimentControllerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        function_map = self.root / "tools/Resources/functions_map.txt"
        function_map.parent.mkdir(parents=True)
        function_map.write_text(
            "0x00401000 Probe\n0x00401010 Other\n", encoding="utf-8"
        )
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "."], cwd=self.root, check=True)
        subprocess.run(
            [
                "git",
                "-c",
                "user.name=Experiment Tests",
                "-c",
                "user.email=experiment@example.invalid",
                "commit",
                "-qm",
                "Prepare experiment fixture",
            ],
            cwd=self.root,
            check=True,
        )
        self.head = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=self.root,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _session(self, *, max_trials: int = 3) -> tuple[Path, dict[str, object]]:
        address = "0x00401000"
        session_id = "20250101T000000Z-0123456789ab"
        directory = (
            self.root
            / "build/decomp-experiments"
            / address
            / "sessions"
            / session_id
        )
        directory.mkdir(parents=True)
        session = {
            "address": address,
            "session_id": session_id,
            "receipt_id": "session-receipt",
            "head": self.head,
            "campaign": None,
            "limits": {"max_trials": max_trials, "max_non_improving": 2},
            "function_map": {
                "sha256": experiment.file_hash(
                    self.root / "tools/Resources/functions_map.txt"
                )
            },
            "tool_identity": experiment._tool_identity(self.root),
        }
        return directory, session

    def _reserve(
        self,
        directory: Path,
        session: dict[str, object],
        label: str,
        **options: object,
    ) -> dict[str, object]:
        with (
            mock.patch.object(
                experiment, "read_session", return_value=(directory, session)
            ),
            mock.patch.object(experiment, "_session_is_current"),
            mock.patch.object(
                experiment,
                "select_best",
                return_value={"status": "provisional"},
            ),
            mock.patch.object(
                experiment,
                "_baseline_candidate",
                return_value={
                    "status": "provisional",
                    "score": 0.5,
                    "repository_effective_score": 0.5,
                    "changed_patch_lines": 0,
                    "sequence": 0,
                },
            ),
            mock.patch.object(experiment, "_trajectory", return_value=(0, [])),
        ):
            return experiment.reserve_trial(
                "0x00401000", label, root=self.root, **options
            )

    def _write_report(
        self,
        path: Path,
        target_score: float,
        other_score: float = 0.8,
        *,
        target_effective: bool = False,
    ) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps(
                {
                    "data": [
                        {
                            "address": "0x00401000",
                            "matching": target_score,
                            "effective": target_effective,
                            "type": 1,
                            "diff": [],
                        },
                        {
                            "address": "0x00401010",
                            "matching": other_score,
                            "type": 1,
                            "diff": [],
                        },
                    ]
                }
            )
            + "\n",
            encoding="utf-8",
        )
        experiment.provenance_path(path).write_text("{}\n", encoding="utf-8")

    def _controller_mocks(self, identity: dict[str, object]) -> ExitStack:
        stack = ExitStack()

        def report_receipt(path: Path) -> dict[str, object]:
            return {
                "receipt_id": f"report-{path.parent.name}-{path.name}",
                "input_identity": identity,
                "artifact": {
                    "path": str(path.resolve()),
                    "sha256": experiment.file_hash(path),
                },
            }

        stack.enter_context(
            mock.patch.object(
                experiment, "validate_report_artifact", side_effect=report_receipt
            )
        )
        stack.enter_context(
            mock.patch.object(
                experiment,
                "validate_diff",
                side_effect=lambda path, *_args, **_kwargs: {
                    "receipt_id": f"diff-{Path(path).parent.name}"
                },
            )
        )
        return stack

    def _complete_controller_session(
        self, target_score: float, *, target_effective: bool = False
    ) -> tuple[dict[str, object], Path, dict[str, object], dict[str, object]]:
        identity = {
            "head": self.head,
            "source_worktree_sha256": experiment._snapshot_hash(
                experiment.source_worktree_snapshot(self.root)
            ),
            "files": {
                "functions_map": experiment.file_hash(
                    self.root / "tools/Resources/functions_map.txt"
                )
            },
            "validation_tools": {"controller": "test"},
        }
        with self._controller_mocks(identity):
            plan = experiment.begin_session("0x00401000", root=self.root)
            session_directory = Path(str(plan["session_directory"]))
            baseline_report = Path(str(plan["baseline_report"]))
            baseline_data_report = Path(str(plan["baseline_data_report"]))
            self._write_report(baseline_report, 0.6)
            self._write_report(baseline_data_report, 0.6)
            Path(str(plan["compiler_context"])).write_text(
                json.dumps(
                    {
                        "git_head": self.head,
                        "implemented_addresses": [0x401000, 0x401010],
                        "stub_addresses": [],
                        "source_debt": {},
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            Path(str(plan["source_patch"])).write_bytes(b"")
            session = experiment.attach_baseline(
                "0x00401000", str(plan["session_id"]), root=self.root
            )
            trial = experiment.reserve_trial(
                "0x00401000",
                "candidate",
                root=self.root,
                question="Does this source shape improve the target?",
                parent="baseline",
                route="control-flow",
                model="Use the candidate source shape.",
            )
            trial_directory = Path(str(trial["trial_directory"]))
            self._write_report(
                Path(str(trial["report"])),
                target_score,
                target_effective=target_effective,
            )
            Path(str(trial["diff"])).write_text(
                f"Probe is only {target_score * 100:.2f}% similar to the original\n",
                encoding="utf-8",
            )
            experiment.provenance_path(Path(str(trial["diff"]))).write_text(
                "{}\n", encoding="utf-8"
            )
            Path(str(trial["source_patch"])).write_bytes(b"")
            Path(str(trial["compiler_context"])).write_text(
                json.dumps(
                    {
                        "git_head": self.head,
                        "implemented_addresses": [0x401000, 0x401010],
                        "stub_addresses": [],
                        "source_debt": {},
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            completed = experiment.record_trial(
                "0x00401000",
                str(trial["trial_id"]),
                root=self.root,
            )
            status = experiment.status_document("0x00401000", root=self.root)
        return status, trial_directory, completed, identity

    def test_begin_session_allocates_unique_directories_and_final_paths(self):
        fixed = datetime(2025, 1, 2, 3, 4, 5, tzinfo=timezone.utc)
        first = experiment.begin_session("0x401000", root=self.root, now=fixed)
        second = experiment.begin_session("0x401000", root=self.root, now=fixed)

        self.assertNotEqual(first["session_id"], second["session_id"])
        self.assertNotEqual(first["session_directory"], second["session_directory"])
        for plan in (first, second):
            session_directory = Path(str(plan["session_directory"]))
            self.assertEqual(
                Path(str(plan["baseline_report"])),
                session_directory / "baseline/report.json",
            )
            self.assertTrue((session_directory / "start.json").is_file())

    def test_reserve_writes_pending_before_any_artifact_exists(self):
        directory, session = self._session()
        plan = self._reserve(
            directory,
            session,
            "signed-loop",
            question="Does the loop use a signed bound?",
            route="control-flow",
            model="Use a signed loop counter.",
        )

        trial = Path(str(plan["trial_directory"]))
        pending = experiment.read_receipt(
            trial / "pending.json", kind="experiment-trial-pending"
        )
        self.assertEqual(pending["label"], "signed-loop")
        self.assertEqual(pending["question"], "Does the loop use a signed bound?")
        self.assertFalse((trial / "report.json").exists())

    def test_branch_binds_declared_parent_and_legacy_reserve_cannot_measure(self):
        directory, session = self._session()
        snapshot = experiment.source_worktree_snapshot(self.root)
        session[experiment.SOURCE_SNAPSHOT_FIELD] = snapshot
        with (
            mock.patch.object(
                experiment, "read_session", return_value=(directory, session)
            ),
            mock.patch.object(experiment, "_session_is_current"),
            mock.patch.object(
                experiment,
                "select_best",
                return_value={"status": "provisional"},
            ),
            mock.patch.object(
                experiment,
                "_baseline_candidate",
                return_value={"status": "provisional", "score": 0.5},
            ),
            mock.patch.object(experiment, "_trajectory", return_value=(0, [])),
        ):
            plan = experiment.reserve_trial(
                "0x00401000",
                "prepared",
                root=self.root,
                parent="baseline",
                route="abi",
                bind_parent=True,
            )
        pending = experiment.read_receipt(
            Path(str(plan["trial_directory"])) / "pending.json",
            kind="experiment-trial-pending",
        )
        self.assertEqual(pending[experiment.PARENT_SNAPSHOT_FIELD], snapshot)
        with (
            mock.patch.object(
                experiment, "read_session", return_value=(directory, session)
            ),
            mock.patch.object(experiment, "_session_is_current"),
        ):
            measured = experiment.measure_plan(
                "0x00401000", str(plan["trial_id"]), root=self.root
            )
        self.assertEqual(measured["pending_receipt_id"], pending["receipt_id"])

        with mock.patch.object(
            experiment, "read_session", return_value=(directory, session)
        ):
            experiment.fail_trial(
                "0x00401000",
                str(plan["trial_id"]),
                root=self.root,
                stage="interrupted",
            )
        legacy = self._reserve(directory, session, "legacy", route="call")
        with (
            mock.patch.object(
                experiment, "read_session", return_value=(directory, session)
            ),
            mock.patch.object(experiment, "_session_is_current"),
        ):
            with self.assertRaisesRegex(experiment.ExperimentError, "source snapshot"):
                experiment.measure_plan(
                    "0x00401000", str(legacy["trial_id"]), root=self.root
                )

    def test_branch_rejects_a_worktree_that_differs_from_the_declared_parent(self):
        directory, session = self._session()
        baseline_snapshot = {"src/Baseline.cpp": "a" * 64}
        session[experiment.SOURCE_SNAPSHOT_FIELD] = baseline_snapshot
        completed_snapshot = {"src/Trial.cpp": "b" * 64}
        completed = {
            "trial_id": "001-first",
            "label": "first",
            experiment.SOURCE_SNAPSHOT_FIELD: completed_snapshot,
            "comparison_identity": {
                "source_worktree_sha256": experiment._snapshot_hash(
                    completed_snapshot
                )
            },
        }
        for parent, trials in (
            ("baseline", []),
            ("first", [(directory / "trials/001-first", completed, "completed")]),
        ):
            with (
                self.subTest(parent=parent),
                mock.patch.object(
                    experiment, "read_session", return_value=(directory, session)
                ),
                mock.patch.object(experiment, "_session_is_current"),
                mock.patch.object(experiment, "_trial_receipts", return_value=trials),
                mock.patch.object(
                    experiment,
                    "select_best",
                    return_value={"status": "provisional"},
                ),
                mock.patch.object(
                    experiment,
                    "_baseline_candidate",
                    return_value={"status": "provisional", "score": 0.5},
                ),
                mock.patch.object(experiment, "_trajectory", return_value=(0, [])),
                mock.patch.object(
                    experiment,
                    "source_worktree_snapshot",
                    return_value={"src/Different.cpp": "c" * 64},
                ),
                self.assertRaisesRegex(
                    experiment.ExperimentError,
                    "does not equal the declared parent",
                ),
            ):
                experiment.reserve_trial(
                    "0x00401000",
                    f"child-{parent}",
                    root=self.root,
                    parent=parent,
                    route="abi",
                    bind_parent=True,
                )

    def test_reserve_rejects_pending_and_duplicate_labels(self):
        directory, session = self._session()
        first = self._reserve(directory, session, "shape")
        with self.assertRaisesRegex(experiment.ExperimentError, "pending"):
            self._reserve(directory, session, "next")

        with mock.patch.object(
            experiment, "read_session", return_value=(directory, session)
        ):
            experiment.fail_trial(
                "0x00401000",
                str(first["trial_id"]),
                root=self.root,
                stage="build",
            )
        with self.assertRaisesRegex(experiment.ExperimentError, "already exists"):
            self._reserve(directory, session, "SHAPE")

    def test_failed_trial_consumes_budget_and_keeps_first_failure(self):
        directory, session = self._session(max_trials=1)
        plan = self._reserve(directory, session, "candidate")
        with mock.patch.object(
            experiment, "read_session", return_value=(directory, session)
        ):
            failure = experiment.fail_trial(
                "0x00401000",
                str(plan["trial_id"]),
                root=self.root,
                stage="comparison",
                exit_code=9,
                message="Comparator failed.",
            )
            with self.assertRaisesRegex(experiment.ExperimentError, "terminal"):
                experiment.fail_trial(
                    "0x00401000",
                    str(plan["trial_id"]),
                    root=self.root,
                    stage="diff",
                )

        self.assertEqual(failure["first_failing_constraint"], "comparison-failure")
        with self.assertRaisesRegex(experiment.ExperimentError, "budget"):
            self._reserve(directory, session, "second")

    def test_limits_and_campaign_deadline_fail_closed(self):
        with self.assertRaisesRegex(experiment.ExperimentError, "max-trials"):
            experiment.begin_session("0x401000", root=self.root, max_trials=0)

        _, session = self._session()
        session["campaign"] = {"campaign_id": "campaign-1"}
        expired = (datetime.now(timezone.utc) - timedelta(seconds=1)).isoformat()
        with (
            mock.patch.object(experiment, "_head", return_value=self.head),
            mock.patch.object(
                experiment,
                "_campaign_context",
                return_value={"campaign_id": "campaign-1", "deadline": expired},
            ),
        ):
            with self.assertRaisesRegex(experiment.ExperimentError, "deadline"):
                experiment._session_is_current(self.root, session)

    def test_receipt_rejects_tampering_oversize_and_path_escape(self):
        receipt_path = self.root / "build/receipt.json"
        experiment.write_receipt(receipt_path, {"kind": "probe", "value": 1})
        value = json.loads(receipt_path.read_text(encoding="utf-8"))
        value["value"] = 2
        receipt_path.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(experiment.ExperimentError, "identity"):
            experiment.read_receipt(receipt_path)

        oversized = self.root / "build/oversized.json"
        oversized.write_bytes(b"x" * (experiment.MAX_RECORD_BYTES + 1))
        with self.assertRaisesRegex(experiment.ExperimentError, "missing or invalid"):
            experiment.read_receipt(oversized)
        with self.assertRaisesRegex(experiment.ExperimentError, "escapes"):
            experiment._resolve_relative("../outside", self.root)

    def test_descriptor_rejects_symlink_and_size_change(self):
        artifact = self.root / "build/artifact.txt"
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_text("first\n", encoding="utf-8")
        descriptor = experiment._descriptor(
            artifact, self.root, maximum=100, description="artifact"
        )
        artifact.write_text("second\n", encoding="utf-8")
        with self.assertRaisesRegex(experiment.ExperimentError, "changed"):
            experiment._validate_descriptor(
                descriptor, self.root, maximum=100, description="artifact"
            )

        target = self.root / "build/target.txt"
        target.write_text("target\n", encoding="utf-8")
        link = self.root / "build/link.txt"
        try:
            link.symlink_to(target)
        except OSError:
            self.skipTest("symbolic links are not available")
        with self.assertRaisesRegex(experiment.ExperimentError, "symbolic link"):
            experiment._descriptor(
                link, self.root, maximum=100, description="linked artifact"
            )

    def test_report_descriptor_requires_a_provenance_sidecar(self):
        report = self.root / "build/report.json"
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text('{"data": []}\n', encoding="utf-8")
        with mock.patch.object(
            experiment,
            "validate_report_artifact",
            return_value={"receipt_id": "report-receipt"},
        ):
            with self.assertRaisesRegex(experiment.ExperimentError, "provenance"):
                experiment._report_descriptor(report, self.root)

    def test_gate_rejects_wrong_target_and_regressions(self):
        baseline = {
            0x401000: MatchStatus(0.8),
            0x401010: MatchStatus(1.0),
        }
        current = {0x401010: MatchStatus(0.9)}
        gate = experiment._gate_result(
            baseline,
            current,
            0x401000,
            {"implemented_addresses": [0x401000], "source_debt": {}},
            {"implemented_addresses": [], "source_debt": {}},
        )

        self.assertFalse(gate["passed"])
        self.assertEqual(gate["first_failing_constraint"], "source-integrity")
        names = [item["name"] for item in gate["constraints"]]
        self.assertIn("missing-target", names)
        self.assertIn("unrelated-regression", names)

        target_regression = experiment._gate_result(
            {0x401000: MatchStatus(0.8)},
            {0x401000: MatchStatus(0.7)},
            0x401000,
            {"implemented_addresses": [0x401000], "source_debt": {}},
            {"implemented_addresses": [0x401000], "source_debt": {}},
        )
        self.assertFalse(target_regression["passed"])
        self.assertEqual(
            target_regression["first_failing_constraint"],
            "target-score-regression",
        )

    def test_complete_controller_lifecycle_records_and_selects_trial(self):
        status, _, completed, identity = self._complete_controller_session(0.7)

        self.assertTrue(completed["eligible"])
        self.assertEqual(status["trials_completed"], 1)
        self.assertEqual(status["best"]["label"], "candidate")
        self.assertEqual(experiment.advice_document(status)["action"], "repair")
        with self._controller_mocks(identity):
            report = experiment.report_document("0x00401000", root=self.root)
        self.assertEqual(report["best"]["label"], "candidate")

    def test_ineligible_target_regression_keeps_baseline_best(self):
        status, _, completed, _ = self._complete_controller_session(0.4)

        self.assertFalse(completed["eligible"])
        self.assertEqual(completed["first_failing_constraint"], "target-below-threshold")
        self.assertEqual(status["best"]["candidate"], "baseline")
        self.assertEqual(status["best"]["score"], 0.6)

    def test_reserve_refuses_after_terminal_best(self):
        for score, effective in ((1.0, False), (0.7, True)):
            with self.subTest(score=score, effective=effective):
                status, _, _, identity = self._complete_controller_session(
                    score, target_effective=effective
                )
                self.assertIn(status["best"]["status"], {"exact", "effective"})
                with self._controller_mocks(identity):
                    with self.assertRaisesRegex(
                        experiment.ExperimentError, "terminal"
                    ):
                        experiment.reserve_trial(
                            "0x00401000", "after-terminal", root=self.root
                        )

    def test_reserve_refuses_after_non_improvement_limit(self):
        status, _, _, identity = self._complete_controller_session(0.4)
        self.assertEqual(status["consecutive_non_improving"], 1)
        with self._controller_mocks(identity):
            second = experiment.reserve_trial(
                "0x00401000",
                "failed-build",
                root=self.root,
                route="control-flow",
            )
            experiment.fail_trial(
                "0x00401000",
                str(second["trial_id"]),
                root=self.root,
                stage="build",
            )
            with self.assertRaisesRegex(
                experiment.ExperimentError, "non-improvement limit"
            ):
                experiment.reserve_trial(
                    "0x00401000", "over-limit", root=self.root
                )

    def test_completed_report_and_diff_tampering_is_rejected(self):
        status, trial_directory, _, identity = self._complete_controller_session(0.7)
        self.assertEqual(status["best"]["label"], "candidate")
        report = trial_directory / "report.json"
        report.write_text(report.read_text(encoding="utf-8") + " ", encoding="utf-8")
        with self._controller_mocks(identity):
            with self.assertRaisesRegex(experiment.ExperimentError, "changed"):
                experiment.status_document("0x00401000", root=self.root)

        status, trial_directory, _, identity = self._complete_controller_session(0.7)
        self.assertEqual(status["best"]["label"], "candidate")
        diff = trial_directory / "diff.txt"
        diff.write_text(diff.read_text(encoding="utf-8") + "changed\n", encoding="utf-8")
        with self._controller_mocks(identity):
            with self.assertRaisesRegex(experiment.ExperimentError, "changed"):
                experiment.status_document("0x00401000", root=self.root)

    def test_ranking_prefers_terminal_status_then_documented_ties(self):
        provisional = {
            "status": "provisional",
            "score": 1.0,
            "repository_effective_score": 50,
            "changed_patch_lines": 1,
            "sequence": 1,
        }
        effective = dict(provisional, status="effective", score=0.8)
        exact = dict(effective, status="exact", score=1.0)
        self.assertGreater(experiment._rank(effective), experiment._rank(provisional))
        self.assertGreater(experiment._rank(exact), experiment._rank(effective))

        fewer_lines = dict(effective, changed_patch_lines=4, sequence=2)
        more_lines = dict(effective, changed_patch_lines=5, sequence=1)
        self.assertEqual(experiment._rank(fewer_lines), experiment._rank(more_lines))
        earlier = dict(effective, changed_patch_lines=4, sequence=1)
        self.assertEqual(experiment._rank(earlier), experiment._rank(fewer_lines))

        rounded_tie = dict(effective, score=0.8000004)
        self.assertEqual(experiment._rank(rounded_tie), experiment._rank(effective))

    def test_advice_covers_finalize_deadline_budget_evidence_and_repair(self):
        base = {
            "state": "verified",
            "address": "0x00401000",
            "session_id": "session",
            "deadline_expired": False,
            "limits": {"max_trials": 3, "max_non_improving": 2},
            "trials_reserved": 0,
            "consecutive_non_improving": 0,
            "non_improving_routes": [],
            "best": {"status": "provisional"},
        }
        self.assertEqual(experiment.advice_document(base)["action"], "repair")
        self.assertEqual(
            experiment.advice_document(dict(base, deadline_expired=True))["action"],
            "stop",
        )
        self.assertEqual(
            experiment.advice_document(
                dict(base, best={"status": "effective"})
            )["action"],
            "finalize",
        )
        self.assertEqual(
            experiment.advice_document(dict(base, trials_reserved=3))["action"],
            "stop",
        )
        repeated = dict(
            base,
            trials_reserved=2,
            consecutive_non_improving=2,
            non_improving_routes=["control-flow", "control-flow"],
        )
        self.assertEqual(experiment.advice_document(repeated)["action"], "get-evidence")
        mixed = dict(
            repeated,
            non_improving_routes=["control-flow", "type-layout"],
        )
        self.assertEqual(experiment.advice_document(mixed)["action"], "repair")

    def test_legacy_flat_directory_is_unverified(self):
        directory = self.root / "build/decomp-experiments/0x00401000"
        directory.mkdir(parents=True)
        (directory / "old.report.json").write_text("{}\n", encoding="utf-8")
        status = experiment.status_document("0x401000", root=self.root)
        self.assertEqual(status["state"], "legacy-unverified")
        self.assertIsNone(status["best"])

    def test_cli_exposes_named_controller_operations(self):
        parser = experiment._parser()
        help_text = parser.format_help()
        for operation in (
            "begin-session",
            "attach-baseline",
            "reserve",
            "branch",
            "measure-plan",
            "fail",
            "record",
            "seal",
            "status",
            "best",
            "advise",
            "report",
        ):
            self.assertIn(operation, help_text)
        for operation in ("status", "best", "advise"):
            parsed = parser.parse_args([operation, "0x00401000"])
            self.assertEqual(parsed.action, operation)
            self.assertFalse(hasattr(parsed, "label"))
        parsed = parser.parse_args(["report", "0x00401000", "trial"])
        self.assertEqual(parsed.label, "trial")


if __name__ == "__main__":
    unittest.main()
