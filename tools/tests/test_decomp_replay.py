from __future__ import annotations

import copy
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from datetime import datetime, timedelta, timezone
import hashlib
from io import StringIO
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest import mock

from tools import decomp_campaigns as campaigns
from tools import decomp_brief as brief
from tools import decomp_context as context
from tools import decomp_doctor as doctor
from tools import decomp_experiment as experiment
from tools import decomp_replay as replay
from tools import decomp_route as route


HASH = "a" * 64
HEAD = "b" * 40


def replay_case(
    index: int = 1,
    *,
    routes: tuple[str, str] = ("abi", "call"),
    subsystem: str = "render",
) -> dict[str, object]:
    """Return one valid two-measurement terminal replay case."""

    first_route, second_route = routes
    return {
        "schema_version": 1,
        "kind": "private-replay-case",
        "case_id": f"{index:032x}",
        "provenance": {
            "target": f"0x{0x00401000 + index * 0x10:08X}",
            "session_id": f"20260909T120000Z-{index:012x}",
            "session_receipt_id": HASH,
            "campaign_id": f"campaign-{index}",
            "campaign_record_sha256": HASH,
            "delivery_receipt_sha256": HASH,
            "source_commit": HEAD,
            "context_pack_sha256": HASH,
            "impact_pack_sha256": HASH,
            "leaf_oracle_sha256": HASH,
            "trajectory_sha256": HASH,
        },
        "classification": {"subsystem": subsystem},
        "budget": {"max_trials": 3, "max_non_improving": 2},
        "graph": {
            "nodes": [
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": [first_route, second_route],
                },
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": [],
                },
                {
                    "status_level": 2,
                    "score_millionths": 700_000,
                    "eligible": True,
                    "routes": [],
                },
            ],
            "edges": [
                {"sequence": 1, "from": 0, "to": 1, "route": first_route},
                {"sequence": 2, "from": 0, "to": 2, "route": second_route},
            ],
        },
    }


def capped_replay_case(*, reset_after_miss: bool = False) -> dict[str, object]:
    """Return one valid three-measurement case stopped by the trial cap."""

    case = replay_case()
    if reset_after_miss:
        # miss from baseline, improve from baseline, then miss from the new
        # incumbent.  This proves that route-attempt state resets on promotion.
        case["graph"] = {
            "nodes": [
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": ["abi", "call"],
                },
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": False,
                    "routes": [],
                },
                {
                    "status_level": 1,
                    "score_millionths": 700_000,
                    "eligible": True,
                    "routes": ["abi"],
                },
                {
                    "status_level": 1,
                    "score_millionths": 650_000,
                    "eligible": True,
                    "routes": [],
                },
            ],
            "edges": [
                {"sequence": 1, "from": 0, "to": 1, "route": "abi"},
                {"sequence": 2, "from": 0, "to": 2, "route": "call"},
                {"sequence": 3, "from": 2, "to": 3, "route": "abi"},
            ],
        }
    else:
        # Improve first, then exhaust two distinct routes from the promoted
        # incumbent.  The final state is stopped by both misses and the cap.
        case["graph"] = {
            "nodes": [
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": ["abi"],
                },
                {
                    "status_level": 1,
                    "score_millionths": 700_000,
                    "eligible": True,
                    "routes": ["call", "control-flow"],
                },
                {
                    "status_level": 1,
                    "score_millionths": 690_000,
                    "eligible": False,
                    "routes": [],
                },
                {
                    "status_level": 1,
                    "score_millionths": 680_000,
                    "eligible": True,
                    "routes": [],
                },
            ],
            "edges": [
                {"sequence": 1, "from": 0, "to": 1, "route": "abi"},
                {"sequence": 2, "from": 1, "to": 2, "route": "call"},
                {
                    "sequence": 3,
                    "from": 1,
                    "to": 3,
                    "route": "control-flow",
                },
            ],
        }
    return case


def delivery_chain() -> tuple[
    dict[str, object],
    list[dict[str, object]],
    dict[str, str],
]:
    """Return one exact five-row delivery chain and its shared identities."""

    campaign = {
        "campaign_id": "campaign-delivery",
        "mode": "refinement",
        "lane": "closure",
        "addresses": ["0x00401000"],
        "resource": None,
        "ended_at": "2026-09-09T12:00:00+00:00",
    }
    identity = {
        "finalized_hash": "c" * 64,
        "delivery_hash": "d" * 64,
        "base_commit": "e" * 40,
        "source_commit": "f" * 40,
        "receipt_path": "/repo/build/decomp-cache/delivery/campaign-delivery/"
        + "d" * 64
        + ".json",
        "receipt_created_at": "2026-09-09T12:02:30+00:00",
    }
    statuses = ("staged", "accepted", "integrated", "committed", "pushed")
    phases = ("staged", "accepted", "integrated", "commit", "push")
    rows: list[dict[str, object]] = []
    for index, (status, phase) in enumerate(zip(statuses, phases), start=1):
        timestamp = f"2026-09-09T12:0{index}:00+00:00"
        precommit = index <= 2
        row: dict[str, object] = {
            "schema_version": campaigns.SCHEMA_VERSION,
            "record_type": "delivery",
            "delivery_id": f"00000000-0000-4000-8000-{index:012d}",
            "campaign_id": campaign["campaign_id"],
            "timestamp": timestamp,
            "status": status,
            "phase": phase,
            "phase_timestamps": {phase: timestamp},
            "mode": campaign["mode"],
            "lane": campaign["lane"],
            "addresses": campaign["addresses"],
            "resource": None,
            "artifact_sha256": None,
            "receipt_sha256": (
                identity["finalized_hash"]
                if precommit
                else identity["delivery_hash"]
            ),
            "base_commit": None if precommit else identity["base_commit"],
            "commit": None if precommit else identity["source_commit"],
            "delivery_receipt_path": (
                None if precommit else identity["receipt_path"]
            ),
            "note": "",
        }
        if status == "accepted":
            row["accepted_review"] = {"content_sha256": HASH}
            row["accepted_claims"] = []
        rows.append(row)
    return campaign, rows, identity


def trajectory_identity() -> dict[str, object]:
    descriptor = {
        "path": "build/decomp-cache/experiments/receipt.json",
        "sha256": HASH,
        "bytes": 1,
        "receipt_id": HASH,
    }
    snapshot = {"src/example.cpp": HASH, "src/deleted.cpp": "missing"}
    identity: dict[str, object] = {
        "schema_version": 1,
        "kind": "experiment-trajectory-identity",
        "address": "0x00401000",
        "session_id": "20260909T120000Z-0123456789ab",
        "head": HEAD,
        "campaign": {
            "campaign_id": "campaign-1",
            "mode": "refinement",
            "lane": "closure",
            "active_addresses": ["0x00401000"],
            "campaign_head": HEAD,
            "deadline": None,
        },
        "brief": None,
        "doctor_receipt": None,
        "limits": {"max_trials": 3, "max_non_improving": 2},
        "session_receipt": dict(descriptor),
        "baseline": {
            "artifact_identity_sha256": HASH,
            "comparison_identity_sha256": HASH,
            "source_worktree_snapshot": dict(snapshot),
        },
        "trials": [
            {
                "sequence": 1,
                "trial_id": "001-first",
                "parent": "baseline",
                "route": "abi",
                "pending_receipt": dict(descriptor),
                "terminal_receipt": dict(descriptor),
                "artifact_identity_sha256": HASH,
                "comparison_identity_sha256": HASH,
                "parent_source_worktree_snapshot": dict(snapshot),
                "source_worktree_snapshot": dict(snapshot),
            },
            {
                "sequence": 2,
                "trial_id": "002-second",
                "parent": "baseline",
                "route": "call",
                "pending_receipt": dict(descriptor),
                "terminal_receipt": dict(descriptor),
                "artifact_identity_sha256": HASH,
                "comparison_identity_sha256": HASH,
                "parent_source_worktree_snapshot": dict(snapshot),
                "source_worktree_snapshot": dict(snapshot),
            },
        ],
    }
    identity["trajectory_sha256"] = experiment._snapshot_hash(identity)
    return identity


def observation(**changes: object) -> dict[str, object]:
    value: dict[str, object] = {
        "schema_version": 1,
        "kind": "route-observation",
        "routes": ["abi", "call"],
        "tried_routes": [],
        "frontier": [{"route": "abi", "count": 1}],
        "selected_trials": 0,
        "consecutive_non_improving": 0,
        "incumbent_terminal": False,
    }
    value.update(changes)
    return value


class ReplayPolicyTests(unittest.TestCase):
    def test_two_measurement_terminal_case_and_exact_quality(self):
        case = replay_case()
        self.assertIs(replay.validate_case(case), case)
        self.assertEqual(replay.quality(2, 700_000), 2_700_002)
        result = replay.replay_case(case)
        self.assertEqual(result["status"], "passed")
        self.assertTrue(result["candidate"]["terminal"])
        self.assertEqual(result["candidate_gain"], result["oracle_gain"])

    def test_early_stop_and_post_stop_sequences_are_rejected(self):
        early = replay_case()
        early["graph"]["nodes"][2].update(
            status_level=1, score_millionths=650_000
        )
        with self.assertRaises(replay.ReplayError):
            replay.validate_case(early)

        no_gain = replay_case()
        no_gain["graph"]["nodes"][2].update(
            status_level=1, score_millionths=600_000
        )
        with self.assertRaises(replay.ReplayError):
            replay.validate_case(no_gain)

        after_terminal = replay_case()
        after_terminal["graph"]["nodes"].append(
            {
                "status_level": 2,
                "score_millionths": 800_000,
                "eligible": True,
                "routes": [],
            }
        )
        after_terminal["graph"]["nodes"][0]["routes"].append("control-flow")
        after_terminal["graph"]["edges"].append(
            {"sequence": 3, "from": 0, "to": 3, "route": "control-flow"}
        )
        with self.assertRaises(replay.ReplayError):
            replay.validate_case(after_terminal)

    def test_three_measurement_cap_and_new_incumbent_route_state(self):
        for case in (capped_replay_case(), capped_replay_case(reset_after_miss=True)):
            with self.subTest(reset=case["graph"]["edges"][1]["from"] == 0):
                replay.validate_case(case)
                candidate = replay.candidate_replay(case)
                self.assertEqual(candidate["status"], "passed")
                self.assertEqual(candidate["selections"], 3)
                self.assertEqual(candidate["sequence"], [1, 2, 3])

        reset = capped_replay_case(reset_after_miss=True)
        # The first result is ineligible even though its numeric score ties the
        # baseline.  It consumes one miss and never becomes the incumbent.
        nodes = reset["graph"]["nodes"]
        edges = reset["graph"]["edges"]
        state = replay._advance(replay._initial_state(nodes), 0, nodes, edges)
        self.assertEqual(state.incumbent, 0)
        self.assertEqual(state.non_improving, 1)
        state = replay._advance(state, 1, nodes, edges)
        self.assertEqual(state.incumbent, 2)
        self.assertEqual(state.non_improving, 0)
        edge_index, reason = replay._candidate_edge(state, nodes, edges)
        self.assertEqual((edge_index, reason), (2, None))

    def test_candidate_is_incumbent_only_while_oracle_keeps_frontier(self):
        case = replay_case()
        case["graph"] = {
            "nodes": [
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": ["abi", "call", "control-flow"],
                },
                {
                    "status_level": 1,
                    "score_millionths": 700_000,
                    "eligible": True,
                    "routes": [],
                },
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": [],
                },
                {
                    "status_level": 2,
                    "score_millionths": 800_000,
                    "eligible": True,
                    "routes": [],
                },
            ],
            "edges": [
                {"sequence": 1, "from": 0, "to": 1, "route": "abi"},
                {"sequence": 2, "from": 0, "to": 2, "route": "call"},
                {
                    "sequence": 3,
                    "from": 0,
                    "to": 3,
                    "route": "control-flow",
                },
            ],
        }
        replay.validate_case(case)
        self.assertEqual(replay.candidate_replay(case)["status"], "unsupported")
        self.assertTrue(replay.exhaustive_replay(case)["terminal"])
        aggregate, results = replay._aggregate([case])
        self.assertEqual(results[0]["status"], "unsupported")
        self.assertEqual(aggregate["terminal_oracle_cases"], 1)
        self.assertEqual(aggregate["terminal_recovered"], 0)

    def test_candidate_selection_cannot_observe_hidden_child_outcomes(self):
        case = capped_replay_case(reset_after_miss=True)
        nodes = case["graph"]["nodes"]
        edges = case["graph"]["edges"]
        state = replay._initial_state(nodes)
        selected = replay._candidate_edge(state, nodes, edges)
        for node in nodes[1:]:
            node["status_level"] = 3 - int(node["status_level"])
            node["score_millionths"] = 1_000_000 - int(
                node["score_millionths"]
            )
            node["eligible"] = not bool(node["eligible"])
        self.assertEqual(replay._candidate_edge(state, nodes, edges), selected)

    def test_branch_requires_distinct_routes_from_one_parent(self):
        case = replay_case()
        case["graph"] = {
            "nodes": [
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": ["abi"],
                },
                {
                    "status_level": 1,
                    "score_millionths": 700_000,
                    "eligible": True,
                    "routes": ["call"],
                },
                {
                    "status_level": 1,
                    "score_millionths": 600_000,
                    "eligible": True,
                    "routes": [],
                },
                {
                    "status_level": 2,
                    "score_millionths": 800_000,
                    "eligible": True,
                    "routes": [],
                },
            ],
            "edges": [
                {"sequence": 1, "from": 0, "to": 1, "route": "abi"},
                {"sequence": 2, "from": 0, "to": 2, "route": "abi"},
                {"sequence": 3, "from": 1, "to": 3, "route": "call"},
            ],
        }
        with self.assertRaisesRegex(replay.ReplayError, "branch"):
            replay.validate_case(case)

    def test_strict_integer_and_section_schemas(self):
        for mutate in (
            lambda value: value.__setitem__("schema_version", True),
            lambda value: value["budget"].__setitem__("max_trials", True),
            lambda value: value["graph"]["edges"][0].__setitem__("sequence", True),
            lambda value: value["graph"]["nodes"][0].__setitem__("extra", 1),
        ):
            case = replay_case()
            mutate(case)
            with self.assertRaises(replay.ReplayError):
                replay.validate_case(case)

    def test_threshold_certificate_math_uses_distinct_cases(self):
        pairs = (
            ("abi", "call"),
            ("control-flow", "integer"),
            ("memory", "stack"),
            ("floating-point", "side-effect"),
        )
        cases = [
            replay_case(
                index,
                routes=pairs[index % len(pairs)],
                subsystem=f"subsystem-{index % 4}",
            )
            for index in range(1, 13)
        ]
        aggregate, _ = replay._aggregate(cases)
        self.assertEqual(aggregate["cases"], 12)
        self.assertEqual(aggregate["campaigns"], 12)
        self.assertEqual(aggregate["targets"], 12)
        self.assertEqual(aggregate["edges"], 24)
        self.assertEqual(replay._failure_codes(aggregate), [])
        aggregate["candidate_gain"] = aggregate["oracle_gain"] * 9 - 1
        aggregate["oracle_gain"] *= 10
        self.assertIn("insufficient-gain-capture", replay._failure_codes(aggregate))

    def test_thresholds_are_independent_and_capture_boundary_is_exact(self):
        aggregate = {
            "cases": 12,
            "campaigns": 12,
            "targets": 12,
            "subsystems": 4,
            "routes": 4,
            "edges": 24,
            "terminal_oracle_cases": 3,
            "terminal_recovered": 3,
            "invalid": 0,
            "unsupported": 0,
            "positive_gain_cases": 12,
            "oracle_gain": 10,
            "candidate_gain": 9,
        }
        self.assertEqual(replay._failure_codes(aggregate), [])
        aggregate["candidate_gain"] = 8
        self.assertIn("insufficient-gain-capture", replay._failure_codes(aggregate))
        aggregate["candidate_gain"] = 9
        aggregate["positive_gain_cases"] = 11
        self.assertIn("nonpositive-candidate-gain", replay._failure_codes(aggregate))
        aggregate["positive_gain_cases"] = 12
        aggregate["terminal_recovered"] = 2
        self.assertIn("terminal-not-recovered", replay._failure_codes(aggregate))
        aggregate["terminal_recovered"] = 3
        for field in (
            "cases",
            "campaigns",
            "targets",
            "subsystems",
            "routes",
            "edges",
            "terminal_oracle_cases",
        ):
            changed = dict(aggregate)
            changed[field] = replay.THRESHOLDS[field] - 1
            failure = (
                "minimum-terminal-oracle-cases"
                if field == "terminal_oracle_cases"
                else f"minimum-{field}"
            )
            self.assertIn(
                failure, replay._failure_codes(changed), field
            )

    def test_subsystem_slug_prevents_spelling_weighting(self):
        self.assertEqual(replay._subsystem_slug(" Render / Core "), "render-core")
        self.assertEqual(replay._subsystem_slug("RENDER---CORE"), "render-core")
        with self.assertRaises(replay.ReplayError):
            replay._subsystem_slug("---")

    def test_source_lane_mode_and_impact_classification_matrix_is_closed(self):
        self.assertEqual(
            replay.ELIGIBLE_LANE_MODES,
            {
                ("research", "coverage"),
                ("research", "refinement"),
                ("closure", "refinement"),
                ("production", "refinement"),
            },
        )
        self.assertEqual(
            replay.SOURCE_IMPACT_CLASSIFICATIONS,
            {"coverage": "added", "refinement": "improved"},
        )
        for rejected in (
            ("closure", "coverage"),
            ("production", "coverage"),
            ("data", "data"),
            ("resource", "resource"),
            ("meta", "meta"),
        ):
            self.assertNotIn(rejected, replay.ELIGIBLE_LANE_MODES)

    def test_one_invalid_selected_case_is_counted_once(self):
        entry = {"case_id": "1" * 32, "bytes": 2, "sha256": HASH}
        manifest = {
            "schema_version": 1,
            "kind": "private-replay-manifest",
            "cases": [entry],
        }
        with (
            mock.patch.object(replay, "_case_file_names", return_value={"1" * 32 + ".json"}),
            mock.patch.object(replay, "_case_bytes", return_value=b"{}"),
        ):
            cases, invalid, storage_invalid = replay._loaded_cases(
                manifest, Path("/tmp"), [], HEAD, HEAD
            )
        self.assertEqual(cases, [])
        self.assertEqual(len(invalid), 1)
        self.assertEqual(storage_invalid, 0)


class RoutePolicyTests(unittest.TestCase):
    def test_public_selector_matches_replay_candidate_observation(self):
        case = replay_case()
        nodes = case["graph"]["nodes"]
        edges = case["graph"]["edges"]
        state = replay._initial_state(nodes)
        edge_index, reason = replay._candidate_edge(state, nodes, edges)
        selected, public_reason = route.select_route(observation())
        self.assertEqual((edges[edge_index]["route"], reason), (selected, public_reason))

        state = replay._advance(state, edge_index, nodes, edges)
        edge_index, reason = replay._candidate_edge(state, nodes, edges)
        selected, public_reason = route.select_route(
            observation(
                tried_routes=["abi"],
                frontier=[{"route": "call", "count": 1}],
                selected_trials=1,
                consecutive_non_improving=1,
            )
        )
        self.assertEqual((edges[edge_index]["route"], reason), (selected, public_reason))

        duplicate_edges = [*edges, {"sequence": 3, "from": 0, "to": 2, "route": "abi"}]
        state = replay._initial_state(nodes)
        self.assertEqual(
            replay._candidate_edge(state, nodes, duplicate_edges),
            (None, "ambiguous-route"),
        )
        self.assertEqual(
            route.select_route(observation(frontier=[{"route": "abi", "count": 2}])),
            (None, "ambiguous-route"),
        )

    def test_unique_ambiguous_missing_tried_and_stop_states(self):
        self.assertEqual(route.select_route(observation()), ("abi", None))
        self.assertEqual(
            route.select_route(observation(frontier=[{"route": "abi", "count": 2}])),
            (None, "ambiguous-route"),
        )
        self.assertEqual(
            route.select_route(observation(frontier=[])),
            (None, "missing-incumbent-route"),
        )
        tried = observation(
            tried_routes=["abi"],
            frontier=[{"route": "call", "count": 1}],
            selected_trials=1,
            consecutive_non_improving=1,
        )
        self.assertEqual(route.select_route(tried), ("call", None))
        for value, reason in (
            (observation(incumbent_terminal=True), "terminal-incumbent"),
            (observation(selected_trials=3), "trial-budget-exhausted"),
            (
                observation(
                    tried_routes=["abi", "call"],
                    frontier=[],
                    selected_trials=2,
                    consecutive_non_improving=2,
                ),
                "non-improvement-budget-exhausted",
            ),
        ):
            self.assertEqual(route.select_route(value), (None, reason))

    def test_observation_is_strict_and_reachable(self):
        for value in (
            observation(schema_version=True),
            observation(selected_trials=True),
            observation(
                routes=["abi"],
                frontier=[{"route": "call", "count": 1}],
            ),
            observation(
                tried_routes=["abi"],
                selected_trials=1,
                consecutive_non_improving=0,
                frontier=[],
            ),
        ):
            with self.assertRaises(route.RouteError):
                route.validate_observation(value)

    def test_cli_checks_certificate_before_observation_and_withholds_invalid(self):
        output = StringIO()
        with (
            mock.patch.object(route, "current_passing_certificate", return_value=None),
            mock.patch.object(
                route, "_read_observation", side_effect=AssertionError("private sentinel")
            ),
            redirect_stdout(output),
        ):
            self.assertEqual(route.main(["advise", "--observation", "/private/sentinel", "--json"]), 0)
        self.assertEqual(json.loads(output.getvalue())["reason"], "private-replay-not-certified")
        self.assertNotIn("sentinel", output.getvalue())

        output = StringIO()
        error = StringIO()
        with (
            mock.patch.object(route, "current_passing_certificate", return_value={"status": "passed"}),
            mock.patch.object(route, "_read_observation", side_effect=route.RouteError("private sentinel")),
            redirect_stdout(output),
            redirect_stderr(error),
        ):
            self.assertEqual(route.main(["advise", "--observation", "/private/sentinel", "--json"]), 0)
        self.assertEqual(json.loads(output.getvalue())["reason"], "invalid-observation")
        self.assertEqual(error.getvalue(), "")

    def test_unexpected_public_failure_is_generic(self):
        output = StringIO()
        error = StringIO()
        with (
            mock.patch.object(
                route,
                "current_passing_certificate",
                side_effect=RuntimeError("private sentinel /secret/path"),
            ),
            redirect_stdout(output),
            redirect_stderr(error),
        ):
            self.assertEqual(route.main(["advise", "--json"]), 1)
        self.assertEqual(output.getvalue(), "")
        self.assertEqual(error.getvalue(), "error: the route advice operation failed\n")


class TrajectorySchemaTests(unittest.TestCase):
    def test_exact_seal_schema_and_deleted_source_snapshot(self):
        identity = trajectory_identity()
        self.assertIs(experiment._validate_trajectory_identity(identity), identity)
        seal: dict[str, object] = {
            "schema_version": 1,
            "kind": "experiment-trajectory-seal",
            "trajectory": identity,
        }
        seal["content_sha256"] = experiment._trajectory_seal_hash(seal)
        self.assertIs(experiment._validate_trajectory_seal_document(seal), seal)

    def test_extra_missing_and_boolean_trajectory_fields_are_rejected(self):
        mutations = (
            lambda value: value.__setitem__("extra", "private"),
            lambda value: value.pop("brief"),
            lambda value: value["trials"][0].__setitem__("extra", 1),
            lambda value: value["trials"][0].__setitem__("sequence", True),
            lambda value: value["baseline"]["source_worktree_snapshot"].__setitem__(
                "src/link.cpp", "symlink:outside"
            ),
        )
        for mutate in mutations:
            identity = trajectory_identity()
            mutate(identity)
            identity["trajectory_sha256"] = experiment._snapshot_hash(
                {key: item for key, item in identity.items() if key != "trajectory_sha256"}
            )
            with self.assertRaises(experiment.ExperimentError):
                experiment._validate_trajectory_identity(identity)

    def test_private_trajectory_publish_read_and_binding_are_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(
                ["git", "init", "-q", "-b", "agent/continuous"],
                cwd=root,
                check=True,
            )
            (root / ".gitignore").write_text(
                "/.decomp-replay/\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", ".gitignore"], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Replay Tests",
                    "-c",
                    "user.email=replay@example.invalid",
                    "commit",
                    "-qm",
                    "Ignore private replay data",
                ],
                cwd=root,
                check=True,
            )
            identity = trajectory_identity()
            seal: dict[str, object] = {
                "schema_version": 1,
                "kind": "experiment-trajectory-seal",
                "trajectory": identity,
            }
            seal["content_sha256"] = experiment._trajectory_seal_hash(seal)
            binding = replay.publish_private_trajectory(seal, root=root)
            self.assertEqual(
                experiment.trajectory_from_binding(binding, root=root), seal
            )
            self.assertEqual(
                replay.read_private_trajectory(binding, root=root), seal
            )

            for name, mutate in (
                ("extra", lambda value: value.__setitem__("extra", True)),
                ("missing", lambda value: value.pop("bytes")),
                (
                    "kind",
                    lambda value: value.__setitem__(
                        "kind", "experiment-trajectory-commitment"
                    ),
                ),
                (
                    "path",
                    lambda value: value.__setitem__("content_sha256", "b" * 64),
                ),
            ):
                changed = dict(binding)
                mutate(changed)
                with self.subTest(name=name), self.assertRaises(
                    experiment.ExperimentError
                ):
                    experiment.validate_trajectory_binding(changed, root=root)

            for name, mutate in (
                ("seal-extra", lambda value: value.__setitem__("extra", True)),
                ("seal-missing", lambda value: value.pop("trajectory")),
            ):
                changed_seal = copy.deepcopy(seal)
                mutate(changed_seal)
                changed_seal["content_sha256"] = experiment._trajectory_seal_hash(
                    changed_seal
                )
                with self.subTest(name=name), self.assertRaises(
                    experiment.ExperimentError
                ):
                    experiment._validate_trajectory_seal_document(changed_seal)

            path = (
                root
                / replay.PRIVATE_RELATIVE
                / replay.TRAJECTORIES_NAME
                / f"{binding['content_sha256']}.json"
            )
            path.write_bytes(path.read_bytes() + b" ")
            with self.assertRaises(experiment.ExperimentError):
                experiment.validate_trajectory_binding(binding, root=root)


class ReconstructionEligibilityTests(unittest.TestCase):
    def reconstruct(
        self,
        root: Path,
        outcomes: list[dict[str, object]],
        *,
        states: list[str] | None = None,
        pending_parent_override: object = None,
    ) -> tuple[dict[str, object], mock.Mock]:
        target = "0x00401000"
        session_id = "20260909T120000Z-000000000001"
        baseline_snapshot = {"src/base.cpp": HASH}
        source_snapshots = [
            {"src/base.cpp": chr(ord("a") + index) * 64}
            for index in range(1, len(outcomes) + 1)
        ]
        session = {
            "session_id": session_id,
            "receipt_id": HASH,
            "limits": {"max_trials": 3, "max_non_improving": 2},
            experiment.SOURCE_SNAPSHOT_FIELD: baseline_snapshot,
            "comparison_identity": {
                "source_worktree_sha256": experiment._snapshot_hash(
                    baseline_snapshot
                )
            },
            "artifacts": {"baseline_report": {}},
        }
        baseline = {"status": "provisional", "score": 0.6, "eligible": True}
        routes = ["abi", "call", "control-flow"]
        receipts: list[dict[str, object]] = []
        pendings: list[dict[str, object]] = []
        trials: list[tuple[Path, dict[str, object], str]] = []
        for sequence, outcome in enumerate(outcomes, 1):
            trial_id = f"{sequence:03d}-trial"
            parent = str(outcome.get("parent", "baseline"))
            parent_snapshot = baseline_snapshot
            if parent != "baseline":
                parent_index = int(parent.split("-", 1)[0]) - 1
                parent_snapshot = source_snapshots[parent_index]
            pending_id = chr(ord("d") + sequence) * 64
            receipt = {
                "sequence": sequence,
                "result": "comparison",
                "eligible": outcome.get("eligible", True),
                "address": target,
                "session_id": session_id,
                "session_receipt_id": HASH,
                "trial_id": trial_id,
                "route": routes[sequence - 1],
                "parent": parent,
                experiment.PARENT_SNAPSHOT_FIELD: parent_snapshot,
                "pending_receipt_id": pending_id,
                "comparison_identity": {"trial": sequence},
            }
            pending = {
                "receipt_id": pending_id,
                experiment.PARENT_SNAPSHOT_FIELD: (
                    pending_parent_override
                    if pending_parent_override is not None and sequence == 1
                    else parent_snapshot
                ),
            }
            receipt.update(outcome.get("receipt_changes", {}))
            receipts.append(receipt)
            pendings.append(pending)
            trials.append(
                (
                    root / "sessions" / "trials" / trial_id,
                    receipt,
                    (states or ["completed"] * len(outcomes))[sequence - 1],
                )
            )
        trajectory = {"trajectory_sha256": HASH}
        evidence = mock.Mock(
            return_value=(
                {
                    "target": target,
                    "session_id": session_id,
                    "session_receipt_id": HASH,
                    "campaign_id": "campaign-source",
                    "campaign_record_sha256": HASH,
                    "delivery_receipt_sha256": HASH,
                    "source_commit": HEAD,
                    "context_pack_sha256": HASH,
                    "impact_pack_sha256": HASH,
                    "leaf_oracle_sha256": HASH,
                },
                {"subsystem": "render"},
            )
        )
        taxonomies = [
            {
                "schema_version": 1,
                "signals": [{"route": route} for route in routes],
            },
            *(
                {"schema_version": 1, "signals": []}
                for _ in outcomes
            ),
        ]
        recomputed = [
            (
                {
                    "status": str(outcome["status"]),
                    "score": outcome["score"],
                    "eligible": outcome.get("eligible", True),
                },
                taxonomies[index + 1],
            )
            for index, outcome in enumerate(outcomes)
        ]
        with (
            mock.patch.object(experiment, "trajectory_identity", return_value=trajectory),
            mock.patch.object(experiment, "read_session", return_value=(root / "sessions", session)),
            mock.patch.object(experiment, "_trial_receipts", return_value=trials),
            mock.patch.object(experiment, "_baseline_candidate", return_value=baseline),
            mock.patch.object(experiment, "_validate_report_descriptor", return_value=(root / "report.json", {})),
            mock.patch.object(experiment, "read_receipt", side_effect=pendings),
            mock.patch.object(experiment, "_validate_completed_artifacts"),
            mock.patch.object(experiment, "_receipt_source_snapshot", side_effect=source_snapshots),
            mock.patch.object(replay, "_recomputed_trial", side_effect=recomputed),
            mock.patch.object(replay, "_verified_campaign_evidence", evidence),
            mock.patch("tools.decomp_verify.experiment_record", return_value=taxonomies[0] | {"mismatch_taxonomy": taxonomies[0]}),
        ):
            result = replay.reconstruct_case(
                "1" * 32,
                target,
                session_id,
                root=root,
                records=[],
                current_head=HEAD,
                origin_head=HEAD,
                assume_private_lock=True,
            )
        return result, evidence

    def test_prepared_parent_and_unique_best_are_wired_to_source_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            case, evidence = self.reconstruct(
                Path(directory),
                [
                    {"status": "provisional", "score": 0.6},
                    {"status": "effective", "score": 0.7},
                ],
            )
        replay.validate_case(case)
        self.assertEqual(case["graph"]["edges"][1]["from"], 0)
        self.assertEqual(evidence.call_args.args[3], {"src/base.cpp": "c" * 64})

    def test_tied_best_failed_pending_missing_score_and_parent_tamper_reject(self):
        failures = (
            (
                [
                    {"status": "provisional", "score": 0.7},
                    {"status": "provisional", "score": 0.7},
                ],
                None,
                None,
            ),
            (
                [
                    {"status": "provisional", "score": 0.6},
                    {"status": "effective", "score": 0.7},
                ],
                ["failed", "completed"],
                None,
            ),
            (
                [
                    {"status": "provisional", "score": 0.6},
                    {"status": "effective", "score": None},
                ],
                None,
                None,
            ),
            (
                [
                    {"status": "provisional", "score": 0.6},
                    {"status": "effective", "score": 0.7},
                ],
                None,
                {"src/base.cpp": "f" * 64},
            ),
        )
        for outcomes, states, parent_override in failures:
            with self.subTest(states=states, parent=parent_override), tempfile.TemporaryDirectory() as directory:
                with self.assertRaises(replay.ReplayError):
                    self.reconstruct(
                        Path(directory),
                        outcomes,
                        states=states,
                        pending_parent_override=parent_override,
                    )


class VerifiedCampaignEvidenceTests(unittest.TestCase):
    def fixture(self, *, mode: str = "refinement") -> dict[str, object]:
        target = "0x00401000"
        other = "0x00402000"
        lane = "research" if mode == "coverage" else "closure"
        snapshot = {"src/Target.cpp": HASH}
        brief_descriptor = {
            "target": target,
            "path": "/repo/brief.json",
            "sha256": HASH,
            "doctor_receipt": {
                "path": "/repo/doctor.json",
                "sha256": HASH,
            },
        }
        campaign = {
            "schema_version": campaigns.SCHEMA_VERSION,
            "record_type": "campaign",
            "campaign_id": "campaign-source",
            "campaign_head": HEAD,
            "result": "source",
            "mode": mode,
            "lane": lane,
            "active_addresses": [target, other],
            "impact_review_required": True,
            "leaf_oracle_required": True,
            "briefs": [brief_descriptor],
            "replay_experiment": None,
            "subsystem": "Render/Core",
        }
        session_tools = {name: HASH for name in experiment.TOOL_FILES}
        session = {
            "session_id": "20260909T120000Z-000000000001",
            "receipt_id": HASH,
            "head": HEAD,
            "campaign": {
                "campaign_id": campaign["campaign_id"],
                "mode": mode,
                "lane": lane,
                "active_addresses": campaign["active_addresses"],
                "campaign_head": HEAD,
            },
            "tool_identity": session_tools,
            "brief": brief_descriptor,
            "doctor_receipt": brief_descriptor["doctor_receipt"],
        }
        key_payload = {
            "source_root": "/repo",
            "campaign_ledger_relative": replay.LEDGER_RELATIVE.as_posix(),
            "campaign_ledger_base_commit": HEAD,
            "briefs": campaign["briefs"],
            "active_addresses": campaign["active_addresses"],
            "campaign_id": campaign["campaign_id"],
            "campaign_head": HEAD,
            "mode": mode,
            "lane": lane,
            "source_worktree_snapshot": snapshot,
            "source_worktree_sha256": campaigns._snapshot_hash(snapshot),
            "replay_experiment": None,
            "inputs": {},
        }
        finalize = {
            "receipt_version": campaigns.FINALIZE_RECEIPT_VERSION,
            "receipt_sha256": "d" * 64,
            "key_payload": key_payload,
        }
        classification = replay.SOURCE_IMPACT_CLASSIFICATIONS[mode]
        impact = {
            "content_sha256": "e" * 64,
            "comparison_changes": [
                {
                    "address": target,
                    "target": True,
                    "classification": classification,
                    "improvement": True,
                    "after": {
                        "exact": False,
                        "effective": True,
                        "matching": 0.7,
                    },
                }
            ],
        }
        comparison = {
            "validation_tools": {
                "reccmp_user": {"config_sha256": HASH},
            }
        }
        return {
            "target": target,
            "other": other,
            "snapshot": snapshot,
            "campaign": campaign,
            "session": session,
            "finalize": finalize,
            "impact": impact,
            "leaf": {"content_sha256": "f" * 64},
            "comparison": comparison,
            "trajectory": trajectory_identity(),
            "sealed_trajectory": trajectory_identity(),
        }

    def verified(self, value: dict[str, object]) -> tuple[dict[str, object], dict[str, object]]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(
                ["git", "init", "-q", "-b", "agent/continuous"],
                cwd=root,
                check=True,
            )
            (root / ".gitignore").write_text(
                "/.decomp-replay/\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", ".gitignore"], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Replay Tests",
                    "-c",
                    "user.email=replay@example.invalid",
                    "commit",
                    "-qm",
                    "Ignore private replay data",
                ],
                cwd=root,
                check=True,
            )
            sealed_trajectory = value["sealed_trajectory"]
            seal: dict[str, object] = {
                "schema_version": 1,
                "kind": "experiment-trajectory-seal",
                "trajectory": sealed_trajectory,
            }
            seal["content_sha256"] = experiment._trajectory_seal_hash(seal)
            binding = replay.publish_private_trajectory(seal, root=root)
            campaign = value["campaign"]
            finalize = value["finalize"]
            key_payload = finalize["key_payload"]
            campaign["replay_experiment"] = binding
            key_payload["replay_experiment"] = (
                {**binding, "sha256": "b" * 64}
                if value.get("row_key_commitment_mismatch") is True
                else binding
            )
            key_payload["source_root"] = str(root.resolve())
            impact = value["impact"]
            leaf = value["leaf"]
            trajectory = value["trajectory"]
            with (
                mock.patch.object(replay, "_git_ancestor", return_value=True),
                mock.patch.object(replay, "_historical_files"),
                mock.patch.object(replay, "_historical_validation_identity"),
                mock.patch.object(replay, "_same_artifact_descriptor", return_value=True),
                mock.patch.object(replay, "_preflight_campaign_receipts"),
                mock.patch.object(replay, "_validate_trajectory_chronology"),
                mock.patch.object(replay, "_validate_finalization_inputs"),
                mock.patch.object(replay, "_brief_context", return_value=(HASH, HASH)),
                mock.patch.object(replay, "_delivery_evidence", return_value=(HEAD, HASH)),
                mock.patch.object(campaigns, "_validate_campaign_record_receipt"),
                mock.patch.object(campaigns, "_campaign_finalize_receipt", return_value=finalize),
                mock.patch.object(campaigns, "_leaf_oracle_artifact", return_value=(leaf, {})),
                mock.patch("tools.decomp_impact.impact_artifact", return_value=(impact, {})),
            ):
                return replay._verified_campaign_evidence(
                    value["session"],
                    str(value["target"]),
                    {"status_level": 2, "score_millionths": 700_000},
                    value["snapshot"],
                    value["comparison"],
                    root,
                    HEAD,
                    HEAD,
                    trajectory,
                    True,
                    [campaign],
                )

    def test_retained_target_lane_impact_and_commitment_are_exact(self):
        for mode in ("coverage", "refinement"):
            with self.subTest(mode=mode):
                provenance, classification = self.verified(self.fixture(mode=mode))
                self.assertEqual(provenance["target"], "0x00401000")
                self.assertEqual(classification, {"subsystem": "render-core"})

        def wrong_score(value: dict[str, object]) -> None:
            value["impact"]["comparison_changes"][0]["after"]["matching"] = 0.69

        def wrong_status(value: dict[str, object]) -> None:
            value["impact"]["comparison_changes"][0]["after"]["effective"] = False

        def wrong_snapshot(value: dict[str, object]) -> None:
            value["finalize"]["key_payload"]["source_worktree_snapshot"] = {
                "src/Target.cpp": "b" * 64
            }

        def other_target_only(value: dict[str, object]) -> None:
            value["impact"]["comparison_changes"][0]["address"] = value["other"]

        def wrong_impact(value: dict[str, object]) -> None:
            value["impact"]["comparison_changes"][0]["classification"] = "added"

        def wrong_lane_mode(value: dict[str, object]) -> None:
            value["campaign"]["mode"] = "coverage"
            value["session"]["campaign"]["mode"] = "coverage"
            value["finalize"]["key_payload"]["mode"] = "coverage"

        def row_key_commitment_mismatch(value: dict[str, object]) -> None:
            value["row_key_commitment_mismatch"] = True

        def seal_trajectory_mismatch(value: dict[str, object]) -> None:
            value["sealed_trajectory"] = {"trajectory_sha256": "b" * 64}

        for name, mutate in (
            ("score", wrong_score),
            ("status", wrong_status),
            ("snapshot", wrong_snapshot),
            ("other-target", other_target_only),
            ("impact", wrong_impact),
            ("lane-mode", wrong_lane_mode),
            ("row-key-commitment", row_key_commitment_mismatch),
            ("seal-trajectory", seal_trajectory_mismatch),
        ):
            value = self.fixture()
            mutate(value)
            with self.subTest(name=name), self.assertRaises(replay.ReplayError):
                self.verified(value)


class DeliveryEvidenceTests(unittest.TestCase):
    def validate_rows(
        self,
        campaign: dict[str, object],
        rows: list[dict[str, object]],
        identity: dict[str, str],
    ) -> None:
        replay._validate_delivery_rows(
            campaign,
            replay._ordered_delivery_rows(campaign, rows),
            **identity,
        )

    def test_exact_five_row_delivery_chain_and_shared_identity(self):
        campaign, rows, identity = delivery_chain()
        self.validate_rows(campaign, rows, identity)

        mutations = {
            "missing": lambda value: value.pop(),
            "extra": lambda value: value.append(copy.deepcopy(value[-1])),
            "reordered": lambda value: value.__setitem__(
                slice(0, 2), [value[1], value[0]]
            ),
            "duplicate-status": lambda value: value[1].__setitem__(
                "status", "staged"
            ),
        }
        for name, mutate in mutations.items():
            changed = copy.deepcopy(rows)
            mutate(changed)
            with self.subTest(name=name), self.assertRaises(replay.ReplayError):
                self.validate_rows(campaign, changed, identity)

        row_mutations = {
            "duplicate-id": lambda value: value[4].__setitem__(
                "delivery_id", value[3]["delivery_id"]
            ),
            "extra-key": lambda value: value[0].__setitem__("extra", True),
            "receipt": lambda value: value[4].__setitem__(
                "receipt_sha256", HASH
            ),
            "base": lambda value: value[3].__setitem__("base_commit", HEAD),
            "commit": lambda value: value[2].__setitem__("commit", HEAD),
            "path": lambda value: value[4].__setitem__(
                "delivery_receipt_path", "/different.json"
            ),
            "time-order": lambda value: value[3].__setitem__(
                "timestamp", "2026-09-09T12:02:00+00:00"
            ),
        }
        for name, mutate in row_mutations.items():
            changed = copy.deepcopy(rows)
            mutate(changed)
            if name == "time-order":
                changed[3]["phase_timestamps"] = {
                    "commit": changed[3]["timestamp"]
                }
            with self.subTest(name=name), self.assertRaises(replay.ReplayError):
                self.validate_rows(campaign, changed, identity)

        changed_identity = dict(identity)
        changed_identity["receipt_created_at"] = "2026-09-09T12:05:30+00:00"
        with self.assertRaises(replay.ReplayError):
            self.validate_rows(campaign, copy.deepcopy(rows), changed_identity)

    def test_source_commit_first_parent_and_both_tip_ancestry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(
                ["git", "init", "-q", "-b", "agent/continuous"],
                cwd=root,
                check=True,
            )
            commits: list[str] = []
            tracked = root / "history.txt"
            (root / ".gitignore").write_text(
                "/.decomp-replay/\n", encoding="utf-8"
            )
            subprocess.run(["git", "add", ".gitignore"], cwd=root, check=True)
            for index in range(4):
                tracked.write_text(f"history {index}\n", encoding="utf-8")
                subprocess.run(["git", "add", "history.txt"], cwd=root, check=True)
                subprocess.run(
                    [
                        "git",
                        "-c",
                        "user.name=Replay Tests",
                        "-c",
                        "user.email=replay@example.invalid",
                        "commit",
                        "-qm",
                        f"history {index}",
                    ],
                    cwd=root,
                    check=True,
                )
                commits.append(
                    subprocess.run(
                        ["git", "rev-parse", "HEAD"],
                        cwd=root,
                        check=True,
                        capture_output=True,
                        text=True,
                    ).stdout.strip()
                )
            campaign_head, base_commit, source_commit, current_head = commits
            replay._validate_delivery_ancestry(
                root,
                campaign_head,
                base_commit,
                source_commit,
                current_head,
                current_head,
            )

            subprocess.run(
                ["git", "checkout", "-q", "--detach", base_commit],
                cwd=root,
                check=True,
            )
            tracked.write_text("diverged\n", encoding="utf-8")
            subprocess.run(["git", "add", "history.txt"], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Replay Tests",
                    "-c",
                    "user.email=replay@example.invalid",
                    "commit",
                    "-qm",
                    "diverged tip",
                ],
                cwd=root,
                check=True,
            )
            divergent = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            for name, changed_base, changed_current, changed_origin in (
                ("wrong-first-parent", campaign_head, current_head, current_head),
                ("source-not-on-head", base_commit, divergent, current_head),
                ("source-not-on-origin", base_commit, current_head, divergent),
            ):
                with self.subTest(name=name), self.assertRaises(replay.ReplayError):
                    replay._validate_delivery_ancestry(
                        root,
                        campaign_head,
                        changed_base,
                        source_commit,
                        changed_current,
                        changed_origin,
                    )

            subprocess.run(
                ["git", "checkout", "-q", "--detach", base_commit],
                cwd=root,
                check=True,
            )
            leaked = root / ".decomp-replay/cases/leaked.json"
            leaked.parent.mkdir(parents=True)
            leaked.write_text("private\n", encoding="utf-8")
            subprocess.run(
                ["git", "add", "-f", ".decomp-replay/cases/leaked.json"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Replay Tests",
                    "-c",
                    "user.email=replay@example.invalid",
                    "commit",
                    "-qm",
                    "leak private data",
                ],
                cwd=root,
                check=True,
            )
            leaked_source = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            subprocess.run(
                ["git", "rm", "-q", "--cached", ".decomp-replay/cases/leaked.json"],
                cwd=root,
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Replay Tests",
                    "-c",
                    "user.email=replay@example.invalid",
                    "commit",
                    "-qm",
                    "remove private data",
                ],
                cwd=root,
                check=True,
            )
            clean_tip = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            with self.assertRaisesRegex(replay.ReplayError, "private replay"):
                replay._validate_delivery_ancestry(
                    root,
                    campaign_head,
                    base_commit,
                    leaked_source,
                    clean_tip,
                    clean_tip,
                )


class HistoricalBindingTests(unittest.TestCase):
    def historical_doctor(
        self,
        root: Path,
        addresses: list[str],
        *,
        runtime_check: str,
    ) -> tuple[dict[str, object], dict[str, object], bytes]:
        map_content = b"0x00401000 test\n"
        retail_hash = "d" * 64
        artifacts: dict[str, dict[str, dict[str, object]]] = {}
        for target in addresses:
            artifacts[target] = {
                kind: doctor._store_ghidra_artifact(
                    root, target, kind, {"rows": [target, kind]}
                )
                for kind in sorted(doctor.SOURCE_ARTIFACT_KINDS)
            }
        counts = doctor._required_check_counts("refinement", len(addresses))
        counts.pop("native-build-runtime", None)
        counts.pop("wine-prefix", None)
        counts[runtime_check] = 1
        branch = {"actual": "agent/continuous", "expected": "agent/continuous"}
        origin = {
            "fetched": True,
            "head": HEAD,
            "remote": HEAD,
            "remote_ref": "refs/remotes/origin/agent/continuous",
        }
        checks: list[dict[str, object]] = []
        artifact_offsets = {kind: 0 for kind in doctor.SOURCE_ARTIFACT_KINDS}
        for name in sorted(counts):
            for _ in range(counts[name]):
                if name in doctor.SOURCE_ARTIFACT_KINDS:
                    target = addresses[artifact_offsets[name]]
                    artifact_offsets[name] += 1
                    data: object = {
                        "target": target,
                        "artifact": artifacts[target][name],
                    }
                elif name == "branch":
                    data = branch
                elif name == "head":
                    data = {"actual": HEAD}
                elif name == "origin-integration":
                    data = origin
                elif name == "selection":
                    data = {"addresses": addresses, "resource": None}
                else:
                    data = {}
                checks.append({"name": name, "ok": True, "detail": "ready", "data": data})
        input_hashes = {
            name: HASH
            for name in {
                "source_worktree_sha256",
                "source_index_sha256",
                "repository_worktree_sha256",
                "repository_index_sha256",
                "resource_sources_sha256",
                "functions_map_sha256",
                "function_sizes_file_sha256",
                "retail_executable_sha256",
                "recompiled_executable_sha256",
                "recompiled_symbols_sha256",
                "reccmp_build_sha256",
                "reccmp_user_sha256",
                "current_report_sha256",
                "current_report_provenance_sha256",
                "current_data_report_sha256",
                "current_data_report_provenance_sha256",
                "configured_retail_executable_sha256",
            }
        }
        input_hashes["functions_map_sha256"] = hashlib.sha256(map_content).hexdigest()
        input_hashes["retail_executable_sha256"] = retail_hash
        input_hashes["configured_retail_executable_sha256"] = retail_hash
        selection = "2026-09-09T12:00:00.123Z"
        started = "2026-09-09T12:01:00.456Z"
        ended = "2026-09-09T12:02:00.789Z"
        receipt: dict[str, object] = {
            "schema": 1,
            "status": "ready",
            "ok": True,
            "campaign_timing_started": False,
            "root": str(root.resolve()),
            "mode": "refinement",
            "lane": "closure",
            "addresses": addresses,
            "resource": None,
            "target": addresses[0] if len(addresses) == 1 else None,
            "branch": branch,
            "head": HEAD,
            "origin_integration": origin,
            "checks": checks,
            "input_hashes": input_hashes,
            "selection_started_at": selection,
            "started_at": started,
            "ended_at": ended,
            "doctor_started_at": started,
            "doctor_ended_at": ended,
            "elapsed_seconds": 60.0,
            "receipt_id": "",
            "receipt_path": "",
        }
        receipt_id = doctor._receipt_id(receipt)
        receipt["receipt_id"] = receipt_id
        receipt["receipt_path"] = (
            doctor.RECEIPT_DIRECTORY_RELATIVE / f"{receipt_id}.json"
        ).as_posix()
        content = json.dumps(receipt, indent=2, sort_keys=True).encode() + b"\n"
        path = root / str(receipt["receipt_path"])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        descriptor = {
            "path": str(path.resolve()),
            "sha256": hashlib.sha256(content).hexdigest(),
            "receipt_id": receipt_id,
            "mode": "refinement",
            "lane": "closure",
            "source_artifacts": artifacts[addresses[0]],
        }
        campaign_identity = {
            "path": str(path.resolve()),
            "sha256": descriptor["sha256"],
            "receipt_id": receipt_id,
            "head": HEAD,
            "lane": "closure",
            "mode": "refinement",
            "addresses": addresses,
            "resource": None,
            "input_hashes": input_hashes,
            "status": "ready",
        }
        campaign = {
            "mode": "refinement",
            "lane": "closure",
            "started_at": "2026-09-09T12:03:00+00:00",
            "doctor_receipt": campaign_identity,
            "doctor_receipts": [campaign_identity],
            "selection_started_at": "2026-09-09T12:00:00+00:00",
            "doctor_started_at": "2026-09-09T12:01:00+00:00",
            "doctor_ended_at": "2026-09-09T12:02:00+00:00",
            "target_events": [],
        }
        return descriptor, campaign, map_content

    def historical_brief(
        self,
        root: Path,
        addresses: list[str],
    ) -> tuple[dict[str, object], dict[str, object], object]:
        target = addresses[0]
        doctor_descriptor, campaign, map_content = self.historical_doctor(
            root, addresses, runtime_check="wine-prefix"
        )
        tracked = b"tracked historical input\n"
        ledger = b'{"record_type":"fixture"}\n'
        blockers = b""
        artifacts = b""
        lint = b""

        def git_blob(
            _root: Path,
            _revision: str,
            relative: Path,
            _maximum: int,
            **_kwargs: object,
        ) -> bytes:
            values = {
                Path("tools/Resources/functions_map.txt"): map_content,
                replay.LEDGER_RELATIVE: ledger,
                Path("tools/Resources/reconstruction-blockers.tsv"): blockers,
                Path("tools/Resources/tool_artifacts.tsv"): artifacts,
                Path(".notes/lint-baseline.tsv"): lint,
            }
            return values.get(Path(relative), tracked)

        doctor_id = doctor_descriptor["receipt_id"]
        doctor_hash = doctor_descriptor["sha256"]
        scout_descriptors: list[dict[str, object]] = []
        for scout_id, audit in (
            ("retail-scout", "retail-abi-control-flow-evidence"),
            ("context-scout", "callers-types-layout-translation-unit-analogue"),
        ):
            findings = [
                {
                    "category": category,
                    "claim": f"Supported {category} evidence.",
                    "evidence": [
                        {"source": "retail", "locator": f"{target}:{category}"}
                    ],
                }
                for category in sorted(brief.SCOUT_AUDIT_CATEGORIES[audit])
            ]
            report = {
                "schema": brief.SCOUT_REPORT_SCHEMA,
                "scout_id": scout_id,
                "audit": audit,
                "lane": "closure",
                "target": target,
                "access": "read-only",
                "owns_mutations": False,
                "doctor_receipt": {
                    "receipt_id": doctor_id,
                    "sha256": doctor_hash,
                },
                "findings": findings,
            }
            report_path = root / "build/decomp-cache/scouts" / f"{scout_id}.json"
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text(json.dumps(report), encoding="utf-8")
            scout_descriptors.append(
                brief._scout_report_descriptor(
                    root, "closure", target, report_path, doctor_descriptor
                )
            )
        decoder = {
            "engine": "capstone",
            "version": "5.0.9",
            "pinned_version": "5.0.9",
            "architecture": "x86",
            "mode": 32,
            "detail": True,
        }
        context_tools = (
            "tools/__init__.py",
            "tools/decomp_context.py",
            "tools/decomp_annotations.py",
            "tools/decomp_dependencies.py",
            "tools/decomp_binary.py",
            "tools/decomp_verify.py",
            "tools/decomp_lint.py",
            "tools/decomp_status.py",
            "tools/decomp_campaigns.py",
        )
        context_inputs = {
            "decoder": decoder,
            "retail_image": {"path": "original/toy2.exe", "sha256": "d" * 64},
            "repository": {
                "function_map_sha256": hashlib.sha256(map_content).hexdigest(),
                "function_sizes_sha256": HASH,
                "report_sha256": HASH,
                "ledger_sha256": hashlib.sha256(ledger).hexdigest(),
                "lint_baseline_sha256": hashlib.sha256(lint).hexdigest(),
            },
            "tools": {
                name: hashlib.sha256(tracked).hexdigest() for name in context_tools
            },
        }
        tool_digest = hashlib.sha256()
        for relative in sorted(brief.TOOL_INPUTS, key=str):
            tool_digest.update(relative.as_posix().encode("utf-8"))
            tool_digest.update(
                hashlib.sha256(git_blob(root, HEAD, relative, replay.MAX_TOOL_BYTES))
                .hexdigest()
                .encode("ascii")
            )
        inputs = {
            "head": HEAD,
            "lane": "closure",
            "target": target,
            "target_hash": hashlib.sha256(target.encode("utf-8")).hexdigest(),
            "subsystem": "Render",
            "report_hashes": {"function": HASH, "sizes": HASH},
            "map_hash": hashlib.sha256(map_content).hexdigest(),
            "tool_hash": tool_digest.hexdigest(),
            "history_hash": hashlib.sha256(ledger).hexdigest(),
            "blocker_hash": hashlib.sha256(blockers).hexdigest(),
            "artifact_hash": hashlib.sha256(artifacts).hexdigest(),
            "dwarf_input": {"path": "/historical/dwarf", "sha256": HASH},
            "doctor_receipt": doctor_descriptor,
            "scout_reports": scout_descriptors,
            "production_mismatch": None,
            "context_inputs": context_inputs,
        }
        source_artifacts = doctor_descriptor["source_artifacts"]
        assert isinstance(source_artifacts, dict)
        pack = {
            "schema": context.CONTEXT_PACK_SCHEMA,
            "target": target,
            "abi": None,
            "size": 8,
            "instructions": [],
            "memory_operands": [],
            "call_neighbors": [],
            "accepted_sources": [],
            "control_flow": {"basic_blocks": [], "edges": []},
            "completeness": {"complete": False, "unknown_reasons": []},
            "bindings": {
                "doctor_receipt": {
                    "path": Path(str(doctor_descriptor["path"]))
                    .resolve()
                    .relative_to(root.resolve())
                    .as_posix(),
                    "sha256": doctor_hash,
                    "receipt_id": doctor_id,
                },
                "artifacts": source_artifacts,
                "evidence_sources": {
                    kind: {
                        "path": artifact["path"],
                        "sha256": artifact["sha256"],
                    }
                    for kind, artifact in sorted(source_artifacts.items())
                },
                "context_inputs": context_inputs,
                "decoder": decoder,
            },
            "limits": {
                "instructions": context.MAX_INSTRUCTIONS,
                "basic_blocks": context.MAX_BASIC_BLOCKS,
                "edges": context.MAX_EDGES,
                "memory_operands": context.MAX_MEMORY_OPERANDS,
                "call_neighbors": context.MAX_CALL_NODES,
                "accepted_sources": context.MAX_ACCEPTED_SOURCES,
                "text_chars": context.MAX_TEXT_CHARS,
                "source_excerpt_chars": context.MAX_SOURCE_EXCERPT_CHARS,
                "bytes": context.MAX_CONTEXT_BYTES,
            },
        }
        pack["content_sha256"] = context.context_pack_hash(pack)
        document = {
            "schema": 1,
            "lane": "closure",
            "target": target,
            "cache_key": replay._json_hash(inputs),
            "inputs": inputs,
            "roles": brief.scout_roles(scout_descriptors),
            "evidence": {"readiness": True, "context_pack": pack},
            "subsystem": "Render",
            "scout_findings": [
                {
                    "scout_id": row["content"]["scout_id"],
                    "audit": row["content"]["audit"],
                    "report_sha256": row["sha256"],
                    "findings": row["content"]["findings"],
                }
                for row in scout_descriptors
            ],
        }
        document["content_sha256"] = brief._content_hash(document)
        brief_path = (
            root
            / "build/decomp-cache/briefs"
            / f"closure-{target.lower()}-{document['cache_key']}.json"
        )
        brief_path.parent.mkdir(parents=True, exist_ok=True)
        brief_content = json.dumps(document, indent=2, sort_keys=True).encode() + b"\n"
        brief_path.write_bytes(brief_content)
        brief_descriptor = {
            "path": str(brief_path),
            "sha256": hashlib.sha256(brief_content).hexdigest(),
            "cache_key": document["cache_key"],
            "lane": "closure",
            "target": target,
            "subsystem": "Render",
            "head": HEAD,
            "content_sha256": document["content_sha256"],
            "dwarf_input": inputs["dwarf_input"],
            "doctor_receipt": doctor_descriptor,
            "scout_reports": scout_descriptors,
            "research_route": None,
        }
        campaign["briefs"] = [brief_descriptor]
        return campaign, brief_descriptor, git_blob

    def runtime_identity(self, root: Path, checkout_head: str) -> dict[str, object]:
        module = b"module"
        files = {
            name: HASH for name in replay.HISTORICAL_VALIDATION_IDENTITY_FILES
        }
        return {
            "files": files,
            "reccmp_user": {
                "config_path": "reccmp-user.yml",
                "config_sha256": HASH,
                "project_path": "reccmp-project.yml",
                "project_sha256": HASH,
                "retail_path": "original/toy2.exe",
                "retail_sha256": HASH,
                "retail_expected_sha256": HASH,
            },
            "reccmp_runtime": {
                "module_origin": str(
                    (root / "external/submodules/reccmp/reccmp/__init__.py").resolve()
                ),
                "module_sha256": hashlib.sha256(module).hexdigest(),
                "package_sha256": "c" * 64,
                "checkout": {
                    "path": str((root / "external/submodules/reccmp").resolve()),
                    "head": checkout_head,
                    "status_sha256": hashlib.sha256(b"").hexdigest(),
                    "worktree_sha256": hashlib.sha256(b"").hexdigest(),
                },
                "executables": {
                    name: {
                        "path": str((root / ".tooling/venv/bin" / name).resolve()),
                        "sha256": HASH,
                    }
                    for name in ("reccmp-project", "reccmp-reccmp")
                },
            },
        }

    def test_clean_recorded_runtime_drift_is_allowed_and_locally_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recorded = "f" * 40
            identity = self.runtime_identity(root, recorded)
            with (
                mock.patch.object(replay, "_historical_files"),
                mock.patch.object(replay, "_historical_retail_hash", return_value=HASH),
                mock.patch.object(replay, "_gitlink", return_value="e" * 40),
                mock.patch.object(replay, "_require_submodule_commit"),
                mock.patch.object(replay, "_submodule_blob", return_value=b"module") as blob,
                mock.patch.object(replay, "_submodule_package_hash", return_value="c" * 64),
            ):
                replay._historical_validation_identity(root, HEAD, identity)
            blob.assert_called_once_with(
                (root / "external/submodules/reccmp").resolve(),
                recorded,
                "reccmp/__init__.py",
                replay.MAX_TOOL_BYTES,
            )

    def test_recorded_runtime_identity_requires_a_local_commit_object(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=checkout, check=True)
            (checkout / "file").write_text("content\n", encoding="utf-8")
            subprocess.run(["git", "add", "file"], cwd=checkout, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Replay Tests",
                    "-c",
                    "user.email=replay@example.invalid",
                    "commit",
                    "-qm",
                    "runtime",
                ],
                cwd=checkout,
                check=True,
            )
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=checkout,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            tree = subprocess.run(
                ["git", "rev-parse", "HEAD^{tree}"],
                cwd=checkout,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            replay._require_submodule_commit(checkout, commit)
            for invalid in (tree, "f" * 40):
                with self.subTest(invalid=invalid), self.assertRaises(replay.ReplayError):
                    replay._require_submodule_commit(checkout, invalid)

    def test_runtime_schema_and_retail_forgery_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = self.runtime_identity(root, "f" * 40)
            mutations = (
                lambda value: value["reccmp_runtime"].pop("package_sha256"),
                lambda value: value["reccmp_runtime"].__setitem__("extra", 1),
                lambda value: value["reccmp_user"].__setitem__(
                    "retail_expected_sha256", "d" * 64
                ),
            )
            for mutate in mutations:
                identity = copy.deepcopy(base)
                mutate(identity)
                with (
                    mock.patch.object(replay, "_historical_files"),
                    mock.patch.object(replay, "_historical_retail_hash", return_value=HASH),
                    mock.patch.object(replay, "_gitlink", return_value="e" * 40),
                    mock.patch.object(replay, "_require_submodule_commit"),
                    mock.patch.object(replay, "_submodule_blob", return_value=b"module"),
                    mock.patch.object(replay, "_submodule_package_hash", return_value="c" * 64),
                ):
                    with self.assertRaises(replay.ReplayError):
                        replay._historical_validation_identity(root, HEAD, identity)

    def test_historical_runtime_accepts_windows_entrypoint_descriptors(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            identity = self.runtime_identity(root, "f" * 40)
            runtime = identity["reccmp_runtime"]
            assert isinstance(runtime, dict)
            executables = runtime["executables"]
            assert isinstance(executables, dict)
            for name in ("reccmp-project", "reccmp-reccmp"):
                executables[name]["path"] = (
                    rf"C:\Replay\.tooling\venv\Scripts\{name}.exe"
                )
            patches = (
                mock.patch.object(replay, "_historical_files"),
                mock.patch.object(replay, "_historical_retail_hash", return_value=HASH),
                mock.patch.object(replay, "_gitlink", return_value="e" * 40),
                mock.patch.object(replay, "_require_submodule_commit"),
                mock.patch.object(replay, "_submodule_blob", return_value=b"module"),
                mock.patch.object(
                    replay, "_submodule_package_hash", return_value="c" * 64
                ),
            )
            with patches[0], patches[1], patches[2], patches[3], patches[4], patches[5]:
                replay._historical_validation_identity(root, HEAD, identity)
            for invalid in (
                "Scripts/reccmp-project.exe",
                r"C:\Replay\Scripts\reccmp-reccmp.exe",
                r"C:/Replay/Scripts/reccmp-project.exe",
            ):
                changed = copy.deepcopy(identity)
                changed["reccmp_runtime"]["executables"]["reccmp-project"][
                    "path"
                ] = invalid
                with (
                    mock.patch.object(replay, "_historical_files"),
                    mock.patch.object(
                        replay, "_historical_retail_hash", return_value=HASH
                    ),
                    mock.patch.object(replay, "_gitlink", return_value="e" * 40),
                    mock.patch.object(replay, "_require_submodule_commit"),
                    mock.patch.object(
                        replay, "_submodule_blob", return_value=b"module"
                    ),
                    mock.patch.object(
                        replay, "_submodule_package_hash", return_value="c" * 64
                    ),
                    self.assertRaises(replay.ReplayError),
                ):
                    replay._historical_validation_identity(root, HEAD, changed)

    def test_full_historical_doctor_accepts_single_bundle_and_cross_host_checks(self):
        for addresses in (["0x00401000"], ["0x00401000", "0x00401010"]):
            for runtime_check in ("wine-prefix", "native-build-runtime"):
                with self.subTest(addresses=addresses, runtime_check=runtime_check), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    descriptor, campaign, map_content = self.historical_doctor(
                        root, list(addresses), runtime_check=runtime_check
                    )
                    with (
                        mock.patch.object(replay, "_git_blob", return_value=map_content),
                        mock.patch.object(
                            replay,
                            "_historical_retail_hash",
                            return_value="d" * 64,
                        ),
                        mock.patch.object(replay, "_git_ancestor", return_value=True),
                    ):
                        receipt = replay._validate_historical_doctor(
                            descriptor,
                            campaign=campaign,
                            target="0x00401000",
                            root=root,
                            campaign_head=HEAD,
                        )
                    self.assertEqual(receipt["target"], addresses[0] if len(addresses) == 1 else None)

    def test_full_historical_doctor_rejects_mixed_runtime_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            descriptor, campaign, map_content = self.historical_doctor(
                root, ["0x00401000"], runtime_check="wine-prefix"
            )
            path = Path(str(descriptor["path"]))
            receipt = json.loads(path.read_text(encoding="utf-8"))
            receipt["checks"].append(
                {
                    "name": "native-build-runtime",
                    "ok": True,
                    "detail": "ready",
                    "data": {},
                }
            )
            old_identity = campaign["doctor_receipt"]
            assert isinstance(old_identity, dict)
            receipt_id = doctor._receipt_id(receipt)
            receipt["receipt_id"] = receipt_id
            receipt["receipt_path"] = (
                doctor.RECEIPT_DIRECTORY_RELATIVE / f"{receipt_id}.json"
            ).as_posix()
            content = json.dumps(receipt, indent=2, sort_keys=True).encode() + b"\n"
            path = root / str(receipt["receipt_path"])
            path.write_bytes(content)
            descriptor.update(
                path=str(path.resolve()),
                sha256=hashlib.sha256(content).hexdigest(),
                receipt_id=receipt_id,
            )
            identity = dict(old_identity)
            identity.update(
                path=str(path.resolve()),
                sha256=descriptor["sha256"],
                receipt_id=receipt_id,
            )
            campaign["doctor_receipt"] = identity
            campaign["doctor_receipts"] = [identity]
            with (
                mock.patch.object(replay, "_git_blob", return_value=map_content),
                mock.patch.object(
                    replay, "_historical_retail_hash", return_value="d" * 64
                ),
                mock.patch.object(replay, "_git_ancestor", return_value=True),
                self.assertRaises(replay.ReplayError),
            ):
                replay._validate_historical_doctor(
                    descriptor,
                    campaign=campaign,
                    target="0x00401000",
                    root=root,
                    campaign_head=HEAD,
                )

    def test_full_historical_brief_context_accepts_single_and_bundle_doctors(self):
        for addresses in (["0x00401000"], ["0x00401000", "0x00401010"]):
            with self.subTest(addresses=addresses), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                campaign, _descriptor, git_blob = self.historical_brief(
                    root, list(addresses)
                )
                with (
                    mock.patch.object(replay, "_git_blob", side_effect=git_blob),
                    mock.patch.object(
                        replay, "_historical_retail_hash", return_value="d" * 64
                    ),
                    mock.patch.object(replay, "_git_ancestor", return_value=True),
                ):
                    brief_hash, pack_hash = replay._brief_context(
                        campaign, "0x00401000", root, HEAD
                    )
                self.assertRegex(brief_hash, r"^[0-9a-f]{64}$")
                self.assertRegex(pack_hash, r"^[0-9a-f]{64}$")

    def test_historical_brief_uses_embedded_scouts_after_source_reuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign, descriptor, git_blob = self.historical_brief(
                root, ["0x00401000"]
            )
            scouts = descriptor["scout_reports"]
            self.assertIsInstance(scouts, list)
            first_path = Path(str(scouts[0]["path"]))
            second_path = Path(str(scouts[1]["path"]))
            first_path.write_text("reused by a later campaign", encoding="utf-8")
            second_path.unlink()
            with (
                mock.patch.object(replay, "_git_blob", side_effect=git_blob),
                mock.patch.object(
                    replay, "_historical_retail_hash", return_value="d" * 64
                ),
                mock.patch.object(replay, "_git_ancestor", return_value=True),
            ):
                brief_hash, pack_hash = replay._brief_context(
                    campaign, "0x00401000", root, HEAD
                )
            self.assertRegex(brief_hash, r"^[0-9a-f]{64}$")
            self.assertRegex(pack_hash, r"^[0-9a-f]{64}$")

    def test_historical_brief_rejects_embedded_scout_forgery(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign, descriptor, git_blob = self.historical_brief(
                root, ["0x00401000"]
            )
            path = Path(str(descriptor["path"]))
            document = json.loads(path.read_text(encoding="utf-8"))
            scouts = document["inputs"]["scout_reports"]
            scouts[0]["content"]["doctor_receipt"]["receipt_id"] = "e" * 64
            document["cache_key"] = replay._json_hash(document["inputs"])
            document["content_sha256"] = brief._content_hash(document)
            new_path = path.with_name(
                f"closure-0x00401000-{document['cache_key']}.json"
            )
            content = json.dumps(document, indent=2, sort_keys=True).encode() + b"\n"
            new_path.write_bytes(content)
            descriptor.update(
                path=str(new_path),
                sha256=hashlib.sha256(content).hexdigest(),
                cache_key=document["cache_key"],
                content_sha256=document["content_sha256"],
                scout_reports=scouts,
            )
            with (
                mock.patch.object(replay, "_git_blob", side_effect=git_blob),
                mock.patch.object(
                    replay, "_historical_retail_hash", return_value="d" * 64
                ),
                mock.patch.object(replay, "_git_ancestor", return_value=True),
                self.assertRaises(replay.ReplayError),
            ):
                replay._brief_context(campaign, "0x00401000", root, HEAD)

    def test_full_historical_brief_context_rejects_rehashed_pack_forgery(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign, descriptor, git_blob = self.historical_brief(
                root, ["0x00401000"]
            )
            path = Path(str(descriptor["path"]))
            document = json.loads(path.read_text(encoding="utf-8"))
            pack = document["evidence"]["context_pack"]
            pack["bindings"]["evidence_sources"] = {}
            pack["content_sha256"] = context.context_pack_hash(pack)
            document["content_sha256"] = brief._content_hash(document)
            content = json.dumps(document, indent=2, sort_keys=True).encode() + b"\n"
            path.write_bytes(content)
            descriptor["sha256"] = hashlib.sha256(content).hexdigest()
            descriptor["content_sha256"] = document["content_sha256"]
            with (
                mock.patch.object(replay, "_git_blob", side_effect=git_blob),
                mock.patch.object(
                    replay, "_historical_retail_hash", return_value="d" * 64
                ),
                mock.patch.object(replay, "_git_ancestor", return_value=True),
                self.assertRaises(replay.ReplayError),
            ):
                replay._brief_context(campaign, "0x00401000", root, HEAD)

    def test_full_historical_brief_context_rejects_extra_pack_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign, descriptor, git_blob = self.historical_brief(
                root, ["0x00401000"]
            )
            path = Path(str(descriptor["path"]))
            document = json.loads(path.read_text(encoding="utf-8"))
            pack = document["evidence"]["context_pack"]
            pack["extra"] = "private"
            pack["content_sha256"] = context.context_pack_hash(pack)
            document["content_sha256"] = brief._content_hash(document)
            content = json.dumps(document, indent=2, sort_keys=True).encode() + b"\n"
            path.write_bytes(content)
            descriptor["sha256"] = hashlib.sha256(content).hexdigest()
            descriptor["content_sha256"] = document["content_sha256"]
            with (
                mock.patch.object(replay, "_git_blob", side_effect=git_blob),
                mock.patch.object(
                    replay, "_historical_retail_hash", return_value="d" * 64
                ),
                mock.patch.object(replay, "_git_ancestor", return_value=True),
                self.assertRaises(replay.ReplayError),
            ):
                replay._brief_context(campaign, "0x00401000", root, HEAD)

    def test_doctor_binding_supports_primary_bundle_and_target_event(self):
        selection = "2026-09-09T12:00:00.123Z"
        doctor_start = "2026-09-09T12:01:00.456Z"
        doctor_end = "2026-09-09T12:02:00.789Z"
        campaign_start = "2026-09-09T12:03:00+00:00"
        receipt = {
            "selection_started_at": selection,
            "doctor_started_at": doctor_start,
            "doctor_ended_at": doctor_end,
            "started_at": doctor_start,
            "ended_at": doctor_end,
        }
        identity = {"receipt_id": HASH, "addresses": ["0x00401000"]}
        primary = {
            "started_at": campaign_start,
            "doctor_receipt": identity,
            "doctor_receipts": [identity],
            "selection_started_at": "2026-09-09T12:00:00+00:00",
            "doctor_started_at": "2026-09-09T12:01:00+00:00",
            "doctor_ended_at": "2026-09-09T12:02:00+00:00",
            "target_events": [],
        }
        replay._validate_doctor_campaign_binding(
            receipt, primary, "0x00401000", identity
        )

        other = {"receipt_id": "d" * 64, "addresses": ["0x00400000"]}
        event_receipt = {
            "selection_started_at": "2026-09-09T12:04:00.111Z",
            "doctor_started_at": "2026-09-09T12:05:00.222Z",
            "doctor_ended_at": "2026-09-09T12:06:00.333Z",
            "started_at": "2026-09-09T12:05:00.222Z",
            "ended_at": "2026-09-09T12:06:00.333Z",
        }
        family = {
            "started_at": campaign_start,
            "doctor_receipt": other,
            "doctor_receipts": [other, identity],
            "selection_started_at": "2026-09-09T11:00:00+00:00",
            "doctor_started_at": "2026-09-09T11:01:00+00:00",
            "doctor_ended_at": "2026-09-09T11:02:00+00:00",
            "target_events": [
                {
                    "address": "0x00401000",
                    "doctor_receipt": identity,
                    "selection_started_at": "2026-09-09T12:04:00+00:00",
                    "doctor_started_at": "2026-09-09T12:05:00+00:00",
                    "doctor_ended_at": "2026-09-09T12:06:00+00:00",
                    "added_at": "2026-09-09T12:07:00+00:00",
                }
            ],
        }
        replay._validate_doctor_campaign_binding(
            event_receipt, family, "0x00401000", identity
        )
        for malformed in (
            "2026-09-09T12:00:00Z",
            "2026-09-09T12:00:00.123+00:00",
            "2026-09-09T14:00:00.123+02:00",
        ):
            with self.subTest(malformed=malformed), self.assertRaises(replay.ReplayError):
                replay._doctor_time(malformed, "doctor")

    def test_doctor_binding_rejects_missing_mismatched_and_stale_evidence(self):
        receipt = {
            "selection_started_at": "2026-09-09T10:00:00.000Z",
            "doctor_started_at": "2026-09-09T10:01:00.000Z",
            "doctor_ended_at": "2026-09-09T10:02:00.000Z",
            "started_at": "2026-09-09T10:01:00.000Z",
            "ended_at": "2026-09-09T10:02:00.000Z",
        }
        identity = {"receipt_id": HASH}
        campaign = {
            "started_at": "2026-09-09T11:03:00+00:00",
            "doctor_receipt": identity,
            "doctor_receipts": [identity],
            "selection_started_at": "2026-09-09T10:00:00+00:00",
            "doctor_started_at": "2026-09-09T10:01:00+00:00",
            "doctor_ended_at": "2026-09-09T10:02:00+00:00",
            "target_events": [],
        }
        with self.assertRaises(replay.ReplayError):
            replay._validate_doctor_campaign_binding(
                receipt, campaign, "0x00401000", identity
            )
        campaign["started_at"] = "2026-09-09T10:03:00+00:00"
        campaign["doctor_receipts"] = []
        with self.assertRaises(replay.ReplayError):
            replay._validate_doctor_campaign_binding(
                receipt, campaign, "0x00401000", identity
            )

    def test_experiment_and_finalization_chronology_is_exact(self):
        root = Path("/tmp/replay-chronology")
        session = {
            "created_at": "2026-09-09T12:01:00+00:00",
            "activated_at": "2026-09-09T12:02:00+00:00",
        }
        trajectory = {
            "trials": [
                {
                    "pending_receipt": {"path": "pending-1.json"},
                    "terminal_receipt": {"path": "terminal-1.json"},
                },
                {
                    "pending_receipt": {"path": "pending-2.json"},
                    "terminal_receipt": {"path": "terminal-2.json"},
                },
            ]
        }
        campaign = {
            "started_at": "2026-09-09T12:00:00+00:00",
            "ended_at": "2026-09-09T12:09:00+00:00",
        }
        finalize = {
            "phase_timestamps": {
                "finalization-started": "2026-09-09T12:07:00+00:00"
            },
            "created_at": "2026-09-09T12:08:00+00:00",
        }
        receipts = [
            {"reserved_at": "2026-09-09T12:03:00+00:00"},
            {"completed_at": "2026-09-09T12:04:00+00:00"},
            {"reserved_at": "2026-09-09T12:05:00+00:00"},
            {"completed_at": "2026-09-09T12:06:00+00:00"},
        ]
        with mock.patch.object(experiment, "read_receipt", side_effect=receipts):
            replay._validate_trajectory_chronology(
                session, trajectory, campaign, finalize, root
            )
        invalids = (
            (dict(session, activated_at="2026-09-09T11:59:00+00:00"), finalize, receipts),
            (
                session,
                finalize,
                [
                    {"reserved_at": "2026-09-09T12:03:00+00:00"},
                    {"completed_at": "2026-09-09T12:05:30+00:00"},
                    {"reserved_at": "2026-09-09T12:05:00+00:00"},
                    {"completed_at": "2026-09-09T12:06:00+00:00"},
                ],
            ),
            (
                session,
                {
                    **finalize,
                    "phase_timestamps": {
                        "finalization-started": "2026-09-09T12:05:00+00:00"
                    },
                },
                receipts,
            ),
        )
        for changed_session, changed_finalize, changed_receipts in invalids:
            with self.subTest(session=changed_session, finalize=changed_finalize):
                with mock.patch.object(
                    experiment, "read_receipt", side_effect=changed_receipts
                ):
                    with self.assertRaises(replay.ReplayError):
                        replay._validate_trajectory_chronology(
                            changed_session,
                            trajectory,
                            campaign,
                            changed_finalize,
                            root,
                        )


class TrustCurrentnessTests(unittest.TestCase):
    def trusted_repository(self, root: Path) -> str:
        subprocess.run(["git", "init", "-q", "-b", "agent/continuous"], cwd=root, check=True)
        for name in (*replay.TRUSTED_INPUTS, replay.LEDGER_RELATIVE.as_posix()):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            content = (
                "/.decomp-replay/\n"
                if name == ".gitignore"
                else f"bound input {name}\n"
            )
            path.write_text(content, encoding="utf-8")
            if name == "tools/decomp":
                os.chmod(path, 0o755)
        subprocess.run(["git", "add", "."], cwd=root, check=True)
        subprocess.run(
            [
                "git",
                "-c",
                "user.name=Replay Tests",
                "-c",
                "user.email=replay@example.invalid",
                "commit",
                "-qm",
                "trusted inputs",
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
        ).stdout.strip()
        subprocess.run(
            ["git", "update-ref", "refs/remotes/origin/agent/continuous", head],
            cwd=root,
            check=True,
        )
        return head

    def passing_repository(
        self, root: Path
    ) -> tuple[str, str, Path, Path, Path, Path]:
        parent = self.trusted_repository(root)
        pairs = (
            ("abi", "call"),
            ("control-flow", "integer"),
            ("memory", "stack"),
            ("floating-point", "side-effect"),
        )
        cases = [
            replay_case(
                index,
                routes=pairs[index % len(pairs)],
                subsystem=f"subsystem-{index % 4}",
            )
            for index in range(1, 13)
        ]
        entries = []
        private_root = root / replay.PRIVATE_RELATIVE
        private_root.mkdir(mode=0o700)
        cases_root = private_root / replay.CASES_NAME
        cases_root.mkdir(mode=0o700)
        lock = private_root / "suite.lock"
        lock.write_bytes(b"")
        os.chmod(lock, 0o600)
        for case in cases:
            content = json.dumps(case, indent=2, sort_keys=True).encode() + b"\n"
            path = cases_root / f"{case['case_id']}.json"
            path.write_bytes(content)
            os.chmod(path, 0o600)
            entries.append(
                {
                    "case_id": case["case_id"],
                    "bytes": len(content),
                    "sha256": hashlib.sha256(content).hexdigest(),
                }
            )
        manifest_path = root / replay.MANIFEST_RELATIVE
        manifest_path.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "kind": "private-replay-manifest",
                    "cases": entries,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        os.chmod(manifest_path, 0o644)
        ledger = root / replay.LEDGER_RELATIVE
        ledger.write_text('{"record_type":"fixture"}\n', encoding="utf-8")
        subprocess.run(
            [
                "git",
                "add",
                replay.MANIFEST_RELATIVE.as_posix(),
                replay.LEDGER_RELATIVE.as_posix(),
            ],
            cwd=root,
            check=True,
        )
        subprocess.run(
            [
                "git",
                "-c",
                "user.name=Replay Tests",
                "-c",
                "user.email=replay@example.invalid",
                "commit",
                "-qm",
                "Select replay cases",
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
        ).stdout.strip()
        subprocess.run(
            ["git", "update-ref", "refs/remotes/origin/agent/continuous", head],
            cwd=root,
            check=True,
        )
        return head, parent, manifest_path, cases_root / f"{cases[0]['case_id']}.json", root / replay.TRUSTED_INPUTS[5], ledger

    def test_fixed_tool_closure_rejects_worktree_index_resource_and_deletion_drift(self):
        required = {
            "tools/__init__.py",
            "tools/decomp_replay.py",
            "tools/decomp_experiment.py",
            "tools/decomp_campaigns.py",
            "tools/decomp_context.py",
            "tools/decomp_evidence.py",
            "tools/Resources/functions_map.txt",
            "tools/Resources/tool_artifacts.tsv",
            ".notes/lint-baseline.tsv",
        }
        self.assertTrue(required <= set(replay.TRUSTED_INPUTS))
        for name, state in (
            ("tools/decomp_experiment.py", "worktree"),
            ("tools/Resources/functions_map.txt", "index"),
            ("tools/__init__.py", "deleted"),
        ):
            with self.subTest(name=name, state=state), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                head = self.trusted_repository(root)
                self.assertEqual(set(replay._tool_binding(root, head)), set(replay.TRUSTED_INPUTS))
                path = root / name
                if state == "deleted":
                    path.unlink()
                else:
                    path.write_text("changed\n", encoding="utf-8")
                    if state == "index":
                        subprocess.run(["git", "add", name], cwd=root, check=True)
                with self.assertRaises(replay.ReplayError):
                    replay._tool_binding(root, head)

    def test_private_ignore_and_ledger_must_match_index_worktree_and_head(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            head = self.trusted_repository(root)
            self.assertTrue(replay._private_root_is_untracked(root, head))
            ledger = root / replay.LEDGER_RELATIVE
            saved = ledger.read_bytes()
            self.assertTrue(
                replay._tracked_content_matches_head(
                    root, head, replay.LEDGER_RELATIVE, saved, replay.MAX_LEDGER_BYTES
                )
            )
            ledger.write_text("dirty\n", encoding="utf-8")
            self.assertFalse(
                replay._tracked_content_matches_head(
                    root, head, replay.LEDGER_RELATIVE, saved, replay.MAX_LEDGER_BYTES
                )
            )
            subprocess.run(["git", "add", replay.LEDGER_RELATIVE.as_posix()], cwd=root, check=True)
            self.assertFalse(
                replay._tracked_content_matches_head(
                    root, head, replay.LEDGER_RELATIVE, saved, replay.MAX_LEDGER_BYTES
                )
            )

    def test_campaign_bounded_reader_falls_back_without_dirfd_support(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "receipt.json"
            path.write_bytes(b"{}\n")
            with mock.patch.object(campaigns.os, "supports_dir_fd", set()):
                self.assertEqual(
                    campaigns._read_bounded_regular_file(
                        path, "receipt", max_bytes=16
                    ),
                    b"{}\n",
                )
                alias = path.with_name("alias.json")
                os.link(path, alias)
                with self.assertRaises(ValueError):
                    campaigns._read_bounded_regular_file(
                        path, "receipt", max_bytes=16
                    )

    def test_campaign_finalizer_inputs_bind_the_package_initializer(self):
        root = Path(__file__).resolve().parents[2]
        package = str((root / "tools/__init__.py").resolve())
        self.assertIn(package, campaigns._standard_input_hashes(root)["files"])
        self.assertIn(package, campaigns._meta_input_hashes(root)["files"])

    def test_ledger_parser_caps_rows_and_builds_campaign_index_once(self):
        content = b'\n'.join(
            [
                b'{"campaign_id":"one","record_type":"campaign"}',
                b'{"campaign_id":"two","record_type":"campaign"}',
                b'{"campaign_id":"one","record_type":"delivery"}',
            ]
        ) + b"\n"
        records = replay._parse_ledger_records(content)
        self.assertEqual(len(replay._campaign_records(records, "one")), 2)
        with mock.patch.object(replay, "MAX_LEDGER_RECORDS", 2):
            with self.assertRaisesRegex(replay.ReplayError, "too many"):
                replay._parse_ledger_records(content)

    def test_saved_certificate_revalidates_live_inputs_and_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                head,
                parent,
                manifest,
                selected_case,
                trusted_tool,
                ledger,
            ) = self.passing_repository(root)
            with mock.patch.object(replay, "_revalidate_case"):
                first = replay.evaluate_suite(root)
                second = replay.evaluate_suite(root)
                self.assertEqual(first, second)
                self.assertEqual(first["content_sha256"], second["content_sha256"])
                summary = replay.certify(root)
                self.assertTrue(summary["certified"])
                self.assertIsNotNone(replay.current_passing_certificate(root))
                self.assertEqual(
                    route.advice_document(observation(), root=root)["status"],
                    "available",
                )

                mutations = (
                    (
                        "manifest",
                        manifest,
                        manifest.read_bytes() + b"\n",
                        None,
                    ),
                    (
                        "case",
                        selected_case,
                        selected_case.read_bytes() + b" ",
                        None,
                    ),
                    (
                        "tool",
                        trusted_tool,
                        trusted_tool.read_bytes() + b"# drift\n",
                        None,
                    ),
                    (
                        "ledger",
                        ledger,
                        ledger.read_bytes() + b" \n",
                        None,
                    ),
                    ("origin", None, b"", parent),
                )
                for name, path, changed, changed_origin in mutations:
                    with self.subTest(name=name):
                        if path is not None:
                            saved = path.read_bytes()
                            path.write_bytes(changed)
                        else:
                            saved = b""
                            subprocess.run(
                                [
                                    "git",
                                    "update-ref",
                                    "refs/remotes/origin/agent/continuous",
                                    str(changed_origin),
                                ],
                                cwd=root,
                                check=True,
                            )
                        try:
                            self.assertIsNone(
                                replay.current_passing_certificate(root)
                            )
                            advice = route.advice_document(observation(), root=root)
                            self.assertEqual(advice["status"], "withheld")
                            self.assertIsNone(advice["route"])
                        finally:
                            if path is not None:
                                path.write_bytes(saved)
                            else:
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
                        self.assertIsNotNone(
                            replay.current_passing_certificate(root)
                        )


class PrivateStorageTests(unittest.TestCase):
    def raw_private_file(self, root: Path, name: str, content: bytes) -> None:
        with replay._private_directory(root, create=True) as directory_fd:
            descriptor = os.open(
                name,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                0o600,
                dir_fd=directory_fd,
            )
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
            os.fsync(directory_fd)

    def test_private_writer_recovers_partial_temp_and_fsyncs_exact_final(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with replay._private_directory(root, "cases", create=True) as directory_fd:
                name = "1" * 32 + ".json"
                temporary = f".{name}.tmp"
                descriptor = os.open(
                    temporary,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                    0o600,
                    dir_fd=directory_fd,
                )
                with os.fdopen(descriptor, "wb") as stream:
                    stream.write(b"partial")
                replay._write_exclusive_at(directory_fd, name, b"complete")
                self.assertEqual(
                    replay._read_at(directory_fd, name, 32, "case"), b"complete"
                )
                with self.assertRaises(FileNotFoundError):
                    os.stat(temporary, dir_fd=directory_fd)
                with mock.patch.object(os, "fsync", wraps=os.fsync) as synced:
                    replay._write_exclusive_at(directory_fd, name, b"complete")
                self.assertTrue(any(call.args[0] == directory_fd for call in synced.mock_calls))

    def test_private_writer_cleans_file_and_directory_fsync_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with replay._private_directory(root, "cases", create=True) as directory_fd:
                name = "2" * 32 + ".json"
                real_fsync = os.fsync
                failed = False

                def fail_first(descriptor: int) -> None:
                    nonlocal failed
                    if not failed:
                        failed = True
                        raise OSError("injected")
                    real_fsync(descriptor)

                with mock.patch.object(os, "fsync", side_effect=fail_first):
                    with self.assertRaises(replay.ReplayError):
                        replay._write_exclusive_at(directory_fd, name, b"content")
                self.assertFalse(
                    any(entry.name in {name, f".{name}.tmp"} for entry in os.scandir(directory_fd))
                )

                failed = False

                def fail_directory_once(descriptor: int) -> None:
                    nonlocal failed
                    if descriptor == directory_fd and not failed:
                        failed = True
                        raise OSError("injected")
                    real_fsync(descriptor)

                with mock.patch.object(os, "fsync", side_effect=fail_directory_once):
                    with self.assertRaises(replay.ReplayError):
                        replay._write_exclusive_at(directory_fd, name, b"content")
                self.assertFalse(
                    any(entry.name in {name, f".{name}.tmp"} for entry in os.scandir(directory_fd))
                )

    def test_private_writer_cleans_rename_failure_and_rejects_hardlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with replay._private_directory(root, "cases", create=True) as directory_fd:
                name = "3" * 32 + ".json"
                library = mock.Mock()
                library.renameat2 = mock.Mock(return_value=-1)
                with (
                    mock.patch.object(replay.ctypes, "CDLL", return_value=library),
                    mock.patch.object(replay.ctypes, "get_errno", return_value=5),
                ):
                    with self.assertRaises(replay.ReplayError):
                        replay._write_exclusive_at(directory_fd, name, b"content")
                self.assertEqual(list(os.scandir(directory_fd)), [])

                descriptor = os.open(
                    name,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                    0o600,
                    dir_fd=directory_fd,
                )
                os.write(descriptor, b"content")
                os.close(descriptor)
                os.link(name, "alias", src_dir_fd=directory_fd, dst_dir_fd=directory_fd)
                with self.assertRaises(replay.ReplayError):
                    replay._read_at(directory_fd, name, 32, "case")

    def test_private_modes_are_owner_only_and_wrong_owner_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prior = os.umask(0)
            try:
                with replay._private_directory(root, "cases", create=True) as directory_fd:
                    replay._write_exclusive_at(
                        directory_fd, "4" * 32 + ".json", b"content"
                    )
            finally:
                os.umask(prior)
            self.assertEqual(
                (root / replay.PRIVATE_RELATIVE).stat().st_mode & 0o777, 0o700
            )
            self.assertEqual(
                (root / replay.PRIVATE_RELATIVE / "cases").stat().st_mode & 0o777,
                0o700,
            )
            case_path = root / replay.PRIVATE_RELATIVE / "cases" / ("4" * 32 + ".json")
            self.assertEqual(case_path.stat().st_mode & 0o777, 0o600)
            with replay._private_directory(root, "cases") as directory_fd:
                with mock.patch.object(os, "geteuid", return_value=os.geteuid() + 1):
                    with self.assertRaises(replay.ReplayError):
                        replay._read_at(directory_fd, case_path.name, 32, "case")

    def test_pointer_replace_recovers_ambiguous_directory_fsync(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = b'{"schema_version":1}\n'
            with replay._private_directory(root, create=True) as directory_fd:
                real_fsync = os.fsync
                failed = False

                def fail_pointer_directory(descriptor: int) -> None:
                    nonlocal failed
                    if descriptor == directory_fd and not failed:
                        failed = True
                        raise OSError("injected")
                    real_fsync(descriptor)

                with mock.patch.object(os, "fsync", side_effect=fail_pointer_directory):
                    with self.assertRaises(replay.ReplayError):
                        replay._replace_at(directory_fd, "current-certificate.json", content)
                # The atomic replace already happened.  An exact retry must
                # complete and fsync it rather than treating it as corruption.
                replay._replace_at(directory_fd, "current-certificate.json", content)
                self.assertEqual(
                    replay._read_at(
                        directory_fd,
                        "current-certificate.json",
                        1024,
                        "pointer",
                    ),
                    content,
                )

    def test_static_unsafe_private_objects_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outside = root / "outside"
            outside.write_text("outside", encoding="utf-8")
            with replay._private_directory(root, create=True) as directory_fd:
                os.symlink(outside, "current-certificate.json", dir_fd=directory_fd)
                with self.assertRaises(replay.ReplayError):
                    replay._replace_at(directory_fd, "current-certificate.json", b"{}\n")
                self.assertTrue((root / ".decomp-replay/current-certificate.json").is_symlink())

            lock_path = root / ".decomp-replay/suite.lock"
            lock_path.unlink(missing_ok=True)
            lock_path.symlink_to(outside)
            with self.assertRaises(replay.ReplayError):
                with replay._suite_lock(root, create=False, exclusive=False):
                    pass

    def test_integrated_oracle_parent_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outside = root / "outside-oracle"
            outside.mkdir()
            content_hash = "d" * 64
            (outside / f"{content_hash}.json").write_text(
                '{"status":"passed"}\n', encoding="utf-8"
            )
            cache = root / "build/decomp-cache"
            cache.mkdir(parents=True)
            (cache / "oracle").symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(replay.ReplayError, "missing or invalid"):
                replay._regular_bytes(
                    cache / "oracle" / f"{content_hash}.json",
                    1024,
                    "integrated leaf oracle receipt",
                )

    def test_invalid_journal_temp_is_removed_not_promoted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.raw_private_file(
                root, f".{replay.ENROLLMENT_JOURNAL_NAME}.tmp", b"{}\n"
            )
            manifest = {
                "schema_version": 1,
                "kind": "private-replay-manifest",
                "cases": [],
            }
            content = json.dumps(manifest, indent=2, sort_keys=True).encode() + b"\n"
            replay._recover_enrollment(root, content)
            self.assertFalse(
                (root / ".decomp-replay" / replay.ENROLLMENT_JOURNAL_NAME).exists()
            )
            self.assertFalse(
                (
                    root
                    / ".decomp-replay"
                    / f".{replay.ENROLLMENT_JOURNAL_NAME}.tmp"
                ).exists()
            )

    def test_valid_journal_recovers_old_and_new_manifest_states(self):
        for new_manifest_published in (False, True):
            with self.subTest(new_manifest_published=new_manifest_published), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                case = replay_case()
                case_content = json.dumps(case, indent=2, sort_keys=True).encode() + b"\n"
                entry = {
                    "case_id": case["case_id"],
                    "bytes": len(case_content),
                    "sha256": hashlib.sha256(case_content).hexdigest(),
                }
                old_document = {
                    "schema_version": 1,
                    "kind": "private-replay-manifest",
                    "cases": [],
                }
                new_document = {**old_document, "cases": [entry]}
                old = json.dumps(old_document, indent=2, sort_keys=True).encode() + b"\n"
                new = json.dumps(new_document, indent=2, sort_keys=True).encode() + b"\n"
                journal = replay._journal_document(old, new, [entry])
                journal_content = json.dumps(journal, indent=2, sort_keys=True).encode() + b"\n"
                with replay._private_directory(root, create=True) as directory_fd:
                    # Exercise crash recovery from a complete temp before its
                    # no-replace promotion to the journal name.
                    replay._write_exclusive_at(
                        directory_fd,
                        f".{replay.ENROLLMENT_JOURNAL_NAME}.tmp",
                        journal_content,
                    )
                with replay._private_directory(root, "cases", create=True) as directory_fd:
                    replay._write_exclusive_at(
                        directory_fd, f"{case['case_id']}.json", case_content
                    )
                replay._recover_enrollment(root, new if new_manifest_published else old)
                self.assertFalse(
                    (root / replay.PRIVATE_RELATIVE / replay.ENROLLMENT_JOURNAL_NAME).exists()
                )
                self.assertEqual(
                    (root / replay.PRIVATE_RELATIVE / "cases" / f"{case['case_id']}.json").exists(),
                    new_manifest_published,
                )

    def test_journal_cannot_delete_selected_cases_or_clear_missing_entries(self):
        case = replay_case()
        case_content = json.dumps(case, indent=2, sort_keys=True).encode() + b"\n"
        entry = {
            "case_id": case["case_id"],
            "bytes": len(case_content),
            "sha256": hashlib.sha256(case_content).hexdigest(),
        }

        def manifest(entries: list[dict[str, object]]) -> bytes:
            return json.dumps(
                {
                    "schema_version": 1,
                    "kind": "private-replay-manifest",
                    "cases": entries,
                },
                indent=2,
                sort_keys=True,
            ).encode() + b"\n"

        old_empty = manifest([])
        new_selected = manifest([entry])
        for name, journal in (
            (
                "equal-hashes",
                replay._journal_document(old_empty, old_empty, [entry]),
            ),
            (
                "duplicate-ids",
                replay._journal_document(old_empty, new_selected, [entry, entry]),
            ),
        ):
            content = json.dumps(journal, indent=2, sort_keys=True).encode() + b"\n"
            with self.subTest(name=name), self.assertRaises(replay.ReplayError):
                replay._validated_journal(content)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with replay._private_directory(root, "cases", create=True) as directory_fd:
                replay._write_exclusive_at(
                    directory_fd, f"{case['case_id']}.json", case_content
                )
            malicious = replay._journal_document(
                new_selected,
                manifest([entry, {"case_id": "f" * 32, "bytes": 1, "sha256": HASH}]),
                [entry],
            )
            self.raw_private_file(
                root,
                replay.ENROLLMENT_JOURNAL_NAME,
                json.dumps(malicious, indent=2, sort_keys=True).encode() + b"\n",
            )
            with self.assertRaises(replay.ReplayError):
                replay._recover_enrollment(root, new_selected)
            self.assertTrue(
                (root / replay.PRIVATE_RELATIVE / "cases" / f"{case['case_id']}.json").is_file()
            )
            self.assertTrue(
                (root / replay.PRIVATE_RELATIVE / replay.ENROLLMENT_JOURNAL_NAME).is_file()
            )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            malicious = replay._journal_document(old_empty, new_selected, [entry])
            self.raw_private_file(
                root,
                replay.ENROLLMENT_JOURNAL_NAME,
                json.dumps(malicious, indent=2, sort_keys=True).encode() + b"\n",
            )
            wrong_content = b'{"not":"the journal-owned case"}\n'
            with replay._private_directory(root, "cases", create=True) as directory_fd:
                replay._write_exclusive_at(
                    directory_fd,
                    f"{case['case_id']}.json",
                    wrong_content,
                )
            with self.assertRaises(replay.ReplayError):
                replay._recover_enrollment(root, old_empty)
            self.assertEqual(
                (
                    root
                    / replay.PRIVATE_RELATIVE
                    / "cases"
                    / f"{case['case_id']}.json"
                ).read_bytes(),
                wrong_content,
            )
            self.assertTrue(
                (root / replay.PRIVATE_RELATIVE / replay.ENROLLMENT_JOURNAL_NAME).is_file()
            )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current_new = manifest(
                [{"case_id": "e" * 32, "bytes": 1, "sha256": HASH}]
            )
            malicious = replay._journal_document(old_empty, current_new, [entry])
            self.raw_private_file(
                root,
                replay.ENROLLMENT_JOURNAL_NAME,
                json.dumps(malicious, indent=2, sort_keys=True).encode() + b"\n",
            )
            with self.assertRaises(replay.ReplayError):
                replay._recover_enrollment(root, current_new)
            self.assertTrue(
                (root / replay.PRIVATE_RELATIVE / replay.ENROLLMENT_JOURNAL_NAME).is_file()
            )

    def test_manifest_publication_forces_mode_0644_under_restrictive_umask(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / replay.MANIFEST_RELATIVE
            path.parent.mkdir(parents=True)
            old_document = {
                "schema_version": 1,
                "kind": "private-replay-manifest",
                "cases": [],
            }
            old = json.dumps(old_document, indent=2, sort_keys=True).encode() + b"\n"
            path.write_bytes(old)
            os.chmod(path, 0o644)
            new_document = copy.deepcopy(old_document)
            previous_umask = os.umask(0o077)
            try:
                saved = replay._replace_manifest(root, old, new_document)
            finally:
                os.umask(previous_umask)
            self.assertEqual(path.read_bytes(), saved)
            self.assertEqual(path.stat().st_mode & 0o777, 0o644)

    def test_manifest_replace_ambiguous_fsync_can_be_rolled_back_exactly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / replay.MANIFEST_RELATIVE
            path.parent.mkdir(parents=True)
            old_document = {
                "schema_version": 1,
                "kind": "private-replay-manifest",
                "cases": [],
            }
            old = json.dumps(old_document, indent=2, sort_keys=True).encode() + b"\n"
            path.write_bytes(old)
            os.chmod(path, 0o644)
            new_document = {
                **old_document,
                "cases": [{"case_id": "5" * 32, "bytes": 1, "sha256": HASH}],
            }
            new = json.dumps(new_document, indent=2, sort_keys=True).encode() + b"\n"
            real_fsync = os.fsync
            calls = 0

            def fail_after_replace(descriptor: int) -> None:
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise OSError("injected after replace")
                real_fsync(descriptor)

            with mock.patch.object(os, "fsync", side_effect=fail_after_replace):
                with self.assertRaises(replay.ReplayError):
                    replay._replace_manifest(root, old, new_document)
            self.assertEqual(path.read_bytes(), new)
            restored = replay._replace_manifest(root, new, old_document)
            self.assertEqual(restored, old)
            self.assertEqual(path.read_bytes(), old)

    def test_case_directory_enumeration_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with replay._private_directory(root, "cases", create=True) as directory_fd:
                for index in range(19):
                    descriptor = os.open(
                        f"entry-{index}",
                        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                        0o600,
                        dir_fd=directory_fd,
                    )
                    os.close(descriptor)
            with mock.patch.object(replay, "MAX_CASES", 2):
                with self.assertRaisesRegex(replay.ReplayError, "too large"):
                    replay._case_file_names(root)

    def test_read_cases_rejects_duplicate_campaign_or_target(self):
        for duplicate in ("campaign", "target"):
            with self.subTest(duplicate=duplicate), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                cases = [replay_case(1), replay_case(2)]
                if duplicate == "campaign":
                    cases[1]["provenance"]["campaign_id"] = cases[0]["provenance"]["campaign_id"]
                else:
                    cases[1]["provenance"]["target"] = cases[0]["provenance"]["target"]
                entries = []
                with replay._private_directory(root, "cases", create=True) as directory_fd:
                    for case in cases:
                        content = json.dumps(case, indent=2, sort_keys=True).encode() + b"\n"
                        entry = {
                            "case_id": case["case_id"],
                            "bytes": len(content),
                            "sha256": hashlib.sha256(content).hexdigest(),
                        }
                        entries.append(entry)
                        replay._write_exclusive_at(
                            directory_fd, f"{case['case_id']}.json", content
                        )
                manifest_path = root / replay.MANIFEST_RELATIVE
                manifest_path.parent.mkdir(parents=True)
                manifest_path.write_text(
                    json.dumps(
                        {
                            "schema_version": 1,
                            "kind": "private-replay-manifest",
                            "cases": entries,
                        },
                        indent=2,
                        sort_keys=True,
                    )
                    + "\n",
                    encoding="utf-8",
                )
                with self.assertRaisesRegex(replay.ReplayError, "duplicate"):
                    replay.read_cases(root)

    def test_selected_case_corruption_and_orphans_are_counted_deterministically(self):
        for condition in ("missing", "malformed", "hash", "orphan"):
            with self.subTest(condition=condition), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                case = replay_case()
                valid_content = json.dumps(case, indent=2, sort_keys=True).encode() + b"\n"
                selected_content = b"{" if condition == "malformed" else valid_content
                entry = {
                    "case_id": case["case_id"],
                    "bytes": len(selected_content),
                    "sha256": hashlib.sha256(selected_content).hexdigest(),
                }
                if condition == "hash":
                    entry["sha256"] = "f" * 64
                if condition != "missing":
                    with replay._private_directory(root, "cases", create=True) as directory_fd:
                        replay._write_exclusive_at(
                            directory_fd,
                            f"{case['case_id']}.json",
                            selected_content,
                        )
                        if condition == "orphan":
                            replay._write_exclusive_at(
                                directory_fd, "f" * 32 + ".json", b"orphan"
                            )
                with mock.patch.object(replay, "_revalidate_case"):
                    cases, invalid, storage_invalid = replay._loaded_cases(
                        {
                            "schema_version": 1,
                            "kind": "private-replay-manifest",
                            "cases": [entry],
                        },
                        root,
                        [],
                        HEAD,
                        HEAD,
                    )
                if condition == "orphan":
                    self.assertEqual(len(cases), 1)
                    self.assertEqual(len(invalid), 0)
                    self.assertEqual(storage_invalid, 1)
                else:
                    self.assertEqual(cases, [])
                    self.assertEqual(len(invalid), 1)
                    self.assertEqual(storage_invalid, 0)

    def test_self_rehashed_case_still_requires_canonical_reconstruction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            canonical = replay_case()
            forged = copy.deepcopy(canonical)
            forged["graph"]["nodes"][2]["score_millionths"] = 710_000
            replay.validate_case(forged)
            forged_content = (
                json.dumps(forged, indent=2, sort_keys=True).encode() + b"\n"
            )
            entry = {
                "case_id": forged["case_id"],
                "bytes": len(forged_content),
                "sha256": hashlib.sha256(forged_content).hexdigest(),
            }
            with (
                mock.patch.object(
                    replay,
                    "_case_file_names",
                    return_value={f"{forged['case_id']}.json"},
                ),
                mock.patch.object(replay, "_case_bytes", return_value=forged_content),
                mock.patch.object(
                    replay, "reconstruct_case", return_value=canonical
                ) as reconstruct,
            ):
                cases, invalid, storage_invalid = replay._loaded_cases(
                    {
                        "schema_version": 1,
                        "kind": "private-replay-manifest",
                        "cases": [entry],
                    },
                    root,
                    [],
                    HEAD,
                    HEAD,
                )
            self.assertEqual(cases, [])
            self.assertEqual(len(invalid), 1)
            self.assertEqual(storage_invalid, 0)
            reconstruct.assert_called_once()

    def test_empty_manifest_ignores_hostile_private_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / replay.MANIFEST_RELATIVE
            path.parent.mkdir(parents=True)
            path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "kind": "private-replay-manifest",
                        "cases": [],
                    }
                ),
                encoding="utf-8",
            )
            os.chmod(path, 0o644)
            outside = root / "outside"
            outside.mkdir()
            (root / ".decomp-replay").symlink_to(outside, target_is_directory=True)
            self.assertFalse(replay.certify(root)["certified"])
            self.assertIsNone(replay.current_passing_certificate(root))

    def test_pending_journal_fails_certification_and_advice_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            case = replay_case()
            case_content = json.dumps(case, indent=2, sort_keys=True).encode() + b"\n"
            entry = {
                "case_id": case["case_id"],
                "bytes": len(case_content),
                "sha256": hashlib.sha256(case_content).hexdigest(),
            }
            path = root / replay.MANIFEST_RELATIVE
            path.parent.mkdir(parents=True)
            path.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "kind": "private-replay-manifest",
                        "cases": [entry],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            os.chmod(path, 0o644)
            with replay._private_directory(root, "cases", create=True) as directory_fd:
                replay._write_exclusive_at(
                    directory_fd, f"{case['case_id']}.json", case_content
                )
            with replay._suite_lock(root, create=True, exclusive=True):
                pass
            self.raw_private_file(root, replay.ENROLLMENT_JOURNAL_NAME, b"{}\n")
            document = replay.evaluate_suite(root)
            self.assertEqual(document["status"], "failed")
            self.assertIn("enrollment-transaction-pending", document["failure_codes"])
            self.assertIsNone(replay.current_passing_certificate(root))
            with mock.patch.object(route, "current_passing_certificate", return_value=None):
                advice = route.advice_document(observation(), root=root)
            self.assertEqual(advice["status"], "withheld")
            self.assertIsNone(advice["route"])

    def test_certificate_pointer_identity_and_content_tamper_are_withheld(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document: dict[str, object] = {
                "schema_version": 1,
                "kind": "private-replay-certificate",
                "status": "passed",
            }
            document["content_sha256"] = replay._json_hash(document)
            content = json.dumps(document, indent=2, sort_keys=True).encode() + b"\n"
            with replay._private_directory(root, replay.CERTIFICATES_NAME, create=True) as directory_fd:
                replay._write_exclusive_at(
                    directory_fd, f"{document['content_sha256']}.json", content
                )
            pointer = {
                "schema_version": 1,
                "kind": "private-replay-certificate-pointer",
                "content_sha256": document["content_sha256"],
                "bytes": len(content),
                "sha256": hashlib.sha256(content).hexdigest(),
            }
            with replay._private_directory(root, create=True) as directory_fd:
                replay._replace_at(
                    directory_fd,
                    replay.CERTIFICATE_POINTER_NAME,
                    json.dumps(pointer, indent=2, sort_keys=True).encode() + b"\n",
                )
            self.assertEqual(replay._current_certificate(root), document)
            for field, value in (
                ("schema_version", True),
                ("kind", "wrong"),
                ("sha256", "f" * 64),
            ):
                changed = dict(pointer)
                changed[field] = value
                with replay._private_directory(root) as directory_fd:
                    replay._replace_at(
                        directory_fd,
                        replay.CERTIFICATE_POINTER_NAME,
                        json.dumps(changed, indent=2, sort_keys=True).encode() + b"\n",
                    )
                self.assertIsNone(replay._current_certificate(root))

    def test_unsafe_publication_never_reports_certified_or_writes(self):
        manifest = {
            "schema_version": 1,
            "kind": "private-replay-manifest",
            "cases": [{"case_id": "1" * 32, "bytes": 1, "sha256": HASH}],
        }
        content = b"manifest"
        document = {
            "status": "passed",
            "thresholds": dict(replay.THRESHOLDS),
            "aggregate": {},
            "failure_codes": [],
            "content_sha256": HASH,
        }

        @contextmanager
        def locked(*_args: object, **_kwargs: object):
            yield

        with (
            mock.patch.object(replay, "_manifest_document", return_value=(manifest, content)),
            mock.patch.object(replay, "_private_root_exists", return_value=True),
            mock.patch.object(replay, "_suite_lock", side_effect=locked),
            mock.patch.object(replay, "_evaluate_suite_unlocked", return_value=document),
            mock.patch.object(replay, "_git_ref", return_value=HEAD),
            mock.patch.object(replay, "_symbolic_branch", return_value="agent/continuous"),
            mock.patch.object(replay, "_manifest_matches_head", return_value=False),
            mock.patch.object(
                replay,
                "_private_directory",
                side_effect=AssertionError("private write attempted"),
            ),
        ):
            summary = replay.certify(Path("/tmp/replay-test-root"))
        self.assertFalse(summary["certified"])
        self.assertEqual(summary["status"], "failed")
        self.assertIn("publication-inputs-invalid", summary["failure_codes"])

    def test_public_summary_never_serializes_private_fields(self):
        sentinel = "PRIVATE-SENTINEL-/secret/path-campaign-session"
        summary = replay._public_certificate(
            {
                "status": "failed",
                "thresholds": dict(replay.THRESHOLDS),
                "aggregate": {"cases": 1, "invalid": 1},
                "failure_codes": ["invalid-cases"],
                "results": [{"case_id": sentinel, "reason": sentinel}],
                "bindings": {"path": sentinel, "sha256": sentinel},
                "content_sha256": sentinel,
            }
        )
        serialized = json.dumps(summary, sort_keys=True)
        self.assertNotIn("PRIVATE-SENTINEL", serialized)
        self.assertNotIn("content_sha256", serialized)

    def test_replay_cli_unexpected_failure_is_generic(self):
        output = StringIO()
        error = StringIO()
        with (
            mock.patch.object(
                replay,
                "list_document",
                side_effect=RuntimeError("PRIVATE-SENTINEL /secret/path"),
            ),
            redirect_stdout(output),
            redirect_stderr(error),
        ):
            self.assertEqual(replay.main(["list", "--json"]), 1)
        self.assertEqual(output.getvalue(), "")
        self.assertEqual(
            error.getvalue(), "error: the private replay operation failed\n"
        )


class EnrollmentTransactionTests(unittest.TestCase):
    def write_empty_manifest(self, root: Path) -> bytes:
        document = {
            "schema_version": 1,
            "kind": "private-replay-manifest",
            "cases": [],
        }
        content = json.dumps(document, indent=2, sort_keys=True).encode() + b"\n"
        path = root / replay.MANIFEST_RELATIVE
        path.parent.mkdir(parents=True)
        path.write_bytes(content)
        os.chmod(path, 0o644)
        return content

    @contextmanager
    def mocked_inputs(
        self,
        root: Path,
        *,
        loaded_cases: list[dict[str, object]] | None = None,
        enrollment_effect: object = None,
    ):
        with (
            mock.patch.object(replay, "_git_ref", return_value=HEAD),
            mock.patch.object(replay, "_symbolic_branch", return_value="agent/continuous"),
            mock.patch.object(replay, "_private_root_is_untracked", return_value=True),
            mock.patch.object(replay, "_enrollment_campaign", return_value=("meta", HASH)),
            mock.patch.object(replay, "_manifest_append_base"),
            mock.patch.object(replay, "_git_blob", return_value=b'{"campaign_id":"source"}\n'),
            mock.patch.object(
                replay,
                "_enrollment_inputs",
                side_effect=enrollment_effect,
            ),
            mock.patch.object(
                replay,
                "_loaded_cases",
                return_value=(loaded_cases or [], [], 0),
            ),
        ):
            yield

    @staticmethod
    def reconstructed(case_id: str, target: str, session_id: str, **_kwargs: object) -> dict[str, object]:
        index = int(target, 16) - 0x00401000
        case = replay_case(max(index // 0x10, 1))
        case["case_id"] = case_id
        case["provenance"]["target"] = target
        case["provenance"]["session_id"] = session_id
        case["provenance"]["campaign_id"] = f"source-{target}"
        return case

    def test_batch_prevalidates_all_cases_before_one_manifest_replace(self):
        candidates = [
            ("0x00401010", "20260909T120000Z-000000000001"),
            ("0x00401020", "20260909T120000Z-000000000002"),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_empty_manifest(root)
            with self.mocked_inputs(root), mock.patch.object(
                replay, "reconstruct_case", side_effect=self.reconstructed
            ):
                result = replay.enroll_batch(candidates, root=root)
            self.assertEqual(result["enrolled_count"], 2)
            manifest = replay.read_manifest(root)
            self.assertEqual(len(manifest["cases"]), 2)
            self.assertFalse(
                (root / replay.PRIVATE_RELATIVE / replay.ENROLLMENT_JOURNAL_NAME).exists()
            )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = self.write_empty_manifest(root)
            calls = 0

            def fail_second(*args: object, **kwargs: object) -> dict[str, object]:
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise replay.ReplayError("invalid second candidate")
                return self.reconstructed(*args, **kwargs)

            with self.mocked_inputs(root), mock.patch.object(
                replay, "reconstruct_case", side_effect=fail_second
            ):
                with self.assertRaises(replay.ReplayError):
                    replay.enroll_batch(candidates, root=root)
            self.assertEqual((root / replay.MANIFEST_RELATIVE).read_bytes(), original)
            self.assertEqual(replay._case_file_names(root), set())
            self.assertFalse(replay._enrollment_journal_exists(root))

    def test_batch_rolls_back_stale_end_check_and_prechecks_capacity(self):
        candidate = [("0x00401010", "20260909T120000Z-000000000001")]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = self.write_empty_manifest(root)
            with self.mocked_inputs(
                root,
                enrollment_effect=[None, None, replay.ReplayError("stale")],
            ), mock.patch.object(
                replay, "reconstruct_case", side_effect=self.reconstructed
            ):
                with self.assertRaises(replay.ReplayError):
                    replay.enroll_batch(candidate, root=root)
            self.assertEqual((root / replay.MANIFEST_RELATIVE).read_bytes(), original)
            self.assertEqual(replay._case_file_names(root), set())
            self.assertFalse(replay._enrollment_journal_exists(root))

    def test_batch_preserves_transaction_on_byte_ambiguous_manifest_publish(self):
        candidate = [("0x00401010", "20260909T120000Z-000000000001")]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old_content = self.write_empty_manifest(root)
            published: list[bytes] = []

            def publish_noncanonical(
                _root: Path,
                _expected: bytes,
                document: dict[str, object],
            ) -> bytes:
                content = json.dumps(
                    document, sort_keys=True, separators=(",", ":")
                ).encode() + b"\n"
                (root / replay.MANIFEST_RELATIVE).write_bytes(content)
                os.chmod(root / replay.MANIFEST_RELATIVE, 0o644)
                published.append(content)
                raise replay.ReplayError("failure after manifest rename")

            with (
                self.mocked_inputs(root),
                mock.patch.object(
                    replay, "reconstruct_case", side_effect=self.reconstructed
                ),
                mock.patch.object(
                    replay, "_replace_manifest", side_effect=publish_noncanonical
                ),
                self.assertRaises(replay.ReplayError),
            ):
                replay.enroll_batch(candidate, root=root)
            self.assertTrue(published)
            self.assertNotEqual(published[0], old_content)
            self.assertEqual(
                (root / replay.MANIFEST_RELATIVE).read_bytes(), published[0]
            )
            self.assertEqual(len(replay._case_file_names(root)), 1)
            self.assertTrue(replay._enrollment_journal_exists(root))

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_empty_manifest(root)
            existing = [replay_case(index) for index in range(1, 3)]
            with (
                mock.patch.object(replay, "MAX_CASES", 2),
                self.mocked_inputs(root, loaded_cases=existing),
                mock.patch.object(replay, "reconstruct_case") as reconstruct,
            ):
                with self.assertRaisesRegex(replay.ReplayError, "case limit"):
                    replay.enroll_batch(candidate, root=root)
            reconstruct.assert_not_called()
            self.assertFalse(replay._enrollment_journal_exists(root))

    def test_enrollment_holds_campaign_lock_before_suite_work(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_empty_manifest(root)
            entered = threading.Event()
            release = threading.Event()
            contender_acquired = threading.Event()
            error: list[Exception] = []

            def paused_ref(_root: Path, _name: str) -> str:
                entered.set()
                self.assertTrue(release.wait(timeout=2))
                return HEAD

            def enroll_worker() -> None:
                try:
                    replay.enroll_batch(
                        [("0x00401010", "20260909T120000Z-000000000001")],
                        root=root,
                    )
                except Exception as caught:
                    error.append(caught)

            state = root / "build/decomp-campaign-state.json"
            state.parent.mkdir(parents=True)
            with (
                mock.patch.object(replay, "_git_ref", side_effect=paused_ref),
                mock.patch.object(replay, "_private_root_is_untracked", return_value=False),
            ):
                worker = threading.Thread(target=enroll_worker)
                worker.start()
                self.assertTrue(entered.wait(timeout=2))

                def contend() -> None:
                    with campaigns._campaign_state_lock(state):
                        contender_acquired.set()

                contender = threading.Thread(target=contend)
                contender.start()
                time.sleep(0.05)
                self.assertFalse(contender_acquired.is_set())
                release.set()
                worker.join(timeout=2)
                contender.join(timeout=2)
            self.assertFalse(worker.is_alive())
            self.assertFalse(contender.is_alive())
            self.assertTrue(contender_acquired.is_set())
            self.assertTrue(error)


class CampaignLockTests(unittest.TestCase):
    def test_campaign_start_rejects_live_and_dangling_state_symlinks(self):
        for dangling in (False, True):
            with self.subTest(dangling=dangling), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                state = root / "state.json"
                target = root / "state-target.json"
                if not dangling:
                    target.write_text("{}\n", encoding="utf-8")
                state.symlink_to(target)
                with self.assertRaisesRegex(ValueError, "active campaign already exists"):
                    campaigns.start_campaign(
                        state,
                        "meta",
                        [],
                        "private-replay-enrollment",
                        worktree_root=root,
                        lane="meta",
                    )
                self.assertTrue(state.is_symlink())

    def test_campaign_lock_serializes_and_rejects_static_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / "state.json"
            acquired = threading.Event()

            def contender() -> None:
                with campaigns._campaign_state_lock(state):
                    acquired.set()

            with campaigns._campaign_state_lock(state):
                thread = threading.Thread(target=contender)
                thread.start()
                time.sleep(0.05)
                self.assertFalse(acquired.is_set())
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            self.assertTrue(acquired.is_set())

            lock = state.with_name(f".{state.name}.lock")
            lock.unlink()
            outside = root / "outside"
            outside.write_text("outside", encoding="utf-8")
            lock.symlink_to(outside)
            with self.assertRaises(ValueError):
                with campaigns._campaign_state_lock(state):
                    pass

    def test_standard_finalization_holds_lock_during_all_derivation(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "state.json"
            entered = threading.Event()
            release = threading.Event()
            contender_acquired = threading.Event()
            result: list[dict[str, object]] = []

            def locked_finalize(*_args: object, **_kwargs: object) -> dict[str, object]:
                entered.set()
                self.assertTrue(release.wait(timeout=2))
                return {"status": "done"}

            def run_finalize() -> None:
                result.append(
                    campaigns.finalize_standard(
                        state,
                        "source",
                        mode="refinement",
                        targets=["0x00401000"],
                        staged=True,
                    )
                )

            def mutate() -> None:
                with campaigns._campaign_state_lock(state):
                    contender_acquired.set()

            with mock.patch.object(
                campaigns,
                "_finalize_standard_locked",
                side_effect=locked_finalize,
            ):
                finalizer = threading.Thread(target=run_finalize)
                finalizer.start()
                self.assertTrue(entered.wait(timeout=2))
                mutator = threading.Thread(target=mutate)
                mutator.start()
                time.sleep(0.05)
                self.assertFalse(contender_acquired.is_set())
                release.set()
                finalizer.join(timeout=2)
                mutator.join(timeout=2)
            self.assertFalse(finalizer.is_alive())
            self.assertFalse(mutator.is_alive())
            self.assertTrue(contender_acquired.is_set())
            self.assertEqual(result, [{"status": "done"}])


class WrapperParityTests(unittest.TestCase):
    def test_bash_and_powershell_expose_private_replay_commands(self):
        root = Path(__file__).resolve().parents[2]
        bash = (root / "tools/decomp").read_text(encoding="utf-8")
        powershell = (root / "tools/decomp.ps1").read_text(encoding="utf-8")
        subprocess.run(
            ["bash", "-n", str(root / "tools/decomp")],
            cwd=root,
            check=True,
            capture_output=True,
        )
        for command, module in (
            ("replay", "decomp_replay.py"),
            ("route", "decomp_route.py"),
        ):
            with self.subTest(shell="bash", command=command):
                self.assertIn(
                    f"    {command})\n        exec python tools/{module} \"$@\"",
                    bash,
                )
            with self.subTest(shell="powershell", command=command):
                dispatch = f'if ($Command -eq "{command}")'
                self.assertIn(dispatch, powershell)
                self.assertIn(f'"tools\\{module}"', powershell)
                self.assertLess(powershell.index(dispatch), powershell.index("$Vcvars ="))
        self.assertIn("replay [...]", bash)
        self.assertIn("route advise", bash)
        self.assertIn("replay [args]", powershell)
        self.assertIn("route advise", powershell)

    def test_experiment_branch_measure_and_seal_arguments_match(self):
        root = Path(__file__).resolve().parents[2]
        bash = (root / "tools/decomp").read_text(encoding="utf-8")
        powershell = (root / "tools/decomp.ps1").read_text(encoding="utf-8")
        for text in (bash, powershell):
            self.assertIn("start|branch|measure|try|seal|status", text)
            self.assertIn("decomp_experiment.py", text)
            self.assertIn("measure-plan", text)
            self.assertIn("branch", text)
            self.assertIn("seal", text)
        self.assertIn(
            'python tools/decomp_experiment.py branch "$address" "$label" "$@"',
            bash,
        )
        self.assertIn(
            'measure-plan \\\n                    "$address" --trial "$label" "$@" --format lines',
            bash,
        )
        self.assertIn(
            '$Action -in @("status", "best", "advise", "report", "seal")',
            powershell,
        )
        self.assertIn(
            'measure-plan $Address --trial $Label @Extra --format lines',
            powershell,
        )


if __name__ == "__main__":
    unittest.main()
