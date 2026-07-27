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

    def test_lint_errors_make_a_matched_function_rank_as_work(self):
        # A 100% match that still states byte offsets is unfinished.
        clean = make(0x401000, "N::Clean", size=300, state="FUNCTION", match=1.0)
        debt = make(0x402000, "N::Debt", size=300, state="FUNCTION", match=1.0, lint_errors=6)
        candidates.score(clean)
        candidates.score(debt)
        self.assertGreater(debt.rank, clean.rank)

    def test_a_capped_function_sinks_below_every_uncapped_one(self):
        capped = make(0x401000, "N::A", size=64, state="STUB", cap="CAP-14")
        plain = make(0x402000, "N::B", size=5000, state="FUNCTION", match=0.5)
        candidates.score(capped)
        candidates.score(plain)
        self.assertLess(capped.rank, plain.rank)

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


class SelectTests(unittest.TestCase):
    def setUp(self):
        self.pool = [
            make(0x401000, "N::Stub", size=64, state="STUB"),
            make(0x402000, "N::Fresh", size=64, state="NOT_STARTED"),
            make(0x403000, "N::Big", size=4000, state="NOT_STARTED"),
            make(0x404000, "N::Done", size=64, state="FUNCTION", match=1.0),
            make(0x405000, "N::Near", size=64, state="FUNCTION", match=0.7),
            make(0x406000, "N::Capped", size=64, state="STUB", cap="CAP-01"),
            make(0x407000, "Other::Stub", size=64, state="STUB"),
            make(
                0x408000, "N::Debt", size=300, state="FUNCTION", match=1.0, lint_errors=6
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
        }
        arguments.update(kwargs)
        return [item.name for item in candidates.select(list(self.pool), **arguments)]

    def test_a_fully_matched_function_is_not_a_candidate(self):
        self.assertNotIn("N::Done", self.choose())

    def test_a_fully_matched_function_with_lint_errors_stays_a_candidate(self):
        # Otherwise the worst debt is invisible: it already matches at 100%.
        self.assertIn("N::Debt", self.choose())

    def test_debt_only_keeps_just_the_lint_failures(self):
        self.assertEqual(self.choose(debt_only=True), ["N::Debt"])

    def test_a_capped_function_is_hidden_by_default(self):
        self.assertNotIn("N::Capped", self.choose())
        self.assertIn("N::Capped", self.choose(exclude_capped=False))

    def test_the_namespace_filter_restricts_the_list(self):
        self.assertEqual(
            sorted(self.choose(namespace="Other")),
            ["Other::Stub"],
        )

    def test_stubs_only_keeps_only_stubs(self):
        self.assertEqual(sorted(self.choose(stubs_only=True)), ["N::Stub", "Other::Stub"])

    def test_leaves_only_keeps_small_unannotated_functions(self):
        self.assertEqual(self.choose(leaves_only=True), ["N::Fresh"])

    def test_near_only_keeps_implemented_functions_below_a_match(self):
        self.assertEqual(self.choose(near_only=True), ["N::Near"])

    def test_max_size_drops_a_larger_body(self):
        self.assertNotIn("N::Big", self.choose(max_size=100))

    def test_the_best_candidate_sorts_first(self):
        self.assertEqual(self.choose()[0], "N::Stub")


if __name__ == "__main__":
    unittest.main()
