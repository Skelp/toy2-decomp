from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

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
    return candidates.Candidate(address=address, name=name, **kwargs)


def graph(callees=None, callers=None):
    return candidates.DependencyGraph(
        callees={key: frozenset(value) for key, value in (callees or {}).items()},
        callers={key: frozenset(value) for key, value in (callers or {}).items()},
        indirect_calls={},
        indirect_jumps={},
    )


class CandidateTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
