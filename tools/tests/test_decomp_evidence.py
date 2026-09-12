from __future__ import annotations

import io
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

from tools import decomp_evidence
from tools.decomp_evidence import (
    UnmappedStartEvidence,
    resolve_padding_alignment,
    resolve_unmapped_start,
)


class UnmappedStartTests(unittest.TestCase):
    def test_accepts_an_exact_ghidra_start(self):
        evidence = resolve_unmapped_start(
            0x401000,
            (64, "FUN_00401000"),
            {0x401000: frozenset({0x500000})},
        )
        self.assertEqual(
            evidence,
            UnmappedStartEvidence(64, "FUN_00401000", (0x500000,)),
        )

    def test_accepts_an_annotated_global_relocation_target(self):
        evidence = resolve_unmapped_start(
            0x401000,
            None,
            {0x401000: frozenset({0x500008, 0x500004})},
        )
        self.assertEqual(
            evidence,
            UnmappedStartEvidence(None, "", (0x500004, 0x500008)),
        )

    def test_rejects_an_address_without_start_evidence(self):
        self.assertIsNone(resolve_unmapped_start(0x401000, None, {}))

    def test_resolves_nop_padding_to_the_next_mapped_start(self):
        self.assertEqual(
            resolve_padding_alignment(
                0x401008,
                [0x401000, 0x401010, 0x402000],
                lambda _address, size: b"\x90" * size,
            ),
            0x401010,
        )

    def test_rejects_nonpadding_or_distant_mapped_starts(self):
        self.assertIsNone(
            resolve_padding_alignment(
                0x401008,
                [0x401000, 0x401010],
                lambda _address, size: b"\x90" * (size - 1) + b"\x00",
            )
        )
        self.assertIsNone(
            resolve_padding_alignment(
                0x401000,
                [0x401000, 0x401020],
                lambda _address, size: b"\x90" * size,
            )
        )

    def test_cli_accepts_a_relocation_only_start(self):
        output = io.StringIO()
        with (
            patch.object(
                decomp_evidence.sys,
                "argv",
                ["decomp_evidence.py", "0x00402000", "--unmapped"],
            ),
            patch.object(
                decomp_evidence,
                "parse_map",
                return_value=[(0x401000, "N::A"), (0x403000, "N::D")],
            ),
            patch.object(
                decomp_evidence,
                "annotation_index",
                return_value=({}, {0x500000: ("Renderer.cpp", 10)}),
            ),
            patch.object(
                decomp_evidence,
                "global_symbol_names",
                return_value={0x500000: "g_dispatch"},
            ),
            patch.object(decomp_evidence, "read_match_percentages", return_value={}),
            patch.object(decomp_evidence, "read_original_sizes", return_value={}),
            patch.object(decomp_evidence, "ghidra_function_at", return_value=None),
            patch.object(
                decomp_evidence,
                "scan_annotated_relocation_targets",
                return_value={0x402000: frozenset({0x500004})},
            ),
            patch.object(decomp_evidence, "run_ghidra", return_value=None),
            patch.object(
                decomp_evidence.decomp_binary, "available", return_value=False
            ),
            redirect_stdout(output),
        ):
            result = decomp_evidence.main()

        self.assertEqual(result, 0)
        self.assertIn("annotated-global relocation target", output.getvalue())
        self.assertIn("g_dispatch (Renderer.cpp:10)", output.getvalue())


class PlacementTests(unittest.TestCase):
    ADDRESSES = [0x401000, 0x401100, 0x401200, 0x401300, 0x401400, 0x401500]

    def placement(self, address, owners):
        functions = {key: ("FUNCTION", source, 1) for key, source in owners.items()}
        return decomp_evidence.placement_line(address, self.ADDRESSES, functions)

    def test_agreeing_neighbours_name_their_file(self):
        line = self.placement(0x401200, {0x401000: "Toy2/Ini.cpp", 0x401500: "Toy2/Ini.cpp"})
        self.assertEqual(line, "placement: src/Toy2/Ini.cpp (both neighbours)")

    def test_the_contiguous_side_wins_when_the_neighbours_disagree(self):
        line = self.placement(0x401200, {0x401100: "Toy2/Ini.cpp", 0x401500: "Toy2/Toy2.cpp"})
        self.assertEqual(
            line, "placement: src/Toy2/Ini.cpp (contiguous; other side in src/Toy2/Toy2.cpp)"
        )
        line = self.placement(0x401200, {0x401000: "Toy2/Ini.cpp", 0x401300: "Toy2/Toy2.cpp"})
        self.assertTrue(line.startswith("placement: src/Toy2/Toy2.cpp (contiguous"), line)

    def test_a_gap_on_both_sides_or_no_gap_on_either_names_a_boundary(self):
        expected = (
            "placement: boundary between src/Toy2/Ini.cpp and src/Toy2/Toy2.cpp; a new file "
            "unless a run of one of them reaches this block "
            "(tools/decomp structure src/Toy2/Ini.cpp)"
        )
        gapped = self.placement(0x401200, {0x401000: "Toy2/Ini.cpp", 0x401500: "Toy2/Toy2.cpp"})
        adjacent = self.placement(0x401200, {0x401100: "Toy2/Ini.cpp", 0x401300: "Toy2/Toy2.cpp"})
        self.assertEqual(gapped, expected)
        self.assertEqual(adjacent, expected)

    def test_annotated_one_sided_unmapped_and_empty_cases(self):
        self.assertEqual(
            self.placement(0x401200, {0x401200: "Toy2/Ini.cpp", 0x401000: "Toy2/Toy2.cpp"}),
            "placement: src/Toy2/Ini.cpp (annotated)",
        )
        self.assertEqual(
            self.placement(0x401500, {0x401000: "Toy2/Ini.cpp"}),
            "placement: src/Toy2/Ini.cpp (only annotated neighbour)",
        )
        # An unmapped start between 0x401100 and 0x401200 touches both entries.
        self.assertEqual(
            self.placement(0x401180, {0x401100: "Toy2/Ini.cpp", 0x401300: "Toy2/Toy2.cpp"}),
            "placement: src/Toy2/Ini.cpp (contiguous; other side in src/Toy2/Toy2.cpp)",
        )
        self.assertTrue(self.placement(0x401200, {}).startswith("placement: no annotated"))
        # Outside the mapped range no neighbour means anything.
        for address in (0x400000, 0x900000):
            self.assertEqual(
                self.placement(address, {0x401000: "Toy2/Ini.cpp"}),
                "placement: outside the mapped range; choose the file from retail source paths",
            )


class StructureReportTests(unittest.TestCase):
    def test_runs_prefixes_large_files_and_the_core_count(self):
        from pathlib import Path
        import tempfile

        from tools.decomp_annotations import Annotation

        entries = [
            (0x401000, "Toy2::Ini::Parse"),
            (0x401100, "Toy2::ReadIniFile"),
            (0x401200, "Toy2::Level::Start"),
            (0x401300, "Unmapped::Gap"),
            (0x401400, "Toy2::Ini::Close"),
            (0x401500, "Renderer::Draw"),
        ]
        owners = {0x401000: "Toy2/Toy2.cpp", 0x401100: "Toy2/Toy2.cpp", 0x401200: "Toy2/Levels.cpp",
                  0x401400: "Toy2/Toy2.cpp", 0x401500: "Renderer.cpp"}
        annotations = [Annotation("function", hex(address), source, 1) for address, source in owners.items()]
        annotations.append(Annotation("global", "0x500000", "Toy2/Levels.cpp", 2))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Toy2").mkdir()
            (root / "Toy2" / "Toy2.cpp").write_text("x\n" * 3001, encoding="utf-8")
            (root / "Toy2" / "Levels.cpp").write_text("x\n", encoding="utf-8")
            (root / "Renderer.cpp").write_text("x\n", encoding="utf-8")
            report = decomp_evidence.structure_report(
                entries, annotations, root, core=(0x401100, 0x401400), core_file="Toy2/Toy2.cpp"
            )
        toy2 = report["files"][0]
        self.assertEqual(toy2["file"], "src/Toy2/Toy2.cpp")
        self.assertEqual((toy2["lines"], toy2["functions"], toy2["runs"]), (3001, 3, 2))
        self.assertEqual(toy2["prefixes"], ["Toy2", "Toy2::Ini"])
        # Over 3000 lines with more than one function: no single retail object
        # explains the file, so it is a split candidate on size alone.
        self.assertTrue(toy2["split"])
        self.assertFalse(report["files"][-1]["split"])
        totals = report["totals"]
        self.assertEqual(totals["files"], 3)
        self.assertEqual(totals["median_runs"], 1)
        self.assertEqual(totals["files_over_3000_lines"], ["src/Toy2/Toy2.cpp"])
        self.assertEqual((totals["core_held"], totals["core_functions"]), (2, 4))

    def test_moves_name_the_host_file_and_separate_interleaved_pairs(self):
        from pathlib import Path
        import tempfile

        from tools.decomp_annotations import Annotation

        entries = [(0x401000, "A::One"), (0x401100, "B::Two"), (0x401200, "A::Three"),
                   (0x401300, "C::Four"), (0x401400, "D::Five"), (0x401500, "C::Six"),
                   (0x401600, "D::Seven"), (0x401700, "C::Eight")]
        owners = {0x401000: "A.cpp", 0x401100: "B.cpp", 0x401200: "A.cpp",
                  0x401300: "C.cpp", 0x401400: "D.cpp", 0x401500: "C.cpp",
                  0x401600: "D.cpp", 0x401700: "C.cpp"}
        annotations = [Annotation("function", hex(address), source, 1)
                       for address, source in owners.items()]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("A.cpp", "B.cpp", "C.cpp", "D.cpp"):
                (root / name).write_text("x\n", encoding="utf-8")
            report = decomp_evidence.structure_report(entries, annotations, root)
        moves = {(move["run"]["file"], move["host"], move["kind"]) for move in report["moves"]}
        # B sits inside A's block and A holds no island of B: a clean move.
        self.assertIn(("src/B.cpp", "src/A.cpp", "move"), moves)
        # C and D each hold an island of the other, so neither move is safe.
        self.assertIn(("src/D.cpp", "src/C.cpp", "interleaved"), moves)
        self.assertIn(("src/C.cpp", "src/D.cpp", "interleaved"), moves)
        self.assertEqual(report["totals"]["island_runs"], 4)
        self.assertEqual(report["totals"]["island_functions"], 4)

    def test_a_split_candidate_outranks_a_small_file_and_lists_its_runs(self):
        from pathlib import Path
        import tempfile
        from unittest.mock import patch

        from tools.decomp_annotations import Annotation

        entries = [(0x401000, "Toy2::Ini::Parse"), (0x401100, "Toy2::ReadIniFile"),
                   (0x401200, "Toy2::Level::Start"), (0x401400, "Toy2::Ini::Close"),
                   (0x401500, "Renderer::Draw"), (0x401600, "Toy2::Level::End")]
        owners = {0x401000: "Toy2/Toy2.cpp", 0x401100: "Toy2/Toy2.cpp", 0x401200: "Toy2/Levels.cpp",
                  0x401400: "Toy2/Toy2.cpp", 0x401500: "Renderer.cpp", 0x401600: "Toy2/Levels.cpp"}
        annotations = [Annotation("function", hex(address), source, 1)
                       for address, source in owners.items()]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Toy2").mkdir()
            (root / "Toy2" / "Toy2.cpp").write_text("x\n" * 3001, encoding="utf-8")
            (root / "Toy2" / "Levels.cpp").write_text("x\n", encoding="utf-8")
            (root / "Renderer.cpp").write_text("x\n", encoding="utf-8")
            # Levels.cpp holds more runs; only the large file is a split candidate.
            with patch.object(decomp_evidence, "MIXED_FILE_RUNS", 1):
                report = decomp_evidence.structure_report(
                    entries, annotations, root, detail="src/Toy2/Toy2.cpp"
                )
        self.assertEqual([row["file"] for row in report["files"]][:2],
                         ["src/Toy2/Toy2.cpp", "src/Toy2/Levels.cpp"])
        self.assertEqual([row["split"] for row in report["files"]], [True, False, False])
        self.assertEqual(report["detail"]["file"], "src/Toy2/Toy2.cpp")
        self.assertEqual(
            [(run["start"], run["end"], run["functions"], run["before"], run["after"])
             for run in report["detail"]["runs"]],
            [(0x401000, 0x401100, 2, None, "src/Toy2/Levels.cpp"),
             (0x401400, 0x401400, 1, "src/Toy2/Levels.cpp", "src/Renderer.cpp")],
        )


class PlacementForAnnotatedTargetTests(unittest.TestCase):
    def test_a_placement_that_differs_from_the_annotation_is_reported(self):
        # both neighbours live in one file while the target is annotated in another
        functions = {0x401000: ("FUNCTION", "B/Two.cpp", 0), 0x402000: ("FUNCTION", "A/One.cpp", 0),
                     0x403000: ("FUNCTION", "B/Two.cpp", 0)}
        line = decomp_evidence.placement_line(
            0x402000, [0x401000, 0x402000, 0x403000], functions, ignore_own=True
        )
        self.assertEqual(line, "placement: src/B/Two.cpp (both neighbours)")


if __name__ == "__main__":
    unittest.main()
