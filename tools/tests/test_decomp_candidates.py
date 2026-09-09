from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
for entry in (str(ROOT), str(TOOLS)):
    if entry not in sys.path:
        sys.path.insert(0, entry)

spec = importlib.util.spec_from_file_location(
    "toy2_decomp_candidates", TOOLS / "decomp_candidates.py"
)
assert spec is not None and spec.loader is not None
candidates = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = candidates
spec.loader.exec_module(candidates)


def make(address: int, name: str, **kwargs):
    if kwargs.get("actionable_mismatch"):
        kwargs.setdefault(
            "mismatch_artifact", f"build/decomp-diffs/0x{address:08X}.txt"
        )
        kwargs.setdefault("mismatch_windows", 1)
        kwargs.setdefault("mismatch_classifications", ("instruction",))
    return candidates.Candidate(address=address, name=name, **kwargs)


def graph(callees=None, callers=None):
    return candidates.DependencyGraph(
        callees={key: frozenset(value) for key, value in (callees or {}).items()},
        callers={key: frozenset(value) for key, value in (callers or {}).items()},
        indirect_calls={},
        indirect_jumps={},
    )


def campaign_record(
    address: int,
    campaign_id: str,
    retained_bytes: float,
    *,
    expected_bytes: float = 400.0,
    minutes: float = 10.0,
    timestamp: str = "2026-09-01T10:00:00+00:00",
    result: str | None = None,
    models: list[str] | None = None,
):
    return {
        "schema_version": 3,
        "record_type": "campaign",
        "campaign_id": campaign_id,
        "addresses": [f"0x{address:08X}"],
        "mode": "refinement",
        "result": result or ("source" if retained_bytes > 0.0 else "no-source"),
        "minutes": minutes,
        "expected_retained_bytes": expected_bytes,
        "effective_bytes": retained_bytes,
        "target_deltas": {
            f"0x{address:08X}": {
                "effective_bytes": retained_bytes,
                "initialized_bytes": 0.0,
            }
        },
        "ruled_out_models": models or [],
        "ended_at": timestamp,
        "timestamp": timestamp,
    }


def select_lane(items, lane, records=None, queue=None):
    return candidates.select(
        items,
        namespace=None,
        stubs_only=False,
        leaves_only=False,
        near_only=False,
        max_size=None,
        exclude_capped=True,
        queue=queue,
        lane=lane,
        records=records or [],
    )


class CandidateTests(unittest.TestCase):
    def test_candidate_subsystem_uses_source_file_for_global_name(self):
        item = make(
            0x401000,
            "GlobalFunction",
            size=1,
            source="src/PlayerControl.cpp",
        )
        self.assertEqual(candidates._candidate_subsystem(item), "PlayerControl")
        global_item = make(0x402000, "AnotherGlobal", size=1)
        self.assertEqual(candidates._candidate_subsystem(global_item), "(global)")

    def test_closure_prefers_a_fresh_target_to_an_evidence_unlocked_retry(self):
        fresh = make(
            0x401000,
            "N::Fresh",
            size=100,
            state="FUNCTION",
            match=0.8,
            actionable_mismatch=True,
            tool_artifact="fresh",
        )
        retry = make(
            0x402000,
            "N::Retry",
            size=100,
            state="FUNCTION",
            match=0.9,
            actionable_mismatch=True,
            tool_artifact="retry",
            prior_attempts=1,
            retry_eligible=True,
            fresh_evidence=True,
        )
        with patch.object(candidates, "apply_lane_model"):
            for item in (fresh, retry):
                item.lane = "closure"
                item.success_probability = 0.8
                item.median_minutes = 5.0
            selected = candidates.select(
                [retry, fresh],
                namespace=None,
                stubs_only=False,
                leaves_only=False,
                near_only=False,
                max_size=None,
                exclude_capped=False,
                queue="refinement",
                lane="closure",
                records=[],
            )
        self.assertEqual([item.address for item in selected], [fresh.address, retry.address])

    def test_zero_yield_penalty_has_a_five_percent_floor(self):
        baseline = make(
            0x401000, "N::Baseline", size=1000, state="FUNCTION", match=0.5
        )
        twice = make(
            0x402000,
            "N::Twice",
            size=1000,
            state="FUNCTION",
            match=0.5,
            penalty_attempts=2,
        )
        many = make(
            0x403000,
            "N::Many",
            size=1000,
            state="FUNCTION",
            match=0.5,
            penalty_attempts=20,
        )
        for item in (baseline, twice, many):
            candidates.estimate_yield(item, "refinement")
        self.assertAlmostEqual(
            twice.expected_retained_bytes,
            baseline.expected_retained_bytes * 0.05,
        )
        self.assertEqual(many.expected_retained_bytes, twice.expected_retained_bytes)

    def test_new_evidence_clears_the_active_zero_yield_penalty(self):
        fresh = make(
            0x401000, "N::Fresh", size=1000, state="FUNCTION", match=0.5
        )
        reset = make(
            0x402000,
            "N::Reset",
            size=1000,
            state="FUNCTION",
            match=0.5,
            prior_attempts=3,
            prior_zero_yield_attempts=3,
            penalty_attempts=0,
        )
        for item in (fresh, reset):
            candidates.estimate_yield(item, "refinement")
            candidates.score(item)
        self.assertEqual(reset.expected_retained_bytes, fresh.expected_retained_bytes)
        self.assertEqual(reset.rank, fresh.rank)
        self.assertIn("new evidence reset", "; ".join(reset.reasons))

    def test_zero_yield_history_reduces_expected_rate_and_rank(self):
        fresh = make(
            0x401000, "N::Fresh", size=1000, state="FUNCTION", match=0.5
        )
        retried = make(
            0x402000,
            "N::Retried",
            size=1000,
            state="FUNCTION",
            match=0.5,
            prior_attempts=1,
            prior_zero_yield_attempts=1,
            prior_minutes=12,
        )
        chosen = candidates.select(
            [retried, fresh],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="refinement",
            yield_order=True,
        )
        self.assertEqual(chosen[0].address, fresh.address)
        self.assertLess(retried.expected_bytes_per_minute, fresh.expected_bytes_per_minute)
        self.assertLess(retried.rank, fresh.rank)

    def test_yield_estimate_penalizes_oversized_and_blocked_coverage(self):
        small = make(0x401000, "N::Small", size=1000, state="STUB", source="N.cpp")
        oversized = make(
            0x402000, "N::Oversized", size=13000, state="STUB", source="N.cpp"
        )
        blocked = make(
            0x403000,
            "N::Blocked",
            size=1000,
            state="STUB",
            source="N.cpp",
            manual_blocker=True,
        )
        for item in (small, oversized, blocked):
            candidates.estimate_yield(item, "coverage")
        self.assertGreater(small.expected_bytes_per_minute, oversized.expected_bytes_per_minute)
        self.assertGreater(small.expected_bytes_per_minute, blocked.expected_bytes_per_minute)

    def test_refinement_yield_penalizes_a_large_gate_deficit(self):
        above_gate = make(
            0x401000, "N::Above", size=1000, state="FUNCTION", match=0.55
        )
        below_gate = make(
            0x402000, "N::Below", size=1000, state="FUNCTION", match=0.10
        )
        candidates.estimate_yield(above_gate, "refinement")
        candidates.estimate_yield(below_gate, "refinement")
        self.assertGreater(
            above_gate.expected_bytes_per_minute,
            below_gate.expected_bytes_per_minute,
        )

    def test_parse_map_sorts_and_skips_comments(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "map.txt"
            path.write_text(
                "0x00402000 Second::Function\n# comment\n0x00401000 First::Function\n",
                encoding="utf-8",
            )
            self.assertEqual(
                candidates.parse_map(path),
                [(0x401000, "First::Function"), (0x402000, "Second::Function")],
            )

    def test_report_percentages_and_sizes_use_integer_addresses(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text(json.dumps({"data": [
                {"address": "0x401000", "matching": 0.5, "original_size": 234},
                {"address": "0x402000", "matching": 0.2, "original_size": 1},
            ]}), encoding="utf-8")
            self.assertEqual(candidates.read_match_percentages(path), {
                0x401000: 0.5, 0x402000: 0.2
            })
            self.assertEqual(candidates.read_original_sizes(path), {0x401000: 234})

    def test_compact_blocker_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "blockers.tsv"
            candidates.write_deferral(
                0x401000,
                "Needs the producer layout.",
                (0x402000,),
                path,
                kind="layout",
            )
            self.assertEqual(
                candidates.read_deferrals(path),
                {0x401000: candidates.Deferral(
                    blocked_by=(0x402000,),
                    reason="Needs the producer layout.",
                    kind="layout",
                )},
            )
            self.assertTrue(candidates.clear_deferral(0x401000, path))
            self.assertEqual(candidates.read_deferrals(path), {})

    def test_advisory_blocker_does_not_hide_source_work(self):
        target = make(
            0x401000,
            "N::Target",
            size=3000,
            state="STUB",
            deferred_reason="Needs a layout.",
            manual_blocker=True,
        )
        candidates.add_dependency_evidence([target], graph())
        chosen = candidates.select(
            [target],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            new_work_only=True,
        )
        self.assertEqual([item.address for item in chosen], [0x401000])
        self.assertTrue(target.dependency_ready)

    def test_unfinished_dependency_affects_rank_without_hiding_callers(self):
        goal = make(0x401000, "N::Goal", size=1200, state="STUB")
        leaf = make(0x402000, "N::Leaf", size=80, state="NOT_STARTED")
        dependency_graph = graph(
            callees={0x401000: {0x402000}},
            callers={0x402000: {0x401000}},
        )
        pool = [goal, leaf]
        candidates.add_dependency_evidence(pool, dependency_graph)
        chosen = candidates.select(
            pool,
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            new_work_only=True,
        )
        self.assertEqual({item.address for item in chosen}, {0x401000, 0x402000})
        self.assertEqual(chosen[0].address, 0x402000)
        self.assertFalse(goal.dependency_ready)
        self.assertTrue(leaf.dependency_ready)

    def test_low_similarity_implemented_dependency_is_weak_prerequisite(self):
        goal = make(0x401000, "N::Goal", size=1200, state="STUB")
        dependency = make(
            0x402000, "N::Dependency", size=100, state="FUNCTION", match=0.2
        )
        dependency_graph = graph(
            callees={0x401000: {0x402000}},
            callers={0x402000: {0x401000}},
        )
        candidates.add_dependency_evidence([goal, dependency], dependency_graph)
        self.assertTrue(goal.dependency_ready)
        self.assertTrue(dependency.quality_prerequisite)
        self.assertEqual(goal.weak_dependencies, (0x402000,))

    def test_default_source_filter_excludes_implemented_functions(self):
        source = make(0x401000, "N::Source", size=80, state="STUB")
        implemented = make(
            0x402000, "N::Done", size=80, state="FUNCTION", match=0.5
        )
        chosen = candidates.select(
            [source, implemented],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            new_work_only=True,
        )
        self.assertEqual([item.address for item in chosen], [0x401000])

    def test_near_mode_remains_an_explicit_human_escape_hatch(self):
        implemented = make(
            0x401000, "N::Done", size=80, state="FUNCTION", match=0.5
        )
        chosen = candidates.select(
            [implemented],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=True,
            max_size=None,
            exclude_capped=True,
        )
        self.assertEqual([item.address for item in chosen], [0x401000])

    def test_coverage_excludes_implemented_functions(self):
        source = make(0x401000, "N::Source", size=80, state="STUB")
        unstarted = make(0x402000, "N::New", size=120, state="NOT_STARTED")
        implemented = make(
            0x403000, "N::Done", size=200, state="FUNCTION", match=0.6
        )
        chosen = candidates.select(
            [source, unstarted, implemented],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="coverage",
        )
        self.assertEqual({item.address for item in chosen}, {0x401000, 0x402000})

    def test_refinement_includes_each_nonterminal_source_class(self):
        provisional = make(
            0x401000, "N::Partial", size=100, state="FUNCTION", match=0.6
        )
        tool = make(
            0x402000,
            "N::Tool",
            size=100,
            state="FUNCTION",
            match=0.99,
            tool_artifact="symbol display",
        )
        debt = make(
            0x403000,
            "N::Debt",
            size=100,
            state="FUNCTION",
            match=1.0,
            lint_warnings=1,
        )
        terminal = make(
            0x404000, "N::Terminal", size=100, state="FUNCTION", effective=True
        )
        chosen = candidates.select(
            [provisional, tool, debt, terminal],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="refinement",
        )
        self.assertEqual(
            {item.address for item in chosen}, {0x401000, 0x402000, 0x403000}
        )

    def test_refine_cli_includes_a_non_prerequisite_provisional_function(self):
        provisional = make(
            0x401000,
            "N::Partial",
            size=100,
            map_size=150,
            size_source="ghidra-snapshot",
            state="FUNCTION",
            match=0.6,
        )
        argv = ["decomp_candidates.py", "--refine", "--limit", "0", "--json"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(
                candidates,
                "parse_map",
                return_value=[(provisional.address, provisional.name)],
            ),
            patch.object(
                candidates,
                "load_or_build_candidates",
                return_value=([provisional], False),
            ),
            redirect_stdout(StringIO()) as output,
        ):
            self.assertEqual(candidates.main(), 0)
        rows = json.loads(output.getvalue())
        self.assertEqual([row["address"] for row in rows], ["0x00401000"])
        self.assertFalse(rows[0]["quality_prerequisite"])
        self.assertIn("expected_minutes", rows[0])
        self.assertIsNone(rows[0]["score_ceiling"])
        self.assertTrue(rows[0]["work_target"])

    def test_refinement_ignores_the_coverage_score_ceiling(self):
        implemented = make(
            0x401000,
            "N::Implemented",
            size=300,
            map_size=1000,
            size_source="ghidra-snapshot",
            state="FUNCTION",
            match=0.9,
        )
        chosen = candidates.select(
            [implemented],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="refinement",
            yield_order=True,
        )
        self.assertIsNone(implemented.score_ceiling)
        self.assertFalse(implemented.map_defect)
        self.assertTrue(implemented.work_target)
        self.assertGreater(implemented.match, implemented.size / implemented.map_size)
        self.assertGreater(implemented.expected_retained_bytes, 0.0)
        self.assertEqual(chosen, [implemented])

    def test_score_ceiling_flags_map_defects(self):
        defect = make(
            0x401000,
            "N::Defect",
            size=300,
            map_size=1000,
            size_source="ghidra-snapshot",
            state="STUB",
        )
        unknown = make(0x402000, "N::Unknown", size=1000, map_size=1000)
        overlap = make(
            0x403000,
            "N::Overlap",
            size=1200,
            map_size=1000,
            size_source="ghidra-snapshot",
        )
        self.assertEqual(defect.score_ceiling, 0.3)
        self.assertTrue(defect.map_defect)
        self.assertIsNone(unknown.score_ceiling)
        self.assertFalse(unknown.map_defect)
        self.assertEqual(overlap.score_ceiling, 1.0)

    def test_map_defect_has_no_yield_and_sorts_after_work(self):
        defect = make(
            0x401000,
            "N::Defect",
            size=300,
            map_size=1000,
            size_source="ghidra-snapshot",
            state="STUB",
        )
        work = make(0x402000, "N::Work", size=100, state="STUB")
        chosen = candidates.select(
            [defect, work],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="coverage",
            yield_order=True,
        )
        self.assertEqual([item.address for item in chosen], [work.address, defect.address])
        self.assertEqual(defect.expected_retained_bytes, 0.0)
        self.assertFalse(defect.work_target)
        self.assertIn("map defect", "; ".join(defect.reasons))

    def test_coverage_json_keeps_map_repairs_visible(self):
        defect = make(
            0x401000,
            "N::Defect",
            size=300,
            map_size=1000,
            size_source="ghidra-snapshot",
            state="STUB",
        )
        argv = ["decomp_candidates.py", "--coverage", "--limit", "0", "--json"]
        with (
            patch.object(sys, "argv", argv),
            patch.object(
                candidates,
                "parse_map",
                return_value=[(defect.address, defect.name)],
            ),
            patch.object(
                candidates,
                "load_or_build_candidates",
                return_value=([defect], False),
            ),
            redirect_stdout(StringIO()) as output,
        ):
            self.assertEqual(candidates.main(), 0)
        rows = json.loads(output.getvalue())
        self.assertEqual(len(rows), 1)
        self.assertTrue(rows[0]["map_defect"])
        self.assertFalse(rows[0]["work_target"])
        self.assertEqual(rows[0]["expected_retained_bytes"], 0.0)
        self.assertEqual(rows[0]["expected_bytes_per_minute"], 0.0)

    def test_why_output_shows_score_ceiling(self):
        defect = make(
            0x401000,
            "N::Defect",
            size=300,
            map_size=1000,
            size_source="ghidra-snapshot",
            state="STUB",
        )
        candidates.score(defect)
        output = StringIO()
        with redirect_stdout(output):
            candidates.print_table([defect], 0, True)
        self.assertIn("MAP_REPAIR", output.getvalue())
        self.assertIn("score ceiling 30.0%", output.getvalue())
        self.assertIn("map defect", output.getvalue())

    def test_refine_independent_rejects_for_filter(self):
        argv = [
            "decomp_candidates.py",
            "--refine-independent",
            "--for",
            "0x00401000",
        ]
        with (
            patch.object(sys, "argv", argv),
            redirect_stderr(StringIO()) as error,
            self.assertRaises(SystemExit) as raised,
        ):
            candidates.main()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn(
            "--refine-independent cannot be combined with --for",
            error.getvalue(),
        )

    def test_yield_output_shows_expected_minutes(self):
        item = make(0x401000, "N::Work", size=1000, state="STUB")
        candidates.estimate_yield(item, "coverage")
        output = StringIO()
        with redirect_stdout(output):
            candidates.print_table([item], 0, False, True)
        self.assertIn("EST MIN", output.getvalue())
        self.assertIn(f"{item.expected_minutes:>7.1f}", output.getvalue())

    def test_refinement_ranks_weak_prerequisite_before_larger_opportunity(self):
        goal = make(0x401000, "N::Goal", size=1200, state="STUB")
        prerequisite = make(
            0x402000, "N::Prerequisite", size=80, state="FUNCTION", match=0.6
        )
        other = make(
            0x403000, "N::Other", size=400, state="FUNCTION", match=0.1
        )
        pool = [goal, prerequisite, other]
        candidates.add_dependency_evidence(
            pool, graph(callees={0x401000: {0x402000}})
        )
        chosen = candidates.select(
            pool,
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="refinement",
        )
        self.assertEqual(chosen[0].address, 0x402000)

    def test_refinement_uses_unresolved_byte_opportunity(self):
        small = make(
            0x401000, "N::Small", size=100, state="FUNCTION", match=0.1
        )
        large = make(
            0x402000, "N::Large", size=500, state="FUNCTION", match=0.6
        )
        chosen = candidates.select(
            [small, large],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="refinement",
        )
        self.assertEqual(chosen[0].address, 0x402000)

    def test_independent_refinement_requires_ready_saved_comparison(self):
        independent = make(
            0x401000, "N::Independent", size=500, state="FUNCTION", match=0.6
        )
        small = make(
            0x402000, "N::Small", size=100, state="FUNCTION", match=0.6
        )
        unscored = make(
            0x403000, "N::Unscored", size=500, state="FUNCTION"
        )
        tool = make(
            0x404000,
            "N::Tool",
            size=500,
            state="FUNCTION",
            match=0.6,
            tool_artifact="symbol display",
        )
        prerequisite = make(
            0x405000, "N::Prerequisite", size=500, state="FUNCTION", match=0.6
        )
        prerequisite.quality_prerequisite = True
        chosen = candidates.select(
            [small, unscored, tool, prerequisite, independent],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="refinement",
            yield_order=True,
            independent_refinement=True,
        )
        self.assertEqual([item.address for item in chosen], [independent.address])
        self.assertIn("saved comparison available", independent.reasons)

    def test_independent_refinement_uses_yield_and_history_ranking(self):
        fresh = make(
            0x401000, "N::Fresh", size=500, state="FUNCTION", match=0.6
        )
        attempted = make(
            0x402000,
            "N::Attempted",
            size=500,
            state="FUNCTION",
            match=0.6,
            prior_attempts=1,
            prior_zero_yield_attempts=1,
            prior_minutes=5.0,
        )
        chosen = candidates.select(
            [attempted, fresh],
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            queue="refinement",
            yield_order=True,
            independent_refinement=True,
        )
        self.assertEqual(
            [item.address for item in chosen], [fresh.address, attempted.address]
        )
        self.assertIn("prior zero-yield campaign", "; ".join(attempted.reasons))

    def test_closure_lane_surfaces_tool_only_and_above_99_percent_work(self):
        tool_only = make(
            0x401000,
            "N::ToolOnly",
            size=80,
            state="FUNCTION",
            match=1.0,
            tool_artifact="symbol display",
        )
        almost_exact = make(
            0x402000,
            "N::AlmostExact",
            size=800,
            state="FUNCTION",
            match=0.995,
        )
        below_closure = make(
            0x403000,
            "N::BelowClosure",
            size=800,
            state="FUNCTION",
            match=0.98,
        )
        chosen = select_lane(
            [below_closure, almost_exact, tool_only],
            "closure",
            [
                dict(
                    campaign_record(
                        tool_only.address,
                        "closure-history",
                        8.0,
                        expected_bytes=8.0,
                    ),
                    lane="closure",
                )
            ],
            queue="refinement",
        )
        self.assertEqual(
            [item.address for item in chosen],
            [almost_exact.address, tool_only.address],
        )
        self.assertTrue(all(item.closure_eligible for item in chosen))

    def test_production_lane_enforces_all_source_gates(self):
        good = make(
            0x401000,
            "N::Good",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        below_match = make(
            0x402000,
            "N::Below",
            size=1000,
            state="FUNCTION",
            match=0.49,
            actionable_mismatch=True,
        )
        too_large = make(
            0x403000,
            "N::Large",
            size=3001,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        too_resolved = make(
            0x404000,
            "N::SmallGap",
            size=300,
            state="FUNCTION",
            match=0.80,
            actionable_mismatch=True,
        )
        too_weak = make(
            0x405000,
            "N::Weak",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
            weak_dependencies=(0x410000, 0x410010, 0x410020),
        )
        blocked = make(
            0x406000,
            "N::Blocked",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
            deferred_reason="Needs layout evidence.",
        )
        unscored = make(
            0x407000,
            "N::Unscored",
            size=1000,
            state="FUNCTION",
            actionable_mismatch=True,
        )
        chosen = select_lane(
            [
                below_match,
                too_large,
                too_resolved,
                too_weak,
                blocked,
                unscored,
                good,
            ],
            "production",
            [campaign_record(good.address, "production-history", 40.0)],
            queue="refinement",
        )
        self.assertEqual([item.address for item in chosen], [good.address])
        self.assertTrue(good.production_eligible)
        self.assertGreaterEqual(good.unresolved_bytes, 100.0)

    def test_production_restores_prior_yield_order_and_keeps_cohort_forecast(self):
        higher_prior_yield = make(
            0x402000,
            "N::HigherPriorYield",
            size=1000,
            state="FUNCTION",
            source="N.cpp",
            match=0.60,
            actionable_mismatch=True,
        )
        higher_cohort_forecast = make(
            0x401000,
            "N::HigherCohortForecast",
            size=500,
            state="FUNCTION",
            source="N.cpp",
            match=0.60,
            actionable_mismatch=True,
        )

        def apply_forecasts(items, records, queue):
            del records, queue
            for item in items:
                item.lane = "production"
                item.production_eligible = True
                item.lane_eligible = True
                item.success_probability = 0.5
                item.median_minutes = 10.0
            higher_prior_yield.median_retained_bytes = 10.0
            higher_prior_yield.expected_retained_bytes = 10.0
            higher_prior_yield.expected_minutes = 10.0
            higher_prior_yield.expected_bytes_per_minute = 1.0
            higher_cohort_forecast.median_retained_bytes = 100.0
            higher_cohort_forecast.expected_retained_bytes = 100.0
            higher_cohort_forecast.expected_minutes = 10.0
            higher_cohort_forecast.expected_bytes_per_minute = 10.0

        with patch.object(candidates, "apply_lane_model", apply_forecasts):
            chosen = select_lane(
                [higher_cohort_forecast, higher_prior_yield],
                "production",
                queue="refinement",
            )

        self.assertEqual(
            [item.address for item in chosen],
            [higher_prior_yield.address, higher_cohort_forecast.address],
        )
        self.assertEqual(higher_prior_yield.expected_retained_bytes, 10.0)
        self.assertEqual(higher_cohort_forecast.expected_retained_bytes, 100.0)
        self.assertEqual(higher_prior_yield.expected_bytes_per_minute, 1.0)
        self.assertEqual(higher_cohort_forecast.expected_bytes_per_minute, 10.0)

    def test_production_accepts_a_current_score_without_a_saved_diff(self):
        target = make(
            0x401000, "N::Target", size=1000, state="FUNCTION", match=0.60
        )
        lane, reason = candidates._lane_rule(target)
        self.assertEqual(lane, "production")
        self.assertEqual(reason, "the target passes all production gates")

    def test_saved_diff_needs_a_classified_two_sided_window(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "0x00401000.txt").write_text(
                "---\n+++\n@@ -0x401000,3 +0x500000,3 @@\n"
                "0x401000 : -mov eax, dword ptr [ecx]\n"
                "         : +mov edx, dword ptr [ecx]\n"
                "N::Target is only 60.00% similar to the original, diff above\n",
                encoding="utf-8",
            )
            (root / "0x00402000.txt").write_text(
                "0x402000: N::Exact 100% match.\n",
                encoding="utf-8",
            )
            artifacts = candidates.read_actionable_mismatches(root)
        self.assertEqual(set(artifacts), {0x401000})
        self.assertEqual(artifacts[0x401000].windows, 1)
        self.assertIn("data-access", artifacts[0x401000].classifications)
        self.assertFalse(
            candidates.mismatch_artifact_is_current(artifacts[0x401000], 0.6)
        )
        self.assertFalse(
            candidates.mismatch_artifact_is_current(artifacts[0x401000], 0.7)
        )

    def test_research_lane_uses_evidence_value_without_a_byte_forecast(self):
        dependency = make(
            0x401000, "N::Dependency", size=1000, state="STUB"
        )
        dependency.immediate_unlocks = 2
        dependency.declared_dependencies = (0x403000,)
        ordinary = make(0x402000, "N::Ordinary", size=1000, state="STUB")
        chosen = select_lane([ordinary, dependency], "research")
        self.assertEqual(chosen[0].address, dependency.address)
        self.assertIsNone(dependency.expected_retained_bytes)
        self.assertIsNone(dependency.lower_retained_bytes)
        self.assertIsNone(dependency.median_minutes)
        self.assertGreater(dependency.research_value, ordinary.research_value)

    def test_lane_queue_filters_do_not_mix_coverage_and_refinement(self):
        refinement = make(
            0x401000,
            "N::Refinement",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        coverage = make(0x402000, "N::Coverage", size=1000, state="STUB")
        coverage.deferred_reason = "Needs caller evidence."
        production = select_lane(
            [coverage, refinement],
            "production",
            [campaign_record(refinement.address, "production-history", 40.0)],
            queue="refinement",
        )
        research = select_lane(
            [coverage, refinement], "research", queue="coverage"
        )
        self.assertEqual([item.address for item in production], [refinement.address])
        self.assertEqual([item.address for item in research], [coverage.address])

    def test_research_lane_rejects_a_generic_unblocked_stub(self):
        target = make(0x401000, "N::Generic", size=1000, state="STUB")
        self.assertEqual(select_lane([target], "research", queue="coverage"), [])
        self.assertEqual(
            target.lane_reason, "no specific evidence blocker is recorded"
        )

    def test_current_base_lane_reuses_the_exact_selector_decision(self):
        cases = {
            "production": make(
                0x401000,
                "N::Production",
                size=1000,
                state="FUNCTION",
                match=0.60,
            ),
            "inactive": make(
                0x401000,
                "N::Inactive",
                size=1000,
                state="FUNCTION",
                match=0.40,
            ),
        }
        for expected, target in cases.items():
            with (
                self.subTest(expected=expected),
                patch.object(candidates, "parse_map", return_value=[]),
                patch.object(
                    candidates,
                    "load_or_build_candidates",
                    return_value=([target], True),
                ),
            ):
                self.assertEqual(
                    candidates.current_base_lane(
                        target.address,
                        root=candidates.ROOT,
                    ),
                    expected,
                )

        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "canonical repository root"):
                candidates.current_base_lane(0x401000, root=Path(directory))

    def test_under_yield_examples_receive_conservative_penalties(self):
        first = candidates.CampaignAttempt(
            address=0x401000,
            campaign_id="first",
            mode="refinement",
            result="source",
            timestamp="2026-09-01T10:00:00+00:00",
            minutes=10.0,
            retained_bytes=16.23,
            expected_retained_bytes=718.9,
        )
        second = candidates.CampaignAttempt(
            address=0x401000,
            campaign_id="second",
            mode="refinement",
            result="source",
            timestamp="2026-09-01T11:00:00+00:00",
            minutes=10.0,
            retained_bytes=1.3904786132043228,
            expected_retained_bytes=453.2838438438439,
        )
        self.assertTrue(first.under_yield)
        self.assertTrue(second.under_yield)
        self.assertAlmostEqual(
            candidates._history_penalty([first]),
            (16.23 / 718.9) / candidates.UNDER_YIELD_RATIO,
        )
        self.assertEqual(
            candidates._history_penalty([second]),
            candidates.UNDER_YIELD_PENALTY_FLOOR,
        )
        self.assertEqual(
            candidates._history_penalty([first, second]),
            candidates.HISTORY_PENALTY_FLOOR,
        )

    def test_generic_evidence_does_not_make_a_failed_target_retryable(self):
        address = 0x401000
        target = make(
            address,
            "N::Target",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        records = [
            campaign_record(
                address,
                "failed-campaign",
                0.0,
                models=["wrong loop model"],
            ),
            {
                "schema_version": 2,
                "record_type": "evidence",
                "evidence_id": "generic",
                "addresses": [f"0x{address:08X}"],
                "evidence_kind": "caller",
                "note": "A caller is now known.",
                "timestamp": "2026-09-01T11:00:00+00:00",
            },
        ]
        chosen = select_lane([target], "research", records, "refinement")
        self.assertEqual([item.address for item in chosen], [address])
        self.assertFalse(target.retry_eligible)
        self.assertFalse(target.fresh_evidence)
        self.assertEqual(target.lane_reason, "the latest failure has no valid retry evidence")

    def test_exact_linked_evidence_permits_only_one_retry(self):
        address = 0x401000
        first_failure = campaign_record(
            address,
            "failed-campaign",
            0.0,
            timestamp="2026-09-01T10:00:00+00:00",
            models=["wrong loop model"],
        )
        evidence = {
            "schema_version": 3,
            "record_type": "evidence",
            "evidence_id": "linked",
            "addresses": [f"0x{address:08X}"],
            "failed_campaign_id": "failed-campaign",
            "failed_model": "wrong loop model",
            "changed_assumption": "The loop stops on the prior element.",
            "changed_source": None,
            "timestamp": "2026-09-01T11:00:00+00:00",
        }
        target = make(
            address,
            "N::Target",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        chosen = select_lane(
            [target], "production", [first_failure, evidence], "refinement"
        )
        self.assertEqual([item.address for item in chosen], [address])
        self.assertTrue(target.retry_eligible)
        self.assertEqual(target.retry_evidence_id, "linked")

        retry_failure = campaign_record(
            address,
            "retry-campaign",
            0.0,
            timestamp="2026-09-01T12:00:00+00:00",
            models=["new loop model"],
        )
        after_retry = make(
            address,
            "N::Target",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        chosen = select_lane(
            [after_retry],
            "research",
            [first_failure, evidence, retry_failure],
            "refinement",
        )
        self.assertEqual([item.address for item in chosen], [address])
        self.assertFalse(after_retry.retry_eligible)
        self.assertEqual(after_retry.latest_campaign_id, "retry-campaign")

    def test_three_recent_failures_open_the_source_cohort_circuit(self):
        items = [
            make(
                0x401000 + index * 0x1000,
                f"N::Target{index}",
                size=1000,
                state="FUNCTION",
                match=0.60,
                actionable_mismatch=True,
            )
            for index in range(4)
        ]
        records = [
            campaign_record(
                item.address,
                f"failed-{index}",
                0.0,
                timestamp=f"2026-09-01T1{index}:00:00+00:00",
                models=[f"model {index}"],
            )
            for index, item in enumerate(items[:3])
        ]
        chosen = select_lane(items, "research", records, "refinement")
        fresh = next(item for item in chosen if item.address == items[3].address)
        self.assertTrue(fresh.circuit_breaker_open)
        self.assertEqual(fresh.lane_reason, "the source cohort circuit breaker is open")
        self.assertIsNone(fresh.expected_retained_bytes)

    def test_one_bundle_failure_counts_as_one_circuit_campaign(self):
        items = [
            make(
                0x409000 + index * 0x1000,
                f"N::Bundle{index}",
                size=1000,
                state="FUNCTION",
                match=0.60,
                actionable_mismatch=True,
            )
            for index in range(4)
        ]
        failed = campaign_record(
            items[0].address,
            "failed-bundle",
            0.0,
            expected_bytes=1200.0,
            minutes=30.0,
            models=["shared bundle model"],
        )
        failed["addresses"] = [
            f"0x{item.address:08X}" for item in items[:3]
        ]
        failed["subsystem"] = "N"
        failed["target_deltas"] = {
            f"0x{item.address:08X}": {
                "effective_bytes": 0.0,
                "initialized_bytes": 0.0,
            }
            for item in items[:3]
        }
        chosen = select_lane(items, "production", [failed], "refinement")
        fresh = items[-1]
        self.assertFalse(fresh.circuit_breaker_open)
        self.assertIn(fresh, chosen)

    def test_retired_pivot_target_is_not_a_calibration_success(self):
        old_address = 0x409000
        new_address = 0x40A000
        record = campaign_record(
            old_address,
            "successful-pivot",
            0.0,
            expected_bytes=100.0,
            minutes=60.0,
            timestamp="2026-09-01T13:00:00+00:00",
            result="source",
        )
        record["started_at"] = "2026-09-01T12:00:00+00:00"
        record["lane"] = "closure"
        record["addresses"] = [
            f"0x{old_address:08X}",
            f"0x{new_address:08X}",
        ]
        record["active_addresses"] = [f"0x{new_address:08X}"]
        record["retired_addresses"] = [f"0x{old_address:08X}"]
        record["target_events"] = [
            {
                "address": f"0x{new_address:08X}",
                "replaces": f"0x{old_address:08X}",
                "added_at": "2026-09-01T12:55:00+00:00",
            }
        ]
        record["target_deltas"] = {
            f"0x{old_address:08X}": {
                "effective_bytes": 0.0,
                "initialized_bytes": 0.0,
            },
            f"0x{new_address:08X}": {
                "effective_bytes": 80.0,
                "initialized_bytes": 0.0,
            },
        }
        attempts = candidates.campaign_attempts([record])
        by_address = {attempt.address: attempt for attempt in attempts}
        retired = by_address[old_address]
        active = by_address[new_address]
        self.assertEqual(retired.result, "no-source")
        self.assertFalse(retired.calibration_eligible)
        self.assertEqual(active.result, "source")
        self.assertTrue(active.calibration_eligible)
        self.assertAlmostEqual(retired.minutes, 55.0)
        self.assertAlmostEqual(active.minutes, 5.0)
        self.assertAlmostEqual(sum(item.minutes for item in attempts), 60.0)

    def test_three_severe_results_in_the_last_five_open_the_circuit(self):
        items = [
            make(
                0x411000 + index * 0x1000,
                f"N::Window{index}",
                size=1000,
                state="FUNCTION",
                match=0.60,
                actionable_mismatch=True,
            )
            for index in range(6)
        ]
        retained = (0.0, 100.0, 5.0, 100.0, 0.0)
        records = [
            campaign_record(
                item.address,
                f"window-{index}",
                value,
                expected_bytes=100.0,
                timestamp=f"2026-09-02T1{index}:00:00+00:00",
            )
            for index, (item, value) in enumerate(zip(items, retained))
        ]
        chosen = select_lane(items, "research", records, "refinement")
        fresh = next(item for item in chosen if item.address == items[-1].address)
        self.assertTrue(fresh.circuit_breaker_open)

    def test_sparse_cohort_uses_zero_lower_bound_and_measured_fields(self):
        observed = make(
            0x401000,
            "N::Observed",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        target = make(
            0x402000,
            "N::Target",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        record = campaign_record(observed.address, "observed", 40.0, minutes=8.0)
        chosen = select_lane(
            [observed, target], "production", [record], "refinement"
        )
        estimated = next(item for item in chosen if item.address == target.address)
        self.assertEqual(estimated.cohort_sample_size, 1)
        self.assertEqual(estimated.lower_retained_bytes, 0.0)
        self.assertGreater(estimated.median_retained_bytes, 0.0)
        self.assertEqual(
            estimated.expected_retained_bytes,
            estimated.median_retained_bytes,
        )
        self.assertEqual(estimated.median_minutes, 8.0)
        self.assertGreater(estimated.success_probability, 0.0)

    def test_schema_v3_lane_and_attempt_buckets_are_preserved(self):
        address = 0x401000
        first = campaign_record(address, "first", 20.0)
        first["lane"] = "closure"
        first["subsystem"] = "Renderer"
        second = campaign_record(
            address,
            "second",
            30.0,
            timestamp="2026-09-01T11:00:00+00:00",
        )
        second["lane"] = "closure"
        second["subsystem"] = "Renderer"
        parsed = candidates.campaign_attempts([second, first])
        self.assertEqual([item.campaign_id for item in parsed], ["first", "second"])
        self.assertEqual([item.lane for item in parsed], ["closure", "closure"])
        self.assertEqual([item.subsystem for item in parsed], ["Renderer", "Renderer"])
        self.assertEqual([item.attempt_bucket for item in parsed], ["0", "1"])

    def test_recorded_closure_history_does_not_seed_a_production_cohort(self):
        observed = make(
            0x401000,
            "N::Observed",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        target = make(
            0x402000,
            "N::Target",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        record = campaign_record(observed.address, "closure", 40.0)
        record["lane"] = "closure"
        chosen = select_lane(
            [observed, target], "production", [record], "refinement"
        )
        self.assertEqual(chosen, [])
        self.assertEqual(target.lane, "inactive")
        self.assertFalse(target.lane_eligible)
        self.assertIn("no completed campaign cohort", target.lane_reason)

    def test_specific_cohort_key_contains_all_recorded_dimensions(self):
        items = []
        records = []
        for index in range(8):
            address = 0x401000 + index * 0x1000
            items.append(
                make(
                    address,
                    f"N::Observed{index}",
                    size=1000,
                    state="FUNCTION",
                    match=0.60,
                    actionable_mismatch=True,
                )
            )
            record = campaign_record(
                address,
                f"observed-{index}",
                20.0 + index,
                expected_bytes=20.0,
            )
            record["lane"] = "production"
            record["subsystem"] = "N"
            record["prediction"] = {
                "features": {
                    "size_bucket": "medium",
                    "score_bucket": "50-75",
                    "attempt_bucket": "0",
                }
            }
            records.append(record)
        target = make(
            0x410000,
            "N::Target",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        chosen = select_lane(
            [*items, target], "production", records, "refinement"
        )
        estimated = next(item for item in chosen if item.address == target.address)
        self.assertEqual(
            estimated.cohort_key,
            "refinement/production/n/medium/50-75/0",
        )
        self.assertEqual(estimated.cohort_sample_size, 8)

    def test_walk_forward_fixture_has_80_percent_lower_bound_coverage(self):
        # These retained-byte and time values come from tracked refinement rows.
        outcomes = [
            (100.85, 8.33, 300.0),
            (10.84, 5.00, 120.0),
            (0.00, 5.67, 250.0),
            (46.70, 3.00, 180.0),
            (0.00, 7.00, 200.0),
            (16.23, 11.20, 718.9),
            (0.00, 9.50, 400.0),
            (1.3904786132043228, 9.78, 453.2838438438439),
            (38.78, 16.68, 300.0),
            (0.00, 12.00, 350.0),
        ]
        items = []
        records = []
        for index, (retained, minutes, expected) in enumerate(outcomes):
            address = 0x401000 + index * 0x1000
            items.append(
                make(
                    address,
                    f"N::Replay{index}",
                    size=1000,
                    state="FUNCTION",
                    match=0.60,
                    actionable_mismatch=True,
                )
            )
            records.append(
                campaign_record(
                    address,
                    f"replay-{index}",
                    retained,
                    expected_bytes=expected,
                    minutes=minutes,
                    timestamp=f"2026-09-{index + 1:02d}T10:00:00+00:00",
                )
            )
        points = candidates.replay_cohort_calibration(items, records, warmup=5)
        self.assertGreaterEqual(len(points), 5)
        coverage = sum(point.covered for point in points) / len(points)
        self.assertGreaterEqual(coverage, 0.80)
        self.assertTrue(all(point.sample_size >= 5 for point in points))
        self.assertTrue(all(item.lane == "" for item in items))

    def test_campaign_attempts_prefer_and_split_prediction_events(self):
        first_address = 0x401000
        second_address = 0x402000
        record = campaign_record(first_address, "event-forecast", 40.0)
        record["addresses"] = [
            f"0x{first_address:08X}",
            f"0x{second_address:08X}",
        ]
        record["minutes"] = 20.0
        record["expected_retained_bytes"] = 1000.0
        record["prediction"] = {
            "features": {
                "size_bucket": "oversized",
                "score_bucket": "above-90",
                "attempt_bucket": "2+",
            }
        }
        record["target_deltas"] = {
            f"0x{first_address:08X}": {"effective_bytes": 20.0},
            f"0x{second_address:08X}": {"effective_bytes": 20.0},
        }
        record["prediction_events"] = [
            {
                "addresses": [
                    f"0x{first_address:08X}",
                    f"0x{second_address:08X}",
                ],
                "selected_at": "2026-09-01T09:00:00+00:00",
                "prediction": {
                    "version": "cohort-v1",
                    "expected_minutes": 12.0,
                    "expected_retained_bytes": 80.0,
                    "lower_bound_retained_bytes": 10.0,
                    "features": {
                        "size_bucket": "medium",
                        "score_bucket": "50-75",
                        "attempt_bucket": "0",
                    },
                },
            }
        ]

        attempts = candidates.campaign_attempts([record])

        self.assertEqual(len(attempts), 2)
        self.assertTrue(
            all(attempt.expected_retained_bytes == 40.0 for attempt in attempts)
        )
        self.assertTrue(all(attempt.size_bucket == "medium" for attempt in attempts))
        self.assertTrue(all(attempt.score_bucket == "50-75" for attempt in attempts))
        self.assertTrue(all(attempt.attempt_bucket == "0" for attempt in attempts))

    def test_campaign_attempts_use_unequal_per_target_forecasts(self):
        first_address = 0x401000
        second_address = 0x402000
        record = campaign_record(first_address, "unequal-forecast", 100.0)
        record["addresses"] = [
            f"0x{first_address:08X}",
            f"0x{second_address:08X}",
        ]
        record["active_addresses"] = list(record["addresses"])
        record["target_deltas"] = {
            f"0x{first_address:08X}": {"effective_bytes": 9.0},
            f"0x{second_address:08X}": {"effective_bytes": 9.0},
        }
        record["prediction_events"] = [
            {
                "addresses": list(record["addresses"]),
                "prediction": {
                    "expected_retained_bytes": 100.0,
                    "features": {
                        "per_target": {
                            f"0x{first_address:08X}": {
                                "median_retained_bytes": 90.0,
                                "lower_retained_bytes": 20.0,
                            },
                            f"0x{second_address:08X}": {
                                "median_retained_bytes": 10.0,
                                "lower_retained_bytes": 2.0,
                            },
                        }
                    },
                },
            }
        ]

        attempts = candidates.campaign_attempts([record])

        self.assertEqual(
            [attempt.expected_retained_bytes for attempt in attempts],
            [90.0, 10.0],
        )
        self.assertEqual(
            [attempt.retained_bytes for attempt in attempts], [9.0, 9.0]
        )

    def test_lane_json_and_table_expose_calibration_fields(self):
        item = make(
            0x401000,
            "N::Work",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        argv = [
            "decomp_candidates.py",
            "--lane",
            "production",
            "--limit",
            "0",
            "--json",
        ]
        with (
            patch.object(sys, "argv", argv),
            patch.object(
                candidates, "parse_map", return_value=[(item.address, item.name)]
            ),
            patch.object(
                candidates,
                "load_or_build_candidates",
                return_value=([item], False),
            ),
            patch.object(
                candidates,
                "read_records",
                return_value=[campaign_record(item.address, "history", 40.0)],
            ),
            redirect_stdout(StringIO()) as output,
        ):
            self.assertEqual(candidates.main(), 0)
        row = json.loads(output.getvalue())[0]
        self.assertEqual(row["lane"], "production")
        self.assertTrue(row["lane_eligible"])
        self.assertIn("success_probability", row)
        self.assertIn("sample_size", row)
        self.assertIn("lower_retained_bytes", row)
        self.assertIn("median_retained_bytes", row)
        self.assertIn("median_minutes", row)

        table = StringIO()
        with redirect_stdout(table):
            candidates.print_table([item], 0, False, lane="production")
        self.assertIn("P SRC", table.getvalue())
        self.assertIn("LOW B", table.getvalue())
        self.assertIn("MED MIN", table.getvalue())

    def test_prediction_export_writes_one_campaign_ready_json_object(self):
        item = make(
            0x401000,
            "N::Work",
            size=1000,
            state="FUNCTION",
            match=0.60,
            actionable_mismatch=True,
        )
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "handoff" / "prediction.json"
            argv = [
                "decomp_candidates.py",
                "--lane",
                "production",
                "--limit",
                "1",
                "--json",
                "--prediction-features-out",
                str(output_path),
            ]
            with (
                patch.object(sys, "argv", argv),
                patch.object(
                    candidates, "parse_map", return_value=[(item.address, item.name)]
                ),
                patch.object(
                    candidates,
                    "load_or_build_candidates",
                    return_value=([item], False),
                ),
                patch.object(
                    candidates,
                    "read_records",
                    return_value=[campaign_record(item.address, "history", 40.0)],
                ),
                redirect_stdout(StringIO()) as output,
            ):
                self.assertEqual(candidates.main(), 0)
            payload = json.loads(output_path.read_text(encoding="utf-8"))
            temporary_files = list(output_path.parent.glob(".*.tmp"))

        self.assertEqual(len(json.loads(output.getvalue())), 1)
        self.assertEqual(payload["schema_version"], 2)
        self.assertIn("generated_at", payload)
        self.assertIn("selection_fingerprint", payload)
        self.assertIn("selection_fingerprint_sha256", payload)
        self.assertEqual(payload["prediction_version"], "cohort-v1")
        self.assertEqual(payload["lane"], "production")
        self.assertEqual(payload["address"], "0x00401000")
        self.assertEqual(
            payload["expected_retained_bytes"], payload["median_retained_bytes"]
        )
        self.assertEqual(payload["expected_minutes"], payload["median_minutes"])
        self.assertEqual(
            payload["prediction_lower_bound_bytes"],
            payload["lower_retained_bytes"],
        )
        self.assertIn("success_probability", payload)
        self.assertIn("cohort_sample_size", payload)
        self.assertEqual(temporary_files, [])

    def test_exact_production_export_does_not_need_a_dependency_frontier(self):
        item = make(
            0x401000,
            "N::Work",
            size=1000,
            state="FUNCTION",
            match=0.60,
        )
        records = [campaign_record(item.address, "history", 40.0)]
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "prediction.json"
            argv = [
                "decomp_candidates.py",
                "--lane",
                "production",
                "--for",
                "0x00401000",
                "--prediction-features-out",
                str(output_path),
            ]
            with (
                patch.object(sys, "argv", argv),
                patch.object(
                    candidates, "parse_map", return_value=[(item.address, item.name)]
                ),
                patch.object(
                    candidates,
                    "load_or_build_candidates",
                    return_value=([item], False),
                ),
                patch.object(candidates, "read_records", return_value=records),
                patch.object(
                    candidates, "dependency_frontier_for", return_value=set()
                ),
                redirect_stdout(StringIO()),
            ):
                self.assertEqual(candidates.main(), 0)
            self.assertTrue(output_path.is_file())
            with (
                patch.object(
                    candidates, "parse_map", return_value=[(item.address, item.name)]
                ),
                patch.object(
                    candidates,
                    "load_or_build_candidates",
                    return_value=([item], False),
                ),
                patch.object(candidates, "read_records", return_value=records),
                patch.object(
                    candidates, "dependency_frontier_for", return_value=set()
                ) as frontier,
            ):
                current = candidates.current_prediction_feature_handoff(
                    item.address, "production"
                )
            self.assertEqual(current["address"], "0x00401000")
            frontier.assert_not_called()

    def test_selection_fingerprint_ignores_post_selection_diff_receipts(self):
        cache_fingerprint = {
            "head": "a" * 40,
            "report_sha256": "b" * 64,
            "report_semantic_sha256": "f" * 64,
            "report_provenance_sha256": "c" * 64,
            "mismatch_sha256": "d" * 64,
            "ledger_sha256": "e" * 64,
        }
        with patch.object(
            candidates,
            "candidate_cache_fingerprint",
            return_value=cache_fingerprint,
        ):
            selection = candidates.candidate_selection_fingerprint(
                dependency_mode=True
            )
        self.assertNotIn("mismatch_sha256", selection)
        self.assertNotIn("report_sha256", selection)
        self.assertNotIn("report_provenance_sha256", selection)
        self.assertEqual(selection["report_semantic_sha256"], "f" * 64)
        self.assertEqual(selection["ledger_sha256"], "e" * 64)

    def test_semantic_report_digest_ignores_report_metadata_and_formatting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.json"
            second = root / "second.json"
            first.write_text(
                json.dumps(
                    {
                        "created_at": "first",
                        "data": [
                            {"address": "0x401000", "matching": 0.6}
                        ],
                    },
                    separators=(",", ":"),
                ),
                encoding="utf-8",
            )
            second.write_text(
                json.dumps(
                    {
                        "created_at": "second",
                        "data": [
                            {"matching": 0.6, "address": "0x00401000"}
                        ],
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )
            self.assertEqual(
                candidates._semantic_report_digest(first),
                candidates._semantic_report_digest(second),
            )
            second.write_text(
                '{"data":[{"address":"0x401000","matching":0.7}]}',
                encoding="utf-8",
            )
            self.assertNotEqual(
                candidates._semantic_report_digest(first),
                candidates._semantic_report_digest(second),
            )

    def test_prediction_export_rejects_more_than_one_visible_candidate(self):
        items = [
            make(
                0x401000 + index * 0x1000,
                f"N::Work{index}",
                size=1000,
                state="FUNCTION",
                match=0.60,
                actionable_mismatch=True,
            )
            for index in range(2)
        ]
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "prediction.json"
            argv = [
                "decomp_candidates.py",
                "--lane",
                "production",
                "--prediction-features-out",
                str(output_path),
            ]
            with (
                patch.object(sys, "argv", argv),
                patch.object(
                    candidates,
                    "parse_map",
                    return_value=[(item.address, item.name) for item in items],
                ),
                patch.object(
                    candidates,
                    "load_or_build_candidates",
                    return_value=(items, False),
                ),
                patch.object(candidates, "read_records", return_value=[]),
                redirect_stderr(StringIO()) as error,
            ):
                with self.assertRaises(SystemExit) as raised:
                    candidates.main()

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("exactly one selected candidate", error.getvalue())
        self.assertFalse(output_path.exists())

    def test_candidate_cache_is_deterministic_and_returns_an_immutable_copy(self):
        fingerprint = {
            "cache_version": candidates.CANDIDATE_CACHE_VERSION,
            "dependency_mode": False,
            "head": "fixed",
        }
        item = make(0x401000, "N::Work", size=100, state="STUB")
        with tempfile.TemporaryDirectory() as directory:
            cache_root = Path(directory)
            with (
                patch.object(
                    candidates,
                    "candidate_cache_fingerprint",
                    return_value=fingerprint,
                ),
                patch.object(
                    candidates, "build_candidates", return_value=[item]
                ) as builder,
            ):
                first, first_hit = candidates.load_or_build_candidates(
                    [(item.address, item.name)],
                    dependency_mode=False,
                    cache_root=cache_root,
                )
                cache_files = list(cache_root.glob("*.json"))
                first_content = cache_files[0].read_bytes()
                first[0].lane = "research"
                second, second_hit = candidates.load_or_build_candidates(
                    [(item.address, item.name)],
                    dependency_mode=False,
                    cache_root=cache_root,
                )
                second_content = cache_files[0].read_bytes()
        self.assertFalse(first_hit)
        self.assertTrue(second_hit)
        self.assertEqual(builder.call_count, 1)
        self.assertEqual(len(cache_files), 1)
        self.assertEqual(first_content, second_content)
        self.assertEqual(second[0].lane, "")

    def test_invalid_candidate_cache_fails_closed_and_rebuilds(self):
        fingerprint = {
            "cache_version": candidates.CANDIDATE_CACHE_VERSION,
            "dependency_mode": False,
            "head": "fixed",
        }
        first_item = make(0x401000, "N::First", size=100, state="STUB")
        second_item = make(0x402000, "N::Second", size=100, state="STUB")
        with tempfile.TemporaryDirectory() as directory:
            cache_root = Path(directory)
            with patch.object(
                candidates,
                "candidate_cache_fingerprint",
                return_value=fingerprint,
            ):
                with patch.object(
                    candidates, "build_candidates", return_value=[first_item]
                ):
                    candidates.load_or_build_candidates(
                        [(first_item.address, first_item.name)],
                        dependency_mode=False,
                        cache_root=cache_root,
                    )
                cache_path = next(cache_root.glob("*.json"))
                payload = json.loads(cache_path.read_text(encoding="utf-8"))
                payload["candidates"][0]["address"] = "not-an-address"
                cache_path.write_text(json.dumps(payload), encoding="utf-8")
                with patch.object(
                    candidates, "build_candidates", return_value=[second_item]
                ) as builder:
                    rebuilt, cache_hit = candidates.load_or_build_candidates(
                        [(second_item.address, second_item.name)],
                        dependency_mode=False,
                        cache_root=cache_root,
                    )
        self.assertFalse(cache_hit)
        self.assertEqual(builder.call_count, 1)
        self.assertEqual([item.address for item in rebuilt], [second_item.address])


if __name__ == "__main__":
    unittest.main()
