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
# `dataclasses` resolves annotations through `sys.modules`, so the module must
# be registered before it executes.
sys.modules[spec.name] = candidates
spec.loader.exec_module(candidates)


def make(address: int, name: str, **kwargs) -> "candidates.Candidate":
    return candidates.Candidate(address=address, name=name, **kwargs)


class ParseMapTests(unittest.TestCase):
    def test_parse_map_sorts_and_skips_comments(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "map.txt"
            path.write_text(
                "0x00402000 Second::Function\n"
                "# a comment\n"
                "\n"
                "0x00401000 First::Function\n",
                encoding="utf-8",
            )
            self.assertEqual(
                candidates.parse_map(path),
                [(0x401000, "First::Function"), (0x402000, "Second::Function")],
            )

    def test_parse_map_keeps_an_unnamed_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "map.txt"
            path.write_text("0x00401000\n", encoding="utf-8")
            self.assertEqual(candidates.parse_map(path), [(0x401000, "")])


class ReadMatchPercentagesTests(unittest.TestCase):
    def test_absent_report_yields_no_percentages(self):
        self.assertEqual(
            candidates.read_match_percentages(Path("/nonexistent/report.json")), {}
        )

    def test_report_percentages_are_keyed_by_integer_address(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text(
                json.dumps(
                    {"data": [{"address": "0x401000", "matching": 0.5}, {"address": "bad"}]}
                ),
                encoding="utf-8",
            )
            self.assertEqual(candidates.read_match_percentages(path), {0x401000: 0.5})


class ReadCapsTests(unittest.TestCase):
    def test_caps_registry_skips_comments_and_reads_the_id(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "caps.tsv"
            path.write_text(
                "# address\tcap-id\n0x004A3980\tCAP-14\t30.18\t2026-07-26\tnote\n",
                encoding="utf-8",
            )
            self.assertEqual(candidates.read_caps(path), {0x4A3980: "CAP-14"})

    def test_absent_registry_yields_no_caps(self):
        self.assertEqual(candidates.read_caps(Path("/nonexistent/caps.tsv")), {})

    def test_audit_ledger_reads_state_and_freeze_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "audit.tsv"
            path.write_text(
                "# address\tstatus\n"
                "0x00401000\tprovisional\tpartial\t40\tclean\taudit\tuncertain\ttrigger\t"
                "-\t-\t-\t-\tpending\tsub-50\n",
                encoding="utf-8",
            )
            self.assertEqual(
                candidates.read_audit_ledger(path),
                {0x401000: ("pending", "sub-50")},
            )

    def test_deferral_round_trip_preserves_other_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "deferrals.tsv"
            candidates.write_deferral(0x402000, "unknown dispatch table", path)
            candidates.write_deferral(
                0x401000,
                "unknown structure layout",
                (0x403000, 0x404000),
                path,
            )
            self.assertEqual(
                candidates.read_deferrals(path),
                {
                    0x401000: candidates.Deferral(
                        blocked_by=(0x403000, 0x404000),
                        reason="unknown structure layout",
                    ),
                    0x402000: candidates.Deferral(reason="unknown dispatch table"),
                },
            )

    def test_legacy_deferral_is_a_manual_blocker(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "deferrals.tsv"
            path.write_text(
                "# address\treason\n0x00401000\tunknown structure layout\n",
                encoding="utf-8",
            )
            self.assertEqual(
                candidates.read_deferrals(path)[0x401000],
                candidates.Deferral(reason="unknown structure layout"),
            )


class ScoreTests(unittest.TestCase):
    def test_a_stub_outranks_an_unannotated_function_of_the_same_size(self):
        stub = make(0x401000, "N::A", size=100, state="STUB")
        fresh = make(0x402000, "N::B", size=100, state="NOT_STARTED")
        candidates.score(stub)
        candidates.score(fresh)
        self.assertGreater(stub.rank, fresh.rank)

    def test_a_leaf_outranks_a_large_body(self):
        leaf = make(0x401000, "N::A", size=64, state="NOT_STARTED")
        large = make(0x402000, "N::B", size=5000, state="NOT_STARTED")
        candidates.score(leaf)
        candidates.score(large)
        self.assertGreater(leaf.rank, large.rank)

    def test_a_medium_unannotated_function_outranks_an_oversized_stub(self):
        medium = make(0x401000, "N::A", size=800, state="NOT_STARTED", siblings=4)
        oversized = make(0x402000, "N::B", size=5000, state="STUB", siblings=4)
        candidates.score(medium)
        candidates.score(oversized)
        self.assertGreater(medium.rank, oversized.rank)

    def test_lint_errors_make_a_matched_function_rank_as_work(self):
        # A 100% match that still states byte offsets is unfinished.
        clean = make(0x401000, "N::Clean", size=300, state="FUNCTION", match=1.0)
        debt = make(0x402000, "N::Debt", size=300, state="FUNCTION", match=1.0, lint_errors=6)
        candidates.score(clean)
        candidates.score(debt)
        self.assertGreater(debt.rank, clean.rank)

    def test_a_legacy_cap_is_prioritized_for_audit(self):
        capped = make(0x401000, "N::A", size=64, state="STUB", cap="CAP-14")
        plain = make(0x402000, "N::B", size=5000, state="FUNCTION", match=0.5)
        candidates.score(capped)
        candidates.score(plain)
        self.assertGreater(capped.rank, plain.rank)

    def test_an_already_matching_stub_sinks(self):
        empty = make(0x401000, "N::A", size=16, state="STUB", match=1.0)
        real = make(0x402000, "N::B", size=16, state="STUB", match=0.0)
        candidates.score(empty)
        candidates.score(real)
        self.assertLess(empty.rank, real.rank)

    def test_reconstructed_siblings_raise_the_rank(self):
        alone = make(0x401000, "N::A", size=64, state="STUB", siblings=0)
        supported = make(0x402000, "N::B", size=64, state="STUB", siblings=5)
        candidates.score(alone)
        candidates.score(supported)
        self.assertGreater(supported.rank, alone.rank)

    def test_weak_nearby_siblings_lower_the_rank(self):
        supported = make(0x401000, "N::A", size=300, state="STUB", siblings=5)
        weak_model = make(
            0x402000,
            "N::B",
            size=300,
            state="STUB",
            siblings=5,
            nearby_provisional_scores=(0.70, 0.62, 0.51),
        )
        candidates.score(supported)
        candidates.score(weak_model)
        self.assertGreater(supported.rank, weak_model.rank)
        self.assertTrue(any("nearby provisional" in reason for reason in weak_model.reasons))


class SelectTests(unittest.TestCase):
    def setUp(self):
        self.pool = [
            make(0x401000, "N::Stub", size=64, state="STUB"),
            make(0x402000, "N::Fresh", size=64, state="NOT_STARTED"),
            make(0x403000, "N::Big", size=4000, state="NOT_STARTED"),
            make(0x404000, "N::Done", size=64, state="FUNCTION", match=1.0),
            make(0x405000, "N::Near", size=64, state="FUNCTION", match=0.7),
            make(
                0x405100,
                "N::Effective",
                size=64,
                state="FUNCTION",
                match=0.91,
                effective=True,
            ),
            make(
                0x405200,
                "N::Tool",
                size=64,
                state="FUNCTION",
                match=0.95,
                tool_artifact="TOOL-SYMBOL",
            ),
            make(0x406000, "N::Capped", size=64, state="STUB", cap="CAP-01"),
            make(0x407000, "Other::Stub", size=64, state="STUB"),
            make(
                0x408000, "N::Debt", size=300, state="FUNCTION", match=1.0, lint_errors=6
            ),
            make(
                0x409000,
                "N::Deferred",
                size=64,
                state="NOT_STARTED",
                deferred_reason="unknown dispatch table",
            ),
        ]

    def choose(self, **kwargs):
        arguments = {
            "namespace": None,
            "stubs_only": False,
            "leaves_only": False,
            "near_only": False,
            "max_size": None,
            "exclude_capped": True,
            "debt_only": False,
            "new_work_only": False,
            "include_deferred": False,
        }
        arguments.update(kwargs)
        return [item.name for item in candidates.select(list(self.pool), **arguments)]

    def test_a_fully_matched_function_is_not_a_candidate(self):
        self.assertNotIn("N::Done", self.choose())

    def test_effective_and_tool_functions_are_not_new_work(self):
        self.assertNotIn("N::Effective", self.choose())
        self.assertNotIn("N::Tool", self.choose())

    def test_a_fully_matched_function_with_lint_errors_stays_a_candidate(self):
        # Otherwise the worst debt is invisible: it already matches at 100%.
        self.assertIn("N::Debt", self.choose())

    def test_debt_only_keeps_just_the_lint_failures(self):
        self.assertEqual(self.choose(debt_only=True), ["N::Debt"])

    def test_new_work_excludes_implemented_functions_and_keeps_large_work(self):
        chosen = self.choose(new_work_only=True)
        self.assertNotIn("N::Near", chosen)
        self.assertNotIn("N::Debt", chosen)
        self.assertIn("N::Big", chosen)
        self.assertIn("N::Fresh", chosen)

    def test_deferrals_are_hidden_unless_requested(self):
        self.assertNotIn("N::Deferred", self.choose())
        self.assertIn("N::Deferred", self.choose(include_deferred=True))

    def test_a_legacy_cap_is_visible_by_default(self):
        self.assertIn("N::Capped", self.choose())
        self.assertIn("N::Capped", self.choose(exclude_capped=False))

    def test_the_namespace_filter_restricts_the_list(self):
        self.assertEqual(
            sorted(self.choose(namespace="Other")),
            ["Other::Stub"],
        )

    def test_stubs_only_keeps_only_stubs(self):
        self.assertEqual(
            sorted(self.choose(stubs_only=True)),
            ["N::Capped", "N::Stub", "Other::Stub"],
        )

    def test_leaves_only_keeps_small_unannotated_functions(self):
        self.assertEqual(self.choose(leaves_only=True), ["N::Fresh"])

    def test_near_only_keeps_implemented_functions_below_a_match(self):
        self.assertEqual(self.choose(near_only=True), ["N::Near"])

    def test_max_size_drops_a_larger_body(self):
        self.assertNotIn("N::Big", self.choose(max_size=100))

    def test_the_best_candidate_sorts_first(self):
        self.assertEqual(self.choose()[0], "N::Capped")


class DependencySelectionTests(unittest.TestCase):
    def choose(self, pool, include_deferred=False):
        return candidates.select(
            pool,
            namespace=None,
            stubs_only=False,
            leaves_only=False,
            near_only=False,
            max_size=None,
            exclude_capped=True,
            new_work_only=True,
            include_deferred=include_deferred,
        )

    @staticmethod
    def graph(edges=None, indirect_calls=None, indirect_jumps=None):
        edges = edges or {}
        callers = {}
        for source, targets in edges.items():
            for target in targets:
                callers.setdefault(target, set()).add(source)
        return candidates.DependencyGraph(
            callees={source: frozenset(targets) for source, targets in edges.items()},
            callers={target: frozenset(sources) for target, sources in callers.items()},
            indirect_calls=indirect_calls or {},
            indirect_jumps=indirect_jumps or {},
        )

    def test_large_dependency_ready_target_is_new_work(self):
        target = make(0x401000, "N::Large", size=5000, state="STUB")
        candidates.add_dependency_evidence([target], self.graph())
        self.assertEqual([item.name for item in self.choose([target])], ["N::Large"])

    def test_frontier_recurses_to_the_unresolved_leaf(self):
        goal = make(0x401000, "N::Goal", size=3000, state="STUB")
        dependency = make(0x402000, "N::Dependency", size=800, state="NOT_STARTED")
        leaf = make(0x403000, "N::Leaf", size=100, state="NOT_STARTED")
        pool = [goal, dependency, leaf]
        candidates.add_dependency_evidence(
            pool,
            self.graph({goal.address: {dependency.address}, dependency.address: {leaf.address}}),
        )
        self.assertEqual(candidates.dependency_frontier_for(goal.address, pool), {leaf.address})
        self.assertEqual([item.name for item in self.choose(pool)], ["N::Leaf"])

    def test_unlock_impact_outranks_an_unrelated_leaf(self):
        goal_a = make(0x401000, "N::GoalA", size=3000, state="STUB")
        goal_b = make(0x402000, "N::GoalB", size=3000, state="STUB")
        shared = make(0x403000, "N::Shared", size=1200, state="NOT_STARTED")
        unrelated = make(0x404000, "N::Unrelated", size=80, state="NOT_STARTED")
        pool = [goal_a, goal_b, shared, unrelated]
        candidates.add_dependency_evidence(
            pool,
            self.graph(
                {
                    goal_a.address: {shared.address},
                    goal_b.address: {shared.address},
                }
            ),
        )
        self.assertEqual(self.choose(pool)[0].name, "N::Shared")
        self.assertEqual(shared.immediate_unlocks, 2)
        self.assertEqual(shared.large_goal_reach, 3)

    def test_recursive_group_does_not_block_its_members(self):
        first = make(0x401000, "N::First", size=200, state="NOT_STARTED")
        second = make(0x402000, "N::Second", size=200, state="NOT_STARTED")
        pool = [first, second]
        candidates.add_dependency_evidence(
            pool,
            self.graph({first.address: {second.address}, second.address: {first.address}}),
        )
        self.assertTrue(first.dependency_ready)
        self.assertTrue(second.dependency_ready)
        self.assertEqual(
            candidates.dependency_frontier_for(first.address, pool),
            {first.address, second.address},
        )

    def test_declared_prerequisite_reactivates_automatically(self):
        target = make(
            0x401000,
            "N::Target",
            size=3000,
            state="STUB",
            declared_dependencies=(0x402000,),
            deferred_reason="needs the producer layout",
        )
        dependency = make(0x402000, "N::Producer", size=100, state="NOT_STARTED")
        pool = [target, dependency]
        candidates.add_dependency_evidence(pool, self.graph())
        self.assertFalse(target.dependency_ready)

        dependency.state = "FUNCTION"
        candidates.add_dependency_evidence(pool, self.graph())
        self.assertTrue(target.dependency_ready)

    def test_manual_blocker_stays_out_of_the_frontier(self):
        target = make(
            0x401000,
            "N::Target",
            size=3000,
            state="STUB",
            manual_blocker=True,
            deferred_reason="unknown indirect dispatch",
        )
        candidates.add_dependency_evidence([target], self.graph())
        self.assertEqual(self.choose([target]), [])
        self.assertEqual(self.choose([target], include_deferred=True), [target])

    def test_manual_blocked_caller_does_not_raise_dependency_impact(self):
        target = make(0x401000, "N::Target", size=100, state="NOT_STARTED")
        blocked_caller = make(
            0x402000,
            "N::BlockedCaller",
            size=3000,
            state="STUB",
            manual_blocker=True,
            deferred_reason="unknown dispatch table",
        )
        pool = [target, blocked_caller]
        candidates.add_dependency_evidence(
            pool, self.graph({blocked_caller.address: {target.address}})
        )
        self.assertEqual(target.immediate_unlocks, 0)
        self.assertEqual(target.large_goal_reach, 0)

    def test_low_score_function_is_weak_but_resolved(self):
        target = make(0x401000, "N::Target", size=3000, state="STUB")
        weak = make(0x402000, "N::Weak", size=100, state="FUNCTION", match=0.6)
        pool = [target, weak]
        candidates.add_dependency_evidence(
            pool, self.graph({target.address: {weak.address}})
        )
        self.assertTrue(target.dependency_ready)
        self.assertEqual(target.weak_dependencies, (weak.address,))


if __name__ == "__main__":
    unittest.main()
