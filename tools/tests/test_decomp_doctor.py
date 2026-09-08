from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timedelta, timezone
from io import StringIO
from pathlib import Path
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools import decomp_doctor as doctor  # noqa: E402
from tools import decomp_campaigns as campaigns  # noqa: E402

CAMPAIGN_HASHES = {
    "source_worktree_sha256": "1" * 64,
    "source_index_sha256": "2" * 64,
    "repository_worktree_sha256": "3" * 64,
    "repository_index_sha256": "4" * 64,
    "resource_sources_sha256": "5" * 64,
    "functions_map_sha256": "6" * 64,
    "function_sizes_file_sha256": "7" * 64,
    "retail_executable_sha256": "8" * 64,
    "recompiled_executable_sha256": "9" * 64,
    "recompiled_symbols_sha256": "a" * 64,
    "reccmp_build_sha256": "b" * 64,
    "reccmp_user_sha256": "0" * 64,
    "configured_retail_executable_sha256": "1" * 64,
    "current_report_sha256": "c" * 64,
    "current_report_provenance_sha256": "d" * 64,
    "current_data_report_sha256": "e" * 64,
    "current_data_report_provenance_sha256": "f" * 64,
}


class FakeRunner:
    def __init__(self, *, empty_disassembly: bool = False):
        self.empty_disassembly = empty_disassembly
        self.calls: list[tuple[list[str], dict[str, object]]] = []

    def __call__(self, command: list[str], **kwargs: object) -> subprocess.CompletedProcess:
        self.calls.append((command, kwargs))
        if command[:3] == ["git", "branch", "--show-current"]:
            return subprocess.CompletedProcess(command, 0, "agent/continuous\n", "")
        if command == ["git", "rev-parse", "HEAD"]:
            return subprocess.CompletedProcess(command, 0, "a" * 40 + "\n", "")
        if command[:2] == ["git", "fetch"]:
            return subprocess.CompletedProcess(command, 0, "", "")
        if command[:3] == ["git", "rev-parse", "--verify"]:
            return subprocess.CompletedProcess(command, 0, "9" * 40 + "\n", "")
        if command[:3] == ["git", "merge-base", "--is-ancestor"]:
            return subprocess.CompletedProcess(command, 0, "", "")
        if command[:2] == ["git", "status"]:
            return subprocess.CompletedProcess(command, 0, "", "")
        if "disasm" in command:
            instructions = [] if self.empty_disassembly else [{"mnemonic": "ret"}]
            return subprocess.CompletedProcess(command, 0, json.dumps(instructions), "")
        if "decompile" in command:
            return subprocess.CompletedProcess(command, 0, json.dumps([{"code": "void Probe() {}"}]), "")
        if "function" in command and "get" in command:
            return subprocess.CompletedProcess(
                command,
                0,
                json.dumps({"address": "0x00401000", "name": "Probe"}),
                "",
            )
        if "x-ref" in command:
            return subprocess.CompletedProcess(command, 0, json.dumps([]), "")
        if "status" in command:
            return subprocess.CompletedProcess(command, 0, json.dumps({"status": "ready"}), "")
        return subprocess.CompletedProcess(command, 0, "ok\n", "")


def prepare_root(root: Path) -> None:
    for relative, content in {
        "original/toy2.exe": b"retail",
        "build/toy2.exe": b"rebuilt",
        "build/toy2.pdb": b"symbols",
        "reccmp-project.yml": b"targets: {}\n",
        "build/reccmp-build.yml": b"targets: {}\n",
        "tools/Resources/functions_map.txt": b"0x00401000 Probe\n",
        "build/decomp-current-report.json": b'{"data":[{"address":"0x00401000"}]}',
        "build/decomp-function-sizes.json": b'[{"address":"0x00401000","size":4}]',
        "tools/decomp_doctor.py": b"tool\n",
    }.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    (root / ".tooling/wineprefix").mkdir(parents=True)


class DoctorTests(unittest.TestCase):
    def setUp(self):
        self.provenance_patch = patch.object(
            doctor, "_validate_report_provenance", return_value=None
        )
        self.provenance_patch.start()
        self.addCleanup(self.provenance_patch.stop)

    def test_invalid_lane_mode_pair_stops_before_preflight_checks(self):
        runner = FakeRunner()
        with self.assertRaisesRegex(ValueError, "closure lane cannot use coverage"):
            doctor.run_doctor(
                "coverage",
                "0x00401000",
                lane="closure",
                runner=runner,
            )
        self.assertEqual(runner.calls, [])

    def test_invalid_selection_shape_stops_before_preflight_checks(self):
        runner = FakeRunner()
        with self.assertRaisesRegex(ValueError, "targets do not match"):
            doctor.run_doctor("refinement", lane="production", runner=runner)
        self.assertEqual(runner.calls, [])

    def test_origin_check_force_fetches_and_rejects_a_stale_head(self):
        class OriginRunner:
            def __init__(self):
                self.calls: list[list[str]] = []

            def __call__(self, command: list[str], **_kwargs: object):
                self.calls.append(command)
                if command[:2] == ["git", "fetch"]:
                    return subprocess.CompletedProcess(command, 0, "", "")
                if command[:3] == ["git", "rev-parse", "--verify"]:
                    return subprocess.CompletedProcess(command, 0, "b" * 40 + "\n", "")
                if command == ["git", "rev-parse", "HEAD"]:
                    return subprocess.CompletedProcess(command, 0, "a" * 40 + "\n", "")
                if command[:3] == ["git", "merge-base", "--is-ancestor"]:
                    return subprocess.CompletedProcess(command, 1, "", "")
                raise AssertionError(command)

        runner = OriginRunner()
        check = doctor._origin_integration_check(
            Path.cwd(), runner, "agent/continuous"
        )
        self.assertFalse(check.ok)
        self.assertTrue(check.data["fetched"])
        self.assertIn(
            [
                "git",
                "fetch",
                "--quiet",
                "--no-tags",
                "origin",
                "+refs/heads/agent/continuous:refs/remotes/origin/agent/continuous",
            ],
            runner.calls,
        )

    def test_origin_check_fails_closed_when_fetch_fails(self):
        calls: list[list[str]] = []

        def runner(command: list[str], **_kwargs: object):
            calls.append(command)
            return subprocess.CompletedProcess(command, 1, "", "fetch failed")

        check = doctor._origin_integration_check(
            Path.cwd(), runner, "agent/continuous"
        )
        self.assertFalse(check.ok)
        self.assertFalse(check.data["fetched"])
        self.assertEqual(len(calls), 1)

    def test_json_timestamp_error_has_one_machine_readable_result(self):
        output = StringIO()
        error = StringIO()
        arguments = [
            "decomp_doctor.py",
            "--mode",
            "refinement",
            "--lane",
            "production",
            "--target",
            "0x00401000",
            "--selection-started-at",
            "not-a-time",
            "--json",
        ]
        with (
            patch.object(sys, "argv", arguments),
            patch.object(doctor, "run_doctor") as run,
            redirect_stdout(output),
            redirect_stderr(error),
        ):
            self.assertEqual(doctor.main(), 2)
        self.assertFalse(json.loads(output.getvalue())["ok"])
        self.assertEqual(error.getvalue(), "")
        run.assert_not_called()

    def test_json_parse_errors_do_not_start_preflight(self):
        cases = {
            "mode": ["--mode", "invalid", "--lane", "production", "--json"],
            "selection": [
                "--mode",
                "refinement",
                "--lane",
                "production",
                "--json",
            ],
            "target": [
                "--mode",
                "refinement",
                "--lane",
                "production",
                "--target",
                "not-an-address",
                "--json",
            ],
        }
        for name, options in cases.items():
            output = StringIO()
            error = StringIO()
            with (
                self.subTest(name=name),
                patch.object(sys, "argv", ["decomp_doctor.py", *options]),
                patch.object(doctor, "run_doctor") as run,
                redirect_stdout(output),
                redirect_stderr(error),
            ):
                self.assertEqual(doctor.main(), 2)
            self.assertFalse(json.loads(output.getvalue())["ok"])
            self.assertEqual(error.getvalue(), "")
            run.assert_not_called()

    def test_allowed_dirt_preserves_porcelain_status_prefixes(self):
        class DirtRunner:
            def __call__(self, command: list[str], **_kwargs: object) -> subprocess.CompletedProcess:
                output = " M external/submodules/reccmp\0?? .codex/local.json\0"
                return subprocess.CompletedProcess(command, 0, output, "")

        check = doctor._dirt_check(Path.cwd(), DirtRunner())
        self.assertTrue(check.ok)
        self.assertEqual(check.data["rejected"], [])

    def test_ignored_workflow_handoffs_do_not_stale_doctor_dirt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            (root / ".gitignore").write_text("build/\n", encoding="utf-8")
            subprocess.run(["git", "add", ".gitignore"], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Doctor Tests",
                    "-c",
                    "user.email=doctor-tests@example.invalid",
                    "commit",
                    "-q",
                    "-m",
                    "Prepare fixture",
                ],
                cwd=root,
                check=True,
            )
            before = doctor._dirt_check(root, doctor._default_runner)
            cache = root / "build/decomp-cache"
            cache.mkdir(parents=True)
            for name in (
                "prediction-handoff.json",
                "retail-scout.json",
                "context-scout.json",
            ):
                (cache / name).write_text("{}\n", encoding="utf-8")
            after = doctor._dirt_check(root, doctor._default_runner)
            self.assertTrue(before.ok)
            self.assertTrue(after.ok)
            self.assertEqual(after.data, before.data)

            (root / "unexpected.json").write_text("{}\n", encoding="utf-8")
            rejected = doctor._dirt_check(root, doctor._default_runner)
            self.assertFalse(rejected.ok)
            self.assertEqual(rejected.data["rejected"], ["?? unexpected.json"])

    def test_active_campaign_allows_only_bound_product_source_dirt(self):
        class DirtRunner:
            def __init__(self, output: str):
                self.output = output

            def __call__(self, command: list[str], **_kwargs: object):
                return subprocess.CompletedProcess(command, 0, self.output, "")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / doctor.ACTIVE_STATE_RELATIVE
            state_path.parent.mkdir(parents=True)
            state_path.write_text(
                json.dumps(
                    {
                        "campaign_id": "campaign-1",
                        "campaign_head": "a" * 40,
                        "mode": "refinement",
                        "phase": "scoring",
                        "baseline_at": "2026-09-08T10:00:00+00:00",
                        "source_worktree_root": str(root.resolve()),
                    }
                ),
                encoding="utf-8",
            )
            source = doctor._dirt_check(
                root,
                DirtRunner(" M src/Target.cpp\0"),
                mode="refinement",
                head="a" * 40,
            )
            self.assertTrue(source.ok)
            self.assertEqual(source.data["campaign_entries"], [" M src/Target.cpp"])

            workflow = doctor._dirt_check(
                root,
                DirtRunner(" M src/Target.cpp\0 M tools/decomp.py\0"),
                mode="refinement",
                head="a" * 40,
            )
            self.assertFalse(workflow.ok)
            self.assertEqual(workflow.data["rejected"], [" M tools/decomp.py"])

            wrong_head = doctor._dirt_check(
                root,
                DirtRunner(" M src/Target.cpp\0"),
                mode="refinement",
                head="b" * 40,
            )
            self.assertFalse(wrong_head.ok)
            self.assertIn("HEAD does not match", wrong_head.detail)

    def test_source_evidence_failure_precedes_campaign_timing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            state = root / "build/decomp-campaign-state.json"
            state.write_text("sentinel\n", encoding="utf-8")
            runner = FakeRunner(empty_disassembly=True)
            moments = iter(
                [
                    datetime(2026, 9, 8, 10, 0, tzinfo=timezone.utc),
                    datetime(2026, 9, 8, 10, 0, 2, tzinfo=timezone.utc),
                ]
            )
            ticks = iter([5.0, 7.0])
            with (
                patch.object(doctor, "_find_executable", side_effect=lambda _root, name, local=None: f"/bin/{name}"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    "0x00401000",
                    lane="production",
                    root=root,
                    runner=runner,
                    now=lambda: next(moments),
                    clock=lambda: next(ticks),
                )

            self.assertFalse(receipt["ok"])
            self.assertEqual(receipt["status"], "failed")
            self.assertFalse(receipt["campaign_timing_started"])
            self.assertEqual(receipt["lane"], "production")
            self.assertEqual(receipt["root"], str(root.resolve()))
            self.assertEqual(receipt["head"], "a" * 40)
            self.assertEqual(
                receipt["branch"],
                {"actual": "agent/continuous", "expected": "agent/continuous"},
            )
            self.assertEqual(receipt["selection_started_at"], receipt["doctor_started_at"])
            self.assertRegex(str(receipt["receipt_id"]), r"^[0-9a-f]{64}$")
            self.assertEqual(state.read_text(encoding="utf-8"), "sentinel\n")
            self.assertFalse(any("campaign" in " ".join(call) for call, _ in runner.calls))
            stored = json.loads((root / doctor.RECEIPT_RELATIVE).read_text(encoding="utf-8"))
            self.assertEqual(stored["receipt_id"], receipt["receipt_id"])
            immutable = json.loads(
                (root / str(receipt["receipt_path"])).read_text(encoding="utf-8")
            )
            self.assertEqual(immutable, stored)
            self.assertIn(str(receipt["receipt_id"]), str(receipt["receipt_path"]))

    def test_source_modes_require_both_evidence_forms(self):
        runner = FakeRunner(empty_disassembly=True)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            environment, paths = doctor.ghidra_environment(root)
            with (
                patch.object(doctor, "_find_executable", return_value="/bin/ghidra"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                checks = doctor._ghidra_checks(root, runner, "coverage", 0x401000)
        by_name = {check.name: check for check in checks}
        self.assertFalse(by_name["disassembly"].ok)
        self.assertTrue(by_name["decompilation"].ok)
        ghidra_call = next(kwargs for command, kwargs in runner.calls if "disasm" in command)
        self.assertEqual(ghidra_call["env"]["XDG_DATA_HOME"], paths["state"])
        self.assertEqual(ghidra_call["env"]["RUST_LOG"], "warn")
        self.assertEqual(environment["GHIDRA_CLI_FULL_RESPONSE_LOGGING"], "0")

    def test_ready_source_receipt_has_content_addressed_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = FakeRunner()
            with (
                patch.object(
                    doctor,
                    "_find_executable",
                    side_effect=lambda _root, name, local=None: f"/bin/{name}",
                ),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={
                        "source": "live",
                        "target": "cache",
                        "active": ["bridge-test"],
                        "unverified": [],
                        "copied": [],
                    },
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    "0x00401000",
                    lane="production",
                    root=root,
                    runner=runner,
                )

            descriptors = doctor.source_artifact_descriptors(receipt, "0x00401000")
            self.assertEqual(
                set(descriptors),
                {"disassembly", "decompilation", "function", "xrefs"},
            )
            for kind, descriptor in descriptors.items():
                with self.subTest(kind=kind):
                    self.assertIn(str(descriptor["sha256"]), str(descriptor["path"]))
                    self.assertLessEqual(int(descriptor["bytes"]), doctor.GHIDRA_ARTIFACT_LIMIT)
                    self.assertTrue((root / str(descriptor["path"])).is_file())
                    payload = doctor.load_ghidra_artifact(
                        root,
                        "0x00401000",
                        kind,
                        descriptor,
                    )
                    self.assertIsNotNone(payload)

    def test_ready_source_receipt_rejects_a_changed_evidence_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = FakeRunner()
            with (
                patch.object(
                    doctor,
                    "_find_executable",
                    side_effect=lambda _root, name, local=None: f"/bin/{name}",
                ),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={
                        "source": "live",
                        "target": "cache",
                        "active": ["bridge-test"],
                        "unverified": [],
                        "copied": [],
                    },
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    "0x00401000",
                    lane="production",
                    root=root,
                    runner=runner,
                )
            descriptor = doctor.source_artifact_descriptors(
                receipt, "0x00401000"
            )["disassembly"]
            path = root / str(descriptor["path"])
            changed = bytearray(path.read_bytes())
            changed[-2] = ord(" ") if changed[-2] != ord(" ") else ord("x")
            path.write_bytes(changed)
            with (
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
                self.assertRaisesRegex(ValueError, "artifact hash does not match"),
            ):
                doctor.validate_doctor_receipt(
                    receipt,
                    root=root,
                    runner=runner,
                    expected_input_hashes=CAMPAIGN_HASHES,
                )

    def test_source_artifact_size_limit_fails_the_evidence_check(self):
        runner = FakeRunner()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (
                patch.object(doctor, "_find_executable", return_value="/bin/ghidra"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={
                        "source": "live",
                        "target": "cache",
                        "active": ["bridge-test"],
                        "unverified": [],
                        "copied": [],
                    },
                ),
                patch.object(doctor, "GHIDRA_ARTIFACT_LIMIT", 32),
            ):
                checks = doctor._ghidra_checks(root, runner, "coverage", 0x401000)
        by_name = {check.name: check for check in checks}
        self.assertFalse(by_name["disassembly"].ok)
        self.assertFalse(by_name["decompilation"].ok)
        self.assertIn("exceeds", by_name["disassembly"].detail)

    def test_live_bridge_markers_are_mirrored_without_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            source_home = Path(directory) / "live-data"
            source = source_home / "ghidra-cli"
            source.mkdir(parents=True)
            marker = "bridge-current"
            (source / f"{marker}.pid").write_text(str(os.getpid()), encoding="utf-8")
            (source / f"{marker}.port").write_text("32123", encoding="utf-8")
            (source / "ghidra-cli.log.2026-09-08").write_text("secret response", encoding="utf-8")

            with patch.object(doctor, "_bridge_process_state", return_value="verified"):
                stats = doctor.mirror_ghidra_bridge_markers(root, source_data_home=source_home)
            target = Path(str(stats["target"]))

            self.assertEqual(stats["copied"], [marker])
            self.assertEqual((target / f"{marker}.pid").read_text(encoding="utf-8"), str(os.getpid()))
            self.assertEqual((target / f"{marker}.port").read_text(encoding="utf-8"), "32123")
            self.assertFalse(any("log" in path.name for path in target.iterdir()))

    @unittest.skipUnless(Path("/proc/self/cmdline").exists(), "Linux process identity test")
    def test_unrelated_live_process_marker_is_not_mirrored(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            source_home = Path(directory) / "live-data"
            source = source_home / "ghidra-cli"
            source.mkdir(parents=True)
            (source / "bridge-reused.pid").write_text(str(os.getpid()), encoding="utf-8")
            (source / "bridge-reused.port").write_text("32123", encoding="utf-8")
            stats = doctor.mirror_ghidra_bridge_markers(root, source_data_home=source_home)
        self.assertEqual(stats["active"], [])
        self.assertEqual(stats["copied"], [])

    def test_unverified_marker_needs_successful_ghidra_status(self):
        class FailedStatusRunner(FakeRunner):
            def __call__(self, command: list[str], **kwargs: object) -> subprocess.CompletedProcess:
                if "status" in command:
                    self.calls.append((command, kwargs))
                    return subprocess.CompletedProcess(command, 0, "No bridge running", "")
                return super().__call__(command, **kwargs)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (
                patch.object(doctor, "_find_executable", return_value="/bin/ghidra"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": [], "unverified": ["bridge-test"], "copied": []},
                ),
            ):
                checks = doctor._ghidra_checks(root, FailedStatusRunner(), "data", None)
        by_name = {check.name: check for check in checks}
        self.assertFalse(by_name["ghidra-state"].ok)
        self.assertFalse(by_name["ghidra-bridge-markers"].ok)

    def test_log_rotation_keeps_three_bounded_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log_root = root / doctor.CACHE_RELATIVE / "ghidra/cache/ghidra-cli"
            log_root.mkdir(parents=True)
            for index in range(5):
                (log_root / f"ghidra-cli.log.{index}").write_bytes(bytes([index]) * 24)
            with patch.object(doctor, "LOG_LIMIT", 16):
                stats = doctor.prune_ghidra_logs(root)
            logs = list(log_root.glob("*.log.*"))
            sizes = [path.stat().st_size for path in logs]
        self.assertEqual(len(logs), 3)
        self.assertTrue(all(size <= 16 for size in sizes))
        self.assertEqual(stats["removed"], 2)

    def test_selection_timestamp_is_retained_in_ready_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = FakeRunner()
            started = datetime(2026, 9, 8, 10, 0, tzinfo=timezone.utc)
            selected = started - timedelta(seconds=4)
            moments = iter([started, started + timedelta(seconds=2)])
            with (
                patch.object(doctor, "_find_executable", side_effect=lambda _root, name, local=None: f"/bin/{name}"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    "0x00401000",
                    lane="production",
                    selection_started_at=selected,
                    root=root,
                    runner=runner,
                    now=lambda: next(moments),
                )
        self.assertTrue(receipt["ok"])
        self.assertEqual(receipt["status"], "ready")
        self.assertEqual(receipt["addresses"], ["0x00401000"])
        self.assertIsNone(receipt["resource"])
        self.assertEqual(receipt["input_hashes"], CAMPAIGN_HASHES)
        self.assertEqual(receipt["selection_started_at"], "2026-09-08T09:59:56.000Z")

    def test_bundled_source_receipt_checks_every_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            (root / "tools/Resources/functions_map.txt").write_text(
                "0x00401000 ProbeA\n0x00401100 ProbeB\n", encoding="utf-8"
            )
            (root / "build/decomp-current-report.json").write_text(
                '{"data":[{"address":"0x00401000"},{"address":"0x00401100"}]}',
                encoding="utf-8",
            )
            (root / "build/decomp-function-sizes.json").write_text(
                '[{"address":"0x00401000","size":4},{"address":"0x00401100","size":4}]',
                encoding="utf-8",
            )
            runner = FakeRunner()
            with (
                patch.object(doctor, "_find_executable", side_effect=lambda _root, name, local=None: f"/bin/{name}"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    targets=["0x00401000", "0x00401100"],
                    lane="production",
                    root=root,
                    runner=runner,
                )
        self.assertTrue(receipt["ok"])
        self.assertEqual(receipt["addresses"], ["0x00401000", "0x00401100"])
        self.assertIsNone(receipt["target"])
        self.assertEqual(sum("disasm" in command for command, _ in runner.calls), 2)
        self.assertEqual(sum("decompile" in command for command, _ in runner.calls), 2)

    def test_a_later_target_receipt_does_not_replace_the_first_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            (root / "tools/Resources/functions_map.txt").write_text(
                "0x00401000 ProbeA\n0x00401100 ProbeB\n",
                encoding="utf-8",
            )
            (root / "build/decomp-current-report.json").write_text(
                '{"data":[{"address":"0x00401000"},{"address":"0x00401100"}]}',
                encoding="utf-8",
            )
            (root / "build/decomp-function-sizes.json").write_text(
                '[{"address":"0x00401000","size":4},{"address":"0x00401100","size":4}]',
                encoding="utf-8",
            )
            runner = FakeRunner()
            with (
                patch.object(
                    doctor,
                    "_find_executable",
                    side_effect=lambda _root, name, local=None: f"/bin/{name}",
                ),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={
                        "source": "live",
                        "target": "cache",
                        "active": ["bridge-test"],
                        "unverified": [],
                        "copied": [],
                    },
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                first = doctor.run_doctor(
                    "refinement",
                    "0x00401000",
                    lane="production",
                    root=root,
                    runner=runner,
                )
                second = doctor.run_doctor(
                    "refinement",
                    "0x00401100",
                    lane="production",
                    root=root,
                    runner=runner,
                )
                validated = doctor.validate_doctor_receipt(
                    root / str(first["receipt_path"]),
                    root=root,
                    runner=runner,
                    expected_addresses=["0x00401000"],
                    expected_input_hashes=CAMPAIGN_HASHES,
                )

            self.assertNotEqual(first["receipt_path"], second["receipt_path"])
            self.assertTrue((root / str(first["receipt_path"])).is_file())
            self.assertTrue((root / str(second["receipt_path"])).is_file())
            latest = json.loads(
                (root / doctor.RECEIPT_RELATIVE).read_text(encoding="utf-8")
            )
            self.assertEqual(latest["receipt_id"], second["receipt_id"])
            self.assertEqual(validated["receipt_id"], first["receipt_id"])

    def test_resource_receipt_stores_the_exact_tuple(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = FakeRunner()
            with (
                patch.object(doctor, "_find_executable", side_effect=lambda _root, name, local=None: f"/bin/{name}"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "resource",
                    resource="0x2,127,2057",
                    lane="resource",
                    root=root,
                    runner=runner,
                )
        self.assertTrue(receipt["ok"])
        self.assertEqual(receipt["addresses"], [])
        self.assertEqual(receipt["resource"], "2,127,2057")

    def test_ready_receipt_matches_campaign_identity_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = FakeRunner()
            started = datetime(2026, 9, 8, 10, 0, tzinfo=timezone.utc)
            moments = iter([started, started + timedelta(seconds=2)])
            with (
                patch.object(doctor, "_find_executable", side_effect=lambda _root, name, local=None: f"/bin/{name}"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    targets=["0x00401000"],
                    lane="production",
                    root=root,
                    runner=runner,
                    now=lambda: next(moments),
                )
            times, identity = campaigns._read_doctor_receipt(
                root / doctor.RECEIPT_RELATIVE,
                root,
                "production",
                "refinement",
                ["0x00401000"],
                None,
                CAMPAIGN_HASHES,
                started + timedelta(seconds=3),
                runner=runner,
            )
        self.assertEqual(identity["receipt_id"], receipt["receipt_id"])
        self.assertEqual(identity["input_hashes"], CAMPAIGN_HASHES)
        self.assertEqual(times["selection_started_at"], "2026-09-08T10:00:00+00:00")

    def test_public_validator_rejects_forged_repository_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = FakeRunner()
            with (
                patch.object(doctor, "_find_executable", side_effect=lambda _root, name, local=None: f"/bin/{name}"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    "0x00401000",
                    lane="production",
                    root=root,
                    runner=runner,
                )
            validated = doctor.validate_doctor_receipt(
                receipt,
                root=root,
                runner=runner,
                expected_mode="refinement",
                expected_lane="production",
                expected_addresses=["0x00401000"],
                expected_resource=None,
                expected_input_hashes=CAMPAIGN_HASHES,
            )
            self.assertEqual(validated["receipt_id"], receipt["receipt_id"])

            for field, value, message in (
                ("root", str(root / "other"), "root"),
                ("head", "b" * 40, "HEAD"),
                (
                    "branch",
                    {"actual": "other", "expected": "agent/continuous"},
                    "branch",
                ),
                ("lane", "data", "data lane cannot use refinement"),
            ):
                with self.subTest(field=field):
                    forged = dict(receipt)
                    forged[field] = value
                    forged["receipt_id"] = doctor._receipt_id(forged)
                    with self.assertRaisesRegex(ValueError, message):
                        doctor.validate_doctor_receipt(
                            forged,
                            root=root,
                            runner=runner,
                            expected_input_hashes=CAMPAIGN_HASHES,
                        )

    def test_public_validator_rejects_an_incomplete_check_set(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            runner = FakeRunner()
            with (
                patch.object(doctor, "_find_executable", side_effect=lambda _root, name, local=None: f"/bin/{name}"),
                patch.object(
                    doctor,
                    "mirror_ghidra_bridge_markers",
                    return_value={"source": "live", "target": "cache", "active": ["bridge-test"], "unverified": [], "copied": []},
                ),
                patch.object(doctor, "input_hashes", return_value=CAMPAIGN_HASHES),
            ):
                receipt = doctor.run_doctor(
                    "refinement",
                    "0x00401000",
                    lane="production",
                    root=root,
                    runner=runner,
                )
            receipt["checks"] = [
                check for check in receipt["checks"] if check["name"] != "reports"
            ]
            receipt["receipt_id"] = doctor._receipt_id(receipt)
            with self.assertRaisesRegex(ValueError, "check set"):
                doctor.validate_doctor_receipt(
                    receipt,
                    root=root,
                    runner=runner,
                    expected_input_hashes=CAMPAIGN_HASHES,
                )

    def test_winepath_runs_directly_with_the_project_prefix(self):
        runner = FakeRunner()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(
                doctor,
                "_find_executable",
                side_effect=lambda _root, name, local=None: f"/bin/{name}",
            ):
                checks = doctor._wine_checks(root, runner)
        self.assertTrue(all(check.ok for check in checks))
        commands = [command for command, _ in runner.calls]
        self.assertEqual(commands[0], ["/bin/wine", "--version"])
        self.assertEqual(commands[1][:2], ["/bin/winepath", "-w"])
        self.assertNotIn("winepath", commands[0])
        self.assertTrue(str(runner.calls[1][1]["env"]["WINEPREFIX"]).endswith(".tooling/wineprefix"))

    def test_native_windows_marks_wine_not_applicable(self):
        runner = FakeRunner()
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(doctor.sys, "platform", "win32"),
            patch.object(
                doctor,
                "_find_executable",
                side_effect=lambda _root, name, local=None: "C:/Windows/System32/cmd.exe"
                if name == "cmd.exe"
                else None,
            ),
        ):
            root = Path(directory)
            (root / ".tooling").mkdir()
            checks = doctor._wine_checks(root, runner)
            writable = doctor._writable_check(root)
        self.assertTrue(all(check.ok for check in checks))
        self.assertTrue(writable.ok)
        self.assertIn("not applicable", checks[0].detail)
        self.assertEqual(runner.calls, [])

    def test_stale_report_fails_freshness_check(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            report = root / "build/decomp-current-report.json"
            os.utime(report, ns=(1, 1))
            check = doctor._reports_check(root, "refinement", 0x401000)
            self.assertFalse(check.ok)
            self.assertIn("older than", check.detail)

    def test_nested_decoy_address_does_not_count_as_a_function_report_row(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            report = root / "build/decomp-current-report.json"
            report.write_text(
                json.dumps(
                    {
                        "data": [
                            {
                                "address": "0x00402000",
                                "matching": 0.5,
                                "diff": [{"address": "0x00401000"}],
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )
            check = doctor._reports_check(
                root, "refinement", [0x00401000]
            )
        self.assertFalse(check.ok)
        self.assertIn("absent from the code report", check.detail)

    def test_size_snapshot_cannot_supply_a_missing_code_report_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepare_root(root)
            (root / "build/decomp-current-report.json").write_text(
                '{"data":[{"address":"0x00402000"}]}', encoding="utf-8"
            )
            check = doctor._reports_check(root, "refinement", 0x401000)
        self.assertFalse(check.ok)
        self.assertIn("absent from the code report", check.detail)


if __name__ == "__main__":
    unittest.main()
